#include "hle.h"
#include <assert.h>
#include <stdatomic.h>
static void *open_default_function, *open_named_function;
#undef HLE_FORCE
#define HLE_FORCE(symbol, reason, ret, name, ...) \
    static ret name(__VA_ARGS__); \
    __attribute__((constructor)) static void register_##name(void) { \
        if (!strcmp(symbol,"_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutERKNS0_17AudioOutParameterE")) open_default_function=(void *)name; \
        if (!strcmp(symbol,"_ZN2nn5audio12OpenAudioOutEPNS0_8AudioOutEPKcRKNS0_17AudioOutParameterE")) open_named_function=(void *)name; \
    } \
    static ret name(__VA_ARGS__)
#include "../../runtime/common/ss_audio.c"
uint64_t now_ns(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec; }
static struct {const void *key;void *value;} objects[8];
void *objmap_get(const void *key) { for(int i=0;i<8;i++)if(objects[i].key==key)return objects[i].value;return NULL; }
void objmap_put(const void *key,void *value) { for(int i=0;i<8;i++)if(!objects[i].key){objects[i].key=key;objects[i].value=value;return;}abort(); }
void *objmap_del(const void *key) { for(int i=0;i<8;i++)if(objects[i].key==key){objects[i].key=NULL;return objects[i].value;}return NULL; }
static _Atomic int signals,binds;
void hle_signal_system_event(void *p) { (void)p;signals++; }
void hle_bind_system_event(void *p,int clear) { (void)p;(void)clear;binds++; }
int main(void) {
    Result (*open_default)(void *,const uint8_t *)=open_default_function;
    Result (*open_named)(void *,const char *,const uint8_t *)=open_named_function;
    assert(open_default && open_named && "AudioOut no-event overloads are missing");
    uint8_t parameter[16];h_au_InitParam(parameter);
    for(int i=0;i<2;++i){
        char out[64];gbuf buffer;int16_t pcm[480*2]={0};
        assert(!(i?open_named(out,"DeviceOut",parameter):open_default(out,parameter)));
        assert(h_au_Rate(out)==48000 && h_au_Ch(out)==2);
        h_au_SetBuf(&buffer,pcm,sizeof pcm,sizeof pcm);assert(h_au_Append(out,&buffer));
        assert(!h_au_Start(out));uint64_t start=now_ns();gbuf *released=NULL;
        while(!released && now_ns()-start<1000000000){released=h_au_Released(out);if(!released)usleep(1000);}
        assert(released==&buffer);h_au_Close(out);
    }
    assert(!signals && !binds);
    puts("PASS: default/named no-event AudioOut consumes PCM and releases buffers without event access");
}
