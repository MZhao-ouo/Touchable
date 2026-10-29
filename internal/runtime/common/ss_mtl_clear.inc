/* Metal replay support for masked and scissored NVN clears. */
static void ss_clear_attachment(const p_clear_color *color, const p_clear_ds *depth) {
    const tex_t *target = color ? R.rt.color[color->index] : R.rt.depth;
    if (!target) return;
    const view_t *view = color ? &R.rt.cview[color->index] : &R.rt.dview;
    int has_view = color ? R.rt.has_cview[color->index] : R.rt.has_dview;
    unsigned level = has_view && view->set_levels ? view->base_level : 0;
    NSUInteger w = mip_extent(target->d.width, level), h = mip_extent(target->d.height, level);
    int64_t x0 = 0, y0 = 0, x1 = w, y1 = h;
    if (R.has_sc) {
        x0 = MAX(0, R.sc.x); y0 = MAX(0, R.sc.y);
        x1 = MIN((int64_t)w, (int64_t)R.sc.x + R.sc.w);
        y1 = MIN((int64_t)h, (int64_t)R.sc.y + R.sc.h);
    }
    if (x1 <= x0 || y1 <= y0 || (color && !(color->mask & 15)) ||
        (depth && !depth->depth_mask && !depth->stencil_mask)) return;
    rp_flush();
    if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    id<MTLTexture> texture = tex_mtl(target);
    if (!texture) MDIE("Clear target has no Metal texture");
    static id<MTLLibrary> library;
    static NSMutableDictionary<NSString *, id<MTLRenderPipelineState>> *pipelines;
    static NSMutableDictionary<NSString *, id<MTLDepthStencilState>> *states;
    if (!library) {
        NSString *source = @"#include <metal_stdlib>\nusing namespace metal;\n"
            "struct C { float4 color; float depth; };\n"
            "vertex float4 ss_clear_vs(uint i [[vertex_id]], constant C &c [[buffer(0)]]) {"
            "float2 p = float2((i << 1) & 2, i & 2) * 2.0 - 1.0; return float4(p, c.depth, 1); }\n"
            "fragment float4 ss_clear_fs(constant C &c [[buffer(0)]]) { return c.color; }\n";
        NSError *error = nil;
        library = [g_dev newLibraryWithSource:source options:nvn_msl_options() error:&error];
        if (!library) MDIE("Clear shader: %s", error.localizedDescription.UTF8String);
        pipelines = [NSMutableDictionary dictionary]; states = [NSMutableDictionary dictionary];
    }
    BOOL has_depth = depth && texture.pixelFormat != MTLPixelFormatStencil8;
    BOOL has_stencil = depth && (texture.pixelFormat == MTLPixelFormatDepth32Float_Stencil8 ||
                                texture.pixelFormat == MTLPixelFormatStencil8);
    NSString *key = [NSString stringWithFormat:@"%lu/%lu/%d/%d", (unsigned long)texture.pixelFormat,
                     (unsigned long)texture.sampleCount, color != NULL, color ? color->mask & 15 : 0];
    id<MTLRenderPipelineState> pipeline = pipelines[key];
    if (!pipeline) {
        MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = [library newFunctionWithName:@"ss_clear_vs"];
        descriptor.rasterSampleCount = texture.sampleCount;
        if (color) {
            descriptor.fragmentFunction = [library newFunctionWithName:@"ss_clear_fs"];
            descriptor.colorAttachments[0].pixelFormat = texture.pixelFormat;
            int mask = color->mask;
            descriptor.colorAttachments[0].writeMask =
                (mask & 1 ? MTLColorWriteMaskRed : 0) | (mask & 2 ? MTLColorWriteMaskGreen : 0) |
                (mask & 4 ? MTLColorWriteMaskBlue : 0) | (mask & 8 ? MTLColorWriteMaskAlpha : 0);
        }
        if (has_depth) descriptor.depthAttachmentPixelFormat = texture.pixelFormat;
        if (has_stencil) descriptor.stencilAttachmentPixelFormat = texture.pixelFormat;
        NSError *error = nil;
        pipeline = [g_dev newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (!pipeline) MDIE("Clear pipeline: %s", error.localizedDescription.UTF8String);
        pipelines[key] = pipeline;
    }
    NSString *state_key = [NSString stringWithFormat:@"%d/%u", has_depth && depth->depth_mask,
                           has_stencil ? (unsigned)depth->stencil_mask & 255 : 0];
    id<MTLDepthStencilState> state = states[state_key];
    if (!state) {
        MTLDepthStencilDescriptor *descriptor = [MTLDepthStencilDescriptor new];
        descriptor.depthCompareFunction = MTLCompareFunctionAlways;
        descriptor.depthWriteEnabled = has_depth && depth->depth_mask;
        if (has_stencil && depth->stencil_mask) {
            MTLStencilDescriptor *stencil = [MTLStencilDescriptor new];
            stencil.stencilCompareFunction = MTLCompareFunctionAlways;
            stencil.depthStencilPassOperation = MTLStencilOperationReplace;
            stencil.writeMask = depth->stencil_mask & 255;
            descriptor.frontFaceStencil = descriptor.backFaceStencil = stencil;
        }
        state = [g_dev newDepthStencilStateWithDescriptor:descriptor];
        states[state_key] = state;
    }
    MTL4RenderPassDescriptor *pass = [MTL4RenderPassDescriptor new];
    MTLRenderPassAttachmentDescriptor *attachment = color ? pass.colorAttachments[0] :
        has_depth ? pass.depthAttachment : pass.stencilAttachment;
    attach(attachment, target, view, has_view, 0);   /* the clear pipeline uses the base format */
    attachment.loadAction = MTLLoadActionLoad; attachment.storeAction = MTLStoreActionStore;
    if (has_depth && has_stencil) {
        attach(pass.stencilAttachment, target, view, has_view, 0);
        pass.stencilAttachment.loadAction = MTLLoadActionLoad;
        pass.stencilAttachment.storeAction = MTLStoreActionStore;
    }
    struct { float rgba[4], depth, pad[3]; } constants = {{0}, depth ? depth->depth : 0, {0}};
    if (color) memcpy(constants.rgba, color->rgba, 16);
    id<MTLBuffer> buffer; size_t offset = arena_alloc(sizeof(constants), &buffer);
    memcpy((uint8_t *)buffer.contents + offset, &constants, sizeof(constants));
    ser_gate();
    id<MTL4RenderCommandEncoder> encoder = enc_r_begin(pass);
    enc_r_pso(encoder, pipeline); enc_r_dss(encoder, state);
    enc_r_viewport(encoder, (MTLViewport){0, 0, w, h, 0, 1});
    enc_r_scissor(encoder, (MTLScissorRect){x0, y0, x1 - x0, y1 - y0});
    enc_r_cull(encoder, MTLCullModeNone);
    enc_r_args(encoder, MTLRenderStageVertex, buffer.gpuAddress + offset, 0);
    if (color) enc_r_args(encoder, MTLRenderStageFragment, buffer.gpuAddress + offset, 0);
    if (has_stencil) enc_r_stencil_ref(encoder, depth->stencil, depth->stencil);
    enc_r_draw(encoder, MTLPrimitiveTypeTriangle, 0, 3);
    enc_r_end(encoder); args_dirty_all(); zskip_del(target->serial);
    PD("SS clear ser=%llu color=%d rect=%lld,%lld %lldx%lld", target->serial, color != NULL,
       x0, y0, x1-x0, y1-y0);
}
