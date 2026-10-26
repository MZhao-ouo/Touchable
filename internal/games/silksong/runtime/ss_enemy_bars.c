/* Enemy health bars for Silksong. Check the method slot and the code addresses of each main build before use.

   HealthManager.Update runs once for each active enemy in each game frame, on the game thread. The hook
   reads hp and initHp, projects the top of the enemy's BoxCollider2D with the main camera, and adds a bar.
   When the same HealthManager runs again, a new frame has started, and the bars of the previous frame
   are published to the phone layer (ss_health_bars.h). The phone draws them above the game picture.

   SS_HEALTH_BARS=1 turns the bars on. SS_HEALTH_TRACE=1 logs the bar count every 600 frames.
   SS_HEALTH_TEST=1 also shows enemies that are not hit yet, and logs the first bars every 60 frames (Mac host checks). */
#include "dm.h"
#include "touchable_game.h"
#include "ss_health_bars.h"
#include <math.h>

typedef struct { float x, y, z; } vector3;
typedef struct { float x, y, width, height; } rect;
typedef struct { vector3 center, extents; } bounds;

typedef struct {
    const char *build;
    uintptr_t update_slot, update;   /* HealthManager.Update: method slot and method */
    uintptr_t cameras_class;         /* class slot of GameCameras */
    uintptr_t world_to_viewport;     /* Camera.WorldToViewportPoint(Vector3) */
    uintptr_t camera_rect;           /* Camera.get_rect */
    uintptr_t collider_bounds;       /* Collider2D.get_bounds */
    uintptr_t component_transform;   /* Component.get_transform */
    uintptr_t transform_position;    /* Transform.get_position */
} enemy_bars_layout;

static const enemy_bars_layout layouts[] = {
    {"fc9ea4cc (v655360, 1.0.30000)", 0x7da0970, 0x239fff0, 0x7e14870, 0x5f54f20, 0x5f522b0, 0x60c83c0, 0x5fdeef0, 0x6002e30},
};

/* Il2CppClass and object fields of this IL2CPP version, and the HealthManager and GameCameras fields. */
enum { CLASS_STATICS = 0xb8, CLASS_READY = 0xe4, OBJECT_NATIVE = 0x10 };
enum { HEALTH_COLLIDER = 0x20, HEALTH_HP = 0xa8, HEALTH_SEND_DAMAGE_TO = 0xd8, HEALTH_DEAD = 0x1d9,
       HEALTH_INIT_HP = 0x2ac, HEALTH_TOOK_DAMAGE = 0x2b0 };
enum { CAMERAS_INSTANCE = 0x0, CAMERAS_MAIN = 0x28 };
enum { SEEN_MAX = 256 };

static const module_t *main_module;
static const enemy_bars_layout *layout;
static void (*update)(void *, const void *);
static vector3 (*world_to_viewport)(void *, vector3, const void *);
static rect (*camera_rect)(void *, const void *);
static bounds (*collider_bounds)(void *, const void *);
static void *(*component_transform)(void *, const void *);
static vector3 (*transform_position)(void *, const void *);
static int trace, test;

/* Only the game thread uses the frame state. */
static struct {
    const void *seen[SEEN_MAX];
    int seen_count, count;
    ss_health_bar bars[SS_HEALTH_BARS_MAX];
    void *camera;      /* the main camera of this frame; NULL until the first enemy needs it */
    rect view;         /* its viewport rectangle */
    int camera_read;
    unsigned frames;
} frame;

static void *read_pointer(const void *p, unsigned offset) { return p ? *(void *const *)((const uint8_t *)p + offset) : NULL; }
static int read_int(const void *p, unsigned offset) { return *(const int *)((const uint8_t *)p + offset); }
static int alive(const void *object) { return object && read_pointer(object, OBJECT_NATIVE); }

static void *static_object(uintptr_t class_slot, unsigned offset) {
    uintptr_t klass = *(const uintptr_t *)(main_module->base + class_slot);
    if (!klass || (klass & 7) || !read_int((void *)klass, CLASS_READY)) return NULL;
    void *statics = read_pointer((void *)klass, CLASS_STATICS);
    void *object = statics ? read_pointer(statics, offset) : NULL;
    return alive(object) ? object : NULL;
}

static void publish_frame(void) {
    ss_health_bars_publish(frame.bars, frame.count);
    if ((trace || test) && frame.frames++ % (test ? 60 : 600) == 0) {
        char text[256] = ""; int used = 0;
        for (int i = 0; i < frame.count && i < 4 && used < (int)sizeof text; ++i)
            used += snprintf(text + used, sizeof text - used, " (%.3f, %.3f, w %.3f, fill %.2f)", frame.bars[i].x, frame.bars[i].y, frame.bars[i].width, frame.bars[i].fill);
        LOG("HEALTH BARS frame %u: %d enemies, %d bars%s", frame.frames, frame.seen_count, frame.count, text);
    }
    frame.seen_count = frame.count = frame.camera_read = 0;
    frame.camera = NULL;
}

/* Project a world point to the game picture: 0 to 1 from the upper left. */
static int picture_point(vector3 world, float *x, float *y) {
    vector3 v = world_to_viewport(frame.camera, world, NULL);
    if (!isfinite(v.x) || !isfinite(v.y) || v.z <= 0) return 0;
    *x = frame.view.x + v.x * frame.view.width;
    *y = 1 - (frame.view.y + v.y * frame.view.height);
    return 1;
}

static void add_bar(void *health) {
    int hp = read_int(health, HEALTH_HP), initial = read_int(health, HEALTH_INIT_HP);
    if (hp <= 0 || initial <= 0 || *((const uint8_t *)health + HEALTH_DEAD)) return;
    if (hp >= initial && !*((const uint8_t *)health + HEALTH_TOOK_DAMAGE) && !test) return;   /* not hit yet */
    if (alive(read_pointer(health, HEALTH_SEND_DAMAGE_TO))) return;   /* a body part: another HealthManager keeps the health */
    if (frame.count >= SS_HEALTH_BARS_MAX) return;
    if (!frame.camera_read) {
        frame.camera_read = 1;
        void *cameras = static_object(layout->cameras_class, CAMERAS_INSTANCE);
        void *camera = read_pointer(cameras, CAMERAS_MAIN);
        if (alive(camera)) {
            frame.view = camera_rect(camera, NULL);
            if (frame.view.width > 0 && frame.view.height > 0) frame.camera = camera;
        }
    }
    if (!frame.camera) return;
    vector3 top, left, right;
    void *collider = read_pointer(health, HEALTH_COLLIDER);
    if (alive(collider)) {
        bounds b = collider_bounds(collider, NULL);
        top = (vector3){b.center.x, b.center.y + b.extents.y, b.center.z};
        left = (vector3){b.center.x - b.extents.x, top.y, top.z};
        right = (vector3){b.center.x + b.extents.x, top.y, top.z};
    } else {
        void *transform = component_transform(health, NULL);
        if (!alive(transform)) return;
        top = transform_position(transform, NULL);
        top.y += 1.5f;
        left = (vector3){top.x - 0.75f, top.y, top.z};
        right = (vector3){top.x + 0.75f, top.y, top.z};
    }
    float x, y, lx, ly, rx, ry;
    if (!picture_point(top, &x, &y) || !picture_point(left, &lx, &ly) || !picture_point(right, &rx, &ry)) return;
    if (x < -0.05f || x > 1.05f || y < -0.2f || y > 1.05f) return;   /* off screen */
    frame.bars[frame.count++] = (ss_health_bar){x, y, fabsf(rx - lx), (float)hp / (float)(hp > initial ? hp : initial)};
}

static void update_hook(void *health, const void *method) {
    update(health, method);
    if (!alive(health)) return;
    for (int i = 0; i < frame.seen_count; ++i)
        if (frame.seen[i] == health) { publish_frame(); break; }
    if (frame.seen_count == SEEN_MAX) publish_frame();
    frame.seen[frame.seen_count++] = health;
    add_bar(health);
}

/* The method slot is a relocated pointer to the method in the IL2CPP code registration. A layout without a
   slot finds it once by its value; there must be exactly one. */
static uintptr_t method_slot(const module_t *main, const enemy_bars_layout *candidate) {
    uintptr_t target = (uintptr_t)main->base + candidate->update;
    if (candidate->update_slot)
        return main->size > candidate->update_slot + sizeof(uintptr_t) &&
               *(const uintptr_t *)(main->base + candidate->update_slot) == target ? candidate->update_slot : 0;
    uintptr_t found = 0; int matches = 0;
    for (uintptr_t offset = main->data_off & ~(uintptr_t)7; offset + 8 <= main->patch_off; offset += 8)
        if (*(const uintptr_t *)(main->base + offset) == target) { found = offset; ++matches; }
    LOG("HEALTH BARS: %d method slots hold HealthManager.Update%s%#lx", matches, matches == 1 ? " at main+" : "; last ",
        (unsigned long)found);
    return matches == 1 ? found : 0;
}

void ss_enemy_bars_install(void) {
    if (!ss_health_bars_enabled()) return;
    for (int i = 0; i < g_nmod; ++i)
        if (!strcmp(g_mod[i].name, "main")) main_module = &g_mod[i];
    if (!main_module) return;
    uintptr_t slot = 0;
    for (unsigned i = 0; i < sizeof layouts / sizeof layouts[0] && !layout; ++i) {
        const enemy_bars_layout *candidate = &layouts[i];
        const uintptr_t code[] = {candidate->update, candidate->world_to_viewport, candidate->camera_rect,
                                  candidate->collider_bounds, candidate->component_transform, candidate->transform_position};
        int ok = main_module->size > candidate->cameras_class + sizeof(uintptr_t);
        for (unsigned k = 0; k < sizeof code / sizeof code[0]; ++k) ok = ok && main_module->text_end > code[k];
        if (ok && (slot = method_slot(main_module, candidate))) layout = candidate;
    }
    if (!layout) { LOG("HEALTH BARS disabled: main module does not match a supported build"); return; }
    uint8_t *base = main_module->base;
    world_to_viewport = (void *)(base + layout->world_to_viewport);
    camera_rect = (void *)(base + layout->camera_rect);
    collider_bounds = (void *)(base + layout->collider_bounds);
    component_transform = (void *)(base + layout->component_transform);
    transform_position = (void *)(base + layout->transform_position);
    trace = getenv("SS_HEALTH_TRACE") != NULL;
    test = getenv("SS_HEALTH_TEST") != NULL;
    uintptr_t *pointer = (uintptr_t *)(base + slot);
    update = (void *)*pointer;
    *pointer = (uintptr_t)update_hook;
    LOG("HEALTH BARS main %s: HealthManager.Update hook installed (slot main+%#lx)", layout->build, (unsigned long)slot);
}
