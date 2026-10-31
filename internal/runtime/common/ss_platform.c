#include "hle.h"
#include "ss_language.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdatomic.h>
#include <time.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>

void hle_bind_system_event(void *event, int auto_clear);
void hle_signal_system_event(void *event);
static _Atomic int focus = 1;
static _Atomic int focus_pending, opmode_pending, opmode_notify;
static _Alignas(16) uint8_t notification_event[0x100];
static _Alignas(16) uint8_t health_event[0x100];
static int notification_ready, health_ready;
static void *vsync_event;
/* The display event rate in Hz: 60, 80, 90, or 120. SS_DISPLAY_HZ in the game profile selects the rate.
   The iOS app gives the rate that the screen runs at, up to the selected rate (60 Hz in Low Power Mode,
   when the device is hot, or on a 60 Hz screen). */
static _Atomic int display_hz;
static int display_hz_valid(int hz) { return hz == 60 || hz == 80 || hz == 90 || hz == 120; }
int ss_display_hz(void) {
    int hz = atomic_load(&display_hz);
    if (hz) return hz;
    const char *value = getenv("SS_DISPLAY_HZ");
    hz = value && display_hz_valid(atoi(value)) ? atoi(value) : 60;
    int none = 0;
    atomic_compare_exchange_strong(&display_hz, &none, hz);
    return atomic_load(&display_hz);
}
void nvn_present_hz(int hz);
/* Set the display event rate and the matching present period. The vsync thread uses the new
   period from its next event. */
void ss_display_set_hz(int hz) {
    if (!display_hz_valid(hz)) return;
    int old = ss_display_hz();
    if (old == hz) return;
    atomic_store(&display_hz, hz);
    nvn_present_hz(hz);
    LOG("Display event rate %d Hz -> %d Hz", old, hz);
}
static void vsync_policy(uint64_t period) {
    thread_time_constraint_policy_data_t policy = {
        .period = (uint32_t)period, .computation = (uint32_t)(period / 100),
        .constraint = (uint32_t)(period / 10), .preemptible = 1};
    if (thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
                          (thread_policy_t)&policy, THREAD_TIME_CONSTRAINT_POLICY_COUNT) != KERN_SUCCESS)
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
}
/* The game waits for this event once per frame, so its period is the frame rate.
   Wake on an absolute schedule: a relative sleep adds its wake-up delay to
   every period (about 17.5 ms on iPhone and 21 ms on a Mac). */
static void *vsync_run(void *unused) {
    (void)unused;
    pthread_setname_np("display-vsync");
    mach_timebase_info_data_t base;
    mach_timebase_info(&base);
    int hz = ss_display_hz();
    uint64_t period = 1000000000ull / (uint64_t)hz * base.denom / base.numer;
    vsync_policy(period);
    nvn_present_hz(hz);
    LOG("Display event rate %d Hz", hz);
    uint64_t next = mach_absolute_time() + period;
    for (;;) {
        mach_wait_until(next);
        hle_signal_system_event(vsync_event);
        int now_hz = atomic_load(&display_hz);
        if (now_hz != hz) {
            hz = now_hz;
            period = 1000000000ull / (uint64_t)hz * base.denom / base.numer;
            vsync_policy(period);
        }
        next += period;
        /* After a long stall, such as app suspension, restart the cadence without a burst. */
        uint64_t now = mach_absolute_time();
        if (now >= next) next = now + period;
    }
    return NULL;
}
HLE_FORCE("_ZN2nn2vi20GetDisplayVsyncEventEPNS_2os15SystemEventTypeEPNS0_7DisplayE", "Provide a 60, 80, 90, or 120 Hz display event", Result, ss_display_event, void *event, void *display) {
    (void)display;
    if (vsync_event && vsync_event != event) die("Multiple display events are not supported");
    if (!vsync_event) {
        hle_bind_system_event(event, 1); vsync_event = event;
        pthread_t thread;
        if (pthread_create(&thread, NULL, vsync_run, NULL)) die("Cannot create display event thread");
        pthread_detach(thread);
    }
    return RES_OK;
}

/* Operation mode: 0 handheld, 1 console (TV). DM_OPMODE gives the start value. Rhythm Heaven
   uses the console value to offer the TV latency calibration. dm_opmode_set() changes the mode
   while the game runs, like docking a Switch: the game gets notification 30 (operation mode
   changed) and a controller style event, and reads the mode and the controller style again. */
static _Atomic int ss_opmode = -1;
int dm_opmode(void) {
    int mode = atomic_load(&ss_opmode);
    if (mode < 0) {
        const char *value = getenv("DM_OPMODE");
        mode = value && *value && atoi(value) ? 1 : 0;
        int none = -1;
        atomic_compare_exchange_strong(&ss_opmode, &none, mode);
        mode = atomic_load(&ss_opmode);
        LOG("Operation mode: %s (DM_OPMODE)", mode ? "TV" : "handheld");
    }
    return mode;
}
/* Performance mode for nn::oe::GetPerformanceMode: 0 normal, 1 boost (the docked value).
   A game adapter can set it in touchable_game_install(). */
int ss_performance_mode;
void ss_input_style_changed(void);
void dm_opmode_set(int mode) {
    mode = mode != 0;
    int old = dm_opmode();
    if (old == mode) return;
    atomic_store(&ss_opmode, mode);
    LOG("Operation mode: %s -> %s (live)%s", old ? "TV" : "handheld", mode ? "TV" : "handheld",
        atomic_load(&opmode_notify) ? "" : "; the game has not enabled the notification yet");
    ss_input_style_changed();
    if (!atomic_load(&opmode_notify)) return;
    atomic_store(&opmode_pending, 1);
    if (notification_ready) hle_signal_system_event(notification_event);
}
void dm_focus_set(int value) {
    if (atomic_exchange(&focus, value) == value) return;
    atomic_store(&focus_pending, 1);
    if (notification_ready) hle_signal_system_event(notification_event);
}
HLE_FORCE("_ZN2nn2oe15SetCpuBoostModeENS0_12CpuBoostModeE", "Host controls CPU frequency", void, ss_cpu_boost, int mode) { (void)mode; }
HLE_FORCE("_ZN2nn2oe27SetPerformanceConfigurationENS0_15PerformanceModeEi", "Host controls CPU and GPU frequency", void, ss_perf_set, int mode, int config) { (void)mode; (void)config; }
HLE_FORCE("_ZN2nn2oe27SetPerformanceConfigurationEi", "Host controls CPU and GPU frequency", void, ss_perf_set_one, int config) { (void)config; }
HLE_FORCE("_ZN2nn2oe16GetOperationModeEv", "Read the current operation mode", int, ss_mode, void) { return dm_opmode(); }
HLE_FORCE("_ZN2nn2oe18GetPerformanceModeEv", "The game adapter selects the performance mode", int, ss_performance, void) { return ss_performance_mode; }
HLE_FORCE("_ZN2nn2oe20GetCurrentFocusStateEv", "Read app focus", int, ss_focus, void) { return atomic_load(&focus); }
HLE_FORCE("_ZN2nn2oe20SetFocusHandlingModeENS0_17FocusHandlingModeE", "App lifecycle delivers focus changes", void, ss_focus_mode, int mode) { (void)mode; }
HLE_FORCE("_ZN2nn2oe28SetResumeNotificationEnabledEb", "App lifecycle delivers resume changes", void, ss_resume_notify, unsigned on) { (void)on; }
HLE_FORCE("_ZN2nn2oe42SetOperationModeChangedNotificationEnabledEb", "The game asks for operation mode notifications", void, ss_mode_notify, unsigned on) { atomic_store(&opmode_notify, on != 0); }
HLE_FORCE("_ZN2nn2oe44SetPerformanceModeChangedNotificationEnabledEb", "Host controls performance", void, ss_perf_notify, unsigned on) { (void)on; }
HLE_FORCE("_ZN2nn2oe25TryPopNotificationMessageEPj", "Read app lifecycle notifications", unsigned, ss_notification, uint32_t *out) {
    /* 15 focus changed, 30 operation mode changed. Both ask the game to read the current state. */
    if (atomic_exchange(&focus_pending, 0)) { *out = 15; return 1; }
    if (atomic_exchange(&opmode_pending, 0)) { LOG("Operation mode: the game took notification 30"); *out = 30; return 1; }
    return 0;
}
HLE_FORCE("_ZN2nn2oe27GetNotificationMessageEventEv", "App lifecycle event", void *, ss_notification_event, void) {
    if (!notification_ready) { hle_bind_system_event(notification_event, 0); notification_ready = 1; }
    return notification_event;
}
HLE_FORCE("_ZN2nn2oe38GetHealthWarningDisappearedSystemEventEv", "No platform health overlay is active", void *, ss_health_event, void) {
    if (!health_ready) { hle_bind_system_event(health_event, 0); hle_signal_system_event(health_event); health_ready = 1; }
    return health_event;
}
HLE_FORCE("_ZN2nn2oe18GetDesiredLanguageEv", "Read the selected app language", uint64_t, ss_language, void) {
    const char *tag = ss_switch_language(getenv("DM_LANG"));
    char code[8] = {0};
    strncpy(code, tag, sizeof code - 1);
    uint64_t value;
    memcpy(&value, code, sizeof value);
    return value;
}
HLE_FORCE("_ZN2nn2oe25GetProgramTotalActiveTimeEv", "Read the monotonic app clock", uint64_t, ss_active_time, void) { return guest_ns(); }
HLE_FORCE("_ZN2nn2oe17GetPseudoDeviceIdEPNS_4util4UuidE", "Local profile ID", void, ss_device_id, uint8_t *out) {
    static const uint8_t id[16] = {0x4e,0x69,0x74,0x65,0x6e,0x70,0x48,0x6f,0x9e,0x65,0,0,0,0,0,1};
    memcpy(out, id, 16);
}
#define SS_OE_VOID(symbol, function) HLE_FORCE(symbol, "Handled by the host app lifecycle", void, function, void) {}
SS_OE_VOID("_ZN2nn2oe17EnableCrashReportEv", ss_crash_report_on)
SS_OE_VOID("_ZN2nn2oe18DisableCrashReportEv", ss_crash_report_off)
SS_OE_VOID("_ZN2nn2oe16EnableScreenShotEv", ss_screenshot_on)
SS_OE_VOID("_ZN2nn2oe17DisableScreenShotEv", ss_screenshot_off)
SS_OE_VOID("_ZN2nn2oe15EnableRecordingEv", ss_record_on)
SS_OE_VOID("_ZN2nn2oe16DisableRecordingEv", ss_record_off)
SS_OE_VOID("_ZN2nn2oe31EnterExitRequestHandlingSectionEv", ss_exit_enter)
SS_OE_VOID("_ZN2nn2oe31LeaveExitRequestHandlingSectionEv", ss_exit_leave)

HLE_FORCE("_ZN2nn3err9ShowErrorENS_6ResultE", "Report platform errors", void, ss_show_error, Result value) {
    LOG("Game error: %#x (module %u, description %u)", value, value & 511, value >> 9);
}
