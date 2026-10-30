#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(int argc, char **argv) { @autoreleasepool {
    assert(argc==2);
    id<MTLDevice> dev=MTLCreateSystemDefaultDevice(); assert(dev);
    NSError *error=nil;
    NSString *source=[NSString stringWithContentsOfFile:@(argv[1]) encoding:NSUTF8StringEncoding error:&error]; assert(source);
    id<MTLLibrary> lib=[dev newLibraryWithSource:source options:nil error:&error];
    if (!lib) { fprintf(stderr,"%s\n",error.description.UTF8String); return 1; }
    MTLRenderPipelineDescriptor *pd=[MTLRenderPipelineDescriptor new];
    pd.vertexFunction=[lib newFunctionWithName:@"vs"];
    pd.fragmentFunction=[lib newFunctionWithName:@"main0"];
    pd.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
    id<MTLRenderPipelineState> ps=[dev newRenderPipelineStateWithDescriptor:pd error:&error]; assert(ps);
    MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:8 height:6 mipmapped:NO];
    td.storageMode=MTLStorageModePrivate;td.usage=MTLTextureUsageRenderTarget;
    id<MTLTexture> tex=[dev newTextureWithDescriptor:td]; assert(tex);
    id<MTLCommandQueue> q=[dev newCommandQueue];id<MTLCommandBuffer> cb=[q commandBuffer];
    MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor new];pass.colorAttachments[0].texture=tex;
    pass.colorAttachments[0].loadAction=MTLLoadActionClear;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
    id<MTLRenderCommandEncoder> en=[cb renderCommandEncoderWithDescriptor:pass];[en setRenderPipelineState:ps];
    const float alphas[]={0,.5,0,.5,NAN,.5}, refs[]={0,0,.5,.5,0,NAN};
    const unsigned char expected[6][8]={{0,0,1,1,0,0,1,1},{0,0,0,0,1,1,1,1},{0,1,0,1,0,1,0,1},{0,0,1,1,0,0,1,1},{0,0,0,0,0,1,0,1},{0,0,0,0,0,1,0,1}};
    for (int row=0;row<6;++row) for (int op=1;op<=8;++op) {
        float param[2]={(float)op,refs[row]};
        [en setViewport:(MTLViewport){op-1,row,1,1,0,1}];
        [en setFragmentBytes:&alphas[row] length:4 atIndex:0];
        [en setFragmentBytes:param length:8 atIndex:30];
        [en drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    }
    [en endEncoding];
    id<MTLBuffer> pixels=[dev newBufferWithLength:256*6 options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> blit=[cb blitCommandEncoder];
    [blit copyFromTexture:tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(8,6,1) toBuffer:pixels destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:256*6];
    [blit endEncoding];[cb commit];[cb waitUntilCompleted];assert(cb.status==MTLCommandBufferStatusCompleted);
    unsigned char *p=pixels.contents;
    for(int row=0;row<6;++row) for(int col=0;col<8;++col) {
        unsigned got=p[row*256+col*4];
        if(got!=(expected[row][col]?255:0)){fprintf(stderr,"row%d op%d got%u\n",row,col+1,got);return 1;}
    }
    puts("PASS: 48 actual GPU pixels, eight alpha operations, changing reference and NaN comparisons");
    return 0;
}}
