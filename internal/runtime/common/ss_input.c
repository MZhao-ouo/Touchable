#include "hle.h"
#include <stdatomic.h>
#include "ss_haptics.h"
#include "ss_touch.h"

typedef struct {
    int64_t sample;
    uint64_t buttons;
    int32_t lx, ly, rx, ry;
    uint32_t attributes, reserved;
} ss_pad_state;
_Static_assert(sizeof(ss_pad_state) == 0x28, "Npad ABI");
static _Atomic uint64_t buttons;
static _Atomic int32_t axis_x, axis_y;
static _Atomic uint32_t styles = 3;
static uint32_t player = 0x20;
static uint32_t hold_type, activation_mode, communication_mode;
void hle_bind_system_event(void *, int);
void hle_signal_system_event(void *);

/* A touch tap can be shorter than one game frame. Each new press stays visible for at least
 * SS_PRESS_HOLD_NS so that a game polling once per frame still sees it. */
#define SS_PRESS_HOLD_NS 25000000ull
static _Atomic uint64_t press_ns[64];
void ss_input_set(uint64_t mask, int32_t x, int32_t y) {
    uint64_t old = atomic_exchange(&buttons, mask), pressed = mask & ~old, now = guest_ns();
    for (int bit = 0; pressed; ++bit, pressed >>= 1) if (pressed & 1) atomic_store(&press_ns[bit], now ? now : 1);
    atomic_store(&axis_x, x); atomic_store(&axis_y, y);
}
static _Atomic uint64_t touch_left = SS_TOUCH_MENU;
void ss_touch_left_set(int layout, int first, int second, int extra) {
    uint64_t value = (uint64_t)(layout & 0xff) | (uint64_t)(first & 0xff) << 8 | (uint64_t)(second & 0xff) << 16 |
                     (uint64_t)(extra & 0xff) << 24;
    uint64_t old = atomic_load(&touch_left);
    if ((old & 0xffffffffull) == value) return;
    atomic_store(&touch_left, value | ((old >> 32) + 1) << 32);
}
uint64_t ss_touch_left_get(void) { return atomic_load(&touch_left); }
void ss_touch_press(uint64_t mask) {
    uint64_t now = guest_ns();
    for (int bit = 0; mask; ++bit, mask >>= 1) if (mask & 1) atomic_store(&press_ns[bit], now ? now : 1);
}
static uint64_t held_buttons(void) {
    uint64_t mask = atomic_load(&buttons), now = guest_ns();
    for (int bit = 0; bit < 64; ++bit) {
        uint64_t at = atomic_load_explicit(&press_ns[bit], memory_order_relaxed);
        if (at && now - at < SS_PRESS_HOLD_NS) mask |= 1ull << bit;
    }
    return mask;
}
static uint32_t player_style(uint32_t id) {
    /* The game checks the controller style before it offers the TV latency calibration.
       Console mode presents a full-key controller for the selected player or player 0.
       Handheld mode keeps the prior handheld/full-key behavior. */
    int tv = dm_opmode() != 0;
    if (tv) {
        if (id != player && id != 0) return 0;
        return 1u & atomic_load(&styles);
    }
    return id == player ? (player == 0x20 ? 2u : 1u) & atomic_load(&styles) : 0;
}
static void pad_read(ss_pad_state *out, uint32_t id, uint32_t style) {
    *out = (ss_pad_state){.sample = guest_ns() / 1000000};
    if (player_style(id) & style) {
        out->buttons = held_buttons();
        out->lx = atomic_load(&axis_x); out->ly = atomic_load(&axis_y);
        out->attributes = 3;
        if (out->buttons) {
            static int logged;
            if (!logged) {
                logged = 1;
                LOG("Input: guest read buttons %#llx id %#x style %#x", (unsigned long long)out->buttons, id, style);
            }
        }
    }
}
#define INPUT_VOID(name, cname) HLE_FORCE(name, "Host provides input", void, cname, void) {}
INPUT_VOID("_ZN2nn3hid14InitializeNpadEv", ss_pad_init)
INPUT_VOID("_ZN2nn3hid21InitializeTouchScreenEv", ss_touch_init)
INPUT_VOID("_ZN2nn3hid17InitializeGestureEv", ss_gesture_init)
INPUT_VOID("_ZN2nn3hid18InitializeDebugPadEv", ss_debug_init)
HLE_FORCE("_ZN2nn3hid21InitializeTouchScreenEii", "Host provides input", void, ss_touch_init_size, int width, int height) { (void)width; (void)height; }
HLE_FORCE("_ZN2nn3hid17InitializeGestureEii", "Host provides input", void, ss_gesture_init_size, int width, int height) { (void)width; (void)height; }
HLE_FORCE("_ZN2nn3hid22SetSupportedNpadIdTypeEPKjm", "Select one local controller", void, ss_pad_ids, const uint32_t *ids, uint64_t count) {
    uint32_t selected = count ? ids[0] : UINT32_MAX;
    for (uint64_t i = 0; i < count; ++i) if (ids[i] == 0x20) { selected = 0x20; break; }
    if (selected != player) ss_haptics_stop();
    player = selected;
    LOG("Input: selected player %#x", player);
}
HLE_FORCE("_ZN2nn3hid24SetSupportedNpadStyleSetENS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE", "Set supported controller styles", void, ss_pad_styles, uint32_t value) { atomic_store(&styles, value); LOG("Input: supported controller styles %#x", value); if (!player_style(player)) ss_haptics_stop(); }
HLE_FORCE("_ZN2nn3hid24GetSupportedNpadStyleSetEv", "Read supported controller styles", uint32_t, ss_pad_styles_get, void) { return atomic_load(&styles); }
HLE_FORCE("_ZN2nn3hid15GetNpadStyleSetERKj", "Read local controller style", uint32_t, ss_pad_style, const uint32_t *id) {
    uint32_t style = player_style(*id);
    static uint32_t logged = ~0u;   /* log the first read and each change for the selected player */
    if (*id == player && style != logged) {
        logged = style;
        LOG("Input: player %#x id %#x style %#x supported %#x mode %d", player, *id, style, atomic_load(&styles), dm_opmode());
    }
    return style;
}
/* The style events that the game bound. A live operation mode change signals them, so the game
   reads the controller style again (handheld or full-key). */
static void *style_events[8];
static _Atomic int style_event_count;
void ss_input_style_changed(void) {
    int count = atomic_load(&style_event_count);
    for (int i = 0; i < count; ++i) hle_signal_system_event(style_events[i]);
}
HLE_FORCE("_ZN2nn3hid27BindNpadStyleSetUpdateEventERKjPNS_2os15SystemEventTypeENS3_14EventClearModeE", "Create controller state event", void, ss_pad_event, const uint32_t *id, void *event, int clear) {
    (void)id; hle_bind_system_event(event, clear); hle_signal_system_event(event);
    int count = atomic_load(&style_event_count);
    if (count < 8) { style_events[count] = event; atomic_store(&style_event_count, count + 1); }
}
#define PAD_STATE(type, cname, mask) \
 HLE_FORCE("_ZN2nn3hid12GetNpadStateEPNS0_" type "ERKj", "Read controller state", void, cname, ss_pad_state *out, const uint32_t *id) { pad_read(out, *id, mask); } \
 HLE_FORCE("_ZN2nn3hid13GetNpadStatesEPNS0_" type "EiRKj", "Read controller state", int, cname##_many, ss_pad_state *out, int count, const uint32_t *id) { if (count <= 0) return 0; pad_read(out, *id, mask); return 1; }
PAD_STATE("16NpadFullKeyState", ss_pad_full, 1)
PAD_STATE("17NpadHandheldState", ss_pad_handheld, 2)
/* The phone presents one full-key or handheld controller. Joy-Con styles read as disconnected. */
PAD_STATE("16NpadJoyDualState", ss_pad_joy_dual, 4)
PAD_STATE("16NpadJoyLeftState", ss_pad_joy_left, 8)
PAD_STATE("17NpadJoyRightState", ss_pad_joy_right, 16)
HLE_FORCE("_ZN2nn3hid14DisconnectNpadERKj", "The phone controller stays connected", void, ss_pad_disconnect, const uint32_t *id) { (void)id; }
/* Controller applet: the phone controller is already connected and selected. */
HLE_FORCE("_ZN2nn3hid21ShowControllerSupportERKNS0_20ControllerSupportArgE", "Controller applet: one local controller",
          Result, ss_pad_support, const void *argument) { (void)argument; return RES_OK; }
HLE_FORCE("_ZN2nn3hid21ShowControllerSupportEPNS0_27ControllerSupportResultInfoERKNS0_20ControllerSupportArgE", "Controller applet: one local controller",
          Result, ss_pad_support_result, uint8_t *result, const void *argument) {
    (void)argument;
    dm_guest_write_prepare(result, 8);
    memset(result, 0, 8);
    result[0] = 1;
    memcpy(result + 4, &player, 4);
    return RES_OK;
}
HLE_FORCE("_ZN2nn3hid19GetPlayerLedPatternERKj", "One local controller", uint32_t, ss_pad_led, const uint32_t *id) { return player_style(*id) ? 1 : 0; }
HLE_FORCE("_ZN2nn3hid22GetNpadControllerColorEPNS0_19NpadControllerColorERKj", "Neutral controller color", Result, ss_pad_color, uint32_t *out, const uint32_t *id) { (void)id; out[0] = 0x303030; out[1] = 0x909090; return RES_OK; }
HLE_FORCE("_ZN2nn3hid22GetNpadControllerColorEPNS0_19NpadControllerColorES2_RKj", "Neutral controller color", Result, ss_pad_colors, uint32_t *left, uint32_t *right, const uint32_t *id) { ss_pad_color(left, id); return ss_pad_color(right, id); }
HLE_FORCE("_ZN2nn3hid18SetNpadJoyHoldTypeENS0_15NpadJoyHoldTypeE", "Store controller orientation", void, ss_hold_set, uint32_t value) { hold_type = value; }
HLE_FORCE("_ZN2nn3hid18GetNpadJoyHoldTypeEv", "Read controller orientation", uint32_t, ss_hold_get, void) { return hold_type; }
HLE_FORCE("_ZN2nn3hid29SetNpadHandheldActivationModeENS0_26NpadHandheldActivationModeE", "Store activation mode", void, ss_activation_set, uint32_t value) { activation_mode = value; }
HLE_FORCE("_ZN2nn3hid29GetNpadHandheldActivationModeEv", "Read activation mode", uint32_t, ss_activation_get, void) { return activation_mode; }
HLE_FORCE("_ZN2nn3hid24SetNpadCommunicationModeENS0_21NpadCommunicationModeE", "Store communication mode", void, ss_communication_set, uint32_t value) { communication_mode = value; }
HLE_FORCE("_ZN2nn3hid24GetNpadCommunicationModeEv", "Read communication mode", uint32_t, ss_communication_get, void) { return communication_mode; }
/* Switch permission is a system setting. Focus loss only mutes the sink; it
 * must not appear to revoke permission or change a cached game preference. */
HLE_FORCE("_ZN2nn3hid20IsVibrationPermittedEv", "Report phone haptic support", unsigned, ss_vibration_allowed, void) { return ss_haptics_status().supported; }
HLE_FORCE("_ZN2nn3hid25GetVibrationDeviceHandlesEPNS0_21VibrationDeviceHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE", "Map controller motors to phone haptics", int, ss_vibration_handles, uint32_t *out, int count, const uint32_t *id, uint32_t style) {
    if (!out || !id || count <= 0 || !ss_haptics_status().supported || !(player_style(*id) & style)) return 0;
    int result = count < 2 ? count : 2;
    dm_guest_write_prepare(out, result * sizeof(*out));
    out[0] = SS_VIBRATION_LEFT;
    if (result > 1) out[1] = SS_VIBRATION_RIGHT;
    return result;
}
HLE_FORCE("_ZN2nn3hid22GetVibrationDeviceInfoEPNS0_19VibrationDeviceInfoERKNS0_21VibrationDeviceHandleE", "Describe the two controller motor channels", void, ss_vibration_info_get, ss_vibration_info *out, const uint32_t *handle) {
    if (!out) return;
    dm_guest_write_prepare(out, sizeof(*out));
    *out = (ss_vibration_info){0, 0};
    if (handle && (*handle == SS_VIBRATION_LEFT || *handle == SS_VIBRATION_RIGHT))
        *out = (ss_vibration_info){1, *handle == SS_VIBRATION_LEFT ? 1 : 2};
}
HLE_FORCE("_ZN2nn3hid25InitializeVibrationDeviceERKNS0_21VibrationDeviceHandleE", "Initialize the phone motor channel", void, ss_vibration_init, const uint32_t *handle) { if (handle) ss_haptics_initialize(*handle); }
HLE_FORCE("_ZN2nn3hid18SendVibrationValueERKNS0_21VibrationDeviceHandleERKNS0_14VibrationValueE", "Send game HD vibration to phone haptics", void, ss_vibration_send, const uint32_t *handle, const ss_vibration_value *value) { if (handle) ss_haptics_send(*handle, value); }
HLE_FORCE("_ZN2nn3hid23GetActualVibrationValueEPNS0_14VibrationValueERKNS0_21VibrationDeviceHandleE", "Read the accepted controller vibration", void, ss_vibration_actual, ss_vibration_value *out, const uint32_t *handle) {
    if (!out) return;
    dm_guest_write_prepare(out, sizeof(*out));
    *out = ss_haptics_actual(handle ? *handle : 0);
}
HLE_FORCE("_ZN2nn3hid23GetSixAxisSensorHandlesEPNS0_19SixAxisSensorHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE", "No motion sensors", int, ss_motion_handles, void *out, int count, const uint32_t *id, uint32_t style) { (void)out; (void)count; (void)id; (void)style; return 0; }
HLE_FORCE("_ZN2nn3hid16GetGestureStatesEPNS0_12GestureStateEi", "No native Switch gestures", int, ss_gestures, void *out, int count) { (void)out; (void)count; return 0; }
HLE_FORCE("_ZN2nn3hid17GetDebugPadStatesEPNS0_13DebugPadStateEi", "No development controller", int, ss_debug_states, void *out, int count) { (void)out; (void)count; return 0; }
HLE_FORCE("_ZN2nn3hid16GetDebugPadStateEPNS0_13DebugPadStateE", "No development controller", void, ss_debug_state, void *out) { memset(out, 0, 0x20); }

/* Phone touch controls are translated to controller buttons. The game does
 * not receive raw touches on top of the virtual controller. */
#define TOUCH_STATE(n) \
 HLE_FORCE("_ZN2nn3hid19GetTouchScreenStateILm" #n "EEEvPNS0_16TouchScreenStateIXT_EEE", "No raw Switch touches", void, ss_touch_##n, void *out) { memset(out, 0, 16 + 40 * n); *(int64_t *)out = guest_ns() / 1000000; } \
 HLE_FORCE("_ZN2nn3hid20GetTouchScreenStatesILm" #n "EEEiPNS0_16TouchScreenStateIXT_EEEi", "No raw Switch touches", int, ss_touches_##n, void *out, int count) { if (count <= 0) return 0; ss_touch_##n(out); return 1; }
TOUCH_STATE(1) TOUCH_STATE(2) TOUCH_STATE(3) TOUCH_STATE(4)
TOUCH_STATE(5) TOUCH_STATE(6) TOUCH_STATE(7) TOUCH_STATE(8)
TOUCH_STATE(9) TOUCH_STATE(10) TOUCH_STATE(11) TOUCH_STATE(12)
TOUCH_STATE(13) TOUCH_STATE(14) TOUCH_STATE(15) TOUCH_STATE(16)
