#include "ss_pointer.h"
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static ss_pointer_frame current;
static uint64_t generation, published_at;
static _Atomic uint64_t wait_hold = SS_POINTER_WAIT_HOLD_NS;
static ss_pointer_event queue[32];
static unsigned queued;

uint64_t ss_pointer_time_ns(void) {
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (uint64_t)value.tv_sec * 1000000000ull + value.tv_nsec;
}
uint64_t ss_pointer_publish(const ss_pointer_frame *frame) {
    uint64_t now = ss_pointer_time_ns();
    pthread_mutex_lock(&lock);
    if (frame->mode == SS_POINTER_HINTS && (current.mode == SS_POINTER_MENU || current.mode == SS_POINTER_MAP) &&
        now - published_at < 40000000ull) { pthread_mutex_unlock(&lock); return 0; }
    if (current.owner != frame->owner || current.mode != frame->mode || now - published_at > 250000000ull) {
        ++generation;
        queued = 0;
    }
    current = *frame;
    if (current.count < 0) current.count = 0;
    if (current.count > SS_POINTER_MAX_TARGETS) current.count = SS_POINTER_MAX_TARGETS;
    current.token = generation;
    published_at = now;
    uint64_t token = generation;
    pthread_mutex_unlock(&lock);
    return token;
}
int ss_pointer_read(ss_pointer_frame *frame) {
    uint64_t now = ss_pointer_time_ns();
    pthread_mutex_lock(&lock);
    int valid = current.mode && now - published_at <= 250000000ull;
    *frame = valid ? current : (ss_pointer_frame){0};
    pthread_mutex_unlock(&lock);
    return valid;
}
int ss_pointer_controls(void) {
    uint64_t now = ss_pointer_time_ns();
    pthread_mutex_lock(&lock);
    uint64_t age = now - published_at;
    int mode = published_at ? current.mode : SS_POINTER_NONE, result = SS_CONTROLS_SHOW;   /* no frame: the default */
    if (mode == SS_POINTER_WAIT) result = age <= wait_hold ? SS_CONTROLS_HIDE : SS_CONTROLS_SHOW;
    else if (mode != SS_POINTER_NONE && age > 250000000ull && age <= SS_POINTER_GAP_HOLD_NS) result = SS_CONTROLS_KEEP;
    pthread_mutex_unlock(&lock);
    return result;
}
void ss_pointer_set_wait_hold(uint64_t ns) { wait_hold = ns; }
void ss_pointer_clear(void) {
    pthread_mutex_lock(&lock);
    current = (ss_pointer_frame){0};
    ++generation;
    queued = 0;
    published_at = 0;
    pthread_mutex_unlock(&lock);
}
void ss_pointer_push(ss_pointer_event event) {
    event.time = ss_pointer_time_ns();
    if (!isfinite(event.x) || !isfinite(event.y) || event.type < SS_POINTER_DOWN || event.type > SS_POINTER_CANCEL) return;
    pthread_mutex_lock(&lock);
    if (event.token == current.token && current.mode) {
        /* Keep reversals. A drag that returns to its start must not become a tap. */
        if (queued < sizeof queue / sizeof queue[0]) queue[queued++] = event;
        else { queued = 1; queue[0] = (ss_pointer_event){event.token, SS_POINTER_CANCEL, event.x, event.y, event.time}; }
    }
    pthread_mutex_unlock(&lock);
}
int ss_pointer_take(uint64_t token, ss_pointer_event *event) {
    pthread_mutex_lock(&lock);
    int found = 0;
    while (queued) {
        ss_pointer_event value = queue[0];
        --queued;
        memmove(queue, queue + 1, queued * sizeof queue[0]);
        if (value.token == token) { *event = value; found = 1; break; }
    }
    pthread_mutex_unlock(&lock);
    return found;
}
int ss_pointer_contains(ss_pointer_rect rect, float x, float y) {
    return isfinite(x) && isfinite(y) && rect.width > 0 && rect.height > 0 &&
           x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}
int ss_pointer_hit(const ss_pointer_frame *frame, float x, float y) {
    for (int i = frame->count - 1; i >= 0; --i)
        if (ss_pointer_contains(frame->targets[i].rect, x, y)) return i;
    return -1;
}
int ss_pointer_location(double width, double height, double x, double y, float *game_x, float *game_y) {
    if (!isfinite(width) || !isfinite(height) || !isfinite(x) || !isfinite(y) || width <= 0 || height <= 0) return 0;
    double scale = fmin(width / 1920, height / 1080);
    double w = 1920 * scale, h = 1080 * scale;
    if (fabs((width / height) / (1920.0 / 1080) - 1) < 0.01) { w = width; h = height; }
    double left = (width - w) / 2, top = (height - h) / 2;
    *game_x = (float)((x - left) * 1920 / w);
    *game_y = (float)((y - top) * 1080 / h);
    return x >= left && y >= top && x < left + w && y < top + h;
}
ss_pointer_action ss_pointer_track(ss_pointer_gesture *gesture, ss_pointer_event event) {
    ss_pointer_action action = {.x = event.x, .y = event.y};
    if (event.type == SS_POINTER_DOWN) {
        *gesture = (ss_pointer_gesture){event.token, event.x, event.y, event.x, event.y, 1, 0};
        return action;
    }
    if (!gesture->down || event.token != gesture->token || event.type == SS_POINTER_CANCEL) {
        *gesture = (ss_pointer_gesture){0};
        return action;
    }
    float total_x = event.x - gesture->start_x, total_y = event.y - gesture->start_y;
    if (!gesture->dragging && total_x * total_x + total_y * total_y >= 16 * 16) {
        gesture->dragging = 1;
        action.dx = total_x; action.dy = total_y;
    } else {
        action.dx = event.x - gesture->last_x; action.dy = event.y - gesture->last_y;
    }
    if (gesture->dragging) action.flags |= SS_POINTER_PAN;
    if (event.type == SS_POINTER_UP) {
        action.flags |= SS_POINTER_FINISH;
        if (!gesture->dragging) action.flags |= SS_POINTER_TAP;
        gesture->down = 0;
    }
    gesture->last_x = event.x; gesture->last_y = event.y;
    return action;
}
