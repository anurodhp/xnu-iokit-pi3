# WebKit browser feasibility (WPE WebKit + GNUstep chrome)

Study date 2026-10-01. Research plus host-only compile trials; nothing was
built into `tools/userland_staging/libc_build`, no QEMU, no Pi. Scratch
work lived in the session scratchpad (`.../scratchpad/webkit/`), not in this
repo.

Scope, per the user: **WebKit only.** WPE WebKit is the primary candidate;
WebKitGTK (X11), WebKitLegacy and Apple's Mac port are compared. Software
rendering only (Skia/Cairo on the CPU), no WebGL, no accelerated
compositing, no GPU process, no media/WebRTC.

## 0. Answer first

**Go later.** A stripped WPE WebKit with a GNUstep chrome is technically
feasible on this system. It is not blocked by anything structural in XNU or
our dyld, but it needs a large stack under it that we do not ship yet.

- The cheapest real milestone, **JavaScriptCore**, is close. WebKit trunk's
  JSC (LLInt interpreter, JIT off) **compiled for `arm64-apple-ios14.4`
  against the Xcode-12 iPhoneOS SDK in this study** (clean build 2 min
  wall / 10 min CPU on the M1 host). It needed 3 small shims and 2 build
  tweaks. It links with 15 symbols unresolved against the SDK and 108
  against the dylibs we ship (§5). Stripped size is **15.6 MB**.
- **GLib** (the biggest new dependency of WPE) also **cross-compiled and
  linked clean in 19 s** for the same target. It is missing only 8 exports
  from our libSystem (§5.3).
- The real cost is everything else WPE needs. We have to build or upgrade
  about 15 libraries (libsoup3, OpenSSL + glib-networking, HarfBuzz, sqlite,
  libjpeg/webp, libgcrypt, libtasn1, libepoxy, libxkbcommon, ICU ≥ 70,
  libxml2 ≥ 2.9.13, and a **system libc++ upgrade from LLVM 11 to ≥ 19**),
  then port WebCore/WebKit2 itself (Skia, IPC, process launcher), then
  write a GNUstep WPEPlatform backend and the chrome.
- Rough effort: **3–5 months** of focused work (§4). Runtime fits a Pi 3
  only with the full ~940 MB that DAR-412 gives XNU (budget about 200–300 MB
  for one simple tab, §3). JS-heavy sites will be very slow without a JIT
  (LLInt is about 4–5x slower than JSC's tiered JIT on JetStream 2, and up
  to about 29x slower on hot numeric code, §1.5).
- Recommendation: run **Phase 0 (JSC on the Pi) now**. It is about 1–2
  weeks, and most of its work (libc++ upgrade, ICU bump, libm/libplatform
  export gaps) pays off for any later C++ port. Decide on the full WPE
  build after Phase 0's measurements.

Not pursued (user decision, 2026-10-01): NetSurf, Dillo, Servo,
Ladybird/LibWeb and Chromium. They are dropped as candidates. The short
reason: the user wants a real WebKit-family engine that can track modern
web content, and Chromium/Blink is a strictly larger version of every
WebKit cost listed here (bigger build, V8 JIT/W^X assumptions, no
non-Linux/non-Cocoa Darwin port). NetSurf or Dillo would be a cheaper but
different product, not a step toward WebKit.

## 1. Options landscape (WebKit family)

### 1.1 WPE WebKit (primary)

WPE is WebKit's embedded port: no toolkit, and a pluggable platform layer.
Trunk at `315714135986` (2026-10-01) is version 2.55; stable is
**2.54.0** (Debian ships `libjavascriptcoregtk-6.0-1 2.54.0-1~deb13u1`).

Hard requirements, from `Source/cmake/OptionsWPE.cmake:14-28` at that
commit:

| Package | Min | Notes |
|---|---|---|
| GLib (GioUnix, Thread, Module) | 2.70 | event loop, GObject API, GIO modules |
| HarfBuzz (+ICU) | 2.7.4 | text shaping |
| ICU (data, i18n, uc) | **70.1** | we ship Apple ICU-66109 (66.1) |
| libjpeg, libpng, libwebp(demux) | | image decoders |
| libepoxy | 1.5.4 | always required, even with GL unused; dlopens GL at runtime |
| libgcrypt | 1.7.0 | `USE_GCRYPT` (WebCrypto, PAL crypto), `OptionsWPE.cmake:447` |
| libsoup3 | 3.0.0 | **only network stack**: `USE_SOUP` is hard-wired for GTK/WPE (`Source/WTF/wtf/PlatformUse.h:82-84`) |
| libtasn1, libxkbcommon | | crypto key import; keymaps |
| libxml2 | **2.9.13** | we ship Apple libxml2-34.9 = **2.9.4** |
| sqlite3, zlib, FreeType ≥ 2.9 | | |
| C++ | **C++23** (`OptionsCommon.cmake:1`), GCC ≥ 13.1 or recent Clang (`WebKitCommon.cmake:135`; `Compiler.h` gates features on clang ≥ 19/21) | |
| CMake ≥ 3.20, Ninja, Perl, Python 3, Ruby, gperf, unifdef | host | all present on the host |

These are optional and can be turned off for us: GStreamer (`ENABLE_VIDEO`/`WEB_AUDIO`/
`MEDIA_STREAM`/`WEB_RTC`/`WEB_CODECS`/`MEDIA_RECORDER`), `ENABLE_GPU_PROCESS`
(depends on `USE_GBM`), `USE_LIBDRM`/`USE_GBM`, `ENABLE_WPE_PLATFORM_DRM`
(libinput/udev), `ENABLE_WPE_PLATFORM_WAYLAND`, `ENABLE_WPE_LEGACY_API`
(libwpe), `ENABLE_BUBBLEWRAP_SANDBOX` (already OFF off-Linux,
`OptionsWPE.cmake:136-144`), `ENABLE_SPEECH_SYNTHESIS`/`USE_FLITE`,
`USE_ATK`, `ENABLE_JOURNALD_LOG`, `ENABLE_INTROSPECTION`/`DOCUMENTATION`,
`USE_LIBBACKTRACE`, `USE_SYSPROF_CAPTURE`, `ENABLE_WEBDRIVER`,
`ENABLE_GAMEPAD` (libmanette), `ENABLE_XSLT` (libxslt), `USE_AVIF`,
`USE_JPEGXL`, `USE_LCMS`, `USE_WOFF2` (FreeType's own WOFF2 is used if it
was built with brotli, `OptionsWPE.cmake:282-292`), `ENABLE_WEBGL`,
`ENABLE_WEBXR`, `ENABLE_NOTIFICATIONS`, `ENABLE_PDFJS`, `ENABLE_SPELLCHECK`.

**Graphics: Skia only on trunk/2.54.** `USE_CAIRO FALSE`
(`OptionsWPE.cmake:451`, GTK `OptionsGTK.cmake:314`). Skia became the
default in 2.46 ([WPE 2.46 highlights](https://wpewebkit.org/blog/2024-wpewebkit-2.46.html),
[Phoronix](https://www.phoronix.com/news/WebKitGTK-2.46-Skia)). Cairo stayed
only as a big-endian fallback and is no longer developed. Igalia found
**CPU rendering faster than GPU on common embedded SoCs** and made it the
2.46 default. That is the mode we want. Skia ships inside WebKit
(`Source/ThirdParty/skia`, 3957 source files) and has NEON raster paths.
Pinning ≤ 2.44 to reuse our cairo 1.18.4 is possible, but it means an
unmaintained renderer and two years of unpatched security bugs. Not
recommended.

**The software-rendering path exists.** When `hardwareAccelerationEnabled`
is false, the WebProcess picks a `SharedMemory` swap chain
(`Source/WebKit/WebProcess/WebPage/CoordinatedGraphics/AcceleratedSurface.cpp:594-602`).
It renders with Skia CPU raster into a `ShareableBitmap` and sends
`DidCreateSHMBuffer` over IPC (`:413-439`). The UI process wraps the pixels
in a `WPEBufferSHM` (`Source/WebKit/UIProcess/wpe/AcceleratedBackingStore.cpp:153-166`)
and passes it to the `WPEView`'s `render_buffer` vfunc
(`WPEPlatform/wpe/WPEView.h:58,131`). That ARGB8888 buffer plus stride is
exactly the hand-off a GNUstep `NSView` needs (§2.4).

**Platform backends.** The new WPEPlatform API (2.0) has `drm` (KMS),
`wayland` and `headless` displays (`Source/WebKit/WPEPlatform/wpe/`). None
is X11. Displays are GObject types registered on a GIO extension point
(`WPEExtensions.cpp:50`) and selected by `WPE_PLATFORM`/`WPE_DISPLAY`
(`WPEDisplay.cpp:291-318`). A third-party display (ours:
`WPEDisplayGNUstep`) can be registered from the embedding app itself. The
headless display tries EGL (GBM, then surfaceless Mesa;
`WPEDisplayHeadless.cpp:101-160`), which we will not have. The SHM path
does not need EGL, but the "no EGL at all" case must be made non-fatal. The
MacPorts GTK port carries exactly this fix (`patch-egl-display-no-crash.diff`,
§1.2). The legacy libwpe + WPEBackend-fdo path needs Wayland + EGL: no-go.

**Linux-specific parts and how WPE handles a non-Linux Unix:**

- epoll/eventfd/inotify: none required. GLib's main loop uses `poll()`,
  and GIO file monitoring picks **kqueue** on Darwin (the GLib configure
  trial found `kqueue` and used `file_monitor_backend: auto`).
- memfd: `SharedMemoryUnix.cpp:87` uses `memfd_create` on Linux and falls
  back to `shm_open` (`:102-111`). XNU-7195 POSIX shm works, with one
  quirk: **only one `ftruncate()` per shm object**
  (`bsd/kern/posix_shm.c:590-594`). WebKit sizes once, which is compatible.
- seccomp/bubblewrap/xdg-dbus-proxy: Linux-only, already OFF off-Linux.
- D-Bus: only for optional features (MPRIS media session, a11y,
  notifications, portals). It can all be compiled out.
- systemd/journald: optional (`ENABLE_JOURNALD_LOG`).
- Process launch: `ProcessLauncherGLib.cpp` uses `g_subprocess_new`
  (posix_spawn/fork underneath) and passes one socket fd. Its `OS(LINUX)`
  blocks only add memfd/pidfd extras.
- IPC: `Platform/IPC/unix/ConnectionUnix.cpp` already has `OS(DARWIN)`
  cases: `MSG_NOSIGNAL 0` (`:49-50`), and it avoids `SOCK_SEQPACKET` on
  Darwin (`:53-56`). It sends fds with `SCM_RIGHTS` (`:263,424`). XNU's
  AF_UNIX datagram buffers are tiny: `unpdg_sendspace = 2 KB`,
  `unpdg_recvspace = 4 KB` (`bsd/kern/uipc_usrreq.c:865-866`). MacPorts had
  to raise them with `setsockopt` (`patch-ipc-darwin-sock-dgram.diff`).
- Threads/GC: on `OS(DARWIN)`, WTF suspends threads and reads their
  registers with Mach `thread_suspend`/`thread_get_state`
  (`wtf/posix/ThreadingPOSIX.cpp:578-692`). That is a **good** fit: it is
  real XNU, the same mechanism Apple's builds use, and it needs no signal
  tricks.

### 1.2 WebKitGTK (X11)

Same WebCore/WebKit2/JSC/Skia/soup stack as WPE, plus GTK3 (or 4), GDK X11,
Pango, gdk-pixbuf, ATK, at-spi and their dependencies. **It is proven on
Darwin:** MacPorts ships `webkit2-gtk` **2.52.4** with an `x11` variant
([port page](https://ports.macports.org/port/webkit2-gtk/details/),
[Portfile](https://github.com/macports/macports-ports/blob/master/www/webkit2-gtk/Portfile)).
It uses C++23 and Clang ≥ 16, with `-DENABLE_BUBBLEWRAP_SANDBOX=OFF
-DENABLE_WEBGL=OFF -DUSE_GBM=OFF -DUSE_LIBDRM=OFF ... -DUSE_SYSTEM_MALLOC=OFF`
and 20 patches. The patches that matter to us:

- `patch-bmalloc-kern-not-found.diff` (macOS < 12): the exact blocker hit
  in §5.1.
- `patch-wtf-apple-cocoa-missing-files.diff` + `OSLogPrintStream.cpp`,
  `stub-TypeCastsOSObject.h`: WTF on Apple assumes Cocoa files.
- `patch-bmalloc-processcheck-darwin-gtk.diff`: `ProcessCheck.mm` uses
  Foundation (we hit it too: `NSBundle`/`NSProcessInfo` imports).
- `patch-webkit-ipc-darwin-gtk.diff`: prefer `USE(UNIX_DOMAIN_SOCKETS)` over
  Mach/XPC attachments, and stub the `xpcConnection()`/audit-token
  methods.
- `patch-ipc-darwin-sock-dgram.diff`: raise AF_UNIX DGRAM buffers to 256 KB.
- `patch-egl-display-no-crash.diff` + `stub-epoxy-egl.h`: survive with no
  EGL and fall back to CPU rendering.
- `patch-wtf-mach-vm-types-include.diff`,
  `patch-bmalloc-pthread-self-is-exiting.diff`,
  `patch-bmalloc-disable-tzone.diff`.

So a non-Cocoa Darwin WebKit with CPU rendering is a maintained,
downstream-patched reality, not a hypothesis. **WPE should take the same
patch set** (most of it is in WTF/bmalloc/IPC, shared by both ports).
WebKitGTK itself costs GTK and its whole stack, and it gives a second
toolkit and main loop inside a GNUstep app. Embedding a GtkWidget into
GNUstep would need XEmbed, which gnustep-back does not implement (no
`XEMBED` anywhere in `third_party/gnustep-back`). **GTK is the fallback if
WPE's platform layer fights us, not the plan.**

### 1.3 Apple's Mac port / WebKit2-Cocoa

It targets Darwin, but on top of closed frameworks. The CMake Cocoa build
(`OptionsCocoa.cmake`, `Source/WebCore/PlatformCocoa.cmake`,
`Source/WebKit/PlatformCocoa.cmake`, `PAL/pal/PlatformCocoa.cmake`) links
Accelerate, AppKit/Cocoa, AVFoundation, AudioToolbox, CFNetwork,
CoreAudio, CoreGraphics, CoreImage, CoreMedia, CoreText, CoreVideo,
ImageIO, IOSurface, Metal, Network, PDFKit, QuartzCore, Security,
UniformTypeIdentifiers, VideoToolbox and more (43 frameworks). The WebKit2
Cocoa process launcher uses XPC services/ExtensionKit
(`UIProcess/Launcher/cocoa/ProcessLauncherCocoa.mm`, `ExtensionProcess.mm`).

Open source in apple-oss-distributions or what we already have:
CoreFoundation (we ship it, from swift-corelibs), IOKit (IOKitUser),
SystemConfiguration (configd), libxpc (we ship PureDarwin's), Security
(open on apple-oss-distributions, but huge and macOS-centric). **Closed:
CFNetwork, CoreGraphics, CoreText, ImageIO, QuartzCore, IOSurface, Metal,
AppKit, AVFoundation, Network.framework, VideoToolbox, Accelerate.**
Reimplementing CoreGraphics + CoreText + CFNetwork is a multi-year project
on its own. **No-go.**

### 1.4 WebKitLegacy (WebKit1, single process)

Single-process is attractive for 1 GB of RAM. But WebKitLegacy is built
only by the Cocoa port (`OptionsCocoa.cmake:205`; JSCOnly, Win and
PlayStation set it OFF), and its sources are `mac/`, `ios/`, `cf/`,
`PlatformCocoa.cmake`. It has the same closed-framework dependency as §1.3.
The GTK port's WebKit1 was removed years ago. **No-go.** The modern
equivalent of "less process overhead" is to keep WebKit2 but run few
processes (§2.3).

### 1.5 JavaScriptCore without a JIT

- Configuration used in the trial: `-DENABLE_JIT=OFF -DENABLE_C_LOOP=OFF`.
  That gives the **LLInt** (offlineasm-generated arm64 assembly
  interpreter, `llint/`, `offlineasm/`), which needs **no executable
  memory**. `ENABLE_C_LOOP=ON` (the portable C++ interpreter) is a slower
  fallback and is not needed on arm64.
- **Speed cost**, from WebKit's own tier measurements on JetStream 2
  ([Speculation in JavaScriptCore](https://webkit.org/blog/10308/speculation-in-javascriptcore/)):
  Baseline and DFG are each "more than 2×" the tier below, and FTL 1.1× DFG.
  So **LLInt-only is about 4.5–5× slower** than full JSC overall. On hot
  numeric code (gaussian-blur: Baseline 3× LLInt, DFG 6× Baseline,
  FTL 1.6× DFG) it is **about 29× slower**. Per-bytecode costs: LLInt
  3.97 ns, Baseline 1.71 ns, DFG 0.349 ns, FTL 0.225 ns.
- **Could we JIT later?** The kernel allows it: on this embedded XNU,
  `VM_MAP_IS_ALIEN()` is always true when `!XNU_TARGET_OS_OSX`
  (`osfmk/vm/vm_map.h:1680-1692`). So W+X mappings get X *stripped*, not
  failed (`VM_MAP_POLICY_WX_STRIP_X`, `vm_map.c:2487-2500`), except an
  `entry_for_jit` (MAP_JIT) entry, one per map (`vm_map.c:2683-2690`).
  `mmap(MAP_JIT)` needs `MAP_ANON|MAP_PRIVATE` (`bsd/kern/kern_mman.c:320-328`).
  The entitlement check lives in AMFI's MAC hook, and this port has no AMFI
  (`docs/sudo-su-dyld-regression.md:4`). But JSC itself refuses to JIT on
  Darwin unless the process has the `dynamic-codesigning` entitlement
  (`jit/ExecutableAllocator.cpp:137`), and `pthread_jit_write_protect_np` is
  macOS-only and needs APRR hardware that the A53 lacks
  (`third_party/libpthread/src/pthread.c:1155-1176`). A JIT experiment
  would mean patching that gate and running a single RWX region with
  `sys_icache_invalidate` (also missing from our libplatform exports today).
  That is a later, separate risk item. Plan on the interpreter.

## 2. Best candidate (WPE): dependency matrix and Darwin hazards

### 2.1 Dependency matrix (vs. what `tools/userland_staging` ships today)

S ≈ ≤ 2 days, M ≈ 1–2 weeks, L ≈ > 2 weeks, all agent-time with the usual
build-script + smoketest loop.

| Dependency | State | Effort | Notes |
|---|---|---|---|
| libc++/libc++abi ≥ 19 | **have LLVM 11.1** (`setup_third_party.sh:937`) | **M, shared** | C++23 headers plus the 39 runtime symbols in §5.2. Upgrade the system `libc++.dylib` (ABI v1 is stable, so it is a superset for old images). Then run `weak_def_audit.sh` and a no-arg `bind_target_audit.sh` across all ~105 images. |
| Host compiler | Xcode-12 clang 12: too old | S | Use the daily Xcode clang (Apple clang 21) with the Xcode-12 iPhoneOS 14.4 SDK as sysroot. This works (§5). |
| ICU ≥ 70 | have Apple ICU-66109 (66.1) | S–M, shared | Bump to a newer Apple ICU tag (macOS 14/15 era, ICU 74); `build_icu_host.sh`/`build_icu_target.sh` exist. JSC needs 12 newer APIs (§5.2). Check CF/launchd consumers. |
| libxml2 ≥ 2.9.13 | have 2.9.4 (Apple libxml2-34.9) | S, shared | Newer Apple tag, or disable XSLT and audit. |
| GLib 2.70+ (+pcre2, proxy-libintl, libffi) | missing | **S–M** | Trial: configured and linked clean for arm64-apple-ios14.4 in 19 s; 8 missing exports (§5.3). Meson cross file; GLib skips Cocoa/Carbon when `subsystem != 'macos'` and explicitly rejects GNUstep (`glib/meson.build:1007-1036`). |
| libsoup3 (+libpsl, nghttp2, sqlite3, brotli) | missing | M | Meson; GIO-based. |
| TLS: glib-networking + OpenSSL 3 (or GnuTLS) | **no TLS library at all** (sshd is `--without-openssl`, `build_sshd.sh:16-29`) | M–L | OpenSSL 3 is the simpler cross-build (perl Configure, `ios64-xcrun`-like target); asm OK for arm64. The glib-networking OpenSSL backend is a GIO module loaded with `dlopen` (we have real `dlopen`). |
| CA bundle `/etc/ssl/cert.pem` | **missing** (no ca-bundle anywhere in `inject_into_sd_image.sh`/build scripts) | S | Mozilla bundle (curl's `cacert.pem`), pinned plus SHA-256, as `build_fonts.sh` does for DejaVu. |
| HarfBuzz (+ICU) | missing (FreeType built without it, `build_freetype.sh:24-25`) | S | Rebuild FreeType with HarfBuzz + brotli (WOFF2). |
| libjpeg-turbo, libwebp | missing (GNUstep gui built `--disable-jpeg`) | S each | |
| sqlite3 | missing | S | |
| libgcrypt + libgpg-error, libtasn1 | missing | M | libgcrypt asm/cross-config is fiddly; it can be built without asm. |
| libepoxy | missing | S | Builds without GL; WPE needs the headers/symbols only. |
| libxkbcommon | missing | S–M | Data from our pinned `xkeyboard-config`. Alternative: a custom `WPEKeymap` fed from GNUstep key events. |
| FreeType, fontconfig, expat, zlib, libpng | **present** (2.14.3, 2.17.1, 2.2.8, 1.2.11, 1.6.58) | — | Fonts: DejaVu (22 faces) + Inter (6) + JetBrains Mono (4) + GNU Unifont + Noto Emoji staged by `build_fonts.sh`. |
| Skia | in-tree (WebKit ThirdParty) | in WPE build | CPU raster only; `USE_SKIA_OPENTYPE_SVG` optional. |
| CoreFoundation, libdispatch, libxpc, objc4, gnustep-* | present | — | Not needed by WPE itself, except that `bmalloc/ProcessCheck.mm` pulls Foundation/libobjc on Apple (patch it out, like MacPorts does). |
| libSystem export gaps | partial | S | §5.2 list: malloc zone API, `memset_pattern*`, `sys_icache_invalidate`, `mincore`, ~43 libm float/long-double functions, `ldiv`/`lldiv`, `*at` syscalls, `getfsstat`, `lchmod`/`lchown`, `close$NOCANCEL`, CommonCrypto (`CC_SHA256`, `CCHmac`, `CCRandomGenerateBytes`), `CFNotificationCenter*`. |
| WebKit source itself | — | L | Fork `anurodhp/WebKit` (project rule) and pin the `wpewebkit-2.54.x` tag; clone `--depth 1 --filter=blob:none` with sparse checkout (no LayoutTests). |

### 2.2 Darwin-specific porting hazards in OUR environment

1. **WebKit's `OS(DARWIN)` means "a current Apple OS".** `OS(DARWIN)` comes
   from `__APPLE__ && __MACH__`, which our compiler always defines, so we
   cannot "be Linux". Trunk uses Darwin APIs newer than Darwin 20 / iOS 14.4:
   `os_unfair_lock_lock_with_flags` / `os_unfair_lock_flags_t`
   (`bmalloc/libpas/src/libpas/pas_lock.h:119-120`, 368 errors from one
   inline), `KERN_NOT_FOUND` (`pas_report_crash.c:232`,
   `pas_mar_report_crash.c:93`), and Mach exception
   `EXCEPTION_STATE_IDENTITY_PROTECTED` / `MACH_EXC_SERVER_TASKIDTOKEN_STATE`
   (`wtf/threads/Signals.cpp:148,277,379`;
   `wtf/PlatformJSCOnly.cmake` mig line). xnu-7195's `mach_exc.defs` has no
   `*_identity_protected` routines (they are in our `xnu-latest`, `:144-194`).
   Fix pattern: a small, cited `darwin20_compat.h` plus WTF `HAVE_*`
   overrides (e.g. old-style exception behaviors, or
   `HAVE_MACH_EXCEPTIONS 0` to take the POSIX-signal path). MacPorts proves
   this is maintainable for macOS < 12.
2. **libc++ version** (above). Two libc++ copies in one process (GNUstep on
   11.1, WebKit on 19+) would be the DAR-23/duplicate-static-copy class of
   bug. Upgrade the one system dylib instead.
3. **No Security.framework, CFNetwork, CoreText**: irrelevant to WPE (soup,
   OpenSSL, HarfBuzz/FreeType). They matter only for the Mac port.
4. **IPC**: unix sockets + `SCM_RIGHTS`, not Mach ports. Raise AF_UNIX
   buffers (above). XPC is not used once `USE(UNIX_DOMAIN_SOCKETS)` wins
   (MacPorts patch). Our libxpc is not on this path.
5. **Process model**: WebKit2 is always multi-process. The UI process (our
   GNUstep app) spawns a **NetworkProcess** (one) and **WebProcess**es (one
   per site/tab; the old shared-secondary-process mode is gone). WPE has no
   single-process build option. Keep it, but cap tabs and processes
   (`webkit_web_context`/`WebKitNetworkSession` memory-pressure settings:
   the WebProcess starts releasing at 33% / 50% of RAM on < 3 GB systems,
   [docs](https://people.igalia.com/aperez/Documentation/wpe-webkit-2.0/type_func.NetworkSession.set_memory_pressure_settings.html)).
   No sandbox (bubblewrap is Linux-only and OFF). The GPU process is OFF.
6. **launchd/dyld**: helper binaries go in
   `/usr/local/libexec/wpe-webkit-2.0/`, spawned with posix_spawn. Every
   new image needs `gap_audit.sh`, `bind_target_audit.sh` and
   `weak_def_audit.sh` (CLAUDE.md). The ICU and libc++ bumps are
   system-wide relinks.
7. **DNS/network**: libsoup resolves through GIO's `GThreadedResolver`, i.e.
   `getaddrinfo` in our `libinfo.dylib` + `libresolv.dylib` (DNS proven:
   DAR-182). There is no `/etc/resolv.conf` dependency beyond what
   ping/chronyd already use.
8. **Memory**: real Pi only after **DAR-412** (Done 2026-09-30: bootxnu now
   passes ARM RAM − phys_base instead of 128 MB). With 128 MB this project
   would be a no-go outright. Verify `sysctl hw.memsize` on the Pi before
   Phase 1.
9. **Shared memory**: one `ftruncate` per POSIX shm object (above).
   SHM-backed `ShareableBitmap`s are created at a fixed size, which is
   compatible, but test resize paths.

### 2.3 Multiprocess choice

Keep WebKit2's model. There is no supported in-process mode. Minimum
footprint configuration: one NetworkProcess, one WebProcess per tab (limit
3–4), no GPU process, no service-worker process unless needed, process
swap on navigation left on (it frees memory). Every process maps the same
`libWPEWebKit` text, so text pages are shared; RSS is dominated by heap and
JS.

### 2.4 GNUstep chrome integration

- **(i) Recommended: bitmap hand-off in-process (UI side).** The GNUstep
  app *is* the WebKit UI process. It links `libWPEWebKit-2.0` and
  `libWPEPlatform`, and registers a small `WPEDisplayGNUstep` /
  `WPEViewGNUstep` / `WPEToplevelGNUstep` (GObject, C). Its `render_buffer`
  receives a `WPEBufferSHM` (ARGB8888 bytes + stride, already in the UI
  process's address space). An `NSView` subclass (`GSWebView`) wraps it as
  a cairo image surface (gnustep-back's cairo backend) or an
  `NSBitmapImageRep`, draws the damaged rects in `-drawRect:`, then calls
  `wpe_view_buffer_rendered`/`buffer_released`. Input goes the other way:
  NSEvent → `wpe_event_pointer_*`, `wpe_event_keyboard_*`, `scroll`, focus,
  resize → `wpe_view_resized`. **Main-loop glue:** run the GLib
  `GMainContext` from the NSRunLoop by watching its poll fds with
  `-[NSRunLoop addEvent:type:watcher:forMode:]` (ET_RDESC) and a timer for
  the next GLib timeout. This is a known, contained pattern (S–M).
  Pixel path per frame is Skia raster (WebProcess) → SHM → cairo blit into
  the X window's back buffer → X server → PDGOP shadow → write-combined VRAM
  (`docs/framebuffer-mapping.md`). That is two to three full-damage copies.
- **(ii) Engine-owned X11 child window**: WPEPlatform has no X11 backend, and
  WebKitGTK would bring GTK. gnustep-back has no XEmbed. Raw
  `XReparentWindow` into the NSWindow's XID would work at the X level, but
  GNUstep would not know about it (focus, key events, resize). Writing an
  X11 WPEPlatform backend means (i) plus an XShm/XPutImage path anyway. **No.**
- **(iii) Separate top-level window for content**: poor UX. **No.**

**Existing GNUstep web views:** **SimpleWebKit/SWK** (goldelico) is a pure
Objective-C, WebKit-API-compatible HTML4 view that renders through
NSAttributedString/NSTextView. It is about 1 MB with no real CSS layout or
modern JS, and is used by the Vespucci browser
([wiki](https://mediawiki.gnustep.org/index.php/SimpleWebKit),
[project](https://projects.goldelico.com/p/swk/)). That makes it a
different engine. A 2017 webkit-dev thread proposed porting WebKit to
GNUstep as a GSoC project. WebKit asked for a downstream port, and nothing
survived ([thread](https://lists.webkit.org/pipermail/webkit-dev/2017-May/029078.html)).
**There is no usable GNUstep WebKit wrapper; we write `GSWebView`.**

**Chrome API.** Model it on WKWebView, backed by WPE's WebKitWebView API
(same API as WebKitGTK): `loadURL`/`webkit_web_view_load_uri`,
`goBack`/`goForward`, `reload`, `stopLoading`, KVO `title`, `URL`,
`estimatedProgress` (`estimated-load-progress`), `isLoading`,
`canGoBack`/`canGoForward` (back-forward list), find
(`WebKitFindController`), zoom (`zoom_level`), downloads (`WebKitDownload`),
and new-window and context-menu signals. Cost once `GSWebView` renders:

| Command | Cost |
|---|---|
| navigate/address bar, back/forward, reload, stop, title, URL, progress | S (API exists) |
| tabs (one WebKitWebView per tab, NSTabView) | S |
| find in page | S |
| zoom | S |
| downloads (save panel + progress) | S–M |
| history (persistent) / bookmarks | S–M each (app-side plist or sqlite) |
| cookies/cache persistence (`WebKitNetworkSession` data dirs) | S |
| TLS error UI (cert details, exceptions) | M |

## 3. Performance feasibility on a Pi 3

Known numbers:

- **Size.** Debian `libwpewebkit-2.0-1` 2.48.3 arm64: 29.2 MB download,
  **117.5 MB installed** (all features, GStreamer-linked)
  ([package](https://packages.debian.org/trixie/libwpewebkit-2.0-1)). Our
  trial JSC alone (LLInt, no wasm/JIT) is **15.6 MB** stripped. A stripped
  WPE (no media/WebRTC/WebGL/a11y/speech) should land around 50–70 MB of
  dylibs plus ~25 MB of dependencies (ICU data alone is 29 MB today,
  `libicudata.66.1.dylib`). That fits the SD card easily.
- **RAM.** Real WPE data points: a WebProcess on a Pi 3/4 was killed at an
  **80 MB** footprint by a 32-bit threshold bug
  ([bug 241012](https://bugs.webkit.org/show_bug.cgi?id=241012)). An i.MX8
  product budgets **140 MB WebProcess / 5 MB NetworkProcess**
  ([WPEWebKit#1569](https://github.com/WebPlatformForEmbedded/WPEWebKit/issues/1569)).
  Budget for us: UI process (GNUstep + WPE UI side) 40–60 MB, NetworkProcess
  15–30 MB, **WebProcess 60–150 MB per simple page** (more for JS apps).
  That gives **~200–300 MB for one text-heavy tab**, on top of Xorg +
  Window Maker + GNUstep services. It is OK with ~940 MB (DAR-412) and
  impossible at 128 MB. There is no swap, so jetsam/kill is the failure mode.
- **CPU.** 4× Cortex-A53 @ 1.2 GHz. Epiphany (WebKitGTK *with* JIT and GPU
  help) was rated "3/5" on a Pi 3 versus Chromium 4/5
  ([MagPi Pi 3 browser test](https://magazine.raspberrypi.com/articles/raspberry-pi-browser-test)).
  WPE 2.32 + Cog on a 64-bit Pi 3 ran a WebGL demo at ~15 fps on the
  VideoCore GPU (webkit-gtk list, 2021-08, "WPEWebKit goes half speed").
  We have neither the JIT nor the GPU, so expect noticeably worse than
  stock Raspberry Pi OS browsers.

Budgets, as estimates to be replaced by Phase 0/1 measurements:

| Path | Estimate on Pi 3 |
|---|---|
| Full-viewport Skia CPU raster, 1280×720, text page | ~30–100 ms (4 painting threads help) |
| SHM → cairo → X → shadow → VRAM, 3.7 MB/frame | ~10–30 ms per full-damage frame (A53 memcpy ~1 GB/s; QEMU shows nothing about this, `framebuffer-mapping.md`) |
| Scrolling (tile reuse, damage = newly exposed strip) | plausibly 10–20 fps on text pages |
| Page load, text-heavy site (Wikipedia article, docs, HN, lite news) | ~2–6 s, dominated by parse/style/layout + TLS on the A53 |
| JS-heavy SPA (Gmail, YouTube, modern news with ad stacks) | tens of seconds to unusable (LLInt ~5×, up to ~29×, slower than JIT) |

**Usable:** documentation, Wikipedia, HN/lobste.rs, plain blogs, man pages,
lite/text versions of news sites, local HTML. **Not usable:** heavy SPAs,
video, WebGL, anything that needs WebRTC.

Measure in Phase 0/1: `jsc` startup time and RSS, SunSpider/JetStream
subset times (LLInt), Speedometer if it fits; WebProcess/NetworkProcess
RSS idle and after N pages (`ps -o rss`, `top`); time to first paint and
to load-finished on a fixed page set; scroll fps (frames counted in
`render_buffer`); bytes copied per frame (`ShadowStats`).

## 4. Staged plan

| Phase | Deliverable | Effort | Exit criteria / measure |
|---|---|---|---|
| **0 — JSC on the Pi** | `anurodhp/WebKit` fork pinned at `wpewebkit-2.54.x`; `build_javascriptcore.sh` (JSCOnly, LLInt, `ENABLE_STATIC_JSC=OFF` → real `JavaScriptCore.dylib` + `jsc` against libedit); libc++ ≥ 19 system upgrade + relink audits; ICU bump; close the §5.2 libSystem/libm/CommonCrypto gaps; `darwin20_compat` patches committed in the fork | **1–2 weeks** | `jsc -e 'print(1+1)'` and a SunSpider/JetStream subset run on the Pi; record dylib size, startup ms, RSS, scores. `bind_target_audit.sh` clean. |
| 1 — dependency stack | GLib, libsoup3 (+psl, nghttp2, sqlite, brotli), OpenSSL 3 + glib-networking, CA bundle, HarfBuzz, libjpeg-turbo, libwebp, libgcrypt/gpg-error, libtasn1, libepoxy, libxkbcommon, libxml2 bump; each with a smoketest (e.g. a GIO `https://` fetch with cert validation on the Pi) | 3–5 weeks | `soup` HTTPS GET of a real site on the Pi with chain validation |
| 2 — WPE headless | WPE 2.54 with the stripped option set (§1.1 list OFF), MacPorts-derived Darwin patches, EGL-optional SHM path; a tiny `wpe-snapshot` tool (C, custom WPEDisplay writing the SHM buffer to PNG) | 4–6 weeks | PNG of a fixed page set rendered on the Pi; WebProcess/NetworkProcess RSS; first-paint time |
| 3 — GNUstep view | `WPEDisplayGNUstep` + `GSWebView` NSView (bitmap hand-off, input, GLib↔NSRunLoop glue) | 2–3 weeks | interactive page in an NSWindow under Window Maker; scroll fps; bytes/frame |
| 4 — Browser.app | /Applications/Browser.app: tabs, address bar, back/forward/reload/stop, find, zoom, progress, downloads, history/bookmarks, TLS error sheet; Eau theme | 2–3 weeks | daily-use smoke list of sites |
| 5 — later | JIT experiment (MAP_JIT + entitlement gate patch), WOFF2/AVIF, memory tuning, security-update cadence | open | |

**Total: ~3–5 months.** Host build cost: the JSC trial was **120.7 s wall,
612.7 s user** (M1, 8 cores, 8 GB, `-j8`, peak compiler RSS 840 MB, build
dir 183 MB). WebCore + WebKit + the Skia subset are about 8,000 more
translation units (WebCore 5,573, WebKit 2,552, Skia 3,957 source files at
the trial commit) and much heavier ones. Estimate **15–25× JSC's CPU**,
which is **~3–4 CPU-hours**. On this 8 GB host, RAM caps parallelism at
about `-j3`–`-j4`, since unified WebCore TUs commonly take 1.5–2.5 GB. That
means **~1–2 h wall per clean WPE build** and ~5–10 GB of build directory.
GNOME's own report has a WebKitGTK release build at ~34 min on a much
bigger machine
([On compiling WebKit](https://blogs.gnome.org/mcatanzaro/2018/02/17/on-compiling-webkit-now-twice-as-fast/)).
Rebuilding on every iteration is the main schedule risk, so use `ccache`.

**Risks:**

- Upstream WebKit's Apple assumptions keep moving. Each WPE bump will
  break the Darwin-20 shims again. MacPorts eats the same churn, so track
  its patch directory.
- The libc++/ICU system upgrades touch every C++/ICU consumer (launchd, CF,
  GNUstep).
- Skia CPU raster plus three copies per frame may be too slow on the A53 at
  720p. The fallback is to render at a lower resolution, or to use
  `ShadowFB off` with care.
- The interpreter-only JS speed makes the modern web mostly out of reach.
- Security: a browser needs prompt updates, which is hard with a
  downstream-patched pin.
- RAM: no swap. One heavy page can get processes killed.

**Recommendation: go later.** Do Phase 0 now: it is cheap, it de-risks
the toolchain (C++23 + libc++ + ICU + Darwin-20 shims), and it produces a
reusable JSC dylib. Make the Phase 1–4 go/no-go call on Phase 0's Pi
measurements (JSC speed, RSS) and with DAR-412 confirmed on hardware. Do
not start WPE before the libc++ and ICU upgrades land.

## 5. Host-only compile trials (done in this study)

All in the session scratchpad. Nothing was written to `libc_build`.
Toolchain: daily Xcode clang (`Apple clang version 21.0.0
(clang-2100.3.34.2)`), `-target arm64-apple-ios14.4`, sysroot Xcode-12
`iPhoneOS.sdk` (14.4). Xcode-12's own clang 12 cannot build WebKit
(C++23).

### 5.1 JavaScriptCore (WebKit trunk `315714135986`, 2026-10-01)

Configure: `cmake -G Ninja -DPORT=JSCOnly -DCMAKE_TOOLCHAIN_FILE=ios-arm64-xnu.cmake
-DCMAKE_BUILD_TYPE=Release -DENABLE_JIT=OFF -DENABLE_C_LOOP=OFF
-DENABLE_WEBASSEMBLY=OFF -DENABLE_SAMPLING_PROFILER=OFF -DENABLE_DFG_JIT=OFF
-DENABLE_FTL_JIT=OFF`, with ICU pointed at our ICU-66 dylibs. The generated
`cmakeconfig.h` confirmed `ENABLE_JIT 0`, `ENABLE_C_LOOP 0`,
`ENABLE_WEBASSEMBLY 0`, `ENABLE_REMOTE_INSPECTOR 0`. Blockers, in the order
hit:

1. **No libc++ headers**: the Xcode-12 iOS SDK has no `usr/include/c++`, and
   ours (LLVM 11.1) predates C++23. For the trial I used the daily Xcode
   MacOSX SDK's libc++ 22 headers (`-nostdinc++ -isystem .../c++/v1
   -D_LIBCPP_DISABLE_AVAILABILITY`); CMake then reports "LIBCPP 19+".
2. **libpas**: `os_unfair_lock_flags_t` / `os_unfair_lock_lock_with_flags`
   undeclared (`pas_lock.h:119-120`, 368 errors in one run), and `KERN_NOT_FOUND`
   undeclared (`pas_report_crash.c:232`, `pas_mar_report_crash.c:93`).
   Neither exists in the iOS 14.4 SDK or in our libplatform-254.80.2.
   Trial shim, force-included:

   ```c
   #define KERN_NOT_FOUND 56
   typedef unsigned int os_unfair_lock_flags_t;
   static inline void os_unfair_lock_lock_with_flags(os_unfair_lock_t l, os_unfair_lock_flags_t f)
   { (void)f; os_unfair_lock_lock(l); }
   ```
3. **MIG**: `mach/mach_exc.defs` is not in the iOS SDK. Fed it xnu-7195's
   `osfmk/mach` (`-I`); the defs have no TASKIDTOKEN routines, so this
   surfaced again at link (item 6).
4. **JSCOnly-on-Apple bitrot**: `wtf/cocoa/MemoryFootprintCocoa.cpp:36` "no
   matching function for call to 'memoryFootprint'" (switched to
   `generic/MemoryFootprintGeneric.cpp`); `wtf/darwin/OSLogPrintStream.mm:27`
   "This file requires ARC" (added `-fobjc-arc`; MacPorts replaces the file).
5. `jsc.cpp:146` needs `readline/history.h`. We ship libedit, which provides
   it; not wired in for the trial. Only the `jsc` shell is affected, not the
   library.
6. After that, **everything compiles**. Clean build: `120.74 real, 612.67
   user, 66.28 sys`, `-j8`, max RSS 840 MB. Link against the iOS 14.4 SDK
   stubs fails with exactly:
   - 11 libc++ runtime symbols newer than the SDK's libc++
     (`__atomic_wait_global_table`, `__atomic_notify_*`,
     `__atomic_monitor_global`, `__hash_memory`, `__libcpp_verbose_abort`,
     `basic_ostringstream`/`basic_stringbuf` vtables,
     `codecvt<char16_t/char32_t,char8_t>` vtables)
   - 12 ICU ≥ 67 APIs missing from ICU 66: `ubrk_clone`,
     `ucal_getIanaTimeZoneID`, `ucal_getTimeZoneOffsetFromLocal`,
     `udtitvfmt_formatCalendarToResult`, `unumrf_*` (7),
     `uplrules_selectForRange`
   - 3 MIG server callbacks (`catch_mach_exception_raise{,_state,_state_identity}`)
     from using the old defs
7. **Measured size** (link redone with `-undefined dynamic_lookup`, for
   measurement only): `JavaScriptCore` 23.05 MB unstripped, **15.57 MB
   `strip -x`**; `__TEXT` 14.27 MB, `__DATA` 4.96 MB, `__DATA_CONST`
   0.29 MB. Load commands: CoreFoundation, Foundation, libobjc, ICU, libc++,
   libSystem.

### 5.2 JSC imports vs. the dylibs we actually ship

`nm -u` of that dylib: 627 imports. Diffed against `nm -gU` of
`tools/userland_staging/libc_build/system/*.dylib` + `libobjc.A.dylib` +
ICU 66: **108 not exported today**:

- 39 libc++ (the 11 above plus all `std::__fs::filesystem` entry points:
  our LLVM 11 libc++ build exports none).
- 6 compiler-rt builtins (`__divti3`, `__modti3`, `__umodti3`, `__fixdfti`,
  `__floattidf`, `__exp10`): normally from static `libclang_rt.ios.a`.
- 43 libm: float/long-double variants (`expf`, `logf`, `log2f`, `tanf`,
  `sinhf`, `hypotf`, `cbrtl`, `erfl`, `tgammal`, …) plus `ldexp`, `log2`.
  Our `libsystem_m.dylib` exports only 103 symbols.
- malloc zones: `malloc_create_zone`, `malloc_zone_register`,
  `malloc_set_zone_name`, `malloc_zone_pressure_relief`,
  `malloc_zone_print`, `malloc_logger`. Our `libsystem_malloc.dylib`
  exports 63.
- libplatform/libc: `memset_pattern4/8/16`, `sys_icache_invalidate`,
  `mincore`, `ldiv`, `lldiv`.
- CommonCrypto: `CC_SHA256`, `CCHmac`, `CCRandomGenerateBytes`.
- CF: `CFNotificationCenterAddObserver`,
  `CFNotificationCenterGetLocalCenter`.
- Foundation: `NSBundle`, `NSProcessInfo` (bmalloc `ProcessCheck.mm`;
  patch out).
- `dyld_stub_binder` (the iOS 14.4 deployment target emits classic lazy
  binds; use chained fixups as our other dylibs do).

Every one of these has a real Apple/FreeBSD source we already pin (Libm or
FreeBSD msun, libmalloc, libplatform, CommonCrypto, swift-corelibs CF) or
is a compiler/linker setting. This is the concrete Phase 0 gap list.

### 5.3 GLib (GNOME/glib main, 2026-09-26)

Meson cross file (`system='darwin'`, `subsystem='ios'`, `kernel='xnu'`, same
clang/sysroot), `-Dtests=false -Dintrospection=disabled -Dlibmount=disabled
-Dselinux=disabled -Dxattr=false -Dnls=disabled`. Subprojects auto-fetched:
libffi, pcre2 10.46, proxy-libintl, gvdb. Configure found `kqueue`. Build:
**612/612 steps, 0 failures, 18.9 s wall**. Sizes (unstripped):
`libglib-2.0` 2.53 MB, `libgio-2.0` 2.94 MB, `libgobject-2.0` 0.59 MB,
`libgmodule-2.0` 0.07 MB. Imports not exported by our shipped dylibs (only
8): `close$NOCANCEL`, `faccessat`, `getfsstat`, `lchmod`, `lchown`,
`utimensat`, `memset_pattern16`, `sys_icache_invalidate` (libffi). Use our
own libffi rather than the subproject.

### 5.4 Not done

- A WebCore/WPE syntax spot check. It needs the whole dependency set's
  headers (GLib, soup, HarfBuzz, Skia config, generated bindings) to get
  past the first include, so it would measure missing headers, not Darwin
  blockers. The real Darwin blockers for WebCore/WebKit2 are documented by
  MacPorts' 20 patches (§1.2) and by §5.1 for WTF/bmalloc/JSC, which WebCore
  shares.
- Any target execution (no QEMU/Pi by instruction).

## 6. Draft Linear ticket (not filed)

**Title:** Browser Phase 0: JavaScriptCore (LLInt, no JIT) as a real dylib + jsc on the Pi

**Labels:** `userland-libc`, `dyld` (new: `browser`)  **Priority:** Low

**Description:**

Phase 0 of `docs/webkit-browser-feasibility.md` (branch `browser-study`).
The goal is the cheapest real WebKit milestone: WebKit's JavaScriptCore,
interpreter only, running on our Darwin.

What the study showed (host trial, WebKit trunk 315714135986): JSCOnly with
`ENABLE_JIT=OFF ENABLE_C_LOOP=OFF ENABLE_WEBASSEMBLY=OFF` compiles for
`arm64-apple-ios14.4` against the Xcode-12 iPhoneOS SDK (2 min on the M1).
The JSC dylib is 15.6 MB stripped. Against the dylibs we ship, it needs 108
symbols we do not export (list in §5.2).

Work:

* Fork WebKit to `anurodhp/WebKit` and pin the `wpewebkit-2.54.x` tag in
  `setup_third_party.sh` (depth 1, blobless, sparse: no LayoutTests).
  Commit the Darwin-20 fixes in the fork, never as patch files:
  * `os_unfair_lock_lock_with_flags` / `KERN_NOT_FOUND` compat (libpas)
  * old-style `mach_exc.defs` or `HAVE_MACH_EXCEPTIONS 0`
  * `MemoryFootprintCocoa` / `OSLogPrintStream` (JSCOnly on Apple)
  * drop Foundation from `bmalloc/ProcessCheck.mm`
  Each compat comment cites the real source per DAR-313.
* Upgrade the system libc++/libc++abi from LLVM 11.1 to LLVM ≥ 19
  (C++23). Then relink everything and run `weak_def_audit.sh` plus
  no-arg `bind_target_audit.sh`.
* Bump ICU to an Apple ICU ≥ 70 tag (the 12 missing `unumrf_*`/`ucal_*`/
  `ubrk_clone`/`uplrules_selectForRange` APIs) and re-verify CF/launchd.
* Close the libSystem export gaps:
  * libm float/long-double functions
  * malloc zone API
  * `memset_pattern*`, `sys_icache_invalidate`, `mincore`, `ldiv`/`lldiv`
  * CommonCrypto `CC_SHA256`/`CCHmac`/`CCRandomGenerateBytes`
  * `CFNotificationCenter*`
  * link compiler-rt builtins
* `tools/userland_staging/build_javascriptcore.sh`: CMake JSCOnly with a
  checked-in toolchain file (daily Xcode clang + Xcode-12 iOS 14.4
  sysroot) producing `/usr/lib/libJavaScriptCore.dylib` (or
  `/System/Library/Frameworks/JavaScriptCore.framework`) plus `jsc` linked
  to libedit. Gate it on `gap_audit.sh` + `bind_target_audit.sh`.
* Inject it into the SD image.

Verify on the real Pi (QEMU numbers are only indicative):

* `jsc -e 'print(1+1)'`
* a SunSpider/JetStream-subset run
* record startup ms, RSS (`ps -o rss`), scores and dylib size in the doc
* also confirm DAR-412 on HW: `sysctl hw.memsize` is about 940 MB

Done when `jsc` runs the subset on the Pi, the audits are clean, and the
numbers are in `docs/webkit-browser-feasibility.md`. That feeds the
go/no-go for Phase 1 (GLib/soup/OpenSSL stack) and Phase 2 (WPE headless).

## Sources

- WebKit trunk at `315714135986` (github.com/WebKit/WebKit), files cited inline.
- [WPE WebKit 2.46 highlights](https://wpewebkit.org/blog/2024-wpewebkit-2.46.html); [Phoronix: WebKitGTK 2.46 uses Skia](https://www.phoronix.com/news/WebKitGTK-2.46-Skia)
- [Speculation in JavaScriptCore (tier speedups)](https://webkit.org/blog/10308/speculation-in-javascriptcore/)
- [MacPorts webkit2-gtk port](https://ports.macports.org/port/webkit2-gtk/details/), [Portfile + patches](https://github.com/macports/macports-ports/tree/master/www/webkit2-gtk)
- [Debian libwpewebkit-2.0-1](https://packages.debian.org/trixie/libwpewebkit-2.0-1)
- [WebKit bug 241012 (Pi 3/4 WebProcess kill at 80 MB)](https://bugs.webkit.org/show_bug.cgi?id=241012); [WPEWebKit issue 1569 (140 MB WebProcess budget)](https://github.com/WebPlatformForEmbedded/WPEWebKit/issues/1569); [WPE memory-pressure settings](https://people.igalia.com/aperez/Documentation/wpe-webkit-2.0/type_func.NetworkSession.set_memory_pressure_settings.html)
- [MagPi Pi 3 browser test](https://magazine.raspberrypi.com/articles/raspberry-pi-browser-test)
- [On compiling WebKit (build times)](https://blogs.gnome.org/mcatanzaro/2018/02/17/on-compiling-webkit-now-twice-as-fast/)
- [SimpleWebKit](https://mediawiki.gnustep.org/index.php/SimpleWebKit), [SWK project](https://projects.goldelico.com/p/swk/), [webkit-dev: Port WebKit to GNUstep (2017)](https://lists.webkit.org/pipermail/webkit-dev/2017-May/029078.html)
- This repo: `third_party/xnu-7195` (`osfmk/vm/vm_map.{c,h}`, `bsd/kern/kern_mman.c`, `bsd/kern/posix_shm.c`, `bsd/kern/uipc_usrreq.c`), `third_party/libpthread/src/pthread.c`, `docs/framebuffer-mapping.md`, `tools/userland_staging/build_{freetype,sshd,fonts,gnustep_gui}.sh`, `setup_third_party.sh`, DAR-412.
