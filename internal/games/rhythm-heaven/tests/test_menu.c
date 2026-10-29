#include "rh_menu_logic.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    /* The map takes touches only in input state 1; the selector (2), transitions (0), and other values do not. */
    assert(rh_map_input_ready(1));
    const int blocked[] = {0, 2, -1, 3, 99, INT_MIN, INT_MAX};
    for (unsigned i = 0; i < sizeof blocked / sizeof blocked[0]; ++i) assert(!rh_map_input_ready(blocked[i]));
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
