#include <switch.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

enum { Channels = 9, Capacity = 32768, MixerFrames = 441, DeviceFrames = 480, Slots = 3 };
typedef struct {
    int16_t samples[Capacity][2];
    unsigned head, count;
    bool dry, played;
    uint64_t last_short_tick;
} Channel;
static Channel channels[Channels];
static Mutex lock;
static Thread worker;
static _Atomic bool running, paused;
static bool attempted, opened;
static int16_t blocks[Slots][2048] __attribute__((aligned(4096)));
static AudioOutBuffer buffers[Slots];
static uint64_t dropped, short_gaps;
static _Atomic unsigned refill_ms = 60;
unsigned nx_audio_refill_frames(void) {
    return atomic_load_explicit(&refill_ms, memory_order_relaxed) * 44100u / 1000u;
}
void nx_audio_stats(uint64_t *gaps, uint64_t *lost) {
    mutexLock(&lock);
    *gaps = short_gaps;
    *lost = dropped;
    mutexUnlock(&lock);
}

static int16_t clamp16(int value) {
    return value < -32768 ? -32768 : value > 32767 ? 32767 : value;
}
static void mix_block(int16_t *out) {
    int mix[MixerFrames+1][2] = {{0}};
    mutexLock(&lock);
    if (!paused) {
        for (unsigned ch=0; ch<Channels; ch++) {
            Channel *q = &channels[ch];
            unsigned used = q->count < MixerFrames ? q->count : MixerFrames;
            for (unsigned i=0; i<used; i++) {
                unsigned index=(q->head+i)%Capacity;
                mix[i][0]+=q->samples[index][0]; mix[i][1]+=q->samples[index][1];
            }
            if (q->count > MixerFrames) {
                unsigned next=(q->head+MixerFrames)%Capacity;
                mix[MixerFrames][0]+=q->samples[next][0];
                mix[MixerFrames][1]+=q->samples[next][1];
            } else if (used == MixerFrames) {
                unsigned last=(q->head+used-1)%Capacity;
                mix[MixerFrames][0]+=q->samples[last][0];
                mix[MixerFrames][1]+=q->samples[last][1];
            }
            q->head=(q->head+used)%Capacity; q->count-=used;
            if (used < MixerFrames && !q->dry) {
                q->dry = true;
                if (q->played) q->last_short_tick = armGetSystemTick();
            }
        }
    }
    mutexUnlock(&lock);
    for (unsigned i=0; i<DeviceFrames; i++) {
        unsigned pos=i*MixerFrames, a=pos/DeviceFrames;
        int f=(int)(pos%DeviceFrames);
        for (unsigned side=0; side<2; side++) {
            int v=(mix[a][side]*(DeviceFrames-f) + mix[a+1][side]*f)/DeviceFrames;
            *out++=clamp16(v);
        }
    }
}
static void playback(void *unused) {
    (void)unused;
    for (unsigned i=0; i<Slots; i++) {
        mix_block(blocks[i]);
        buffers[i].buffer=blocks[i];
        buffers[i].buffer_size=sizeof(blocks[i]);
        buffers[i].data_size=DeviceFrames*4;
        if (R_FAILED(audoutAppendAudioOutBuffer(&buffers[i]))) { running=false; return; }
    }
    while (running) {
        AudioOutBuffer *buffer=NULL;
        u32 count=0;
        Result rc=audoutWaitPlayFinish(&buffer,&count,50000000);
        if (R_FAILED(rc)) {
            if (rc == KERNELRESULT(TimedOut)) continue;
            running=false;
            break;
        }
        if (!buffer || !count) continue;
        mix_block(buffer->buffer);
        if (R_FAILED(audoutAppendAudioOutBuffer(buffer))) running=false;
    }
}
static void shutdown_audio(void) {
    if (!opened) return;
    running=false;
    threadWaitForExit(&worker);
    threadClose(&worker);
    audoutStopAudioOut();
    audoutExit();
    opened=false;
}
int sr_audio_init(void) {
    if (attempted) return running ? 0 : -1;
    attempted=true;
    if (R_FAILED(audoutInitialize())) return -1;
    if (R_FAILED(audoutStartAudioOut())) { audoutExit(); return -1; }
    if (R_SUCCEEDED(threadCreate(&worker,playback,NULL,NULL,65536,0x2b,-2))) {
        running=true;
        if (R_SUCCEEDED(threadStart(&worker))) {
            opened=true;
            atexit(shutdown_audio);
            return 0;
        }
        running=false;
        threadClose(&worker);
    }
    audoutStopAudioOut(); audoutExit();
    return -1;
}
static void push(int ch,const int16_t *lr,int frames,int vl,int vr) {
    if (ch < 0 || ch >= Channels || frames <= 0 || sr_audio_init() != 0) return;
    mutexLock(&lock);
    Channel *q=&channels[ch];
    if ((unsigned)frames <= Capacity-q->count) {
        for (int i=0; i<frames; i++) {
            unsigned slot=(q->head+q->count+i)%Capacity;
            q->samples[slot][0]=lr ? clamp16(((int64_t)lr[i*2]*vl)>>15) : 0;
            q->samples[slot][1]=lr ? clamp16(((int64_t)lr[i*2+1]*vr)>>15) : 0;
        }
        if (q->last_short_tick && lr) {
            if (armTicksToNs(armGetSystemTick() - q->last_short_tick) < 200000000ull) {
                ++short_gaps;
                unsigned ms = atomic_load_explicit(&refill_ms, memory_order_relaxed);
                if (ms < 100) atomic_store_explicit(&refill_ms, ms + 10, memory_order_relaxed);
            }
            q->last_short_tick = 0;
        }
        if (lr) q->played = true;
        q->count+=frames; q->dry=false;
    } else dropped+=frames;
    mutexUnlock(&lock);
}
void sr_audio_push(int ch,const int16_t *lr,int n,int vl,int vr) { if (lr) push(ch,lr,n,vl,vr); }
void sr_audio_push_wide(int ch,const int16_t *lr,const int32_t *wide,int n,int vl,int vr) {
    (void)wide; sr_audio_push(ch,lr,n,vl,vr);
}
void sr_audio_push_silence(int ch,int n) { push(ch,NULL,n,0,0); }
int sr_audio_queued(int ch) {
    if (ch<0 || ch>=Channels || sr_audio_init()!=0) return -1;
    mutexLock(&lock); int n=channels[ch].count; mutexUnlock(&lock); return n;
}
int sr_audio_played_dry(int ch) {
    if (ch<0 || ch>=Channels || sr_audio_init()!=0) return 0;
    mutexLock(&lock);
    Channel *q = &channels[ch];
    /* Resume real PCM immediately after a brief scheduling stall. Priming a
     * recently active channel with silence extends each audible dropout. */
    int dry = q->dry && !q->count && (!q->last_short_tick ||
        armTicksToNs(armGetSystemTick() - q->last_short_tick) >= 200000000ull);
    mutexUnlock(&lock);
    return dry;
}
int sr_audio_host_pull_frames(void) { return running ? MixerFrames : 0; }
int sr_audio_is_active(void) { return running; }
void sr_audio_pause_enter(void) { paused=true; }
void sr_audio_pause_leave(void) { paused=false; }
int sr_audio_pause_script_ready(void) { return 0; }
int sr_audio_set_true_peak_limiter(int on) { return !on; }
void sr_audio_true_peak_stats(uint64_t *a,uint64_t *b,uint64_t *c,double *g) {
    if (a) *a=0;
    if (b) *b=0;
    if (c) *c=0;
    if (g) *g=1.0;
}
void sr_audio_dump_finish(void) {}
void sr_audio_dump_stats(void) {
    if (dropped) fprintf(stderr,"Switch audio dropped %llu mixer frames\n",(unsigned long long)dropped);
}
