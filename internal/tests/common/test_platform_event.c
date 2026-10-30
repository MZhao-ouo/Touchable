/* Build with -pthread and -I.touchable/dependencies/d2r-mobile/dm/host. */
#include "hle.h"
#include <assert.h>

static void *bound;
static int clear_mode, binds, signals;
void hle_bind_system_event(void *event, int auto_clear) {
    bound = event;
    clear_mode = auto_clear;
    ++binds;
}
void hle_signal_system_event(void *event) { (void)event; ++signals; }
void nvn_present_hz(int hz) { (void)hz; }
void ss_input_style_changed(void) {}
uint64_t guest_ns(void) { return 0; }
/* Capture registrations without Mach-O section traversal. ASan inserts
   redzones between section records, so they are not a contiguous C array. */
static void *event_function, *resolution_function;
static void *prepared[2];
static unsigned prepare_count;
void dm_guest_write_prepare(void *address, size_t size) {
    assert(size == sizeof(int));
    assert(prepare_count < 2);
    prepared[prepare_count++] = address;
}
#undef HLE_FORCE
#define HLE_FORCE(symbol, reason, ret, name, ...) \
    static ret name(__VA_ARGS__); \
    __attribute__((constructor)) static void register_##name(void) { \
        if (!strcmp(symbol, "_ZN2nn2oe38GetDefaultDisplayResolutionChangeEventEPNS_2os11SystemEventE")) \
            event_function = (void *)name; \
        if (!strcmp(symbol, "_ZN2nn2oe27GetDefaultDisplayResolutionEPiS1_")) \
            resolution_function = (void *)name; \
    } \
    static ret name(__VA_ARGS__)
#include "../../runtime/common/ss_platform.c"

int main(void) {
    void (*get_event)(void *) = event_function;
    assert(get_event && "Resolution event must bypass console service access");
    _Alignas(16) uint8_t events[2][0x30] = {{0}};
    for (int i = 0; i < 2; ++i) {
        get_event(events[i]);
        assert(bound == events[i]);
        assert(clear_mode == 1);
        assert(binds == i + 1);
        assert(signals == 0);
    }
    void (*get_resolution)(int *, int *) = resolution_function;
    assert(get_resolution && "Resolution query must bypass console service access");
    struct { int guard0, width, height, guard1; } output = {123, -1, -1, 456};
    get_resolution(&output.width, &output.height);
    assert(output.width == 1280 && output.height == 720);
    assert(output.guard0 == 123 && output.guard1 == 456);
    assert(prepare_count == 2 && prepared[0] == &output.width && prepared[1] == &output.height);
    puts("PASS: resolution event takeover, caller storage, auto-clear, no false signal");
    return 0;
}
