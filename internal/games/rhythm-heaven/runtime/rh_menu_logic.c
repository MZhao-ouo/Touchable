#include "rh_menu_logic.h"
#include <math.h>

#define MOTION_WINDOW_NS 100000000ull
#define MOTION_MIN_SPAN_NS 8000000ull

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
void rh_motion_reset(rh_motion *motion) {
    if (motion) *motion = (rh_motion){0};
}
void rh_motion_add(rh_motion *motion, uint64_t time, float x, float y) {
    if (!motion || !isfinite(x) || !isfinite(y)) return;
    motion->sample[motion->next] = (rh_motion_sample){time, x, y};
    motion->next = (motion->next + 1) % RH_MOTION_SAMPLE_COUNT;
    if (motion->count < RH_MOTION_SAMPLE_COUNT) ++motion->count;
}
/* Least-squares slope. A hold before release lowers the result. */
int rh_motion_velocity(const rh_motion *motion, uint64_t release, float *vx, float *vy) {
    if (vx) *vx = 0;
    if (vy) *vy = 0;
    if (!motion || !motion->count) return 0;
    uint64_t oldest = release > MOTION_WINDOW_NS ? release - MOTION_WINDOW_NS : 0;
    uint64_t first_time = 0, last_time = 0;
    int have = 0;
    double count = 0, sum_t = 0, sum_x = 0, sum_y = 0, sum_tt = 0, sum_tx = 0, sum_ty = 0;
    unsigned first = (motion->next + RH_MOTION_SAMPLE_COUNT - motion->count) % RH_MOTION_SAMPLE_COUNT;
    for (unsigned i = 0; i < motion->count; ++i) {
        rh_motion_sample sample = motion->sample[(first + i) % RH_MOTION_SAMPLE_COUNT];
        if (sample.time < oldest || sample.time > release) continue;
        if (!have) { first_time = sample.time; have = 1; }
        last_time = sample.time;
        double t = (double)(sample.time - first_time) / 1e9;
        count += 1;
        sum_t += t; sum_x += sample.x; sum_y += sample.y;
        sum_tt += t * t; sum_tx += t * sample.x; sum_ty += t * sample.y;
    }
    if (count < 2 || last_time - first_time < MOTION_MIN_SPAN_NS) return 0;
    double denominator = count * sum_tt - sum_t * sum_t;
    if (denominator <= 0) return 0;
    if (vx) *vx = (float)((count * sum_tx - sum_t * sum_x) / denominator);
    if (vy) *vy = (float)((count * sum_ty - sum_t * sum_y) / denominator);
    return 1;
}
rh_inertia_step_result rh_inertia_step(float velocity, uint64_t elapsed, float time_constant) {
    rh_inertia_step_result result = {0, velocity};
    if (!isfinite(velocity) || !isfinite(time_constant) || time_constant <= 0 || !elapsed) return result;
    double seconds = (double)elapsed / 1e9;
    double decay = exp(-seconds / (double)time_constant);
    result.distance = (float)((double)velocity * (double)time_constant * (1.0 - decay));
    result.velocity = (float)((double)velocity * decay);
    return result;
}
