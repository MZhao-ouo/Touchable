#include "rh_menu_logic.h"
#include <math.h>

int rh_map_input_ready(int input_state) { return input_state == 1; }

float rh_menu_scroll_limit(float value, int count, int slots, int low, int high, float spacing) {
    if (!isfinite(value) || !isfinite(spacing) || spacing <= 0 || count <= 0 || slots <= 0) return 0;
    float minimum = (-low - slots / 2) * spacing;
    float maximum = (count - 1 - high - slots / 2) * spacing;
    return fminf(fmaxf(value, minimum), fmaxf(minimum, maximum));
}
