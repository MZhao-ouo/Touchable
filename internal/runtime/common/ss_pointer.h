/* Screen coordinates use the displayed 1920 x 1080 game picture. */
#ifndef SS_POINTER_H
#define SS_POINTER_H
#include <stdint.h>

#define SS_POINTER_MAX_TARGETS 192
/* SS_POINTER_WAIT: the screen takes no input, for example a loading screen. The phone hides its touch controls.
   SS_POINTER_CUTSCENE: a cutscene. The phone hides the pad and shows a button that shows it. A button target
   in the frame is the skip button. */
enum { SS_POINTER_NONE, SS_POINTER_MENU, SS_POINTER_MAP, SS_POINTER_HINTS, SS_POINTER_WAIT, SS_POINTER_CUTSCENE };
enum { SS_POINTER_DOWN, SS_POINTER_MOVE, SS_POINTER_UP, SS_POINTER_CANCEL };
enum { SS_POINTER_TAP = 1, SS_POINTER_PAN = 2, SS_POINTER_FINISH = 4 };
enum { SS_POINTER_SELECTED = 1, SS_POINTER_BUTTON = 2 };

typedef struct { float x, y, width, height; } ss_pointer_rect;
typedef struct {
    uint64_t id;
    ss_pointer_rect rect;
    unsigned flags;
    int button;
} ss_pointer_target;
typedef struct {
    uint64_t owner, token;
    /* Available tutorial actions. This mask does not follow label visibility or animation. */
    uint64_t tutorial_buttons;
    int mode, count;
    ss_pointer_rect scroll;
    ss_pointer_target targets[SS_POINTER_MAX_TARGETS];
} ss_pointer_frame;
/* ss_pointer_push sets time to the monotonic push time in nanoseconds. */
typedef struct { uint64_t token; int type; float x, y; uint64_t time; } ss_pointer_event;
typedef struct {
    uint64_t token;
    float start_x, start_y, last_x, last_y;
    int down, dragging;
} ss_pointer_gesture;
typedef struct { int flags; float x, y, dx, dy; } ss_pointer_action;

/* A game publishes a frame on its input thread. The phone reads a copy. */
uint64_t ss_pointer_time_ns(void);
uint64_t ss_pointer_publish(const ss_pointer_frame *frame);
int ss_pointer_read(ss_pointer_frame *frame);
void ss_pointer_clear(void);
/* What the phone does with its touch controls. SHOW: apply the rules of the current frame, or show the
   default controls when no frame exists. HIDE: a WAIT frame is current, or it stopped less than
   the WAIT hold ago (ss_pointer_set_wait_hold). KEEP: another frame stopped less than SS_POINTER_GAP_HOLD_NS ago.
   A fade or a load hitch then does not change the controls. */
enum { SS_CONTROLS_SHOW, SS_CONTROLS_HIDE, SS_CONTROLS_KEEP };
#define SS_POINTER_WAIT_HOLD_NS 2000000000ull
#define SS_POINTER_GAP_HOLD_NS 2500000000ull   /* a Rhythm Heaven menu fades out for up to about 1.6 s */
int ss_pointer_controls(void);
/* Set how long a stopped WAIT frame keeps the controls hidden. A game that publishes WAIT in every
   frame sets a long time: a stopped WAIT frame then means a load hitch. Default: SS_POINTER_WAIT_HOLD_NS. */
void ss_pointer_set_wait_hold(uint64_t ns);
void ss_pointer_push(ss_pointer_event event);
int ss_pointer_take(uint64_t token, ss_pointer_event *event);
int ss_pointer_hit(const ss_pointer_frame *frame, float x, float y);
int ss_pointer_contains(ss_pointer_rect rect, float x, float y);
int ss_pointer_location(double width, double height, double x, double y, float *game_x, float *game_y);
ss_pointer_action ss_pointer_track(ss_pointer_gesture *gesture, ss_pointer_event event);
#endif
