#include <assert.h>
#include <stdio.h>
#include "ScreenLayout.h"
static int overlap(ss_screen_rect a, ss_screen_rect b) {
    return fmin(a.x+a.width,b.x+b.width)-fmax(a.x,b.x)>0.01 && fmin(a.y+a.height,b.y+b.height)-fmax(a.y,b.y)>0.01;
}
int main(void) {
    const double devices[][6] = {{852,393,62,62,0,21},{852,393,0,0,0,0},{844,390,47,47,0,21},
      {667,375,0,0,0,0},{568,320,0,0,0,0},{320,240,0,0,0,0},{1194,834,0,0,24,20},
      {1024,768,0,0,24,20},{744,600,0,0,38,20},{600,375,0,0,38,20}};
    for(unsigned d=0;d<sizeof(devices)/sizeof(*devices);++d) {
        const double *v=devices[d];
        ss_screen_layout p=ss_screen_layout_make(v[0],v[1],v[2],v[3],v[4],v[5]);
        assert(p.game.width>0 && fabs(p.game.width/p.game.height-16.0/9)<0.0001);
        for(int i=0;i<14;++i) {
            ss_screen_rect c=p.buttons[i];
            assert(c.width>=44 && c.height>=44);
            assert(c.x>=v[2]-0.01 && c.y>=v[4]-0.01);
            assert(c.x+c.width<=v[0]-v[3]+0.01 && c.y+c.height<=v[1]-v[5]+0.01);
            assert(!overlap(c,p.game));
            for(int j=0;j<i;++j) assert(!overlap(c,p.buttons[j]));
        }
        int x=-1,y=-1;
        assert(ss_screen_location(p.game,p.game.x,p.game.y,&x,&y) && x==0 && y==0);
        assert(ss_screen_location(p.game,p.game.x+p.game.width/2,p.game.y+p.game.height/2,&x,&y) && x==640 && y==360);
        assert(!ss_screen_location(p.game,p.game.x+p.game.width,p.game.y+p.game.height,&x,&y) && x==1279 && y==719);
        assert(!ss_screen_location(p.game,p.game.x-1,p.game.y-1,&x,&y) && x==0 && y==0);
        assert(!ss_screen_location(p.game,NAN,0,&x,&y));
    }
    puts("PASS: 10 screen sizes, 14 safe non-overlapping controls, 16:9 picture, touch coordinates");
}
