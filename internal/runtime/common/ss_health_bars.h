/* Enemy health bars from a game adapter to the iPhone layer (platform/ios/App.m).
 * The adapter publishes the bars of each game frame. The phone draws them above the game picture. */
#ifndef SS_HEALTH_BARS_H
#define SS_HEALTH_BARS_H
#include <stdint.h>

/* x and y: the center of the bar's lower edge in the game picture, 0 to 1 from the upper left.
   width: part of the picture width. fill: remaining health, 0 to 1. */
typedef struct { float x, y, width, fill; } ss_health_bar;
enum { SS_HEALTH_BARS_MAX = 48 };

/* SS_HEALTH_BARS=1 turns the bars on. The iOS app sets it from the Settings app. */
int ss_health_bars_enabled(void);
/* Called by the game adapter once for each game frame, on the game thread. */
void ss_health_bars_publish(const ss_health_bar *bars, int count);
/* Called by the phone on any thread. Return the bar count of the last frame. *age_ns is the time since it was published. */
int ss_health_bars_read(ss_health_bar *bars, int max, uint64_t *age_ns);
#endif
