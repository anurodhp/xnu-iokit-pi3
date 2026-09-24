# Real-hardware sanity-check SD card (u-boot -> XNU, rpi3)

Everything in `sdcard/` (this doc's parent folder; not `sdcard/docs/`, which only holds these notes) goes on a single **FAT32** partition, copied
directly to its root (no subfolders). **CONFIRMED on real Pi 3 hardware,
2026-08-21: the full chain from GPU firmware through u-boot through
`bootxnu`'s Mach-O load/entry-point fix through XNU's own real kernel
boot, with real serial console output.** `## Starting XNU at ...` appeared
on u-boot's HDMI console, and the real kernel's own boot banner (BSD
copyright text, `kprintf` output over the real `pi3_uart` mini-UART
driver) appeared cleanly over a real USB-serial adapter on GPIO14/15 --
115200 8N1, **with `core_freq=250` pinned in `config.txt`** (without it,
the real symptom was garbled-but-present serial output, not blank --
real firmware clock scaling shifting the mini-UART's actual baud away
from the nominal 115200 that `pe_serial.c`'s fixed divisor assumes; see
`config.txt`'s own comment for the full mechanism). Two real, separate
first-time confirmations from one boot: `bootxnu`'s Mach-O loader/
entry-point fix (see the `u-boot.bin` bullet below) genuinely works on
real silicon, not just in the QEMU-derived reasoning that motivated it;
and XNU's real serial console path works on real hardware for the first
time in this project's history.

**Two follow-up questions from the first test, both resolved same-day,
both expected/by-design, neither a bug:**
- **HDMI goes dark after `## Starting XNU`, no kernel console output
  ever appears there.** NOT a framebuffer-handoff failure (that remains
  genuinely untested either way) -- root cause is in `osfmk/console/
  serial_console.c` (lines ~139-156), which hardcodes
  `cons_ops_index = SERIAL_CONS_OPS` for every non-x86_64 target, a
  deliberate scope decision from QEMU bring-up (QEMU runs
  `-display none`, so video console output would've been invisible
  there). XNU's `kprintf`/`/dev/console` path never targets video on
  this board today, independent of whether the handoff itself works.
- **Boot reaches real IOKit matching (`_IOConfigThread::main entered`)
  then hangs "Waiting for root device" (`IOMedia`/`Apple_HFS`).**
  Expected: this card is a single FAT32 partition with no HFS+ root
  partition at all, and the kernel snapshot here predates this
  project's real SD/HFS+ driver work entirely -- no root device was
  ever going to appear. This is this sanity check's own intended
  stopping point (boot-to-idle), matching QEMU's milestone at this
  exact same historical point in the project.

Read the Known risk points section below for what's still genuinely
unverified past this point (no SD/GPIO/USB/Ethernet exercised at all
yet on this board). Testing any of that for real would need a
2-partition (FAT32 boot + HFS+ root) card built the same way
`qemu/hfs_root_test.img` is, with the CURRENT from-source kernel
(`build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped`),
not this frozen snapshot -- not attempted yet.
something is wrong with XNU itself.

## What's here

- `bootcode.bin`, `start.elf`, `fixup.dat` - stock Raspberry Pi GPU boot
  firmware, unmodified, fetched from `raspberrypi/firmware`'s `boot/`
  directory. These are what actually power on and run before the ARM cores
  do anything at all - completely standard, same files every Pi OS image
  ships.
- `config.txt` - tells the GPU firmware to load `u-boot.bin` as an AArch64
  kernel image instead of Linux, and forces HDMI on. See its comments.
- `u-boot.bin` - **locally rebuilt** from `third_party/u-boot` (still
  `FlorentRevest/u-boot` commit `353ceb88` as its base, confirmed via its own
  `U-Boot 2020.04-rc3-g353ceb88-dirty` banner string - `-dirty` because of
  the three local patches below, uncommitted). No longer the precompiled
  NSG650 binary. Built `CONFIG_ARM64=y` + `CONFIG_DM_VIDEO=y`, same as
  before, so u-boot's *own* console still reaches HDMI. Three local patches
  on top of stock `cmd/bootxnu.c`:
  1. **HDMI framebuffer handoff to XNU.** `PE_init_platform()`
     (`pexpert/arm/pe_init.c`, stock/unmodified, already compiled into
     `kernel.development.bcm2837.unstripped`) unconditionally copies
     `boot_args->Video` into `PE_state.video` before `PE_create_console()`
     decides graphics-vs-text console mode - `bcm2837_video_init()`'s
     in-kernel mailbox call (source-only, still pending the Xcode kernel
     rebuild) is only a *fallback* for when that's zero. Stock
     `bootxnu.c` never populated it (`memset(boot_args, 0, ...)` then never
     touches `video_information` again). Patched `do_bootxnu()` to look up
     u-boot's own already-probed `UCLASS_VIDEO` device
     (`uclass_get_device(UCLASS_VIDEO, 0, &vdev)` +
     `dev_get_uclass_priv(vdev)`) and copy its `fb`/`xsize`/`ysize`/
     `line_length`/`bpix` into `boot_args->video_information` before the
     `armv8_switch_to_el1` jump. **This means XNU's own boot console should
     now actually draw on the HDMI screen, with no kernel source changes
     and no Xcode/from-source rebuild needed at all** - untested on real
     silicon, reasoned from source (see Known risk points).
  2. **Autoboot straight into XNU, no typing required.** `.config`'s
     `CONFIG_BOOTCOMMAND` changed from `"run distro_bootcmd"` to the exact
     three-command sequence documented below. `CONFIG_BOOTDELAY=2` is
     unchanged (stock default) - there's a 2-second window after the u-boot
     banner to mash a key and drop to the interactive `=>` prompt instead,
     useful if something needs debugging before `bootxnu` runs.
  3. **Fix `load_mach_o_image()`'s entry-point computation.** First real
     boot attempt hit a Synchronous Abort at ELR `0x414a488`, immediately at
     the address `"Starting XNU at"` printed - i.e. an instruction abort at
     the jump target itself. Root cause: `ret.base = min_t(uintptr_t,
     sc->dst, ret.base)` (line ~72) took the `min()` over **every**
     `LC_SEGMENT`, including zero-sized ones. This kernel has an empty
     `__PRELINK_TEXT` segment at vmaddr `0xfffffff004004000`, 48MB below the
     real `__TEXT` base (`0xfffffff007004000`) - it contributed no bytes but
     still won the `min()`, so `xnu_entry = entry - base + XNU_LOAD_ADDR`
     computed a physical address 48MB past the end of the ~14.86MB image
     actually copied into RAM, landing on unpopulated memory. Confirmed
     against `otool -l kernel.development.bcm2837.unstripped`: `entry
     (0xfffffff00714a488) - __PRELINK_TEXT base (0xfffffff004004000) +
     XNU_LOAD_ADDR (0x1004000) = 0x414a488`, exact match to the fault ELR.
     Fixed by skipping segments with `dst_len == 0` when computing
     `ret.base`/`ret.end`. Deterministic bug, not flaky - reproduced
     identically from the loader logic alone, no hardware-specific
     reasoning needed.

  Rebuild after any further `cmd/bootxnu.c` change:
  ```bash
  cd third_party/u-boot
  gmake CROSS_COMPILE=aarch64-unknown-linux-gnu- u-boot.bin -j"$(sysctl -n hw.ncpu)"
  cp u-boot.bin ../../sdcard/u-boot.bin
  ```
  Needs `brew install messense/macos-cross-toolchains/aarch64-unknown-linux-gnu`
  (tap must be `brew trust`ed first - a newer Homebrew security gate, not
  optional) and `brew install make` (macOS's bundled GNU Make 3.81 chokes
  with `missing separator` on this Makefile - use `gmake`, not `make`).
  **2026-09-24:** `HOSTLDFLAGS="-Wl,-ld_classic"` no longer works. Current
  Apple ld ignores it ("-ld_classic is no longer supported"). The
  anurodhp/u-boot fork now fixes the root cause instead (commit 9a104b77:
  mkimage's table moves to `__DATA` and is read with `getsectiondata()`),
  so no flag is needed. Same fork, commit 8b25a24d: the auto-boot bootargs
  are now `rd=disk0s2 serial=3 hdmi_console=1 hid-legacy-shim=1`, for the
  HDMI console (DAR-331) and USB keyboard input (DAR-245), neither verified
  on real hardware yet. Historical note on the old flag:
  `HOSTLDFLAGS="-Wl,-ld_classic"` worked around modern Xcode `ld`'s
  "Illegal text-relocations" rejection of `tools/mkimage`'s legacy
  non-PIC function-pointer-table objects (host tool, unrelated to the
  actual aarch64 target code) - without it the build fails before ever
  reaching `u-boot.bin`.

  **[2026-08-21] `.config` is now reproducible for real** -- it used to
  be a gitignored, undocumented-in-any-patch file manually edited
  out-of-band (a real gap this note used to just describe in prose, not
  actually let you reproduce from a fresh clone). Fixed: this checkout's
  real customization beyond stock `configs/rpi_3_defconfig`
  (`CONFIG_CMD_BOOTXNU=y` plus a real `CONFIG_BOOTCOMMAND`) is now
  `configs/rpi3_xnu_defconfig`. It was originally captured in
  `third_party-patches/u-boot.patch`; since 2026-09-24 it lives only in
  the `anurodhp/u-boot` fork, which `setup_third_party.sh` clones directly
  (the patch file and the `git am` step are retired, see
  docs/repos-to-clone.md). So a fresh clone + `setup_third_party.sh` +
  `gmake CROSS_COMPILE=aarch64-unknown-linux-gnu- rpi3_xnu_defconfig`
  reproduces this exact `.config`. To change the auto-boot command (or
  any other config) again: edit `configs/rpi3_xnu_defconfig` directly (or
  run `make menuconfig`/edit `.config`, then `make savedefconfig` and copy
  the result over `configs/rpi3_xnu_defconfig`). Re-run `gmake
  rpi3_xnu_defconfig` before rebuilding `u-boot.bin`, then commit in
  `third_party/u-boot` and push the fork, so the change isn't lost on the
  next fresh clone.

  **Real bug fixed this way, 2026-08-21**: `CONFIG_BOOTCOMMAND` now sets
  `bootargs=rd=disk0s2` before `bootxnu` runs. Without it (the previous
  behavior -- real hardware boots with zero `bootargs` set, unlike
  QEMU's `runvm.sh`), XNU's root-device selection
  (`iokit/bsddev/IOKitBSDInit.cpp`) falls through to generic
  `IOProviderClass=IOMedia`/`Content=Apple_HFS` matching, which can
  never succeed here -- this project's own kext personalities never
  wire up the `kIOFDiskPartitionSchemeContentTable` content-hint table
  the generic path depends on. Confirmed via a real device boot that
  hung forever at "Waiting for root device" despite a verified-good,
  correctly-partitioned/typed real HFS+ card (checked with `fdisk`/`xxd`
  on the actual image file: real `0xAF` MBR type byte, correctly
  formed). `rd=disk0s2` (the same explicit-named-device convention
  `runvm.sh` already uses successfully) skips that whole broken path.

  **Second real bug found+fixed the same way, same day**: `bootargs`
  now also includes `serial=3` (`"rd=disk0s2 serial=3"`). Root mount
  succeeded after the `rd=` fix and reached a real login prompt over
  serial -- but nothing typed reached the shell. Root cause:
  `serialmode` defaults to `0` (`osfmk/arm/arm_init.c`) unless a real
  `serial=` boot-arg is present, and `SERIALMODE_INPUT` (`0x2`)
  specifically gates the real serial-input-polling path
  (`osfmk/console/serial_general.c`) -- entirely separate from the
  always-on `kprintf` output path (`cons_ops`/`SERIAL_CONS_OPS`, see
  the "no HDMI console output" bug above), which is exactly why the
  prompt was visible but typing did nothing. `serial=3` (`OUTPUT|
  INPUT`) is the same value `runvm.sh` already uses for QEMU, and was
  already documented as a requirement in `CLAUDE.md`'s own boot docs --
  it just was never applied to the real-hardware boot chain until now.
- `kernel.development.bcm2837.unstripped` - a standalone snapshot copy of
  the old NSG650-derived `precompiled/`'s already-patched kernel (both
  binary patches applied: the `pmap_virtual_region` region-select fix and
  the `IOPanicPlatform` neutering - see project memory for exact
  offsets/reasoning). `precompiled/` itself was deleted 2026-08-08 (fully
  superseded by the from-source `xnu-7195` build) - this copy is
  self-contained and unaffected, but is not being kept in sync with
  anything going forward.
- `rpi3.dtb` - compiled from the **new** `devicetrees/rpi3-realhw.dts`, not
  the QEMU-tuned `devicetrees/rpi3.dts`. Only difference: `dram-size` is
  `0x08000000` (128MB) here instead of QEMU's `0x3C000000` (960MB) - see
  "Why 128MB" below.

## Preparing the SD card

1. Format a real SD card as a single FAT32 partition (MBR, not GPT - the
   GPU firmware's own boot ROM only understands FAT on an MBR partition
   table).
2. Copy all 7 files in this folder to that partition's root. No subfolders.
3. Insert into the Pi 3, connect an HDMI display, power on.

## Why 128MB (`devicetrees/rpi3-realhw.dts`, not the QEMU one)

`cmd/bootxnu.c` sets `boot_args->mem_size = CONFIG_SYS_SDRAM_SIZE`, a
compile-time constant from `include/configs/rpi.h` - confirmed `SZ_128M`
in this exact build, regardless of the Pi 3's real 1GB. This is independent
of `config.txt`'s `gpu_mem=` and independent of the DT's own `dram-size` -
it's just what this specific `bootxnu.c` unconditionally tells XNU. The DT
handed to XNU has to agree with that number (arm_init() reads `dram-size`
from the DT chosen node separately, and a real vs. claimed RAM size
mismatch is exactly the class of bug already found and fixed once on the
QEMU path - see project memory, `vm_first_phys`/`topOfKernelData`). If
`third_party/u-boot` is ever rebuilt with a larger `CONFIG_SYS_SDRAM_SIZE`,
`devicetrees/rpi3-realhw.dts`'s `dram-size` needs to be regenerated to
match (`devicetrees/Makefile`'s two-step process, but invoke
`third_party/dtc-AppleDeviceTree/dtc` explicitly, not bare `dtc` on PATH -
see `devicetrees/README.md`).

## What to expect

1. GPU firmware runs invisibly (maybe a brief rainbow splash - normal).
2. u-boot starts and should print its own banner/log **on the HDMI
   screen** - this alone is a real sanity check: it confirms the physical
   GPU -> ARM -> u-boot chain works on this board.
3. **After a 2-second countdown, it autoboots** straight into:
   ```
   fatload mmc 0:1 0x04000000 kernel.development.bcm2837.unstripped
   fatload mmc 0:1 0x06000000 rpi3.dtb
   bootxnu 0x04000000 0x06000000
   ```
   (baked into this build's `CONFIG_BOOTCOMMAND` - see the `u-boot.bin`
   bullet above). Mash any key during the countdown to drop to the
   interactive `=>` prompt instead and run those three commands by hand
   (e.g. to debug, or if `mmc 0:1` doesn't find the card - try `mmc list`
   then `mmc dev 0` to confirm the device number first). The two load
   addresses are staging buffers for the raw files - chosen to sit
   comfortably clear of where `bootxnu` actually copies the kernel's
   Mach-O segments to (`CONFIG_SYS_LOAD_ADDR + 0x4000` = `0x1004000`
   onward, ending well under `0x04000000`), and clear of each other.
4. After `bootxnu` runs, **XNU's own boot console should now draw directly
   on the HDMI screen** - `u-boot.bin` hands off its already-working HDMI
   framebuffer via `boot_args->video_information`, which stock
   `PE_init_platform()` (already compiled into
   `kernel.development.bcm2837.unstripped`, no kernel changes needed) reads
   unconditionally. This is untested on real silicon - reasoned from
   source, see Known risk points. **If the screen instead goes blank/frozen
   at whatever u-boot last drew**, that means the framebuffer handoff
   didn't take for some reason (e.g. the GPU driver returned a different
   pixel format/geometry than expected, or didn't reset cleanly across the
   `bootxnu` jump) - fall back to serial to see what's actually happening:
   a USB-to-serial adapter on GPIO14 (TX)/GPIO15 (RX)/GND, 115200 8N1 -
   `enable_uart=1` is already set in `config.txt`. This is the real
   equivalent of QEMU's `-serial stdio`.

## Known risk points (past this point, none of this has run on real silicon yet)

- **[CONFIRMED 2026-08-21]** The two binary patches baked into this kernel
  (derived for the QEMU+`qemu-zhuowei` loader path's `gVirtBase`/
  `LOW_GLOBAL_BASE_ADDRESS` collision) do reproduce and get handled
  correctly on real silicon too, not just in QEMU -- real kernel boot
  output (the BSD copyright banner, over real serial) confirms XNU
  actually ran past the jump rather than crashing, which the reasoned
  prediction below could not have guaranteed on its own. Original
  reasoning kept for context: `bootxnu.c` uses a fixed, non-randomized
  `phys_base`/`virt_base` (no KASLR anywhere in that file), and
  `virt_base = load_info.base` comes straight from the kernel Mach-O's
  own fixed link address - a property of the binary, not the loader - so
  the same collision was expected to reproduce identically here, and now
  has.
- `CONFIG_SYS_LOAD_ADDR` in this u-boot build is `0x1000000`, matching
  real u-boot's own convention, so no new physical-placement assumption
  here.
- This sanity check only exercises boot-to-idle, the same point already
  reached on QEMU. No SD driver, no GPIO pinmux, no Ethernet/USB involved -
  none of that is expected to work here regardless of outcome.
- No `bootargs` env var is set (so no `-v`/`debug=` boot-args reach XNU).
  If wanted: `setenv bootargs "debug=0x8 kextlog=0xfff -v"` at the u-boot
  prompt before running `bootxnu`, matching the QEMU `-append` convention.

## Autoboot (done - no `boot.scr` needed)

This card's `u-boot.bin` now autoboots straight into `bootxnu` on its own
(`CONFIG_BOOTCOMMAND` baked in at build time - see above), so a separate
`mkimage`-built `boot.scr` was never actually needed for this. `mkimage`
itself turned out to be a pain to build on this host anyway (modern Xcode
`ld` rejects its host-tool object files as "Illegal text-relocations" -
worked around with `HOSTLDFLAGS="-Wl,-ld_classic"`, see above).
