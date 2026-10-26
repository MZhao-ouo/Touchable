/* Extra NVN entry points used by Unity. Included by hle_nvn.c. */
#include "nvn_deswizzle.h"
static void gfx_enter(void);
static void gfx_leave(void);
NVN(nvnColorStateSetLogicOp, void, NVNcolorState *state, NVNlogicOp operation) {
    if (operation != NVN_LOGIC_OP_COPY) die("Color logic operation needs a shader variant: %d", operation);
    ((color_st *)state)->logic_op = operation;
}
NVN(nvnColorStateSetAlphaTest, void, NVNcolorState *state, NVNalphaFunc function) {
    if (function != NVN_ALPHA_FUNC_ALWAYS) die("Fixed alpha test needs a shader variant: %d", function);
    ((color_st *)state)->alpha_func = function;
}
void ss_mtl_write_texels(const tex_t *, unsigned, unsigned, const NVNcopyRegion *, const void *, size_t, size_t);
static void ss_write_texels(const tex_t *texture, const view_t *view, const NVNcopyRegion *region,
                            const void *source, ptrdiff_t row_stride, ptrdiff_t image_stride) {
    unsigned level = view && view->set_levels ? view->base_level : 0;
    unsigned layer = view && view->set_layers ? view->min_layer : 0;
    int bw, bh, bpb = nvn_fmt_bpb(texture->d.format, &bw, &bh);
    if (!source || !texture->mem || !bpb || level >= texture->d.levels ||
        region->xoffset < 0 || region->yoffset < 0 || region->zoffset < 0 ||
        region->width < 0 || region->height < 0 || region->depth < 0)
        die("Invalid texture CPU write");
    if (region->xoffset % bw || region->yoffset % bh) die("Unaligned compressed texture CPU write");
    unsigned width = texture->d.width >> level, height = texture->d.height >> level;
    if (!width) width = 1;
    if (!height) height = 1;
    if ((uint64_t)region->xoffset + region->width > width ||
        (uint64_t)region->yoffset + region->height > height) die("Texture CPU write exceeds extent");
    size_t bytes = ((region->width + bw - 1) / bw) * bpb;
    unsigned count = (region->height + bh - 1) / bh;
    if (!row_stride) row_stride = bytes;
    if (!image_stride) image_stride = row_stride * count;
    if (row_stride < (ptrdiff_t)bytes || image_stride < row_stride * count) die("Invalid texture CPU write stride");
    dm_guest_write_prepare(texture->mem, texture->L.size);
    for (int z = 0; z < region->depth; ++z) {
        unsigned depth = texture->d.target == 2 ? region->zoffset + z : 0;
        unsigned slice = texture->d.target == 2 ? layer : layer + region->zoffset + z;
        uint32_t row_bytes, rows, slices, block_bytes; uint64_t base; unsigned tile_h, tile_d;
        if (nvn_deswizzle_geom(&texture->d, level, slice, &row_bytes, &rows, &slices, &base, &tile_h, &tile_d, &block_bytes))
            die("Unsupported texture CPU write layout: target=%d flags=%#x", texture->d.target, texture->d.flags);
        if (depth >= slices) die("Texture CPU write exceeds depth");
        for (unsigned y = 0; y < count; ++y) {
            const uint8_t *input = (const uint8_t *)source + z * image_stride + y * row_stride;
            unsigned dx = region->xoffset / bw * bpb, dy = region->yoffset / bh + y;
            for (size_t x = 0; x < bytes;) {
                size_t step = 16 - ((dx + x) & 15);
                if (step > bytes - x) step = bytes - x;
                uint64_t offset = base + nvn_bl_offset(dx + x, dy, depth, row_bytes, rows, tile_h, tile_d);
                if (offset > texture->L.size || step > texture->L.size - offset) die("Texture CPU write exceeds storage");
                memcpy(texture->mem + offset, input + x, step);
                x += step;
            }
        }
    }
    if (!texture->has_layout && region->width && region->height && region->depth) {
        x_drain();   /* translate the submits queued before this write first */
        gfx_enter();
        ss_mtl_write_texels(texture, level, layer, region, source, row_stride, image_stride);
        gfx_leave();
    }
}
NVN(nvnTextureWriteTexels, void, const NVNtexture *texture, const NVNtextureView *view, const NVNcopyRegion *region, const void *source) {
    ss_write_texels(CTX(texture), (const view_t *)view, region, source, 0, 0);
}
NVN(nvnTextureWriteTexelsStrided, void, const NVNtexture *texture, const NVNtextureView *view, const NVNcopyRegion *region, const void *source, ptrdiff_t row, ptrdiff_t image) {
    ss_write_texels(CTX(texture), (const view_t *)view, region, source, row, image);
}
NVN(nvnTextureFlushTexels, void, const NVNtexture *texture, const NVNtextureView *view, const NVNcopyRegion *region) {
    (void)texture; (void)view; (void)region;
    atomic_thread_fence(memory_order_seq_cst);
}
NVN(nvnQueueWaitSync, NVNboolean, NVNqueue *queue, const NVNsync *sync) {
    (void)queue;
    const sync_t *s = (const void *)sync;
    /* NVN waits on the GPU and returns at once. All host queues and presentation
       share one in-order Metal queue, and every fence value has already been
       committed, so later work runs after it without a CPU wait. Waiting here
       made the CPU idle until the GPU finished each frame.
       SS_QUEUE_WAIT_CPU=1 restores the CPU wait for comparison. */
    static int cpu_wait = -1;
    if (cpu_wait < 0) { const char *e = getenv("SS_QUEUE_WAIT_CPU"); cpu_wait = e && !strcmp(e, "1"); }
    if (cpu_wait) return s->fenced && mtl_wait(s->fence, UINT64_MAX);
    return s->fenced;
}
/* Metal uses one-pixel lines. A point shader can override its default size. */
NVN(nvnCommandBufferSetPointSize, void, NVNcommandBuffer *command, float size) {
    (void)command;
    if (size != 1.0f) die("Non-default point size needs a shader variant: %g", size);
}
NVN(nvnCommandBufferSetLineWidth, void, NVNcommandBuffer *command, float width) {
    (void)command;
    if (width != 1.0f) die("Wide lines need geometry expansion: %g", width);
}
NVN(nvnCommandBufferSetDepthBounds, void, NVNcommandBuffer *command, uint32_t enable, float minimum, float maximum) {
    (void)command; (void)minimum; (void)maximum;
    if (B(enable)) die("Enabled depth bounds need a fragment shader variant");
}
NVN(nvnCommandBufferSetRasterizerDiscard, void, NVNcommandBuffer *command, uint32_t enable) {
    (void)command;
    if (B(enable)) die("Rasterizer discard needs a pipeline variant");
}
NVN(nvnCommandBufferSetConservativeRasterEnable, void, NVNcommandBuffer *command, uint32_t enable) {
    (void)command;
    if (B(enable)) die("Conservative rasterization needs a pipeline variant");
}
NVN(nvnCommandBufferSaveZCullData, void, NVNcommandBuffer *command, NVNbufferAddress address, size_t size) {
    (void)command; (void)address;
    if (size) die("ZCull storage must be zero for the Metal backend");
}
NVN(nvnCommandBufferRestoreZCullData, void, NVNcommandBuffer *command, NVNbufferAddress address, size_t size) {
    (void)command; (void)address;
    if (size) die("ZCull storage must be zero for the Metal backend");
}
NVN(nvnMemoryPoolGetFlags, NVNmemoryPoolFlags, const NVNmemoryPool *pool) { return ((const pool_t *)pool)->flags; }
NVN(nvnMemoryPoolGetSize, size_t, const NVNmemoryPool *pool) { return ((const pool_t *)pool)->size; }
NVN(nvnMemoryPoolMap, void *, const NVNmemoryPool *pool) { return ((const pool_t *)pool)->mem; }
NVN(nvnMemoryPoolInvalidateMappedRange, void, const NVNmemoryPool *pool, ptrdiff_t offset, size_t size) {
    const pool_t *p = (const void *)pool;
    if (offset < 0 || (uint64_t)offset > p->size || size > p->size - offset) die("Pool invalidate range is invalid");
    atomic_thread_fence(memory_order_seq_cst);
}
NVN(nvnTextureBuilderSetSize2D, void, NVNtextureBuilder *builder, int width, int height) {
    TB(builder)->d.width = width; TB(builder)->d.height = height; TB(builder)->d.depth = 1;
}
NVN(nvnTextureBuilderSetSize3D, void, NVNtextureBuilder *builder, int width, int height, int depth) {
    TB(builder)->d.width = width; TB(builder)->d.height = height; TB(builder)->d.depth = depth;
}
NVN(nvnTextureBuilderSetSwizzle, void, NVNtextureBuilder *builder, NVNtextureSwizzle r, NVNtextureSwizzle g, NVNtextureSwizzle b, NVNtextureSwizzle a) {
    if ((unsigned)r > 5 || (unsigned)g > 5 || (unsigned)b > 5 || (unsigned)a > 5) die("Invalid texture swizzle");
    memcpy(TB(builder)->d.swizzle, (uint8_t[]){r,g,b,a}, 4);
    TB(builder)->d.swizzle_set = 1;
}
NVN(nvnTextureBuilderGetZCullStorageSize, size_t, const NVNtextureBuilder *builder) { (void)builder; return 0; }
NVN(nvnTextureGetZCullStorageSize, size_t, const NVNtexture *texture) { (void)texture; return 0; }
NVN(nvnTextureGetTarget, NVNtextureTarget, const NVNtexture *texture) { return CTX(texture)->d.target; }
NVN(nvnTextureGetFormat, NVNformat, const NVNtexture *texture) { return CTX(texture)->d.format; }
NVN(nvnTextureGetLevels, int, const NVNtexture *texture) { return CTX(texture)->d.levels; }
NVN(nvnTextureGetSamples, int, const NVNtexture *texture) { return CTX(texture)->d.samples; }
NVN(nvnTextureGetStride, ptrdiff_t, const NVNtexture *texture) { return CTX(texture)->d.stride; }
NVN(nvnTextureGetMemoryOffset, ptrdiff_t, const NVNtexture *texture) { return CTX(texture)->off; }
/* The imported prototype incorrectly declares this pointer result by value. */
NVN_NOPROTO(nvnTextureGetMemoryPool, NVNmemoryPool *, const NVNtexture *texture) { return (NVNmemoryPool *)CTX(texture)->pool; }
NVN(nvnTextureBuilderSetDepthStencilMode, void, NVNtextureBuilder *builder, NVNtextureDepthStencilMode mode) {
    (void)builder;
    if (mode != NVN_TEXTURE_DEPTH_STENCIL_MODE_DEPTH) die("Stencil texture sampling needs an implementation");
}
NVN_NOPROTO(nvnWindowBuilderSetNumActiveTextures, void, NVNwindowBuilder *builder, int count) {
    win_builder_t *b = (void *)builder;
    if (count != b->ntex) die("Active swapchain texture subset is not supported: %d of %d", count, b->ntex);
}
NVN(nvnWindowBuilderGetPresentInterval, int, const NVNwindowBuilder *builder) { return ((const win_builder_t *)builder)->interval; }
NVN(nvnWindowBuilderGetNativeWindow, NVNnativeWindow, const NVNwindowBuilder *builder) { return ((const win_builder_t *)builder)->native; }
NVN(nvnWindowGetPresentInterval, int, const NVNwindow *window) { return ((const win_t *)window)->interval; }
NVN(nvnWindowGetNativeWindow, NVNnativeWindow, const NVNwindow *window) { return ((const win_t *)window)->native; }
NVN(nvnWindowGetCrop, void, const NVNwindow *window, NVNrectangle *crop) { memcpy(crop, ((const win_t *)window)->crop, 16); }
NVN_NOPROTO(nvnWindowGetNumTextures, int, const NVNwindow *window) { return ((const win_t *)window)->ntex; }
NVN_NOPROTO(nvnWindowGetNumActiveTextures, int, const NVNwindow *window) { return ((const win_t *)window)->ntex; }
typedef struct { NVNdevice *device; pool_t *pool; ptrdiff_t offset; size_t size; } ss_buffer_builder;
typedef struct { pool_t *pool; ptrdiff_t offset; size_t size; uint64_t serial; } ss_buffer;
FITS(ss_buffer_builder, NVNbufferBuilder);
FITS(ss_buffer, NVNbuffer);
NVN(nvnBufferBuilderSetDefaults, void, NVNbufferBuilder *builder) {
    ss_buffer_builder *b = (void *)builder;
    b->pool = NULL; b->offset = 0; b->size = 0;
}
NVN(nvnBufferBuilderSetDevice, void, NVNbufferBuilder *builder, NVNdevice *device) { ((ss_buffer_builder *)builder)->device = device; }
NVN(nvnBufferBuilderSetStorage, void, NVNbufferBuilder *builder, NVNmemoryPool *pool, ptrdiff_t offset, size_t size) {
    ss_buffer_builder *b = (void *)builder; b->pool = (pool_t *)pool; b->offset = offset; b->size = size;
}
NVN(nvnBufferInitialize, NVNboolean, NVNbuffer *buffer, const NVNbufferBuilder *builder) {
    const ss_buffer_builder *b = (const void *)builder;
    if (!b->pool || b->offset < 0 || (uint64_t)b->offset > b->pool->size || b->size > b->pool->size - b->offset) return 0;
    *(ss_buffer *)buffer = (ss_buffer){b->pool, b->offset, b->size, nvn_new_serial()};
    return 1;
}
NVN(nvnBufferFinalize, void, NVNbuffer *buffer) { memset(buffer, 0, sizeof(ss_buffer)); }
NVN(nvnBufferMap, void *, const NVNbuffer *buffer) { const ss_buffer *b = (const void *)buffer; return b->pool->mem + b->offset; }
NVN(nvnBufferGetAddress, NVNbufferAddress, const NVNbuffer *buffer) { return (uintptr_t)h_nvnBufferMap(buffer); }
NVN(nvnBufferGetMemoryPool, NVNmemoryPool *, const NVNbuffer *buffer) { return (NVNmemoryPool *)((const ss_buffer *)buffer)->pool; }
NVN(nvnBufferGetMemoryOffset, ptrdiff_t, const NVNbuffer *buffer) { return ((const ss_buffer *)buffer)->offset; }
NVN(nvnBufferGetSize, size_t, const NVNbuffer *buffer) { return ((const ss_buffer *)buffer)->size; }
NVN(nvnBufferGetDebugID, uint64_t, const NVNbuffer *buffer) { return ((const ss_buffer *)buffer)->serial; }
NVN(nvnBufferFlushMappedRange, void, const NVNbuffer *buffer, ptrdiff_t offset, size_t size) {
    const ss_buffer *b = (const void *)buffer;
    if (offset < 0 || (uint64_t)offset > b->size || size > b->size - offset) die("Buffer flush range is invalid");
    h_nvnMemoryPoolFlushMappedRange((const NVNmemoryPool *)b->pool, b->offset + offset, size);
}
NVN(nvnBufferInvalidateMappedRange, void, const NVNbuffer *buffer, ptrdiff_t offset, size_t size) {
    const ss_buffer *b = (const void *)buffer;
    if (offset < 0 || (uint64_t)offset > b->size || size > b->size - offset) die("Buffer invalidate range is invalid");
    /* Shared Metal buffers are coherent after the queue fence completes. */
    atomic_thread_fence(memory_order_seq_cst);
}
/* Min and max reduction filters need a shader variant. Average is the Metal default. */
NVN(nvnSamplerBuilderSetReductionFilter, void, NVNsamplerBuilder *builder, NVNsamplerReduction filter) {
    (void)builder;
    if (filter != NVN_SAMPLER_REDUCTION_AVERAGE) die("Sampler reduction filter %d is not implemented", filter);
}
/* nvnProgramSetShadersExt is newer than the bundled nvn.h. Each stage record is 0x18 bytes:
 * NVNshaderData followed by one extension word, which nn::gfx leaves zero. */
typedef struct { NVNbufferAddress data; const void *control; uint64_t extension; } ss_shader_data_ext;
static NVNboolean h_nvnProgramSetShaders(NVNprogram *p, int n, const NVNshaderData *sd);
NVN_NOPROTO(nvnProgramSetShadersExt, NVNboolean, NVNprogram *program, int count, const ss_shader_data_ext *stages) {
    if (count < 0 || count > 6) return 0;
    NVNshaderData data[6];
    for (int i = 0; i < count; ++i) {
        if (stages[i].extension) { static int said; if (!said++) LOG("[nvn] SetShadersExt extension word %#llx is ignored", (unsigned long long)stages[i].extension); }
        data[i] = (NVNshaderData){stages[i].data, stages[i].control};
    }
    return h_nvnProgramSetShaders(program, count, data);
}
/* Combined texture handles use the separate-handle bit layout: texture in bits 0-19, sampler above. */
NVN(nvnDeviceGetTextureHandle, NVNtextureHandle, const NVNdevice *device, int texture, int sampler) {
    (void)device;
    if (texture < 0 || texture >= (1 << 20) || sampler < 0) die("Invalid texture handle request: %d, %d", texture, sampler);
    return (NVNtextureHandle)texture | (NVNtextureHandle)sampler << 20;
}
NVN(nvnDeviceGetTexelFetchHandle, NVNtextureHandle, const NVNdevice *device, int texture) {
    (void)device;
    if (texture < 0 || texture >= (1 << 20)) die("Invalid texel fetch handle request: %d", texture);
    return (NVNtextureHandle)texture;
}
NVN(nvnTextureViewSetSwizzle, void, NVNtextureView *view, NVNtextureSwizzle r, NVNtextureSwizzle g, NVNtextureSwizzle b, NVNtextureSwizzle a) {
    if ((unsigned)r > 5 || (unsigned)g > 5 || (unsigned)b > 5 || (unsigned)a > 5) die("Invalid texture view swizzle");
    VW(view)->set_swizzle = 1;
    VW(view)->swizzle = (uint16_t)(r | g << 4 | b << 8 | a << 12);
}
NVN(nvnTextureViewGetSwizzle, NVNboolean, const NVNtextureView *view, NVNtextureSwizzle *r, NVNtextureSwizzle *g, NVNtextureSwizzle *b, NVNtextureSwizzle *a) {
    const view_t *o = (const view_t *)view;
    *r = o->set_swizzle ? VIEW_SWIZZLE(o, 0) : NVN_TEXTURE_SWIZZLE_R; *g = o->set_swizzle ? VIEW_SWIZZLE(o, 1) : NVN_TEXTURE_SWIZZLE_G;
    *b = o->set_swizzle ? VIEW_SWIZZLE(o, 2) : NVN_TEXTURE_SWIZZLE_B; *a = o->set_swizzle ? VIEW_SWIZZLE(o, 3) : NVN_TEXTURE_SWIZZLE_A;
    return o->set_swizzle;
}
NVN(nvnTextureViewSetDepthStencilMode, void, NVNtextureView *view, NVNtextureDepthStencilMode mode) {
    (void)view;
    if (mode != NVN_TEXTURE_DEPTH_STENCIL_MODE_DEPTH) die("Stencil texture view sampling needs an implementation");
}
NVN(nvnTextureViewGetLevels, NVNboolean, const NVNtextureView *view, int *base, int *count) {
    const view_t *o = (const view_t *)view; *base = o->base_level; *count = o->nlevels; return o->set_levels;
}
NVN(nvnTextureViewGetTarget, NVNboolean, const NVNtextureView *view, NVNtextureTarget *target) {
    const view_t *o = (const view_t *)view; *target = (NVNtextureTarget)o->target; return o->set_target;
}
NVN(nvnTextureViewGetFormat, NVNboolean, const NVNtextureView *view, NVNformat *format) {
    const view_t *o = (const view_t *)view; *format = (NVNformat)o->format; return o->set_format;
}
