#ifndef RH_MENU_LOGIC_H
#define RH_MENU_LOGIC_H
#include <stdint.h>
typedef struct { uint64_t token, item; int armed; } rh_map_tap;
int rh_map_tap_activate(rh_map_tap *state, uint64_t token, uint64_t item, int selected);
float rh_menu_scroll_limit(float value, int count, int slots, int low, int high, float spacing);

#define RH_MOTION_SAMPLE_COUNT 16
typedef struct { uint64_t time; float x, y; } rh_motion_sample;
typedef struct { rh_motion_sample sample[RH_MOTION_SAMPLE_COUNT]; unsigned count, next; } rh_motion;

void rh_motion_reset(rh_motion *motion);
void rh_motion_add(rh_motion *motion, uint64_t time, float x, float y);
/* Use the samples in the 100 ms window before release. */
/* Return 1 and set vx and vy when the window has a time span. */
int rh_motion_velocity(const rh_motion *motion, uint64_t release, float *vx, float *vy);

typedef struct { float distance, velocity; } rh_inertia_step_result;
/* Decay a speed over elapsed nanoseconds. Distance is the exact integral. */
rh_inertia_step_result rh_inertia_step(float velocity, uint64_t elapsed, float time_constant);
#endif
