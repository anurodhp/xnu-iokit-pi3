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

## Limits
Output only; one device (48 kHz stereo); no AudioQueue/AudioUnit/AudioConverter; no property listeners
(`AudioObjectAddPropertyListener` is deliberately not exported, so a port fails at link time instead of waiting for a
callback that never comes). The stream's virtual format is settable (Float32 interleaved, 1 or 2 channels, 8-192 kHz);
audiod converts it to the device format with linear interpolation. Stream ids are protected by a per-stream random token
(anyone who can reach the Mach service can still open streams and change the device-wide volume; there is no per-user
policy). A client that dies is detected by a 3 s write timeout, not by a dead-name notification. Latency is about 2560
(engine lead) + 2048 (queued) frames, roughly 100 ms. Property reads cost an RPC (and a daemon start when it is not
running).

## Verified (QEMU, `pwm-sim=1`, 2026-10-10)
- launchd starts `audiod` on the first `bootstrap_look_up` (MachServices, no RunAtLoad); it exits after 10 s idle.
- `ca_play -v 0.8`: volume set/read-back through the public API; engine peak 6553/32767 = 0.25 x 0.8.
- Two concurrent `ca_play` (440 + 660 Hz): one engine session, peak about the sum of both.
- `ca_play -r 44100 -c 1`: virtual format set through the public API; 3 s of 44.1 kHz mono plays as 3.4 s at the engine
  with peak 8191, so the resampler works end to end.
- Closing a stream drains it: a 1 s tone ends with its tail (1.3 s through the engine, peak as set).
- `ca_proto_test`: wrong token/id, bad sizes/counts/rates/channels/volume/version, ops after close, the stream table limit.
- A client killed with `kill -9` is reaped (`reaping stale stream`); `killall audiod` during playback: launchd restarts
  the daemon, the client reopens its stream and finishes (`ca_play` exit 0).
- Host: `mixer_test` PASS; `bind_target_audit.sh` (all images) and `weak_def_audit.sh` (new images) clean.
- NOT verified: real Pi (by ear), IOProc timing (under TCG the IOProc thread ran at about half real time, so streams
  underrun on QEMU; frame counts at the engine are still right).

## Kernel finding (DARW-322, fixed)
Two concurrent engine clients (two `audio_play`, or `audio_play` + anything) used to PANIC the kernel on QEMU: data abort in
`IOAudioEngine::mixOutputSamples` (from `IOAudioStream::processOutputSamples`), far = sourceBuf + 0xff0. Root cause: the
multi-client add loop ran with preemption disabled (added for the kernel-FP rule) and the client's wired source mapping had
only its first page touched (by registration); the first read of the second page faulted, and `vm_fault()` returns
KERN_FAILURE when `get_preemption_level() != 0` (osfmk/vm/vm_fault.c:3983), which the abort handler turns into the panic.
Single-client mixing is a bcopy with preemption enabled, so it faulted the page in without trouble. Fix (fork
anurodhp/IOAudioFamily, IOAudioEngineMixer.cpp): touch every page of the source and mix ranges before
`_disable_preemption()`. Verified on QEMU with the original 512-frame chunks: two `audio_play` and two `ca_play` run clean.
Second bug, also fixed: `BCM2837PWMAudioEngine::eraseOutputSamples` did not clear the float mix buffer (the family's version does), so with
several clients the mixer's add accumulated every ring loop and two 0.25 sines peaked at 32767. Now cleared; one client peaks at
8191, two at about 23600 on QEMU, still above the in-phase sum (16383). Diagnostics added to the engine's stop line
(`erased N frames in M calls`) show the erase head is NOT lagging (erased 162132 frames for 152706 clipped), so the surplus
(about one extra 0.25 contribution at some frames) looks like a chunk being mixed twice around a client resync under QEMU's slow
time stamps; undecided, and worth checking on a real Pi.
