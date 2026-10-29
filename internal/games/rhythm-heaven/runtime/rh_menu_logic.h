#ifndef RH_MENU_LOGIC_H
#define RH_MENU_LOGIC_H
#include <stdint.h>
/* The level map takes touches only in its native input state 1. */
int rh_map_input_ready(int input_state);
float rh_menu_scroll_limit(float value, int count, int slots, int low, int high, float spacing);
#endif
