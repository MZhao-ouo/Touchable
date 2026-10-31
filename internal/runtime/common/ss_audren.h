/* Switch AudioRenderer mixing model: voices, mixes, I3DL2 reverb, and one device sink.
 * The nn::audio guest bindings are in ss_audio_renderer.c. This engine reads wave data
 * through plain pointers and has no guest or platform dependencies, so tests can link it.
 *
 * Parameter changes are client state. ss_ar_update() applies them to the server state,
 * which ss_ar_render() mixes, and copies the server status (queued wave buffers and played
 * samples) back to the client. This matches RequestUpdateAudioRenderer on the console. */
#ifndef SS_AUDREN_H
#define SS_AUDREN_H
#include <pthread.h>
#include <stdint.h>

enum { SS_AR_WAVEBUFFERS = 4, SS_AR_CHANNELS = 6, SS_AR_MIX_BUFFERS = 24, SS_AR_MAX_FRAME = 1024 };
enum { SS_AR_STARTED = 0, SS_AR_STOPPED = 1, SS_AR_PAUSED = 2 };
enum { SS_AR_FORMAT_PCM16 = 2 };

typedef struct {
    const int16_t *data;     /* interleaved PCM16 frames */
    uint64_t size;           /* bytes */
    int32_t start, end;      /* sample frame offsets; playback covers [start, end) */
    int loop, end_of_stream;
} ss_ar_wavebuffer;

/* nn::audio::I3dl2ReverbParameterSet. Gains are millibels except dry_gain. */
typedef struct {
    float room_gain, room_hf_gain, decay_time, hf_decay_ratio, reflections_gain, reflections_delay,
          reverb_gain, reverb_delay, diffusion, density, hf_reference, dry_gain;
} ss_ar_i3dl2;
_Static_assert(sizeof(ss_ar_i3dl2) == 0x30, "I3dl2ReverbParameterSet ABI");

typedef struct ss_ar_mix ss_ar_mix;
typedef struct ss_ar_voice ss_ar_voice;
typedef struct ss_ar_reverb ss_ar_reverb;
typedef struct ss_ar_renderer ss_ar_renderer;

ss_ar_renderer *ss_ar_create(int rate, int frame, int voices, int submixes, int mix_buffers, int effects);
void ss_ar_destroy(ss_ar_renderer *renderer);
int ss_ar_rate(const ss_ar_renderer *renderer);
int ss_ar_frame(const ss_ar_renderer *renderer);

ss_ar_mix *ss_ar_acquire_final(ss_ar_renderer *renderer, int buffers);
ss_ar_mix *ss_ar_acquire_sub(ss_ar_renderer *renderer, int buffers);
void ss_ar_mix_destination(ss_ar_mix *mix, ss_ar_mix *destination);
void ss_ar_mix_volume(ss_ar_mix *mix, ss_ar_mix *destination, float volume, int source, int target);

ss_ar_voice *ss_ar_acquire_voice(ss_ar_renderer *renderer, int rate, int channels, int format);
void ss_ar_release_voice(ss_ar_voice *voice);
void ss_ar_voice_state(ss_ar_voice *voice, int state);
void ss_ar_voice_pitch(ss_ar_voice *voice, float pitch);
void ss_ar_voice_destination(ss_ar_voice *voice, ss_ar_mix *destination);
void ss_ar_voice_volume(ss_ar_voice *voice, ss_ar_mix *destination, float volume, int source, int target);
int ss_ar_append(ss_ar_voice *voice, const ss_ar_wavebuffer *buffer);   /* 0 when four buffers are queued */
int ss_ar_queued(const ss_ar_voice *voice);
int64_t ss_ar_played(const ss_ar_voice *voice);

ss_ar_reverb *ss_ar_add_reverb(ss_ar_renderer *renderer, ss_ar_mix *mix, int channels);
void ss_ar_reverb_io(ss_ar_reverb *reverb, const int8_t *input, const int8_t *output, int count);
void ss_ar_reverb_enable(ss_ar_reverb *reverb, int enabled);
void ss_ar_reverb_params(ss_ar_reverb *reverb, const ss_ar_i3dl2 *params);

int ss_ar_add_sink(ss_ar_renderer *renderer, ss_ar_mix *mix, const int8_t *input, int count);
void ss_ar_master_volume(ss_ar_renderer *renderer, float volume);

void ss_ar_update(ss_ar_renderer *renderer);
/* Mix one frame into interleaved stereo PCM16. */
void ss_ar_render(ss_ar_renderer *renderer, int16_t *stereo);

/* Voice play (iOS, ss_audio_renderer.c): set before the game opens its renderer. The handler gets the
   echo-cancelled microphone as mono float samples on the audio thread. */
typedef void (*ss_audio_input_handler)(const float *samples, uint32_t frames, int rate);
void ss_audio_set_input_handler(ss_audio_input_handler handler);
#endif
