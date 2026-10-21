/* Menu layout for main Build ID 03ff4a3bec96bde3. Check its code and vtables before use. */
#include "rh_menu.h"
#include "rh_menu_logic.h"
#include "ss_pointer.h"
#include "ss_touch.h"
#include <math.h>
#include <time.h>
#include <unistd.h>

static uint8_t *base;
static const module_t *module;
static int trace;
static ss_pointer_gesture gesture;
static rh_map_tap map_tap;
static int can_drag;
static uint64_t traced_owner;
static uint64_t traced_hints;

enum { RH_GLIDE_NONE, RH_GLIDE_LIST, RH_GLIDE_MAP };
#define RH_GLIDE_START_SPEED 700.0f
#define RH_GLIDE_STOP_SPEED 40.0f
#define RH_GLIDE_MAX_SPEED 6000.0f
#define RH_GLIDE_LIST_TAU 0.35f
#define RH_GLIDE_MAP_TAU 0.30f
#define RH_GLIDE_SNAP_TAU 0.045f
#define RH_GLIDE_MAX_STEP_NS 50000000ull
#define RH_GLIDE_MAX_GAP_NS 100000000ull

typedef struct {
    int surface;
    uint64_t owner, token, time;
    float vx, vy;
    int snapping;
    float target;
} rh_glide_state;

static rh_motion motion;
static rh_glide_state glide;

static void pointer_snapshot(const ss_pointer_frame *frame, const char *path) {
    char temporary[1024]; snprintf(temporary, sizeof temporary, "%s.tmp", path);
    FILE *file = fopen(temporary, "w");
    if (!file) return;
    fprintf(file, "{\"mode\":%d,\"owner\":%llu,\"token\":%llu,\"tutorial_buttons\":%llu,\"targets\":[", frame->mode,
            (unsigned long long)frame->owner, (unsigned long long)frame->token, (unsigned long long)frame->tutorial_buttons);
    for (int i = 0; i < frame->count; ++i) {
        const ss_pointer_target *t = &frame->targets[i];
        fprintf(file, "%s{\"id\":%llu,\"x\":%.3f,\"y\":%.3f,\"width\":%.3f,\"height\":%.3f,\"flags\":%u,\"button\":%d}",
                i ? "," : "", (unsigned long long)t->id, t->rect.x,t->rect.y,t->rect.width,t->rect.height,t->flags,t->button);
    }
    fputs("]}\n", file); fclose(file); rename(temporary, path);
}

/* Development input uses the same event queue as UIKit. Normal runs leave it unset. */
static void *pointer_file(void *argument) {
    const char *path = argument;
    unsigned long long previous = 0;
    uint64_t held = 0, held_token = 0;
    const char *snapshot = getenv("RH_POINTER_SNAPSHOT"), *shots = getenv("RH_POINTER_SHOTS");
    unsigned ticks = 0;
    int controls = -1, controls_mode = -1;
    void ss_input_set(uint64_t, int32_t, int32_t);
    for (;;) {
        ss_pointer_frame active;
        if (trace) {   /* the phone's touch control decision, with the time of each change */
            int now_controls = ss_pointer_controls(), now_mode = ss_pointer_read(&active) ? active.mode : -1;
            if (now_controls != controls || now_mode != controls_mode) {
                static const char *const names[] = {"show", "hide", "keep"};
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                LOG("CONTROLS %lld.%03ld %s (frame mode %d)", (long long)now.tv_sec, now.tv_nsec / 1000000,
                    names[now_controls], now_mode);
                controls = now_controls; controls_mode = now_mode;
            }
        }
        if (held && (!ss_pointer_read(&active) || active.token != held_token)) { ss_input_set(0, 0, 0); held = 0; }
        if (snapshot && ++ticks % 20 == 0) {
            ss_pointer_frame frame;
            ss_pointer_read(&frame);
            pointer_snapshot(&frame, snapshot);
        }
        FILE *file = fopen(path, "r");
        if (file) {
            unsigned long long sequence; int type; float x, y;
            if (fscanf(file, "%llu %d %f %f", &sequence, &type, &x, &y) == 4 && sequence != previous) {
                previous = sequence;
                ss_pointer_frame frame;
                if (type == 5) {   /* a screenshot does not need a menu frame */
                    if (shots) {
                        char output[1024]; snprintf(output, sizeof output, "%s/touch-%llu.png", shots, sequence);
                        void dm_gfx_shot_at(long, const char *);
                        dm_gfx_shot_at(1, output);
                        LOG("POINTER screenshot %s", output);
                    }
                } else if (ss_pointer_read(&frame)) {
                    int hit = ss_pointer_hit(&frame, x, y);
                    if (type == SS_POINTER_DOWN && hit >= 0 && (frame.targets[hit].flags & SS_POINTER_BUTTON)) {
                        int bit = frame.targets[hit].button;
                        if (bit >= 0 && bit < 64) { held = 1ull << bit; held_token = frame.token; ss_input_set(held, 0, 0); }
                    } else if (held) {
                        if (type == SS_POINTER_UP || type == SS_POINTER_CANCEL) { ss_input_set(0, 0, 0); held = 0; }
                    } else ss_pointer_push((ss_pointer_event){frame.token, type, x, y, 0});
                    if (trace) LOG("POINTER test %llu type %d %.1f %.1f held %#llx", sequence, type, x,y,(unsigned long long)held);
                } else if (held) { ss_input_set(0, 0, 0); held = 0; }
            }
            fclose(file);
        }
        usleep(5000);
    }
}
static void start_pointer_file(void) {
    const char *name = getenv("RH_POINTER_FILE"), *home = getenv("HOME");
    if (!name || !*name) return;
    static char path[1024];
    if (*name == '/' || !home) snprintf(path, sizeof path, "%s", name);
    else snprintf(path, sizeof path, "%s/%s", home, name);
    pthread_t thread;
    if (pthread_create(&thread, NULL, pointer_file, path)) die("Cannot start pointer test input");
    pthread_detach(thread);
}

static void *ptr(const void *p, unsigned offset) { return *(void *const *)((const uint8_t *)p + offset); }
static int integer(const void *p, unsigned offset) { return *(const int *)((const uint8_t *)p + offset); }
static float real(const void *p, unsigned offset) { return *(const float *)((const uint8_t *)p + offset); }
static void put_int(void *p, unsigned offset, int value) { memcpy((uint8_t *)p + offset, &value, 4); }
static void put_real(void *p, unsigned offset, float value) { memcpy((uint8_t *)p + offset, &value, 4); }
static int sprite(const void *node) {
    if (!node) return 0;
    uintptr_t table = (uintptr_t)ptr(node, 0), start = (uintptr_t)base;
    return table >= start + module->data_off && table + 8 < start + module->patch_off &&
           *(void **)table == base + 0x4f7720;
}
static int point(void *root, const char *label, float *x, float *y) {
    return sprite(root) && ((int (*)(void *, float *, float *, const char *, int))(base + 0x4fa270))(root, x, y, label, 1) &&
           isfinite(*x) && isfinite(*y);
}
static int node_point(void *root, void *node, float *x, float *y) {
    return sprite(root) && sprite(node) &&
           ((int (*)(void *, float *, float *, void *, int))(base + 0x4fa3d0))(root, x, y, node, 1) && isfinite(*x) && isfinite(*y);
}
static ss_pointer_rect intersect(ss_pointer_rect a, ss_pointer_rect b) {
    float x = fmaxf(a.x, b.x), y = fmaxf(a.y, b.y);
    return (ss_pointer_rect){x, y, fmaxf(0, fminf(a.x + a.width, b.x + b.width) - x),
                                    fmaxf(0, fminf(a.y + a.height, b.y + b.height) - y)};
}
static const ss_pointer_rect picture = {0, 0, 1920, 1080};
static void target(ss_pointer_frame *frame, uint64_t id, float x, float y, float w, float h, unsigned flags, int button) {
    ss_pointer_rect rect = intersect((ss_pointer_rect){960 + x - w / 2, 540 - y - h / 2, w, h}, picture);
    if (!(flags & SS_POINTER_BUTTON) && frame->scroll.width > 0) rect = intersect(rect, frame->scroll);
    if (rect.width < 2 || rect.height < 2 || frame->count == SS_POINTER_MAX_TARGETS) return;
    frame->targets[frame->count++] = (ss_pointer_target){id, rect, flags, button};
}
static int menu_origin(void *list, float *x, float *y) {
    const char *labels[] = {"menu", "select", "list"};
    void *owner = ptr(list, 8);
    const char *anchor = NULL;
    /* Category lists bind to CATEGORY (1000) or ITEM (1001). Both anchors exist
       in the same scene. The inactive menu anchor can be off screen. */
    if (ptr(list, 0) == base + 0xd0a8f0 && ptr(list, 0x10) == (uint8_t *)owner + 0x3880) {
        int column = integer(list, 0x28);
        if (column != 1000 && column != 1001) return 0;
        anchor = column == 1000 ? "category" : "menu";
    }
    void *root = ptr(owner, 0x40);
    for (unsigned n = 0; root && n < 96; ++n, root = ptr(root, 0x98)) {
        if (anchor) {
            if (point(root, anchor, x, y)) return 1;
        } else for (unsigned i = 0; i < sizeof labels / sizeof labels[0]; ++i)
                if (point(root, labels[i], x, y)) return 1;
    }
    return 0;
}
static void trace_frame(const ss_pointer_frame *frame) {
    uint64_t key = frame->owner ^ (uint64_t)frame->mode << 48 ^ (uint64_t)frame->count << 40 ^ frame->tutorial_buttons << 32;
    for (int i = 0; i < frame->count; ++i) if (frame->targets[i].flags & SS_POINTER_SELECTED) key ^= frame->targets[i].id << 8;
    if (!trace || traced_owner == key) return;
    traced_owner = key;
    LOG("POINTER frame mode %d owner %#llx targets %d tutorial %#llx", frame->mode, (unsigned long long)frame->owner, frame->count,
        (unsigned long long)frame->tutorial_buttons);
    for (int i = 0; i < frame->count; ++i) {
        const ss_pointer_target *t = &frame->targets[i];
        LOG("POINTER target %llu %.1f %.1f %.1f %.1f flags %u button %d", (unsigned long long)t->id,
            t->rect.x,t->rect.y,t->rect.width,t->rect.height,t->flags,t->button);
    }
}
static void glide_stop(void) {
    if (trace && glide.surface) LOG("POINTER glide stop surface %d", glide.surface);
    glide = (rh_glide_state){0};
}
static void glide_start(int surface, uint64_t owner, uint64_t token, uint64_t time, float vx, float vy) {
    if (glide.surface) return;
    float speed = surface == RH_GLIDE_LIST ? vy : vx;
    if (!isfinite(speed) || fabsf(speed) < RH_GLIDE_START_SPEED) return;
    speed = fmaxf(-RH_GLIDE_MAX_SPEED, fminf(RH_GLIDE_MAX_SPEED, speed));
    glide = (rh_glide_state){surface, owner, token, time, surface == RH_GLIDE_MAP ? speed : 0,
                             surface == RH_GLIDE_LIST ? speed : 0, 0, 0};
    if (trace) LOG("POINTER glide start surface %d vx %.1f vy %.1f", surface, vx, vy);
}
static void reset_gesture(uint64_t token) {
    if (gesture.token != token) {
        gesture = (ss_pointer_gesture){0}; can_drag = 0;
        rh_motion_reset(&motion); glide_stop();
    }
}
static void list_apply_offset(void *list, float offset) {
    float spacing = real(list, 0x4c);
    if (!isfinite(spacing) || spacing == 0) return;
    offset = rh_menu_scroll_limit(offset, integer(list, 0x20), integer(list, 0x44),
                                  integer(list, 0x58), integer(list, 0x5c), spacing);
    put_real(list, 0x50, offset); put_real(list, 0x54, offset);
    put_int(list, 0x48, (int)lroundf(offset / spacing) + integer(list, 0x44) / 2);
    ((void (*)(void *))(base + 0x135ab0))(list);
}
static void glide_list_step(void *list) {
    uint64_t now = ss_pointer_time_ns();
    uint64_t elapsed = now > glide.time ? now - glide.time : 0;
    glide.time = now;
    if (elapsed > RH_GLIDE_MAX_GAP_NS) { glide_stop(); return; }
    if (elapsed > RH_GLIDE_MAX_STEP_NS) elapsed = RH_GLIDE_MAX_STEP_NS;
    float spacing = real(list, 0x4c);
    if (!isfinite(spacing) || fabsf(spacing) < 8) { glide_stop(); return; }
    float offset = real(list, 0x50);
    if (glide.snapping) {
        float next = offset + (glide.target - offset) * (1 - expf(-(float)elapsed / 1e9f / RH_GLIDE_SNAP_TAU));
        if (fabsf(glide.target - next) < 0.5f) next = glide.target;
        list_apply_offset(list, next);
        if (next == glide.target) glide_stop();
        return;
    }
    rh_inertia_step_result step = rh_inertia_step(glide.vy, elapsed, RH_GLIDE_LIST_TAU);
    glide.vy = step.velocity;
    float wanted = offset - step.distance;
    float clamped = rh_menu_scroll_limit(wanted, integer(list, 0x20), integer(list, 0x44),
                                         integer(list, 0x58), integer(list, 0x5c), spacing);
    int edge = fabsf(clamped - wanted) > 0.01f;
    list_apply_offset(list, clamped);
    if (!edge && fabsf(glide.vy) >= RH_GLIDE_STOP_SPEED) return;
    /* Stop on a row. The list uses a row index, not a free offset. */
    glide.snapping = 1;
    glide.target = rh_menu_scroll_limit(lroundf(real(list, 0x50) / spacing) * spacing, integer(list, 0x20),
                                        integer(list, 0x44), integer(list, 0x58), integer(list, 0x5c), spacing);
    if (trace) LOG("POINTER glide snap %.1f to %.1f", real(list, 0x50), glide.target);
}
static int list_frame(void *list, int scroll, ss_pointer_frame *frame) {
    int count = integer(list, 0x20), selected = integer(list, 0x24);
    if (count < 1 || count > 128 || ((uint8_t *)list)[0x30] != 1) return 0;
    *frame = (ss_pointer_frame){.owner = (uintptr_t)list, .mode = SS_POINTER_MENU};
    void *owner = ptr(list, 8), **items = ptr(list, 0x18);
    float ox = 0, oy = 0, spacing = scroll ? fabsf(real(list, 0x4c)) : 0;
    if (scroll) {
        if (!menu_origin(list, &ox, &oy) || spacing < 8 || spacing > 500) return 0;
        int rows = integer(list, 0x5c) - integer(list, 0x58) + 1;
        if (rows < 1 || rows > 20) return 0;
        float width = fmaxf(integer(items[0], 0x30) + 100, 200);
        frame->scroll = intersect((ss_pointer_rect){960 + ox - width / 2, 540 - oy - (rows + 1) * spacing / 2,
                                                   width, (rows + 1) * spacing}, picture);
    }
    for (int i = 0; i < count; ++i) {
        void *item = items[i];
        if (!item) continue;
        void *node = ptr(item, ((uint8_t *)item)[0x28] ? 0x18 : 0x10);
        /* Long lists retain hidden cloned rows. Their local positions can match
           visible rows. Do not let those clones take a visible row's tap. */
        if (!sprite(node) || ((uint8_t *)node)[0x84] != 1) continue;
        float x = 0, y = 0;
        int found = 0;
        if (scroll) found = node_point(ptr(list, 0x70), node, &x, &y);
        else {
            void *root = ptr(owner, 0x40);
            for (int n = 0; root && n < 96 && !found; ++n, root = ptr(root, 0x98)) found = node_point(root, node, &x, &y);
        }
        if (!found) continue;
        float width = fmaxf(integer(item, 0x30) + 48, 200);
        target(frame, i, x + ox, y + oy, width, scroll ? spacing : 84, i == selected ? SS_POINTER_SELECTED : 0, -1);
    }
    trace_frame(frame);
    return frame->count > 0;
}
static int list_input(void *list, void *pad, int scroll, uintptr_t original) {
    ss_pointer_frame frame;
    if (list_frame(list, scroll, &frame)) {
        frame.token = ss_pointer_publish(&frame);
        reset_gesture(frame.token);
        ss_pointer_event event;
        while (ss_pointer_take(frame.token, &event)) {
            if (event.type == SS_POINTER_DOWN) {
                glide_stop(); rh_motion_reset(&motion);
                can_drag = ss_pointer_contains(frame.scroll, event.x, event.y);
                if (can_drag) rh_motion_add(&motion, event.time, event.x, event.y);
            }
            if (event.type == SS_POINTER_CANCEL) { glide_stop(); rh_motion_reset(&motion); can_drag = 0; }
            ss_pointer_action action = ss_pointer_track(&gesture, event);
            if (can_drag && (event.type == SS_POINTER_MOVE || event.type == SS_POINTER_UP))
                rh_motion_add(&motion, event.time, event.x, event.y);
            if ((action.flags & SS_POINTER_PAN) && scroll && can_drag)
                list_apply_offset(list, real(list, 0x50) - action.dy);
            if ((action.flags & SS_POINTER_FINISH) && scroll && can_drag && gesture.dragging) {
                float vx = 0, vy = 0;
                if (rh_motion_velocity(&motion, event.time, &vx, &vy))
                    glide_start(RH_GLIDE_LIST, frame.owner, frame.token, event.time, vx, vy);
                rh_motion_reset(&motion);
            }
            if (action.flags & SS_POINTER_TAP) {
                int hit = ss_pointer_hit(&frame, action.x, action.y);
                if (hit >= 0) {
                    int index = (int)frame.targets[hit].id;
                    void **table = ptr(list, 0);
                    ((void (*)(void *, int, int))table[5])(list, index, 1);
                    ((void (*)(void *, int))table[6])(list, index);
                    if (trace) LOG("POINTER menu execute %d", index);
                    return 1;
                }
            }
        }
        if (glide.surface == RH_GLIDE_LIST) {
            if (glide.owner == frame.owner && glide.token == frame.token && scroll) glide_list_step(list);
            else glide_stop();
        }
    }
    return ((int (*)(void *, void *))(base + original))(list, pad);
}
static int scroll_input(void *list, void *pad) { return list_input(list, pad, 1, 0x135ba0); }
static int flat_input(void *list, void *pad) { return list_input(list, pad, 0, 0x135060); }

static void *child(void *root, const char *label) {
    return sprite(root) ? ((void *(*)(void *, const char *, int))(base + 0x4fba20))(root, label, 1) : NULL;
}
static int selector_input(void *selector, void *root, void *scene) {
    if (!selector || !sprite(root)) return 0;
    int count = integer(selector, 0x188), selected = integer(selector, 0x190), state = integer(selector, 0x180);
    if (count < 1 || count > 3 || (state != 2 && state != 3)) return 0;
    ss_pointer_frame frame = {.owner = (uintptr_t)selector, .mode = SS_POINTER_MENU};
    const char *names[] = {"item_00", "item_01", "item_02"};
    void *layout = child(root, "layout");
    for (int i = 0; i < count; ++i) {
        float x = 0, y = 0;
        void *item = child(layout, names[i]);
        int found = 0;
        void *top = scene ? ptr(scene, 0x40) : NULL;
        for (int n = 0; top && n < 96 && !found; ++n, top = ptr(top, 0x98)) found = node_point(top, item, &x, &y);
        if (!found) found = node_point(root, item, &x, &y);
        if (found) target(&frame, i, x, y, 1000, 100, i == selected ? SS_POINTER_SELECTED : 0, -1);
    }
    if (!frame.count) return 0;
    frame.token = ss_pointer_publish(&frame);
    trace_frame(&frame);
    reset_gesture(frame.token);
    ss_pointer_event event;
    while (ss_pointer_take(frame.token, &event)) {
        ss_pointer_action action = ss_pointer_track(&gesture, event);
        if (!(action.flags & SS_POINTER_TAP)) continue;
        int hit = ss_pointer_hit(&frame, action.x, action.y);
        if (hit < 0) continue;
        int index = (int)frame.targets[hit].id;
        if (integer(selector, 0x180) == 2) ((void (*)(void *))(base + 0x125b60))(selector);
        for (int n = 0; n < 3 && integer(selector, 0x190) != index; ++n)
            ((void (*)(void *))(base + (integer(selector, 0x190) < index ? 0x125c40 : 0x125b60)))(selector);
        ((void (*)(void *))(base + 0x125dd0))(selector);
        if (trace) LOG("POINTER selector execute %d", index);
        break;
    }
    return 1;
}
static void dialog_update(void *scene, float elapsed) {
    void *selector = ptr(scene, 0x4140);
    if (selector) selector_input(selector, ptr(selector, 0x18), scene);
    ((void (*)(void *, float))(base + 0x123960))(scene, elapsed);
}
static int map_frame(void *scene, ss_pointer_frame *frame) {
    int world = integer(scene, 0x5274);
    if (world < 0 || world > 2) return 0;
    void *stage = ptr(scene, 0x4650);
    float ox = 0, oy = 0;
    if (!point(stage, "icon_orig", &ox, &oy)) {
        if (trace && traced_owner != (uintptr_t)scene) {
            traced_owner = (uintptr_t)scene;
            LOG("POINTER map needs origin; scene %p stage %p", scene, stage);
            const char *labels[] = {"icon_pos", "stage", "layer_info", "box_game"};
            for (unsigned i = 0; i < 4; ++i) if (point(stage, labels[i], &ox, &oy)) LOG("POINTER map anchor %s %.1f %.1f", labels[i],ox,oy);
        }
        return 0;
    }
    *frame = (ss_pointer_frame){.owner = (uintptr_t)scene ^ (uint64_t)world << 56, .mode = SS_POINTER_MAP,
                               .scroll = {0, 0, 1080, 1000}};
    float width = real(scene, 0x4660), height = real(scene, 0x4664);
    if (width < 100 || width > 500 || height < 80 || height > 500) return 0;
    for (int col = 0; col < 12; ++col) for (int row = 0; row < 5; ++row) {
        void *icon = ptr(scene, 0x4668 + 0x30 + col * 0xc8 + row * 0x28);
        if (!icon || ((uint8_t *)icon)[0x84] != 1) continue;
        uint64_t id = 1 + world * 60 + col * 5 + row;
        unsigned flags = col == integer(scene, 0x5298) && row == integer(scene, 0x529c) ? SS_POINTER_SELECTED : 0;
        target(frame, id, ox + col * width, oy + row * height, 160, 150, flags, -1);
    }
    trace_frame(frame);
    return frame->count > 0;
}
static void map_focus(void *scene, int col, int row) {
    int old_x = integer(scene, 0x5298), old_y = integer(scene, 0x529c);
    void *stage = ptr(scene, 0x4650);
    int relative = (int)lroundf(col + real(stage, 0x18) / real(scene, 0x4660));
    put_int(scene, 0x52a0, relative < 0 ? 0 : relative > 2 ? 2 : relative);
    put_int(scene, 0x5298, col); put_int(scene, 0x529c, row);
    ((void (*)(void *, int))(base + 0x1d2750))(scene, 0);
    ((void (*)(void *, int, int))(base + 0x1d2b50))(scene, old_x, old_y);
}
static void map_pan(void *scene, float dx) {
    int world = integer(scene, 0x5274), last = world == 2 ? 10 : 8;
    float width = real(scene, 0x4660);
    void *stage = ptr(scene, 0x4650);
    float x = fmaxf(-(last - 2) * width, fminf(0, real(stage, 0x18) + dx));
    put_real(scene, 0x52a8, x); put_real(scene, 0x52ac, x); put_real(scene, 0x52b0, 1);
    put_real(scene, 0x52b4, 0); put_real(scene, 0x52b8, 0); put_real(scene, 0x52bc, 1);
    ((void (*)(void *, float, float))(base + 0x4fe260))((uint8_t *)stage + 8, x, real(stage, 0x1c));
}
static void glide_map_step(void *scene) {
    uint64_t now = ss_pointer_time_ns();
    uint64_t elapsed = now > glide.time ? now - glide.time : 0;
    glide.time = now;
    if (elapsed > RH_GLIDE_MAX_GAP_NS) { glide_stop(); return; }
    if (elapsed > RH_GLIDE_MAX_STEP_NS) elapsed = RH_GLIDE_MAX_STEP_NS;
    void *stage = ptr(scene, 0x4650);
    if (!stage) { glide_stop(); return; }
    rh_inertia_step_result step = rh_inertia_step(glide.vx, elapsed, RH_GLIDE_MAP_TAU);
    glide.vx = step.velocity;
    float before = real(stage, 0x18);
    map_pan(scene, step.distance);
    int edge = fabsf((real(stage, 0x18) - before) - step.distance) > 0.01f;
    if (edge || fabsf(glide.vx) < RH_GLIDE_STOP_SPEED) glide_stop();
}
static void map_input(void *scene) {
    if (integer(scene, 0x5288) == 2) {
        glide_stop();
        void *selector = ptr(scene, 0x6d68);
        if (selector) selector_input(selector, ptr(selector, 0x18), scene);
        map_tap.armed = 0;
        ((void (*)(void *))(base + 0x1d1410))(scene);
        return;
    }
    ss_pointer_frame frame;
    if (map_frame(scene, &frame)) {
        frame.token = ss_pointer_publish(&frame);
        reset_gesture(frame.token);
        if (map_tap.armed) {
            uint64_t selected = 1 + integer(scene, 0x5274) * 60 + integer(scene, 0x5298) * 5 + integer(scene, 0x529c);
            if (map_tap.token != frame.token || map_tap.item != selected) map_tap.armed = 0;
        }
        ss_pointer_event event;
        while (ss_pointer_take(frame.token, &event)) {
            if (event.type == SS_POINTER_DOWN) {
                glide_stop(); rh_motion_reset(&motion);
                can_drag = ss_pointer_contains(frame.scroll, event.x, event.y);
                if (can_drag) rh_motion_add(&motion, event.time, event.x, event.y);
            }
            if (event.type == SS_POINTER_CANCEL) {
                map_tap.armed = 0; can_drag = 0; glide_stop(); rh_motion_reset(&motion);
            }
            ss_pointer_action action = ss_pointer_track(&gesture, event);
            if (can_drag && (event.type == SS_POINTER_MOVE || event.type == SS_POINTER_UP))
                rh_motion_add(&motion, event.time, event.x, event.y);
            if ((action.flags & SS_POINTER_PAN) && can_drag) { map_tap.armed = 0; map_pan(scene, action.dx); }
            if ((action.flags & SS_POINTER_FINISH) && can_drag && gesture.dragging) {
                float vx = 0, vy = 0;
                if (rh_motion_velocity(&motion, event.time, &vx, &vy))
                    glide_start(RH_GLIDE_MAP, frame.owner, frame.token, event.time, vx, vy);
                rh_motion_reset(&motion);
            }
            if (action.flags & SS_POINTER_TAP) {
                int hit = ss_pointer_hit(&frame, action.x, action.y);
                if (hit < 0) { map_tap.armed = 0; continue; }
                const ss_pointer_target *item = &frame.targets[hit];
                int activate = rh_map_tap_activate(&map_tap, frame.token, item->id, item->flags & SS_POINTER_SELECTED);
                unsigned position = (unsigned)(item->id - 1) % 60;
                if (!activate) map_focus(scene, position / 5, position % 5);
                else ((void (*)(void *, int, void *, int))(base + 0x12d8f0))((uint8_t *)scene + 0x2978, 8, NULL, 0);
                if (trace) LOG("POINTER map %s %llu", activate ? "activate" : "select", (unsigned long long)item->id);
                return;
            }
        }
        if (glide.surface == RH_GLIDE_MAP) {
            if (glide.owner == frame.owner && glide.token == frame.token) glide_map_step(scene);
            else glide_stop();
        }
    } else glide_stop();
    ((void (*)(void *))(base + 0x1d1410))(scene);
}
/* The pause menu uses the small selector widget, not the scrolling list class. */
static int pause_input(void *scene) {
    void *pause = (uint8_t *)scene + 0x3e58;
    int state = integer(pause, 0x2940);
    if (!state) return 0;
    void *selector = ptr(pause, 0x2aa8), *root = ptr(pause, 0x2aa0);
    if (state != 2 || !selector_input(selector, root, NULL)) {
        ss_pointer_frame frame = {.owner = (uintptr_t)pause, .mode = SS_POINTER_MENU};
        ss_pointer_publish(&frame);
    }
    return 1;
}
static void publish_hints(void *scene) {
    if (pause_input(scene)) return;
    if (ss_touch_layout(ss_touch_left_get()) == SS_TOUCH_MENU) return;
    void *info = ptr(scene, 0x9408);
    if (!sprite(info)) return;
    ss_pointer_frame frame = {.owner = (uintptr_t)scene, .mode = SS_POINTER_HINTS};
    const char *labels[] = {"label_skip", "label_model"};
    const int buttons[] = {11, 3};
    unsigned enabled = ((uint8_t *)scene)[0x9400], model = ((uint8_t *)scene)[0x9419];
    /* SkipPosOn/Off set 0x9400. The model capability at 0x9419 is separate from the
       animated label nodes. Native demo input uses button 3, as does the clickable hint. */
    if (enabled == 1 && model == 1) frame.tutorial_buttons = 1ull << buttons[1];
    uint64_t hints_key = (uintptr_t)scene ^ (uint64_t)((uint8_t *)scene)[0x941a] << 56 ^
        (uint64_t)enabled << 48 ^ (uint64_t)model << 40;
    if (trace && traced_hints != hints_key) {
        traced_hints = hints_key;
        LOG("POINTER hint state %p enabled %u model %u shown %u", scene, enabled, model, ((uint8_t *)scene)[0x941a]);
    }
    for (int i = 0; i < 2; ++i) {
        if (enabled != 1 || (i && model != 1)) continue;
        float x = 0, y = 0;
        void *label = child(info, labels[i]);
        if (node_point(info, label, &x, &y) || point(info, labels[i], &x, &y))
            target(&frame, i + 1, x, y, 440, 90, SS_POINTER_BUTTON, buttons[i]);
    }
    if (ss_pointer_publish(&frame)) trace_frame(&frame);
}
static float hint_alpha(void *scene) {
    float value = ((float (*)(void *))(base + 0x131740))(scene);
    publish_hints(scene);
    return value;
}
static int hint_update(void *scene) {
    int value = ((int (*)(void *))(base + 0x132300))(scene);
    publish_hints(scene);
    return value;
}
void rh_menu_install(const module_t *main) {
    base = main->base; module = main;
    trace = getenv("RH_MENU_TRACE") != NULL;
    if (getenv("RH_POINTER_DISABLE")) return;
    const struct { unsigned slot, function; } expected[] = {
        {0xd0a908, 0x135ba0}, {0xd1b0c8, 0x1d1410}, {0xd0bd18, 0x135060},
        {0xd0a5a8, 0x123960},
    };
    for (unsigned i = 0; i < sizeof expected / sizeof expected[0]; ++i)
        if (main->data_off > expected[i].slot || main->patch_off < expected[i].slot + 8 || ptr(base, expected[i].slot) != base + expected[i].function)
            die("Menu adapter module layout does not match at %#x", expected[i].slot);
    if (*(uint32_t *)(base + 0x135ba0) != 0xa9be7bfd || *(uint32_t *)(base + 0x1d1410) != 0xa9bd7bfd)
        die("Menu adapter input code does not match");
    unsigned count = 0;
    for (uintptr_t offset = main->data_off; offset + 8 <= main->patch_off; offset += 8) {
        void **slot = (void **)(base + offset);
        if (*slot == base + 0x135ba0) { *slot = scroll_input; ++count; }
        else if (*slot == base + 0x135060) { *slot = flat_input; ++count; }
        else if (*slot == base + 0x131740) { *slot = hint_alpha; ++count; }
        else if (*slot == base + 0x132300) { *slot = hint_update; ++count; }
        else if (*slot == base + 0x123960) { *slot = dialog_update; ++count; }
    }
    *(void **)(base + 0xd1b0c8) = map_input;
    start_pointer_file();
    LOG("Menu pointer adapter installed: %u shared method slots", count);
}
