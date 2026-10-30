/* SDK-verified size fixtures and independent sparse row offsets. */
#include "nvn_deswizzle.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const unsigned w[] = {1,19,128,127,256,64,128,129,1920,1920};
    const unsigned h[] = {1,16,127,128,64,256,128,129,1080,64};
    const uint64_t sizes[2][10] = {
        {512,2048,65536,65536,65536,65536,65536,262144,8847360,491520},
        {512,3072,131072,131072,131072,131072,131072,393216,16711680,983040}
    };
    nvn_texdesc d = {.target=1,.format=0x3d,.flags=0x28,.depth=1,.levels=1};
    nvn_texlayout L;
    for (unsigned f=0; f<2; ++f) {
        d.format = f ? 0x36 : 0x3d;
        for (unsigned k=0; k<10; ++k) {
            d.width=w[k]; d.height=h[k]; nvn_tex_layout(&d,&L);
            assert(L.size==sizes[f][k]);
            assert(L.align==65536);
        }
    }
    d.format=0x3d; d.width=d.height=129; nvn_tex_layout(&d,&L);
    unsigned char *storage=calloc(1,L.size), *lin=malloc(129*129*4), *out=malloc(129*129*4);
    assert(storage && lin && out);
    for (unsigned i=0; i<129*129*4; ++i) lin[i]=(unsigned char)(i*11+3);
    assert(!nvn_swizzle(&d,0,0,lin,129*4,storage));
    assert(!memcmp(storage+131072,lin+128*129*4,16));
    assert(!nvn_deswizzle(&d,0,0,storage,out,129*4));
    assert(!memcmp(lin,out,129*129*4));
    free(storage);free(lin);free(out);
    d.format=0x4d;d.flags=0x20;d.width=d.height=257;d.pkg_block_h=4;d.pkg_block_w=5;
    nvn_tex_layout(&d,&L);assert(L.size==262144 && L.tileW==4 && L.tileH==3);
    d.width=1920;d.height=1080;nvn_tex_layout(&d,&L);assert(L.size==2621440);
    d.width=d.height=255;nvn_tex_layout(&d,&L);assert(L.size==65536);
    d.format=0x3d;d.pkg_block_h=d.pkg_block_w=0;
    d.levels=2;
    nvn_tex_layout(&d,&L); assert(!L.size);
    unsigned char untouched=123;
    assert(nvn_deswizzle(&d,0,0,&untouched,&untouched,129*4)==-1 && untouched==123);
    d.levels=1; d.depth=2;
    nvn_tex_layout(&d,&L); assert(!L.size);
    d.depth=1; d.target=2;
    nvn_tex_layout(&d,&L); assert(!L.size);
    puts("PASS: SDK sparse sizes, 64KiB alignment, independent second-row offset and roundtrip");
}
