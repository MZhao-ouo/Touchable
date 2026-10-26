#include "dm.h"
#include "ss_display.h"
#include "touchable_game.h"
#include "ss_touch_ui.h"

static void (*set_resolution)(int, int, int, const void *);

/* Data slots of each supported main module. defaults and handheld are the
   SwitchPlatform resolution records. resolution_slot is the relocated native
   method pointer of Screen.SetResolution. */
typedef struct {
    const char *build;
    uintptr_t defaults, handheld, resolution_slot, set_resolution;
} ss_display_layout;

static const ss_display_layout layouts[] = {
    {"9049ea1c (v0)", 0x61fd050, 0x61f40a0, 0x772ad60, 0x1a96dd0},
    {"fc9ea4cc (v655360, 1.0.30000)", 0x6679240, 0x666efa0, 0x7cfaeb0, 0x1b15c80},
};

static void phone_set_resolution(int width, int height, int mode, const void *refresh_rate) {
    (void)width;
    /* The records hold output pixels. Convert the height back to the 720-line reference. */
    ss_display_size size = ss_display_resolution(height * 720 / ss_display_output_height());
    set_resolution(size.width, size.height, mode, refresh_rate);
}

int ss_display_host_call(int number, uint64_t *registers) {
    if (number != 0xf0 && number != 0xf1) return 0;
    ss_display_size size = ss_display_resolution(720);
    registers[8] = number == 0xf0 ? (uint32_t)size.width : (uint32_t)size.height;
    return 1;
}

static int layout_matches(const module_t *main, const ss_display_layout *layout) {
    if (main->size <= layout->resolution_slot + sizeof(uintptr_t) ||
        main->size <= layout->defaults + 16 || main->size <= layout->handheld + 16 ||
        main->text_end <= layout->set_resolution + 0x1c)
        return 0;
    const uint32_t expected[4] = {1280, 720, 1920, 1080};
    const uint32_t expected_handheld[4] = {1024, 576, 1280, 720};
    return !memcmp((void *)(main->base + layout->defaults), expected, sizeof(expected)) &&
           !memcmp((void *)(main->base + layout->handheld), expected_handheld, sizeof(expected_handheld)) &&
           *(uintptr_t *)(main->base + layout->resolution_slot) == (uintptr_t)main->base + layout->set_resolution;
}

void ss_display_install(void) {
    module_t *main = NULL;
    for (int i = 0; i < g_nmod; ++i)
        if (!strcmp(g_mod[i].name, "main")) main = &g_mod[i];
    if (!main) die("Display adapter cannot find the main module");
    /* Check both the original default records and the relocated native
       method pointer. Guest executable pages remain read-only. */
    const ss_display_layout *layout = NULL;
    for (unsigned i = 0; i < sizeof(layouts) / sizeof(layouts[0]); ++i)
        if (layout_matches(main, &layouts[i])) layout = &layouts[i];
    if (!layout) die("Display adapter module data does not match a supported game build");
    if (!gate_svc_present(0xf0) || !gate_svc_present(0xf1))
        die("Display adapter requires the resolution constructor gates");
    uint32_t *defaults = (void *)(main->base + layout->defaults);
    uint32_t *handheld = (void *)(main->base + layout->handheld);
    uintptr_t *resolution_slot = (void *)(main->base + layout->resolution_slot);
    set_resolution = (void *)*resolution_slot;
    ss_display_size size = ss_display_resolution(720);
    defaults[0] = size.width;
    defaults[1] = size.height;
    /* SwitchPlatform publishes this scaled size to CameraRenderScaled.
       Its render texture determines the world camera's projection ratio.
       Resizing only Screen would stretch the old 16:9 world texture. */
    /* SS_RENDER_HEIGHT scales both handheld records. The world texture keeps
       the game's 576:720 ratio to the output. */
    ss_display_size world = ss_display_resolution(handheld[1]);
    handheld[0] = world.width;
    handheld[1] = world.height;
    handheld[2] = size.width;
    handheld[3] = size.height;
    *resolution_slot = (uintptr_t)phone_set_resolution;
    LOG("DISPLAY main %s adaptive %dx%d aspect=%.6f; world texture %dx%d; output height %d", layout->build, size.width, size.height,
        (double)size.width / size.height, world.width, world.height, ss_display_output_height());
}

void ss_enemy_bars_install(void);
void touchable_game_install(void) { ss_display_install(); ss_touch_ui_install(); ss_enemy_bars_install(); }
int touchable_game_host_call(int number, uint64_t *registers) { return ss_display_host_call(number, registers); }
