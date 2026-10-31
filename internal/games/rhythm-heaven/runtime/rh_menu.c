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
static int can_drag;
static uint64_t traced_owner;
static uint64_t traced_hints;

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
                    } else ss_pointer_push((ss_pointer_event){frame.token, type, x, y});
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
    LOG("Pointer file %s", path);
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
static void reset_gesture(uint64_t token) {
    if (gesture.token != token) { gesture = (ss_pointer_gesture){0}; can_drag = 0; }
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
    int slots = scroll ? integer(list, 0x44) : 0;
    float base_y = 0, ref_x = 0, ref_width = fmaxf(items[0] ? integer(items[0], 0x30) + 48 : 200, 200);
    int have_rows = 0;
    if (scroll) {
        int first_page = slots > 0 && slots < count ? slots : count;
        for (int i = 0; i < count && !have_rows; ++i) {
            void *item = items[i];
            if (!item) continue;
            void *node = ptr(item, ((uint8_t *)item)[0x28] ? 0x18 : 0x10);
            if (!sprite(node) || ((uint8_t *)node)[0x84] != 1) continue;
            float x = 0, y = 0;
            if (!node_point(ptr(list, 0x70), node, &x, &y)) continue;
            /* The first page is laid out item by item. The game parks every later
               item at the same off-page position, but draws it on the next row. */
            base_y = i < first_page ? y + i * spacing : y;
            ref_x = x;
            have_rows = 1;
        }
    }
    int dump = 0;
    if (trace) {
        static void *dump_list;
        static int dump_count, dump_selected, dump_row, dump_slot;
        int row = integer(list, 0x48);
        int slot = spacing > 0 ? (int)lroundf(real(list, 0x50) / spacing) : 0;
        if (list != dump_list || count != dump_count || selected != dump_selected || row != dump_row || slot != dump_slot) {
            dump = 1; dump_list = list; dump_count = count; dump_selected = selected; dump_row = row; dump_slot = slot;
            LOG("POINTER dump list %p count %d selected %d row %d slot %d offset %.1f slots %d low %d high %d anchor %.1f %.1f scroll %.1f %.1f %.1fx%.1f",
                list, count, selected, row, slot, real(list, 0x50), integer(list, 0x44), integer(list, 0x58),
                integer(list, 0x5c), ox, oy, frame->scroll.x, frame->scroll.y, frame->scroll.width, frame->scroll.height);
        }
    }
    for (int i = 0; i < count; ++i) {
        void *item = items[i];
        void *node = NULL;
        float x = 0, y = 0;
        int visible = 0, found = 0;
        if (item) {
            node = ptr(item, ((uint8_t *)item)[0x28] ? 0x18 : 0x10);
            visible = sprite(node) && ((uint8_t *)node)[0x84] == 1;
        }
        /* Long lists retain hidden cloned rows. Their local positions can match
           visible rows. Do not let those clones take a visible row's tap. */
        if (!visible) {
            if (dump) LOG("POINTER dump item %d item %p node %p vis 0", i, item, node);
            continue;
        }
        if (scroll && have_rows) {
            x = ref_x;
            y = base_y - i * spacing;
            found = 1;
        } else if (scroll) found = node_point(ptr(list, 0x70), node, &x, &y);
        else {
            void *root = ptr(owner, 0x40);
            for (int n = 0; root && n < 96 && !found; ++n, root = ptr(root, 0x98)) found = node_point(root, node, &x, &y);
        }
        if (!found) {
            if (dump) LOG("POINTER dump item %d item %p node %p vis 1 not found", i, item, node);
            continue;
        }
        float width = scroll ? ref_width : fmaxf(integer(item, 0x30) + 48, 200);
        int before = frame->count;
        target(frame, i, x + ox, y + oy, width, scroll ? spacing : 84, i == selected ? SS_POINTER_SELECTED : 0, -1);
        if (dump) LOG("POINTER dump item %d item %p node %p vis 1 at %.1f %.1f target %d", i, item, node, x, y, frame->count > before);
    }
    trace_frame(frame);
    /* A long list can scroll past the rows that the game has laid out. Keep the
       empty menu frame so the phone does not show the fallback A and D-pad
       areas. Those areas turn a continued drag into a button press. A short
       menu without targets keeps the fallback, because it may be a menu that
       this adapter cannot read. */
    return scroll || frame->count > 0;
}
static int list_input(void *list, void *pad, int scroll, uintptr_t original) {
    ss_pointer_frame frame;
    if (list_frame(list, scroll, &frame)) {
        frame.token = ss_pointer_publish(&frame);
        reset_gesture(frame.token);
        ss_pointer_event event;
        while (ss_pointer_take(frame.token, &event)) {
            if (event.type == SS_POINTER_DOWN) can_drag = ss_pointer_contains(frame.scroll, event.x, event.y);
            ss_pointer_action action = ss_pointer_track(&gesture, event);
            if ((action.flags & SS_POINTER_PAN) && scroll && can_drag) {
                float spacing = real(list, 0x4c);
                float offset = rh_menu_scroll_limit(real(list, 0x50) - action.dy, integer(list, 0x20), integer(list, 0x44),
                                                    integer(list, 0x58), integer(list, 0x5c), spacing);
                put_real(list, 0x50, offset); put_real(list, 0x54, offset);
                put_int(list, 0x48, (int)lroundf(offset / spacing) + integer(list, 0x44) / 2);
                ((void (*)(void *))(base + 0x135ab0))(list);
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
        float x = ox + col * width, y = oy + row * height, scale = 1;
        /* The multiplayer map (world 2) draws the same grid 1.1 times larger and moved by (30, 204);
           measured from its three rows and first columns in a host screenshot. */
        if (world == 2) { scale = 1.1f; x = x * scale + 30; y = y * scale + 204; }
        target(frame, id, x, y, 160 * scale, 150 * scale, flags, -1);
    }
    trace_frame(frame);
    /* Keep an empty map frame so a drag at the edge cannot fall back to a button press. */
    return 1;
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
/* The native map command handler (main+0x1d1560) moves the map only in input state 1 and uses the
   selector in state 2. Other states, for example an unlock transition, ignore input. A touch there
   could change the selection that the unlock routine then uses (issue #2). */
static void map_input(void *scene) {
    int state = integer(scene, 0x5288);
    if (!rh_map_input_ready(state)) {
        void *selector = state == 2 ? ptr(scene, 0x6d68) : NULL;
        if (!selector || !selector_input(selector, ptr(selector, 0x18), scene)) {
            /* An empty menu frame takes the touches, so no fallback button acts. Drop them. */
            ss_pointer_frame blocked = {.owner = (uintptr_t)scene, .mode = SS_POINTER_MENU};
            blocked.token = ss_pointer_publish(&blocked);
            gesture = (ss_pointer_gesture){0};
            can_drag = 0;
            ss_pointer_event ignored;
            while (ss_pointer_take(blocked.token, &ignored)) {}
        }
        ((void (*)(void *))(base + 0x1d1410))(scene);
        return;
    }
    ss_pointer_frame frame;
    if (map_frame(scene, &frame)) {
        frame.token = ss_pointer_publish(&frame);
        reset_gesture(frame.token);
        ss_pointer_event event;
        while (ss_pointer_take(frame.token, &event)) {
            if (event.type == SS_POINTER_DOWN) can_drag = ss_pointer_contains(frame.scroll, event.x, event.y);
            ss_pointer_action action = ss_pointer_track(&gesture, event);
            if ((action.flags & SS_POINTER_PAN) && can_drag) map_pan(scene, action.dx);
            if (action.flags & SS_POINTER_TAP) {
                int hit = ss_pointer_hit(&frame, action.x, action.y);
                if (hit < 0) continue;
                const ss_pointer_target *item = &frame.targets[hit];
                /* Select, then confirm: a tap on the selected level enters it. A tap on another level selects it. */
                int activate = (item->flags & SS_POINTER_SELECTED) != 0;
                unsigned position = (unsigned)(item->id - 1) % 60;
                if (!activate) map_focus(scene, position / 5, position % 5);
                else ((void (*)(void *, int, void *, int))(base + 0x12d8f0))((uint8_t *)scene + 0x2978, 8, NULL, 0);
                if (trace) LOG("POINTER map %s %llu", activate ? "activate" : "select", (unsigned long long)item->id);
                return;
            }
        }
    }
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
/* Multiplayer member select (main+0xd1cd28; input at main+0x1e60a0, commands at main+0x1e55a0).
   Each player's state is at +0x40f8 + 4 * player: 0 picks a name, 1 changes over, 2 picks an avatar,
   3 and 4 change over, 5 is done. +0x40f4 is set while the start dialog shows; its selector is at
   +0x3e88 and takes one tap, like other dialogs. The name grid at +0x3e80 is shared: the
   cursor of each player is at +0x980 (column) and +0x990 (row) + 4 * player; cell row * 4 + column has
   its node at +0x768 and a taken flag at +0x8a8. Each player has an avatar strip at +0x3e90 + 8 * player:
   index +0x2d0 (0 to 9), item offsets +0x270, active when +0x298 is 1. The strip shows over the
   chosen name cell, with the selected avatar at the cell center.
   Touch sets up one player at a time: player 1 picks a name and an avatar, then player 2. A tap on
   another item walks that player's cursor with D-pad presses; a tap on the selected item presses A
   for that player. */
enum { MEMBER_NAME = 1, MEMBER_AVATAR = 2 };
static double seconds(void);
struct member_state { uint64_t owner, goal; int player, presses; double last; };
static struct member_state member_walk;
static int member_cursor(void *list, int player) {
    return integer(list, 0x990 + 4 * player) * 4 + integer(list, 0x980 + 4 * player);
}
static int member_frame(void *scene, ss_pointer_frame *frame, int *player_out) {
    if (integer(scene, 0x40f4)) return 0;
    int players = ss_input_players(), player = -1;
    if (players < 2 || players > 4) return 0;
    for (int p = 0; p < players && player < 0; ++p) if (integer(scene, 0x40f8 + 4 * p) < 3) player = p;
    if (player < 0) return 0;
    int state = integer(scene, 0x40f8 + 4 * player);
    *player_out = player;
    *frame = (ss_pointer_frame){.owner = (uintptr_t)scene ^ (uint64_t)(player * 4 + state) << 56, .mode = SS_POINTER_MENU};
    if (state == 0) {
        void *list = ptr(scene, 0x3e80), *root = list ? ptr(list, 0x10) : NULL;
        if (!sprite(root)) return 1;
        int cursor = member_cursor(list, player);
        for (int cell = 0; cell < 20; ++cell) {
            float x = 0, y = 0;
            if (cell != cursor && integer(list, 0x8a8 + 4 * cell)) continue;   /* another player has it */
            if (!node_point(root, ptr(list, 0x768 + 8 * cell), &x, &y)) continue;
            target(frame, (uint64_t)MEMBER_NAME << 8 | (unsigned)cell, x, y, 290, 110, cell == cursor ? SS_POINTER_SELECTED : 0, -1);
        }
    } else if (state == 2) {
        /* The strip replaces the chosen name cell; the selected avatar sits at the cell center. */
        uint8_t *strip = ptr(scene, 0x3e90 + 8 * player);
        void *list = ptr(scene, 0x3e80), *root = list ? ptr(list, 0x10) : NULL;
        if (!strip || strip[0x298] != 1 || !sprite(root)) return 1;
        int index = integer(strip, 0x2d0);
        float x = 0, y = 0;
        if (index < 0 || index > 9 || !node_point(root, ptr(list, 0x768 + 8 * member_cursor(list, player)), &x, &y)) return 1;
        x -= real(strip, 0x270 + 4 * index);
        for (int item = 0; item < 10; ++item) {
            float dx = real(strip, 0x270 + 4 * item);
            if (fabsf(dx - real(strip, 0x270 + 4 * index)) > 200) continue;   /* outside the box */
            target(frame, (uint64_t)MEMBER_AVATAR << 8 | (unsigned)item, x + dx, y, 96, 120, item == index ? SS_POINTER_SELECTED : 0, -1);
        }
    }
    return 1;
}
/* One walk step per call. The next press waits until the game has read the previous press. */
static void member_step(void *scene, uint64_t owner) {
    if (!member_walk.goal) return;
    int player = member_walk.player, kind = (int)(member_walk.goal >> 8), goal = (int)(member_walk.goal & 0xff);
    int state = integer(scene, 0x40f8 + 4 * player), now = -1, button = -1;
    if (kind == MEMBER_NAME && state == 0) {
        now = member_cursor(ptr(scene, 0x3e80), player);
        if (now / 4 != goal / 4) button = goal / 4 < now / 4 ? 13 : 15;
        else if (now != goal) button = goal < now ? 12 : 14;
    } else if (kind == MEMBER_AVATAR && state == 2) {
        now = integer(ptr(scene, 0x3e90 + 8 * player), 0x2d0);
        if (now != goal) button = goal < now ? 12 : 14;
    }
    if (button < 0 || owner != member_walk.owner || member_walk.presses >= 12) {
        if (trace) LOG("POINTER member walk ends at %d after %d presses", now, member_walk.presses);
        member_walk.goal = 0;
        return;
    }
    double time = seconds();
    if (time - member_walk.last < 0.07) return;
    ss_input_press_player(player, 1ull << button);
    member_walk.presses++;
    member_walk.last = time;
}
static void member_input(void *scene) {
    ss_pointer_frame frame;
    int player = 0;
    void *confirm = ptr(scene, 0x3e88);   /* the start dialog: "没错！" and "等一下" */
    if (integer(scene, 0x40f4) && confirm && selector_input(confirm, ptr(confirm, 0x18), scene)) member_walk.goal = 0;
    else if (member_frame(scene, &frame, &player)) {
        frame.token = ss_pointer_publish(&frame);
        trace_frame(&frame);
        reset_gesture(frame.token);
        member_step(scene, frame.owner);
        ss_pointer_event event;
        while (ss_pointer_take(frame.token, &event)) {
            ss_pointer_action action = ss_pointer_track(&gesture, event);
            if (!(action.flags & SS_POINTER_TAP)) continue;
            int hit = ss_pointer_hit(&frame, action.x, action.y);
            if (hit < 0) continue;
            const ss_pointer_target *item = &frame.targets[hit];
            if (item->flags & SS_POINTER_SELECTED) {
                ss_input_press_player(player, 1ull << 0);
                member_walk.goal = 0;
            } else {
                member_walk = (struct member_state){.owner = frame.owner, .goal = item->id, .player = player};
                member_step(scene, frame.owner);
            }
            if (trace) LOG("POINTER member player %d %s %llu", player + 1, member_walk.goal ? "walk to" : "confirm", (unsigned long long)item->id);
        }
    } else member_walk.goal = 0;
    ((void (*)(void *))(base + 0x1e60a0))(scene);
}
/* Rhythm Toys. A toy scene uses the minigame scene class (main+0xd2b730). Its list at +0x38 (node:
   next, previous, object) holds the toy's game object.
   Who's Got Rhythm? (main+0xd212e0): the sprite at +0x3cd0 holds the rabbits. The actors at +0x3ce8
   (5 per row, rows 0x28 apart) keep their sprites at +0x18. Columns are at +0x3db0, rows at +0x3db4, the cursor column and row at +0x3dd4
   and +0x3dd8 (main+0x212df0). The cursor shows when +0x3dd0 is 1. +0x3ef0 counts the hearts.
   The toy base sets +0x2a6a while the rabbits take input (main+0x12a980). A dialog that waits for A
   clears it. The sound board leaves it clear.
   Sound board (main+0xd0dce0): the sprite is at +0x3e08 and the board at +0x3e18. The board keeps the
   grid cursor at +0x40 and +0x44, the palette cursor at +0xac and +0xb0, the page at +0x7b0, and the
   open palette at +0x7b8 (main+0x13f5b0, main+0x13f810, main+0x13f910). The cells are matrix/rowRR/
   columnCC and palette/rowRR/columnCC.
   A tap on an item that is not selected starts a walk: one D-pad press at a time moves the cursor until
   the item is selected. A tap on the selected item presses A. A page takes ZL or ZR presses and needs one
   tap. The game checks each press itself, so a press that the game does not expect has no effect. */
enum { TOY_RABBIT = 1, TOY_GRID = 2, TOY_PALETTE = 3, TOY_PAGE = 4 };
enum { PAD_A = 0, PAD_X = 2, PAD_Y = 3, PAD_ZL = 8, PAD_ZR = 9, PAD_PLUS = 10,
       PAD_LEFT = 12, PAD_UP = 13, PAD_RIGHT = 14, PAD_DOWN = 15 };
struct walk_state { uint64_t owner, goal; int presses; double last; };
static struct walk_state walk;
static const char *const row_labels[] = {"row00", "row01", "row02", "row03"};
static const char *const column_labels[] = {"column00", "column01", "column02", "column03"};

static double seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec / 1e9;
}
static void *toy_object(void *scene, unsigned vtable) {
    if (ptr(scene, 0) != base + 0xd2b730) return NULL;
    uint8_t *head = (uint8_t *)scene + 0x38;
    void *node = ptr(head, 0);
    for (int n = 0; node && node != head && n < 16; ++n, node = ptr(node, 0)) {
        void *object = ptr(node, 0x10);
        if (object && ptr(object, 0) == base + vtable) return object;
    }
    return NULL;
}
static void toy_cell(ss_pointer_frame *frame, void *root, const char *group, int row, int column, int kind,
                     float size, int selected) {
    void *node = child(child(child(root, group), row_labels[row]), column_labels[column]);
    float x = 0, y = 0;
    if (node_point(root, node, &x, &y))
        target(frame, (uint64_t)kind << 8 | (unsigned)(row * 4 + column), x, y, size, size, selected ? SS_POINTER_SELECTED : 0, -1);
}
static void toy_button(ss_pointer_frame *frame, void *root, const char *label, float dx, float dy, float w, float h, int button) {
    float x = 0, y = 0;
    if (point(root, label, &x, &y)) target(frame, 0x1000 + (uint64_t)button, x + dx, y + dy, w, h, SS_POINTER_BUTTON, button);
}
static int rabbit_frame(void *game, ss_pointer_frame *frame) {
    int columns = integer(game, 0x3db0), rows = integer(game, 0x3db4);
    uint8_t *bytes = game;
    if (bytes[0x2a6a] != 1 || bytes[0x3dd0] != 1 || integer(game, 0x3ef0) <= 0) return 0;
    if (columns < 1 || columns > 5 || rows < 1 || rows > 5) return 0;
    *frame = (ss_pointer_frame){.owner = (uintptr_t)game, .mode = SS_POINTER_MENU};
    /* The actor sprite (actor +0x18) stands on its point. The rabbit is about 0.9 column wide and
       1.2 columns high. */
    float feet[5][5][2] = {0}, size = 200;
    int found[5][5] = {0};
    for (int row = 0; row < rows; ++row) for (int column = 0; column < columns; ++column) {
        void *actor = ptr(game, 0x3ce8 + row * 0x28 + column * 8);
        found[row][column] = actor && node_point(ptr(game, 0x3cd0), ptr(actor, 0x18), &feet[row][column][0], &feet[row][column][1]);
    }
    if (columns > 1 && found[0][0] && found[0][1]) size = fminf(200, fabsf(feet[0][1][0] - feet[0][0][0]));
    /* Row 0 stands in front and covers the rows behind it. The last target takes an overlap. */
    for (int row = rows - 1; row >= 0; --row) for (int column = 0; column < columns; ++column) {
        if (!found[row][column]) continue;
        int selected = column == integer(game, 0x3dd4) && row == integer(game, 0x3dd8);
        target(frame, (uint64_t)TOY_RABBIT << 8 | (unsigned)(row * 5 + column), feet[row][column][0],
               feet[row][column][1] + size * 0.6f, size * 0.95f, size * 1.2f, selected ? SS_POINTER_SELECTED : 0, -1);
    }
    return frame->count > 0;
}
static int board_frame(void *game, ss_pointer_frame *frame) {
    void *board = ptr(game, 0x3e18), *root = ptr(game, 0x3e08);
    if (!board || !sprite(root)) return 0;
    int palette = ((uint8_t *)board)[0x7b8] == 1, page = integer(board, 0x7b0);
    *frame = (ss_pointer_frame){.owner = (uintptr_t)board ^ (uint64_t)palette << 56, .mode = SS_POINTER_MENU};
    if (palette) {
        for (int row = 0; row < 3; ++row) for (int column = 0; column < 3; ++column)
            toy_cell(frame, root, "palette", row, column, TOY_PALETTE, 150,
                     column == integer(board, 0xac) && row == integer(board, 0xb0));
    } else {
        for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
            toy_cell(frame, root, "matrix", row, column, TOY_GRID, 150,
                     column == integer(board, 0x40) && row == integer(board, 0x44));
        for (int i = 0; i < 6; ++i) {
            char label[16];
            snprintf(label, sizeof label, "pattern%02d", i);
            float x = 0, y = 0;
            if (point(root, label, &x, &y))
                target(frame, (uint64_t)TOY_PAGE << 8 | (unsigned)i, x, y, 200, 180, i == page ? SS_POINTER_SELECTED : 0, -1);
        }
        toy_button(frame, root, "text_L_button", -25, 0, 120, 110, PAD_ZL);
        toy_button(frame, root, "text_R_button", 25, 0, 120, 110, PAD_ZR);
        toy_button(frame, root, "speed", 0, 0, 440, 150, PAD_Y);
        toy_button(frame, root, "text_Y_button", 0, 0, 260, 90, PAD_Y);
    }
    /* The point of text_AB_button is the upper left corner of the two lines "A Place" and "X Remove". */
    toy_button(frame, root, "text_AB_button", 80, -26, 220, 72, PAD_A);
    toy_button(frame, root, "text_AB_button", 80, -98, 220, 72, PAD_X);
    toy_button(frame, root, "text_plus_button", -35, 0, 180, 80, PAD_PLUS);
    return 1;
}
/* One walk step per call. The next press waits until the game has read the previous press. */
static void toy_walk(const ss_pointer_frame *frame) {
    if (!walk.goal) return;
    int goal = -1, now = -1;
    for (int i = 0; i < frame->count; ++i) {
        uint64_t id = frame->targets[i].id;
        if (id == walk.goal) goal = i;
        if (id >> 8 == walk.goal >> 8 && (frame->targets[i].flags & SS_POINTER_SELECTED)) now = i;
    }
    if (walk.owner != frame->owner || goal < 0 || now < 0 || goal == now || walk.presses >= 12) {
        if (trace) LOG("POINTER toy walk ends at %lld after %d presses", now < 0 ? -1ll : (long long)frame->targets[now].id, walk.presses);
        walk.goal = 0;
        return;
    }
    double time = seconds();
    if (time - walk.last < 0.07) return;
    const ss_pointer_rect *a = &frame->targets[now].rect, *b = &frame->targets[goal].rect;
    float dx = (b->x + b->width / 2) - (a->x + a->width / 2), dy = (b->y + b->height / 2) - (a->y + a->height / 2);
    int button = walk.goal >> 8 == TOY_PAGE ? (dx < 0 ? PAD_ZL : PAD_ZR)
               : fabsf(dx) >= fabsf(dy) ? (dx < 0 ? PAD_LEFT : PAD_RIGHT) : (dy < 0 ? PAD_UP : PAD_DOWN);
    ss_touch_press(1ull << button);
    walk.presses++;
    walk.last = time;
}
static int toy_input(void *scene) {
    void *game;
    ss_pointer_frame frame;
    int shown = 0;
    if ((game = toy_object(scene, 0xd212e0))) shown = rabbit_frame(game, &frame);
    else if ((game = toy_object(scene, 0xd0dce0))) shown = board_frame(game, &frame);
    if (!game) return 0;
    if (!shown) { walk.goal = 0; return 0; }
    frame.token = ss_pointer_publish(&frame);
    trace_frame(&frame);
    reset_gesture(frame.token);
    toy_walk(&frame);
    ss_pointer_event event;
    while (ss_pointer_take(frame.token, &event)) {
        ss_pointer_action action = ss_pointer_track(&gesture, event);
        if (!(action.flags & SS_POINTER_TAP)) continue;
        int hit = ss_pointer_hit(&frame, action.x, action.y);
        if (hit < 0 || (frame.targets[hit].flags & SS_POINTER_BUTTON)) continue;
        const ss_pointer_target *item = &frame.targets[hit];
        if ((item->flags & SS_POINTER_SELECTED) && item->id >> 8 != TOY_PAGE) {
            ss_touch_press(1ull << PAD_A);
            walk.goal = 0;
        } else {
            walk = (struct walk_state){.owner = frame.owner, .goal = item->id};
            toy_walk(&frame);
        }
        if (trace) LOG("POINTER toy %s %llu", walk.goal ? "walk to" : "confirm", (unsigned long long)item->id);
    }
    return 1;
}
static void publish_hints(void *scene) {
    if (pause_input(scene)) return;
    if (toy_input(scene)) return;
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
        {0xd0a5a8, 0x123960}, {0xd1cdf0, 0x1e60a0},
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
    *(void **)(base + 0xd1cdf0) = member_input;
    start_pointer_file();
    LOG("Menu pointer adapter installed: %u shared method slots", count);
}
