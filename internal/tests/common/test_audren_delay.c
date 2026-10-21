/* Build with ss_audren.c and -pthread. Use synthetic PCM only. */
#include "../../runtime/common/ss_audren.h"
#include <assert.h>
#include <stdio.h>

static void test_effect_order_after_slot_reuse(void) {
    ss_ar_renderer *r=ss_ar_create(48000,8,1,1,5,2);assert(r);
    ss_ar_mix *final=ss_ar_acquire_final(r,2),*sub=ss_ar_acquire_sub(r,3);assert(final&&sub);
    const int8_t sink[2]={0,1};assert(ss_ar_add_sink(r,final,sink,2));
    ss_ar_mix_destination(sub,final);ss_ar_mix_volume(sub,final,1,2,0);
    ss_ar_delay *a=ss_ar_add_delay(r,sub,1,1000000),*b=ss_ar_add_delay(r,sub,1,1000000);assert(a&&b);
    ss_ar_remove_delay(a);
    ss_ar_delay *c=ss_ar_add_delay(r,sub,1,1000000);assert(c);
    const int8_t zero=0,one=1,two=2;
    ss_ar_delay_io(b,&zero,&one,1);ss_ar_delay_io(c,&one,&two,1);
    ss_ar_voice *v=ss_ar_acquire_voice(r,48000,1,SS_AR_FORMAT_PCM16);assert(v);
    ss_ar_voice_destination(v,sub);ss_ar_voice_volume(v,sub,1,0,0);
    int16_t data[8]={1000,1000,1000,1000,1000,1000,1000,1000};
    ss_ar_wavebuffer wave={data,sizeof data,0,8,0,0};assert(ss_ar_append(v,&wave));
    ss_ar_voice_state(v,SS_AR_STARTED);ss_ar_update(r);
    int16_t result[16];ss_ar_render(r,result);assert(result[4]==1000);
    ss_ar_destroy(r);
}

int main(void) {
    test_effect_order_after_slot_reuse();
    ss_ar_renderer *r=ss_ar_create(48000,16,1,1,14,1);assert(r);
    ss_ar_mix *final=ss_ar_acquire_final(r,2),*sub=ss_ar_acquire_sub(r,12);assert(final&&sub);
    const int8_t sink[2]={0,1};assert(ss_ar_add_sink(r,final,sink,2));
    ss_ar_mix_destination(sub,final);
    ss_ar_delay *delay=ss_ar_add_delay(r,sub,6,1000000);assert(delay);
    const int8_t input[6]={0,1,2,3,4,5},output[6]={6,7,8,9,10,11};
    ss_ar_delay_io(delay,input,output,6);
    ss_ar_delay_set_time(delay,62500); /* Three samples at 48 kHz. */
    assert(ss_ar_delay_time(delay)==62500);
    ss_ar_delay_gain(delay,SS_AR_DELAY_INPUT,1);
    ss_ar_delay_gain(delay,SS_AR_DELAY_DRY,0);
    ss_ar_delay_gain(delay,SS_AR_DELAY_FEEDBACK,0.5f);
    ss_ar_delay_enable(delay,1);
    ss_ar_voice *v=ss_ar_acquire_voice(r,48000,6,SS_AR_FORMAT_PCM16);assert(v);
    ss_ar_voice_destination(v,sub);
    for(int c=0;c<6;c++){ss_ar_voice_volume(v,sub,1,c,c);ss_ar_mix_volume(sub,final,1,c+6,c&1);}
    int16_t samples[16*6]={0};
    for(int c=0;c<6;c++)samples[c]=(c+1)*100;
    ss_ar_wavebuffer wave={samples,sizeof samples,0,16,0,0};assert(ss_ar_append(v,&wave));
    ss_ar_voice_state(v,SS_AR_STARTED);ss_ar_update(r);
    int16_t result[32];ss_ar_render(r,result);
    for(int i=0;i<16;i++) {
        int left=i==5?900:i==8?450:i==11?225:i==14?112:0;
        int right=i==5?1200:i==8?600:i==11?300:i==14?150:0;
        assert(result[i*2]==left);assert(result[i*2+1]==right);
    }
    for (int frame=0;frame<8;frame++) ss_ar_render(r,result);
    assert(!ss_ar_add_reverb(r,sub,6));
    assert(!ss_ar_delay_removable(delay));
    ss_ar_delay_enable(delay,0);ss_ar_update(r);assert(ss_ar_delay_removable(delay));
    ss_ar_remove_delay(delay);
    assert(ss_ar_add_reverb(r,sub,6)); /* Released effect slot can be reused. */
    ss_ar_destroy(r);puts("PASS: six-channel delay, feedback, routing and removal");return 0;
}
