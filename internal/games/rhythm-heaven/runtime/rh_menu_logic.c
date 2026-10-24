#include "rh_menu_logic.h"
#include <math.h>

int rh_map_input_ready(rh_map_tap *state, int input_state) {
    if (input_state == 1) return 1;
    *state = (rh_map_tap){0};
    return 0;
}
int rh_map_tap_activate(rh_map_tap *state, uint64_t token, uint64_t item, int selected) {
    int activate = state->armed && state->token == token && state->item == item && selected;
    *state = (rh_map_tap){token, item, !activate};
    return activate;
}
float rh_menu_scroll_limit(float value, int count, int slots, int low, int high, float spacing) {
    if (!isfinite(value) || !isfinite(spacing) || spacing <= 0 || count <= 0 || slots <= 0) return 0;
    float minimum = (-low - slots / 2) * spacing;
    float maximum = (count - 1 - high - slots / 2) * spacing;
    return fminf(fmaxf(value, minimum), fmaxf(minimum, maximum));
}
