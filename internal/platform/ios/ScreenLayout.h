/* Geometry for direct screen input and opaque controls outside a 16:9 picture. */
#ifndef SS_SCREEN_LAYOUT_H
#define SS_SCREEN_LAYOUT_H
#include <math.h>
typedef struct { double x, y, width, height; } ss_screen_rect;
typedef struct { ss_screen_rect game, buttons[14]; } ss_screen_layout;
static inline ss_screen_layout ss_screen_layout_make(double width, double height, double left, double right,
                                                     double top, double bottom) {
    ss_screen_layout p = {0};
    double w = width-left-right, h = height-top-bottom;
    if (w <= 0 || h <= 0) return p;
    ss_screen_rect area = {left, top, w, h};
    if (h >= 332 && w >= 200) {
        area.x += 56; area.width -= 112;
        for (int i=0; i<14; ++i)
            p.buttons[i] = (ss_screen_rect){left+(i/7)*(w-44), top+(i%7)*(h-44)/6, 44, 44};
    } else {
        int columns = (int)fmin(7, fmax(1, floor((w+4)/48)));
        int rows = (14+columns-1)/columns;
        double bw = fmin(44,w), controls_height = rows*48-4;
        area.height -= controls_height+12;
        for (int i=0; i<14; ++i)
            p.buttons[i] = (ss_screen_rect){left+(w-(columns*(bw+4)-4))/2+(i%columns)*(bw+4),
                                          top+h-controls_height+(i/columns)*48,bw,44};
    }
    if (area.width <= 0 || area.height <= 0) return p;
    double gw = fmin(area.width, area.height*16/9), gh = gw*9/16;
    p.game = (ss_screen_rect){area.x+(area.width-gw)/2,area.y+(area.height-gh)/2,gw,gh};
    return p;
}
static inline int ss_screen_location(ss_screen_rect game, double x, double y, int *gx, int *gy) {
    if (game.width <= 0 || game.height <= 0 || !isfinite(x) || !isfinite(y)) return 0;
    int inside = x>=game.x && y>=game.y && x<game.x+game.width && y<game.y+game.height;
    *gx = (int)fmin(1279,fmax(0,floor((x-game.x)*1280/game.width)));
    *gy = (int)fmin(719,fmax(0,floor((y-game.y)*720/game.height)));
    return inside;
}
#endif
