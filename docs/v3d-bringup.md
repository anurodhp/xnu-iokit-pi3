# V3D bring-up checklist (first time on a real Pi 3)

Everything below `BCM2837V3D::start()`'s ASB check is unverified on hardware
(QEMU raspi3 has no V3D). This is the order to find out what is wrong with the
least confusion. Background: `docs/gpu-opengl-plan.md`; sources:
`iokit/BCM2837V3D/`.

## Before booting

1. Kernel built with the kext: `tools/build_xnu_7195.sh` (needs the V3D lines
   in `third_party/xnu-7195/iokit/conf/files`, see the plan doc).
2. Device tree: the real-hardware DTB (`devicetrees/rpi3-realhw.dtb`) is the
   one with the `v3d` node (`RPI3_HAVE_V3D`). Rebuild with `make -C devicetrees`.
3. SD image: `tools/init_binary/inject_into_sd_image.sh <image>` installs
   `/usr/X11/lib/libdrm.2.dylib` and `/usr/X11/bin/v3dprobe`
   (`tools/userland_staging/build_libdrm_v3d.sh`, `build_v3dprobe.sh`).
4. Serial console on (see the real-hardware notes in project memory:
   boot-args via `uEnv.txt`, never over the u-boot serial).

## Boot-time expectations (serial log)

| Line | Meaning |
|---|---|
| `Kext __kernel__ registered class BCM2837V3D` | driver compiled in |
| *(nothing from BCM2837V3D)* | the `v3d` node is missing from the DTB, or the personality did not match: check `BCM2837Personalities.cpp` and the DTB |
| `BCM2837V3D: no ASB bridge (id X) - no V3D on this machine` | the ASB block at ARM `0x3f00a000` did not read `0x62726467` ('brdg'). Wrong base address, or the PM/ASB `reg` entries in the DT node are off |
| `BCM2837V3D: power through the firmware (domain V3D)` | expected on a Pi: the firmware has the power-domain interface and powers V3D (firmware domain 11), as Linux's `raspberrypi-power.c` does |
| `BCM2837V3D: power through the PM block (...)` | no firmware domain interface (QEMU model, or very old firmware): the `bcm2835-power.c` sequence is used |
| `BCM2837V3D: firmware would not power the V3D domain` | `SET_DOMAIN_STATE {11, 1}` was not answered |
| `BCM2837V3D: GRAFX power never reported POWOK (PM_GRAFX ...)` | PM-block path only. On a real Pi 3 this is what the PM-block path does (`0x1000 -> 0x1001`): the firmware owns the domain |
| `BCM2837V3D: ASB master did not ack` / `ASB slave did not ack` / `... V3D clock` | PM-block path only |
| `BCM2837V3D: IDENT0 0x... , expected 0x02443356` | V3D window at ARM `0x3fc00000` reads something else: do not go further |
| `BCM2837V3D: V3D rev N, 3 slices, 12 QPUs, 6 TMUs, raw submit off` | success (VideoCore IV has 3 slices of 4 QPUs; the decode is `vc4_v3d.c`'s v3d_ident) |

A hang or abort right after the ASB check line and before any of the above
points at the first access to PM (`0x3f100000`) or the V3D window.

## First userland contact

```
/usr/X11/bin/v3dprobe -q     # idents only
/usr/X11/bin/v3dprobe        # + BO map round trip, validated shader, clear job
```

Expected: `V3D_IDENT0 0x2443356`, `ok:` lines, and finally `v3dprobe: PASS`.

* `cannot open BCM2837V3D (...)`: the service is not registered (driver
  declined, see the boot log).
* `FAIL: write-combined BO reads back what was written`: BO cache attributes
  or the `0xC0000000` bus alias are wrong.
* `FAIL: submit clear job`: validator rejected the job (`errno`); with no log
  output by design. Reproduce the call on the host: the render-only submit is
  the same one `tools/v3d_validate_test` builds.
* `FAIL: job completed within 2 s` then `note: the GPU hung`: interrupt not
  arriving (check the DT interrupt `<10>`) or CT1 never started. The hang
  detector resets after ~200 ms and the log prints `GPU hang, resetting`.
* pixels not cleared / wrong: render target format, or the BO was not
  written back (cache coherency): compare `px[0]` in the message.

## What to capture when something fails

* the whole serial log from boot;
* `DRM_IOCTL_VC4_GET_HANG_STATE` output (printed by `v3dprobe` after a hang);
* V3D registers: `CT0CS/CT1CS`, `CT0CA/CT1CA`, `ERRSTAT`, `DBGE`, `FDBGO`
  (offsets in `BCM2837V3DRegs.h`).

## Known unverified assumptions (from reading Linux, never run here)

1. V3D registers at bus `0x7ec00000` = ARM `0x3fc00000`; PM `0x3f100000`; ASB
   `0x3f00a000`; interrupt = VideoCore IRQ 74 = bank 1 bit 10.
2. Power-up sequence = `bcm2835_asb_power_on()` with the firmware V3D clock
   (clock id 5) pulsed for the reset. If the firmware already left V3D
   running this is a harmless repeat; if it did not, this sequence is what
   must work.
3. GPU bus address of RAM = ARM physical + `0xC0000000` (dma-ranges in
   bcm2837.dtsi) and BOs are write-combined with a `dsb` before kicking the
   GPU. If cached bus aliases (`0x40000000`) were needed the symptom is stale
   or missing pixels.
4. The binner overflow pool (4 MiB, rewound when idle) is enough for simple
   frames; `V3D_INT_OUTOMEM` is intentionally masked.
5. `V3D_L2CACTL`/`SLCACTL` clears before each job as Linux does.

## GL on the GPU (after `v3dprobe` passes)

`MESA_VC4=1 tools/userland_staging/build_mesa.sh` builds the libGL that
tries the V3D first (`mesa_xlib_vc4/xlib_vc4.c` replaces Mesa's `xlib.c`),
then falls back to softpipe when there is no V3D. Rebuild without
`MESA_VC4` for the software-only library. Install and run, on the Pi under
Xorg/Xvfb:

```
DISPLAY=:0 /usr/X11/bin/gl_smoketest :0
```

`GL_RENDERER` should read `VC4 V3D 2.1` (on QEMU: `softpipe`). `MESA_VC4=0`
forces software. Frames are copied from the GPU render target to the X
drawable on the CPU (`xlib_vc4_flush_frontbuffer`): correct but not fast.
Untested on hardware: the presenter, vc4's render-target format choice for
the X visual, and everything the GPU does with the jobs.

## QEMU model of the V3D (what runs without a Pi)

`tools/qemu_v3d_model/` is a test model of the V3D block for the QEMU fork
(see its README). With `QEMU_DTB=devicetrees/rpi3-v3dtest.dtb`:

* `tools/v3d_qemu_probe.sh`: the driver powers the block up (mailbox V3D clock,
  PM_GRAFX reset release, ASB handshake), reads IDENT, takes interrupts, and
  `v3dprobe` passes end to end including the clear job's pixels.
* `tools/v3d_qemu_hang_test.sh`: the model never completes the first render
  job; the driver logs `GPU hang, resetting`, power-cycles, fails that job,
  and a second `v3dprobe` passes on the recovered GPU (8/8 runs).

It is written from the same Linux sources as the driver, so a pass means the
plumbing and driver logic agree with those sources, not that the silicon does.
The model reports bus accesses the real chip would hang on (V3D registers
while unpowered/unclocked/behind a stopped bridge) with `-d guest_errors`.

### Found by the model: DC ZVA on write-combined memory

`bzero()` of a freshly allocated write-combined BO intermittently spun forever
under QEMU (PC parked in `_bzero`'s `dc zva` loop, 2 of 5 runs, caught with the
monitor). The driver now zeroes WC memory with plain stores (0 of 8 stall).
Whether real hardware is affected is unknown (DC ZVA to Normal non-cacheable
memory is architecturally allowed), but watch for the same signature in
userland: a process stuck in `memset`/`bzero` on a mapped BO. Mesa's vc4
uploads with memcpy/memset into mapped BOs.

## Real Pi 3 results (2026-10-05)

First hardware runs, Pi 3 B, stock `start.elf`, `gpu_mem=64`:

1. PM-block power-up (as first written, from `bcm2835-power.c`): GRAFX off at
   entry (`PM_GRAFX 0x1000`), POWUP latches, POWOK never rises at any inrush
   step, not with a 10 ms wait, not with the firmware's V3D clock on. Cause:
   on Pi boards the firmware owns the domain; every Pi DT routes the v3d
   node's power domain to `raspberrypi-power.c` (firmware
   `SET_DOMAIN_STATE`). Fixed: `a01c638`.
2. Firmware power: `V3D rev 1, 3 slices, 12 QPUs, 6 TMUs`; BO map, validated
   shader, job submit and completion all pass, but the clear job stored
   zeros. Cause: the vendored render-list code is Mesa's simulator copy,
   missing Linux 54aec44ab02b (clear colours must precede the render config
   on hardware). Fixed: `a01c638`.
3. With both fixes: `v3dprobe` PASS; under Xvfb `gl_smoketest` PASS on
   `VC4 V3D 2.1` (GL 2.1), `gles2_smoketest` PASS (ES 2.0),
   `epoxy_smoketest` PASS; `MESA_VC4=0` falls back to softpipe.

Not yet run on hardware: the hang recovery (firmware power off/on), Xorg on
HDMI with GL, anything heavier than the smoketests (glmark2, the GNUstep
stack), and performance.

## X11 and GNUstep on the GPU (real Pi 3, 2026-10-05)

- gnustep-back is built with GLX (`build_gnustep_back.sh`), so
  `NSOpenGLView`/`NSOpenGLContext` run on Mesa's libGL. Note: gnustep-back
  asserts if a view's GL context is created before the view is in a window
  (`XGGLContext.m:81`); AppKit catches the exception, which looks like a hang.
- `GLTest.app` (`build_gnustep_gltest.sh`): GSGLTEST PASS on `VC4 V3D 2.1`,
  180 fps for a 128x128 view (softpipe: 80 fps).
- `glxinfo`/`glxgears` (`build_mesa_demos.sh`, mesa-demos 9.0.0) in
  /usr/X11/bin: glxgears about 61 fps on the V3D, 8.4 fps on softpipe
  (300x300, Xvfb).
- Binner memory is one 16 MiB pool (4 MiB overflow + 24 x 512 KiB slots), as
  in Linux; a submit waits for a slot rather than failing. Without the wait,
  a third of glxgears' frames were rejected (shown as EINVAL: the validator
  turns any packet-handler failure into EINVAL).

Diagnostics:
- boot-arg `v3d-debug=1` (uEnv.txt `extra_bootargs`): the validators print
  why they reject a submission (`v3d validate: ...` on the console).
- `V3D_DRM_DEBUG=1` in an app's environment: the libdrm shim describes each
  rejected SUBMIT_CL on stderr.
- Mesa prints "Draw call returned Invalid argument" only once per process;
  count rejections with `V3D_DRM_DEBUG=1`, not from that message.

Not yet done: Xorg on HDMI with GL, MyGL (needs GLU), a faster present path
than the CPU copy.

## Rome, a GNUstep terminal on this stack (2026-10-06)

`Rome.app` (repo `anurodhp/rome`, pinned in `setup_third_party.sh`; built by
`build_libvterm.sh` + `build_rome.sh`; installs as `/Applications/Rome.app`,
Terminal.app is gone, Rome replaced it) draws its cells either with the CPU into an
MIT-SHM image (`x11`, the default) or through Mesa's libGL (`-RomeRenderer gl`,
the V3D with `MESA_VC4=1`, softpipe otherwise). Both redraw only the changed
cells and present only the changed box (`glXCopySubBufferMESA` for GL), and a
scroll is an `XCopyArea` in the X server. Design, the numbers and why x11 is
the default are in the repo's README.

Measured so far, QEMU only (softpipe, emulated CPU, so relative):
`seq 1 2000` takes 4.2 s in Rome x11 (80x24), 8.1 s (150x45), 7.7 s / 47.5 s in
Rome gl. Rome x11 on the real Pi, 80x24: 8.2 ms per
frame (draw 3.9, present 3.1).

Still to measure on the Pi (it was off): GL on the V3D against the CPU renderer
at 80x24 and 150x45 with `XLIB_VC4_STATS=1` and `ROME_STATS=1`; Rome against
the old Terminal.app numbers for scrolling, typing latency and full-screen `top`. The harness
is `tools/bench.sh` in the rome repo (`OUT=... R=... XPID=... DISPLAY=...`).
Expected from the present path above: the GL renderer's frame cost is bounded
by the whole-frame GPU read-back, so it should not beat x11 at normal sizes
until the driver can read back just the damaged box.

Test: `tools/rome_qemu_test.sh` (Xvfb under QEMU, both renderers).
