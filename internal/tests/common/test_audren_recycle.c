#define main existing_mixer_tests
#include "test_audren.c"
#undef main
int main(void) {
    ss_ar_mix *final; ss_ar_renderer *r=stereo_renderer(&final,0,0);
    ss_ar_voice *v=ss_ar_acquire_voice(r,48000,1,SS_AR_FORMAT_PCM16);
    ss_ar_voice_destination(v,final); ss_ar_voice_volume(v,final,1,0,0);
    int16_t data[FRAME]; for(int i=0;i<FRAME;++i)data[i]=1000;
    ss_ar_wavebuffer buffers[4];
    for(int i=0;i<4;++i) { buffers[i]=(ss_ar_wavebuffer){data,sizeof(data),0,FRAME,0,0,&buffers[i]}; assert(ss_ar_append(v,&buffers[i])); }
    ss_ar_voice_state(v,SS_AR_STARTED); assert(ss_ar_voice_play_state(v)==SS_AR_STARTED);
    ss_ar_update(r);
    int16_t out[FRAME*2];
    for(int i=0;i<100;++i) {
        ss_ar_render(r,out); ss_ar_update(r);
        const void *returned=ss_ar_released_buffer(v);
        assert(returned==&buffers[i%4]);
        assert(!ss_ar_released_buffer(v));
        assert(out[FRAME]>0);
        assert(ss_ar_append(v,returned)); ss_ar_update(r);
    }
    ss_ar_voice_state(v,SS_AR_STOPPED); ss_ar_update(r);
    for(int i=0;i<4;++i) assert(ss_ar_released_buffer(v)==&buffers[i]);
    assert(!ss_ar_released_buffer(v));
    assert(ss_ar_voice_play_state(v)==SS_AR_STOPPED);
    assert(ss_ar_append(v,&buffers[0]));
    ss_ar_voice_state(v,SS_AR_STARTED); ss_ar_update(r);
    assert(ss_ar_append(v,&buffers[1]));
    ss_ar_voice_state(v,SS_AR_STOPPED); ss_ar_update(r);
    assert(ss_ar_released_buffer(v)==&buffers[0]);
    assert(ss_ar_released_buffer(v)==&buffers[1]);
    assert(ss_ar_queued(v)==0);
    ss_ar_destroy(r);
    puts("PASS: 100 buffer recycle iterations, original pointer identity, audible PCM, stop returns buffers");
    return existing_mixer_tests();
}
