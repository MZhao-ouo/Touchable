#ifndef RH_MENU_LOGIC_H
#define RH_MENU_LOGIC_H
#include <stdint.h>
typedef struct { uint64_t token, item; int armed; } rh_map_tap;
int rh_map_input_ready(rh_map_tap *state, int input_state);
int rh_map_tap_activate(rh_map_tap *state, uint64_t token, uint64_t item, int selected);
float rh_menu_scroll_limit(float value, int count, int slots, int low, int high, float spacing);
#endif
