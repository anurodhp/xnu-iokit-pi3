# Audio bring-up (3.5mm jack): boot-args, device trees, tests

Umbrella ticket DARW-310 (PCM audio out of the Pi 3's 3.5mm jack, PWM0/PWM1 on
GPIO40/41 fed by DMA); steps DARW-312 to DARW-320. HDMI audio is a separate
umbrella (DARW-311) and is not covered here.

## What exists

| Piece | Where | Ticket |
|---|---|---|
| IOAudioFamily-300.6.1 (fork `anurodhp/IOAudioFamily`, `iokit-port`), vendored into the kernel | `tools/vendor_ioaudio_into_xnu.sh`, `tools/ioaudio_gap_audit.sh` | 312 |
| `BCM2837DMA`, DMA controller (channels 0,2,4,5,8,9,10) | `iokit/BCM2837DMA/` | 313 |
| `BCM2837PWM`, PWM block as 2-channel FIFO+DMA sink; PWM clock programmed in the clock manager (plld_per / 2) | `iokit/BCM2837PWM/` | 314 |
| `BCM2837PWMAudioDevice/Engine`, the IOAudioFamily device | `iokit/BCM2837PWMAudio/` | 315 |
| DT nodes `dma`, `pwm`; personalities | `devicetrees/rpi3.dts`, `iokit/BCM2837ArmIO/BCM2837Personalities.cpp` | 316 |

## Boot-args

Audio is **on by default** (since 2026-10-10, after it was verified on a real
Pi). `audio=0` turns the DMA, PWM and audio-engine drivers off; then a boot never
touches the DMA controller, the PWM block or GPIO40/41. Until a client starts the
engine, a default boot only reads PWM/DMA registers and resets the DMA channels
it may use.

| Arg | Needs DTB | Effect | Touches hardware |
|---|---|---|---|
| `audio=0` | any | turns audio off (the default is on: BCM2837DMA, BCM2837PWM and the audio device start; the engine configures PWM and DMA only when a client starts it) | no, when 0 |
| `pwm-sim=1` | `rpi3-pwmtest.dtb` (its `pwm` node is marked `sim-only`, so without this arg the PWM driver declines) | PWM registers live in RAM, no clock/pin access, the DMA ring is replaced by a thread issuing the per-ring time stamp; starts the audio device | no |
| `audio-selftest=1` | with `pwm-sim=1` (QEMU), or on the Pi by default | conversion/volume/mute checks, then engine run 400 ms with time stamps; logs `selftest PASS/FAIL` | as above |
| `dma-test=1` | any (`rpi3.dtb` has the `dma` node) | starts BCM2837DMA and copies 4 KB memory to memory, expects the completion interrupt | DMA registers |
| `pwm-test=1` | `rpi3-realhw.dtb` only | starts BCM2837PWM and runs `configure(48000)`, logging the registers; **real hardware only**: on QEMU it reads an unmapped range and takes an external abort | yes |
| `audio-test=<s>` | with `pwm-sim=1` (QEMU), or on the Pi by default | the engine plays a fixed 375 Hz tone at -12 dB for `<s>` seconds (1 = 10 s) with no userland, logging once a second the DMA read position, ring loop count, PWM STA/CTL and DMA error interrupts (DARW-317) | as above |
| `pwm-ms=1` | with a running engine | mark-space PWM instead of the balanced algorithm, to compare by ear | PWM CTL |

QEMU maps no PWM block, so `rpi3-pwmtest.dtb` marks its `pwm` node `sim-only`
and the driver declines unless `pwm-sim=1` is given.

## Userland: `audio_play` (DARW-319)

`/usr/local/bin/audio_play` plays through `IOAudioEngineUserClient` the way the
CoreAudio HAL would (this port has no CoreAudio):

```
audio_play [-f hz] [-s seconds] [-a amplitude]      # sine, default 440 Hz, 3 s, 0.25
audio_play -w file.wav [-a amplitude]               # 16-bit PCM, mono/stereo, 48000 Hz
```

Build: `tools/userland_staging/build_audio_play.sh` (ad-hoc signed with
`com.apple.private.audio.driver-host`, which the user client demands of every
caller); `inject_into_sd_image.sh` installs it. Needs the engine, i.e. the default
on the Pi or `pwm-sim=1` on QEMU. On QEMU the evidence is the kernel line
`BCM2837PWMAudio: stop: N frames through clipOutputSamples, peak P/32767`.
Protocol notes (all from `third_party/IOAudioFamily`): the registered client
buffer starts with a 16-byte `IOAudioBufferDataDescriptor` whose
`fTotalDataByteSize` must equal the registered size; the play position comes
from the mapped status buffer (loop count + `fLastLoopTime`); the trap
`performClientIO` takes the sample interval in mach ticks per frame; calls must
lead the play position (the tool uses 2560 frames of the 4096-frame ring), or
`kIOReturnIsoTooOld` (0xe00002ee) is returned for chunks that miss their
watchdog deadline.

**Two engine clients (DARW-322, fixed):** the engine's multi-client mix ran its float add with preemption disabled
and faulted on a not-yet-touched page of the client mapping (vm_fault fails with preemption disabled,
`osfmk/vm/vm_fault.c:3983`), panicking the kernel. `IOAudioEngineMixer.cpp` now touches the source and mix pages first.
Also fixed: `BCM2837PWMAudioEngine::eraseOutputSamples` now clears the float mix buffer too (it accumulated across ring loops with
several clients; two 0.25 sines peaked at 32767, now 23533 on QEMU vs 16383 ideal, likely erase-head lag under TCG).

**launchd jobs need ProcessType (found on the Pi, 2026-10-10):** a job with no `ProcessType` runs as
`DAEMON_BACKGROUND` here (throttled, background timer coalescing: a 1.5 ms sleep took up to 50 ms). Anything audio from a launchd job
(`org.puredarwin.coreaudiod.plist`) must set `<key>ProcessType</key><string>Interactive</string>`. Details and numbers in
`coreaudio/README.md`.

## Running the QEMU checks

```
# DMA
EXTRA_BOOT_ARGS="dma-test=1" tools/boot_test.py --expect 'BCM2837DMA: self-test'
# audio engine, simulated
QEMU_DTB=devicetrees/rpi3-pwmtest.dtb EXTRA_BOOT_ARGS="pwm-sim=1 audio-selftest=1" \
    tools/boot_test.py --expect 'PWMAudio: selftest (PASS|FAIL)'
# userland tool through the user client (after inject_into_sd_image.sh)
QEMU_DTB=devicetrees/rpi3-pwmtest.dtb EXTRA_BOOT_ARGS="pwm-sim=1" \
    tools/boot_test.py --login --cmd 'audio_play -f 440 -s 3' --expect 'PWMAudio: stop'
# host-side arithmetic
cc -I iokit/BCM2837PWM -o /tmp/pwm_math_test tools/tests/pwm_math_test.c && /tmp/pwm_math_test
```

On the Pi, `uEnv.txt` `extra_bootargs=audio-selftest=1` (see
`tools/make_realhw_sd_image.sh`).

## Status on real hardware (2026-10-10)

Verified on a real Pi 3: the kernel tone (`audio-test`) is audible and clear,
and `audio_play` plays from userland. Audio is on by default in the kernel (`audio=0` disables it). Facts that came out of the hardware runs:

- **DMA interrupt acknowledge** must write `INT | ACTIVE | config` (Linux
  `drivers/dma/bcm2835-dma.c:792-808`). Writing INT alone writes 0 to ACTIVE,
  which pauses the channel, and stopped the audio ring after its first
  interrupt.
- **PWM clock**: the VideoCore mailbox "set clock rate" for clock id 10 is
  acknowledged but has no effect. The driver programs `CM_PWMCTL/CM_PWMDIV`
  directly (field layout from Linux `drivers/clk/bcm/clk-bcm2835.c`, sequence
  from Circle `lib/gpioclock.cpp`), source plld_per, divisor 2.
- **The clock runs at 5/8 of nominal**: with plld_per assumed 500 MHz (as Circle
  does for Pi 1-3) the measured frame rate was exactly 5/8 of expected at
  divisors 2, 5 and 10, i.e. plld_per is about 312.5 MHz on this board and
  firmware. The engine therefore times the first ring wraps after the first
  start and trims RANGE (5208 -> 3255 at 48 kHz, about 11.7 bits), cached per
  rate for the boot. Reading the A2W PLLD registers or the CM frequency counter
  (Linux `bcm2835_measure_tcnt_mux`) would give the source rate directly; not
  done yet.

## Facts that are not verified in this tree

The BCM2835 ARM Peripherals datasheet is not vendored. PWM register bits and
DMA PERMAP 5 are from it as remembered (they match Circle's
`pwmsoundbasedevice.cpp` and work on hardware). The clock-manager fields and the
DMA CS bit classes are now confirmed from Linux's drivers (cited in the code).
Cross-checked in the tree: register block sizes, GPIO40/41 ALT0, interrupt
numbers, the DMA channel mask, firmware clock id 10 (u-boot `mach/mbox.h:228-238`).
Unknown: why plld_per is 5/8 of nominal, and whether the firmware ever
reprograms `CM_PWM` (its own analog audio is not used here).

## Gotchas found

- `IOAudioEngine::performFlush()` (erase timer) stops a running engine with no
  active user client; the self-test registers a stand-in client.
- Kernel FP registers are not saved across a context switch
  (`osfmk/arm64/cswitch.s:87-101`); the clip loop and the float mixer run with
  preemption disabled.
- In FIFO mode PWM `DAT` is unused and the idle level is low, so starting and
  stopping step the line; the engine does not ramp yet.
