/* Switch AudioRenderer mixing model. See ss_audren.h for the update contract.
 *
 * Processing per frame follows the console renderer: voices are resampled into the mix
 * buffers of their destination, each mix applies its effects, then sends its buffers to its
 * destination mix. The final mix feeds the device sink. Mix volumes ramp linearly across a
 * frame when they change, except on the first frame of a voice or mix.
 *
 * The I3DL2 reverb is an approximation: a filtered early-reflection tap line and a
 * four-line feedback delay network driven by the I3DL2 parameters. The dry path is exact. */
#include "ss_audren.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { RV_LINES = 4, RV_DELAY = 1 << 15, RV_LINE_MAX = 1 << 13, RV_ALLPASS = 2, RV_ALLPASS_MAX = 1 << 10 };

struct ss_ar_mix {
    ss_ar_renderer *owner;
    int used, final, buffers, depth, ramp;
    struct { ss_ar_mix *destination; float volume[SS_AR_MIX_BUFFERS][SS_AR_MIX_BUFFERS]; } client, server;
    float applied[SS_AR_MIX_BUFFERS][SS_AR_MIX_BUFFERS];
    float *samples;
};

struct ss_ar_voice {
    ss_ar_renderer *owner;
    int used, released, rate, channels, format;
    int16_t coefficients[16], adpcm_history[2], loop_history[2];
    struct { int state; float pitch; ss_ar_mix *destination; float volume[SS_AR_CHANNELS][SS_AR_MIX_BUFFERS]; } client, server;
    ss_ar_wavebuffer pending[SS_AR_WAVEBUFFERS];
    int pending_count, client_queued, outstanding;
    const void *completed[SS_AR_WAVEBUFFERS];
    int completed_head, completed_count;
    int64_t client_played;
    ss_ar_wavebuffer queue[SS_AR_WAVEBUFFERS];
    int queue_head, queue_count, in_buffer, ramp;
    int64_t position, played;
    double phase;
    float history[SS_AR_CHANNELS][4];
    float applied[SS_AR_CHANNELS][SS_AR_MIX_BUFFERS];
};

struct ss_ar_reverb {
    ss_ar_renderer *owner;
    ss_ar_mix *mix;
    int used, channels;
    struct { int enabled, count; int8_t input[SS_AR_CHANNELS], output[SS_AR_CHANNELS]; ss_ar_i3dl2 params; } client, server;
    int configured;
    ss_ar_i3dl2 active;
    float room_b, reflections, late, dry, allpass_gain;
    float feedback[RV_LINES], damping_b[RV_LINES];
    int taps[4], late_delay, length[RV_LINES], allpass_length[RV_ALLPASS];
    float delay[RV_DELAY];
    int delay_position;
    float line[RV_LINES][RV_LINE_MAX], line_state[RV_LINES];
    int line_position[RV_LINES];
    float allpass[RV_ALLPASS][RV_ALLPASS_MAX];
    int allpass_position[RV_ALLPASS];
    float room_state;
};

struct ss_ar_delay {
    ss_ar_renderer *owner;
    ss_ar_mix *mix;
    int used, channels, capacity, position;
    int64_t maximum_ns;
    float *samples, filter[SS_AR_CHANNELS];
    struct { int enabled, count; int8_t input[SS_AR_CHANNELS], output[SS_AR_CHANNELS]; int64_t ns; float gains[4]; } client, server;
};

struct ss_ar_renderer {
    pthread_mutex_t client_lock, server_lock;
    int rate, frame, voice_count, mix_count, mix_buffers, effect_count, buffers_used;
    ss_ar_voice *voices;
    ss_ar_mix *mixes;          /* [0] is the final mix */
    ss_ar_reverb *reverbs;
    ss_ar_delay *delays;
    int *effect_order, effect_active;
    struct { ss_ar_mix *mix; int count; int8_t input[SS_AR_CHANNELS]; float master; } client_sink, server_sink;
    float master_applied;
    int master_ramp;
    float scratch[SS_AR_CHANNELS][SS_AR_MAX_FRAME];
};

static float clampf(float value, float low, float high) {
    return !(value >= low) ? low : value > high ? high : value;
}

ss_ar_renderer *ss_ar_create(int rate, int frame, int voices, int submixes, int mix_buffers, int effects) {
    if ((rate != 32000 && rate != 48000) || frame < 1 || frame > SS_AR_MAX_FRAME || voices < 0 || voices > 1024 ||
        submixes < 0 || submixes > 256 || mix_buffers < 1 || mix_buffers > 1024 || effects < 0 || effects > 256)
        return NULL;
    ss_ar_renderer *renderer = calloc(1, sizeof(*renderer));
    if (!renderer) return NULL;
    renderer->rate = rate; renderer->frame = frame;
    renderer->voice_count = voices; renderer->mix_count = submixes + 1;
    renderer->mix_buffers = mix_buffers; renderer->effect_count = effects;
    renderer->voices = calloc((size_t)voices + 1, sizeof(*renderer->voices));
    renderer->mixes = calloc((size_t)renderer->mix_count, sizeof(*renderer->mixes));
    renderer->reverbs = calloc((size_t)effects + 1, sizeof(*renderer->reverbs));
    renderer->delays = calloc((size_t)effects + 1, sizeof(*renderer->delays));
    renderer->effect_order = calloc((size_t)effects + 1, sizeof(*renderer->effect_order));
    if (!renderer->voices || !renderer->mixes || !renderer->reverbs || !renderer->delays || !renderer->effect_order) { ss_ar_destroy(renderer); return NULL; }
    pthread_mutex_init(&renderer->client_lock, NULL);
    pthread_mutex_init(&renderer->server_lock, NULL);
    renderer->client_sink.master = renderer->server_sink.master = renderer->master_applied = 1;
    return renderer;
}

void ss_ar_destroy(ss_ar_renderer *renderer) {
    if (!renderer) return;
    if (renderer->mixes) for (int i = 0; i < renderer->mix_count; ++i) free(renderer->mixes[i].samples);
    if (renderer->delays) for (int i = 0; i < renderer->effect_count; ++i) free(renderer->delays[i].samples);
    free(renderer->delays); free(renderer->effect_order);
    free(renderer->voices); free(renderer->mixes); free(renderer->reverbs);
    pthread_mutex_destroy(&renderer->client_lock);
    pthread_mutex_destroy(&renderer->server_lock);
    free(renderer);
}

int ss_ar_rate(const ss_ar_renderer *renderer) { return renderer->rate; }
int ss_ar_frame(const ss_ar_renderer *renderer) { return renderer->frame; }

static ss_ar_mix *acquire_mix(ss_ar_renderer *renderer, int index, int buffers) {
    if (buffers < 1 || buffers > SS_AR_MIX_BUFFERS) return NULL;
    pthread_mutex_lock(&renderer->client_lock);
    ss_ar_mix *mix = NULL;
    if (renderer->buffers_used + buffers <= renderer->mix_buffers) {
        for (int i = index; i < (index ? renderer->mix_count : 1); ++i)
            if (!renderer->mixes[i].used) { mix = &renderer->mixes[i]; break; }
    }
    if (mix) {
        float *samples = calloc((size_t)buffers * renderer->frame, sizeof(float));
        if (samples) {
            pthread_mutex_lock(&renderer->server_lock);
            free(mix->samples);
            memset(mix, 0, sizeof(*mix));
            mix->owner = renderer; mix->final = index == 0; mix->buffers = buffers; mix->samples = samples;
            mix->used = 1;
            renderer->buffers_used += buffers;
            pthread_mutex_unlock(&renderer->server_lock);
        } else mix = NULL;
    }
    pthread_mutex_unlock(&renderer->client_lock);
    return mix;
}
ss_ar_mix *ss_ar_acquire_final(ss_ar_renderer *renderer, int buffers) { return acquire_mix(renderer, 0, buffers); }
ss_ar_mix *ss_ar_acquire_sub(ss_ar_renderer *renderer, int buffers) { return acquire_mix(renderer, 1, buffers); }

void ss_ar_mix_destination(ss_ar_mix *mix, ss_ar_mix *destination) {
    if (!mix || mix->final || destination == mix) return;
    pthread_mutex_lock(&mix->owner->client_lock);
    mix->client.destination = destination;
    pthread_mutex_unlock(&mix->owner->client_lock);
}

void ss_ar_mix_volume(ss_ar_mix *mix, ss_ar_mix *destination, float volume, int source, int target) {
    if (!mix || !destination || source < 0 || source >= mix->buffers || target < 0 || target >= destination->buffers) return;
    pthread_mutex_lock(&mix->owner->client_lock);
    mix->client.volume[source][target] = isfinite(volume) ? volume : 0;
    pthread_mutex_unlock(&mix->owner->client_lock);
}

ss_ar_voice *ss_ar_acquire_voice(ss_ar_renderer *renderer, int rate, int channels, int format) {
    if (rate < 1000 || rate > 192000 || channels < 1 || channels > SS_AR_CHANNELS || (format != SS_AR_FORMAT_PCM16 && (format != SS_AR_FORMAT_ADPCM || channels != 1))) return NULL;
    pthread_mutex_lock(&renderer->client_lock);
    ss_ar_voice *voice = NULL;
    for (int i = 0; i < renderer->voice_count; ++i)
        if (!renderer->voices[i].used) { voice = &renderer->voices[i]; break; }
    if (voice) {
        pthread_mutex_lock(&renderer->server_lock);
        memset(voice, 0, sizeof(*voice));
        voice->owner = renderer; voice->rate = rate; voice->channels = channels; voice->format = format;
        voice->client.state = voice->server.state = SS_AR_STOPPED;
        voice->client.pitch = voice->server.pitch = 1;
        voice->phase = 1;
        voice->used = 1;
        pthread_mutex_unlock(&renderer->server_lock);
    }
    pthread_mutex_unlock(&renderer->client_lock);
    return voice;
}

void ss_ar_voice_adpcm_parameters(ss_ar_voice *voice, const int16_t coefficients[16]) {
    if (!voice || !coefficients) return;
    pthread_mutex_lock(&voice->owner->server_lock);
    memcpy(voice->coefficients, coefficients, sizeof(voice->coefficients));
    pthread_mutex_unlock(&voice->owner->server_lock);
}

void ss_ar_release_voice(ss_ar_voice *voice) {
    if (!voice) return;
    pthread_mutex_lock(&voice->owner->client_lock);
    voice->released = 1;
    pthread_mutex_unlock(&voice->owner->client_lock);
}

void ss_ar_voice_state(ss_ar_voice *voice, int state) {
    if (!voice || state < SS_AR_STARTED || state > SS_AR_PAUSED) return;
    pthread_mutex_lock(&voice->owner->client_lock);
    voice->client.state = state;
    pthread_mutex_unlock(&voice->owner->client_lock);
}

void ss_ar_voice_pitch(ss_ar_voice *voice, float pitch) {
    if (!voice || !isfinite(pitch) || pitch <= 0) return;
    pthread_mutex_lock(&voice->owner->client_lock);
    voice->client.pitch = pitch;
    pthread_mutex_unlock(&voice->owner->client_lock);
}

void ss_ar_voice_destination(ss_ar_voice *voice, ss_ar_mix *destination) {
    if (!voice) return;
    pthread_mutex_lock(&voice->owner->client_lock);
    if (voice->client.destination != destination) memset(voice->client.volume, 0, sizeof(voice->client.volume));
    voice->client.destination = destination;
    pthread_mutex_unlock(&voice->owner->client_lock);
}

void ss_ar_voice_volume(ss_ar_voice *voice, ss_ar_mix *destination, float volume, int source, int target) {
    if (!voice || !destination || source < 0 || source >= voice->channels || target < 0 || target >= destination->buffers) return;
    pthread_mutex_lock(&voice->owner->client_lock);
    if (voice->client.destination == destination) voice->client.volume[source][target] = isfinite(volume) ? volume : 0;
    pthread_mutex_unlock(&voice->owner->client_lock);
}

int ss_ar_append(ss_ar_voice *voice, const ss_ar_wavebuffer *buffer) {
    if (!voice || !buffer) return 0;
    pthread_mutex_lock(&voice->owner->client_lock);
    int accepted = voice->client_queued < SS_AR_WAVEBUFFERS && voice->pending_count < SS_AR_WAVEBUFFERS && (!buffer->tag || voice->outstanding < SS_AR_WAVEBUFFERS);
    if (accepted) {
        voice->pending[voice->pending_count++] = *buffer;
        ++voice->client_queued;
        if (buffer->tag) ++voice->outstanding;
    }
    pthread_mutex_unlock(&voice->owner->client_lock);
    return accepted;
}

const void *ss_ar_released_buffer(ss_ar_voice *voice) {
    if (!voice) return NULL;
    pthread_mutex_lock(&voice->owner->client_lock);
    pthread_mutex_lock(&voice->owner->server_lock);
    const void *result = NULL;
    if (voice->completed_count) {
        result = voice->completed[voice->completed_head];
        voice->completed_head = (voice->completed_head + 1) % SS_AR_WAVEBUFFERS;
        --voice->completed_count; --voice->outstanding;
    }
    pthread_mutex_unlock(&voice->owner->server_lock);
    pthread_mutex_unlock(&voice->owner->client_lock);
    return result;
}
int ss_ar_voice_play_state(ss_ar_voice *voice) {
    if (!voice) return SS_AR_STOPPED;
    pthread_mutex_lock(&voice->owner->client_lock);
    int state = voice->client.state;
    pthread_mutex_unlock(&voice->owner->client_lock);
    return state;
}

int ss_ar_queued(const ss_ar_voice *voice) {
    if (!voice) return 0;
    pthread_mutex_lock(&voice->owner->client_lock);
    int count = voice->client_queued;
    pthread_mutex_unlock(&voice->owner->client_lock);
    return count;
}

int64_t ss_ar_played(const ss_ar_voice *voice) {
    if (!voice) return 0;
    pthread_mutex_lock(&voice->owner->client_lock);
    int64_t count = voice->client_played;
    pthread_mutex_unlock(&voice->owner->client_lock);
    return count;
}

ss_ar_reverb *ss_ar_add_reverb(ss_ar_renderer *renderer, ss_ar_mix *mix, int channels) {
    if (!mix || channels < 1 || channels > SS_AR_CHANNELS) return NULL;
    pthread_mutex_lock(&renderer->client_lock);
    ss_ar_reverb *reverb = NULL;
    for (int i = 0; i < renderer->effect_count; ++i)
        if (!renderer->reverbs[i].used && !renderer->delays[i].used) { reverb = &renderer->reverbs[i]; break; }
    if (reverb) {
        pthread_mutex_lock(&renderer->server_lock);
        memset(reverb, 0, sizeof(*reverb));
        reverb->owner = renderer; reverb->mix = mix; reverb->channels = channels;
        /* I3DL2 defaults: a disabled effect passes its input to its output. */
        ss_ar_i3dl2 defaults = {-10000, 0, 1.0f, 0.5f, -10000, 0.02f, -10000, 0.04f, 100, 100, 5000, 1};
        reverb->client.params = reverb->server.params = defaults;
        reverb->used = 1;
        renderer->effect_order[renderer->effect_active++] = (int)(reverb - renderer->reverbs);
        pthread_mutex_unlock(&renderer->server_lock);
    }
    pthread_mutex_unlock(&renderer->client_lock);
    return reverb;
}

void ss_ar_reverb_io(ss_ar_reverb *reverb, const int8_t *input, const int8_t *output, int count) {
    if (!reverb || count < 0 || count > SS_AR_CHANNELS) return;
    pthread_mutex_lock(&reverb->owner->client_lock);
    reverb->client.count = count;
    for (int i = 0; i < count; ++i) { reverb->client.input[i] = input[i]; reverb->client.output[i] = output[i]; }
    pthread_mutex_unlock(&reverb->owner->client_lock);
}

void ss_ar_reverb_enable(ss_ar_reverb *reverb, int enabled) {
    if (!reverb) return;
    pthread_mutex_lock(&reverb->owner->client_lock);
    reverb->client.enabled = enabled != 0;
    pthread_mutex_unlock(&reverb->owner->client_lock);
}

void ss_ar_reverb_params(ss_ar_reverb *reverb, const ss_ar_i3dl2 *params) {
    if (!reverb || !params) return;
    pthread_mutex_lock(&reverb->owner->client_lock);
    reverb->client.params = *params;
    pthread_mutex_unlock(&reverb->owner->client_lock);
}

int ss_ar_add_sink(ss_ar_renderer *renderer, ss_ar_mix *mix, const int8_t *input, int count) {
    if (!mix || count < 1 || count > SS_AR_CHANNELS) return 0;
    for (int i = 0; i < count; ++i) if (input[i] < 0 || input[i] >= mix->buffers) return 0;
    pthread_mutex_lock(&renderer->client_lock);
    renderer->client_sink.mix = mix; renderer->client_sink.count = count;
    memcpy(renderer->client_sink.input, input, (size_t)count);
    pthread_mutex_unlock(&renderer->client_lock);
    return 1;
}

void ss_ar_master_volume(ss_ar_renderer *renderer, float volume) {
    pthread_mutex_lock(&renderer->client_lock);
    renderer->client_sink.master = clampf(volume, 0, 1);
    pthread_mutex_unlock(&renderer->client_lock);
}

/* ---------- Server ---------- */

static void finish_buffer(ss_ar_voice *voice);
static void voice_reset(ss_ar_voice *voice) {
    while (voice->queue_count) finish_buffer(voice);
    voice->queue_head = voice->queue_count = voice->in_buffer = voice->ramp = 0;
    voice->position = voice->played = 0;
    voice->phase = 1;
    memset(voice->history, 0, sizeof(voice->history));
    memset(voice->adpcm_history, 0, sizeof(voice->adpcm_history));
}

static int mix_depth(ss_ar_mix *mix) {
    int depth = 0;
    for (ss_ar_mix *walk = mix; walk && !walk->final; walk = walk->server.destination)
        if (++depth > 64) return -1;   /* A cycle is not rendered. */
    return depth;
}

void ss_ar_update(ss_ar_renderer *renderer) {
    pthread_mutex_lock(&renderer->client_lock);
    pthread_mutex_lock(&renderer->server_lock);
    for (int i = 0; i < renderer->voice_count; ++i) {
        ss_ar_voice *voice = &renderer->voices[i];
        if (!voice->used) continue;
        if (voice->released) { voice->used = 0; voice->released = 0; continue; }
        for (int k = 0; k < voice->pending_count && voice->queue_count < SS_AR_WAVEBUFFERS; ++k)
            voice->queue[(voice->queue_head + voice->queue_count++) % SS_AR_WAVEBUFFERS] = voice->pending[k];
        voice->pending_count = 0;
        int old_state = voice->server.state, new_state = voice->client.state;
        if (new_state == SS_AR_STOPPED && old_state != SS_AR_STOPPED) voice_reset(voice);
        if (new_state == SS_AR_STARTED && old_state == SS_AR_STOPPED) voice->ramp = 0;
        voice->server.state = new_state;
        voice->server.pitch = voice->client.pitch;
        if (voice->server.destination != voice->client.destination) voice->ramp = 0;
        voice->server.destination = voice->client.destination;
        memcpy(voice->server.volume, voice->client.volume, sizeof(voice->server.volume));
        voice->client_queued = voice->queue_count;
        voice->client_played = voice->played;
    }
    for (int i = 0; i < renderer->mix_count; ++i) {
        ss_ar_mix *mix = &renderer->mixes[i];
        if (!mix->used) continue;
        if (mix->server.destination != mix->client.destination) mix->ramp = 0;
        mix->server = mix->client;
    }
    for (int i = 0; i < renderer->mix_count; ++i)
        if (renderer->mixes[i].used) renderer->mixes[i].depth = mix_depth(&renderer->mixes[i]);
    for (int i = 0; i < renderer->effect_count; ++i)
        if (renderer->reverbs[i].used) renderer->reverbs[i].server = renderer->reverbs[i].client;
    for (int i = 0; i < renderer->effect_count; ++i)
        if (renderer->delays[i].used) renderer->delays[i].server = renderer->delays[i].client;
    renderer->server_sink = renderer->client_sink;
    pthread_mutex_unlock(&renderer->server_lock);
    pthread_mutex_unlock(&renderer->client_lock);
}

static void finish_buffer(ss_ar_voice *voice) {
    const void *tag = voice->queue[voice->queue_head].tag;
    if (tag) voice->completed[(voice->completed_head + voice->completed_count++) % SS_AR_WAVEBUFFERS] = tag;
    int end_of_stream = voice->queue[voice->queue_head].end_of_stream;
    voice->queue_head = (voice->queue_head + 1) % SS_AR_WAVEBUFFERS;
    --voice->queue_count;
    voice->in_buffer = 0;
    if (end_of_stream) voice->played = 0;
}

/* DSP ADPCM: each eight-byte frame contains a predictor/scale and fourteen signed nibbles. */
static int16_t decode_adpcm(ss_ar_voice *voice, const uint8_t *data, int64_t position) {
    const uint8_t *block = data + position / 14 * 8;
    int nibble = (int)(position % 14);
    unsigned predictor = block[0] >> 4;
    if (predictor > 7) predictor = 7;
    int value = (block[1 + nibble / 2] >> (nibble % 2 ? 0 : 4)) & 15;
    if (value >= 8) value -= 16;
    int64_t sample = (int64_t)value * (1 << (block[0] & 15)) * 2048 + 1024;
    sample += (int64_t)voice->coefficients[predictor * 2] * voice->adpcm_history[0];
    sample += (int64_t)voice->coefficients[predictor * 2 + 1] * voice->adpcm_history[1];
    sample >>= 11;
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
    voice->adpcm_history[1] = voice->adpcm_history[0];
    voice->adpcm_history[0] = (int16_t)sample;
    return (int16_t)sample;
}

/* Read the next source frame into history slot 3. Silence when the queue is empty. */
static void fetch(ss_ar_voice *voice) {
    int channels = voice->channels;
    for (int c = 0; c < channels; ++c) {
        float *history = voice->history[c];
        history[0] = history[1]; history[1] = history[2]; history[2] = history[3]; history[3] = 0;
    }
    while (voice->queue_count) {
        const ss_ar_wavebuffer *buffer = &voice->queue[voice->queue_head];
        int adpcm = voice->format == SS_AR_FORMAT_ADPCM;
        int64_t frames = !buffer->data ? 0 : adpcm ? (int64_t)(buffer->size / 8 * 14) : (int64_t)(buffer->size / (2u * (unsigned)channels));
        int64_t end = buffer->end < frames ? buffer->end : frames, start = buffer->start;
        if (start < 0 || start >= end) { finish_buffer(voice); continue; }
        if (!voice->in_buffer) {
            voice->position = start; voice->in_buffer = 1;
            if (adpcm) {
                if (buffer->context && buffer->context_size >= 6)
                    memcpy(voice->adpcm_history, buffer->context + 1, sizeof(voice->adpcm_history));
                memcpy(voice->loop_history, voice->adpcm_history, sizeof(voice->loop_history));
            }
        }
        if (adpcm) voice->history[0][3] = decode_adpcm(voice, (const uint8_t *)buffer->data, voice->position) * (1.0f / 32768);
        else {
            const int16_t *frame = buffer->data + voice->position * channels;
            for (int c = 0; c < channels; ++c) voice->history[c][3] = frame[c] * (1.0f / 32768);
        }
        ++voice->played;
        if (++voice->position >= end) {
            if (buffer->loop) {
                voice->position = start;
                if (adpcm) memcpy(voice->adpcm_history, voice->loop_history, sizeof(voice->adpcm_history));
            }
            else finish_buffer(voice);
        }
        return;
    }
}

static float hermite(const float *y, float x) {
    float c1 = 0.5f * (y[2] - y[0]);
    float c2 = y[0] - 2.5f * y[1] + 2 * y[2] - 0.5f * y[3];
    float c3 = 0.5f * (y[3] - y[0]) + 1.5f * (y[1] - y[2]);
    return ((c3 * x + c2) * x + c1) * x + y[1];
}

static void accumulate(float *target, const float *source, int frame, float start, float end) {
    if (start == 0 && end == 0) return;
    if (start == end) { for (int i = 0; i < frame; ++i) target[i] += source[i] * end; return; }
    float step = (end - start) / frame;
    for (int i = 0; i < frame; ++i) target[i] += source[i] * (start + step * (i + 1));
}

static void render_voice(ss_ar_renderer *renderer, ss_ar_voice *voice) {
    ss_ar_mix *destination = voice->server.destination;
    if (!destination || !destination->used || destination->depth < 0) return;
    int frame = renderer->frame, channels = voice->channels;
    double step = (double)voice->server.pitch * voice->rate / renderer->rate;
    if (!(step > 0) || step > 64) return;
    for (int i = 0; i < frame; ++i) {
        while (voice->phase >= 1) { fetch(voice); voice->phase -= 1; }
        for (int c = 0; c < channels; ++c) renderer->scratch[c][i] = hermite(voice->history[c], (float)voice->phase);
        voice->phase += step;
    }
    if (!voice->ramp) memcpy(voice->applied, voice->server.volume, sizeof(voice->applied));
    for (int c = 0; c < channels; ++c)
        for (int b = 0; b < destination->buffers; ++b) {
            float target = voice->server.volume[c][b];
            accumulate(destination->samples + (size_t)b * frame, renderer->scratch[c], frame, voice->applied[c][b], target);
            voice->applied[c][b] = target;
        }
    voice->ramp = 1;
}

/* ---------- I3DL2 reverb ---------- */

static float millibels(float value) { return powf(10, value / 2000); }

/* One-pole low-pass y = (1 - b) x + b y with gain g at angular frequency w. */
static float lowpass_b(float g, float w) {
    if (g >= 0.9999f) return 0;
    g = g < 0.0001f ? 0.0001f : g;
    float g2 = g * g, c = cosf(w), a = 1 - g2 * c;
    float root = a * a - (1 - g2) * (1 - g2);
    float b = (a - sqrtf(root > 0 ? root : 0)) / (1 - g2);
    return clampf(b, 0, 0.9999f);
}

static void reverb_configure(ss_ar_reverb *reverb) {
    ss_ar_i3dl2 p = reverb->server.params;
    p.room_gain = clampf(p.room_gain, -10000, 0);
    p.room_hf_gain = clampf(p.room_hf_gain, -10000, 0);
    p.decay_time = clampf(p.decay_time, 0.1f, 20);
    p.hf_decay_ratio = clampf(p.hf_decay_ratio, 0.1f, 2);
    p.reflections_gain = clampf(p.reflections_gain, -10000, 1000);
    p.reflections_delay = clampf(p.reflections_delay, 0, 0.3f);
    p.reverb_gain = clampf(p.reverb_gain, -10000, 2000);
    p.reverb_delay = clampf(p.reverb_delay, 0, 0.1f);
    p.diffusion = clampf(p.diffusion, 0, 100);
    p.density = clampf(p.density, 0, 100);
    p.hf_reference = clampf(p.hf_reference, 20, 20000);
    p.dry_gain = clampf(p.dry_gain, 0, 1);
    float rate = (float)reverb->owner->rate, w = 2 * (float)M_PI * p.hf_reference / rate;
    float room = millibels(p.room_gain);
    reverb->room_b = lowpass_b(millibels(p.room_hf_gain), w);
    reverb->reflections = p.room_gain <= -10000 || p.reflections_gain <= -10000 ? 0 : room * millibels(p.reflections_gain);
    reverb->late = p.room_gain <= -10000 || p.reverb_gain <= -10000 ? 0 : room * millibels(p.reverb_gain);
    reverb->dry = p.dry_gain;
    float spread = 0.6f + 0.4f * p.density / 100;
    static const float tap_seconds[4] = {0, 0.0043f, 0.0101f, 0.0167f};
    for (int i = 0; i < 4; ++i) {
        int tap = (int)((p.reflections_delay + tap_seconds[i] * spread) * rate);
        reverb->taps[i] = tap < RV_DELAY - 1 ? tap : RV_DELAY - 1;
    }
    int late = (int)((p.reflections_delay + p.reverb_delay) * rate);
    reverb->late_delay = late < RV_DELAY - 1 ? late : RV_DELAY - 1;
    static const float line_seconds[RV_LINES] = {0.0297f, 0.0371f, 0.0411f, 0.0437f};
    for (int i = 0; i < RV_LINES; ++i) {
        int length = (int)(line_seconds[i] * spread * rate);
        length = length < 16 ? 16 : length > RV_LINE_MAX ? RV_LINE_MAX : length;
        if (reverb->length[i] != length) { reverb->length[i] = length; reverb->line_position[i] = 0; }
        float seconds = (float)length / rate;
        reverb->feedback[i] = powf(10, -3 * seconds / p.decay_time);
        float hf = powf(10, -3 * seconds / (p.decay_time * p.hf_decay_ratio));
        reverb->damping_b[i] = p.hf_decay_ratio < 1 ? lowpass_b(hf / reverb->feedback[i], w) : 0;
    }
    static const float allpass_seconds[RV_ALLPASS] = {0.00235f, 0.00702f};
    for (int i = 0; i < RV_ALLPASS; ++i) {
        int length = (int)(allpass_seconds[i] * rate);
        reverb->allpass_length[i] = length < 1 ? 1 : length > RV_ALLPASS_MAX ? RV_ALLPASS_MAX : length;
    }
    reverb->allpass_gain = 0.7f * p.diffusion / 100;
    reverb->active = reverb->server.params;
    reverb->configured = 1;
}

static float delayed(const ss_ar_reverb *reverb, int samples) {
    return reverb->delay[(reverb->delay_position - samples) & (RV_DELAY - 1)];
}

static void reverb_process(ss_ar_reverb *reverb) {
    ss_ar_mix *mix = reverb->mix;
    int frame = reverb->owner->frame, count = reverb->server.count;
    float *input[SS_AR_CHANNELS], *output[SS_AR_CHANNELS];
    for (int k = 0; k < count; ++k) {
        if (reverb->server.input[k] < 0 || reverb->server.input[k] >= mix->buffers ||
            reverb->server.output[k] < 0 || reverb->server.output[k] >= mix->buffers) return;
        input[k] = mix->samples + (size_t)reverb->server.input[k] * frame;
        output[k] = mix->samples + (size_t)reverb->server.output[k] * frame;
    }
    if (!reverb->server.enabled) {
        for (int k = 0; k < count; ++k) if (input[k] != output[k]) memcpy(output[k], input[k], sizeof(float) * frame);
        return;
    }
    if (!reverb->configured || memcmp(&reverb->active, &reverb->server.params, sizeof(reverb->active))) reverb_configure(reverb);
    float late_scale = reverb->late * 0.5f, g = reverb->allpass_gain;
    for (int i = 0; i < frame; ++i) {
        float x = 0, in[SS_AR_CHANNELS];
        for (int k = 0; k < count; ++k) { in[k] = input[k][i]; x += in[k]; }
        x = count ? x / count : 0;
        reverb->room_state = (1 - reverb->room_b) * x + reverb->room_b * reverb->room_state;
        reverb->delay_position = (reverb->delay_position + 1) & (RV_DELAY - 1);
        reverb->delay[reverb->delay_position] = reverb->room_state;
        float early = (delayed(reverb, reverb->taps[0]) + 0.8f * delayed(reverb, reverb->taps[1]) +
                       0.6f * delayed(reverb, reverb->taps[2]) + 0.45f * delayed(reverb, reverb->taps[3])) * 0.35f * reverb->reflections;
        float late_in = delayed(reverb, reverb->late_delay);
        for (int a = 0; a < RV_ALLPASS; ++a) {
            float *z = &reverb->allpass[a][reverb->allpass_position[a]];
            float y = -g * late_in + *z;
            *z = late_in + g * y;
            late_in = y;
            if (++reverb->allpass_position[a] >= reverb->allpass_length[a]) reverb->allpass_position[a] = 0;
        }
        float out[RV_LINES], sum = 0;
        for (int l = 0; l < RV_LINES; ++l) {
            out[l] = reverb->line[l][reverb->line_position[l]];
            reverb->line_state[l] = (1 - reverb->damping_b[l]) * out[l] + reverb->damping_b[l] * reverb->line_state[l];
            sum += reverb->line_state[l] * reverb->feedback[l];
        }
        sum *= 0.5f;
        for (int l = 0; l < RV_LINES; ++l) {
            reverb->line[l][reverb->line_position[l]] = late_in + reverb->line_state[l] * reverb->feedback[l] - sum;
            if (++reverb->line_position[l] >= reverb->length[l]) reverb->line_position[l] = 0;
        }
        float wet[2] = {early + late_scale * (out[0] - out[1] + out[2] - out[3]),
                        early + late_scale * (out[0] + out[1] - out[2] - out[3])};
        for (int k = 0; k < count; ++k) output[k][i] = reverb->dry * in[k] + wet[k & 1];
    }
}

/* Delay lines share effect slots with reverbs, preserving allocation order. */
ss_ar_delay *ss_ar_add_delay(ss_ar_renderer *r, ss_ar_mix *mix, int channels, int64_t maximum_ns) {
    if (!mix || mix->owner != r || channels < 1 || channels > SS_AR_CHANNELS || maximum_ns < 1 || maximum_ns > 8000000000ll) return NULL;
    int capacity = (int)(maximum_ns * r->rate / 1000000000ll) + 1;
    float *samples = calloc((size_t)capacity * channels, sizeof(float));
    if (!samples) return NULL;
    pthread_mutex_lock(&r->client_lock);
    pthread_mutex_lock(&r->server_lock);
    ss_ar_delay *delay = NULL;
    for (int i = 0; i < r->effect_count; ++i) if (!r->delays[i].used && !r->reverbs[i].used) { delay=&r->delays[i]; break; }
    if (delay) {
        memset(delay, 0, sizeof(*delay));
        delay->owner=r; delay->mix=mix; delay->channels=channels; delay->capacity=capacity;
        delay->maximum_ns=maximum_ns; delay->samples=samples; delay->used=1;
        delay->client.gains[SS_AR_DELAY_INPUT]=delay->client.gains[SS_AR_DELAY_DRY]=1;
        delay->server=delay->client;
        r->effect_order[r->effect_active++] = (int)(delay - r->delays);
    } else free(samples);
    pthread_mutex_unlock(&r->server_lock);
    pthread_mutex_unlock(&r->client_lock);
    return delay;
}
void ss_ar_delay_io(ss_ar_delay *d, const int8_t *input, const int8_t *output, int count) {
    if (!d || count < 0 || count > d->channels) return;
    for (int i=0;i<count;++i) if (input[i]<0 || input[i]>=d->mix->buffers || output[i]<0 || output[i]>=d->mix->buffers) return;
    pthread_mutex_lock(&d->owner->client_lock);
    d->client.count=count;
    memcpy(d->client.input,input,(size_t)count); memcpy(d->client.output,output,(size_t)count);
    pthread_mutex_unlock(&d->owner->client_lock);
}
void ss_ar_delay_enable(ss_ar_delay *d, int enabled) {
    if (!d) return;
    pthread_mutex_lock(&d->owner->client_lock); d->client.enabled=enabled!=0; pthread_mutex_unlock(&d->owner->client_lock);
}
void ss_ar_delay_set_time(ss_ar_delay *d, int64_t ns) {
    if (!d) return;
    pthread_mutex_lock(&d->owner->client_lock); d->client.ns=ns<0?0:ns>d->maximum_ns?d->maximum_ns:ns; pthread_mutex_unlock(&d->owner->client_lock);
}
int64_t ss_ar_delay_time(ss_ar_delay *d) {
    if (!d) return 0;
    pthread_mutex_lock(&d->owner->client_lock); int64_t ns=d->client.ns; pthread_mutex_unlock(&d->owner->client_lock); return ns;
}
void ss_ar_delay_gain(ss_ar_delay *d, int kind, float gain) {
    if (!d || kind<0 || kind>SS_AR_DELAY_LOWPASS) return;
    pthread_mutex_lock(&d->owner->client_lock); d->client.gains[kind]=clampf(gain,0,1); pthread_mutex_unlock(&d->owner->client_lock);
}
int ss_ar_delay_removable(ss_ar_delay *d) {
    if (!d) return 1;
    pthread_mutex_lock(&d->owner->server_lock); int ready=!d->server.enabled; pthread_mutex_unlock(&d->owner->server_lock); return ready;
}
void ss_ar_remove_delay(ss_ar_delay *d) {
    if (!d) return;
    ss_ar_renderer *r=d->owner;
    pthread_mutex_lock(&r->client_lock); pthread_mutex_lock(&r->server_lock);
    if (!d->server.enabled) {
        int slot=(int)(d-r->delays);
        for (int i=0;i<r->effect_active;++i) if (r->effect_order[i]==slot) {
            memmove(&r->effect_order[i],&r->effect_order[i+1],(size_t)(r->effect_active-i-1)*sizeof(*r->effect_order));
            --r->effect_active;
            break;
        }
        free(d->samples); memset(d,0,sizeof(*d));
    }
    pthread_mutex_unlock(&r->server_lock); pthread_mutex_unlock(&r->client_lock);
}
static void delay_process(ss_ar_delay *d) {
    int frames=d->owner->frame, channels=d->server.count;
    int lag=(int)(d->server.ns*d->owner->rate/1000000000ll);
    if (lag<1) lag=1;
    if (lag>=d->capacity) lag=d->capacity-1;
    for (int i=0;i<frames;++i) {
        float input[SS_AR_CHANNELS];
        for (int c=0;c<channels;++c) input[c]=d->mix->samples[(size_t)d->server.input[c]*frames+i];
        int read=(d->position+d->capacity-lag)%d->capacity;
        for (int c=0;c<channels;++c) {
            float output=input[c];
            if (d->server.enabled) {
                float delayed=d->samples[(size_t)c*d->capacity+read];
                float lowpass=d->server.gains[SS_AR_DELAY_LOWPASS];
                d->filter[c]=(1-lowpass)*delayed+lowpass*d->filter[c];
                d->samples[(size_t)c*d->capacity+d->position]=input[c]*d->server.gains[SS_AR_DELAY_INPUT]+d->filter[c]*d->server.gains[SS_AR_DELAY_FEEDBACK];
                output=input[c]*d->server.gains[SS_AR_DELAY_DRY]+d->filter[c];
            }
            d->mix->samples[(size_t)d->server.output[c]*frames+i]=output;
        }
        if (d->server.enabled && ++d->position>=d->capacity) d->position=0;
    }
}

/* ---------- Frame ---------- */

static void send_mix(ss_ar_renderer *renderer, ss_ar_mix *mix) {
    ss_ar_mix *destination = mix->server.destination;
    if (!destination || !destination->used) return;
    int frame = renderer->frame;
    if (!mix->ramp) memcpy(mix->applied, mix->server.volume, sizeof(mix->applied));
    for (int s = 0; s < mix->buffers; ++s)
        for (int d = 0; d < destination->buffers; ++d) {
            float target = mix->server.volume[s][d];
            accumulate(destination->samples + (size_t)d * frame, mix->samples + (size_t)s * frame, frame, mix->applied[s][d], target);
            mix->applied[s][d] = target;
        }
    mix->ramp = 1;
}

static void process_effects(ss_ar_renderer *renderer, ss_ar_mix *mix) {
    for (int rank = 0; rank < renderer->effect_active; ++rank) {
        int i = renderer->effect_order[rank];
        if (renderer->reverbs[i].used && renderer->reverbs[i].mix == mix) reverb_process(&renderer->reverbs[i]);
        else if (renderer->delays[i].used && renderer->delays[i].mix == mix) delay_process(&renderer->delays[i]);
    }
}

static int16_t pcm16(float value) {
    value *= 32768;
    return (int16_t)(value >= 32767 ? 32767 : value <= -32768 ? -32768 : lrintf(value));
}

void ss_ar_render(ss_ar_renderer *renderer, int16_t *stereo) {
    pthread_mutex_lock(&renderer->server_lock);
    int frame = renderer->frame, deepest = 0;
    for (int i = 0; i < renderer->mix_count; ++i) {
        ss_ar_mix *mix = &renderer->mixes[i];
        if (!mix->used) continue;
        memset(mix->samples, 0, sizeof(float) * (size_t)mix->buffers * frame);
        if (mix->depth > deepest) deepest = mix->depth;
    }
    for (int i = 0; i < renderer->voice_count; ++i) {
        ss_ar_voice *voice = &renderer->voices[i];
        if (voice->used && voice->server.state == SS_AR_STARTED) render_voice(renderer, voice);
    }
    for (int depth = deepest; depth >= 1; --depth)
        for (int i = 1; i < renderer->mix_count; ++i) {
            ss_ar_mix *mix = &renderer->mixes[i];
            if (!mix->used || mix->depth != depth) continue;
            process_effects(renderer, mix);
            send_mix(renderer, mix);
        }
    ss_ar_mix *sink = renderer->server_sink.mix;
    if (renderer->mixes[0].used) process_effects(renderer, &renderer->mixes[0]);
    float master = renderer->server_sink.master, from = renderer->master_ramp ? renderer->master_applied : master;
    float step = (master - from) / frame;
    renderer->master_applied = master; renderer->master_ramp = 1;
    if (!sink || !sink->used || !sink->final) { memset(stereo, 0, sizeof(int16_t) * 2 * (size_t)frame); pthread_mutex_unlock(&renderer->server_lock); return; }
    const int8_t *in = renderer->server_sink.input;
    int count = renderer->server_sink.count;
    const float *channel[SS_AR_CHANNELS];
    for (int k = 0; k < count; ++k) channel[k] = sink->samples + (size_t)in[k] * frame;
    for (int i = 0; i < frame; ++i) {
        float left = channel[0][i], right = count > 1 ? channel[1][i] : left;
        if (count >= 6) {   /* 5.1 order: FL FR FC LFE RL RR */
            left += 0.7071f * (channel[2][i] + channel[4][i]);
            right += 0.7071f * (channel[2][i] + channel[5][i]);
        }
        float gain = from + step * (i + 1);
        stereo[2 * i] = pcm16(left * gain);
        stereo[2 * i + 1] = pcm16(right * gain);
    }
    pthread_mutex_unlock(&renderer->server_lock);
}
