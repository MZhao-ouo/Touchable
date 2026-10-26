#ifndef TOUCH_LAYOUT_H
#define TOUCH_LAYOUT_H
/* Positions of the Switch-style touch controls, in view points.
   This is plain C, so a Mac test can check the layout. */
#include <math.h>

/* The order matches the button list in App.m. */
enum { SS_PAD_A, SS_PAD_B, SS_PAD_X, SS_PAD_Y, SS_PAD_L, SS_PAD_R, SS_PAD_ZL, SS_PAD_ZR,
       SS_PAD_PLUS, SS_PAD_MINUS, SS_PAD_BUTTONS };

typedef struct { double x, y, size, width; } ss_pad_circle;   /* center and height; width 0: a circle */
/* The drawn width. A wider control is a horizontal capsule. */
static inline double ss_pad_width(ss_pad_circle c) { return c.width > c.size ? c.width : c.size; }
/* The distance between the edges of two controls. Each control is a horizontal capsule or a circle. */
static inline double ss_pad_gap(ss_pad_circle a, ss_pad_circle b) {
    double reach = (ss_pad_width(a) - a.size) / 2 + (ss_pad_width(b) - b.size) / 2;
    double dx = fmax(0, fabs(a.x - b.x) - reach), dy = fabs(a.y - b.y);
    return hypot(dx, dy) - a.size / 2 - b.size / 2;
}
typedef struct {
    int tablet;
    ss_pad_circle dpad, buttons[SS_PAD_BUTTONS];
    /* Menu touch mode hides the pad. It shows Back (B) in the place of − and a pad toggle below or next to +. */
    ss_pad_circle back, pad_toggle;
    double hit_margin, dpad_margin;   /* touch area outside each drawn circle */
    double dpad_center;               /* a touch this near the D-pad center selects no direction */
} ss_pad_layout;

/* A phone keeps the controls near the lower corners. A tablet raises them a little
   toward the resting thumbs and makes them 1.2 times larger. It also puts them near the
   side edges, so they cover less of the game. On an iPad mini the D-pad center is
   205 points above the lower edge and 111 points from the left edge. A small iPad window uses
   the phone layout. The shoulder buttons sit above their side, below the game HUD
   in the upper-left corner. ZL and ZR take the outer places, because ZR is the
   default dash and the outer place is nearer the thumb. − and + take the outer upper corners.
   WINDOW_TOP is the top inset below the iPadOS window controls in the upper-left corner
   (the safe area with vertical corner adaptation). A phone passes SAFE_TOP. */
static inline ss_pad_layout ss_pad_layout_make(double width, double height, double safe_left, double safe_right,
                                               double safe_top, double safe_bottom, double window_top, int pad_idiom) {
    ss_pad_layout p = {0};
    p.tablet = pad_idiom && fmin(width, height) >= 600;
    double k = p.tablet ? 1.2 : 1, left, right, bottom;
    if (p.tablet) {
        left = safe_left + 8;   /* 16 before 2026-10-23: the user asked for controls nearer the edges */
        right = width - safe_right - 8;
        bottom = height - fmax(safe_bottom + 70, 90);
    } else {
        left = fmax(safe_left, 16);   /* the edge of the safe area; 12 points inside it before 2026-10-23 */
        right = width - fmax(safe_right, 16);
        bottom = height - fmax(safe_bottom + 18, 36);
    }
    /* The D-pad is as wide as the face-button diamond. Its center has the height of the
       face-button center, which is set below. */
    double face = 54 * k, small = 46 * k, d = 57 * k, dpad = 2 * d + face;
    double sx = left + dpad / 2 + 2 * k, sy = bottom - 70 * k;
    double fx = right - face / 2 - d - 2 * k, fy = bottom - face / 2 - d;
    p.hit_margin = 8 * k;
    p.dpad_margin = 24 * k;
    p.dpad_center = 0.12 * dpad;
    /* − and + take the outer upper corners, at the side edges of the safe area. On an iPad the
       window controls take the upper-left corner, so − and + go below them. This includes a small window
       with the phone layout. On a tablet they are 8 points from the side edges and 40 points large,
       so − stays clear of the larger game HUD. */
    double top = fmax(safe_top, 0) + 32;
    double menu = p.tablet ? 40 : small, edge = p.tablet ? 8 : 0;
    double mx = fmax(safe_left + edge, 8) + menu / 2;
    double px = width - fmax(safe_right + edge, 8) - menu / 2;
    double below = fmax(window_top, safe_top) + 4 + menu / 2;
    double my = p.tablet ? below : pad_idiom ? fmax(top, below) : top;
    ss_pad_circle minus = {mx, my, menu, 0}, plus = {px, my, menu, 0};
    /* The right side follows the user's sketch of 2026-10-23, in units of d (from B to Y). ZL and ZR are
       slanted buttons (parallelograms) 1.33 d beside B and 0.56 d below it. R is a diamond (a square
       turned 45 degrees) 0.1 d outside A and 1.82 d above it. The app draws the shapes. The layout treats
       ZL and ZR as capsules and R as the circle around the diamond. Each moves out until it keeps
       2 points between touch areas. The face buttons rise 12 points. When ZR crosses the right edge of
       the safe area (less 8 points), the diamond moves to the left. When ZL and ZR cross the lower edge
       of the safe area, the diamond rises. */
    double trigger_w = 64 * k, trigger = 37 * k, diamond = 39 * k * sqrt(2), room = 2 * p.hit_margin + 2;
    fy -= 12 * k;
    double spread = 1.33 * d, drop = 0.56 * d;   /* from the center of B to the centers of ZL and ZR */
    for (;; spread += 0.5 * k) {
        ss_pad_circle b = {fx, fy + d, face, 0}, y = {fx - d, fy, face, 0}, a = {fx + d, fy, face, 0};
        ss_pad_circle zl = {fx - spread, fy + d + drop, trigger, trigger_w}, zr = {fx + spread, fy + d + drop, trigger, trigger_w};
        if (ss_pad_gap(zl, b) >= room && ss_pad_gap(zr, b) >= room && ss_pad_gap(zl, y) >= room && ss_pad_gap(zr, a) >= room)
            break;
    }
    double edge_gap = p.tablet ? 8 : 0;   /* ZR and R keep this distance from the right edge of the safe area */
    double limit = width - safe_right - edge_gap - trigger_w / 2;
    if (fx + spread > limit) fx = limit - spread;
    double low = height - safe_bottom - trigger / 2 - 2 * k;
    if (fy + d + drop > low) fy = low - d - drop;
    sy = fy;   /* the D-pad center has the height of the face-button center (2026-10-25) */
    /* R is above A. It rises until it keeps 2 points from X and A. In a short window R would hit + or
       the map button next to +. R then goes above Y. */
    /* The map button (L) is a diamond like R. Next to + it is at map_x. */
    double map_x = px - menu / 2 - diamond / 2 - 2 * p.hit_margin - 4;
    ss_pad_circle r_button = {0}, map_next_to_plus = {map_x, my, diamond, 0};
    double row = fy - 1.82 * d;
    for (int side = 0; side < 2; ++side) {
        double rx = side ? fx - 1.1 * d : fx + 1.1 * d;
        if (!side) rx = fmin(rx, width - safe_right - edge_gap - diamond / 2);
        ss_pad_circle x_button = {fx, fy - d, face, 0}, below_r = {side ? fx - d : fx + d, fy, face, 0};
        for (row = fy - 1.82 * d;; row -= 0.5 * k) {
            r_button = (ss_pad_circle){rx, row, diamond, 0};
            if (ss_pad_gap(r_button, x_button) >= room && ss_pad_gap(r_button, below_r) >= room) break;
        }
        if (ss_pad_gap(r_button, plus) >= room && ss_pad_gap(r_button, map_next_to_plus) >= room) break;
    }
    p.dpad = (ss_pad_circle){sx, sy, dpad, 0};
    p.buttons[SS_PAD_A] = (ss_pad_circle){fx + d, fy, face, 0};
    p.buttons[SS_PAD_B] = (ss_pad_circle){fx, fy + d, face, 0};
    p.buttons[SS_PAD_X] = (ss_pad_circle){fx, fy - d, face, 0};
    p.buttons[SS_PAD_Y] = (ss_pad_circle){fx - d, fy, face, 0};
    p.buttons[SS_PAD_R] = r_button;
    p.buttons[SS_PAD_ZL] = (ss_pad_circle){fx - spread, fy + d + drop, trigger, trigger_w};
    p.buttons[SS_PAD_ZR] = (ss_pad_circle){fx + spread, fy + d + drop, trigger, trigger_w};
    p.buttons[SS_PAD_MINUS] = minus;
    p.buttons[SS_PAD_PLUS] = plus;
    /* Back and − never show together. L opens the map. It is a diamond like R, below + at the right edge.
       A short phone has no space above R there, so L goes next to +. The pad toggle also shows with
       the pad, so it keeps clear of + and L: it goes next to them in the row of +. */
    p.back = p.buttons[SS_PAD_MINUS];
    double ty = my + menu / 2 + diamond / 2 + 2 * p.hit_margin + 4, step = menu + 24 * k;
    int fits = ty + diamond / 2 + p.hit_margin + 2 + p.hit_margin + r_button.size / 2 <= row;
    /* Below +, the map diamond has the horizontal center of R. It stays inside the right edge of +. */
    double map_below_x = fmin(r_button.x, px + menu / 2 - diamond / 2);
    p.buttons[SS_PAD_L] = fits ? (ss_pad_circle){map_below_x, ty, diamond, 0} : map_next_to_plus;
    /* The toggle takes the first place to the left that keeps 2 points between touch areas. */
    for (int n = 1; n < 12; ++n) {
        p.pad_toggle = (ss_pad_circle){px - n * step, my, menu, 0};
        int clear = ss_pad_gap(p.pad_toggle, p.dpad) - p.hit_margin - p.dpad_margin >= 2;
        for (int i = 0; i < SS_PAD_BUTTONS && clear; ++i)
            clear = ss_pad_gap(p.pad_toggle, p.buttons[i]) - 2 * p.hit_margin >= 2;
        if (clear) break;
    }
    return p;
}

/* D-pad directions. The bit order matches SS_BUTTON_LEFT to SS_BUTTON_DOWN (buttons 12 to 15). */
enum { SS_DPAD_LEFT = 1, SS_DPAD_UP = 2, SS_DPAD_RIGHT = 4, SS_DPAD_DOWN = 8 };
#define SS_DPAD_HOLD (5.0 / 45)   /* a held direction keeps 5 more degrees on each side */

/* Return the directions for a touch at (x, y) from the D-pad center. y grows down.
   Each of the 8 directions takes a 45-degree sector. The diagonals hold two directions.
   PREVIOUS keeps its sector a little wider, so a thumb on a sector edge does not flicker. */
static inline unsigned ss_dpad_directions(double x, double y, double center, unsigned previous) {
    static const unsigned sectors[8] = {
        SS_DPAD_RIGHT, SS_DPAD_RIGHT | SS_DPAD_UP, SS_DPAD_UP, SS_DPAD_UP | SS_DPAD_LEFT,
        SS_DPAD_LEFT, SS_DPAD_LEFT | SS_DPAD_DOWN, SS_DPAD_DOWN, SS_DPAD_DOWN | SS_DPAD_RIGHT};
    if (hypot(x, y) <= center) return 0;
    double sector = atan2(-y, x) / atan(1.0);   /* 0 right, 2 up, 4 left, 6 down */
    for (int i = 0; i < 8; ++i)
        if (previous == sectors[i] && fabs(remainder(sector - i, 8.0)) <= 0.5 + SS_DPAD_HOLD) return previous;
    return sectors[(int)floor(sector + 0.5) & 7];
}
#endif
