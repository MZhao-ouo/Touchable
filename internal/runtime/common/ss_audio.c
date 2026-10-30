/* AudioOut and the AudioRenderer (ss_audio_renderer.c) both follow the app lifecycle. */
#define dm_audio_suspend ss_audio_out_suspend
#include <hle_audio.c>
#undef dm_audio_suspend
void (*g_ss_audio_suspend_hook)(int on);
void (*g_ss_media_suspend_hook)(int on);
void dm_audio_suspend(int on) {
    if (g_ss_media_suspend_hook) g_ss_media_suspend_hook(on);
    ss_audio_out_suspend(on);
    if (g_ss_audio_suspend_hook) g_ss_audio_suspend_hook(on);
}

HLE_FORCE("_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutEPNS_2os11SystemEventERKNS0_17AudioOutParameterE",
          "Open the host default audio output", Result, ss_audio_open_default, void *output, void *event, const uint8_t *parameter) {
    return h_au_Open(output, event, NULL, parameter);
}
/* Some SDK clients poll released buffers instead of requesting an event. */
HLE_FORCE("_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutERKNS0_17AudioOutParameterE",
          "Open default output with polling completion", Result, ss_audio_default_poll, void *output, const uint8_t *parameter) {
    return h_au_Open(output, NULL, NULL, parameter);
}
HLE_FORCE("_ZN2nn5audio12OpenAudioOutEPNS0_8AudioOutEPKcRKNS0_17AudioOutParameterE",
          "Open named output with polling completion", Result, ss_audio_named_poll, void *output, const char *name, const uint8_t *parameter) {
    return h_au_Open(output, NULL, name, parameter);
}
