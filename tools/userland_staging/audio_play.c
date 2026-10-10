/*
 * audio_play (DARW-319): plays a sine tone through the BCM2837PWMAudioEngine
 * the way the CoreAudio HAL would, i.e. through IOAudioEngineUserClient. This
 * port has no CoreAudio, so this is the userland end of the audio path.
 *
 *   audio_play [-f freq_hz] [-s seconds] [-a amplitude 0..1]
 *   audio_play -w file.wav [-a amplitude 0..1]     (16-bit PCM, mono or stereo, 48000 Hz)
 *
 * Protocol, from third_party/IOAudioFamily (IOAudioTypes.h, IOAudioEngine
 * UserClient.cpp):
 *   1. IOServiceOpen on the engine. The user client rejects every external
 *      method unless the task has the entitlement
 *      com.apple.private.audio.driver-host (IOAudioEngineUserClient.cpp:1183-
 *      1189), so this binary is ad-hoc signed with audio_play.entitlements; the
 *      kernel's amfi_standin honours self-asserted entitlements (DAR-414).
 *   2. Map kIOAudioStatusBuffer (0): struct IOAudioEngineStatus (IOAudioTypes.h
 *      :100-108): loop count, the time of the last ring wrap, erase head.
 *   3. Register a client buffer (kIOAudioEngineCallRegisterClientBuffer = 0:
 *      stream id, address, bytes, buffer set id; the stream id is the stream's
 *      index in the engine, 0 for the only one, IOAudioEngine.cpp:300-322). The
 *      buffer is an IOAudioBufferDataDescriptor header (IOAudioTypes.h:136-142,
 *      16 bytes: actual data bytes, actual frames, total size incl. header,
 *      nominal data bytes) followed by the float frames; performClientOutput reads the
 *      frame count and sizes from that header and rejects anything larger than
 *      the buffer (IOAudioEngineUserClient.cpp:1990-2008).
 *   4. kIOAudioEngineCallStart (3): the engine starts when the first client
 *      does. Wait for its first time stamp (performClientIO answers
 *      kIOReturnOffline until fCurrentLoopCount or fLastLoopTime is set,
 *      IOAudioEngineUserClient.cpp:1831-1850).
 *   5. Repeatedly: fill the buffer with the next chunk of the tone and call
 *      trap kIOAudioEngineTrapPerformClientIO (0) with (first sample frame,
 *      loop count, inputIO=0, buffer set id, sample interval hi, lo; the
 *      interval is the time of one frame in mach absolute-time ticks,
 *      IOAudioEngine.cpp:2417-2470), aimed a
 *      little ahead of the play position. The play position is derived from
 *      fLastLoopTime and the sample rate, as the HAL does. The kernel mixes the
 *      float chunk into the engine's mix buffer and the engine clips it
 *      (performClientOutput, :1893 onwards).
 *   6. kIOAudioEngineCallStop (4), unregister, unmap, close.
 * The kernel logs "BCM2837PWMAudio: stop: N frames through clipOutputSamples,
 * peak P" when the engine stops, which is the evidence on QEMU.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <IOKit/IOKitLib.h>

#define kEngineClass          "BCM2837PWMAudioEngine"
#define kStatusBuffer         0
#define kCallRegister         0
#define kCallUnregister       1
#define kCallStart            3
#define kCallStop             4
#define kTrapPerformClientIO  0

#define kRingFrames   4096       // BCM2837PWMAudio.h kPWMAudioFrames
#define kChunkFrames  512
#define kChannels     2
#define kRate         48000
#define kLeadFrames   2560       // aim this far ahead of the play position (53 ms; the ring is 4096)

// IOAudioTypes.h:136-142 (only the header; the floats follow it)
typedef struct {
    uint32_t fActualDataByteSize;
    uint32_t fActualNumSampleFrames;
    uint32_t fTotalDataByteSize;
    uint32_t fNominalDataByteSize;
} AudioBufferDataDescriptor;

// IOAudioTypes.h:100-108 (AbsoluteTime is a uint64 here)
typedef struct {
    uint32_t fVersion;
    volatile uint32_t fCurrentLoopCount;
    volatile uint64_t fLastLoopTime;
    volatile uint32_t fEraseHeadSampleFrame;
} AudioEngineStatus;

static uint64_t ticks_to_ns(uint64_t t, const mach_timebase_info_data_t *tb)
{
    return t * tb->numer / tb->denom;
}

// Minimal RIFF/WAVE reader: PCM, 16 bit, 1 or 2 channels, kRate Hz. Returns the
// samples (interleaved int16) and sets *frames / *chans, or NULL with a message.
static int16_t *load_wav(const char *path, uint32_t *frames, int *chans)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(len);
    if (!d || fread(d, 1, len, f) != (size_t)len) { fprintf(stderr, "audio_play: read %s\n", path); fclose(f); return NULL; }
    fclose(f);
    if (len < 44 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) { fprintf(stderr, "audio_play: not a WAV file\n"); return NULL; }
    int fmt_ok = 0, ch = 0;
    for (long o = 12; o + 8 <= len;) {
        uint32_t sz = d[o + 4] | d[o + 5] << 8 | d[o + 6] << 16 | (uint32_t)d[o + 7] << 24;
        if (!memcmp(d + o, "fmt ", 4) && o + 8 + 16 <= len) {
            uint16_t tag = d[o + 8] | d[o + 9] << 8;
            ch = d[o + 10] | d[o + 11] << 8;
            uint32_t rate = d[o + 12] | d[o + 13] << 8 | d[o + 14] << 16 | (uint32_t)d[o + 15] << 24;
            uint16_t bits = d[o + 22] | d[o + 23] << 8;
            if (tag != 1 || bits != 16 || (ch != 1 && ch != 2) || rate != kRate) {
                fprintf(stderr, "audio_play: need 16-bit PCM, 1-2 channels, %d Hz (got tag %u, %u bit, %d ch, %u Hz)\n",
                    kRate, tag, bits, ch, rate);
                return NULL;
            }
            fmt_ok = 1;
        } else if (!memcmp(d + o, "data", 4) && fmt_ok) {
            if (o + 8 + (long)sz > len) sz = (uint32_t)(len - o - 8);
            *chans = ch;
            *frames = sz / (2 * ch);
            return (int16_t *)(d + o + 8);        // d is 8-aligned and chunks are even-sized
        }
        o += 8 + sz + (sz & 1);
    }
    fprintf(stderr, "audio_play: no data chunk\n");
    return NULL;
}

int main(int argc, char **argv)
{
    double freq = 440.0, seconds = 3.0, amp = 0.25;
    const char *wav = NULL;
    int c;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "-w")) wav = argv[i + 1];
        else if (!strcmp(argv[i], "-f")) freq = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "-s")) seconds = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "-a")) amp = atof(argv[i + 1]);
        else { fprintf(stderr, "usage: audio_play [-f hz] [-s seconds] [-a amplitude] | -w file.wav\n"); return 64; }
    }
    (void)c;
    if (amp < 0 || amp > 1 || freq < 20 || freq > 20000 || seconds <= 0) {
        fprintf(stderr, "audio_play: bad argument\n");
        return 64;
    }

    int16_t *pcm = NULL;
    uint32_t pcm_frames = 0;
    int pcm_ch = 0;
    if (wav) {
        pcm = load_wav(wav, &pcm_frames, &pcm_ch);
        if (!pcm) return 1;
        seconds = (double)pcm_frames / kRate;
        printf("audio_play: %s: %u frames, %d channel(s), %.2f s\n", wav, pcm_frames, pcm_ch, seconds);
    }

    io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching(kEngineClass));
    if (!svc) {
        fprintf(stderr, "audio_play: no %s (boot with audio=1, or pwm-sim=1 on QEMU)\n", kEngineClass);
        return 1;
    }
    io_connect_t conn = 0;
    kern_return_t kr = IOServiceOpen(svc, mach_task_self(), 0, &conn);
    if (kr) { fprintf(stderr, "audio_play: IOServiceOpen 0x%x\n", kr); return 1; }

    mach_vm_address_t saddr = 0;
    mach_vm_size_t ssize = 0;
    kr = IOConnectMapMemory64(conn, kStatusBuffer, mach_task_self(), &saddr, &ssize, kIOMapAnywhere);
    if (kr) { fprintf(stderr, "audio_play: map status 0x%x\n", kr); return 1; }
    volatile AudioEngineStatus *st = (volatile AudioEngineStatus *)(uintptr_t)saddr;
    printf("audio_play: status buffer v%u, %llu bytes\n", st->fVersion, (unsigned long long)ssize);

    const size_t dataBytes = kChunkFrames * kChannels * sizeof(float);
    AudioBufferDataDescriptor *desc = calloc(1, sizeof(*desc) + dataBytes);
    desc->fActualDataByteSize = (uint32_t)dataBytes;
    desc->fActualNumSampleFrames = kChunkFrames;
    // registerClientBuffer64 requires fTotalDataByteSize == the registered size,
    // header included (IOAudioEngineUserClient.cpp:1518-1531).
    desc->fTotalDataByteSize = (uint32_t)(sizeof(*desc) + dataBytes);
    desc->fNominalDataByteSize = (uint32_t)dataBytes;
    float *buf = (float *)(desc + 1);
    uint64_t in[4] = { 0, (uint64_t)(uintptr_t)desc, sizeof(*desc) + dataBytes, 1 };
    kr = IOConnectCallScalarMethod(conn, kCallRegister, in, 4, NULL, NULL);
    if (kr) { fprintf(stderr, "audio_play: registerClientBuffer 0x%x\n", kr); return 1; }

    kr = IOConnectCallScalarMethod(conn, kCallStart, NULL, 0, NULL, NULL);
    if (kr) { fprintf(stderr, "audio_play: start 0x%x\n", kr); return 1; }

    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    for (int i = 0; i < 500 && st->fCurrentLoopCount == 0 && st->fLastLoopTime == 0; i++) {
        usleep(2000);
    }
    if (st->fCurrentLoopCount == 0 && st->fLastLoopTime == 0) {
        fprintf(stderr, "audio_play: the engine produced no time stamp\n");
        return 1;
    }

    // Absolute frame number of the play position, from the last wrap time.
    #define PLAY_FRAME() ({ \
        uint64_t el = ticks_to_ns(mach_absolute_time() - st->fLastLoopTime, &tb); \
        (uint64_t)st->fCurrentLoopCount * kRingFrames + (el * kRate / 1000000000ULL); })

    uint64_t total_frames = (uint64_t)(seconds * kRate);
    uint64_t next = PLAY_FRAME() + kLeadFrames;
    const uint64_t first_frame = next;
    uint64_t end = next + total_frames + (pcm ? kChunkFrames : 0);   // let the last chunk (zero padded) play out
    double phase = 0.0, step = 2.0 * M_PI * freq / kRate;
    uint64_t chunks = 0, errors = 0;
    kern_return_t lastkr = 0;
    // One frame in mach ticks (the kernel multiplies this by a frame count).
    uint64_t interval = (1000000000ULL / kRate) * tb.denom / tb.numer;

    while (next < end) {
        for (int f = 0; f < kChunkFrames; f++) {
            if (pcm) {
                uint64_t idx = (next - first_frame) + f;
                if (idx < pcm_frames) {
                    float l = (float)(amp * pcm[idx * pcm_ch] / 32768.0);
                    float r = pcm_ch == 2 ? (float)(amp * pcm[idx * 2 + 1] / 32768.0) : l;
                    buf[f * 2] = l;
                    buf[f * 2 + 1] = r;
                } else {
                    buf[f * 2] = buf[f * 2 + 1] = 0.0f;
                }
                continue;
            }
            float v = (float)(amp * sin(phase));
            phase += step;
            if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
            buf[f * 2] = v;
            buf[f * 2 + 1] = v;
        }
        uint32_t first = (uint32_t)(next % kRingFrames);
        uint32_t loop = (uint32_t)(next / kRingFrames);
        kr = IOConnectTrap6(conn, kTrapPerformClientIO, first, loop, 0, 1,
            (uintptr_t)(interval >> 32), (uintptr_t)(interval & 0xffffffffu));
        if (kr) { errors++; lastkr = kr; }
        chunks++;
        next += kChunkFrames;
        // Sleep until the chunk before the one we just queued has played.
        while (PLAY_FRAME() + kLeadFrames < next) {
            usleep(1500);
        }
    }

    kr = IOConnectCallScalarMethod(conn, kCallStop, NULL, 0, NULL, NULL);
    uint64_t un[2] = { (uint64_t)(uintptr_t)desc, 1 };
    IOConnectCallScalarMethod(conn, kCallUnregister, un, 2, NULL, NULL);
    printf("audio_play: %s amplitude %.2f for %.1f s: %llu chunks, %llu errors (last 0x%x), stop 0x%x, loops %u\n",
        wav ? wav : "sine", amp, seconds, (unsigned long long)chunks, (unsigned long long)errors, lastkr, kr, st->fCurrentLoopCount);
    IOConnectUnmapMemory64(conn, kStatusBuffer, mach_task_self(), saddr);
    IOServiceClose(conn);
    IOObjectRelease(svc);
    // The first chunk or two can miss their watchdog deadline (kIOReturnIsoTooOld,
    // 0xe00002ee) while the engine starts; more than 2% of them is a real problem.
    return errors * 50 > chunks ? 2 : 0;
}
