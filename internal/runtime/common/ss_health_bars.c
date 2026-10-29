/* Storage for the enemy health bars of the last game frame. See ss_health_bars.h. */
#include "ss_health_bars.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static ss_health_bar published[SS_HEALTH_BARS_MAX];
static int published_count;
static uint64_t published_at;

int ss_health_bars_enabled(void) {
    static int enabled = -1;
    if (enabled < 0) { const char *value = getenv("SS_HEALTH_BARS"); enabled = value && *value == '1'; }
    return enabled;
}

void ss_health_bars_publish(const ss_health_bar *bars, int count) {
    if (count < 0) count = 0;
    if (count > SS_HEALTH_BARS_MAX) count = SS_HEALTH_BARS_MAX;
    pthread_mutex_lock(&lock);
    memcpy(published, bars, count * sizeof *bars);
    published_count = count;
    published_at = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    pthread_mutex_unlock(&lock);
}

int ss_health_bars_read(ss_health_bar *bars, int max, uint64_t *age_ns) {
    pthread_mutex_lock(&lock);
    int count = published_count < max ? published_count : max;
    memcpy(bars, published, count * sizeof *bars);
    uint64_t at = published_at;
    pthread_mutex_unlock(&lock);
    if (age_ns) *age_ns = at ? clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - at : UINT64_MAX;
    return count;
}
