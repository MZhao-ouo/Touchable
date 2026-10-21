#include "rh_menu_logic.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static int near(float value, float expected, float tolerance) {
    return fabsf(value - expected) <= tolerance;
}

int main(void) {
    rh_map_tap state = {0};
    /* An already selected level still needs the first touch to arm entry. */
    assert(!rh_map_tap_activate(&state, 1, 7, 1));
    assert(rh_map_tap_activate(&state, 1, 7, 1));
    assert(!rh_map_tap_activate(&state, 1, 7, 1));
    assert(!rh_map_tap_activate(&state, 1, 8, 0));
    assert(rh_map_tap_activate(&state, 1, 8, 1));
    assert(!rh_map_tap_activate(&state, 1, 9, 0));
    /* A controller move or a different scene must not start a level. */
    assert(!rh_map_tap_activate(&state, 1, 9, 0));
    assert(!rh_map_tap_activate(&state, 2, 9, 1));
    state.armed = 0;  /* drag or cancellation */
    assert(!rh_map_tap_activate(&state, 2, 9, 1));
    assert(rh_map_tap_activate(&state, 2, 9, 1));
    /* Start-menu limits use the observed 9 items, 13 slots and 84-pixel spacing. */
    assert(rh_menu_scroll_limit(-500, 9, 13, -2, 2, 84) == -336);
    assert(rh_menu_scroll_limit(500, 9, 13, -2, 2, 84) == 0);
    assert(rh_menu_scroll_limit(-123, 9, 13, -2, 2, 84) == -123);
    assert(isfinite(rh_menu_scroll_limit(NAN, 9, 13, -2, 2, 84)));
    /* The archive category list has 8 items, 13 slots and 3 visible rows. */
    assert(rh_menu_scroll_limit(-1000, 8, 13, -1, 1, 84) == -420);
    assert(rh_menu_scroll_limit(1000, 8, 13, -1, 1, 84) == 0);
    assert(rh_menu_scroll_limit(-252, 8, 13, -1, 1, 84) == -252);

    /* A steady 1200 px/s drag gives the same velocity. */
    rh_motion motion = {0};
    uint64_t time = 1000000000ull;
    float vx = 0, vy = 0;
    rh_motion_add(&motion, time, 100, 200);
    rh_motion_add(&motion, time + 16666667, 120, 180);
    rh_motion_add(&motion, time + 33333333, 140, 160);
    rh_motion_add(&motion, time + 50000000, 160, 140);
    assert(rh_motion_velocity(&motion, time + 50000000, &vx, &vy));
    assert(near(vx, 1200, 1) && near(vy, -1200, 1));

    /* A hold before release lowers the speed. */
    rh_motion_reset(&motion);
    rh_motion_add(&motion, time, 0, 0);
    rh_motion_add(&motion, time + 16666667, 20, 0);
    rh_motion_add(&motion, time + 33333333, 40, 0);
    rh_motion_add(&motion, time + 66666667, 40, 0);
    rh_motion_add(&motion, time + 100000000, 40, 0);
    assert(rh_motion_velocity(&motion, time + 100000000, &vx, &vy));
    assert(vx > 0 && vx < 900 && near(vy, 0, 1));

    /* Samples older than 100 ms do not count. */
    rh_motion_reset(&motion);
    rh_motion_add(&motion, time, 0, 0);
    rh_motion_add(&motion, time + 50000000, 1000, 0);
    assert(!rh_motion_velocity(&motion, time + 200000000, &vx, &vy));
    assert(vx == 0 && vy == 0);
    rh_motion_reset(&motion);
    rh_motion_add(&motion, time, 5, 5);
    assert(!rh_motion_velocity(&motion, time, &vx, &vy));

    /* The distance is the exact integral of the decay. */
    rh_inertia_step_result step = rh_inertia_step(1000, 0, 0.35f);
    assert(step.distance == 0 && step.velocity == 1000);
    rh_inertia_step_result full = rh_inertia_step(1000, 350000000, 0.35f);
    assert(near(full.velocity, 1000 / expf(1), 0.01));
    assert(near(full.distance, 350 * (1 - expf(-1)), 0.01));
    rh_inertia_step_result first = rh_inertia_step(1000, 175000000, 0.35f);
    rh_inertia_step_result second = rh_inertia_step(first.velocity, 175000000, 0.35f);
    assert(near(first.distance + second.distance, full.distance, 0.01));
    assert(near(second.velocity, full.velocity, 0.01));
    step = rh_inertia_step(-1000, 350000000, 0.35f);
    assert(step.distance < 0 && step.velocity < 0);
    puts("test_menu: ok");
    return 0;
}
