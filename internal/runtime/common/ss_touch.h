/* Touch hints from a game adapter to the iPhone layer (platform/ios/App.m, "rhythm" layout).
 * The adapter knows which buttons the current scene uses; the phone shows matching touch areas. */
#ifndef SS_TOUCH_H
#define SS_TOUCH_H
#include <stdint.h>

/* Button numbers are nn::hid::NpadButton bits. */
enum { SS_BUTTON_A = 0, SS_BUTTON_B = 1, SS_BUTTON_LEFT = 12, SS_BUTTON_UP = 13, SS_BUTTON_RIGHT = 14, SS_BUTTON_DOWN = 15 };

/* D-pad directions in NpadButton order. A zero mask means the required directions are unknown. */
enum {
    SS_TOUCH_DIR_LEFT = 1, SS_TOUCH_DIR_UP = 2, SS_TOUCH_DIR_RIGHT = 4, SS_TOUCH_DIR_DOWN = 8,
    SS_TOUCH_DIR_ALL = 15,
};

/* How the left touch area maps to buttons. */
enum {
    SS_TOUCH_DPAD = 0,   /* four direction buttons; `first` holds the required SS_TOUCH_DIR_* mask */
    SS_TOUCH_ONE = 1,    /* the whole area presses `first` */
    SS_TOUCH_TWO = 2,    /* the left part presses `first`, the right part presses `second` */
    SS_TOUCH_MENU = 3,   /* menus: the phone shows a small D-pad instead of the area */
    SS_TOUCH_PAD = 4,    /* a full controller: the phone shows the D-pad and the A, B, X, Y, and ZR buttons, and no touch area */
};

/* Controls that the phone shows in a level in addition to the left area. */
enum {
    SS_TOUCH_SHOW_DPAD = 1,   /* the small D-pad */
    SS_TOUCH_SHOW_X = 2,      /* an X button */
    SS_TOUCH_SHOW_Y = 4,      /* a Y button */
};

/* Called by the game adapter on any thread. `extra` holds SS_TOUCH_SHOW_* bits. */
void ss_touch_left_set(int layout, int first, int second, int extra);
/* Packed hint: layout | first << 8 | second << 16 | extra << 24 | change count << 32. Starts as SS_TOUCH_MENU. */
uint64_t ss_touch_left_get(void);
/* Called by the game adapter on any thread. The game sees the buttons in `mask` (NpadButton bits) as one
   short press, the same as a touch tap. A tap on a game object can so use the game's own input checks. */
void ss_touch_press(uint64_t mask);

static inline int ss_touch_layout(uint64_t hint) { return (int)(hint & 0xff); }
static inline int ss_touch_first(uint64_t hint) { return (int)(hint >> 8 & 0xff); }
static inline int ss_touch_second(uint64_t hint) { return (int)(hint >> 16 & 0xff); }
static inline int ss_touch_extra(uint64_t hint) { return (int)(hint >> 24 & 0xff); }
#endif
