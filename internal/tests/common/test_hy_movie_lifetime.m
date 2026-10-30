#include "../../games/hayarigami/runtime/hy_media.m"
void (*g_ss_media_suspend_hook)(int);
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
_Noreturn void hy_fail(const char *format,...) {va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);abort();}
typedef struct {void *data;size_t capacity;int offset,size;} TestBuffer;
static size_t buffer_capacity(void *p){return ((TestBuffer *)p)->capacity;}
static void *buffer_data(void *p){return ((TestBuffer *)p)->data;}
static void buffer_range(void *p,int offset,int size){((TestBuffer *)p)->offset=offset;((TestBuffer *)p)->size=size;}
void *guest_sym(const char *name){if(strstr(name,"Buffer8Capacity"))return buffer_capacity;if(strstr(name,"Buffer4Data"))return buffer_data;if(strstr(name,"Buffer8SetRange"))return buffer_range;hy_fail("unexpected movie export %s",name);}
int main(void){@autoreleasepool{
    HyMovieDecoder *s=[HyMovieDecoder new];s->width=1920;s->height=1080;s->format=4;
    CVPixelBufferRef image=NULL;NSDictionary *attributes=@{(id)kCVPixelBufferIOSurfacePropertiesKey:@{}};
    assert(!CVPixelBufferCreate(NULL,1920,1080,kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,(__bridge CFDictionaryRef)attributes,&image));
    MovieObject object={.state=(__bridge void *)s};
    TestBuffer client={calloc(1,1920*1080*3/2),1920*1080*3/2,0,0};
    for(int i=0;i<120;i++){
        decoded_video((__bridge void *)s,NULL,0,0,image,CMTimeMake(i,30),kCMTimeInvalid);
        __weak NSData *bytes=s->pending.lastObject.bytes;
        assert(bytes.length==1920*1080*3/2);
        [s->ready addObjectsFromArray:s->pending];[s->pending removeAllObjects];
        int index,remaining;int64_t time;unsigned flags;
        assert(!acquire_output(&object,&index,&time,&flags,&remaining));
        assert(!output_buffer(&object,index,&client));assert(!release_output(&object,index));
        assert(!bytes && "client output copy leaves decoded frame storage autoreleased on the guest thread");
    }
    free(client.data);CFRelease(image);puts("PASS: 120 full HD video callbacks release frame storage immediately after consumption");
}}
