# coreaudio: a basic audio daemon and CoreAudio-subset client library

Status: PLAN (reviewed before implementation). Ticket: none yet.

## Goal
Let more than one process play sound at once, and give ported software a
source-compatible CoreAudio output API, on top of the existing
`BCM2837PWMAudioEngine` (`docs/audio-bringup.md`). Output only, one device.

Not goals: input, multiple devices, AudioUnit/AudioQueue/AudioConverter,
ABI compatibility with Apple's frameworks, HDMI audio (DARW-311), HAL plug-ins.

## Source basis (all public)
Apple's coreaudiod, CoreAudio.framework and the HAL are closed source and are
not in apple-oss-distributions (checked against `docs/apple-oss-catalog.md`; no
CoreAudio/coreaudiod repo). So this is custom code, as the source-priority rule
allows only as a last resort. It is written against Apple's PUBLIC headers
only: `CoreAudio/AudioHardware.h`, `AudioHardwareBase.h`, `CoreAudioTypes.h`
from the Xcode-12 MacOSX SDK (`Platforms/MacOSX.platform/.../CoreAudio.framework/
Headers`), used at compile time through `-I`, never copied into the repo. The
header comments are the behavioural spec for each property and call. Where the
public docs are silent, the behaviour is chosen here and documented as a
choice in the code, not claimed to be Apple's.

The daemon side re-uses `tools/userland_staging/audio_play.c`'s verified
engine protocol (IOAudioFamily user client, entitlement
`com.apple.private.audio.driver-host`, status-buffer play position, 2560 frame
lead, `kIOReturnIsoTooOld` handling).

## Layout (everything new is under `coreaudio/`)
```
coreaudio/
  PLAN.md
  README.md                  what exists, how to build/run, limits
  include/ca_protocol.h      private wire protocol daemon<->lib
  daemon/audiod.c            the daemon (coreaudiod stand-in)
  daemon/mixer.[ch]          pure functions: resample, mix, clip, ring (host-testable)
  lib/libaudiohal.c          the CoreAudio subset
  test/mixer_test.c          host test of mixer.c (cc on macOS)
  test/ca_play.c             client: sine/WAV via AudioDeviceCreateIOProcID
  build_audiod.sh  build_libaudiohal.sh  build_ca_play.sh
  org.puredarwin.coreaudiod.plist
```
Build scripts live next to the sources (they follow the shape of
`tools/userland_staging/build_audio_play.sh`: Xcode 12 iPhoneOS SDK, arm64-
apple-ios14.4, fixup chains, `bind_target_audit.sh` as a hard gate, ad-hoc
sign). `tools/init_binary/inject_into_sd_image.sh` gets copy-loop entries.
The daemon and `ca_play` are dynamic; the library is a real dylib
(`/usr/lib/libaudiohal.dylib`, install name matches), per the dylib-first rule.
Nothing is static.

## Architecture

```
 client process                                 audiod (launchd job, root)
 ca_play / app                                  
  AudioDeviceStart --------- Mach msg --------> service thread (mach_msg recv)
  IOProc thread  ---- float frames (shared ---> per-stream ring (shared memory)
                       memory ring, lock-free)       |
                                                mixer thread: resample -> sum -> volume -> clip
                                                     |  performClientIO (IOAudioEngineUserClient)
                                                BCM2837PWMAudioEngine (kernel)
```

### Rendezvous
launchd plist `MachServices` entry `org.puredarwin.coreaudiod`; the daemon
calls `bootstrap_check_in` (exported by `libxpc.dylib` here), clients call
`bootstrap_look_up`. Job is on-demand (no RunAtLoad): the first client's
look-up starts it. KeepAlive false; the daemon exits after 10 s with no
streams so the engine is released (the engine stops when its last user client
closes).

### Protocol (`ca_protocol.h`), hand-written Mach messages, no MIG
Simple-message request/reply with `mach_msg`. Versioned header field.
- `OPEN_STREAM  {rate_hz, channels(1|2), frames_capacity}` -> reply carries a
  `mach_port_t` memory entry (port descriptor, MOVE) for a shared region and
  a stream id. Client maps it with `mach_vm_map`; the daemon maps the same
  entry.
- `START {id}`, `STOP {id}`, `CLOSE {id}`, `SET_VOLUME {id|device, float}`,
  `GET_INFO` (device name, rate, volume, latency frames). Replies carry
  `kern_return_t`.
Streams die with their client: the daemon requests a dead-name notification on
a per-stream client send-right (the client sends a port in OPEN_STREAM) and
reaps the stream.
Clients are not authenticated beyond Mach port reachability in v1 (single
root-owned service; every local process may play). Documented limit.

### Shared region layout
One page-multiple region: 64-byte header `{u32 version; u32 rate; u32 chans;
u32 capacity_frames; volatile u64 write_pos; volatile u64 read_pos; u32
underruns; u32 overruns}` then `float` frames, interleaved. Single producer
(client IOProc thread) and single consumer (mixer thread); positions are
monotonically increasing frame counts, indexed modulo capacity, published with
acquire/release (C11 atomics). No locks in the audio path. The client does not
write past `read_pos + capacity`; the mixer reads only `[read_pos,
write_pos)`; an empty ring contributes silence and bumps `underruns`.

### Mixer thread (daemon)
Copies `audio_play.c`'s loop: wait for the engine time stamp, derive the play
position from `fLastLoopTime`, aim `kLeadFrames` ahead, resync when behind,
call the trap per 512-frame chunk. Per chunk: for each RUNNING stream pull
`n * rate/48000` source frames from its ring, linear-resample to 48 kHz (state
kept per stream), accumulate into a float mix, apply device volume and
per-stream gain, clamp to [-1, 1], write into the registered buffer. Engine
is opened lazily on the first START and kept open until the idle timeout.
`mixer.c` holds resample/mix/clip/ring-pointer math with no I/O so it is unit
tested on the host.

Latency: client ring 2048 frames at stream rate plus the 2560 frame engine
lead, about 100 ms. Documented, not tuned.

### The library: CoreAudio subset (`libaudiohal.dylib`)
Source-compatible with `AudioHardware.h` for exactly these entry points;
everything else is not exported (so a link error, not a silent no-op, tells a
porter what is missing):
- `AudioObjectGetPropertyData / GetPropertyDataSize / HasProperty /
  SetPropertyData / IsPropertySettable` for:
  - `kAudioObjectSystemObject`: `kAudioHardwarePropertyDevices`,
    `kAudioHardwarePropertyDefaultOutputDevice`,
    `kAudioHardwarePropertyDefaultSystemOutputDevice`.
  - the one output device (`AudioObjectID` 2): `kAudioObjectPropertyName`,
    `kAudioDevicePropertyNominalSampleRate` (48000),
    `kAudioDevicePropertyStreams` (1 output stream), `kAudioDevicePropertyStreamConfiguration`,
    `kAudioDevicePropertyDeviceIsAlive`, `kAudioDevicePropertyBufferFrameSize`,
    `kAudioDevicePropertyLatency`, `kAudioDevicePropertyVolumeScalar`
    (master, settable) and `kAudioDevicePropertyMute` (settable).
  - the stream (id 3): `kAudioStreamPropertyVirtualFormat` /
    `PhysicalFormat` (Float32, 48 kHz, 2 ch, packed, interleaved).
  Unknown property -> `kAudioHardwareUnknownPropertyError`.
- `AudioDeviceCreateIOProcID`, `AudioDeviceDestroyIOProcID`, `AudioDeviceStart`,
  `AudioDeviceStop`. `AudioDeviceIOProc` is called from a dedicated pthread with
  `outOutputData` an `AudioBufferList` of one interleaved Float32 stereo buffer of
  `kAudioDevicePropertyBufferFrameSize` (default 512) frames and an
  `AudioTimeStamp` whose `mSampleTime`/`mHostTime` advance. The thread
  fills the ring, then sleeps until the ring has room (condition: poll the shared
  `read_pos` with `nanosleep` of a quarter buffer; v1 has no wakeup port).
- Sample rate: v1 devices are 48 kHz; the daemon resampler exists and is unit
  tested for 44.1/22.05/etc, but it is only reachable by the private
  `OPEN_STREAM` rate field (used by `ca_play -r`), not by the public API.

### Error handling and edge cases
Daemon unreachable -> `kAudioHardwareNotRunningError` from every call; engine
missing (no `audio=1`) -> daemon exits non-zero with a message to its log, clients get
`kAudioHardwareNotRunningError` after a bounded retry. Bad/unknown messages
(wrong size, version, id) are rejected, never trusted: the region header is
validated by the daemon from its own bookkeeping, not from the shared page,
because the client can scribble on that page. A client that corrupts its own
`write_pos` can only cause its own garbage/silence (the daemon clamps
`write_pos - read_pos <= capacity`).

## Verification
1. Host: `cc test/mixer_test.c daemon/mixer.c` (resample ratio, sum, clip, ring
   wrap, underrun/overrun, volume).
2. Build scripts: `bind_target_audit.sh` per image, and with no arguments after
   adding the dylib.
3. QEMU (serialized, one boot): `pwm-sim=1`, two `ca_play` processes at once (440 Hz and 660 Hz)
   plus the kernel line `BCM2837PWMAudio: stop: N frames through
   clipOutputSamples, peak P/32767` with N covering both and a peak above the
   single-tone peak (shows both mixed).
4. Real Pi: same two tones, by ear, user-run; not claimed until done.
5. Adversarial review of the implementation before commit (project rule).

## Risks / open questions the reviewer should attack
- Memory entry port transfer: `mach_make_memory_entry_64` + `mach_vm_map` in
  this port's userland (exports present in libsystem_kernel; behaviour on this
  XNU untested).
- Dead-name notification / client death reaping with raw Mach messages.
- `bootstrap_check_in` from libxpc.dylib with a PID-1 launchd-842: DAR-270 hints
  at check_in failures for some jobs.
- Float math/threads in a launchd daemon here (pthread works; `nanosleep`
  accuracy under QEMU).
- ld64 alias/weak-def hazards for the new dylib (run `weak_def_audit.sh`).

---
## Revision after plan review (these OVERRIDE the sections above)

1. **Premise corrected.** The kernel engine is NOT single-client:
   `IOAudioEngine.cpp:1316-1342` counts active user clients and the stock
   IOAudioFamily mixes several clients' float chunks (`audio_play.c:35-38`). An
   earlier claim that "only one process can drive the engine" was wrong. What the
   daemon still adds: the CoreAudio API (the real gap), arbitrary client rates
   (kernel path is 48 kHz float stereo only), a device-wide volume/mute, one
   place for the ring-lead/entitlement/timestamp details, and Apple's
   architecture shape. A userland mixer is therefore a convenience, not a
   necessity; step 0 below measures whether two `audio_play` processes mix.
2. **No shared memory in v1.** `mach_make_memory_entry_64` is exported only by
   `libdispatch.dylib` here (not libsystem_kernel), and a client that unmaps or
   truncates shared memory can fault the daemon. Instead PCM travels INLINE in
   `WRITE` messages (<= 16 KB, copied by the kernel). The daemon owns a private
   per-stream ring guarded by a mutex; no client-writable memory is ever read.
   This also removes the lock-free/read_pos-rewind issues.
3. **Protocol** (all `mach_msg` with `msgh_local_port` reply port):
   `OPEN{rate,channels}`->`{id}`, `WRITE{id,frames,data}`->`{accepted,free_frames}`
   (partial accept = backpressure; client sleeps ~ a quarter buffer), `START`,
   `STOP`, `CLOSE`, `SET_VOLUME{scalar,mute}`, `GET_INFO`->`{rate,volume,mute,
   name}`. Caps validated by the daemon: <= 16 streams total, rate 8000..192000,
   WRITE <= 16 KB, size == header-declared size; anything else -> `KERN_INVALID_ARGUMENT`.
4. **Client death:** no port set and no dead-name notification in v1. A stream
   that has not received a WRITE for 3 s is reaped; explicit `CLOSE` is the
   normal path. Stream lifetime is mutex-protected, so the mixer can never touch
   a freed stream. Documented limit: ids are guessable, any local process can
   write/close another's stream (no authentication in v1).
5. **Resampler phase:** per-stream fractional source position carried across chunks
   (double); underrun resets nothing but emits silence for the missing part and
   counts it; the rate is the engine's nominal 48000, play position taken from
   `fLastLoopTime` as `audio_play` does (calibration is invisible to userland).
6. **Engine lifecycle:** as `audio_play`: wait up to 1 s for the first time stamp
   after `kCallStart`; keep the user client open while any stream exists; on idle
   (no streams for 10 s) stop, unregister, close and EXIT 0; SIGTERM/SIGINT do the
   same. Clients retry `bootstrap_look_up` (up to ~3 s) so an exiting daemon is
   restarted by launchd on demand.
7. **launchd:** plist declares `MachServices`; precedent is configd/notifyd
   check-in (`build_notifyd.sh` header). DAR-270's true lesson is that a second
   `bootstrap_check_in` returns `BOOTSTRAP_SERVICE_ACTIVE`; the daemon checks in
   once. Whether launchd starts a job on first look-up of an *unloaded* service is
   exactly what the smoketest verifies; if it does not, the plist gets `RunAtLoad`.
   `inject_into_sd_image.sh` entries for daemon, dylib, `ca_play` and the plist
   are part of the work.
8. **Headers:** compile against the MacOSX SDK CoreAudio headers (`-I` a
   build-dir copy with a `CoreAudio/` prefix, made by the build script, not
   checked in) plus `-I third_party/xnu-7195/libkern` for `libkern/OSTypes.h`.
   Volume/mute are scope-qualified: output scope (`'outp'`) element 0 (master);
   a request with another scope/element returns
   `kAudioHardwareUnknownPropertyError`.
9. **Pass criteria:** host mixer test passes; on QEMU two concurrent `ca_play`
   (440/660 Hz, one at 44100 Hz) give `stop:` frames >= 2 x playtime and peak
   > single-tone peak; measured latency is reported by `ca_play` (first-IOProc to
   daemon-accepted) but not gated. Real-Pi by-ear check is the user's.
10. **Step 0:** `audio_play -f 440 & audio_play -f 660` on `pwm-sim=1`; record
    whether the kernel mixes. Its result goes in README.md, not into a design change.
