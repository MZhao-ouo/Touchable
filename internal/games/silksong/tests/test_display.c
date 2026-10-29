/* Build: xcrun clang -Wall -Wextra -Werror
 * -Iruntime/common -Iupstream/d2r-mobile/dm/host games/silksong/tests/test_display.c -o build/games/silksong/tests/test_display
 * Run: build/games/silksong/tests/test_display */
#include <assert.h>
#include <float.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../../../../.touchable/dependencies/d2r-mobile/dm/host/dm.h"

module_t g_mod[MAX_MOD];
int g_nmod;
static unsigned present_gates = 3;
int gate_svc_present(int number) {
    return number == 0xf0 ? !!(present_gates & 1) :
           number == 0xf1 ? !!(present_gates & 2) : 0;
}
/* touchable_game_install also installs the touch adapter. It needs the game modules. */
void ss_touch_ui_install(void) {}
void ss_enemy_bars_install(void) {}

#include "../../../runtime/common/ss_display.c"
#include "../runtime/ss_display.c"

static void expect_resolution(int requested_height, int width, int height) {
    ss_display_size size = ss_display_resolution(requested_height);
    assert(size.width == width && size.height == height);
}

static void check_view_dimensions(void) {
    expect_resolution(720, 1280, 720);
    ss_display_set_view(2868, 1320); /* iPhone 16 Pro Max in landscape. */
    expect_resolution(720, 1564, 720);
    expect_resolution(576, 1252, 576);
    expect_resolution(360, 784, 360);
    expect_resolution(540, 1172, 540);
    expect_resolution(0, 1564, 720);
    expect_resolution(-1, 1564, 720);
    expect_resolution(1080, 1564, 720);
    /* These checks cover the requested display and render-target dimensions.
     * They cannot prove the live camera projection. Verify its native aspect
     * and pixel dimensions on the device to exclude a stretched 16:9 image. */
    ss_display_size size = ss_display_resolution(720);
    assert(size.width > 1280);
    assert(fabs((double)size.width / size.height - 2868.0 / 1320) < 0.002);

    ss_display_set_view(1320, 2868); /* First UIKit layout can be portrait. */
    expect_resolution(720, 1564, 720);
    ss_display_set_view(1920, 1080);
    expect_resolution(720, 1280, 720);
    expect_resolution(576, 1024, 576);
    expect_resolution(360, 640, 360);
    ss_display_set_view(1280, 720);
    expect_resolution(720, 1280, 720);

    ss_display_set_view(2868, 1320);
    const double invalid[][2] = {
        {NAN, 1320}, {2868, NAN}, {INFINITY, 1320}, {2868, INFINITY},
        {-INFINITY, 1320}, {0, 1320}, {2868, 0}, {0, 0},
        {-1, 1320}, {2868, -1}, {1000000, 1}, {DBL_MAX, DBL_MIN}
    };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        ss_display_set_view(invalid[i][0], invalid[i][1]);
        expect_resolution(720, 1564, 720);
    }
}

static void check_gate_registers(void) {
    uint64_t registers[31], original[31];
    for (unsigned i = 0; i < 31; ++i)
        original[i] = 0x1234567800000000ull + i;
    memcpy(registers, original, sizeof(registers));
    assert(ss_display_host_call(0xf0, registers) == 1);
    assert(registers[8] == 1564);
    for (unsigned i = 0; i < 31; ++i)
        if (i != 8) assert(registers[i] == original[i]);
    memcpy(registers, original, sizeof(registers));
    assert(ss_display_host_call(0xf1, registers) == 1);
    assert(registers[8] == 720);
    for (unsigned i = 0; i < 31; ++i)
        if (i != 8) assert(registers[i] == original[i]);
    const int unknown[] = {-1, 0, 0xef, 0xf2, INT32_MAX};
    for (unsigned i = 0; i < sizeof(unknown) / sizeof(unknown[0]); ++i) {
        memcpy(registers, original, sizeof(registers));
        assert(ss_display_host_call(unknown[i], registers) == 0);
        assert(memcmp(registers, original, sizeof(registers)) == 0);
    }
}

static struct {
    int calls, width, height, mode;
    const void *refresh_rate;
} native_resolution;
static void capture_native_resolution(int width, int height, int mode,
                                      const void *refresh_rate) {
    ++native_resolution.calls;
    native_resolution.width = width;
    native_resolution.height = height;
    native_resolution.mode = mode;
    native_resolution.refresh_rate = refresh_rate;
}
static void check_native_forwarding(void) {
    set_resolution = capture_native_resolution;
    const uint64_t refresh_rate[2] = {60, 1};
    phone_set_resolution(1280, 720, 3, refresh_rate);
    assert(native_resolution.calls == 1);
    assert(native_resolution.width == 1564 && native_resolution.height == 720);
    assert(native_resolution.mode == 3 && native_resolution.refresh_rate == refresh_rate);
    phone_set_resolution(1024, 576, 5, refresh_rate);
    assert(native_resolution.calls == 2);
    assert(native_resolution.width == 1252 && native_resolution.height == 576);
    assert(native_resolution.mode == 5 && native_resolution.refresh_rate == refresh_rate);
    phone_set_resolution(640, 360, 7, NULL);
    assert(native_resolution.calls == 3);
    assert(native_resolution.width == 784 && native_resolution.height == 360);
    assert(native_resolution.mode == 7 && native_resolution.refresh_rate == NULL);
    ss_display_set_view(1920, 1080);
    phone_set_resolution(1280, 720, 4, refresh_rate);
    assert(native_resolution.width == 1280 && native_resolution.height == 720);
    ss_display_set_view(2868, 1320);
}

static void expect_output_height(const char *value, int height) {
    atomic_store(&output_height, 0);
    assert(setenv("SS_RENDER_HEIGHT", value, 1) == 0);
    assert(ss_display_output_height() == height);
}

static void check_render_height(void) {
    ss_display_set_view(2868, 1320);
    expect_output_height("1080", 882); /* The 1920-wide window limits a wide view. */
    expect_resolution(720, 1916, 882);
    expect_resolution(576, 1532, 706);
    set_resolution = capture_native_resolution;
    phone_set_resolution(1916, 882, 3, NULL);
    assert(native_resolution.width == 1916 && native_resolution.height == 882);
    phone_set_resolution(1532, 706, 3, NULL);
    assert(native_resolution.width == 1532 && native_resolution.height == 706);
    ss_display_set_view(2266, 1488); /* iPad mini (A17 Pro) in landscape. */
    assert(ss_display_output_height() == 1080);
    expect_resolution(720, 1644, 1080);
    expect_resolution(576, 1316, 864);
    phone_set_resolution(1644, 1080, 3, NULL);
    assert(native_resolution.width == 1644 && native_resolution.height == 1080);
    ss_display_set_view(1920, 1080);
    expect_resolution(720, 1920, 1080);
    ss_display_set_view(2266, 1488);
    expect_output_height("1440", 1260);
    expect_resolution(720, 1920, 1260);
    expect_output_height("1081", 1080);
    const char *ignored[] = {"", "719", "1442", "-1080", "high"};
    for (unsigned i = 0; i < sizeof(ignored) / sizeof(ignored[0]); ++i) {
        expect_output_height(ignored[i], 720);
        expect_resolution(720, 1096, 720);
    }
    assert(unsetenv("SS_RENDER_HEIGHT") == 0);
    atomic_store(&output_height, 0);
    ss_display_set_view(2868, 1320);
    assert(ss_display_output_height() == 720);
    set_resolution = NULL;
}

static void expect_install_rejection(module_t *module, const ss_display_layout *layout, int error) {
    pid_t pid = fork();
    assert(pid >= 0);
    if (!pid) {
        assert(freopen("/dev/null", "w", stderr));
        if (error == 0) *(uint32_t *)(module->base + layout->defaults) = 1279;
        else if (error == 1) *(uintptr_t *)(module->base + layout->resolution_slot) += 4;
        else if (error < 4) present_gates &= ~(error == 2 ? 1 : 2);
        else {
            uint32_t *handheld = (void *)(module->base + layout->handheld);
            handheld[error - 4] += 1;
        }
        ss_display_install();
        _exit(0);
    }
    int status;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}

static void check_install_data_only(const ss_display_layout *layout) {
    const size_t page = (size_t)getpagesize();
    const size_t length = (layout->resolution_slot + 0x20 + page - 1) / page * page;
    uint8_t *memory = mmap(NULL, length, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(memory != MAP_FAILED);
    g_nmod = 2;
    strcpy(g_mod[0].name, "sdk"); /* Select the main module by its name. */
    module_t *module = &g_mod[1];
    strcpy(module->name, "main");
    module->base = memory;
    module->size = length;
    module->text_end = layout->set_resolution + 0x30;

    uint8_t *defaults_region = memory + layout->defaults - 0x10;
    uint8_t *handheld_region = memory + layout->handheld - 0x10;
    uint8_t *slot_region = memory + layout->resolution_slot - 0x10;
    memset(defaults_region, 0xa5, 64);
    memset(handheld_region, 0x3c, 64);
    memset(slot_region, 0x5a, 48);
    uint32_t *defaults = (void *)(memory + layout->defaults);
    const uint32_t expected[4] = {1280, 720, 1920, 1080};
    memcpy(defaults, expected, sizeof(expected));
    uint32_t *handheld = (void *)(memory + layout->handheld);
    const uint32_t handheld_expected[4] = {1024, 576, 1280, 720};
    memcpy(handheld, handheld_expected, sizeof(handheld_expected));
    uintptr_t *slot = (void *)(memory + layout->resolution_slot);
    *slot = (uintptr_t)memory + layout->set_resolution;

    uint8_t original_defaults[64], original_handheld[64], original_slot[48], original_code[128];
    memcpy(original_defaults, defaults_region, sizeof(original_defaults));
    memcpy(original_handheld, handheld_region, sizeof(original_handheld));
    memcpy(original_slot, slot_region, sizeof(original_slot));
    uint8_t *code = memory + layout->set_resolution - 0x50;
    memset(code, 0xcc, sizeof(original_code));
    memcpy(original_code, code, sizeof(original_code));
    /* All text pages are read-only. Any attempt to patch executable code
     * will fault, including writes away from the native method entry. */
    size_t text_pages = (module->text_end + page - 1) / page * page;
    assert(mprotect(memory, text_pages, PROT_READ) == 0);

    for (int error = 0; error < 8; ++error) expect_install_rejection(module, layout, error);
    ss_display_install();
    assert(defaults[0] == 1564 && defaults[1] == 720);
    assert(defaults[2] == 1920 && defaults[3] == 1080); /* Keep max size. */
    assert(handheld[0] == 1252 && handheld[1] == 576);
    assert(handheld[2] == 1564 && handheld[3] == 720);
    assert(fabs((double)handheld[0] / handheld[1] - 2868.0 / 1320) < 0.002);
    assert(fabs((double)handheld[2] / handheld[3] - 2868.0 / 1320) < 0.002);
    assert(set_resolution == (void *)((uintptr_t)memory + layout->set_resolution));
    assert(*slot == (uintptr_t)phone_set_resolution);
    memcpy(original_defaults + 16, defaults, 4); /* Only native width changes. */
    memcpy(original_handheld + 16, handheld, 4); /* Only scaled RT width changes. */
    memcpy(original_handheld + 24, handheld + 2, 4); /* Only full width changes. */
    memcpy(original_slot + 16, slot, sizeof(*slot));
    assert(memcmp(original_defaults, defaults_region, sizeof(original_defaults)) == 0);
    assert(memcmp(original_handheld, handheld_region, sizeof(original_handheld)) == 0);
    assert(memcmp(original_slot, slot_region, sizeof(original_slot)) == 0);
    assert(memcmp(original_code, code, sizeof(original_code)) == 0);
    set_resolution = NULL;
    assert(munmap(memory, length) == 0);
    memset(g_mod, 0, sizeof(g_mod));
    g_nmod = 0;
}

int main(void) {
    check_view_dimensions();
    check_gate_registers();
    for (unsigned i = 0; i < sizeof(layouts) / sizeof(layouts[0]); ++i)
        check_install_data_only(&layouts[i]);
    check_native_forwarding();
    check_render_height();
    puts("PASS: phone display and recommended world RT dimensions, lower resolutions, portrait layout, invalid views, register ABI, native forwarding, SS_RENDER_HEIGHT with the 1920-wide window limit, pinned data guards for each supported main build, unchanged heights/max sizes, and read-only guest code");
    return 0;
}
