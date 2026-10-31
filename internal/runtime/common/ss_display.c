/* Shared phone render-size selection. Game module changes use game hooks. */
#include "ss_display.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>

static _Atomic uint64_t view_size = (uint64_t)720 << 32 | 1280;
static _Atomic int output_height, world_percent;

void ss_display_set_view(double width, double height) {
    if (!isfinite(width) || !isfinite(height) || width <= 0 || height <= 0) return;
    /* The guest always renders in landscape. UIKit can lay out the launch
       view in portrait before it applies the scene orientation. */
    double aspect = fmax(width, height) / fmin(width, height);
    if (!isfinite(aspect) || aspect > 1920.0 / 720) return;
    int render_width = (int)(floor(720 * aspect / 4 + 0.5) * 4);
    if (render_width < 720 || render_width > 1920) return;
    atomic_store(&view_size, (uint64_t)720 << 32 | (uint32_t)render_width);
}

/* SS_RENDER_HEIGHT requests an output height from 720 to 1440. The default is 720. */
static int requested_output_height(void) {
    int height = atomic_load(&output_height);
    if (height) return height;
    height = 720;
    const char *value = getenv("SS_RENDER_HEIGHT");
    if (value && *value) {
        long requested = strtol(value, NULL, 10);
        if (requested >= 720 && requested <= 1440) height = (int)requested & ~1;
    }
    atomic_store(&output_height, height);
    return height;
}

/* The guest window textures are 1920 pixels wide. A wide view gets a lower
   height so that the output width stays in the window. */
int ss_display_output_height(void) {
    int height = requested_output_height();
    int limit = 1920 * 720 / (int)(uint32_t)atomic_load(&view_size) & ~1;
    return height < limit ? height : limit;
}

static ss_display_size view_resolution(int height) {
    uint64_t size = atomic_load(&view_size);
    int width = (int)(floor((double)(uint32_t)size * height / 720 / 4 + 0.5) * 4);
    return (ss_display_size){width, height};
}

/* requested_height is in the 720-line reference space of the game records.
   The result is scaled to the output height. */
ss_display_size ss_display_resolution(int requested_height) {
    int output = ss_display_output_height();
    int reference = requested_height > 0 && requested_height < 720 ? requested_height : 720;
    int height = output == 720 ? reference : (int)(floor((double)reference * output / 720 / 2 + 0.5) * 2);
    return view_resolution(height);
}

/* SS_WORLD_SCALE requests the world texture size in percent, from 50 to 100. The default is 100. */
int ss_display_world_percent(void) {
    int percent = atomic_load(&world_percent);
    if (percent) return percent;
    percent = 100;
    const char *value = getenv("SS_WORLD_SCALE");
    if (value && *value) {
        char *end;
        long requested = strtol(value, &end, 10);
        if (!*end && requested >= 50 && requested <= 100) percent = (int)requested;
    }
    atomic_store(&world_percent, percent);
    return percent;
}

ss_display_size ss_display_world_resolution(int requested_height) {
    ss_display_size size = ss_display_resolution(requested_height);
    int percent = ss_display_world_percent();
    if (percent == 100) return size;
    return view_resolution((int)(floor((double)size.height * percent / 100 / 2 + 0.5) * 2));
}
