/* Synthetic DSP ADPCM vectors; no game data. Include the mixer to verify source samples before resampling. */
#include <assert.h>
#include <stdio.h>
#include "../../runtime/common/ss_audren.c"
static int sample(ss_ar_voice *v) { fetch(v); return (int)(v->history[0][3] * 32768); }
int main(void) {
    ss_ar_renderer *r = ss_ar_create(48000, 240, 2, 0, 2, 0);
    ss_ar_voice *v = ss_ar_acquire_voice(r, 48000, 1, SS_AR_FORMAT_ADPCM);
    assert(v);
    assert(!ss_ar_acquire_voice(r, 48000, 2, SS_AR_FORMAT_ADPCM));
    uint8_t data[16] = {0x01,0x17,0x8f,0x20,0x00,0x00,0x00,0x00, 0x00,0x12,0x34,0x56,0x70,0x00,0x00,0x00};
    ss_ar_wavebuffer b = {.data=(const int16_t *)data,.size=sizeof(data),.start=0,.end=28,.tag=&b};
    assert(ss_ar_append(v,&b)); ss_ar_update(r);
    int expected[28] = {2,14,-16,-2,4,0,0,0,0,0,0,0,0,0,1,2,3,4,5,6,7,0,0,0,0,0,0,0};
    for (int i=0;i<28;++i) assert(sample(v)==expected[i]);
    assert(ss_ar_released_buffer(v)==&b); assert(sample(v)==0);
    /* Predictor uses both histories, honors context and restores it on every loop. */
    int16_t coefficients[16] = {2048,1024}; ss_ar_voice_adpcm_parameters(v,coefficients);
    int16_t context[3] = {0,100,20};
    uint8_t zero[8] = {0};
    b=(ss_ar_wavebuffer){.data=(const int16_t *)zero,.size=8,.start=3,.end=5,.loop=1,.context=context,.context_size=6};
    assert(ss_ar_append(v,&b)); ss_ar_update(r);
    for(int i=0;i<20;++i) { assert(sample(v)==110); assert(sample(v)==160); }
    voice_reset(v); assert(v->adpcm_history[0]==0 && v->adpcm_history[1]==0);
    /* Decoder history continues across buffers; signed saturation is bounded. */
    coefficients[0]=2048; coefficients[1]=0; ss_ar_voice_adpcm_parameters(v,coefficients);
    uint8_t loud[8]={0x0f,0x78,0,0,0,0,0,0};
    b=(ss_ar_wavebuffer){.data=(const int16_t *)loud,.size=8,.end=1};
    assert(ss_ar_append(v,&b)); ss_ar_update(r); assert(sample(v)==32767);
    b.start=1;b.end=2;assert(ss_ar_append(v,&b));ss_ar_update(r);assert(sample(v)==-32768);
    /* Truncated encoded frames must never be read past the supplied byte count. */
    b.size=7;b.start=0;b.end=14;assert(ss_ar_append(v,&b));ss_ar_update(r);assert(sample(v)==0);
    ss_ar_destroy(r);
    puts("test_audren_adpcm: vectors, frame boundaries, context, loops, stop, saturation and truncation pass");
}
