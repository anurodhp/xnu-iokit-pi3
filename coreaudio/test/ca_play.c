/*
 * ca_play: plays a sine tone through the public CoreAudio HAL API
 * (libaudiohal -> audiod -> engine). Doubles as the smoketest.
 *   ca_play [-f hz] [-s seconds] [-a amplitude 0..1] [-v device_volume 0..1] [-r rate_hz] [-c channels]
 * -r/-c set the stream's virtual format through the public API (the daemon converts to 48 kHz stereo).
 */
#include <CoreAudio/AudioHardware.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

typedef struct { double phase, step; float amp; volatile unsigned long calls; } tone_t;

static OSStatus ioproc(AudioObjectID dev, const AudioTimeStamp *now, const AudioBufferList *in,
                       const AudioTimeStamp *intime, AudioBufferList *out, const AudioTimeStamp *outtime, void *cd)
{
    (void)dev; (void)now; (void)in; (void)intime; (void)outtime;
    tone_t *t = cd;
    if (out->mNumberBuffers < 1) return 0;
    float *p = out->mBuffers[0].mData;
    UInt32 ch = out->mBuffers[0].mNumberChannels;
    UInt32 frames = out->mBuffers[0].mDataByteSize / (ch * sizeof(float));
    for (UInt32 i = 0; i < frames; i++) {
        float v = (float)(t->amp * sin(t->phase));
        t->phase += t->step;
        if (t->phase > 2.0 * M_PI) t->phase -= 2.0 * M_PI;
        for (UInt32 c = 0; c < ch; c++) p[i * ch + c] = v;
    }
    t->calls++;
    return 0;
}

int main(int argc, char **argv)
{
    double freq = 440, seconds = 3, amp = 0.25, vol = -1, want_rate = 0, want_ch = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        double v = atof(argv[i + 1]);
        if (!strcmp(argv[i], "-f")) freq = v; else if (!strcmp(argv[i], "-s")) seconds = v;
        else if (!strcmp(argv[i], "-a")) amp = v; else if (!strcmp(argv[i], "-v")) vol = v;
        else if (!strcmp(argv[i], "-r")) want_rate = v; else if (!strcmp(argv[i], "-c")) want_ch = v;
        else { fprintf(stderr, "usage: ca_play [-f hz] [-s sec] [-a amp] [-v volume] [-r rate] [-c channels]\n"); return 64; }
    }
    if (amp < 0 || amp > 1 || freq < 20 || freq > 20000 || seconds <= 0 || seconds > 3600) { fprintf(stderr, "ca_play: argument out of range\n"); return 64; }

    AudioObjectPropertyAddress a = { kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster };
    AudioObjectID dev = 0;
    UInt32 sz = sizeof(dev);
    OSStatus st = AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &sz, &dev);
    if (st) { fprintf(stderr, "ca_play: default output device: %d (is audiod reachable?)\n", (int)st); return 1; }

    a.mSelector = kAudioObjectPropertyName;
    CFStringRef name = NULL; sz = sizeof(name);
    char nbuf[128] = "?";
    if (!AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, &name) && name) { CFStringGetCString(name, nbuf, sizeof nbuf, kCFStringEncodingUTF8); CFRelease(name); }
    a.mSelector = kAudioDevicePropertyNominalSampleRate;
    Float64 rate = 0; sz = sizeof(rate);
    st = AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, &rate);
    if (st || rate < 8000) { fprintf(stderr, "ca_play: sample rate: %d\n", (int)st); return 1; }
    printf("ca_play: device %u \"%s\" %.0f Hz\n", (unsigned)dev, nbuf, rate);

    /* The stream (id 3 on this HAL) is found through the device's stream list, as a real client would. */
    AudioObjectPropertyAddress sa = { kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMaster };
    AudioObjectID stream = 0; sz = sizeof(stream);
    st = AudioObjectGetPropertyData(dev, &sa, 0, NULL, &sz, &stream);
    if (st || !stream) { fprintf(stderr, "ca_play: stream list: %d\n", (int)st); return 1; }
    AudioObjectPropertyAddress fa = { kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster };
    AudioStreamBasicDescription fmt; sz = sizeof(fmt);
    st = AudioObjectGetPropertyData(stream, &fa, 0, NULL, &sz, &fmt);
    if (st) { fprintf(stderr, "ca_play: stream format: %d\n", (int)st); return 1; }
    if (want_rate > 0 || want_ch > 0) {
        if (want_rate > 0) fmt.mSampleRate = want_rate;
        if (want_ch > 0) { fmt.mChannelsPerFrame = (UInt32)want_ch; fmt.mBytesPerFrame = fmt.mBytesPerPacket = 4 * fmt.mChannelsPerFrame; }
        st = AudioObjectSetPropertyData(stream, &fa, 0, NULL, sizeof(fmt), &fmt);
        if (st) { fprintf(stderr, "ca_play: set stream format: %d\n", (int)st); return 1; }
        sz = sizeof(fmt);
        st = AudioObjectGetPropertyData(stream, &fa, 0, NULL, &sz, &fmt);
        if (st) return 1;
    }
    rate = fmt.mSampleRate;
    printf("ca_play: stream format %.0f Hz, %u ch, float32\n", fmt.mSampleRate, (unsigned)fmt.mChannelsPerFrame);

    if (vol >= 0) {
        AudioObjectPropertyAddress va = { kAudioDevicePropertyVolumeScalar, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMaster };
        Float32 v = (Float32)vol;
        st = AudioObjectSetPropertyData(dev, &va, 0, NULL, sizeof(v), &v);
        Float32 back = -1; sz = sizeof(back);
        OSStatus st2 = AudioObjectGetPropertyData(dev, &va, 0, NULL, &sz, &back);
        printf("ca_play: set volume %.2f -> %d, read back %.2f (%d)\n", vol, (int)st, back, (int)st2);
        if (st || st2 || fabsf(back - v) > 1e-6f) return 1;
    }

    tone_t t = { 0, 2.0 * M_PI * freq / rate, (float)amp, 0 };
    AudioDeviceIOProcID id = NULL;
    st = AudioDeviceCreateIOProcID(dev, ioproc, &t, &id);
    if (st) { fprintf(stderr, "ca_play: CreateIOProcID %d\n", (int)st); return 1; }
    st = AudioDeviceStart(dev, id);
    if (st) { fprintf(stderr, "ca_play: Start %d\n", (int)st); return 1; }
    usleep((useconds_t)(seconds * 1e6));
    AudioDeviceStop(dev, id);
    AudioDeviceDestroyIOProcID(dev, id);
    printf("ca_play: %.1f Hz amp %.2f for %.1f s: %lu IOProc calls (expected ~%.0f)\n", freq, amp, seconds, t.calls, seconds * rate / 512);
    return t.calls > 0 ? 0 : 1;
}
