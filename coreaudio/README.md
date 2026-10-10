# coreaudio

A basic audio daemon (`audiod`) and a source-compatible subset of the CoreAudio
HAL API (`libaudiohal.dylib`) on top of the BCM2837 PWM audio engine. Design and
review notes: `PLAN.md` (read its "Revision after plan review" section; it
overrides the earlier text).

Apple's coreaudiod/CoreAudio are closed source, so this is project code written
against the public `CoreAudio/AudioHardware.h` (taken from the Xcode-12 MacOSX SDK
at build time, not copied here).

## Pieces
| Path | What |
|---|---|
| `daemon/audiod.c` | launchd on-demand job `org.puredarwin.coreaudiod`; mixes client streams (resample to 48 kHz, float sum, device volume, clip) and drives `IOAudioEngineUserClient` like `tools/userland_staging/audio_play.c` |
| `daemon/mixer.[ch]` | pure ring/resample/mix/clip math, host-tested |
| `lib/libaudiohal.c` | `/usr/lib/libaudiohal.dylib`: exports only `AudioObject{Has,IsSettable,GetSize,Get,Set}Property*` and `AudioDevice{CreateIOProcID,DestroyIOProcID,Start,Stop}` |
| `include/ca_protocol.h` | project-defined Mach message protocol (PCM inline, no shared memory) |
| `test/ca_play.c` | smoketest client via the public API |
| `test/mixer_test.c` | host test: `cc -I daemon -o /tmp/mixer_test test/mixer_test.c daemon/mixer.c -lm` |

Build (Xcode 12 + the `libc_build/system` dylibs): `build_libaudiohal.sh`,
`build_audiod.sh`, `build_ca_play.sh`; `tools/init_binary/inject_into_sd_image.sh`
installs `/usr/sbin/audiod`, the LaunchDaemons plist, `/usr/lib/libaudiohal.dylib`
(generic dylib loop) and `/usr/local/bin/ca_play`.

## Device model
Object 1 = system, 2 = the one output device ("Raspberry Pi 3.5mm Output",
48 kHz, interleaved Float32 stereo, IOProc buffer 512 frames), 3 = its output
stream. Volume and mute are output scope, master element, settable and
device-wide (held by the daemon).

## Limits (v1)
Output only; one device; the public API runs at 48 kHz (the daemon resamples any
8-192 kHz stream but only the private protocol can ask for one, so the resampler
is exercised by the host test, not end to end); no authentication (any local
process can use or close any stream id); client-death detection is a 3 s write
timeout, not dead-name notification; latency about 2560 (engine lead) + 2048
(queued) frames, roughly 100 ms; no AudioQueue/AudioUnit/AudioConverter.
The stock IOAudioEngine nominally mixes several user clients, but that path panics here (see "Kernel finding"), so the daemon's mixer is currently the only safe way to play two sounds at once.

## Verified (QEMU, `pwm-sim=1`, 2026-10-10)
- launchd starts `audiod` on the first `bootstrap_look_up` (MachServices, no RunAtLoad).
- `ca_play -v 0.8`: volume set/read-back through the public API; engine peak 6553/32767 = 0.25 x 0.8.
- Two concurrent `ca_play` (440 + 660 Hz): one engine session, peak 12820/32767 (sum of both, volume 0.8 kept by the daemon).
- `audiod` releases the engine and exits after 10 s idle.
- Host: `mixer_test` PASS; `bind_target_audit.sh` (all images) and `weak_def_audit.sh` (new images) clean.
- NOT verified: real Pi (by ear), IOProc timing (under TCG the IOProc thread ran at about half real time, so streams underrun
  on QEMU; frame counts at the engine are still right), client crash reaping, the daemon-exit-during-request retry path.

## Kernel finding (not fixed here)
Two concurrent `audio_play` processes (two IOAudioEngineUserClients on the same engine) PANIC the kernel on QEMU:
data abort in `IOAudioEngine::mixOutputSamples` called from `IOAudioStream::processOutputSamples`
(esr 0x96000007, far = one page past x28; pc/lr resolved with nm on the unstripped kernel). So the kernel-side
multi-client mix path is not usable today, and `audiod` (a single engine client that mixes in userland) is the safe way
to play more than one sound until that is fixed. Not investigated further.
