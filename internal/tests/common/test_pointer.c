/* Build with runtime/common/ss_pointer.c and -pthread. */
#include "ss_pointer.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

static void close_to(float actual, float expected) { assert(fabsf(actual - expected) < 0.01f); }
static ss_pointer_event sample(uint64_t token, int type, float x, float y) { return (ss_pointer_event){token, type, x, y, 0}; }

int main(void) {
    float x = -1, y = -1;
    assert(ss_pointer_location(2868, 1320, 1434, 660, &x, &y));
    close_to(x, 960); close_to(y, 540);
    assert(!ss_pointer_location(2868, 1320, 100, 660, &x, &y));
    assert(x < 0);
    assert(!ss_pointer_location(2868, 1320, 2768, 660, &x, &y));
    assert(x > 1920);
    assert(ss_pointer_location(1920, 1080, 510, 400, &x, &y));
    close_to(x, 510); close_to(y, 400);
    assert(!ss_pointer_location(0, 1080, 0, 0, &x, &y));
    assert(!ss_pointer_location(NAN, 1080, 0, 0, &x, &y));

    ss_pointer_gesture gesture = {0};
    ss_pointer_track(&gesture, sample(1, SS_POINTER_DOWN, 500, 600));
    assert(!ss_pointer_track(&gesture, sample(1, SS_POINTER_MOVE, 505, 604)).flags);
    ss_pointer_action action = ss_pointer_track(&gesture, sample(1, SS_POINTER_UP, 506, 603));
    assert(action.flags == (SS_POINTER_TAP | SS_POINTER_FINISH));
    assert(!ss_pointer_track(&gesture, sample(1, SS_POINTER_UP, 506, 603)).flags);
    ss_pointer_track(&gesture, sample(2, SS_POINTER_DOWN, 900, 800));
    action = ss_pointer_track(&gesture, sample(2, SS_POINTER_MOVE, 900, 700));
    assert(action.flags == SS_POINTER_PAN); close_to(action.dy, -100);
    action = ss_pointer_track(&gesture, sample(2, SS_POINTER_MOVE, 900, 650));
    close_to(action.dy, -50);
    action = ss_pointer_track(&gesture, sample(2, SS_POINTER_UP, 900, 800));
    assert(action.flags == (SS_POINTER_PAN | SS_POINTER_FINISH));
    ss_pointer_track(&gesture, sample(3, SS_POINTER_DOWN, 500, 600));
    ss_pointer_track(&gesture, sample(3, SS_POINTER_CANCEL, 500, 600));
    assert(!ss_pointer_track(&gesture, sample(3, SS_POINTER_UP, 500, 600)).flags);
    ss_pointer_track(&gesture, sample(4, SS_POINTER_DOWN, 500, 600));
    assert(!ss_pointer_track(&gesture, sample(5, SS_POINTER_UP, 500, 600)).flags);

    ss_pointer_frame frame = {.owner = 10, .mode = SS_POINTER_MENU, .count = 2,
        .targets = {{.id = 1, .rect = {100,100,200,80}}, {.id = 2, .rect = {100,200,200,80}}}};
    uint64_t token = ss_pointer_publish(&frame);
    assert(ss_pointer_hit(&frame, 150, 225) == 1);
    assert(ss_pointer_hit(&frame, 150, 185) == -1);
    assert(ss_pointer_hit(&frame, NAN, 225) == -1);
    ss_pointer_push(sample(token, SS_POINTER_DOWN, 150, 225));
    ss_pointer_push(sample(token, SS_POINTER_MOVE, 150, 125));
    ss_pointer_push(sample(token, SS_POINTER_MOVE, 150, 225));
    ss_pointer_push(sample(token, SS_POINTER_UP, 150, 126));
    ss_pointer_event event;
    gesture = (ss_pointer_gesture){0};
    while (ss_pointer_take(token, &event)) {
        action = ss_pointer_track(&gesture, event);
        assert(!(action.flags & SS_POINTER_TAP));
    }
    for (int i = 0; i < 100; ++i) ss_pointer_push(sample(token, SS_POINTER_MOVE, 150, 225 - i));
    assert(ss_pointer_take(token, &event) && event.type == SS_POINTER_CANCEL);
    while (ss_pointer_take(token, &event)) assert(!(ss_pointer_track(&gesture, event).flags & SS_POINTER_TAP));
    assert(!ss_pointer_take(token, &event));
    ss_pointer_push(sample(token, SS_POINTER_DOWN, 150, 225));
    frame.owner = 11;
    uint64_t next = ss_pointer_publish(&frame);
    assert(next != token);
    ss_pointer_push(sample(token, SS_POINTER_UP, 150, 225));
    assert(!ss_pointer_take(next, &event));
    ss_pointer_frame hints = {.owner = 12, .mode = SS_POINTER_HINTS, .tutorial_buttons = 1ull << 3};
    assert(ss_pointer_publish(&hints) == 0);
    assert(ss_pointer_read(&frame) && frame.owner == 11);
    ss_pointer_clear();
    assert(!ss_pointer_read(&frame));
    uint64_t hints_token = ss_pointer_publish(&hints);
    assert(hints_token);
    assert(ss_pointer_read(&frame) && frame.mode == SS_POINTER_HINTS);
    assert(!frame.count && frame.tutorial_buttons == (1ull << 3));
    ss_pointer_push(sample(hints_token, SS_POINTER_DOWN, 150, 225));
    hints.count = 1;
    hints.targets[0] = (ss_pointer_target){.id = 1, .rect = {100,200,200,80}};
    assert(ss_pointer_publish(&hints) == hints_token);
    assert(ss_pointer_read(&frame) && frame.count == 1 && frame.tutorial_buttons == (1ull << 3));
    hints.count = 0;
    hints.tutorial_buttons = 0;
    assert(ss_pointer_publish(&hints) == hints_token);
    assert(ss_pointer_read(&frame) && !frame.count && !frame.tutorial_buttons);
    assert(ss_pointer_take(hints_token, &event) && event.type == SS_POINTER_DOWN);
    struct timespec delay = {.tv_nsec = 300000000};
    nanosleep(&delay, NULL);
    assert(!ss_pointer_read(&frame));
    assert(ss_pointer_publish(&hints) != hints_token);
    ss_pointer_clear();

    /* Touch controls: no frame shows the default controls. */
    assert(ss_pointer_controls() == SS_CONTROLS_SHOW);
    ss_pointer_frame none = {.owner = 1, .mode = SS_POINTER_NONE};
    ss_pointer_publish(&none);
    assert(ss_pointer_controls() == SS_CONTROLS_SHOW);
    nanosleep(&delay, NULL);
    assert(ss_pointer_controls() == SS_CONTROLS_SHOW);
    /* A WAIT frame hides the controls, also after a load hitch. */
    ss_pointer_frame wait = {.owner = 1, .mode = SS_POINTER_WAIT};
    uint64_t wait_token = ss_pointer_publish(&wait);
    assert(wait_token && ss_pointer_read(&frame) && frame.mode == SS_POINTER_WAIT && !frame.count);
    assert(ss_pointer_controls() == SS_CONTROLS_HIDE);
    nanosleep(&delay, NULL);
    assert(!ss_pointer_read(&frame) && ss_pointer_controls() == SS_CONTROLS_HIDE);
    /* A menu frame replaces WAIT at once. A gap after the menu keeps the controls. */
    ss_pointer_frame menu = {.owner = 2, .mode = SS_POINTER_MENU};
    assert(ss_pointer_publish(&menu) != wait_token);
    assert(ss_pointer_controls() == SS_CONTROLS_SHOW);
    nanosleep(&delay, NULL);
    assert(ss_pointer_controls() == SS_CONTROLS_KEEP);
    struct timespec gap = {.tv_sec = 2, .tv_nsec = 300000000};
    nanosleep(&gap, NULL);
    assert(ss_pointer_controls() == SS_CONTROLS_SHOW);
    /* A stopped WAIT frame expires. Then the default controls show again. */
    ss_pointer_publish(&wait);
    struct timespec hold = {.tv_sec = 2, .tv_nsec = 100000000};
    nanosleep(&hold, NULL);
    assert(ss_pointer_controls() == SS_CONTROLS_SHOW);
    /* A game that publishes WAIT in every frame sets a longer hold for load hitches. */
    ss_pointer_set_wait_hold(30000000000ull);
    ss_pointer_publish(&wait);
    nanosleep(&hold, NULL);
    assert(ss_pointer_controls() == SS_CONTROLS_HIDE);
    ss_pointer_set_wait_hold(SS_POINTER_WAIT_HOLD_NS);
    assert(ss_pointer_controls() == SS_CONTROLS_SHOW);
    ss_pointer_clear();
    assert(ss_pointer_controls() == SS_CONTROLS_SHOW);
    puts("test_pointer: ok");
}
