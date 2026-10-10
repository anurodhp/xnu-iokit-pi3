/*
 * libaudiohal: a small, source-compatible subset of CoreAudio's HAL API
 * (CoreAudio/AudioHardware.h, public header) for ports of software that expects
 * to find a CoreAudio output device. Apple's implementation is closed source;
 * this is project code written from that header's documented contract. Where the
 * header is silent the choice is made here and is noted as a choice.
 *
 * Exports exactly: AudioObjectHasProperty, AudioObjectIsPropertySettable,
 * AudioObjectGetPropertyDataSize, AudioObjectGetPropertyData,
 * AudioObjectSetPropertyData, AudioDeviceCreateIOProcID,
 * AudioDeviceDestroyIOProcID, AudioDeviceStart, AudioDeviceStop. Anything else
 * is deliberately absent so a missing function is a link error, not a silent no-op.
 *
 * Objects (choice): 1 = system object (kAudioObjectSystemObject), 2 = the one
 * output device, 3 = its output stream. Talks to audiod (see ca_protocol.h).
 */
#include <CoreAudio/AudioHardware.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <time.h>
#include "ca_protocol.h"

extern mach_port_t bootstrap_port;
extern mach_port_t mach_reply_port(void);
extern kern_return_t bootstrap_look_up(mach_port_t bp, const char *name, mach_port_t *sp);

#define EXPORT __attribute__((visibility("default")))

enum { OBJ_SYSTEM = kAudioObjectSystemObject, OBJ_DEVICE = 2, OBJ_STREAM = 3 };
#define IO_FRAMES        512     /* choice: the buffer size handed to an IOProc */
#define TARGET_FILL      2048    /* frames kept queued in audiod per stream (stream rate) */
#define MAX_PROCS        8

/* ---------------- RPC to audiod ---------------- */

static pthread_mutex_t g_rpc_mu = PTHREAD_MUTEX_INITIALIZER;
static mach_port_t g_svc = MACH_PORT_NULL;
static mach_port_t g_reply = MACH_PORT_NULL;
static uint32_t g_seq;
static ca_request_rcv_t g_buf;     /* one buffer for the combined send+receive; guarded by g_rpc_mu */

static int lookup_locked(int tries)
{
    for (int i = 0; i < tries; i++) {
        mach_port_t p = MACH_PORT_NULL;
        if (bootstrap_look_up(bootstrap_port, CA_SERVICE_NAME, &p) == KERN_SUCCESS && p != MACH_PORT_NULL) {
            g_svc = p;
            return 0;
        }
        usleep(100000);
    }
    return -1;
}

/* MACH_NOTIFY_SEND_ONCE (mach/notify.h): delivered to the reply port when audiod dies with our request unanswered. */
#define CA_NOTIFY_SEND_ONCE 71

static void reset_conn_locked(void)
{
    if (g_svc != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), g_svc);
    g_svc = MACH_PORT_NULL;
    if (g_reply != MACH_PORT_NULL) mach_port_mod_refs(mach_task_self(), g_reply, MACH_PORT_RIGHT_RECEIVE, -1);
    g_reply = MACH_PORT_NULL;
}

/* Sends g_buf.m (already filled except header/seq) and leaves the reply in g_buf.
 * Retries with a fresh look-up when audiod went away (idle exit / crash) or is exiting. */
static kern_return_t rpc_locked(uint32_t op, mach_msg_size_t size, ca_reply_t *out)
{
    for (int attempt = 0; attempt < 3; attempt++) {
        int fresh = 0;
        if (g_svc == MACH_PORT_NULL) { if (lookup_locked(30)) return KERN_FAILURE; fresh = 1; }
        if (g_reply == MACH_PORT_NULL) g_reply = mach_reply_port();
        ca_request_t *rq = &g_buf.m;
        rq->version = CA_PROTO_VERSION;
        rq->seq = ++g_seq;
        rq->h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
        rq->h.msgh_remote_port = g_svc;
        rq->h.msgh_local_port = g_reply;
        rq->h.msgh_size = size;
        rq->h.msgh_id = (mach_msg_id_t)op;
        const uint32_t want = rq->seq;
        /* A just-looked-up service may be launchd's placeholder: the first request waits for the job to start (dyld, CF). */
        const mach_msg_timeout_t tmo = fresh ? 20000 : 3000;
        kern_return_t kr = mach_msg(&rq->h, MACH_SEND_MSG | MACH_RCV_MSG | MACH_RCV_TIMEOUT | MACH_SEND_TIMEOUT,
            size, sizeof(g_buf), g_reply, tmo, MACH_PORT_NULL);
        int dead = 0;
        for (int tries = 0; ; tries++) {
            if (kr != KERN_SUCCESS) break;
            const mach_msg_header_t *h = &g_buf.m.h;
            if (h->msgh_id == CA_NOTIFY_SEND_ONCE) { dead = 1; break; }       /* audiod died with our request queued */
            const ca_reply_t *rp = (const ca_reply_t *)&g_buf;
            if (h->msgh_id == (mach_msg_id_t)(op + 100) && h->msgh_size == sizeof(ca_reply_t) && rp->seq == want) {
                *out = *rp;
                if (out->kr == KERN_ABORTED) { dead = 1; break; }              /* audiod is exiting: look it up again */
                return out->kr;
            }
            if (tries >= 8) return KERN_FAILURE;                              /* too many stale replies */
            kr = mach_msg(&g_buf.m.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(g_buf), g_reply, tmo, MACH_PORT_NULL);
        }
        if (kr == MACH_RCV_TIMED_OUT || kr == MACH_SEND_TIMED_OUT) { reset_conn_locked(); return KERN_FAILURE; }
        reset_conn_locked();
        if (!dead && kr != MACH_SEND_INVALID_DEST && kr != MACH_RCV_PORT_DIED && kr != MACH_SEND_INVALID_REPLY &&
            kr != MACH_SEND_INVALID_RIGHT) return KERN_FAILURE;
        usleep(200000);                       /* give launchd time to see the exit before the next look-up */
    }
    return KERN_FAILURE;
}

typedef struct { uint32_t id, token, a, b, nframes, channels; float f; const void *data; size_t data_bytes; } call_t;

static kern_return_t call(uint32_t op, const call_t *c, ca_reply_t *out)
{
    pthread_mutex_lock(&g_rpc_mu);
    ca_request_t *rq = &g_buf.m;
    memset(rq, 0, offsetof(ca_request_t, data));
    rq->id = c->id; rq->token = c->token; rq->a = c->a; rq->b = c->b; rq->f = c->f;
    rq->nframes = c->nframes; rq->channels_check = c->channels;
    if (c->data_bytes) memcpy(rq->data, c->data, c->data_bytes);
    kern_return_t kr = rpc_locked(op, CA_REQ_SIZE(c->data_bytes), out);
    pthread_mutex_unlock(&g_rpc_mu);
    return kr;
}

static OSStatus daemon_status(kern_return_t kr)
{
    if (kr == KERN_SUCCESS) return kAudioHardwareNoError;
    if (kr == KERN_FAILURE) return kAudioHardwareNotRunningError;
    if (kr == KERN_INVALID_ARGUMENT) return kAudioHardwareIllegalOperationError;
    return kAudioHardwareUnspecifiedError;
}

static kern_return_t get_info(ca_reply_t *r) { call_t c = {0}; return call(CA_OP_GET_INFO, &c, r); }

/* ---------------- properties ---------------- */

static int is_global(const AudioObjectPropertyAddress *a) { return a->mScope == kAudioObjectPropertyScopeGlobal; }
static int is_output(const AudioObjectPropertyAddress *a)  { return a->mScope == kAudioObjectPropertyScopeOutput; }

/* The stream format clients see and may change (kAudioStreamPropertyVirtualFormat, settable like a real HAL's: the
 * HAL converts between the client's format and the device's). audiod converts any 8-192 kHz mono/stereo Float32 to the
 * device's 48 kHz stereo. Applies to IOProcs started afterwards. Guarded by g_fmt_mu. */
static pthread_mutex_t g_fmt_mu = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_vfmt_rate = CA_DEVICE_RATE, g_vfmt_ch = 2;

static AudioStreamBasicDescription device_format(void)
{
    AudioStreamBasicDescription f = {0};
    pthread_mutex_lock(&g_fmt_mu);
    const uint32_t rate = g_vfmt_rate, ch = g_vfmt_ch;
    pthread_mutex_unlock(&g_fmt_mu);
    f.mSampleRate = rate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;     /* interleaved: kAudioFormatFlagIsNonInterleaved unset */
    f.mBytesPerPacket = 4 * ch; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4 * ch;
    f.mChannelsPerFrame = ch; f.mBitsPerChannel = 32;
    return f;
}

/* Returns the property's size (and whether it is settable) or -1 if the object does not have it. */
static int prop_size(AudioObjectID obj, const AudioObjectPropertyAddress *a, int *settable)
{
    *settable = 0;
    if (!a) return -1;
    switch (obj) {
    case OBJ_SYSTEM:
        if (a->mSelector == kAudioObjectPropertyClass || a->mSelector == kAudioObjectPropertyBaseClass) return (int)sizeof(AudioClassID);
        if (a->mSelector == kAudioHardwarePropertyDevices) return is_global(a) ? (int)sizeof(AudioObjectID) : -1;
        if (a->mSelector == kAudioHardwarePropertyDefaultOutputDevice ||
            a->mSelector == kAudioHardwarePropertyDefaultSystemOutputDevice) return (int)sizeof(AudioObjectID);
        return -1;
    case OBJ_DEVICE:
        switch (a->mSelector) {
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyBaseClass:      return (int)sizeof(AudioClassID);
        case kAudioObjectPropertyOwner:          return (int)sizeof(AudioObjectID);
        case kAudioObjectPropertyName:
        case kAudioObjectPropertyManufacturer:
        case kAudioDevicePropertyDeviceUID:      return (int)sizeof(CFStringRef);
        case kAudioDevicePropertyTransportType:  return (int)sizeof(UInt32);
        case kAudioDevicePropertyBufferFrameSizeRange: return (int)sizeof(AudioValueRange);
        case kAudioDevicePropertyNominalSampleRate: return (int)sizeof(Float64);
        case kAudioDevicePropertyStreams:        return (is_output(a) || is_global(a)) ? (int)sizeof(AudioObjectID) : 0;
        case kAudioDevicePropertyStreamConfiguration:
            return (int)(offsetof(AudioBufferList, mBuffers) + (is_output(a) ? sizeof(AudioBuffer) : 0));
        case kAudioDevicePropertyDeviceIsAlive:  return (int)sizeof(UInt32);
        case kAudioDevicePropertyBufferFrameSize:return (int)sizeof(UInt32);
        case kAudioDevicePropertyLatency:        return is_output(a) ? (int)sizeof(UInt32) : -1;
        case kAudioDevicePropertyVolumeScalar:
        case kAudioDevicePropertyMute:
            if (!is_output(a) || a->mElement != kAudioObjectPropertyElementMaster) return -1;
            *settable = 1;
            return a->mSelector == kAudioDevicePropertyMute ? (int)sizeof(UInt32) : (int)sizeof(Float32);
        }
        return -1;
    case OBJ_STREAM:
        switch (a->mSelector) {
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyBaseClass:      return (int)sizeof(AudioClassID);
        case kAudioObjectPropertyOwner:          return (int)sizeof(AudioObjectID);
        case kAudioStreamPropertyVirtualFormat:  *settable = 1; return (int)sizeof(AudioStreamBasicDescription);
        case kAudioStreamPropertyPhysicalFormat: return (int)sizeof(AudioStreamBasicDescription);
        case kAudioStreamPropertyDirection:      return (int)sizeof(UInt32);
        }
        return -1;
    }
    return -1;
}

EXPORT Boolean AudioObjectHasProperty(AudioObjectID obj, const AudioObjectPropertyAddress *a)
{
    int s; return prop_size(obj, a, &s) >= 0;
}

EXPORT OSStatus AudioObjectIsPropertySettable(AudioObjectID obj, const AudioObjectPropertyAddress *a, Boolean *out)
{
    int s;
    if (!out) return kAudioHardwareIllegalOperationError;
    if (obj < OBJ_SYSTEM || obj > OBJ_STREAM) return kAudioHardwareBadObjectError;
    if (prop_size(obj, a, &s) < 0) return kAudioHardwareUnknownPropertyError;
    *out = s ? true : false;
    return kAudioHardwareNoError;
}

EXPORT OSStatus AudioObjectGetPropertyDataSize(AudioObjectID obj, const AudioObjectPropertyAddress *a,
    UInt32 qsize, const void *q, UInt32 *out)
{
    (void)qsize; (void)q;
    int s, n;
    if (!out) return kAudioHardwareIllegalOperationError;
    if (obj < OBJ_SYSTEM || obj > OBJ_STREAM) return kAudioHardwareBadObjectError;
    n = prop_size(obj, a, &s);
    if (n < 0) return kAudioHardwareUnknownPropertyError;
    *out = (UInt32)n;
    return kAudioHardwareNoError;
}

EXPORT OSStatus AudioObjectGetPropertyData(AudioObjectID obj, const AudioObjectPropertyAddress *a,
    UInt32 qsize, const void *q, UInt32 *io_size, void *out)
{
    (void)qsize; (void)q;
    int s;
    if (!io_size || !out) return kAudioHardwareIllegalOperationError;
    if (obj < OBJ_SYSTEM || obj > OBJ_STREAM) return kAudioHardwareBadObjectError;
    int need = prop_size(obj, a, &s);
    if (need < 0) return kAudioHardwareUnknownPropertyError;
    const int is_list = (obj == OBJ_SYSTEM && a->mSelector == kAudioHardwarePropertyDevices) ||
                        (obj == OBJ_DEVICE && a->mSelector == kAudioDevicePropertyStreams);
    if (is_list && (int)*io_size < need) { *io_size = 0; return kAudioHardwareNoError; }   /* a list copies as many whole items as fit */
    if ((int)*io_size < need) return kAudioHardwareBadPropertySizeError;
    *io_size = (UInt32)need;
    ca_reply_t r;

    if (a->mSelector == kAudioObjectPropertyClass || a->mSelector == kAudioObjectPropertyBaseClass) {
        const int base = a->mSelector == kAudioObjectPropertyBaseClass;
        *(AudioClassID *)out = base ? kAudioObjectClassID
            : (obj == OBJ_SYSTEM ? kAudioSystemObjectClassID : obj == OBJ_DEVICE ? kAudioDeviceClassID : kAudioStreamClassID);
        return kAudioHardwareNoError;
    }
    if (a->mSelector == kAudioObjectPropertyOwner) {
        *(AudioObjectID *)out = obj == OBJ_STREAM ? OBJ_DEVICE : OBJ_SYSTEM;
        return kAudioHardwareNoError;
    }
    if (obj == OBJ_SYSTEM) {
        *(AudioObjectID *)out = OBJ_DEVICE;
        return kAudioHardwareNoError;
    }
    if (obj == OBJ_STREAM) {
        if (a->mSelector == kAudioStreamPropertyDirection) *(UInt32 *)out = 0;       /* 0 = output */
        else *(AudioStreamBasicDescription *)out = device_format();
        return kAudioHardwareNoError;
    }
    switch (a->mSelector) {
    case kAudioObjectPropertyName:
        *(CFStringRef *)out = CFStringCreateWithCString(NULL, CA_DEVICE_NAME, kCFStringEncodingUTF8);
        return kAudioHardwareNoError;
    case kAudioObjectPropertyManufacturer:
        *(CFStringRef *)out = CFStringCreateWithCString(NULL, "PureDarwin", kCFStringEncodingUTF8);
        return kAudioHardwareNoError;
    case kAudioDevicePropertyDeviceUID:          /* choice: stable, one device */
        *(CFStringRef *)out = CFStringCreateWithCString(NULL, "org.puredarwin.coreaudiod.bcm2837pwm0", kCFStringEncodingUTF8);
        return kAudioHardwareNoError;
    case kAudioDevicePropertyTransportType:      *(UInt32 *)out = kAudioDeviceTransportTypeBuiltIn; return kAudioHardwareNoError;
    case kAudioDevicePropertyBufferFrameSizeRange: {
        AudioValueRange *r = out;
        r->mMinimum = r->mMaximum = IO_FRAMES;   /* v1: the IO buffer size is fixed */
        return kAudioHardwareNoError;
    }
    case kAudioDevicePropertyNominalSampleRate: *(Float64 *)out = CA_DEVICE_RATE; return kAudioHardwareNoError;
    case kAudioDevicePropertyStreams:
        if (need) *(AudioObjectID *)out = OBJ_STREAM;
        *io_size = (UInt32)need;
        return kAudioHardwareNoError;
    case kAudioDevicePropertyStreamConfiguration: {
        AudioBufferList *l = out;
        l->mNumberBuffers = is_output(a) ? 1 : 0;
        if (is_output(a)) { l->mBuffers[0].mNumberChannels = 2; l->mBuffers[0].mDataByteSize = IO_FRAMES * 8; l->mBuffers[0].mData = NULL; }
        return kAudioHardwareNoError;
    }
    case kAudioDevicePropertyDeviceIsAlive: {
        kern_return_t kr = get_info(&r);                  /* alive = the daemon answers */
        *(UInt32 *)out = kr == KERN_SUCCESS;
        return kAudioHardwareNoError;
    }
    case kAudioDevicePropertyBufferFrameSize: *(UInt32 *)out = IO_FRAMES; return kAudioHardwareNoError;
    case kAudioDevicePropertyLatency: *(UInt32 *)out = 2560 + TARGET_FILL; return kAudioHardwareNoError;   /* engine lead + queued */
    case kAudioDevicePropertyVolumeScalar: {
        kern_return_t kr = get_info(&r);
        if (kr) return daemon_status(kr);
        *(Float32 *)out = r.f;
        return kAudioHardwareNoError;
    }
    case kAudioDevicePropertyMute: {
        kern_return_t kr = get_info(&r);
        if (kr) return daemon_status(kr);
        *(UInt32 *)out = r.a;
        return kAudioHardwareNoError;
    }
    }
    return kAudioHardwareUnknownPropertyError;
}

EXPORT OSStatus AudioObjectSetPropertyData(AudioObjectID obj, const AudioObjectPropertyAddress *a,
    UInt32 qsize, const void *q, UInt32 size, const void *data)
{
    (void)qsize; (void)q;
    int settable;
    if (!data) return kAudioHardwareIllegalOperationError;
    if (obj < OBJ_SYSTEM || obj > OBJ_STREAM) return kAudioHardwareBadObjectError;
    int need = prop_size(obj, a, &settable);
    if (need < 0) return kAudioHardwareUnknownPropertyError;
    if (!settable) return kAudioHardwareIllegalOperationError;
    if ((int)size != need) return kAudioHardwareBadPropertySizeError;
    ca_reply_t r;
    call_t c = {0};
    if (obj == OBJ_STREAM) {                                    /* only VirtualFormat is settable on the stream */
        const AudioStreamBasicDescription *f = data;
        if (f->mFormatID != kAudioFormatLinearPCM || f->mFramesPerPacket != 1 ||
            !(f->mFormatFlags & kAudioFormatFlagIsFloat) || (f->mFormatFlags & kAudioFormatFlagIsNonInterleaved) ||
            f->mBitsPerChannel != 32 || (f->mChannelsPerFrame != 1 && f->mChannelsPerFrame != 2) ||
            f->mBytesPerFrame != 4 * f->mChannelsPerFrame || f->mBytesPerPacket != f->mBytesPerFrame ||
            !(f->mSampleRate >= CA_MIN_RATE && f->mSampleRate <= CA_MAX_RATE) ||
            f->mSampleRate != (Float64)(uint32_t)f->mSampleRate) return kAudioDeviceUnsupportedFormatError;
        pthread_mutex_lock(&g_fmt_mu);
        g_vfmt_rate = (uint32_t)f->mSampleRate; g_vfmt_ch = f->mChannelsPerFrame;
        pthread_mutex_unlock(&g_fmt_mu);
        return kAudioHardwareNoError;
    }
    if (a->mSelector == kAudioDevicePropertyVolumeScalar) {
        float v = *(const Float32 *)data;
        if (!(v >= 0.0f && v <= 1.0f)) return kAudioHardwareIllegalOperationError;
        c.f = v; c.b = CA_SET_VOLUME;
    } else {
        c.a = *(const UInt32 *)data ? 1 : 0; c.b = CA_SET_MUTE;
    }
    return daemon_status(call(CA_OP_SET_VOLUME, &c, &r));
}

/* ---------------- IOProcs ---------------- */

typedef struct ioproc {
    int                 used;
    AudioDeviceIOProc   proc;
    void               *client;
    volatile int        run;             /* set by Start, cleared by Stop (or by the IOProc stopping itself) */
    volatile int        destroy_pending; /* Destroy came from inside an IOProc: the thread frees the slot as it exits */
    int                 started;         /* a thread exists (guarded by g_procs_mu except at its own exit) */
    pthread_t           th;
    uint32_t            stream, token;
    uint32_t            rate, chans;     /* stream format this IOProc was started with */
} ioproc_t;

static pthread_mutex_t g_procs_mu = PTHREAD_MUTEX_INITIALIZER;
static ioproc_t g_procs[MAX_PROCS];

static void sleep_ms(long ms) { struct timespec ts = { 0, ms * 1000000L }; nanosleep(&ts, NULL); }

/* After fork() only the calling thread exists: the parent's port names, locks and IO threads are not ours. */
static void atfork_child(void)
{
    pthread_mutex_t fresh = PTHREAD_MUTEX_INITIALIZER;
    g_rpc_mu = fresh; g_procs_mu = fresh; g_fmt_mu = fresh;
    g_svc = MACH_PORT_NULL; g_reply = MACH_PORT_NULL;
    memset(g_procs, 0, sizeof(g_procs));
}
__attribute__((constructor)) static void libaudiohal_init(void) { pthread_atfork(NULL, NULL, atfork_child); }

static int open_stream(ioproc_t *p)
{
    ca_reply_t r;
    call_t oc = { .a = p->rate, .b = p->chans };
    if (call(CA_OP_OPEN, &oc, &r) != KERN_SUCCESS) return -1;
    p->stream = r.id;
    p->token = r.token;
    return 0;
}

static void *io_thread(void *arg)
{
    ioproc_t *p = arg;
    float buf[IO_FRAMES * 2];
    AudioBufferList noinput = { 0 };          /* output only: zero input buffers */
    AudioBufferList abl;                      /* one buffer: AudioBufferList already holds mBuffers[1] */
    abl.mNumberBuffers = 1;
    const uint32_t ch = p->chans, rate = p->rate;
    abl.mBuffers[0].mNumberChannels = ch;
    abl.mBuffers[0].mDataByteSize = IO_FRAMES * ch * sizeof(float);
    abl.mBuffers[0].mData = buf;
    uint64_t frames_done = 0;
    int stream_started = 0;
    ca_reply_t r;
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);

    while (p->run) {
        /* Pace: keep about TARGET_FILL frames queued in the daemon. A zero-frame WRITE reports the room. */
        call_t pc = { .id = p->stream, .token = p->token, .channels = ch };
        kern_return_t kr = call(CA_OP_WRITE, &pc, &r);
        if (kr == KERN_INVALID_NAME) {
            /* The daemon dropped our stream (it reaped it after a stall, or restarted): open a new one. */
            if (open_stream(p) == 0) stream_started = 0;
            else sleep_ms(50);
            continue;
        }
        if (kr != KERN_SUCCESS) { sleep_ms(20); continue; }
        if (CA_STREAM_RING_FRAMES - r.b > TARGET_FILL) {
            if (!stream_started) { call_t sc = { .id = p->stream, .token = p->token }; call(CA_OP_START, &sc, &r); stream_started = 1; }
            sleep_ms(4);
            continue;
        }

        AudioTimeStamp now = {0}, out = {0};
        uint64_t h = mach_absolute_time();
        now.mSampleTime = (Float64)frames_done; now.mHostTime = h;
        now.mFlags = kAudioTimeStampSampleTimeValid | kAudioTimeStampHostTimeValid;
        out = now;
        out.mSampleTime = (Float64)(frames_done + 2560ULL * rate / CA_DEVICE_RATE + TARGET_FILL);     /* engine lead + queued, in stream frames */
        out.mHostTime = h + (uint64_t)(2560 * (1e9 / CA_DEVICE_RATE) + TARGET_FILL * (1e9 / rate)) * tb.denom / tb.numer;
        memset(buf, 0, IO_FRAMES * ch * sizeof(float));         /* choice: an IOProc that writes nothing produces silence */
        p->proc(OBJ_DEVICE, &now, &noinput, &now, &abl, &out, p->client);
        frames_done += IO_FRAMES;

        uint32_t off = 0;
        while (off < IO_FRAMES && p->run) {
            uint32_t n = IO_FRAMES - off;
            call_t wc = { .id = p->stream, .token = p->token, .nframes = n, .channels = ch, .data = buf + off * ch, .data_bytes = n * ch * sizeof(float) };
            kr = call(CA_OP_WRITE, &wc, &r);
            if (kr == KERN_INVALID_NAME) { if (open_stream(p) == 0) stream_started = 0; else sleep_ms(50); break; }
            if (kr != KERN_SUCCESS) { sleep_ms(10); continue; }
            off += r.a;
            if (r.a < n) sleep_ms(4);
        }
    }
    call_t cc = { .id = p->stream, .token = p->token };
    call(CA_OP_CLOSE, &cc, &r);
    if (p->destroy_pending) {                 /* Destroy was called from an IOProc: nobody will join us */
        pthread_detach(pthread_self());
        p->started = 0;
        p->used = 0;
    }
    return NULL;
}

static ioproc_t *proc_from_id(AudioDeviceIOProcID id)
{
    for (int i = 0; i < MAX_PROCS; i++) if ((void *)&g_procs[i] == (void *)id && g_procs[i].used) return &g_procs[i];
    return NULL;
}

/* True when the caller is one of our IO threads, i.e. an IOProc calling Stop/Destroy: it must not join. */
static int in_io_thread(void)
{
    for (int i = 0; i < MAX_PROCS; i++)
        if (g_procs[i].used && g_procs[i].started && pthread_equal(pthread_self(), g_procs[i].th)) return 1;
    return 0;
}

EXPORT OSStatus AudioDeviceCreateIOProcID(AudioObjectID dev, AudioDeviceIOProc proc, void *client, AudioDeviceIOProcID *out)
{
    if (dev != OBJ_DEVICE) return kAudioHardwareBadObjectError;
    if (!proc || !out) return kAudioHardwareIllegalOperationError;
    pthread_mutex_lock(&g_procs_mu);
    for (int i = 0; i < MAX_PROCS; i++) {
        if (!g_procs[i].used) {
            memset(&g_procs[i], 0, sizeof(g_procs[i]));
            g_procs[i].used = 1; g_procs[i].proc = proc; g_procs[i].client = client;
            *out = (AudioDeviceIOProcID)(void *)&g_procs[i];
            pthread_mutex_unlock(&g_procs_mu);
            return kAudioHardwareNoError;
        }
    }
    pthread_mutex_unlock(&g_procs_mu);
    return kAudioHardwareUnspecifiedError;
}

EXPORT OSStatus AudioDeviceStart(AudioObjectID dev, AudioDeviceIOProcID id)
{
    if (dev != OBJ_DEVICE) return kAudioHardwareBadObjectError;
    if (!id) return kAudioHardwareNoError;       /* AudioHardware.h: NULL means start the hardware with no IOProc; nothing to do here */
    pthread_mutex_lock(&g_procs_mu);
    ioproc_t *p = proc_from_id(id);
    if (!p) { pthread_mutex_unlock(&g_procs_mu); return kAudioHardwareIllegalOperationError; }
    if (p->started && p->run) { pthread_mutex_unlock(&g_procs_mu); return kAudioHardwareNoError; }
    if (p->started) {                            /* the IOProc stopped itself earlier: collect that thread first */
        if (pthread_equal(pthread_self(), p->th)) { pthread_mutex_unlock(&g_procs_mu); return kAudioHardwareIllegalOperationError; }
        pthread_join(p->th, NULL);
        p->started = 0;
    }
    pthread_mutex_lock(&g_fmt_mu);
    p->rate = g_vfmt_rate; p->chans = g_vfmt_ch;
    pthread_mutex_unlock(&g_fmt_mu);
    if (open_stream(p)) { pthread_mutex_unlock(&g_procs_mu); return kAudioHardwareNotRunningError; }
    p->run = 1;
    if (pthread_create(&p->th, NULL, io_thread, p)) {
        p->run = 0;
        ca_reply_t r; call_t cc = { .id = p->stream, .token = p->token }; call(CA_OP_CLOSE, &cc, &r);
        pthread_mutex_unlock(&g_procs_mu);
        return kAudioHardwareUnspecifiedError;
    }
    p->started = 1;
    pthread_mutex_unlock(&g_procs_mu);
    return kAudioHardwareNoError;
}

EXPORT OSStatus AudioDeviceStop(AudioObjectID dev, AudioDeviceIOProcID id)
{
    if (dev != OBJ_DEVICE) return kAudioHardwareBadObjectError;
    if (!id) return kAudioHardwareNoError;
    if (in_io_thread()) {                        /* from inside an IOProc: only ask the thread to finish */
        ioproc_t *p = proc_from_id(id);
        if (!p) return kAudioHardwareIllegalOperationError;
        p->run = 0;
        return kAudioHardwareNoError;
    }
    pthread_mutex_lock(&g_procs_mu);
    ioproc_t *p = proc_from_id(id);
    if (!p) { pthread_mutex_unlock(&g_procs_mu); return kAudioHardwareIllegalOperationError; }
    if (p->started) { p->run = 0; pthread_join(p->th, NULL); p->started = 0; }
    pthread_mutex_unlock(&g_procs_mu);
    return kAudioHardwareNoError;
}

EXPORT OSStatus AudioDeviceDestroyIOProcID(AudioObjectID dev, AudioDeviceIOProcID id)
{
    if (dev != OBJ_DEVICE) return kAudioHardwareBadObjectError;
    if (in_io_thread()) {
        ioproc_t *p = proc_from_id(id);
        if (!p) return kAudioHardwareIllegalOperationError;
        if (p->started) { p->destroy_pending = 1; p->run = 0; }   /* the thread frees the slot when it exits */
        else p->used = 0;
        return kAudioHardwareNoError;
    }
    pthread_mutex_lock(&g_procs_mu);
    ioproc_t *p = proc_from_id(id);
    if (!p) { pthread_mutex_unlock(&g_procs_mu); return kAudioHardwareIllegalOperationError; }
    if (p->started) { p->run = 0; pthread_join(p->th, NULL); p->started = 0; }
    p->used = 0;
    pthread_mutex_unlock(&g_procs_mu);
    return kAudioHardwareNoError;
}
