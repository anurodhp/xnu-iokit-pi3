/*
 * audiod: the project's coreaudiod stand-in (PLAN.md). Apple's coreaudiod is
 * closed source, so this is project code. It owns the BCM2837PWMAudioEngine
 * through IOAudioEngineUserClient exactly as tools/userland_staging/audio_play.c
 * does (that file documents the engine protocol and cites IOAudioFamily line
 * by line; the constants and the status-buffer / lead logic below are copied
 * from it), takes PCM from any number of clients over Mach messages, resamples
 * every stream to 48 kHz, mixes in float, applies the device volume, and feeds
 * the engine.
 *
 * launchd starts it on demand through the MachServices entry in
 * org.puredarwin.coreaudiod.plist and the daemon claims the port with ONE
 * bootstrap_check_in (a second returns BOOTSTRAP_SERVICE_ACTIVE; DAR-270).
 * It exits (status 0) after IDLE_EXIT_S seconds with no streams.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <IOKit/IOKitLib.h>
#include "mixer.h"
#include "ca_protocol.h"

extern mach_port_t bootstrap_port;
extern kern_return_t bootstrap_check_in(mach_port_t bp, const char *name, mach_port_t *sp);

#define kEngineClass          "BCM2837PWMAudioEngine"
#define kStatusBuffer         0
#define kCallRegister         0
#define kCallUnregister       1
#define kCallStart            3
#define kCallStop             4
#define kTrapPerformClientIO  0
#define kRingFrames           4096       /* BCM2837PWMAudio.h kPWMAudioFrames */
#define kChunkFrames          512
#define kLeadFrames           2560       /* audio_play.c: aim this far ahead of the play position */
#define IDLE_EXIT_S           10
#define STREAM_STALE_S        3          /* a RUNNING stream with no WRITE for this long is reaped */

typedef struct {
    int                active;
    int                running;
    uint32_t           id;
    ca_stream_ring     ring;
    uint64_t           last_write_ns;
} stream_t;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static stream_t        g_streams[CA_MAX_STREAMS];
static uint32_t        g_next_id = 1;
static float           g_volume = 1.0f;
static int             g_mute;
static volatile sig_atomic_t g_stop;
static int             g_exiting;            /* mixer decided to exit; guarded by g_mu */
static uint64_t        g_idle_since_ns;     /* 0 while a stream exists; guarded by g_mu */

/* IOAudioTypes.h:136-142 and :100-108, as in audio_play.c */
typedef struct { uint32_t fActualDataByteSize, fActualNumSampleFrames, fTotalDataByteSize, fNominalDataByteSize; } AudioBufferDataDescriptor;
typedef struct { uint32_t fVersion; volatile uint32_t fCurrentLoopCount; volatile uint64_t fLastLoopTime; volatile uint32_t fEraseHeadSampleFrame; } AudioEngineStatus;

static mach_timebase_info_data_t g_tb;
static uint64_t now_ns(void) { return mach_absolute_time() * g_tb.numer / g_tb.denom; }
static void on_signal(int s) { (void)s; g_stop = 1; }

static int count_streams_locked(void)
{
    int n = 0;
    for (int i = 0; i < CA_MAX_STREAMS; i++) n += g_streams[i].active;
    return n;
}

static stream_t *find_locked(uint32_t id)
{
    for (int i = 0; i < CA_MAX_STREAMS; i++)
        if (g_streams[i].active && g_streams[i].id == id) return &g_streams[i];
    return NULL;
}

static void close_locked(stream_t *s)
{
    ca_ring_free(&s->ring);
    memset(s, 0, sizeof(*s));
    if (count_streams_locked() == 0) g_idle_since_ns = now_ns();
}

/* ---------------- engine side (the mixer thread owns the connection) ---------------- */

static void *mixer_thread(void *unused)
{
    (void)unused;
    io_connect_t conn = 0;
    io_service_t svc = 0;
    mach_vm_address_t saddr = 0;
    mach_vm_size_t ssize = 0;
    AudioBufferDataDescriptor *desc = NULL;
    volatile AudioEngineStatus *st = NULL;
    int engine_up = 0;
    uint64_t next = 0, chunks = 0, errors = 0;
    const uint64_t interval = (1000000000ULL / CA_DEVICE_RATE) * g_tb.denom / g_tb.numer;
    float mix[kChunkFrames * 2];

    #define PLAY_FRAME() ({ \
        uint32_t c1, c2; uint64_t t, nw; \
        do { \
            c1 = st->fCurrentLoopCount; __asm__ volatile ("dmb ishld" ::: "memory"); \
            t = st->fLastLoopTime;      __asm__ volatile ("dmb ishld" ::: "memory"); \
            c2 = st->fCurrentLoopCount; \
        } while (c1 != c2); \
        nw = mach_absolute_time(); \
        if (nw < t) nw = t; \
        (uint64_t)c1 * kRingFrames + ((nw - t) * g_tb.numer / g_tb.denom * CA_DEVICE_RATE / 1000000000ULL); })

    while (!g_stop) {
        pthread_mutex_lock(&g_mu);
        int n = count_streams_locked();
        uint64_t idle = g_idle_since_ns;
        pthread_mutex_unlock(&g_mu);

        if (n == 0) {
            if (engine_up) {            /* let the queued tail drain, then release the engine */
                usleep(100000);
                IOConnectCallScalarMethod(conn, kCallStop, NULL, 0, NULL, NULL);
                uint64_t un[2] = { (uint64_t)(uintptr_t)desc, 1 };
                IOConnectCallScalarMethod(conn, kCallUnregister, un, 2, NULL, NULL);
                IOConnectUnmapMemory64(conn, kStatusBuffer, mach_task_self(), saddr);
                IOServiceClose(conn);
                IOObjectRelease(svc);
                free(desc);
                engine_up = 0;
                printf("audiod: engine released (%llu chunks, %llu errors)\n",
                    (unsigned long long)chunks, (unsigned long long)errors);
                fflush(stdout);
            }
            if (idle && now_ns() - idle > (uint64_t)IDLE_EXIT_S * 1000000000ULL) {
                int go = 0;
                pthread_mutex_lock(&g_mu);
                if (count_streams_locked() == 0) { g_exiting = 1; go = 1; }   /* an OPEN that won the race keeps us alive */
                pthread_mutex_unlock(&g_mu);
                if (go) {
                    printf("audiod: idle, exiting\n");
                    fflush(stdout);
                    g_stop = 1;
                    break;
                }
            }
            usleep(20000);
            continue;
        }

        if (!engine_up) {
            int mapped = 0, registered = 0;
            desc = NULL;
            svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching(kEngineClass));
            if (!svc) { fprintf(stderr, "audiod: no %s (boot with audio on)\n", kEngineClass); fflush(stderr); sleep(1); continue; }
            kern_return_t kr = IOServiceOpen(svc, mach_task_self(), 0, &conn);
            if (kr) { fprintf(stderr, "audiod: IOServiceOpen 0x%x\n", kr); IOObjectRelease(svc); sleep(1); continue; }
            #define ENGINE_FAIL(...) do { fprintf(stderr, __VA_ARGS__); fflush(stderr); \
                if (registered) { uint64_t un_[2] = { (uint64_t)(uintptr_t)desc, 1 }; IOConnectCallScalarMethod(conn, kCallUnregister, un_, 2, NULL, NULL); } \
                if (mapped) IOConnectUnmapMemory64(conn, kStatusBuffer, mach_task_self(), saddr); \
                IOServiceClose(conn); IOObjectRelease(svc); free(desc); desc = NULL; sleep(1); goto engine_fail_next; } while (0)
            kr = IOConnectMapMemory64(conn, kStatusBuffer, mach_task_self(), &saddr, &ssize, kIOMapAnywhere);
            if (kr) ENGINE_FAIL("audiod: map status 0x%x\n", kr);
            mapped = 1;
            st = (volatile AudioEngineStatus *)(uintptr_t)saddr;
            const size_t dataBytes = kChunkFrames * 2 * sizeof(float);
            if (posix_memalign((void **)&desc, 4096, 8192) || !desc) { desc = NULL; ENGINE_FAIL("audiod: out of memory\n"); }
            memset(desc, 0, 8192);
            desc->fActualDataByteSize = (uint32_t)dataBytes;
            desc->fActualNumSampleFrames = kChunkFrames;
            desc->fTotalDataByteSize = (uint32_t)(sizeof(*desc) + dataBytes);   /* IOAudioEngineUserClient.cpp:1518-1531 */
            desc->fNominalDataByteSize = (uint32_t)dataBytes;
            uint64_t in[4] = { 0, (uint64_t)(uintptr_t)desc, sizeof(*desc) + dataBytes, 1 };
            kr = IOConnectCallScalarMethod(conn, kCallRegister, in, 4, NULL, NULL);
            if (kr) ENGINE_FAIL("audiod: register 0x%x\n", kr);
            registered = 1;
            kr = IOConnectCallScalarMethod(conn, kCallStart, NULL, 0, NULL, NULL);
            if (kr) ENGINE_FAIL("audiod: start 0x%x\n", kr);
            for (int i = 0; i < 500 && st->fCurrentLoopCount == 0 && st->fLastLoopTime == 0; i++) usleep(2000);
            if (st->fCurrentLoopCount == 0 && st->fLastLoopTime == 0) {
                IOConnectCallScalarMethod(conn, kCallStop, NULL, 0, NULL, NULL);
                ENGINE_FAIL("audiod: the engine produced no time stamp\n");
            }
            #undef ENGINE_FAIL
            next = PLAY_FRAME() + kLeadFrames;
            engine_up = 1;
            printf("audiod: engine up\n");
            fflush(stdout);
        }

        /* Fell behind: this chunk's frames are already playing; skip to a fresh lead. */
        uint64_t pf = PLAY_FRAME();
        if (next < pf + 512) next = pf + kLeadFrames;

        memset(mix, 0, sizeof mix);
        float vol; int mute;
        pthread_mutex_lock(&g_mu);
        uint64_t t = now_ns();
        for (int i = 0; i < CA_MAX_STREAMS; i++) {
            stream_t *s = &g_streams[i];
            if (!s->active) continue;
            if (t - s->last_write_ns > (uint64_t)STREAM_STALE_S * 1000000000ULL) {
                printf("audiod: reaping stale stream %u\n", s->id);
                fflush(stdout);
                close_locked(s);
                continue;
            }
            if (s->running) ca_ring_mix_into(&s->ring, mix, kChunkFrames, CA_DEVICE_RATE, 1.0f);
        }
        vol = g_volume; mute = g_mute;
        pthread_mutex_unlock(&g_mu);
        ca_clip(mix, kChunkFrames * 2, vol, mute);
        memcpy(desc + 1, mix, sizeof mix);

        uint32_t first = (uint32_t)(next % kRingFrames);
        uint32_t loop = (uint32_t)(next / kRingFrames);
        kern_return_t kr = IOConnectTrap6(conn, kTrapPerformClientIO, first, loop, 0, 1,
            (uintptr_t)(interval >> 32), (uintptr_t)(interval & 0xffffffffu));
        if (kr) {
            errors++;
            if (errors <= 4) { printf("audiod: trap rc 0x%x at play frame %llu\n", kr, (unsigned long long)pf); fflush(stdout); }
        }
        chunks++;
        next += kChunkFrames;
        while (!g_stop && PLAY_FRAME() + kLeadFrames < next) usleep(1500);
    engine_fail_next: ;
    }
    if (engine_up) {
        IOConnectCallScalarMethod(conn, kCallStop, NULL, 0, NULL, NULL);
        uint64_t un[2] = { (uint64_t)(uintptr_t)desc, 1 };
        IOConnectCallScalarMethod(conn, kCallUnregister, un, 2, NULL, NULL);
        IOServiceClose(conn);
    }
    return NULL;
}

/* ---------------- request handling ---------------- */

static void handle(const ca_request_t *rq, mach_msg_size_t rq_size, ca_reply_t *rp)
{
    memset(rp, 0, sizeof(*rp));
    rp->version = CA_PROTO_VERSION;
    rp->seq = rq->seq;
    rp->kr = KERN_SUCCESS;
    if (rq_size < offsetof(ca_request_t, data) || rq->version != CA_PROTO_VERSION) { rp->kr = KERN_INVALID_ARGUMENT; return; }

    pthread_mutex_lock(&g_mu);
    switch (rq->h.msgh_id) {
    case CA_OP_OPEN: {
        if (g_exiting) { rp->kr = KERN_ABORTED; break; }      /* client re-looks-up; launchd restarts us */
        if (rq->a < CA_MIN_RATE || rq->a > CA_MAX_RATE || (rq->b != 1 && rq->b != 2)) { rp->kr = KERN_INVALID_ARGUMENT; break; }
        stream_t *s = NULL;
        for (int i = 0; i < CA_MAX_STREAMS; i++) if (!g_streams[i].active) { s = &g_streams[i]; break; }
        if (!s) { rp->kr = KERN_RESOURCE_SHORTAGE; break; }
        memset(s, 0, sizeof(*s));
        if (ca_ring_init(&s->ring, CA_STREAM_RING_FRAMES, rq->b, rq->a)) { rp->kr = KERN_RESOURCE_SHORTAGE; break; }
        s->active = 1;
        s->id = g_next_id++;
        if (g_next_id == 0) g_next_id = 1;
        s->last_write_ns = now_ns();
        g_idle_since_ns = 0;
        rp->id = s->id;
        break;
    }
    case CA_OP_WRITE: {
        stream_t *s = find_locked(rq->id);
        if (!s) { rp->kr = KERN_INVALID_NAME; break; }
        /* The declared frame count must match the bytes that actually arrived. */
        size_t bytes = (size_t)rq->nframes * s->ring.ch * sizeof(float);
        if (rq->channels_check != s->ring.ch || bytes > CA_MAX_WRITE_BYTES ||
            rq_size != CA_REQ_SIZE(bytes)) { rp->kr = KERN_INVALID_ARGUMENT; break; }
        float tmp[CA_MAX_WRITE_BYTES / sizeof(float)];
        memcpy(tmp, rq->data, bytes);            /* data[] is not guaranteed float aligned */
        rp->a = ca_ring_write(&s->ring, tmp, rq->nframes);
        rp->b = ca_ring_room(&s->ring);
        s->last_write_ns = now_ns();
        break;
    }
    case CA_OP_START: {
        stream_t *s = find_locked(rq->id);
        if (!s) { rp->kr = KERN_INVALID_NAME; break; }
        s->running = 1;
        s->last_write_ns = now_ns();
        break;
    }
    case CA_OP_STOP: {
        stream_t *s = find_locked(rq->id);
        if (!s) { rp->kr = KERN_INVALID_NAME; break; }
        s->running = 0;
        break;
    }
    case CA_OP_CLOSE: {
        stream_t *s = find_locked(rq->id);
        if (!s) { rp->kr = KERN_INVALID_NAME; break; }
        close_locked(s);
        break;
    }
    case CA_OP_SET_VOLUME:
        if (!(rq->b & (CA_SET_VOLUME | CA_SET_MUTE)) || ((rq->b & CA_SET_VOLUME) && !(rq->f >= 0.0f && rq->f <= 1.0f))) { rp->kr = KERN_INVALID_ARGUMENT; break; }
        if (rq->b & CA_SET_VOLUME) g_volume = rq->f;
        if (rq->b & CA_SET_MUTE)   g_mute = rq->a ? 1 : 0;
        break;
    case CA_OP_GET_INFO:
        rp->rate = CA_DEVICE_RATE;
        rp->f = g_volume;
        rp->a = (uint32_t)g_mute;
        strncpy(rp->name, CA_DEVICE_NAME, sizeof(rp->name) - 1);
        break;
    default:
        rp->kr = KERN_INVALID_ARGUMENT;
    }
    pthread_mutex_unlock(&g_mu);
}

int main(void)
{
    mach_timebase_info(&g_tb);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    setvbuf(stdout, NULL, _IOLBF, 0);

    mach_port_t svc_port = MACH_PORT_NULL;
    kern_return_t kr = bootstrap_check_in(bootstrap_port, CA_SERVICE_NAME, &svc_port);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "audiod: bootstrap_check_in(%s) failed: 0x%x\n", CA_SERVICE_NAME, kr);
        return 1;
    }
    pthread_mutex_lock(&g_mu);
    g_idle_since_ns = now_ns();
    pthread_mutex_unlock(&g_mu);
    printf("audiod: up\n");

    pthread_t mt;
    if (pthread_create(&mt, NULL, mixer_thread, NULL)) { fprintf(stderr, "audiod: pthread_create failed\n"); return 1; }

    static ca_request_rcv_t rcv;
    static ca_reply_t reply;
    while (!g_stop) {
        memset(&rcv.m.h, 0, sizeof(rcv.m.h));
        kr = mach_msg(&rcv.m.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(rcv), svc_port, 100, MACH_PORT_NULL);
        if (kr == MACH_RCV_TIMED_OUT) continue;
        if (kr == MACH_RCV_TOO_LARGE) { continue; }
        if (kr != KERN_SUCCESS) { fprintf(stderr, "audiod: mach_msg receive 0x%x\n", kr); usleep(100000); continue; }

        if (rcv.m.h.msgh_bits & MACH_MSGH_BITS_COMPLEX) {   /* we never expect descriptors; do not leak what they carry */
            mach_msg_destroy(&rcv.m.h);
            continue;
        }
        handle(&rcv.m, rcv.m.h.msgh_size, &reply);
        mach_port_t rport = rcv.m.h.msgh_remote_port;
        if (rport == MACH_PORT_NULL || !MACH_MSGH_BITS_REMOTE(rcv.m.h.msgh_bits)) continue;
        reply.h.msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(rcv.m.h.msgh_bits), 0);
        reply.h.msgh_remote_port = rport;
        reply.h.msgh_local_port = MACH_PORT_NULL;
        reply.h.msgh_size = sizeof(reply);
        reply.h.msgh_id = rcv.m.h.msgh_id + 100;
        /* Never block on a client that does not listen: send-once replies cannot block, but be safe. */
        kr = mach_msg(&reply.h, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(reply), 0, MACH_PORT_NULL, 200, MACH_PORT_NULL);
        if (kr != KERN_SUCCESS) mach_msg_destroy(&reply.h);
    }
    g_stop = 1;
    pthread_join(mt, NULL);
    return 0;
}
