// Native replacement for the title's movie::Decoder boundary. The original
// extractor still supplies compressed access units; Apple decodes those bytes.
#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>
#import <AudioToolbox/AudioToolbox.h>
#include "hle.h"
#include <string.h>
#include <stdio.h>

static void *movie_symbol(const char *name){void *p=guest_sym(name);if(!p)die("movie export missing: %s",name);return p;}
#define MOV(type,name) ((type)movie_symbol(name))
@interface HyMovieFrame : NSObject
@property(nonatomic,strong) NSData *bytes;
@property(nonatomic) int64_t time;
@property(nonatomic) unsigned flags;
@property(nonatomic) int index;
@end
@implementation HyMovieFrame
@end
@interface HyMoviePacket : NSObject
@property(nonatomic,strong) NSData *bytes;
@property(nonatomic) int64_t time;
@end
@implementation HyMoviePacket
@end
@interface HyMovieDecoder : NSObject {
@public
    VTDecompressionSessionRef video;CMVideoFormatDescriptionRef description;AudioConverterRef audio;
    void **events;int type,format,mode,width,height,rate,channels,next_index;
    BOOL running,input_acquired,eos;unsigned error;OSStatus callback_status;int64_t packet_time;
    NSMutableArray<HyMoviePacket *> *recovery;NSMutableSet<NSNumber *> *decoded_times;NSUInteger recovery_bytes;
    NSMutableData *input;NSMutableArray<HyMovieFrame *> *ready,*pending;NSMutableDictionary<NSNumber *,HyMovieFrame *> *held;
}
@end
@implementation HyMovieDecoder
- (instancetype)init {self=[super init];if(self){recovery=[NSMutableArray new];decoded_times=[NSMutableSet new];input=[NSMutableData dataWithLength:1024*1024];ready=[NSMutableArray new];pending=[NSMutableArray new];held=[NSMutableDictionary new];}return self;}
- (void)dealloc {if(video){VTDecompressionSessionInvalidate(video);CFRelease(video);}if(description)CFRelease(description);if(audio)AudioConverterDispose(audio);}
@end
typedef struct {void **vtable;void *state;} MovieObject;
static HyMovieDecoder *state(MovieObject *object){if(!object || !object->state)die("invalid native movie decoder");return (__bridge HyMovieDecoder *)object->state;}
static void signal_event(HyMovieDecoder *s,unsigned index){if(s->events && s->events[index])MOV(void (*)(void *),"_ZN2nn2os11SignalEventEPNS0_9EventTypeE")(s->events[index]);}
static void notify(HyMovieDecoder *s){BOOL output;unsigned error;@synchronized(s){output=s->ready.count>0;error=s->error;}signal_event(s,0);if(output)signal_event(s,1);if(error)signal_event(s,3);}
static int integer(void *metadata,const char *key,int fallback){int value=fallback;MOV(int (*)(void *,const char *,int *),"_ZNK5movie9MediaData9FindInt32EPKcPi")(metadata,key,&value);return value;}
static NSData *config_bytes(void *metadata,const char *key){void *buffer=NULL;if(!MOV(int (*)(void *,const char *,void **),"_ZNK5movie9MediaData10FindBufferEPKcPPNS_6BufferE")(metadata,key,&buffer) || !buffer)return nil;const void *bytes=MOV(const void *(*)(void *),"_ZNK5movie6Buffer4DataEv")(buffer);int size=MOV(int (*)(void *),"_ZNK5movie6Buffer4SizeEv")(buffer);if(size<0 || size>1024*1024 || (size && !bytes))die("invalid movie codec config");return [NSData dataWithBytes:bytes length:(NSUInteger)size];}
static NSArray<NSData *> *annex_units(NSData *data){
    const uint8_t *p=data.bytes;NSUInteger n=data.length,start=NSNotFound;NSMutableArray *units=[NSMutableArray new];
    for(NSUInteger i=0;i+3<=n;) {
        NSUInteger prefix=(p[i]==0 && p[i+1]==0 && p[i+2]==1)?3:((i+4<=n && p[i]==0 && p[i+1]==0 && p[i+2]==0 && p[i+3]==1)?4:0);
        if(!prefix){i++;continue;}if(start!=NSNotFound && i>start)[units addObject:[data subdataWithRange:NSMakeRange(start,i-start)]];start=i+prefix;i=start;
    }
    if(start!=NSNotFound && start<n)[units addObject:[data subdataWithRange:NSMakeRange(start,n-start)]];return units;
}
static NSData *avcc_bytes(NSData *data) {
    const uint8_t *bytes=data.bytes;NSUInteger offset=0;BOOL valid=data.length>0;
    while(data.length-offset>=4){uint32_t n;memcpy(&n,bytes+offset,4);n=CFSwapInt32BigToHost(n);offset+=4;if(!n || n>data.length-offset || !(bytes[offset]&31) || (bytes[offset]&31)>23){valid=NO;break;}offset+=n;}
    if(valid && offset==data.length)return data;
    NSArray<NSData *> *units=annex_units(data);if(!units.count)die("invalid AVC access unit framing");NSMutableData *result=[NSMutableData new];
    for(NSData *unit in units){uint32_t length=CFSwapInt32HostToBig((uint32_t)unit.length);[result appendBytes:&length length:4];[result appendData:unit];}return result;
}
static void movie_error(HyMovieDecoder *s,unsigned error){@synchronized(s){s->error=error;}}
static void decoded_video(void *context,void *source,OSStatus status,VTDecodeInfoFlags flags,CVImageBufferRef image,CMTime time,CMTime duration){
    @autoreleasepool {
    (void)source;(void)flags;(void)duration;HyMovieDecoder *s=(__bridge HyMovieDecoder *)context;
    if(status){@synchronized(s){s->callback_status=status;}return;}
    if(!image){if(!(flags&kVTDecodeInfo_FrameDropped))movie_error(s,0x1000);return;}
    int64_t pts=CMTimeConvertScale(time,1000000,kCMTimeRoundingMethod_Default).value;
    @synchronized(s){if([s->decoded_times containsObject:@(pts)])return;}
    if(CVPixelBufferGetWidth(image)!=(size_t)s->width || CVPixelBufferGetHeight(image)!=(size_t)s->height) {fprintf(stderr,"Movie dimensions: %zu x %zu expected %d x %d\n",CVPixelBufferGetWidth(image),CVPixelBufferGetHeight(image),s->width,s->height);movie_error(s,0x1000);return;}
    if(CVPixelBufferLockBaseAddress(image,kCVPixelBufferLock_ReadOnly)){movie_error(s,0x1000);return;}
    NSMutableData *output;
    if(s->format==4) {
        if(CVPixelBufferGetPlaneCount(image)!=2){CVPixelBufferUnlockBaseAddress(image,kCVPixelBufferLock_ReadOnly);movie_error(s,0x1000);return;}
        NSUInteger w=(NSUInteger)s->width,h=(NSUInteger)s->height,cw=(w+1)/2,ch=(h+1)/2;
        output=[NSMutableData dataWithLength:w*h+2*cw*ch];uint8_t *dest=output.mutableBytes;
        if(!dest){CVPixelBufferUnlockBaseAddress(image,kCVPixelBufferLock_ReadOnly);movie_error(s,0x1000);return;}
        const uint8_t *y=CVPixelBufferGetBaseAddressOfPlane(image,0),*uv=CVPixelBufferGetBaseAddressOfPlane(image,1);
        for(NSUInteger row=0;row<h;row++)memcpy(dest+row*w,y+row*CVPixelBufferGetBytesPerRowOfPlane(image,0),w);
        for(NSUInteger row=0;row<ch;row++)memcpy(dest+w*h+row*cw*2,uv+row*CVPixelBufferGetBytesPerRowOfPlane(image,1),cw*2);
    } else {movie_error(s,0x1000);CVPixelBufferUnlockBaseAddress(image,kCVPixelBufferLock_ReadOnly);return;}
    CVPixelBufferUnlockBaseAddress(image,kCVPixelBufferLock_ReadOnly);
    HyMovieFrame *frame=[HyMovieFrame new];frame.bytes=output;frame.time=pts;
    @synchronized(s){
        [s->decoded_times addObject:@(pts)];frame.index=s->next_index++;[s->pending addObject:frame];
        [s->pending sortUsingComparator:^NSComparisonResult(HyMovieFrame *a,HyMovieFrame *b){return a.time<b.time ? NSOrderedAscending : a.time>b.time ? NSOrderedDescending : NSOrderedSame;}];
        // AVC permits at most 16 decoded pictures in its reorder buffer.
        // Keep that conservative window; callbacks otherwise arrive in decode order.
        if(s->pending.count>16){[s->ready addObject:s->pending.firstObject];[s->pending removeObjectAtIndex:0];}
    }
    }
}
static NSData *aac_cookie(NSData *asc) {
    if(!asc.length || asc.length>64)die("unsupported AAC AudioSpecificConfig size");
    const uint8_t config[]={0x40,0x15,0,0,0,0,0,0,0,0,0,0,0};NSMutableData *decoder=[NSMutableData dataWithBytes:config length:sizeof config];
    uint8_t specific[]={5,(uint8_t)asc.length};[decoder appendBytes:specific length:2];[decoder appendData:asc];
    uint8_t es[]={0,1,0,4,(uint8_t)decoder.length};NSMutableData *body=[NSMutableData dataWithBytes:es length:sizeof es];[body appendData:decoder];
    const uint8_t sl[]={6,1,2};[body appendBytes:sl length:sizeof sl];uint8_t header[]={3,(uint8_t)body.length};
    NSMutableData *result=[NSMutableData dataWithBytes:header length:2];[result appendData:body];return result;
}
static pthread_mutex_t active_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t active_changed=PTHREAD_COND_INITIALIZER;
static int media_active=1;
void hy_movie_set_active(int active){pthread_mutex_lock(&active_lock);media_active=!!active;pthread_cond_broadcast(&active_changed);pthread_mutex_unlock(&active_lock);}
static void media_wait_active(void){pthread_mutex_lock(&active_lock);while(!media_active)pthread_cond_wait(&active_changed,&active_lock);pthread_mutex_unlock(&active_lock);}
static void native_decode(void (^work)(void)) {
    media_wait_active();
    static dispatch_queue_t worker;static dispatch_once_t once;dispatch_once(&once,^{worker=dispatch_queue_create("com.guiyu.hayarigami.media",DISPATCH_QUEUE_SERIAL);});
    // async + semaphore guarantees an Apple-owned worker stack; dispatch_sync
    // is allowed to execute inline on the much smaller guest movie stack.
    dispatch_semaphore_t completed=dispatch_semaphore_create(0);
    dispatch_async(worker,^{@autoreleasepool{media_wait_active();work();}dispatch_semaphore_signal(completed);});
    dispatch_semaphore_wait(completed,DISPATCH_TIME_FOREVER);
}
static OSStatus create_video_session(HyMovieDecoder *s){
    NSDictionary *attributes=@{(id)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),(id)kCVPixelBufferIOSurfacePropertiesKey:@{}};
    VTDecompressionOutputCallbackRecord callback={decoded_video,(__bridge void *)s};
    return VTDecompressionSessionCreate(kCFAllocatorDefault,s->description,NULL,(__bridge CFDictionaryRef)attributes,&callback,&s->video);
}
static OSStatus submit_video(HyMovieDecoder *s,NSData *avcc,int64_t time){
    media_wait_active();
    CMBlockBufferRef block=NULL;CMSampleBufferRef sample=NULL;OSStatus result=CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault,NULL,avcc.length,kCFAllocatorDefault,NULL,0,avcc.length,0,&block);
    if(!result)result=CMBlockBufferReplaceDataBytes(avcc.bytes,block,0,avcc.length);
    CMSampleTimingInfo timing={kCMTimeInvalid,CMTimeMake(time,1000000),kCMTimeInvalid};size_t size=avcc.length;
    if(!result)result=CMSampleBufferCreateReady(kCFAllocatorDefault,block,s->description,1,1,&timing,1,&size,&sample);
    if(!result)for(;;){
        media_wait_active();@synchronized(s){s->callback_status=0;}
        result=VTDecompressionSessionDecodeFrame(s->video,sample,0,NULL,NULL);
        if(!result)result=VTDecompressionSessionWaitForAsynchronousFrames(s->video);
        @synchronized(s){if(!result)result=s->callback_status;}
        if(result!=kVTVideoDecoderNotAvailableNowErr)break;
        // Suspension can race the foreground check. Keep this access unit until
        // the decoder is available again; a resumed callback is deduplicated by PTS.
        media_wait_active();usleep(10000);
    }
    if(sample)CFRelease(sample);if(block)CFRelease(block);return result;
}
static BOOL contains_idr(NSData *avcc){
    const uint8_t *bytes=avcc.bytes;NSUInteger offset=0;
    while(offset+4<=avcc.length){uint32_t size;memcpy(&size,bytes+offset,4);size=CFSwapInt32BigToHost(size);offset+=4;
        if(!size || size>avcc.length-offset)die("invalid AVC recovery framing");if((bytes[offset]&31)==5)return YES;offset+=size;}
    return NO;
}
static unsigned configure(MovieObject *object,void *metadata){
    HyMovieDecoder *s=state(object);const char *mime=NULL;MOV(int (*)(void *,const char *,const char **),"_ZNK5movie9MediaData10FindStringEPKcPS2_")(metadata,"mime",&mime);
    if(s->type==1 && s->format==4 && s->mode==1 && mime && !strcmp(mime,"video/avc")) {
        s->width=integer(metadata,"width",0);s->height=integer(metadata,"height",0);
        if(s->width<=0 || s->height<=0 || s->width>4096 || s->height>4096 || (s->width&1) || (s->height&1))die("invalid AVC dimensions");
        NSMutableArray<NSData *> *units=[NSMutableArray new];for(const char **key=(const char *[]){"csd-0","csd-1",NULL};*key;key++){NSData *data=config_bytes(metadata,*key);if(data)[units addObjectsFromArray:annex_units(data)];}
        NSData *sps=nil,*pps=nil;for(NSData *unit in units){unsigned kind=*(const uint8_t *)unit.bytes&31;if(kind==7)sps=unit;if(kind==8)pps=unit;}
        if(!sps || !pps)die("AVC config requires Annex B SPS and PPS");
        const uint8_t *parameters[]={sps.bytes,pps.bytes};size_t sizes[]={sps.length,pps.length};
        OSStatus result=CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault,2,parameters,sizes,4,&s->description);
        if(!result)result=create_video_session(s);
        if(result)die("Apple AVC decoder creation failed: %d",(int)result);
    } else if(s->type==4 && s->format==1 && s->mode==1 && mime && !strcmp(mime,"audio/mp4a-latm")) {
        s->rate=integer(metadata,"sample-rate",0);s->channels=integer(metadata,"channel-count",0);
        if((s->rate!=48000 && s->rate!=44100) || (s->channels!=1 && s->channels!=2))die("unsupported AAC output parameters");
        AudioStreamBasicDescription source={0},destination={0};source.mSampleRate=s->rate;source.mFormatID=kAudioFormatMPEG4AAC;source.mFramesPerPacket=1024;source.mChannelsPerFrame=(UInt32)s->channels;
        destination=(AudioStreamBasicDescription){s->rate,kAudioFormatLinearPCM,kAudioFormatFlagIsSignedInteger|kAudioFormatFlagIsPacked,(UInt32)s->channels*2,1,(UInt32)s->channels*2,(UInt32)s->channels,16,0};
        OSStatus result=AudioConverterNew(&source,&destination,&s->audio);NSData *asc=config_bytes(metadata,"csd-0");NSData *cookie=asc ? aac_cookie(asc) : nil;
        if(!result && cookie.length)result=AudioConverterSetProperty(s->audio,kAudioConverterDecompressionMagicCookie,(UInt32)cookie.length,cookie.bytes);
        if(result)die("Apple AAC decoder creation failed: %d",(int)result);
    } else die("unsupported native movie decoder: type=%d format=%d mode=%d mime=%s",s->type,s->format,s->mode,mime?mime:"missing");
    fprintf(stderr,"Movie: Apple decoder configured: type=%d video=%dx%d audio=%d/%d\n",s->type,s->width,s->height,s->rate,s->channels);return 0;
}
static unsigned register_events(MovieObject *object,void **events){HyMovieDecoder *s=state(object);s->events=events;return 0;}
static void set_integer(void *metadata,const char *key,int value){MOV(void (*)(void *,const char *,int),"_ZN5movie9MediaData8SetInt32EPKci")(metadata,key,value);}
static unsigned output_format(MovieObject *object,void *metadata){HyMovieDecoder *s=state(object);if(!metadata)return 0x100b;
    MOV(void (*)(void *),"_ZN5movie9MediaData5ClearEv")(metadata);
    if(s->audio){set_integer(metadata,"sample-rate",s->rate);set_integer(metadata,"channel-count",s->channels);return 0;}
    const char *keys[]={"width","height","stride","slice-height","display-width","display-height","color-format"};
    int values[]={s->width,s->height,s->width,s->height,s->width,s->height,19};for(unsigned i=0;i<7;i++)set_integer(metadata,keys[i],values[i]);
    MOV(void (*)(void *,const char *,int,int,int,int),"_ZN5movie9MediaData7SetRectEPKciiii")(metadata,"crop",0,0,s->width-1,s->height-1);return 0;}
static unsigned start(MovieObject *object){HyMovieDecoder *s=state(object);@synchronized(s){s->running=YES;}notify(s);signal_event(s,2);return 0;}
static unsigned stop(MovieObject *object){HyMovieDecoder *s=state(object);@synchronized(s){s->running=NO;}return 0;}
static unsigned reset(MovieObject *object){HyMovieDecoder *s=state(object);if(s->video){VTDecompressionSessionFinishDelayedFrames(s->video);VTDecompressionSessionWaitForAsynchronousFrames(s->video);}@synchronized(s){[s->ready removeAllObjects];[s->pending removeAllObjects];[s->held removeAllObjects];[s->recovery removeAllObjects];[s->decoded_times removeAllObjects];s->recovery_bytes=0;s->input_acquired=NO;s->eos=NO;s->error=0;}if(s->audio && AudioConverterReset(s->audio))die("AAC reset failed");if(s->running)notify(s);return 0;}
static unsigned acquire_input(MovieObject *object,int *index,int *remaining){HyMovieDecoder *s=state(object);*index=-1;*remaining=0;@synchronized(s){if(!s->running || s->input_acquired || s->eos || s->ready.count+s->held.count>=8)return 0x1011;s->input_acquired=YES;*index=0;}return 0;}
static void set_buffer(void *buffer,void *data,size_t size){MOV(void (*)(void *,void *,size_t),"_ZN5movie6Buffer18SetDataAndCapacityEPvm")(buffer,data,size);MOV(void (*)(void *,int,int),"_ZN5movie6Buffer8SetRangeEii")(buffer,0,(int)size);}
static unsigned input_buffer(MovieObject *object,int index,void *buffer){HyMovieDecoder *s=state(object);if(index || !s->input_acquired)return 0x100b;set_buffer(buffer,s->input.mutableBytes,s->input.length);return 0;}
enum { AAC_NEED_INPUT=0x68796e69 };
typedef struct {const void *bytes;UInt32 size;BOOL supplied;AudioStreamPacketDescription description;} AacPacket;
static OSStatus aac_input(AudioConverterRef converter,UInt32 *count,AudioBufferList *buffers,AudioStreamPacketDescription **description,void *context){
    (void)converter;AacPacket *packet=context;
    if(packet->supplied){*count=0;buffers->mBuffers[0].mData=NULL;buffers->mBuffers[0].mDataByteSize=0;return AAC_NEED_INPUT;}
    packet->supplied=YES;*count=1;buffers->mNumberBuffers=1;buffers->mBuffers[0].mData=(void *)packet->bytes;buffers->mBuffers[0].mDataByteSize=packet->size;
    packet->description=(AudioStreamPacketDescription){0,1024,packet->size};if(description)*description=&packet->description;return noErr;
}
static unsigned send_input(MovieObject *object,int index,int offset,int size,int64_t time,unsigned flags){@autoreleasepool{HyMovieDecoder *s=state(object);if(index || !s->input_acquired || offset<0 || size<0 || (size_t)offset>s->input.length || (size_t)size>s->input.length-(size_t)offset)return 0x100b;
    if(flags&~7u)die("unsupported movie buffer flags %#x",flags);
    if(s->audio && size && !(flags&2)) {
        NSMutableData *data=[NSMutableData dataWithLength:(NSUInteger)s->channels*2*2048];UInt32 frames=2048;
        AudioBufferList buffers={.mNumberBuffers=1,.mBuffers={{(UInt32)s->channels,(UInt32)data.length,data.mutableBytes}}};
        AacPacket packet={(uint8_t *)s->input.bytes+offset,(UInt32)size,NO,{0}};
        __block OSStatus result;
        native_decode(^{AacPacket local_packet=packet;UInt32 local_frames=frames;AudioBufferList local_buffers=buffers;result=AudioConverterFillComplexBuffer(s->audio,aac_input,&local_packet,&local_frames,&local_buffers,NULL);data.length=local_buffers.mBuffers[0].mDataByteSize;});
        if(result && result!=AAC_NEED_INPUT)die("Apple AAC access unit decode failed: %d",(int)result);

        if(data.length){HyMovieFrame *frame=[HyMovieFrame new];frame.bytes=data;frame.time=time;@synchronized(s){frame.index=s->next_index++;[s->ready addObject:frame];}}
    } else if(size && !(flags&2)) {
        NSData *data=[NSData dataWithBytes:(uint8_t *)s->input.bytes+offset length:(NSUInteger)size];NSData *avcc=avcc_bytes(data);
        if(contains_idr(avcc)){[s->recovery removeAllObjects];s->recovery_bytes=0;@synchronized(s){[s->decoded_times removeAllObjects];}}
        if(s->recovery.count>=1024 || avcc.length>64*1024*1024-s->recovery_bytes)die("AVC recovery window exceeds supported bound");
        HyMoviePacket *packet=[HyMoviePacket new];packet.bytes=avcc;packet.time=time;[s->recovery addObject:packet];s->recovery_bytes+=avcc.length;
        __block OSStatus result;
        native_decode(^{
            result=submit_video(s,avcc,time);
            if(result==kVTInvalidSessionErr){
                VTDecompressionSessionInvalidate(s->video);CFRelease(s->video);s->video=NULL;media_wait_active();result=create_video_session(s);
                if(!result)for(HyMoviePacket *saved in s->recovery){result=submit_video(s,saved.bytes,saved.time);if(result)break;}
                if(!result)fputs("Movie: invalidated AVC session recovered from retained keyframe\n",stderr);
            }
        });
        if(result)die("Apple AVC access unit decode failed: %d",(int)result);
    }
    if(flags&4){if(s->video){VTDecompressionSessionFinishDelayedFrames(s->video);VTDecompressionSessionWaitForAsynchronousFrames(s->video);}HyMovieFrame *frame=[HyMovieFrame new];frame.bytes=[NSData data];frame.time=time;frame.flags=4;@synchronized(s){[s->ready addObjectsFromArray:s->pending];[s->pending removeAllObjects];frame.index=s->next_index++;[s->ready addObject:frame];s->eos=YES;}}
    s->input_acquired=NO;notify(s);@synchronized(s){return s->error;}
}}
static unsigned acquire_output(MovieObject *object,int *index,int64_t *time,unsigned *flags,int *remaining){HyMovieDecoder *s=state(object);*index=-1;*time=0;*flags=0;*remaining=0;@synchronized(s){if(!s->ready.count)return 0x100e;HyMovieFrame *frame=s->ready.firstObject;[s->ready removeObjectAtIndex:0];s->held[@(frame.index)]=frame;*index=frame.index;*time=frame.time;*flags=frame.flags;*remaining=(int)s->ready.count;}return 0;}
static unsigned output_buffer(MovieObject *object,int index,void *buffer){@autoreleasepool{HyMovieDecoder *s=state(object);@synchronized(s){HyMovieFrame *frame=s->held[@(index)];if(!frame)return 0x100b;size_t capacity=MOV(size_t (*)(void *),"_ZNK5movie6Buffer8CapacityEv")(buffer);
        void *destination=MOV(void *(*)(void *),"_ZNK5movie6Buffer4DataEv")(buffer);
        if(frame.bytes.length>capacity || (frame.bytes.length && !destination))die("movie client output buffer too small");
        if(frame.bytes.length)memcpy(destination,frame.bytes.bytes,frame.bytes.length);
        MOV(void (*)(void *,int,int),"_ZN5movie6Buffer8SetRangeEii")(buffer,0,(int)frame.bytes.length);}return 0;}}
static unsigned output_size(MovieObject *object,int *size,int *alignment){HyMovieDecoder *s=state(object);*size=s->audio ? s->channels*2*2048 : s->width*s->height*3/2;*alignment=1;return 0;}
static unsigned release_output(MovieObject *object,int index){HyMovieDecoder *s=state(object);@synchronized(s){if(!s->held[@(index)])return 0x100b;[s->held removeObjectForKey:@(index)];}notify(s);return 0;}
static unsigned last_error(MovieObject *object){HyMovieDecoder *s=state(object);@synchronized(s){return s->error;}}
static unsigned output_buffer_format(MovieObject *object,int index,void *metadata){(void)index;return output_format(object,metadata);}
static unsigned output_buffer_meta(MovieObject *object,int index,void *buffer,void *metadata){unsigned result=output_buffer(object,index,buffer);return result?result:output_format(object,metadata);}
static void destroy(MovieObject *object){HyMovieDecoder *s=(__bridge_transfer HyMovieDecoder *)object->state;object->state=NULL;(void)s;}
static void delete_object(MovieObject *object){destroy(object);MOV(void (*)(void *),"_ZdlPv")(object);}
static void *vtable[]={destroy,delete_object,configure,register_events,output_format,start,stop,reset,reset,acquire_input,input_buffer,send_input,acquire_output,output_buffer,output_size,release_output,last_error,output_buffer_meta,output_buffer_format};
static void construct(MovieObject *object,int type,int format,int mode,int priority){(void)priority;HyMovieDecoder *s=[HyMovieDecoder new];s->type=type;s->format=format;s->mode=mode;object->vtable=vtable;object->state=(__bridge_retained void *)s;fprintf(stderr,"Movie native decoder type=%d format=%d mode=%d\n",type,format,mode);}
extern void (*g_ss_media_suspend_hook)(int);
static void movie_suspend(int suspended) { hy_movie_set_active(!suspended); }
__attribute__((constructor)) static void install_movie_lifecycle(void) { g_ss_media_suspend_hook = movie_suspend; }
HLE_FORCE("_ZN5movie7DecoderC1ENS_11DecoderTypeENS_19DecoderOutputFormatENS_11DecoderModeEi", "Decode AVC/AAC through Apple media", void, hy_movie_construct_c1, MovieObject *object, int type, int format, int mode, int priority) {
    construct(object, type, format, mode, priority);
}
HLE_FORCE("_ZN5movie7DecoderC2ENS_11DecoderTypeENS_19DecoderOutputFormatENS_11DecoderModeEi", "Decode AVC/AAC through Apple media", void, hy_movie_construct_c2, MovieObject *object, int type, int format, int mode, int priority) {
    construct(object, type, format, mode, priority);
}
