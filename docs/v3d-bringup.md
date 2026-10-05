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
| `BCM2837V3D: GRAFX power never reported POWOK` / `memory repair timed out` | firmware left GRAFX off and the manual power-up failed (`powerOn()`, from `bcm2835-power.c`) |
| `BCM2837V3D: firmware would not enable the V3D clock` | mailbox tag `0x00038001` (clock id 5) refused |
| `BCM2837V3D: ASB master did not ack` / `ASB slave did not ack` | bridge handshake failed; the V3D block is not reachable |
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
