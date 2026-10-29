#import <VideoToolbox/VideoToolbox.h>
static OSStatus interrupted_decode(VTDecompressionSessionRef, CMSampleBufferRef, VTDecodeFrameFlags, void *, VTDecodeInfoFlags *);
#define VTDecompressionSessionDecodeFrame interrupted_decode
#include "../../games/hayarigami/runtime/hy_media.m"
#undef VTDecompressionSessionDecodeFrame
void (*g_ss_media_suspend_hook)(int);
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <unistd.h>
typedef struct {void *data;size_t capacity;int offset,size;} TestBuffer;
static NSData *config;static const char *test_mime;
_Noreturn void hy_fail(const char *format,...) {va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);abort();}
static int find_int(void *m,const char *key,int *out){(void)m;if(!strcmp(key,"width")||!strcmp(key,"height"))*out=32;else if(!strcmp(key,"sample-rate"))*out=48000;else if(!strcmp(key,"channel-count"))*out=2;else return 0;return 1;}
static int find_string(void *m,const char *key,const char **out){(void)m;(void)key;*out=test_mime;return 1;}
static int find_buffer(void *m,const char *key,void **out){(void)m;if(strcmp(key,"csd-0"))return 0;*out=(__bridge void *)config;return 1;}
static const void *buffer_data(void *p){return p==(__bridge void *)config ? config.bytes : ((TestBuffer *)p)->data;}
static int buffer_size(void *p){return (int)((__bridge NSData *)p).length;}
static size_t buffer_capacity(void *p){return ((TestBuffer *)p)->capacity;}
static void buffer_range(void *p,int offset,int size){((TestBuffer *)p)->offset=offset;((TestBuffer *)p)->size=size;}
void *guest_sym(const char *name){if(strstr(name,"Buffer8Capacity"))return buffer_capacity;if(strstr(name,"Buffer8SetRange"))return buffer_range;if(strstr(name,"FindInt32"))return find_int;if(strstr(name,"FindString"))return find_string;if(strstr(name,"FindBuffer"))return find_buffer;if(strstr(name,"Buffer4Data"))return buffer_data;if(strstr(name,"Buffer4Size"))return buffer_size;hy_fail("unexpected movie test export %s",name);}
static dispatch_semaphore_t block_entered,block_release;
static void *blocking_decode(void *unused){(void)unused;native_decode(^{dispatch_semaphore_signal(block_entered);dispatch_semaphore_wait(block_release,DISPATCH_TIME_FOREVER);});return NULL;}
static _Atomic int pause_entered,pause_decoded;
static void *paused_decode(void *unused){(void)unused;atomic_store(&pause_entered,1);native_decode(^{atomic_store(&pause_decoded,1);});return NULL;}
static _Atomic int interrupt_mode,send_done;
static dispatch_semaphore_t interrupted;
static HyMovieDecoder *interrupted_state;
static OSStatus interrupted_decode(VTDecompressionSessionRef session,CMSampleBufferRef sample,VTDecodeFrameFlags flags,void *context,VTDecodeInfoFlags *info){
 int mode=atomic_exchange(&interrupt_mode,0);
 if(mode){hy_movie_set_active(0);dispatch_semaphore_signal(interrupted);if(mode==1)return kVTVideoDecoderNotAvailableNowErr;decoded_video((__bridge void *)interrupted_state,NULL,kVTVideoDecoderNotAvailableNowErr,0,NULL,kCMTimeInvalid,kCMTimeInvalid);return 0;}
 return VTDecompressionSessionDecodeFrame(session,sample,flags,context,info);
}
static void *send_interrupted(void *object){assert(!send_input(object,0,0,(int)config.length,12345,0));atomic_store(&send_done,1);return NULL;}
int main(int argc,char **argv){@autoreleasepool{
 hy_movie_set_active(0);pthread_t waiting;assert(!pthread_create(&waiting,NULL,paused_decode,NULL));while(!atomic_load(&pause_entered))sched_yield();usleep(20000);assert(!atomic_load(&pause_decoded));hy_movie_set_active(1);assert(!pthread_join(waiting,NULL));assert(atomic_load(&pause_decoded));
 block_entered=dispatch_semaphore_create(0);block_release=dispatch_semaphore_create(0);pthread_t blocker;assert(!pthread_create(&blocker,NULL,blocking_decode,NULL));dispatch_semaphore_wait(block_entered,DISPATCH_TIME_FOREVER);
 atomic_store(&pause_entered,0);atomic_store(&pause_decoded,0);assert(!pthread_create(&waiting,NULL,paused_decode,NULL));while(!atomic_load(&pause_entered))sched_yield();usleep(20000);hy_movie_set_active(0);dispatch_semaphore_signal(block_release);assert(!pthread_join(blocker,NULL));usleep(20000);assert(!atomic_load(&pause_decoded) && "queued decode must recheck foreground state on its worker");hy_movie_set_active(1);assert(!pthread_join(waiting,NULL));assert(atomic_load(&pause_decoded));
 assert(argc==5);NSData *video_data=[NSData dataWithContentsOfFile:@(argv[1])];assert(video_data.length);config=video_data;test_mime="video/avc";
 MovieObject object={0};construct(&object,1,4,1,16);assert(!configure(&object,(void *)1));assert(!start(&object));HyMovieDecoder *s=state(&object);memcpy(s->input.mutableBytes,video_data.bytes,video_data.length);int index,remaining;assert(!acquire_input(&object,&index,&remaining));assert(!send_input(&object,index,0,(int)video_data.length,12345,0));assert(!acquire_input(&object,&index,&remaining));assert(!send_input(&object,index,0,0,45678,4));
 int64_t time;unsigned flags;assert(!acquire_output(&object,&index,&time,&flags,&remaining));assert(time==12345 && flags==0);HyMovieFrame *frame=s->held[@(index)];assert(frame.bytes.length==32*32*3/2);uint8_t copied[32*32*3/2+16];memset(copied,0x5a,sizeof copied);TestBuffer client={copied,32*32*3/2,0,0};assert(!output_buffer(&object,index,&client));assert(client.data==copied && client.size==32*32*3/2 && !memcmp(copied,frame.bytes.bytes,frame.bytes.length) && copied[sizeof copied-1]==0x5a);const uint8_t *yuv=frame.bytes.bytes;assert(yuv[0]>=79 && yuv[0]<=84);assert(yuv[1024]>=88 && yuv[1024]<=93);assert(yuv[1025]>=237 && yuv[1025]<=243);assert(!release_output(&object,index));assert(!acquire_output(&object,&index,&time,&flags,&remaining));assert(flags==4 && time==45678);assert(!release_output(&object,index));destroy(&object);
 for(int mode=1;mode<=2;mode++){
 construct(&object,1,4,1,16);assert(!configure(&object,(void *)1));assert(!start(&object));s=state(&object);interrupted_state=s;memcpy(s->input.mutableBytes,video_data.bytes,video_data.length);assert(!acquire_input(&object,&index,&remaining));
 interrupted=dispatch_semaphore_create(0);atomic_store(&interrupt_mode,mode);atomic_store(&send_done,0);pthread_t sender;assert(!pthread_create(&sender,NULL,send_interrupted,&object));dispatch_semaphore_wait(interrupted,DISPATCH_TIME_FOREVER);usleep(20000);assert(!atomic_load(&send_done) && !last_error(&object));hy_movie_set_active(1);assert(!pthread_join(sender,NULL));assert(atomic_load(&send_done));
 assert(!acquire_input(&object,&index,&remaining));assert(!send_input(&object,index,0,0,45678,4));int delivered=0;while(!acquire_output(&object,&index,&time,&flags,&remaining)){if(!(flags&4)){assert(time==12345);delivered++;}assert(!release_output(&object,index));}assert(delivered==1);destroy(&object);
 }puts("PASS: foreground resume retries direct and callback decoder unavailability with one output frame");
 uint8_t asc[]={0x11,0x90,0x56,0xe5,0x00};config=[NSData dataWithBytes:asc length:sizeof asc];test_mime="audio/mp4a-latm";construct(&object,4,1,1,16);assert(!configure(&object,(void *)1));assert(!start(&object));s=state(&object);NSData *aac=[NSData dataWithContentsOfFile:@(argv[2])];const uint8_t *p=aac.bytes;NSUInteger pos=0,packets=0,pcm_bytes=0;
 while(pos+7<=aac.length){assert(p[pos]==255);unsigned header=(p[pos+1]&1)?7:9;unsigned length=((p[pos+3]&3)<<11)|(p[pos+4]<<3)|(p[pos+5]>>5);assert(length>=header && pos+length<=aac.length);assert(!acquire_input(&object,&index,&remaining));memcpy(s->input.mutableBytes,p+pos+header,length-header);assert(!send_input(&object,index,0,(int)(length-header),(int64_t)packets*1024*1000000/48000,0));while(!acquire_output(&object,&index,&time,&flags,&remaining)){__weak NSData *consumed=s->held[@(index)].bytes;pcm_bytes+=consumed.length;assert(!release_output(&object,index));assert(!consumed && "AAC packet remains autoreleased after consumption");}packets++;pos+=length;}
 assert(packets>=3 && pcm_bytes>=5*1024*4);destroy(&object);
 config=[NSData dataWithContentsOfFile:@(argv[3])];test_mime="video/avc";construct(&object,1,4,1,16);assert(!configure(&object,(void *)1));assert(!start(&object));s=state(&object);
 NSData *encoded=[NSData dataWithContentsOfFile:@(argv[4])];const uint8_t *encoded_bytes=encoded.bytes;NSUInteger cursor=0;int frame_count=0,packet_count=0;int64_t previous=-1;
 while(cursor<encoded.length){int64_t pts;uint32_t size;assert(encoded.length-cursor>=12);memcpy(&pts,encoded_bytes+cursor,8);memcpy(&size,encoded_bytes+cursor+8,4);cursor+=12;assert(size<=encoded.length-cursor);if(packet_count++==5)VTDecompressionSessionInvalidate(s->video);
 assert(!acquire_input(&object,&index,&remaining));memcpy(s->input.mutableBytes,encoded_bytes+cursor,size);assert(!send_input(&object,index,0,(int)size,pts,0));cursor+=size;
 while(!acquire_output(&object,&index,&time,&flags,&remaining)){assert(time>previous);previous=time;frame_count++;assert(!release_output(&object,index));}}
 assert(!acquire_input(&object,&index,&remaining));assert(!send_input(&object,index,0,0,400000,4));while(!acquire_output(&object,&index,&time,&flags,&remaining)){if(!(flags&4)){assert(time>previous);previous=time;frame_count++;}assert(!release_output(&object,index));}assert(frame_count==12);destroy(&object);puts("PASS: all 12 B frames emitted once in presentation order after decoder invalidation");printf("PASS: Apple AVC red NV12 frame, timestamps/EOS and %lu AAC packets decoded to %lu PCM bytes\n",(unsigned long)packets,(unsigned long)pcm_bytes);
 }}
