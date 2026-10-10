# Audio bring-up (3.5mm jack): boot-args, device trees, tests

Umbrella ticket DARW-310 (PCM audio out of the Pi 3's 3.5mm jack, PWM0/PWM1 on
GPIO40/41 fed by DMA); steps DARW-312 to DARW-320. HDMI audio is a separate
umbrella (DARW-311) and is not covered here.

## What exists

| Piece | Where | Ticket |
|---|---|---|
| IOAudioFamily-300.6.1 (fork `anurodhp/IOAudioFamily`, `iokit-port`), vendored into the kernel | `tools/vendor_ioaudio_into_xnu.sh`, `tools/ioaudio_gap_audit.sh` | 312 |
| `BCM2837DMA`, DMA controller (channels 0,2,4,5,8,9,10) | `iokit/BCM2837DMA/` | 313 |
| `BCM2837PWM`, PWM block as 2-channel FIFO+DMA sink; firmware PWM clock via mailbox | `iokit/BCM2837PWM/` | 314 |
| `BCM2837PWMAudioDevice/Engine`, the IOAudioFamily device | `iokit/BCM2837PWMAudio/` | 315 |
| DT nodes `dma`, `pwm`; personalities | `devicetrees/rpi3.dts`, `iokit/BCM2837ArmIO/BCM2837Personalities.cpp` | 316 |

## Boot-args

Everything is **off by default**: with none of these args a boot never touches
the DMA controller, the PWM block or GPIO40/41.

| Arg | Needs DTB | Effect | Touches hardware |
|---|---|---|---|
| `audio=1` | `rpi3-realhw.dtb` (has `pwm` and `dma` nodes) | starts BCM2837DMA, BCM2837PWM and the audio device; the engine configures PWM and DMA only when a client starts it | yes (real Pi only) |
| `pwm-sim=1` | `rpi3-pwmtest.dtb` | PWM registers live in RAM, no clock/pin/mailbox access, the DMA ring is replaced by a thread issuing the per-ring time stamp; starts the audio device | no |
| `audio-selftest=1` | with `pwm-sim=1` (QEMU) or `audio=1` (Pi) | conversion/volume/mute checks, then engine run 400 ms with time stamps; logs `selftest PASS/FAIL` | as above |
| `dma-test=1` | any (`rpi3.dtb` has the `dma` node) | starts BCM2837DMA and copies 4 KB memory to memory, expects the completion interrupt | DMA registers |
| `pwm-test=1` | `rpi3-realhw.dtb` only | starts BCM2837PWM and runs `configure(48000)`, logging the registers; **real hardware only**: on QEMU it reads an unmapped range and takes an external abort | yes |
| `pwm-ms=1` | with a running engine | mark-space PWM instead of the balanced algorithm, to compare by ear | PWM CTL |

`rpi3-pwmtest.dtb` without `pwm-sim=1` and with `audio=1` or `pwm-test=1`
aborts on QEMU for the same reason: QEMU maps no PWM block (DARW-318 adds a
model).

## Running the QEMU checks

```
# DMA
EXTRA_BOOT_ARGS="dma-test=1" tools/boot_test.py --expect 'BCM2837DMA: self-test'
# audio engine, simulated
QEMU_DTB=devicetrees/rpi3-pwmtest.dtb EXTRA_BOOT_ARGS="pwm-sim=1 audio-selftest=1" \
    tools/boot_test.py --expect 'PWMAudio: selftest (PASS|FAIL)'
# host-side arithmetic
cc -I iokit/BCM2837PWM -o /tmp/pwm_math_test tools/tests/pwm_math_test.c && /tmp/pwm_math_test
```

On the Pi, `uEnv.txt` `extra_bootargs=audio=1 audio-selftest=1` (see
`tools/make_realhw_sd_image.sh`).

## Facts that are not verified in this tree

The BCM2835 ARM Peripherals datasheet and Linux's bcm2835 drivers are not
vendored. PWM register/bit positions, DMA TI/CS bits and PERMAP 5 are from the
datasheet as remembered (the reviewer agreed from their own knowledge, not from
the tree), the set-clock-rate mailbox message layout likewise. Cross-checked in
the tree: register block sizes, GPIO40/41 ALT0, interrupt numbers, the DMA
channel mask, firmware clock id 10 (u-boot `mach/mbox.h:228-238`). The first run
on a Pi (DARW-317 / DARW-320) is the real check.

## Gotchas found

- `IOAudioEngine::performFlush()` (erase timer) stops a running engine with no
  active user client; the self-test registers a stand-in client.
- Kernel FP registers are not saved across a context switch
  (`osfmk/arm64/cswitch.s:87-101`); the clip loop and the float mixer run with
  preemption disabled.
- In FIFO mode PWM `DAT` is unused and the idle level is low, so starting and
  stopping step the line; the engine does not ramp yet.
