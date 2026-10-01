# SMP on the real Pi 3: experiment for the user (DAR-455)

**Status:** prepared 2026-10-01, **not yet tried**. Everything here was
verified under QEMU only (see `docs/smp-bringup.md`, "Phase 2 results").
The Pi has its own risks QEMU cannot reproduce: caches are real (QEMU
models none), the firmware's armstub may differ from the version read for
the design, and the four cores are real silicon.

## 1. Build the files (on the Mac, in this worktree)

```
tools/build_xnu_7195.sh PYTHON=true       # kernel from xnu fork branch dar-455-smp
make -C devicetrees                       # rpi3-realhw.dtb with cpu1-3
tools/package_smp_pi_experiment.sh        # -> build/smp-pi-experiment/
```

`build/smp-pi-experiment/` then holds:

| File | Goes to (BOOT, the FAT partition 1 of the card) | What it is |
|---|---|---|
| `kernel.development.bcm2837.unstripped` | `kernel.development.bcm2837.unstripped` (replace) | SMP-capable kernel. Built Oct 1 17:51, sha256 `e744adb8…cfd22a` in the 2026-10-01 package |
| `rpi3.dtb` | `rpi3.dtb` (replace) | `devicetrees/rpi3-realhw.dtb` with nodes cpu1-3 |
| `uEnv.txt` | `uEnv.txt` (edit or replace) | `extra_bootargs=fb_width=1280 fb_height=720 cpus=4` |
| `SHA256SUMS` | (not copied) | checksums of the three files |

u-boot loads exactly these names from `mmc 0:1`
(`third_party/u-boot/configs/rpi3_xnu_defconfig`, `CONFIG_BOOTCOMMAND`:
`uEnv.txt`, `kernel.development.bcm2837.unstripped`, `rpi3.dtb`) and
appends `${extra_bootargs}` to the boot-args. If your current `uEnv.txt`
has other settings, keep them and only add ` cpus=4` to the end of the
`extra_bootargs=` line.

The kernel is the Pi's current kernel plus DAR-455. The Pi now runs the
Sep 29 07:07 build; this one is xnu `bcm2837-bringup` c2383ed9e
(fb_width/fb_height, committed 07:15 the same morning) plus the
`dar-455-smp` commits.

## 2. Put them on the card (from the Mac only)

**Do not write the BOOT partition from the running Pi.** FAT writes from
XNU panic the kernel (DAR-457).

1. Shut the Pi down and move the card to the Mac.
2. It mounts as `BOOT` (and `ROOT`). Check with `diskutil list` that
   `BOOT` really is the card, not a disk image.
3. Back up the current files on the card:
   ```
   cd /Volumes/BOOT
   cp kernel.development.bcm2837.unstripped kernel.single.unstripped
   cp rpi3.dtb rpi3.single.dtb
   cp uEnv.txt uEnv.single.txt 2>/dev/null || true
   ```
4. Copy the new ones:
   ```
   cp <worktree>/build/smp-pi-experiment/kernel.development.bcm2837.unstripped .
   cp <worktree>/build/smp-pi-experiment/rpi3.dtb .
   # edit uEnv.txt: extra_bootargs=fb_width=1280 fb_height=720 cpus=4
   ```
5. Run `shasum -a 256` on the kernel and dtb on the card and compare with
   `SHA256SUMS`. Then eject (`diskutil eject`) and put the card back.

## 3. Roll back

Any of these, from the Mac:

- **Fastest:** remove ` cpus=4` from `uEnv.txt`. Without it the new
  kernel keeps only the boot CPU (`ml_parse_cpu_topology()` defaults to 1
  on BCM2837) and behaves like the old one.
- **Full:** copy `kernel.single.unstripped` back to
  `kernel.development.bcm2837.unstripped`, `rpi3.single.dtb` back to
  `rpi3.dtb`, and `uEnv.single.txt` back to `uEnv.txt`.

The old kernel with the new dtb would see four CPUs in the topology
without starting any, so roll the kernel and dtb back together.

## 4. What to look at

**Serial** (`/dev/cu.usbserial-0001`, 115200; `tools/realhw_serial_test.py`
or minicom). The kernel's IOLog/kprintf lines go here; they do not reach
`/var/log/system.log`. Look for, in order:

```
BCM2837CPU: started "cpu0" (boot CPU, 4 CPU(s) in topology)
BCM2837CPU: spin table page: stub xxxxxxxx xxxxxxxx ..., slots 0 0 0 0
BCM2837CPU: releasing core 1 at PA 0x...
arm_cpu_init(): cpu 1 online
BCM2837CPU: cpu1 (core 1) initCPU on core 1; <n> ticks after release, CNTFRQ 19200000
BCM2837CPU: cpu1 (core 1) is running
... same for cores 2 and 3 ...
BCM2837LocalIntc: started, ..., 4 core(s)
```

- The first stub word should be the armstub8's first instruction, not
  zero.
- The slots must read `0 0 0 0`.
- `<n> ticks after release` should be a small positive number (QEMU:
  50-175k ticks at 62.5 MHz; the Pi counts at 19.2 MHz).

**After login** (ssh or the console):

- `top -l 2 -n 0 -s 3 | grep CPU`: at an idle desktop, idle% should be
  well above 0.
  - For reference, on 2026-10-01 the single-core Pi at the xdm greeter
    showed `14% user, 79% sys, 7% idle`, load average 1.1.
- Load test: `for i in 1 2 3 4; do yes > /dev/null & done`, then
  `top -l 2 -n 8 -s 3`. All four `yes` should run (~100% each, idle near
  0%), and the desktop should still respond. Stop them with
  `kill %1 %2 %3 %4`.
- With one `yes`, idle should stay near 75%.
- Then the normal desktop session (xdm, wmaker, Terminal, Ink) for a
  while.

## 5. What failure looks like

| Symptom | Meaning | Action |
|---|---|---|
| `cpuN (core N) did NOT come up within 5 s; continuing without it` | The core never reached the kernel: armstub/spin table not as expected, the EL2 trampoline, or caches (the woken core read stale data with its caches off) | The machine keeps running on fewer cores. Send the serial log, especially the `spin table page` line |
| `startCPU: core N spin slot already ...` | The spin slot was non-zero: not the stub we know | Same |
| Hang right after `releasing core N` | The woken core corrupted shared state, e.g. it ran with a stale view of memory | Roll back; send the serial log |
| `panic(...)` | A real SMP bug (locking in a driver, pmap, ...) | Since DAR-334 a panic resets the board after printing on serial; with `cpus=4` still set it will loop. Roll back from the Mac; send the panic text |
| Boots, but the desktop misbehaves or apps crash | Concurrency bug in userland or a driver, newly exposed | Note what, roll back |

Expected QEMU-only differences: QEMU showed no speedup (its TLB-flush
emulation stops all vCPUs). That says nothing about the Pi.
