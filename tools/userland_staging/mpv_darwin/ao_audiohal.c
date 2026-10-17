/*
 * ao_audiohal: an mpv audio output for this system's audiod, through the
 * CoreAudio HAL subset in /usr/lib/libaudiohal.dylib (coreaudio/README.md).
 *
 * mpv's own CoreAudio outputs cannot be used: ao_coreaudio is an AudioUnit
 * client, and ao_coreaudio_exclusive needs hog mode and property listeners;
 * libaudiohal has neither (no AudioUnit, and AudioObjectAddPropertyListener
 * is deliberately not exported). It does have the HAL's IOProc model, which
 * is what this uses: a pull AO whose IOProc calls ao_read_data().
 *
 * build_libmpv.sh compiles this file against mpv's own headers and links it
 * into libmpv.dylib as audio_out_oss, the driver slot ao.c lists under
 * HAVE_OSS_AUDIO (that define is set in the generated config.h; there is no
 * OSS on this system). Its name is "audiohal", so --ao=audiohal selects it,
 * and it is the first native driver, so it is the default. mpv itself is
 * unmodified.
 *
 * libaudiohal is opened with dlopen, as Naples does for its volume control:
 * libmpv does not link it, so an image without audio still plays video (the
 * init fails and mpv moves on to its null output).
 *
 * Device model (libaudiohal): one output device, 48 kHz Float32 stereo,
 * fixed 512-frame IOProc buffers; the stream's virtual format is settable
 * (Float32 interleaved, 1 or 2 channels, 8-192 kHz) and audiod resamples.
 * So mpv is asked for interleaved float, mono or stereo (it downmixes
 * anything wider), at the source rate.
 */
#include <CoreAudio/AudioHardware.h>
#include <dlfcn.h>
#include <mach/mach_time.h>
#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "common/common.h"
#include "common/msg.h"
#include "audio/format.h"
#include "audio/chmap.h"
#include "audio/chmap_sel.h"
#include "audio/out/ao.h"
#include "audio/out/internal.h"
#include "osdep/timer.h"

/* the HAL calls this output makes, from /usr/lib/libaudiohal.dylib */
static struct {
    OSStatus (*get)(AudioObjectID, const AudioObjectPropertyAddress *, UInt32, const void *, UInt32 *, void *);
    OSStatus (*set)(AudioObjectID, const AudioObjectPropertyAddress *, UInt32, const void *, UInt32, const void *);
    OSStatus (*create)(AudioObjectID, AudioDeviceIOProc, void *, AudioDeviceIOProcID *);
    OSStatus (*destroy)(AudioObjectID, AudioDeviceIOProcID);
    OSStatus (*start)(AudioObjectID, AudioDeviceIOProcID);
    OSStatus (*stop)(AudioObjectID, AudioDeviceIOProcID);
} hal;

static bool load_hal(struct ao *ao)
{
    if (hal.stop)
        return true;
    void *h = dlopen("/usr/lib/libaudiohal.dylib", RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        MP_VERBOSE(ao, "no libaudiohal: %s\n", dlerror());
        return false;
    }
    hal.get = dlsym(h, "AudioObjectGetPropertyData");
    hal.set = dlsym(h, "AudioObjectSetPropertyData");
    hal.create = dlsym(h, "AudioDeviceCreateIOProcID");
    hal.destroy = dlsym(h, "AudioDeviceDestroyIOProcID");
    hal.start = dlsym(h, "AudioDeviceStart");
    if (!hal.get || !hal.set || !hal.create || !hal.destroy || !hal.start ||
        !(hal.stop = dlsym(h, "AudioDeviceStop"))) {
        MP_ERR(ao, "libaudiohal lacks a HAL call this output needs\n");
        return false;
    }
    return true;
}

struct priv {
    AudioObjectID device;
    AudioObjectID stream;
    AudioDeviceIOProcID proc;
    bool running;
};

static OSStatus get_prop(AudioObjectID obj, AudioObjectPropertySelector sel, AudioObjectPropertyScope scope,
                         void *out, UInt32 size)
{
    AudioObjectPropertyAddress a = { sel, scope, kAudioObjectPropertyElementMaster };
    UInt32 n = size;
    return hal.get(obj, &a, 0, NULL, &n, out);
}

/* Runs on libaudiohal's real-time IO thread, one 512-frame buffer at a time. */
static OSStatus io_proc(AudioObjectID dev, const AudioTimeStamp *now, const AudioBufferList *in,
                        const AudioTimeStamp *in_time, AudioBufferList *out, const AudioTimeStamp *out_time,
                        void *ctx)
{
    struct ao *ao = ctx;
    AudioBuffer *b = &out->mBuffers[0];
    void *planes[MP_NUM_CHANNELS] = { b->mData };
    int frames = b->mDataByteSize / ao->sstride;

    /* out_time is when the first frame of this buffer reaches the jack (libaudiohal adds the engine lead and what
     * audiod has queued); mpv wants the time its last frame finishes. */
    int64_t end = mp_time_ns_from_raw_time(mp_raw_time_ns_from_mach(out_time->mHostTime));
    end += MP_TIME_S_TO_NS(frames) / ao->samplerate;
    ao_read_data(ao, planes, frames, end, NULL, true, true);
    return kAudioHardwareNoError;
}

static void stop(struct ao *ao)
{
    struct priv *p = ao->priv;
    if (p->running)
        hal.stop(p->device, p->proc);
    p->running = false;
}

static void uninit(struct ao *ao)
{
    struct priv *p = ao->priv;
    stop(ao);
    if (p->proc)
        hal.destroy(p->device, p->proc);
    p->proc = NULL;
}

static int init(struct ao *ao)
{
    struct priv *p = ao->priv;
    OSStatus err;

    if (!load_hal(ao))
        return -1;
    /* audiod is started on demand by launchd; this first RPC is what starts it */
    err = get_prop(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice,
                   kAudioObjectPropertyScopeGlobal, &p->device, sizeof(p->device));
    if (err != kAudioHardwareNoError || !p->device) {
        MP_ERR(ao, "no output device (audiod not reachable?), error %d\n", (int)err);
        return -1;
    }
    err = get_prop(p->device, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput,
                   &p->stream, sizeof(p->stream));
    if (err != kAudioHardwareNoError || !p->stream) {
        MP_ERR(ao, "output device has no stream, error %d\n", (int)err);
        return -1;
    }

    struct mp_chmap_sel sel = {0};
    mp_chmap_sel_add_map(&sel, &(struct mp_chmap)MP_CHMAP_INIT_MONO);
    mp_chmap_sel_add_map(&sel, &(struct mp_chmap)MP_CHMAP_INIT_STEREO);
    if (!ao_chmap_sel_adjust(ao, &sel, &ao->channels))
        return -1;
    ao->format = AF_FORMAT_FLOAT;
    ao->samplerate = MPCLAMP(ao->samplerate, 8000, 192000);

    AudioStreamBasicDescription f = {
        .mSampleRate = ao->samplerate,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagsNativeFloatPacked,
        .mBytesPerPacket = 4 * ao->channels.num,
        .mFramesPerPacket = 1,
        .mBytesPerFrame = 4 * ao->channels.num,
        .mChannelsPerFrame = ao->channels.num,
        .mBitsPerChannel = 32,
    };
    AudioObjectPropertyAddress fa = { kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal,
                                      kAudioObjectPropertyElementMaster };
    err = hal.set(p->stream, &fa, 0, NULL, sizeof(f), &f);
    if (err != kAudioHardwareNoError) {
        MP_ERR(ao, "audiod refused %d Hz, %d channels: error %d\n", ao->samplerate, ao->channels.num, (int)err);
        return -1;
    }

    UInt32 bufsize = 512;
    get_prop(p->device, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal,
             &bufsize, sizeof(bufsize));
    ao->device_buffer = bufsize;

    err = hal.create(p->device, io_proc, ao, &p->proc);
    if (err != kAudioHardwareNoError) {
        MP_ERR(ao, "AudioDeviceCreateIOProcID failed: error %d\n", (int)err);
        return -1;
    }
    MP_VERBOSE(ao, "audiod output: %d Hz, %d channel(s), float, %u-frame buffers\n",
               ao->samplerate, ao->channels.num, (unsigned)bufsize);
    return 1;
}

static void start(struct ao *ao)
{
    struct priv *p = ao->priv;
    if (p->running)
        return;
    OSStatus err = hal.start(p->device, p->proc);
    if (err != kAudioHardwareNoError)
        MP_ERR(ao, "AudioDeviceStart failed: error %d\n", (int)err);
    else
        p->running = true;
}

/* Pausing stops the IO thread (and closes the audiod stream) rather than feeding it silence. */
static bool set_pause(struct ao *ao, bool paused)
{
    if (paused)
        stop(ao);
    else
        start(ao);
    return true;
}

const struct ao_driver audio_out_oss = {
    .description = "audiod (CoreAudio HAL subset, libaudiohal)",
    .name = "audiohal",
    .init = init,
    .uninit = uninit,
    .reset = stop,
    .start = start,
    .set_pause = set_pause,
    .priv_size = sizeof(struct priv),
};
