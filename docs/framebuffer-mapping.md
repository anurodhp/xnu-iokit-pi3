# Framebuffer mapping for Xorg (DAR-460)

How Xorg's `puredarwingop` driver reaches the HDMI framebuffer, what cache
attribute the VRAM mapping has, and the driver options that control it.

## Path

Xorg → `puredarwingop_drv.so` → PDGOP (`PDGOP.c`, PureDarwin fork) →
`IOConnectMapMemory64(connect, kIOFBVRAMMemory = 110, task, …, options)` →
IOGraphics `IOFramebufferUserClient` → `IOBootFramebuffer` VRAM →
the framebuffer the VideoCore firmware allocated
(`pexpert/arm/pe_init.c` `bcm2837_video_init`, `fb_width`/`fb_height`
boot-args; QEMU 640x480 by default, the real Pi 1280x720).

## Cache attribute of the user mapping (xnu-7195, this tree)

Before DAR-460 PDGOP passed only `kIOMapAnywhere`, so the cache bits
(`kIOMapCacheMask` 0xf00) were `kIOMapDefaultCache`. Following it through:

1. `IOUserClient::mapClientMemory64` keeps the caller's
   `kIOMapUserOptionsMask` bits (0xfff, which include the cache mask)
   and maps the descriptor `clientMemoryForType` returned
   (`iokit/Kernel/IOUserClient.cpp:2011-2022`; `iokit/IOKit/IOTypes.h:216-229`).
2. `IOFramebufferUserClient::clientMemoryForType(kIOFBVRAMMemory)` returns a
   fresh `getVRAMRange()` (`iokit/Graphics/IOGraphicsFamily/IOFramebufferUserClient.cpp:265-270`),
   which is `IOBootFramebuffer::getApertureRange`:
   `IODeviceMemory::withRange(bootDisplay.v_baseAddr, bytes)`
   (`IOBootFramebuffer.cpp:236`) → `IOMemoryDescriptor::withAddressRange(…,
   kIODirectionNone | kIOMemoryMapperNone)` (`iokit/Kernel/IODeviceMemory.cpp:39-40`).
   No cache mode in the descriptor.
3. A physical descriptor with no cache mode takes
   `cacheModeForPagerFlags(IODefaultCacheBits(pa))`
   (`iokit/Kernel/IOMemoryDescriptor.cpp:499-503`), and
   `IODefaultCacheBits` is `pmap_cache_attributes(pa)`
   (`osfmk/device/iokit_rpc.c:416-419`).
4. `pmap_cache_attributes` returns `VM_WIMG_IO` for any address that is not
   managed RAM and has no `pmap-io-ranges` entry
   (`osfmk/arm/pmap.c:12101-12104`). The VideoCore framebuffer is above the
   ARM's share of RAM: boot_args.memSize stops at the ARM/GPU split on both
   targets (qemu-zhuowei `hw/arm/boot.c` `macho_setup_bootargs`, commit
   f57405b; u-boot fork `cmd/bootxnu.c:147-158`), and this port's device tree
   has no `pmap-io-ranges`. So the descriptor's mode is `kIOInhibitCache`.
5. The device pager is created with `pagerFlagsForCacheMode(kIOInhibitCache)`
   = `VM_WIMG_IO` (`IOMemoryDescriptor.cpp:311-320, 702-711`), which becomes
   the VM object's `wimg_bits` (`osfmk/vm/device_vm.c:275`) and is passed to
   `pmap_enter` on fault (`osfmk/vm/vm_fault.c:5876`); for an unmanaged page
   `pmap_enter_options` uses those bits (`osfmk/arm/pmap.c:8557-8563`).
6. `wimg_to_pte(VM_WIMG_IO)` = `CACHE_ATTRINDX_DISABLE`
   (`osfmk/arm/pmap.c:7951-7955`), and MAIR attribute 3 is `MAIR_DISABLE`
   0x00, **Device-nGnRnE** (`osfmk/arm64/start.s:763`,
   `osfmk/arm64/proc_reg.h:817, 838`).

**Result: the user VRAM mapping was Device-nGnRnE** (strongly ordered, no
gathering, no early write acknowledgement): the slowest attribute there is
for a stream of pixel stores, and every read is uncached. The kernel's own
mapping of the same range is also uncached (`IOFramebuffer.cpp:104-108`,
`kIOFBMapCacheMode = kIOMapInhibitCache` on non-x86).

### Write-combine is available to user space

A user map that asks for `kIOMapWriteCombineCache` (0x400):
`memoryReferenceMap` turns a non-default cache mode into a
`mach_make_memory_entry(MAP_MEM_ONLY | MAP_MEM_WCOMB)` on the named entry
(`IOMemoryDescriptor.cpp:846-851, 1017-1021`), which changes the VM object's
`wimg_bits` to `VM_WIMG_WCOMB` (`osfmk/vm/vm_user.c:2393-2431`,
`vm_object_change_wimg_mode`); device-pager objects are not `nophyscache`
unless asked (`device_vm.c:271-273`). `VM_WIMG_WCOMB` →
`CACHE_ATTRINDX_WRITECOMB` → `MAIR_WRITECOMB` 0x44, Normal memory, inner and
outer non-cacheable (`pmap.c:7968-7971`, `proc_reg.h:821, 836`). Normal
memory gathers stores in the A53's write buffer and allows unaligned
accesses. Because the user client builds a new descriptor per map call
(comment at `IOFramebufferUserClient.cpp:266-267`), this changes only the
user mapping, not the kernel's. Both mappings stay non-cacheable, so the
mismatched attributes cannot leave stale cache lines.
`kIOMapPostedWrite` (0x600) gives `VM_WIMG_POSTED` →
`CACHE_ATTRINDX_POSTED` → `MAIR_POSTED` 0x04, Device-nGnRE (early ack).

## Driver options (`/etc/X11/xorg.conf`, Device section)

Implemented on PureDarwin fork branch `dar-460-fb`, fast-forwarded into
`iokit-launchd-port` (69276ed8)
(`src/Userspace/xf86-video-puredarwingop/puredarwingop_drv.c`,
`src/Libraries/PDGOP/PDGOP.c`):

| Option | Default | Meaning |
|---|---|---|
| `VRAMCacheMode` | `writecombine` | `writecombine`, `posted`, `inhibit`, or `default` (the kernel's choice, Device-nGnRnE: the mapping before DAR-460). Falls back to `default` if the map fails. |
| `ShadowFB` | `on` | Render into a RAM copy and copy damaged boxes to VRAM from the block handler. `off` renders straight into VRAM. |
| `ShadowUpdateDelay` | `0` | ms. Hold copies until damage is quiet for half this long, or this long since the first undrawn damage. 0 copies every time the server goes idle. |
| `ShadowStats` | off | Log `ShadowStats: N updates, N boxes, N KB copied in N ms over N s` every 10 s. |

The shadow framebuffer already existed before DAR-460 (the driver's own
damage + block-handler copy, the same design as xserver's
`miext/shadow/shadow.c` `shadowRedisplay`/`shadowBlockHandler`, lines
56-75; the `shadow`/`shadowfb` modules are not staged by this port's
`build_xserver.sh`). DAR-460 made it optional and replaced its `memcpy`
with a copy loop whose stores are all size-aligned (4-byte head/tail,
16-byte body), which is safe on a Device mapping (unaligned Device accesses
fault on arm64) and fills write-buffer lines on a write-combine one. The
copy uses the framebuffer's real `bytesPerRow`, which may exceed width × 4.

The driver logs, at startup:

```
(**) PUREDARWINGOP(0): ShadowFB on, ShadowUpdateDelay 0 ms, VRAMCacheMode writecombine
(II) PUREDARWINGOP(0): VRAM mapped writecombine (map options 0x401) at 0x..., size 0x...
(II) PUREDARWINGOP(0): rendering into a shadow framebuffer in RAM
```

`PDGOPPresent` (selector 11, meant for PureDarwin's VirtIO GPU) is now
skipped unless the service is `IOVirtIOGPU`: on an `IOFramebuffer` selector
11 is `extSetGammaTable` with 5 scalar inputs
(`IOFramebufferUserClient.cpp:338-339`), so the call was rejected by the
scalar-count check (`iokit/Kernel/IOUserClient.cpp:6239-6241`) and only cost
a Mach trap per screen update.

## Test tools

- `tools/userland_staging/build_fb_cache_test.sh` → `/usr/local/bin/fb_cache_test`:
  maps VRAM once per cache mode the way the driver does and prints
  store32/store128/copy/read MB/s for RAM and for each mapping, then checks
  the stores landed. `-T n` writes one band per second (DAR-443 check).
- `tools/userland_staging/fb_cache_qemu_prep.sh` + `fb_cache_guest_test.sh`:
  QEMU run of fb_cache_test and of Xorg `-retro` under four driver
  configurations with x11_bench.

## What QEMU can and cannot show

QEMU TCG does not model memory attributes at all (qemu-zhuowei
`target/arm/helper.c`, comment in `HELPER(dc_zva)`: "the usual QEMU
behaviour of not implementing either alignment faults or any memory
attribute handling"). Device, posted and write-combine mappings therefore
run at the same speed under QEMU, and an unaligned store to a Device
mapping does not fault there. QEMU shows only that each mode maps, that
stores land, and that the driver's code paths work.

### QEMU results (2026-10-01, hdmi_console=1, 640x480)

- All four cache modes map (`map options` 0x1/0x401/0x101/0x601), the
  stores read back through the mapping, no fault or panic. This exercises
  the kernel's `MAP_MEM_ONLY` cache-mode change on the device-pager entry.
  Throughput is the same for every mode (store128 about 650-810 MB/s,
  copy about 410-500 MB/s, the same as RAM), as expected without attribute
  modelling.
- Xorg starts with each configuration (WC + shadow, default + shadow,
  WC without shadow, WC + shadow + 16 ms delay) and logs the options,
  `VRAM mapped writecombine (map options 0x401)` and `rendering into ...`.
  x11_bench's 2000 draw+XSync round trips: 78 shadow copies (29.9 MB) with
  no delay, 59 with the default mapping, 40 (22.0 MB) with
  `ShadowUpdateDelay 16`. ShadowFB off was fastest under QEMU (0.8 s vs
  2.1-2.7 s), because QEMU's VRAM is ordinary host RAM; that says nothing
  about the Pi, where it means uncached reads.
- DAR-443 reproduces with fb_cache_test `-T` (one band per second): every
  band reads back correctly through the user mapping, but the monitor's
  `xp` of guest physical 0x3C100000+ shows only the first three bands
  (the first three 16 KB pages, about 0-2 s); later bands are not in guest
  physical memory, and the Xorg screendumps are black. So QEMU cannot show
  X output; the Pi must be used for anything visual.
