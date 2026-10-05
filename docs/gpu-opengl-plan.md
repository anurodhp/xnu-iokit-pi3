# GPU / OpenGL plan (investigation, 2026-10-05)

Status: investigation only. Nothing built. Facts marked (V) were checked in
this tree; (U) are from memory/upstream and must be verified before relying
on them.

## Findings

1. **PureDarwin has no GL and no GPU support to reuse (V).** Grep of the
   pinned `third_party/PureDarwin` (and the org's repo list, 2026-10-05) finds
   no Mesa, libGL, EGL, swrast, glamor or libdrm. What it has for X is
   exactly what we already use: Xorg built via nix (`xorg-server` 21.1.24,
   xvfb.nix) with the busless 2D DDX `xf86-video-puredarwingop` (shadow fb
   into the PDGOP VRAM map) and `xf86-input-iohid`. Their Wayland effort
   (`gershwin-desktop`) is 2D too. So "use what they've done" covers the X
   server only, which we already ship.
2. **Our X build deliberately has no GL (V).** `build_xserver.sh:190-191`:
   `-Dglamor=false -Dglx=false -Ddri1/2/3=false -Ddrm=false`. No Mesa, libdrm,
   libepoxy, or LLVM-for-JIT in `third_party/` or `tools/userland_staging/`
   (`llvm-project` is only used for libc++/libc++abi). `webkit-browser-feasibility.md`
   already needs libepoxy and an "EGL-optional" path.
3. **Display path is a dumb framebuffer (V).** `docs/framebuffer-mapping.md`:
   Xorg -> PDGOP -> `IOFramebufferUserClient` VRAM -> firmware-allocated
   framebuffer (`IOBootFramebuffer`). No modesetting, no GPU involvement.
4. **QEMU cannot test a GPU (U).** `-M raspi3` emulates no V3D/VC4, and no
   virtio-gpu (no PCI/virtio-mmio on that machine). Anything beyond software
   GL is real-hardware-only, which fits the project's existing
   "HW verified separately" pattern but is slow to iterate.
5. **Hardware target is VideoCore IV (VC4 3D pipe, "V3D" regs) (U).** Upstream
   Mesa's `vc4` gallium driver is the only open driver; it talks to Linux DRM
   via `DRM_IOCTL_VC4_{CREATE_BO,MMAP_BO,SUBMIT_CL,WAIT_SEQNO,...}`. GPU memory
   comes from CMA/the GPU split; on this tree the firmware owns the mailbox
   property interface (`pexpert/arm/pe_init.c` already uses it for the
   framebuffer). Broadcom publishes the VideoCore IV 3D Architecture Reference
   Guide; the register map is documented.

## Realistic tiers

| Tier | What | Testable on QEMU | Risk |
|---|---|---|---|
| A | **Software GL**: Mesa swrast (softpipe, then llvmpipe) + GLX in Xorg/Xvfb | yes | medium (build only) |
| B | Kernel **V3D IOKit driver** + user client (BO alloc, CL submit, seqno wait, IRQ) | no (HW only) | high |
| C | Mesa **vc4 gallium** on a Darwin winsys that speaks B's user client instead of DRM; DRI-less GLX/EGL path | no | high |
| D | Zero-copy present (shared BO -> scanout) | no | high; optional |

Recommendation: do **A first**. It delivers working OpenGL to GNUstep/X apps
(and WebKit/libepoxy) with no kernel work and is fully QEMU-testable; B/C are
a separate multi-week effort whose payoff on a Pi 3 is modest (vc4 is GLES2
class; Mesa vc4 exposes GL 2.1 / ES 2.0).

## Tier A plan (software OpenGL)

Order matters; each step ends with a script + smoketest per the repo rule.

1. **Spike (1-2 d):** build Mesa (pin a release ~24.x/25.x; check which still
   builds `-Dgallium-drivers=softpipe -Dglx=dri -Dplatforms=x11 -Dllvm=disabled
   -Dvulkan-drivers=` cross for `arm64-apple-ios14.4`). Question to answer:
   does GLX swrast require libdrm headers/libdrm at link time with
   `-Ddri3=disabled`? (U — Mesa's swrast DRI loader historically needs
   libdrm's `xf86drm.h`; expect to build libdrm with only headers+`xf86drm`
   core, or use Mesa's `-Dglx=xlib` legacy path if still present.) Fork to
   `anurodhp/mesa` per the fork rule; do not carry patch files.
2. **Dependencies as real dylibs** (new-work convention): `libdrm` (minimal),
   `zlib` (have), `expat` (have), `libX11/xcb/xfixes/xext/xxf86vm/xshmfence`
   client libs (mostly have; check `libxshmfence`, `libXxf86vm`, `libXdamage`),
   `python3-mako`+`meson`+`ninja` host-side, `libepoxy` (also unblocks WebKit).
3. **Mesa -> `libGL.dylib` / `libglapi.dylib` / `swrast_dri` (as `.so` bundle
   or static-in-libGL) via `build_mesa.sh`.** Gate with `bind_target_audit.sh`
   and `weak_def_audit.sh` — Mesa's glapi relies on TLS (`__thread`) and weak/
   coalesced symbols; both audits are the known failure modes here (U: TLS on
   this dyld/libpthread needs a check — `_tlv_*` via `libdyld` thread-locals).
4. **Rebuild xserver with GLX:** flip `-Dglx=true` (software only, `-Ddri*`/
   `-Dglamor` off) in `build_xserver.sh`; Xorg then loads `libglx` module
   (`MH_BUNDLE` modules path already exists for DDX). Also covers Xvfb for
   QEMU testing.
5. **Smoketests:** `glxinfo -B` (renderer "softpipe"/"llvmpipe"), `glxgears`
   under Xvfb on QEMU (software; expect low fps), then a GNUstep-back
   NSOpenGLView test. Then real Pi.
6. **llvmpipe (optional, after softpipe works):** needs LLVM as a library
   (`llvm-project` is already pinned for libcxx only), plus JIT executable
   memory: verify `mmap(PROT_EXEC)`/`MAP_JIT` semantics in our xnu fork
   (`third_party/xnu-7195`, `bsd/kern/kern_mman.c`) and W^X handling on ARM
   `pmap`. Likely large + slow on 4xA53 TCG; defer.

## Tier B/C sketch (only if A lands and HW GL is wanted)

- B: new `iokit/BCM2837V3D/` driver: map V3D regs (BCM2837 `0x3fc00000`
  ARM-side (U)), power via mailbox/PM domain, IRQ via the local/GIC
  controller the project already drives, BO allocator on top of
  `IOBufferMemoryDescriptor` with contiguous physical pages + the GPU/bus
  address translation, CL submit with the vc4 validator logic ported from
  Linux `vc4_validate.c` (shader/CL validation is mandatory for isolation).
  Personality goes in `BCM2837Personalities.cpp`.
- C: Darwin `vc4` winsys replacing `drm_ioctl`; small libdrm shim exporting
  only the vc4 entry points over an `IOConnectCallMethod` user client.
- Prior art to read, not reuse directly: Linux `drivers/gpu/drm/vc4/`,
  Mesa `src/gallium/drivers/vc4` and `winsys/vc4`, NetBSD/FreeBSD (no mature
  VC4 3D driver known (U)). Licence: Linux driver is GPL-2; port from the
  hardware docs + Mesa (MIT) interface, don't copy GPL code into the kernel
  tree without deciding on that.

## Open questions for the user

1. Is software GL (Tier A) acceptable as the milestone, with real VC4 as a
   later, separately-ticketed effort?
2. Driver for X: keep `puredarwingop` shadow-fb DDX under GLX (software GL is
   composited by the server into the shadow fb — works, no scanout sharing)?
3. GPL licensing stance for anything ported from Linux's vc4 DRM.

## Next actions (not yet done)

- File Linear umbrella + sub-issues (spike, deps, build_mesa, xserver GLX,
  smoketests, llvmpipe, V3D spike).
- Run Step 1 spike in this worktree; note `third_party/` is gitignored and
  lives only in the main checkout, so clone Mesa/libdrm via
  `setup_third_party.sh` entries (fork first).

## Status 2026-10-05

- **Tier A (software GL): done and QEMU-verified** (deb58c5, DARW-293).
- **Licensing (DARW-299): Linux's vc4 driver may be used as the reference**
  (project owner, 2026-10-05). Code is written independently with the Linux
  file/line it follows cited in comments; no Linux source is copied.
- **V3D kext written, compiles, untested on hardware (DARW-295):**
  `iokit/BCM2837V3D/` (driver, user client, register map, userland ABI
  `BCM2837V3DUAPI.h`), `BCM2837Mailbox::setClockState`, `v3d` personality,
  and a `v3d` DT node under `#ifdef RPI3_HAVE_V3D` (defined only by
  `rpi3-realhw.dts`). Implemented: power sequence (firmware V3D clock, PM
  reset release, ASB bridges), IDENT check, IRQ, two-stage bin/render job
  pipeline, hang detector + reset, BO allocation (contiguous,
  write-combined, GPU address = phys + 0xC0000000), user client with
  `IOConnectMapMemory64` BO mapping, root + `v3d-raw=1`-gated raw submit.
  **Not implemented:** validated submit (the CL/shader validators; Linux
  `vc4_validate*.c`, `vc4_render_cl.c`), binner OOM handling.
- **Not yet in the kernel:** the sources must be added to
  `third_party/xnu-7195/iokit/conf/files` (fork change) and synced with
  `tools/sync_iokit_drivers.sh`. `tools/build_kexts.sh` compiles them
  standalone (BCM2837V3D is in its list).
