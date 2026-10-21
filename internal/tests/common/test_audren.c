/* Build: xcrun clang -Wall -Wextra -pthread -Iruntime/common tests/common/test_audren.c runtime/common/ss_audren.c
 *        -o build/checks/common/test_audren && build/checks/common/test_audren */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../../runtime/common/ss_audren.h"

enum { FRAME = 240 };

/* One final mix with two buffers routed to a stereo sink. */
static ss_ar_renderer *stereo_renderer(ss_ar_mix **final, int submixes, int effects) {
    ss_ar_renderer *r = ss_ar_create(48000, FRAME, 8, submixes, 2 + 4 * submixes, effects);
    assert(r);
    *final = ss_ar_acquire_final(r, 2);
    assert(*final);
    const int8_t input[2] = {0, 1};
    assert(ss_ar_add_sink(r, *final, input, 2));
    return r;
}

static void test_passthrough_and_counts(void) {
    ss_ar_mix *final;
    ss_ar_renderer *r = stereo_renderer(&final, 0, 0);
    static int16_t samples[FRAME * 4];
    for (int i = 0; i < FRAME * 4; ++i) samples[i] = (int16_t)((i * 37) % 2000 - 1000);
    ss_ar_voice *v = ss_ar_acquire_voice(r, 48000, 1, SS_AR_FORMAT_PCM16);
    assert(v);
    ss_ar_voice_destination(v, final);
    ss_ar_voice_volume(v, final, 1, 0, 0);
    ss_ar_voice_volume(v, final, 1, 0, 1);
    ss_ar_wavebuffer b = {samples, sizeof(samples), 0, FRAME * 4, 0, 1};
    for (int i = 0; i < 4; ++i) assert(ss_ar_append(v, &b));
    assert(!ss_ar_append(v, &b));                       /* four buffers at most */
    assert(ss_ar_queued(v) == 4);
    ss_ar_voice_state(v, SS_AR_STARTED);
    ss_ar_update(r);
    assert(ss_ar_queued(v) == 4 && ss_ar_played(v) == 0);
    int16_t out[FRAME * 2];
    ss_ar_render(r, out);
    /* Hermite interpolation at phase 0 returns source samples, two samples behind. */
    for (int i = 2; i < FRAME; ++i) { assert(out[2 * i] == samples[i - 2]); assert(out[2 * i + 1] == samples[i - 2]); }
    assert(ss_ar_played(v) == 0);                       /* status changes only on update */
    ss_ar_update(r);
    assert(ss_ar_played(v) == FRAME);
    for (int f = 0; f < 4; ++f) ss_ar_render(r, out);
    ss_ar_update(r);
    /* Five frames played 1200 samples: buffer 1 (960 samples) ended and, being an end of stream
       buffer, reset the count, so only buffer 2's samples remain counted. */
    assert(ss_ar_queued(v) == 3);
    assert(ss_ar_played(v) == 5 * FRAME - FRAME * 4);
    ss_ar_voice_state(v, SS_AR_STOPPED);
    ss_ar_update(r);
    assert(ss_ar_queued(v) == 0 && ss_ar_played(v) == 0);   /* stop releases every buffer */
    ss_ar_render(r, out);
    for (int i = 0; i < FRAME * 2; ++i) assert(out[i] == 0);
    ss_ar_destroy(r);
}

static void test_loop_and_pitch(void) {
    ss_ar_mix *final;
    ss_ar_renderer *r = stereo_renderer(&final, 0, 0);
    static int16_t samples[100];
    for (int i = 0; i < 100; ++i) samples[i] = 1000;
    ss_ar_voice *v = ss_ar_acquire_voice(r, 48000, 1, SS_AR_FORMAT_PCM16);
    ss_ar_voice_destination(v, final);
    ss_ar_voice_volume(v, final, 0.5f, 0, 0);
    ss_ar_voice_pitch(v, 2);
    ss_ar_wavebuffer b = {samples, sizeof(samples), 0, 100, 1, 0};
    assert(ss_ar_append(v, &b));
    ss_ar_voice_state(v, SS_AR_STARTED);
    ss_ar_update(r);
    int16_t out[FRAME * 2];
    for (int f = 0; f < 10; ++f) ss_ar_render(r, out);
    ss_ar_update(r);
    assert(ss_ar_queued(v) == 1);                       /* a looping buffer is never consumed */
    /* Pitch 2 reads two source samples per output; the last step is fetched at the next frame. */
    assert(ss_ar_played(v) == 2 * 10 * FRAME - 1);
    for (int i = 4; i < FRAME; ++i) { assert(out[2 * i] == 500); assert(out[2 * i + 1] == 0); }
    ss_ar_destroy(r);
}

static void test_reverb_dry_path(void) {
    for (int enabled = 0; enabled < 2; ++enabled) {
        ss_ar_mix *final;
        ss_ar_renderer *r = stereo_renderer(&final, 1, 1);
        ss_ar_mix *sub = ss_ar_acquire_sub(r, 4);
        assert(sub);
        ss_ar_mix_destination(sub, final);
        ss_ar_mix_volume(sub, final, 1, 2, 0);
        ss_ar_mix_volume(sub, final, 1, 3, 1);
        ss_ar_reverb *reverb = ss_ar_add_reverb(r, sub, 2);
        assert(reverb);
        const int8_t in[2] = {0, 1}, out_index[2] = {2, 3};
        ss_ar_reverb_io(reverb, in, out_index, 2);
        ss_ar_reverb_enable(reverb, enabled);
        /* The game's "no reverb" parameters: every wet gain -10000 mB and dry gain 1. */
        ss_ar_i3dl2 p = {-10000, -10000, 1.5f, 0.5f, -10000, 0.01f, -10000, 0.02f, 100, 100, 5000, 1};
        ss_ar_reverb_params(reverb, &p);
        static int16_t samples[FRAME * 2];
        for (int i = 0; i < FRAME * 2; ++i) samples[i] = (int16_t)(i % 2 ? 3000 : -2000);
        ss_ar_voice *v = ss_ar_acquire_voice(r, 48000, 2, SS_AR_FORMAT_PCM16);
        ss_ar_voice_destination(v, sub);
        ss_ar_voice_volume(v, sub, 1, 0, 0);
        ss_ar_voice_volume(v, sub, 1, 1, 1);
        ss_ar_wavebuffer b = {samples, sizeof(samples), 0, FRAME, 0, 0};
        assert(ss_ar_append(v, &b));
        ss_ar_voice_state(v, SS_AR_STARTED);
        ss_ar_update(r);
        int16_t out[FRAME * 2];
        ss_ar_render(r, out);
        for (int i = 2; i < FRAME; ++i) { assert(out[2 * i] == -2000); assert(out[2 * i + 1] == 3000); }
        ss_ar_destroy(r);
    }
}

static void test_six_channel_reverb_routing(void) {
    for (int enabled = 0; enabled < 2; ++enabled) {
        ss_ar_renderer *r = ss_ar_create(48000, FRAME, 1, 1, 14, 1);
        assert(r);
        ss_ar_mix *final = ss_ar_acquire_final(r, 2), *sub = ss_ar_acquire_sub(r, 12);
        assert(final && sub);
        const int8_t sink[2] = {0, 1};
        assert(ss_ar_add_sink(r, final, sink, 2));
        ss_ar_mix_destination(sub, final);
        assert(!ss_ar_add_reverb(r, sub, 0));
        assert(!ss_ar_add_reverb(r, sub, 7));
        ss_ar_reverb *reverb = ss_ar_add_reverb(r, sub, 6);
        assert(reverb);
        const int8_t input[6] = {0, 1, 2, 3, 4, 5}, output[6] = {6, 7, 8, 9, 10, 11};
        ss_ar_reverb_io(reverb, input, output, 6);
        ss_ar_reverb_enable(reverb, enabled);
        ss_ar_i3dl2 parameters = {-10000, -10000, 1.5f, 0.5f, -10000, 0.01f, -10000, 0.02f, 100, 100, 5000, 1};
        ss_ar_reverb_params(reverb, &parameters);
        ss_ar_voice *voice = ss_ar_acquire_voice(r, 48000, 6, SS_AR_FORMAT_PCM16);
        assert(voice);
        ss_ar_voice_destination(voice, sub);
        for (int channel = 0; channel < 6; ++channel) {
            ss_ar_voice_volume(voice, sub, 1, channel, channel);
            ss_ar_mix_volume(sub, final, 1, channel+6, channel & 1);
        }
        int16_t samples[FRAME * 6];
        for (int i = 0; i < FRAME; ++i)
            for (int channel = 0; channel < 6; ++channel) samples[i*6+channel] = (channel+1)*100;
        ss_ar_wavebuffer buffer = {samples, sizeof samples, 0, FRAME, 0, 0};
        assert(ss_ar_append(voice, &buffer));
        ss_ar_voice_state(voice, SS_AR_STARTED);
        ss_ar_update(r);
        int16_t result[FRAME * 2];
        ss_ar_render(r, result);
        for (int i = 2; i < FRAME; ++i) { assert(result[2*i] == 900); assert(result[2*i+1] == 1200); }
        ss_ar_destroy(r);
    }
}

int main(void) {
    test_passthrough_and_counts();
    test_loop_and_pitch();
    test_reverb_dry_path();
    test_six_channel_reverb_routing();
    puts("test_audren: ok");
    return 0;
}
