/* Build with -Iinternal/platform/ios. Checks the Switch-style touch layout and the D-pad directions in TouchLayout.h. */
#include "TouchLayout.h"
#include <assert.h>
#include <stdio.h>

/* WINDOW is the top inset below the iPadOS window controls. The iPadOS 27 simulator gives
   a safe top of 10 and a window top of 53 on an iPad mini in landscape. */
typedef struct { const char *name; double width, height, left, right, top, bottom, window; int pad; int tablet; } device;

static const device devices[] = {
    {"iPhone 16 Pro Max", 956, 440, 62, 62, 0, 21, 0, 0, 0},
    {"iPhone 16 Pro", 874, 402, 62, 62, 0, 21, 0, 0, 0},
    {"iPhone 13 mini", 812, 375, 50, 50, 0, 21, 0, 0, 0},
    {"iPhone SE", 667, 375, 0, 0, 0, 0, 0, 0, 0},
    {"iPad mini", 1133, 744, 0, 0, 10, 20, 53, 1, 1},
    {"iPad mini, no window controls", 1133, 744, 0, 0, 0, 20, 0, 1, 1},
    {"iPad mini, status bar", 1133, 744, 0, 0, 24, 20, 24, 1, 1},
    {"iPad Air 11", 1180, 820, 0, 0, 10, 20, 53, 1, 1},
    {"iPad Pro 13", 1376, 1032, 0, 0, 10, 20, 53, 1, 1},
    {"iPad small window", 700, 520, 0, 0, 0, 0, 0, 1, 0},
    {"iPad small window, window controls", 1133, 372, 0, 0, 10, 10, 53, 1, 0},
};

static void check_device(const device *d) {
    ss_pad_layout p = ss_pad_layout_make(d->width, d->height, d->left, d->right, d->top, d->bottom, d->window, d->pad);
    assert(p.tablet == d->tablet);
    ss_pad_circle all[SS_PAD_BUTTONS + 1];
    double margin[SS_PAD_BUTTONS + 1];
    for (int i = 0; i < SS_PAD_BUTTONS; ++i) { all[i] = p.buttons[i]; margin[i] = p.hit_margin; }
    all[SS_PAD_BUTTONS] = p.dpad; margin[SS_PAD_BUTTONS] = p.dpad_margin;
    for (int i = 0; i <= SS_PAD_BUTTONS; ++i) {
        double r = all[i].size / 2, half = ss_pad_width(all[i]) / 2;
        /* Each control stays inside the safe area. */
        assert(all[i].x - half >= d->left && all[i].x + half <= d->width - d->right);
        assert(all[i].y - r >= 0 && all[i].y + r <= d->height - d->bottom);
        /* Thumb controls stay below the HUD in the upper-left corner. */
        if (i != SS_PAD_PLUS && i != SS_PAD_MINUS)
            assert(!(all[i].x - half < 0.33 * d->width && all[i].y - r < 0.24 * d->height));
        /* Touch areas do not overlap. */
        for (int j = i + 1; j <= SS_PAD_BUTTONS; ++j) {
            double gap = ss_pad_gap(all[i], all[j]) - margin[i] - margin[j];
            assert(gap >= 0);
        }
    }
    /* The face buttons keep the Switch diamond: A right, B down, X up, Y left. */
    assert(p.buttons[SS_PAD_A].x > p.buttons[SS_PAD_B].x && p.buttons[SS_PAD_B].y > p.buttons[SS_PAD_A].y);
    assert(p.buttons[SS_PAD_X].y < p.buttons[SS_PAD_A].y && p.buttons[SS_PAD_Y].x < p.buttons[SS_PAD_B].x);
    /* The user's sketch: ZL and ZR are slanted buttons below and outside the diamond, lower than B.
       R is on the right side above A. L (the map) is on the right side. */
    ss_pad_circle zl = p.buttons[SS_PAD_ZL], zr = p.buttons[SS_PAD_ZR], b_button = p.buttons[SS_PAD_B];
    assert(zl.y == zr.y && zl.y > b_button.y && zl.x < p.buttons[SS_PAD_Y].x && zr.x > p.buttons[SS_PAD_A].x);
    assert(ss_pad_width(zl) > zl.size && ss_pad_width(zr) > zr.size);
    assert(p.buttons[SS_PAD_R].x > d->width / 2 && p.buttons[SS_PAD_R].y < p.buttons[SS_PAD_X].y &&
           ss_pad_width(p.buttons[SS_PAD_R]) == p.buttons[SS_PAD_R].size);   /* the circle around the diamond */
    assert(p.buttons[SS_PAD_L].x > d->width / 2);
    /* - is the control nearest the upper-left corner, and + is the control nearest the upper-right corner. */
    ss_pad_circle minus = p.buttons[SS_PAD_MINUS], plus = p.buttons[SS_PAD_PLUS];
    for (int i = 0; i <= SS_PAD_BUTTONS; ++i) {
        if (i != SS_PAD_MINUS)
            assert(hypot(minus.x - d->left, minus.y - d->top) < hypot(all[i].x - d->left, all[i].y - d->top));
        if (i != SS_PAD_PLUS)
            assert(hypot(plus.x - d->width + d->right, plus.y - d->top) < hypot(all[i].x - d->width + d->right, all[i].y - d->top));
    }
    /* Build 23 moves - and + to the side edges of the safe area (8 points on a tablet or a phone without insets). */
    double edge = p.tablet ? 8 : (d->left > 0 ? 0 : 8);
    assert(fabs(minus.x - minus.size / 2 - d->left - edge) < 1e-9);
    assert(fabs(d->width - d->right - plus.x - plus.size / 2 - edge) < 1e-9);
    assert(minus.y - minus.size / 2 >= d->top && minus.y + minus.size / 2 <= fmax(0.2 * d->height, d->window + 4 + minus.size) && plus.y == minus.y);
    /* On an iPad, also in a small window with the phone layout, - and + stay below the window controls. */
    if (d->pad) assert(minus.y - minus.size / 2 >= d->window);
    /* Back takes the place of -. They never show together. L (the map) is round and goes below + at
       the right edge. A phone up to 390 points high (iPhone 16 Pro: 381 without the home indicator) has
       no space above the capsule ZR, so L goes next to +. The pad toggle goes next to + and L in the row
       of +, apart from the other controls, because it also shows with the pad. */
    assert(p.back.x == minus.x && p.back.y == minus.y && p.back.size == minus.size);
    ss_pad_circle toggle = p.pad_toggle;
    double tr = toggle.size / 2;
    assert(toggle.x - tr >= d->left && toggle.x + tr <= d->width - d->right);
    assert(toggle.y - tr >= d->top && toggle.y + tr <= 0.33 * d->height);
    for (int j = 0; j <= SS_PAD_BUTTONS; ++j)
        assert(hypot(toggle.x - all[j].x, toggle.y - all[j].y) - tr - all[j].size / 2 - p.hit_margin - margin[j] >= 2);
    ss_pad_circle map = p.buttons[SS_PAD_L];
    assert(ss_pad_width(map) == map.size && map.size > plus.size);   /* the circle around the map diamond */
    /* L is below + when the space above R allows it, else next to +. The toggle is left of both. */
    if (map.y > plus.y)   /* below +: the center of R, or the right edge of + where R is farther right */
        assert((fabs(map.x - p.buttons[SS_PAD_R].x) < 1e-9 || fabs(map.x + map.size / 2 - plus.x - plus.size / 2) < 1e-9) &&
               map.x + map.size / 2 <= plus.x + plus.size / 2 + 1e-9 && toggle.x < plus.x);
    else
        assert(map.y == plus.y && map.x < plus.x && toggle.x < map.x);
    if (d->height - d->bottom >= 390) assert(map.y > plus.y);
    assert(toggle.y == plus.y && toggle.x > 0.5 * d->width);
    /* The D-pad center ignores a resting thumb but leaves most of the D-pad for directions. */
    assert(p.dpad_center >= 12 && p.dpad_center <= 0.2 * p.dpad.size);
    /* Build 18 makes the D-pad as wide as the face-button diamond after user feedback.
       Since 2026-10-25 the D-pad center has the height of the face-button center (between Y and A).
       ZL and ZR are below the diamond, so B is above the lower margin.
       The D-pad stays below the row of - and +. */
    ss_pad_circle a = p.buttons[SS_PAD_A], y = p.buttons[SS_PAD_Y], b = p.buttons[SS_PAD_B];
    assert(fabs(p.dpad.size - (a.x - y.x + a.size)) < 1e-9);
    double lower = p.tablet ? d->height - fmax(d->bottom + 70, 90) : d->height - fmax(d->bottom + 18, 36);
    assert(fabs(p.dpad.y - a.y) < 1e-9 && fabs(p.dpad.y - y.y) < 1e-9);
    assert(b.y + b.size / 2 < lower);
    assert(p.dpad.y - p.dpad.size / 2 - p.dpad_margin > minus.y + minus.size / 2 + p.hit_margin);
    if (p.tablet) {
        /* A tablet raises the controls a little toward the thumbs and makes them larger.
           Build 12 put the stick center 254 points up on an iPad mini. That was too high.
           The D-pad takes the place of the stick. */
        assert(p.dpad.size > 200 && p.buttons[SS_PAD_A].size > 60);
        /* Since 2026-10-25 the D-pad has the height of the face buttons: 205 points up on an iPad mini. */
        assert(d->height - p.dpad.y >= 150 && d->height - p.dpad.y <= 215);
        assert(d->height - d->bottom - (p.buttons[SS_PAD_B].y + p.buttons[SS_PAD_B].size / 2) >= 60);
        assert(p.dpad.x <= 160 && d->width - p.buttons[SS_PAD_B].x <= 180);
        /* Build 16 put the D-pad and A 42 points from the side edges. They covered too much of the game. */
        assert(p.dpad.x - p.dpad.size / 2 - d->left <= 20);
        /* Since 2026-10-23 ZR is beside B and is the outermost control at the right. */
        assert(d->width - d->right - p.buttons[SS_PAD_ZR].x - ss_pad_width(p.buttons[SS_PAD_ZR]) / 2 <= 20);
        for (int i = 0; i <= SS_PAD_BUTTONS; ++i)
            assert(all[i].x - ss_pad_width(all[i]) / 2 - d->left >= 7.99 && d->width - d->right - all[i].x - ss_pad_width(all[i]) / 2 >= 7.99);
        /* - and + stay below the window controls. - stays clear of the silk spool in the game HUD.
           The iPad mini simulator shows the spool at (75.5, 70.5) with a 25.5-point radius. The HUD scales with the height. */
        double hud = d->height / 744;
        assert(hypot(minus.x - 75.5 * hud, minus.y - 70.5 * hud) >= minus.size / 2 + 25.5 * hud + 2);
        assert(minus.size >= 28 && p.pad_toggle.size >= 28);
    } else {
        assert(p.dpad.size == 168 && p.buttons[SS_PAD_A].size == 54);
        assert(d->height - d->bottom - (p.dpad.y + p.dpad.size / 2) <= 50);
    }
    printf("%-24s %s: D-pad (%.0f, %.0f) %.0f, A (%.0f, %.0f) %.0f, ZR (%.0f, %.0f), ZL (%.0f, %.0f), - (%.0f, %.0f), + (%.0f, %.0f), toggle (%.0f, %.0f)\n",
           d->name, p.tablet ? "tablet" : "phone", p.dpad.x, p.dpad.y, p.dpad.size,
           p.buttons[SS_PAD_A].x, p.buttons[SS_PAD_A].y, p.buttons[SS_PAD_A].size,
           p.buttons[SS_PAD_ZR].x, p.buttons[SS_PAD_ZR].y, p.buttons[SS_PAD_ZL].x, p.buttons[SS_PAD_ZL].y,
           p.buttons[SS_PAD_MINUS].x, p.buttons[SS_PAD_MINUS].y, p.buttons[SS_PAD_PLUS].x, p.buttons[SS_PAD_PLUS].y, p.pad_toggle.x, p.pad_toggle.y);
}

static unsigned at_angle(double degrees, double distance, unsigned previous) {
    double a = degrees * atan(1.0) / 45;
    return ss_dpad_directions(cos(a) * distance, -sin(a) * distance, 17, previous);
}

/* The D-pad has 8 directions of 45 degrees each. Up is a negative y in view points. */
static void check_dpad(void) {
    const unsigned R = SS_DPAD_RIGHT, U = SS_DPAD_UP, L = SS_DPAD_LEFT, D = SS_DPAD_DOWN;
    const unsigned expected[8] = {R, R | U, U, U | L, L, L | D, D, D | R};
    /* The bits match the Switch D-pad buttons 12 (left) to 15 (down). */
    assert(L == 1u << (12 - 12) && U == 1u << (13 - 12) && R == 1u << (14 - 12) && D == 1u << (15 - 12));
    assert(ss_dpad_directions(0, -60, 17, 0) == U && ss_dpad_directions(60, 0, 17, 0) == R);
    assert(ss_dpad_directions(-40, 40, 17, 0) == (L | D));
    int count[8] = {0};
    for (int tenth = 0; tenth < 3600; ++tenth) {
        unsigned dirs = at_angle(tenth / 10.0 + 0.05, 50, 0);
        int i = 0;
        while (i < 8 && expected[i] != dirs) ++i;
        assert(i < 8);
        /* Each sector is centered on its direction. */
        double off = remainder(tenth / 10.0 + 0.05 - 45 * i, 360);
        assert(fabs(off) < 22.5);
        ++count[i];
    }
    for (int i = 0; i < 8; ++i) assert(count[i] == 450);
    for (int i = 0; i < 8; ++i) {
        /* The center and the edges between sectors. */
        assert(at_angle(45 * i, 16, 0) == 0 && at_angle(45 * i, 16, expected[i]) == 0);
        assert(at_angle(45 * i, 18, 0) == expected[i] && at_angle(45 * i, 200, 0) == expected[i]);
        assert(at_angle(45 * i + 22, 50, 0) == expected[i] && at_angle(45 * i - 22, 50, 0) == expected[i]);
        assert(at_angle(45 * i + 23, 50, 0) == expected[(i + 1) % 8]);
        assert(at_angle(45 * i - 23, 50, 0) == expected[(i + 7) % 8]);
        /* A held direction keeps 5 more degrees, then the next one takes over. */
        assert(at_angle(45 * i + 27, 50, expected[i]) == expected[i]);
        assert(at_angle(45 * i - 27, 50, expected[i]) == expected[i]);
        assert(at_angle(45 * i + 28, 50, expected[i]) == expected[(i + 1) % 8]);
        assert(at_angle(45 * i - 28, 50, expected[i]) == expected[(i + 7) % 8]);
        /* A held direction that is far from the touch has no effect. */
        assert(at_angle(45 * i + 180, 50, expected[i]) == expected[(i + 4) % 8]);
    }
}

int main(void) {
    for (size_t i = 0; i < sizeof devices / sizeof devices[0]; ++i) check_device(&devices[i]);
    check_dpad();
    puts("PASS: touch layout fits phones, tablets, and a small iPad window without overlap or HUD cover; - and + take the outer upper corners and the pad toggle stays clear; the D-pad has 8 equal sectors");
    return 0;
}
