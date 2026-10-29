/* Shared Switch bootstrap. Game changes belong in each game adapter. */
#include "dm.h"
#include "ss_indirect_branch.h"
#include "ss_display.h"
#include "touchable_game.h"
#include "touchable_game_config.h"
#include "ss_haptics.h"
#include <errno.h>
#include <unistd.h>

struct dm_flags g_flag;
uint64_t g_t0_ns;
void guest_thread_init(void);
void ss_crash_install(void);
uint32_t svc_thread_main_init(void);
void stubs_report(void);

static void *required(const char *name) {
    void *symbol = guest_sym(name);
    if (!symbol) die("Missing boot symbol: %s", name);
    return symbol;
}

static void install_traps(const char *directory) {
    ss_trap_site sites[256];
    size_t count = 0;
    for (int i = 0; i < g_nmod; ++i) {
        module_t *module = &g_mod[i];
        char path[1024], line[256];
        snprintf(path, sizeof(path), "%s/%s.indirect.tsv", directory, module->name);
        FILE *file = fopen(path, "r");
        if (!file) {
            if (errno != ENOENT) die("Cannot open trap table: %s", path);
            continue;
        }
        while (fgets(line, sizeof(line), file)) {
            if (line[0] == '#') continue;
            unsigned long trap, original;
            char kind[32], extra;
            if (sscanf(line, "%lx %lx %31s %c", &trap, &original, kind, &extra) != 3)
                die("Invalid trap table: %s", path);
            ss_trap_kind type = !strcmp(kind, "br_x18") ? SS_BR_X18 :
                !strcmp(kind, "blr_x18") ? SS_BLR_X18 :
                !strcmp(kind, "mov_sp_x18") ? SS_MOV_SP_X18 : 0;
            if (!type || count == 256 || trap < module->patch_off ||
                trap + 4 > module->size || original + 4 >= module->text_end)
                die("Trap table bounds: %s", path);
            sites[count++] = (ss_trap_site){(uintptr_t)module->base + trap,
                (uintptr_t)module->base + original + 4, type};
        }
        fclose(file);
    }
    int error = count ? ss_indirect_install(sites, count, g_key18) : 0;
    if (error) die("Cannot install instruction bridge: %s", strerror(error));
    LOG("%s instruction bridge: %zu sites", TOUCHABLE_GAME_ID, count);
}

static void call0(const char *name) {
    LOG("BOOT %s", name);
    ((void (*)(void))required(name))();
}

int ss_host_run(const char *directory) {
    g_t0_ns = now_ns();
    g_flag.svc = !!getenv("DM_SVC");
    g_flag.thr = !!getenv("DM_THR");
    g_flag.fs = !!getenv("DM_FS");
    g_flag.dbg = 1;
    g_flag.imports = !!getenv("DM_IMPORTS");
    g_flag.bootprof = 1;
    setenv("DM_APP_POOL_ADD_MB", "0", 0);
    ss_crash_install();
    const char *names[] = TOUCHABLE_GAME_MODULES;
    load_modules(directory, names, sizeof(names) / sizeof(names[0]));
    touchable_game_install();
    install_traps(directory);
    stubs_report();
    guest_thread_init();
    uint32_t handle = svc_thread_main_init();
    call0("_init_libc0");
    LOG("BOOT nnosInitialize");
    ((void (*)(uint64_t, uint64_t))required("nnosInitialize"))(handle, 0);
    call0("_init_libc1");
    call0("nninitInitializeSdkModule");
    call0("nninitStartup");
    call0("_init_libc2");
    run_module_inits();
    static char *arguments[] = {TOUCHABLE_GAME_PRODUCT, NULL};
    ((void (*)(int))required("_ZN2nn2os11SetHostArgcEi"))(1);
    ((void (*)(char **))required("_ZN2nn2os11SetHostArgvEPPc"))(arguments);
    call0("nnMain");
    LOG("%s nnMain returned", TOUCHABLE_GAME_ID);
    return 0;
}

#ifdef SS_HOST_CLI
void *front_metal_layer(void) { return NULL; }
void ss_haptics_platform_apply(ss_haptic_output output) { (void)output; }
static void *input_file(void *argument) {
    const char *path = argument;
    void ss_input_set(uint64_t, int32_t, int32_t);
    uint64_t previous = 0; int old_x = 0, old_y = 0;
    for (;;) {
        FILE *file = fopen(path, "r");
        if (file) {
            unsigned long long mask; int x, y;
            if (fscanf(file, "%llx %d %d", &mask, &x, &y) == 3 &&
                (mask != previous || x != old_x || y != old_y)) {
                ss_input_set(mask, x, y); previous = mask; old_x = x; old_y = y;
                LOG("TEST input mask=%#llx stick=%d,%d", mask, x, y);
            }
            fclose(file);
        }
        usleep(10000);
    }
}
/* SS_OPMODE_FILE: a test file with 0 (handheld) or 1 (TV). A change switches the mode live. */
static void *opmode_file(void *argument) {
    const char *path = argument;
    void dm_opmode_set(int mode);
    int previous = -1;
    for (;;) {
        FILE *file = fopen(path, "r");
        int mode;
        if (file) {
            if (fscanf(file, "%d", &mode) == 1 && mode != previous) {
                if (previous >= 0) dm_opmode_set(mode);
                previous = mode;
            }
            fclose(file);
        }
        usleep(100000);
    }
}
int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "Usage: touchable-host MODULE_DIRECTORY\n"); return 2; }
    const char *view = getenv("SS_VIEW_SIZE");
    double width, height;
    if (view && sscanf(view, "%lfx%lf", &width, &height) == 2) ss_display_set_view(width, height);
    const char *path = getenv("SS_INPUT_FILE");
    if (path) { pthread_t thread; if (pthread_create(&thread, NULL, input_file, (void *)path)) die("Cannot start test input"); pthread_detach(thread); }
    const char *mode = getenv("SS_OPMODE_FILE");
    if (mode) { pthread_t thread; if (pthread_create(&thread, NULL, opmode_file, (void *)mode)) die("Cannot start test mode input"); pthread_detach(thread); }
    return ss_host_run(argv[1]);
}
#endif
