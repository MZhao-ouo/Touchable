/* nn::audio AudioRenderer for games that mix through the console renderer.
 *
 * The SDK client talks to the audren service. The host replaces the client entry points that a
 * game imports and drives ss_audren.c instead. Each SDK object (AudioRendererConfig, VoiceType,
 * SubMixType, FinalMixType, MemoryPoolType, I3dl2ReverbType, DeviceSinkType) starts with one
 * pointer slot; the host writes only that slot. AudioRendererHandle is 16 bytes passed in x0/x1.
 *
 * Output follows hle_audio.c: CoreAudio on iOS or with DM_AUDIO=1, otherwise a silent sink that
 * consumes frames in real time. A server thread renders 5 ms frames into a ring and signals the
 * renderer event once per frame, as the console renderer does. */
#include "hle.h"
#include "ss_audren.h"
#include <AudioToolbox/AudioToolbox.h>
#include <TargetConditionals.h>
#include <dispatch/dispatch.h>
#include <stdatomic.h>
#include <unistd.h>

enum { RING_FRAMES = 1 << 13 };
extern void (*g_ss_audio_suspend_hook)(int on);   /* ss_audio.c */

typedef struct {
    _Atomic(ss_ar_renderer *) renderer;   /* bound by RequestUpdateAudioRenderer */
    void *event;
    int rate, frame;
    _Atomic int started, running, sink_running;
    _Atomic uint32_t read, write, want;
    int16_t ring[RING_FRAMES * 2];
    dispatch_semaphore_t wake;
    pthread_t server, sink;
    pthread_mutex_t lock;     /* start, stop, suspend */
    AudioUnit unit;
    int unit_running, suspended;
} ar_device;

typedef struct { void *address; uint64_t size; int attach, attached; } ar_pool;

static ar_device *g_device;
static _Atomic float g_handheld_volume = 1;

static int real_audio(void) { const char *e = getenv("DM_AUDIO"); return e ? atoi(e) != 0 : TARGET_OS_IPHONE; }
static void *slot_get(const void *object) { return object ? *(void *const *)object : NULL; }
static void slot_put(void *object, void *value) { dm_guest_write_prepare(object, sizeof(void *)); *(void **)object = value; }
static ar_device *device_of(uint64_t handle) {
    ar_device *device = (ar_device *)(uintptr_t)handle;
    if (!device || device != g_device) die("AudioRendererHandle %#llx is not a host renderer", (unsigned long long)handle);
    return device;
}

static uint32_t ring_fill(ar_device *device) {
    return atomic_load_explicit(&device->write, memory_order_acquire) - atomic_load_explicit(&device->read, memory_order_acquire);
}

/* SS_AUDIO_DUMP=<file.wav>: development check that writes the mixed output (48 kHz stereo PCM16). */
static FILE *dump_open(int rate) {
    const char *path = getenv("SS_AUDIO_DUMP");
    FILE *file = path ? fopen(path, "wb") : NULL;
    if (path && !file) LOG("Cannot open SS_AUDIO_DUMP %s", path);
    if (file) {
        uint32_t header[11] = {0x46464952, 0xffffffff, 0x45564157, 0x20746d66, 16, 0x00020001, (uint32_t)rate, (uint32_t)rate * 4, 0x00100004, 0x61746164, 0xffffffff};
        fwrite(header, 4, 11, file);
    }
    return file;
}
static void dump_close(FILE *file) {
    if (!file) return;
    long size = ftell(file);
    uint32_t riff = (uint32_t)(size - 8), data = (uint32_t)(size - 44);
    fseek(file, 4, SEEK_SET); fwrite(&riff, 4, 1, file);
    fseek(file, 40, SEEK_SET); fwrite(&data, 4, 1, file);
    fclose(file);
}

/* Render frames until the consumer has enough buffered audio. */
static void *server_main(void *argument) {
    ar_device *device = argument;
    int16_t frame[SS_AR_MAX_FRAME * 2];
    FILE *dump = dump_open(device->rate);
    uint64_t dumped = 0;
    while (atomic_load_explicit(&device->running, memory_order_acquire)) {
        dispatch_semaphore_wait(device->wake, dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC));
        while (atomic_load_explicit(&device->started, memory_order_acquire) &&
               ring_fill(device) + (uint32_t)device->frame <= atomic_load_explicit(&device->want, memory_order_relaxed)) {
            ss_ar_renderer *renderer = atomic_load_explicit(&device->renderer, memory_order_acquire);
            if (renderer) ss_ar_render(renderer, frame);
            else memset(frame, 0, sizeof(int16_t) * 2 * (size_t)device->frame);
            uint32_t write = atomic_load_explicit(&device->write, memory_order_relaxed);
            for (int i = 0; i < device->frame; ++i) {
                uint32_t at = ((write + (uint32_t)i) & (RING_FRAMES - 1)) * 2;
                device->ring[at] = frame[2 * i]; device->ring[at + 1] = frame[2 * i + 1];
            }
            atomic_store_explicit(&device->write, write + (uint32_t)device->frame, memory_order_release);
            if (device->event) hle_signal_system_event(device->event);
            if (dump) {
                fwrite(frame, sizeof(int16_t) * 2, (size_t)device->frame, dump);
                if (++dumped % 200 == 0) { long at = ftell(dump); dump_close(dump); dump = fopen(getenv("SS_AUDIO_DUMP"), "r+b"); if (dump) fseek(dump, at, SEEK_SET); }
            }
        }
    }
    dump_close(dump);
    return NULL;
}

static void consume(ar_device *device, int16_t *out, uint32_t frames) {
    uint32_t read = atomic_load_explicit(&device->read, memory_order_relaxed), available = ring_fill(device);
    uint32_t count = available < frames ? available : frames;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t at = ((read + i) & (RING_FRAMES - 1)) * 2;
        if (out) { out[2 * i] = device->ring[at]; out[2 * i + 1] = device->ring[at + 1]; }
    }
    if (out && count < frames) memset(out + 2 * count, 0, sizeof(int16_t) * 2 * (frames - count));
    atomic_store_explicit(&device->read, read + count, memory_order_release);
    /* Keep one callback plus one renderer frame buffered. */
    uint32_t want = frames + (uint32_t)device->frame;
    if (want > atomic_load_explicit(&device->want, memory_order_relaxed) && want < RING_FRAMES / 2)
        atomic_store_explicit(&device->want, want, memory_order_relaxed);
    dispatch_semaphore_signal(device->wake);
}

static OSStatus unit_render(void *context, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *time, UInt32 bus,
                            UInt32 frames, AudioBufferList *buffers) {
    (void)flags; (void)time; (void)bus;
    consume(context, buffers->mBuffers[0].mData, frames);
    return noErr;
}

static void *silent_sink(void *argument) {
    ar_device *device = argument;
    uint64_t period = 1000000000ull * (uint64_t)device->frame / (uint64_t)device->rate, next = now_ns();
    while (atomic_load_explicit(&device->sink_running, memory_order_acquire)) {
        next += period;
        uint64_t now = now_ns();
        if (next > now) usleep((useconds_t)((next - now) / 1000));
        else if (now - next > 20 * period) next = now;
        consume(device, NULL, (uint32_t)device->frame);
    }
    return NULL;
}

/* Voice play (iOS): the platform layer registers an input handler before the game starts. The output
   unit is then Apple's voice-processing unit, which also records the microphone. Its echo canceller
   removes the game sound that this same unit plays, so the handler receives the player's sound only.
   The handler runs on the audio thread with mono float samples. Automatic gain stays off, so levels
   do not drift. Without a handler the unit is a plain RemoteIO output, as before. */
static ss_audio_input_handler input_handler;
void ss_audio_set_input_handler(ss_audio_input_handler handler) { input_handler = handler; }
#if TARGET_OS_IPHONE
static float input_samples[4096];
static OSStatus unit_input(void *context, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *time, UInt32 bus,
                           UInt32 frames, AudioBufferList *unused) {
    (void)unused;
    ar_device *device = context;
    if (frames > 4096) return noErr;
    AudioBufferList list = {1, {{1, frames * (UInt32)sizeof(float), input_samples}}};
    if (AudioUnitRender(device->unit, flags, time, bus, frames, &list) == noErr && input_handler)
        input_handler(input_samples, frames, device->rate);
    return noErr;
}
#endif

static int unit_try_open(ar_device *device, int voice) {
    (void)voice;   /* the Mac host has no voice-processing unit */
    AudioComponentDescription description = {.componentType = kAudioUnitType_Output,
#if TARGET_OS_IPHONE
        .componentSubType = voice ? kAudioUnitSubType_VoiceProcessingIO : kAudioUnitSubType_RemoteIO,
#else
        .componentSubType = kAudioUnitSubType_DefaultOutput,
#endif
        .componentManufacturer = kAudioUnitManufacturer_Apple};
    AudioComponent component = AudioComponentFindNext(NULL, &description);
    if (!component || AudioComponentInstanceNew(component, &device->unit)) die("Cannot create the audio renderer output unit");
    AudioStreamBasicDescription format = {.mSampleRate = device->rate, .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked,
        .mBytesPerPacket = 4, .mFramesPerPacket = 1, .mBytesPerFrame = 4, .mChannelsPerFrame = 2, .mBitsPerChannel = 16};
    AURenderCallbackStruct callback = {unit_render, device};
    int failed = AudioUnitSetProperty(device->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof(format)) ||
                 AudioUnitSetProperty(device->unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof(callback));
#if TARGET_OS_IPHONE
    if (voice && !failed) {
        UInt32 on = 1, off = 0;
        AudioStreamBasicDescription input = {.mSampleRate = device->rate, .mFormatID = kAudioFormatLinearPCM,
            .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked, .mBytesPerPacket = 4, .mFramesPerPacket = 1,
            .mBytesPerFrame = 4, .mChannelsPerFrame = 1, .mBitsPerChannel = 32};
        AURenderCallbackStruct input_callback = {unit_input, device};
        failed = AudioUnitSetProperty(device->unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &on, sizeof(on)) ||
                 AudioUnitSetProperty(device->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &input, sizeof(input)) ||
                 AudioUnitSetProperty(device->unit, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &input_callback, sizeof(input_callback));
        AudioUnitSetProperty(device->unit, kAUVoiceIOProperty_VoiceProcessingEnableAGC, kAudioUnitScope_Global, 0, &off, sizeof(off));
    }
#endif
    if (!failed) failed = AudioUnitInitialize(device->unit) != noErr;
    if (failed) { AudioComponentInstanceDispose(device->unit); device->unit = NULL; }
    return !failed;
}
static void unit_open(ar_device *device) {
    int voice = input_handler != NULL;
    if (voice && unit_try_open(device, 1)) { LOG("Audio renderer: voice-processing output with echo-cancelled microphone"); return; }
    if (voice) LOG("Audio renderer: voice-processing output failed; using the plain output without voice input");
    if (!unit_try_open(device, 0)) die("Cannot configure the audio renderer output unit");
}

static void sink_start(ar_device *device) {
    if (atomic_exchange(&device->sink_running, 1)) return;
    if (pthread_create(&device->sink, NULL, silent_sink, device)) die("Cannot start the silent audio sink");
}
static void sink_stop(ar_device *device) {
    if (atomic_exchange(&device->sink_running, 0)) pthread_join(device->sink, NULL);
}
/* Called with device->lock held. */
static void output_start(ar_device *device) {
    if (device->unit && !device->suspended) {
        OSStatus status = AudioOutputUnitStart(device->unit);
        if (!status) { device->unit_running = 1; return; }
        LOG("Audio renderer output did not start (%d). The silent sink keeps the game clock running.", (int)status);
    }
    sink_start(device);
}
static void output_stop(ar_device *device) {
    if (device->unit_running) { AudioOutputUnitStop(device->unit); device->unit_running = 0; }
    sink_stop(device);
}

static void renderer_suspend(int on) {
    ar_device *device = g_device;
    if (!device) return;
    pthread_mutex_lock(&device->lock);
    if (device->suspended != on) {
        device->suspended = on;
        if (atomic_load(&device->started)) { output_stop(device); output_start(device); }
    }
    pthread_mutex_unlock(&device->lock);
}

/* ---------- Renderer ---------- */

HLE_FORCE("_ZN2nn5audio30GetAudioRendererWorkBufferSizeERKNS0_22AudioRendererParameterE",
          "The host renderer keeps its state in host memory", size_t, ss_ar_work_size, const int32_t *parameter) {
    (void)parameter; return 0x10000;
}

HLE_FORCE("_ZN2nn5audio17OpenAudioRendererEPNS0_19AudioRendererHandleEPNS_2os11SystemEventERKNS0_22AudioRendererParameterEPvm",
          "audren service -> host mixer", Result, ss_ar_open, uint64_t *handle, void *event, const int32_t *parameter, void *work, size_t size) {
    (void)work; (void)size;
    if (g_device) die("Only one audio renderer is supported");
    ar_device *device = calloc(1, sizeof(*device));
    if (!device) die("Cannot allocate the audio renderer");
    device->rate = parameter[0]; device->frame = parameter[1];
    if ((device->rate != 32000 && device->rate != 48000) || device->frame < 1 || device->frame > SS_AR_MAX_FRAME)
        die("Unsupported audio renderer format: %d Hz, %d samples", device->rate, device->frame);
    device->event = event;
    if (event) hle_bind_system_event(event, 1);
    device->wake = dispatch_semaphore_create(0);
    pthread_mutex_init(&device->lock, NULL);
    atomic_store(&device->want, (uint32_t)device->frame * 2);
    atomic_store(&device->running, 1);
    if (real_audio()) unit_open(device);
    if (pthread_create(&device->server, NULL, server_main, device)) die("Cannot start the audio renderer thread");
    g_device = device;
    g_ss_audio_suspend_hook = renderer_suspend;
    dm_guest_write_prepare(handle, 16);
    handle[0] = (uintptr_t)device; handle[1] = 0;
    LOG("audio::OpenAudioRenderer %d Hz, %d samples, voices %d, submixes %d, mix buffers %d, effects %d, %s",
        device->rate, device->frame, parameter[4], parameter[3], parameter[2], parameter[6], device->unit ? "CoreAudio" : "silent sink");
    return RES_OK;
}

HLE_FORCE("_ZN2nn5audio18StartAudioRendererENS0_19AudioRendererHandleE", "Start the host mixer", Result, ss_ar_start, uint64_t handle, uint64_t unused) {
    (void)unused;
    ar_device *device = device_of(handle);
    pthread_mutex_lock(&device->lock);
    if (!atomic_exchange(&device->started, 1)) output_start(device);
    pthread_mutex_unlock(&device->lock);
    dispatch_semaphore_signal(device->wake);
    return RES_OK;
}

HLE_FORCE("_ZN2nn5audio17StopAudioRendererENS0_19AudioRendererHandleE", "Stop the host mixer", void, ss_ar_stop, uint64_t handle, uint64_t unused) {
    (void)unused;
    ar_device *device = device_of(handle);
    pthread_mutex_lock(&device->lock);
    if (atomic_exchange(&device->started, 0)) output_stop(device);
    pthread_mutex_unlock(&device->lock);
}

HLE_FORCE("_ZN2nn5audio18CloseAudioRendererENS0_19AudioRendererHandleE", "Close the host mixer", void, ss_ar_close, uint64_t handle, uint64_t unused) {
    ar_device *device = device_of(handle);
    ss_ar_stop(handle, unused);
    atomic_store(&device->running, 0);
    dispatch_semaphore_signal(device->wake);
    pthread_join(device->server, NULL);
    g_ss_audio_suspend_hook = NULL;
    g_device = NULL;
    if (device->unit) { AudioUnitUninitialize(device->unit); AudioComponentInstanceDispose(device->unit); }
    pthread_mutex_destroy(&device->lock);
    free(device);
}

HLE_FORCE("_ZN2nn5audio29InitializeAudioRendererConfigEPNS0_19AudioRendererConfigERKNS0_22AudioRendererParameterEPvm",
          "The host mixer owns the renderer configuration", void, ss_ar_config, void *config, const int32_t *parameter, void *work, size_t size) {
    (void)work; (void)size;
    /* AudioRendererParameter: rate, samples, mix buffers, submixes, voices, sinks, effects. */
    ss_ar_renderer *renderer = ss_ar_create(parameter[0], parameter[1], parameter[4], parameter[3], parameter[2], parameter[6]);
    if (!renderer) die("Unsupported audio renderer configuration");
    ss_ar_master_volume(renderer, atomic_load(&g_handheld_volume));
    slot_put(config, renderer);
}

HLE_FORCE("_ZN2nn5audio26RequestUpdateAudioRendererENS0_19AudioRendererHandleEPKNS0_19AudioRendererConfigE",
          "Apply client changes to the host mixer", Result, ss_ar_request_update, uint64_t handle, uint64_t unused, const void *config) {
    (void)unused;
    ar_device *device = device_of(handle);
    ss_ar_renderer *renderer = slot_get(config);
    if (!renderer) die("RequestUpdateAudioRenderer received an uninitialized configuration");
    if (ss_ar_rate(renderer) != device->rate || ss_ar_frame(renderer) != device->frame) die("Audio renderer configuration does not match the open renderer");
    ss_ar_update(renderer);
    atomic_store_explicit(&device->renderer, renderer, memory_order_release);
    return RES_OK;
}

/* ---------- Mixes and sink ---------- */

HLE_FORCE("_ZN2nn5audio15AcquireFinalMixEPNS0_19AudioRendererConfigEPNS0_12FinalMixTypeEi", "Host final mix",
          unsigned, ss_ar_final, void *config, void *mix, int buffers) {
    ss_ar_mix *value = ss_ar_acquire_final(slot_get(config), buffers);
    slot_put(mix, value);
    return value != NULL;
}

HLE_FORCE("_ZN2nn5audio13AcquireSubMixEPNS0_19AudioRendererConfigEPNS0_10SubMixTypeEii", "Host submix",
          unsigned, ss_ar_sub, void *config, void *mix, int rate, int buffers) {
    ss_ar_renderer *renderer = slot_get(config);
    if (rate != ss_ar_rate(renderer)) die("Submix sample rate %d differs from the renderer", rate);
    ss_ar_mix *value = ss_ar_acquire_sub(renderer, buffers);
    slot_put(mix, value);
    return value != NULL;
}

HLE_FORCE("_ZN2nn5audio20SetSubMixDestinationEPNS0_19AudioRendererConfigEPNS0_10SubMixTypeEPNS0_12FinalMixTypeE", "Route a host submix",
          void, ss_ar_sub_destination, void *config, void *mix, void *final) {
    (void)config; ss_ar_mix_destination(slot_get(mix), slot_get(final));
}

HLE_FORCE("_ZN2nn5audio18SetSubMixMixVolumeEPNS0_10SubMixTypeEPNS0_12FinalMixTypeEfii", "Set a host submix send",
          void, ss_ar_sub_volume, void *mix, void *final, float volume, int source, int target) {
    ss_ar_mix_volume(slot_get(mix), slot_get(final), volume, source, target);
}

HLE_FORCE("_ZN2nn5audio13AddDeviceSinkEPNS0_19AudioRendererConfigEPNS0_14DeviceSinkTypeEPNS0_12FinalMixTypeEPKaiPKc", "Host device output",
          Result, ss_ar_sink, void *config, void *sink, void *final, const int8_t *input, int count, const char *name) {
    if (!ss_ar_add_sink(slot_get(config), slot_get(final), input, count)) die("Invalid device sink for %s", name ? name : "(null)");
    slot_put(sink, slot_get(final));
    return RES_OK;
}

/* AudioDeviceName = char[256]. The game sets one volume for TV output and one for the others. */
HLE_FORCE("_ZN2nn5audio19ListAudioDeviceNameEPNS0_15AudioDeviceNameEi", "Console audio device names",
          int, ss_ar_devices, char *names, int count) {
    static const char *const list[] = {"AudioTvOutput", "AudioStereoJackOutput", "AudioBuiltInSpeakerOutput"};
    int n = count < 3 ? count : 3;
    if (n <= 0) return 0;
    dm_guest_write_prepare(names, 256 * (size_t)n);
    for (int i = 0; i < n; ++i) { memset(names + 256 * i, 0, 256); strcpy(names + 256 * i, list[i]); }
    return n;
}

HLE_FORCE("_ZN2nn5audio26SetAudioDeviceOutputVolumeEPKNS0_15AudioDeviceNameEf", "Phone output uses the handheld volume",
          void, ss_ar_device_volume, const char *name, float volume) {
    if (!name || !strncmp(name, "AudioTvOutput", 256)) return;
    atomic_store(&g_handheld_volume, volume);
    if (g_device) {
        ss_ar_renderer *renderer = atomic_load(&g_device->renderer);
        if (renderer) ss_ar_master_volume(renderer, volume);
    }
}

/* ---------- Memory pools ---------- */

HLE_FORCE("_ZN2nn5audio17AcquireMemoryPoolEPNS0_19AudioRendererConfigEPNS0_14MemoryPoolTypeEPvm", "Host memory pool record",
          unsigned, ss_ar_pool, void *config, void *pool, void *address, uint64_t size) {
    (void)config;
    ar_pool *value = calloc(1, sizeof(*value));
    if (!value) die("Cannot allocate a memory pool record");
    value->address = address; value->size = size;
    slot_put(pool, value);
    return 1;
}

HLE_FORCE("_ZN2nn5audio23RequestAttachMemoryPoolEPNS0_14MemoryPoolTypeE", "Host memory is always visible to the mixer",
          unsigned, ss_ar_pool_attach, void *pool) {
    ar_pool *value = slot_get(pool);
    if (!value) return 0;
    value->attach = value->attached = 1;
    return 1;
}

HLE_FORCE("_ZN2nn5audio23RequestDetachMemoryPoolEPNS0_14MemoryPoolTypeE", "Host memory pool record",
          unsigned, ss_ar_pool_detach, void *pool) {
    ar_pool *value = slot_get(pool);
    if (!value) return 0;
    value->attach = value->attached = 0;
    return 1;
}

HLE_FORCE("_ZN2nn5audio20IsMemoryPoolAttachedEPKNS0_14MemoryPoolTypeE", "Host memory pool record",
          unsigned, ss_ar_pool_attached, const void *pool) {
    ar_pool *value = slot_get(pool);
    return value && value->attached;
}

/* ---------- I3DL2 reverb ---------- */

HLE_FORCE("_ZN2nn5audio35GetRequiredBufferSizeForI3dl2ReverbEii", "The host reverb keeps its state in host memory",
          size_t, ss_ar_reverb_size, int rate, int channels) {
    (void)rate; (void)channels; return 0x1000;
}

HLE_FORCE("_ZN2nn5audio14AddI3dl2ReverbEPNS0_19AudioRendererConfigEPNS0_15I3dl2ReverbTypeEPvmPNS0_10SubMixTypeEi", "Host I3DL2 reverb",
          Result, ss_ar_reverb_add, void *config, void *reverb, void *buffer, size_t size, void *mix, int channels) {
    (void)buffer; (void)size;
    ss_ar_reverb *value = ss_ar_add_reverb(slot_get(config), slot_get(mix), channels);
    if (!value) die("Cannot add an I3DL2 reverb with %d channels", channels);
    slot_put(reverb, value);
    return RES_OK;
}

HLE_FORCE("_ZN2nn5audio25SetI3dl2ReverbInputOutputEPNS0_15I3dl2ReverbTypeEPKaS4_i", "Host I3DL2 reverb routing",
          void, ss_ar_reverb_route, void *reverb, const int8_t *input, const int8_t *output, int count) {
    ss_ar_reverb_io(slot_get(reverb), input, output, count);
}

HLE_FORCE("_ZN2nn5audio21SetI3dl2ReverbEnabledEPNS0_15I3dl2ReverbTypeEb", "Host I3DL2 reverb switch",
          void, ss_ar_reverb_switch, void *reverb, unsigned enabled) {
    ss_ar_reverb_enable(slot_get(reverb), enabled & 0xff);
}

HLE_FORCE("_ZN2nn5audio24SetI3dl2ReverbParametersEPNS0_15I3dl2ReverbTypeEPKNS0_23I3dl2ReverbParameterSetE", "Host I3DL2 reverb parameters",
          void, ss_ar_reverb_set, void *reverb, const ss_ar_i3dl2 *parameters) {
    ss_ar_reverb_params(slot_get(reverb), parameters);
}

/* ---------- Voices ---------- */

HLE_FORCE("_ZN2nn5audio16AcquireVoiceSlotEPNS0_19AudioRendererConfigEPNS0_9VoiceTypeEiiNS0_12SampleFormatEiPKvm", "Host voice",
          unsigned, ss_ar_voice_acquire, void *config, void *voice, int rate, int channels, int format, int priority, const void *parameter, size_t size) {
    (void)priority; (void)parameter; (void)size;
    if (format != SS_AR_FORMAT_PCM16) die("Audio voice sample format %d is not implemented", format);
    ss_ar_voice *value = ss_ar_acquire_voice(slot_get(config), rate, channels, format);
    slot_put(voice, value);
    return value != NULL;
}

HLE_FORCE("_ZN2nn5audio16ReleaseVoiceSlotEPNS0_19AudioRendererConfigEPNS0_9VoiceTypeE", "Host voice",
          void, ss_ar_voice_release, void *config, void *voice) {
    (void)config;
    ss_ar_release_voice(slot_get(voice));
    slot_put(voice, NULL);
}

HLE_FORCE("_ZN2nn5audio19SetVoiceDestinationEPNS0_19AudioRendererConfigEPNS0_9VoiceTypeEPNS0_10SubMixTypeE", "Route a host voice",
          void, ss_ar_voice_to_sub, void *config, void *voice, void *mix) {
    (void)config; ss_ar_voice_destination(slot_get(voice), slot_get(mix));
}
HLE_FORCE("_ZN2nn5audio19SetVoiceDestinationEPNS0_19AudioRendererConfigEPNS0_9VoiceTypeEPNS0_12FinalMixTypeE", "Route a host voice",
          void, ss_ar_voice_to_final, void *config, void *voice, void *mix) {
    (void)config; ss_ar_voice_destination(slot_get(voice), slot_get(mix));
}
HLE_FORCE("_ZN2nn5audio17SetVoiceMixVolumeEPNS0_9VoiceTypeEPNS0_10SubMixTypeEfii", "Set a host voice send",
          void, ss_ar_voice_sub_volume, void *voice, void *mix, float volume, int source, int target) {
    ss_ar_voice_volume(slot_get(voice), slot_get(mix), volume, source, target);
}
HLE_FORCE("_ZN2nn5audio17SetVoiceMixVolumeEPNS0_9VoiceTypeEPNS0_12FinalMixTypeEfii", "Set a host voice send",
          void, ss_ar_voice_final_volume, void *voice, void *mix, float volume, int source, int target) {
    ss_ar_voice_volume(slot_get(voice), slot_get(mix), volume, source, target);
}

HLE_FORCE("_ZN2nn5audio17SetVoicePlayStateEPNS0_9VoiceTypeENS1_9PlayStateE", "Host voice state",
          void, ss_ar_voice_play, void *voice, int state) {
    ss_ar_voice_state(slot_get(voice), state);
}

HLE_FORCE("_ZN2nn5audio13SetVoicePitchEPNS0_9VoiceTypeEf", "Host voice pitch", void, ss_ar_voice_rate, void *voice, float pitch) {
    ss_ar_voice_pitch(slot_get(voice), pitch);
}

/* WaveBuffer: buffer, size, start and end sample, loop, end of stream, context, context size. */
typedef struct {
    const void *buffer; uint64_t size; int32_t start, end; uint8_t loop, end_of_stream; uint8_t pad[6];
    const void *context; uint64_t context_size;
} ss_guest_wavebuffer;
_Static_assert(sizeof(ss_guest_wavebuffer) == 0x30, "WaveBuffer ABI");

HLE_FORCE("_ZN2nn5audio16AppendWaveBufferEPNS0_9VoiceTypeEPKNS0_10WaveBufferE", "Queue host voice data",
          unsigned, ss_ar_voice_append, void *voice, const ss_guest_wavebuffer *buffer) {
    ss_ar_wavebuffer value = {buffer->buffer, buffer->size, buffer->start, buffer->end, buffer->loop != 0, buffer->end_of_stream != 0};
    return ss_ar_append(slot_get(voice), &value);
}

HLE_FORCE("_ZN2nn5audio18GetWaveBufferCountEPKNS0_9VoiceTypeE", "Host voice status", int, ss_ar_voice_queued, const void *voice) {
    return ss_ar_queued(slot_get(voice));
}

HLE_FORCE("_ZN2nn5audio25GetVoicePlayedSampleCountEPKNS0_9VoiceTypeE", "Host voice status", int64_t, ss_ar_voice_played, const void *voice) {
    return ss_ar_played(slot_get(voice));
}
