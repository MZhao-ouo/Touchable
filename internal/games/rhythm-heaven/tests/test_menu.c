#include "rh_menu_logic.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    rh_map_tap state = {0};
    assert(rh_map_input_ready(&state, 1));
    assert(!rh_map_tap_activate(&state, 1, 7, 1));
    assert(rh_map_input_ready(&state, 1));
    assert(rh_map_tap_activate(&state, 1, 7, 1));
    state = (rh_map_tap){17, 9, 1};
    assert(rh_map_input_ready(&state, 1));
    assert(state.token == 17 && state.item == 9 && state.armed);
    const int blocked_states[] = {0, 2, -1, 3, 99, INT_MIN, INT_MAX};
    for (unsigned index = 0; index < sizeof blocked_states / sizeof blocked_states[0]; ++index) {
        state = (rh_map_tap){1, 7, 1};
        assert(!rh_map_input_ready(&state, blocked_states[index]));
        assert(!state.armed && !state.token && !state.item);
        assert(rh_map_input_ready(&state, 1));
        assert(!rh_map_tap_activate(&state, 1, 7, 1));
        assert(rh_map_tap_activate(&state, 1, 7, 1));
    }
    state = (rh_map_tap){17, 9, 1};
    assert(!rh_map_input_ready(&state, 0));
    assert(!rh_map_input_ready(&state, 0));
    assert(!rh_map_input_ready(&state, 2));
    assert(!state.token && !state.item && !state.armed);
    state = (rh_map_tap){0};
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
    puts("test_menu: ok");
}
