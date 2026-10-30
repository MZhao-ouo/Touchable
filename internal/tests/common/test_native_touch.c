#include <assert.h>
#include <pthread.h>
#include "hle.h"
#include "../../runtime/common/ss_haptics.h"
static uint64_t clock_ns = 1000000000;
uint64_t guest_ns(void) { return clock_ns; }
void dm_guest_write_prepare(void *p, size_t n) { (void)p; (void)n; }
void hle_bind_system_event(void *p, int c) { (void)p; (void)c; }
void hle_signal_system_event(void *p) { (void)p; }
int dm_opmode(void) { return 0; }
void ss_haptics_stop(void) {}
void ss_haptics_initialize(uint32_t h) { (void)h; }
void ss_haptics_send(uint32_t h, const ss_vibration_value *v) { (void)h; (void)v; }
ss_vibration_value ss_haptics_actual(uint32_t h) { (void)h; return (ss_vibration_value){0}; }
ss_haptic_status ss_haptics_status(void) { return (ss_haptic_status){0}; }
#include "../../runtime/common/ss_input.c"

static ss_screen_state read_screen(void) {
    unsigned char bytes[16+40*16];
    ss_touch_16(bytes);
    ss_screen_state s; memcpy(&s,bytes,sizeof(s));
    return s;
}
static void *producer(void *arg) {
    (void)arg;
    for(int i=0;i<10000;++i) { ss_touch_screen_set(1,i%1280,i%720); ss_touch_screen_set(0,i%1280,i%720); }
    return NULL;
}
int main(void) {
    assert(read_screen().count==0);
    /* A complete tap between two polls must retain both edges, then release. */
    ss_touch_screen_set(1,640,360); ss_touch_screen_set(0,640,360);
    ss_screen_state start=read_screen(), end=read_screen(), empty=read_screen();
    assert(start.count==1 && start.point.attributes==1 && start.point.x==640 && start.point.y==360);
    assert(end.count==1 && end.point.attributes==2 && end.point.finger_id==start.point.finger_id);
    assert(empty.count==0 && start.sample<end.sample && end.sample<empty.sample);
    ss_touch_screen_set(1,0,0); assert(read_screen().point.attributes==1);
    ss_touch_screen_set(1,1279,719);
    ss_screen_state moved=read_screen();
    assert(moved.count==1 && !moved.point.attributes && moved.point.x==1279 && moved.point.y==719);
    ss_touch_screen_set(1,-3,900);
    moved=read_screen(); assert(moved.point.x==0 && moved.point.y==719);
    ss_touch_screen_clear(); assert(read_screen().count==0);
    /* A cancelled tap leaves neither a queued start nor a stuck contact. */
    ss_touch_screen_set(1,1,2); ss_touch_screen_set(0,1,2); ss_touch_screen_clear();
    assert(read_screen().count==0);
    /* Every ABI variant prepares exactly its state and does not overwrite a guard. */
    void (*readers[])(void *)={ss_touch_1,ss_touch_2,ss_touch_3,ss_touch_4,ss_touch_5,ss_touch_6,ss_touch_7,ss_touch_8,
      ss_touch_9,ss_touch_10,ss_touch_11,ss_touch_12,ss_touch_13,ss_touch_14,ss_touch_15,ss_touch_16};
    for(int n=1;n<=16;++n) {
        unsigned char bytes[16+40*16+8]; memset(bytes,0xa5,sizeof(bytes));
        ss_touch_screen_set(1,640,360);
        readers[n-1](bytes);
        assert(*(int32_t *)(bytes+8)==1);
        for(size_t i=16+40*n;i<sizeof(bytes);++i) assert(bytes[i]==0xa5);
        ss_touch_screen_clear();
    }
    assert(ss_touches_16(NULL,0)==0 && ss_touches_16(NULL,-1)==0);
    /* Queue overflow and a racing producer must still converge to released input. */
    pthread_t worker; assert(!pthread_create(&worker,NULL,producer,NULL));
    for(int i=0;i<10000;++i) { ss_screen_state s=read_screen(); assert(s.count>=0 && s.count<=1); }
    assert(!pthread_join(worker,NULL));
    for(int i=0;i<33;++i) moved=read_screen();
    assert(moved.count==0);
    /* Virtual controller input remains independent of direct touches. */
    ss_input_set(1,0,0); assert(held_buttons()&1); assert(read_screen().count==0);
    puts("PASS: native screen ABI, tap edges, drag, cancellation, 16 capacities, overflow, concurrency, pad isolation");
}
