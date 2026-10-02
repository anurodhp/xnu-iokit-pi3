# Gershwin desktop as a Window Maker replacement: feasibility study

Date: 2026-10-01. Research plus a host-only compile trial. Nothing was
built into `tools/userland_staging/libc_build`, and nothing ran on QEMU or the Pi.

Sources examined (shallow clones, scratchpad only), at these commits:
`gershwin-windowmanager` 10b6797, `gershwin-workspace` 817491d,
`gershwin-components` c1d9095, `gershwin-eau-theme` 3d741fb,
`gershwin-developer` c7959dc, plus `gershwin-desktop`, `gershwin-system`,
`gershwin-systempreferences`, `gershwin-terminal`, `gershwin-assets` and `docs`
(all at their default-branch HEADs on 2026-10-01).

## TL;DR

- **Recommendation: go later, starting with the window manager only.** Port
  Gershwin's `WindowManager` as a second session choice next to Window Maker.
  Keep Window Maker as the default and the fallback until the Gershwin WM has
  passed on real hardware. Leave Menu, Workspace and LoginWindow for later,
  separate decisions.
- **The window manager is a good technical fit.** It is Objective-C on GNUstep
  plus raw XCB, with no dbus, systemd, udev, GL or Linux-only APIs on its
  required path. All 39 of its sources compile (syntax-only, then to arm64
  objects at -O2 with ARC) against our own objc4, gnustep-base 1.31.1 and
  gnustep-gui 0.32.0 headers. Once the objects are built, the symbols our
  shipped dylibs do not export are exactly: 49 symbols from XCB extension and
  xcb-util libraries we have not built yet, plus `___NSArray0__`.
- **Top blockers:**
  1. Blocks are not objects on our runtime (see section 3.1). Any message
     sent to a block crashes, and that includes `-retain` from ARC or from
     `NSTimer`'s block API. The WM uses both.
  2. Our libxcb ships only the core, shm and render libraries. The WM also
     needs the composite, damage, xfixes, randr, shape and present extension
     libraries, plus xcb-util, xcb-util-wm (icccm) and xcb-util-cursor.
  3. The global menu (`Menu.app`) is not a WM feature. GNUstep apps
     register with it from inside the **Eau** theme (`Eau.m:115-262`,
     `NSConnection` to `org.gnustep.Gershwin.MenuServer`). Eau is now our
     default theme, so the client code is already on the image; what is
     missing is Menu.app itself. Menu also hard-links libdbus.
- **Effort:** the WM alone as a session option is about **medium** (roughly
  1-2 focused sessions plus one real-hardware pass). The full Gershwin
  session (WM, Menu, Workspace, session supervisor) is about **large**.

## 1. What Gershwin is

**Project.** "Desktop Environment based on GNUstep welcoming to switchers"
([gershwin-desktop/gershwin-desktop](https://github.com/gershwin-desktop/gershwin-desktop),
README). The main maintainers are Joe Maloney (`pkgdemon`, GhostBSD) and Simon
Peter (`probonopd`, the AppImage author). The WM came from Alessandro
Sangiuliano's uroswm and XCBKit. Workspace came from Riccardo Mottola's
GWorkspace. The org-level repo has 512 stars. Every component repo has 1-24
stars. Contributor counts (from the GitHub API): WM 447 commits by
AlessandroSangiuliano and 223 by probonopd; Workspace 795 by rmottola and 513
by probonopd; components 1249 by probonopd.

**Activity.** The project is very active. Every repo's `dev` branch got
commits between 2026-09-28 and 2026-10-01. The default branches are merged
from `dev` about weekly; the last merge was 2026-09-17. Much of the recent
history is AI-assisted: the repos ship `AGENTS.md`, `Copilot` appears among
the contributors, and `gershwin-desktop` ships a Claude Code skill. The code
churns fast, so we need a pinned fork.

**Target platforms.** FreeBSD, GhostBSD, NextBSD, OpenBSD, Arch, Artix,
Debian, Devuan, Ubuntu, Void and Zorin (`gershwin-developer/README.md`). Live
ISOs exist for FreeBSD, NextBSD, Debian, Devuan and Arch. NextBSD (FreeBSD
plus Mach, launchd and `/usr/lib/system`) is the closest relative to this port.
Even there, Gershwin builds its own *non-Mach* libdispatch and runs on
**libobjc2** (`install-system-domain.sh:29-105`). There is no Darwin/Mach-O
target.

**Build and install.** Everything is gnustep-make (`GNUmakefile`, `make
install`, `GNUSTEP_INSTALLATION_DOMAIN = SYSTEM`) except libdispatch and
libobjc2, which use CMake. Their `tools-make` is configured with
`--with-layout=gershwin --with-library-combo=ng-gnu-gnu`
(`install-system-domain.sh:147-152`). The sources are 17 months newer than
ours. Gershwin pins libs-base 5bda522 and libs-gui 8f804fd (both 2026-07-26)
and libs-back bbcc3de (2026-07-24) (`checkout.sh` PINS), and carries 15 local
patches (`gershwin-developer/Library/Patches/*`). We run base 1.31.1 and
gui/back 0.32.0 (2025-02).

**Licences.** WM: MIT (`WindowManager/LICENSE`, Sangiuliano 2020; the repo
has no top-level licence). Workspace and SystemPreferences: GPL-2.0.
Components and desktop: BSD-2-Clause. Eau theme: LGPL-2.1. All are fine for
us.

**Repos and what each replaces in our stack.**

| Gershwin repo / component | What it is | Our current equivalent | Replace? |
|---|---|---|---|
| `gershwin-windowmanager` (`WindowManager.app`) | Reparenting X11 WM. ObjC/GNUstep app over XCB. Optional XRender compositor, Alt-Tab switcher, snapping, titlebars drawn by the active GSTheme | Window Maker 0.96 (anurodhp/wmaker `iokit-port`) | **Yes**, the subject of this study |
| `gershwin-components/Menu` | Global menu-bar server: GNUstep menus over DO, GTK/Qt menus over DBusMenu, plus menu extras (clock, battery, volume, WLAN) | None. Eau and `NSMacintoshInterfaceStyle` draw a per-app top menu | Later. Eau's client code is in place; needs a dbus stub |
| `gershwin-workspace` (`Workspace.app`) | GWorkspace fork (~155k lines of `.m` against ~99k in GWorkspace 1.0): file viewer, Desktop and **Dock**, Spatial mode, DS_Store interop | GWorkspace 1.0 (anurodhp/apps-gworkspace) | Later, optional |
| Window Maker dock/clip | (no separate Gershwin dock) | wmaker dock | Workspace's Desktop Dock covers it |
| `gershwin-components/gershwin-session` | 372-line Foundation tool that supervises and restarts `Workspace Menu WindowManager` (uses `ps`, not `/proc`) | xdm `Xsession` exec of wmaker | Cheap to adopt when Menu/Workspace come |
| `gershwin-components/LoginWindow` | GNUstep login greeter. Links `-lpam -lutil` | xdm 1.1.17 | **No**. Keep xdm (no PAM modules here, DAR-226) |
| `gershwin-systempreferences` | GNUstep SystemPreferences fork | none | Optional app |
| `gershwin-eau-theme` | Default Gershwin GSTheme; also the global-menu client | Eau (anurodhp/gershwin-eau-theme, our default) | Already adopted, see section 4 |
| `gershwin-terminal`, `gershwin-textedit` | Forks of the GAP Terminal and TextEdit | Terminal (gap), Ink | No need |
| `gershwin-system`, `gershwin-assets` | `/System/Library` prefs, fonts.conf, `Gershwin.sh` session script, artwork | our x11_config, stage_gnustep_mac_layout.sh | Cherry-pick only |

`Gershwin.sh` defines the desktop as `exec gershwin-session Workspace Menu
WindowManager`. It runs `dbus-launch` only when Menu is installed
(`gershwin-system/Library/Scripts/Gershwin.sh`).

## 2. The window manager

- **Language and structure.** About 28k lines of Objective-C in one
  gnustep-make `APP_NAME = WindowManager` target. XCBKit is merged in under
  `xcb/` (`ARCHITECTURE.md`). It is an `NSApplication` subclass
  (`UROSWMApplication`). `URSHybridEventHandler` puts the XCB fd into the
  NSRunLoop. It builds with `-fobjc-arc -fblocks -O0 -g -D_GNU_SOURCE`
  (`WindowManager/GNUmakefile`).
- **X11 libraries.** Link line: `xcb xcb-icccm xcb-xfixes xcb-aux xcb-cursor
  xcb-shape xcb-randr -lxcb-util -lX11 -lXext -lxcb-composite -lxcb-render
  -lxcb-damage -lxcb-shm -lxcb-present`. There is no EWMH library; it has its
  own `EWMHService`. There is no GLX/EGL/OpenGL and no Cairo of its own.
  Titlebars are drawn by calling the active GSTheme's
  `drawWindowBorder:...` into an `NSImage` (gnustep-back's cairo), converted
  to BGRA, and uploaded with banded `xcb_put_image`
  (`GSThemeTitleBar.m:65-99`, `URSThemeIntegration.m:845-930`,
  `URSImageUpload.h`). That is the generic GSTheme API, so it is not tied to
  Eau: there is no `Eau` reference anywhere in the WM.
- **Compositor.** Built in, optional, and **on by default** (`main.m:66`). It
  is switched off with `-dc`/`--disable-compositing`. It uses
  Composite + Damage + XFixes + XRender, with MIT-SHM and Present when they
  are available. If an extension is missing it falls back to plain
  reparenting (`URSCompositingManager.m:485-631`, "COMPOSITE extension not
  available"). No external compositor is needed.
- **Linux-isms.** None of these is a hard dependency:
  - `killOtherInstances()` scans `/proc/<pid>/exe` (`xcb/XCBConnection.m:139-160`).
    On Darwin `opendir("/proc")` fails and the function returns.
  - The Alt-Tab icon fallback reads `/proc/<pid>/cmdline`
    (`URSWindowSwitcher.m:676-681`). `fopen` fails and the code skips it.
  - There is no epoll, inotify, sysfs, evdev, udev, dbus, systemd, polkit or
    PAM. It defines its own `dispatch_block_t` typedef instead of linking
    libdispatch (`URSCompositingManager.h:15-16`).
- **Runtime features used.** ARC, `__weak`, `@synchronized` (5 sites), and
  blocks (6 literals, 11 block-typed sites). Two of those sites are
  `NSTimer ... block:` animation timers (`xcb/XCBFrame.m:1644-1646`,
  `xcb/XCBTitleBar.m:558`). It calls no libobjc2-only API (no
  `objc_msg_lookup`, `GSObjCRuntime` or `class_*` calls).
- **Relation to Window Maker.** None. It is an independent codebase (uroswm
  and XCBKit, MIT) and shares no code with wmaker or WINGs/wrlib.
- **Runtime cost.** As a GNUstep app it loads gnustep-base, gui, back, cairo,
  fontconfig and freetype. That is a much larger resident set than wmaker
  (C, wrlib). On our PDGOP DDX, XRender compositing is software pixman work
  inside the server, on top of ShadowFB's own copy to VRAM. With compositing
  on, every damaged window costs one extra full-surface composite on a Pi 3
  CPU, so **we should ship it with `-dc`**. Without compositing, titlebar
  redraws (one theme draw and a few hundred KB of PutImage per focus change)
  are cheap.

## 3. Dependency diff against what we ship

What we have was checked against `setup_third_party.sh` and
`libc_build/system/*.dylib`. Built today: libxcb, libxcb-shm, libxcb-render,
libX11, libX11-xcb, libXext, libXrender, libXfixes, libXrandr, libXcursor,
libXft, libXi, libXtst, cairo, pixman, libdispatch, libsystem_blocks
(libclosure), objc4, gnustep-base/gui/back, Eau.

| Need | Used by | Status here | Effort |
|---|---|---|---|
| `libxcb-{composite,damage,xfixes,randr,shape,present}` | WM | Not built. They are the same generated-file shape as shm/render in `build_libxcb.sh:63-97`. Headers were generated fine from our xcb-proto 1.17.0 in the trial | **Small** (extend the `for x in shm render` loops) |
| xcb-util (`xcb_aux`), xcb-util-wm (`xcb_icccm`) | WM | Not pinned | **Small** (two autotools libs, standard `build_<name>.sh`) |
| xcb-util-cursor (needs xcb-util-renderutil and xcb-util-image) | WM | Not pinned | **Small-medium** (three more libs) |
| Blocks as objects (`-retain`/`-copy` on a block) | WM (NSTimer block API, ARC block properties), Menu, Workspace | **Broken**, see section 3.1 | **Medium** (runtime work; benefits every GNUstep app) |
| `___NSArray0__` (clang's `@[]` singleton for an iOS 14 target) | WM `main.m:87` | Not exported by our gnustep-base | **Small** (export it from base, or use `[NSArray array]` in the fork) |
| libobjc2 / `-fobjc-runtime=gnustep-2.0` | Menu and components (`Menu/GNUmakefile`, AGENTS.md) | Impossible on Mach-O (DAR-382) | **Small**: drop the flag. Menu compiles on objc4 without it (section 6) |
| libdbus-1 | Menu (hard), Workspace (optional, `--disable-dbus`) | Not ported | **Small**: stub `DBusConnection.m`/`DBusMenuImporter.m`, the only files calling `dbus_*`. A real port is **medium** |
| libarchive as a dylib | Workspace (`Workspace/GNUmakefile.in:43`) | Pinned (`libarchive-83.40.4`) but only compiled into tar in `build_simple_cmds.sh` | **Small-medium** |
| SoundBackend / NetworkBackend | Menu extras | Shell out to amixer/OSS, nmcli/wpa_cli | Small to stub; they degrade on their own |
| libpam, libutil | LoginWindow | OpenPAM without modules | Not planned (keep xdm) |
| libs-corebase, libs-opal, libs-quartzcore | not needed by WM, Menu or Workspace. Gershwin itself removed corebase (`gershwin-developer` 19858a8, "breaks on arm64") | - | - |
| Gershwin's 15 libs-base/gui/back patches | Polish for Menu and DO (for example `nsrunloop-nested-performers`, `menu-transient`, `net-wm-pid`) | Not applied. Our fork is 17 months older | Medium, only if Menu/Workspace show the same bugs |

### 3.1 Blocks are not objects on our objc4 stack (real, cross-cutting blocker)

- libclosure's `_NSConcreteMallocBlock[32]` and `_NSConcreteGlobalBlock[32]`
  are zero-filled storage (`third_party/libclosure/data.c:22,25`). Their
  size of 32 words is exactly objc4's `OBJC_MAX_CLASS_SIZE` for in-place class
  construction with `objc_initializeClassPair()`
  (`third_party/objc4/runtime/objc-internal.h:56-60,83-89`).
- Nothing here ever builds a class into those buffers. gnustep-base's
  `GSBlocks.m:33` looks for libobjc2's `_NSBlock` and returns when it is
  missing.
- So any Objective-C message sent to a block dereferences a non-class isa.
  Our own Eau fork already hit this. Its comment
  (anurodhp/gershwin-eau-theme `iokit-port`, `GNUmakefile`) reads: "at -O0
  ARC emits objc_retain/objc_storeStrong on global blocks ... whose isa
  (_NSConcreteGlobalBlock, zero-filled data in libsystem_blocks) is not a
  class here: SIGSEGV in objc_msgSend in the first +load. -O2 drops them."
- `-O2` only hides the global-block case. `NSTimer`'s block initializer does
  `ASSIGN(_block, (id)block)`, a real `-retain` message
  (`third_party/gnustep-base/Source/NSTimer.m:151`). The WM's animation
  timers go through exactly that path.
- The proper fix: before first use, construct `__NSStackBlock__`,
  `__NSMallocBlock__` and `__NSGlobalBlock__` (subclasses of an `NSBlock` that
  forwards retain/release/copy to `_Block_copy`/`_Block_release`) in place in
  those buffers with `objc_initializeClassPair`. This could live in
  gnustep-base's `GSBlocks.m` or in a small libsystem_blocks companion.
  Then add a smoketest covering an NSTimer block, a block stored in an
  NSArray, and an ARC `-O0` global block.
- The workaround: rewrite the WM's 2 NSTimer blocks as target/selector timers
  and build with `-O2`. That is cheap but leaves the trap in place for Menu
  and Workspace (51 and 84 block literals).

## 4. Fit with our constraints

- **Mac layout.** Good fit. Gershwin itself installs into a Mac-like
  `/System` domain. The WM and Menu hard-code
  `*_INSTALL_DIR = /System/Library/CoreServices/Applications`, which matches
  our `/System/Library`. Our `stage_gnustep_mac_layout.sh` and `DESTDIR`
  installs have to own that path. LoginWindow's install rule also `cd`s into
  `$(INSTALL_APPS_DIR)` without `DESTDIR`; we don't build it, but it is a
  hazard.
- **Theme.** The WM draws titlebars through whatever GSTheme is active
  (generic `drawWindowBorder:`), so Eau titlebars should just work. Eau is
  our default GSTheme, as in Gershwin (`gershwin-system/.../NSGlobalDomain.plist`,
  `GSTheme = Eau`), and it carries the code that exports each app's menu to
  Menu.app. With Menu.app running, our `NSInterfaceStyleDefault =
  NSMacintoshInterfaceStyle` (`x11_config/gnustep/GlobalDefaults.plist`)
  per-app bar has to give way to the global one; check how Eau arbitrates
  that when Menu.app is ported.
- **Performance on a 4-core Pi 3 with ShadowFB.** Run with `-dc`. The WM
  process is single-threaded, like wmaker. Expect a slower WM start (a full
  GNUstep app launch) and a higher RSS. Nobody has measured either yet:
  `ps` prints honest zeros here, and memory visibility on the Pi is DAR-412
  (`build_top.sh`). Measure both in milestone M3 before deciding to remove
  wmaker.
- **Memory.** The WM, Menu and Workspace would each be a separate process
  linking the full GNUstep GUI stack, plus gdomap and gdnc (already present
  for DO). Against the RAM ceiling in DAR-412, the WM alone is the safe first
  step.
- **Workspace vs GWorkspace.**
  - Gershwin's Workspace is the same lineage as our GWorkspace and carries
    `__APPLE__` and kqueue branches (`Tools/fswatcher/fswatcher-kqueue.m`,
    `Workspace/GWProcessMonitor.m:46`, `Desktop/Dock/DockService.m:17`).
  - The cost: our DAR-392 objc4 changes (typed IMPs, 40 lines, 5dcd90c2)
    must be redone on a much larger, faster-moving tree.
  - It also adds libarchive, `-ldispatch` and the in-tree
    DSStore/GSMetadata subprojects.
  - It needs nothing from gnustep-gui beyond what GWorkspace already uses,
    apart from Gershwin's gui patches (unverified).
  - Not needed to remove wmaker. GWorkspace's own Dock plus Gershwin's WM
    is a complete session.
- **Boot flow (xdm).** No change to launchd → `xdm-launch` → xdm. Only the
  last line of `x11_config/xdm/Xsession` (`exec /usr/X11/bin/wmaker`)
  changes: pick the WM from a system default
  (`/etc/x11-session.wm`) with the existing `~/.xsession` override. Launch
  with `WindowManager -dc` plus `Terminal`/`GWorkspace` autostart. Today
  that autostart comes from wmaker's `autostart`
  (`x11_config/session/autostart`), so a Gershwin session needs its own
  launcher, either Xsession itself or `gershwin-session`.

## 5. Plan, risks, recommendation

**Recommendation: go later, starting with WM-only.** Window Maker works on
real hardware today and carries our tuning (-O2/NEON, kqueue, posix_spawn,
QoS: DAR-431/434/437/438/439). Gershwin's WM is a clean technical fit, but it
is young (repo created 2026-01-09), single-maintainer-heavy and changes daily,
and it depends on one runtime fix (3.1). Do the runtime fix first, because it
pays off for every GNUstep app anyway. Then add the WM as a session choice and
make the removal decision on real-hardware numbers.

Milestones, in order:

1. **M0, runtime (medium). DONE 2026-10-02.** libs-base 93983ab2a (`GSBlocks.m`) builds the three block classes in place and defines `__NSArray0__`/`__NSDictionary0__`; `foundation_smoketest` covers global/malloc blocks, NSArray storage, NSTimer block and `@[]` and passes on QEMU. Decision 2026-10-02: ship the WM with compositing off (`-dc`); wmaker removal only after Gershwin works on the Pi.
   Original scope: Block classes on objc4 (3.1), with a smoketest.
   Export `___NSArray0__` from gnustep-base (and check for `__NSDictionary0__`).
   Verify on QEMU with an NSTimer-block test app.
2. **M1, X libraries (small-medium). DONE 2026-10-02.** `build_libxcb.sh` now also builds composite, damage, xfixes, randr, shape, present and sync (present imports sync and dri3; dri3 is header-only); new `build_libxcb_util.sh` builds xcb-util (with xcb_aux), xcb-icccm, xcb-render-util, xcb-image and xcb-cursor. Pins added to `setup_third_party.sh`. Every dylib passes `bind_target_audit.sh`; nothing run yet.
   Original scope: Extend `build_libxcb.sh` with the
   composite, damage, xfixes, randr, shape and present libraries. Pin and
   build xcb-util, xcb-util-wm, xcb-util-renderutil, xcb-util-image and
   xcb-util-cursor, each as its own `build_<name>.sh` dylib, with
   `bind_target_audit.sh`.
3. **M2, WM fork and build (small).** Fork to
   `anurodhp/gershwin-windowmanager` `iokit-port`, pinned to a commit:
   `-O2 -fno-builtin-sin/cos` (as in our Eau fork), compositing off by
   default or `-dc` in the session, and the `@[]` fix if M0 doesn't export
   it. Write `build_gnustep_windowmanager.sh` on the `gs_build_app` pattern.
   Install into `/System/Library/CoreServices/Applications`, add it to the
   inject list, and run `bind_target_audit.sh`.
4. **M3, session choice (small).** Xsession picks `wmaker` (default) or
   `gershwin` (`WindowManager -dc` plus the Terminal/GWorkspace launch).
   Test on QEMU where possible; QEMU graphics are limited by DAR-443, so the
   real test is on the Pi. Check Eau titlebars, move/resize, Alt-Tab, focus,
   and wmaker-only behaviour we rely on. Record WM RSS and start time
   against wmaker (DAR-412 tooling).
5. **M4, decision point.** If M3 passes on hardware, make Gershwin the
   default and keep wmaker installed as the fallback session for one release.
   Remove wmaker (fork pin, `build_wmaker.sh`, `x11_config/wmaker`) only
   after that.
6. **M5, optional (large).** Global menu (Menu.app against Eau's client, plus
   a Menu fork with the dbus importers stubbed and `p_comm` →
   `kp_proc.p_comm`), `gershwin-session`, then Workspace as a GWorkspace
   replacement (libarchive dylib, redo of the DAR-392 objc4 fixes).

**Risks.**
- Upstream churn and AI-generated volume. Pin a fork commit, take updates
  deliberately, and expect regressions between pins.
- Runtime drift. Gershwin is developed only on libobjc2 with gnustep-base
  and gui from 2026-07. Behaviour our 2025-02 GNUstep lacks shows up at
  runtime, not at compile time. The compile trial cannot see it.
- The blocks fix touches every GNUstep process. Needs a careful smoketest
  and adversarial review.
- Compositing on ShadowFB is likely unusable on a Pi 3. Ship `-dc`; treat
  compositing as out of scope.
- Real-hardware-only validation (DAR-443 QEMU VRAM bug) slows the loop.
- Losing wmaker features nobody has listed yet (dock appicons, workspaces,
  keyboard bindings, our posix_spawn/kqueue launch path). Inventory them
  before M4.

**What to keep of Window Maker.** The whole thing as an xdm session choice
until M4 has held for a release. After that, nothing is load-bearing:
GWorkspace's Dock replaces the wmaker dock, and the Eau GSTheme covers the
look.

## 6. Host-only compile trial (macOS, scratchpad)

Setup (nothing written outside the scratchpad):
- **Headers.** Copied from `libc_build`: gnustep root headers, objc4 public
  headers, x11 include. XCB extension headers were generated with our
  `third_party/libxcb/src/c_client.py` from our `third_party/xcb-proto`.
  xcb-util, xcb-util-wm, xcb-util-cursor, xcb-util-renderutil and
  xcb-util-image headers came from fresh freedesktop clones.
- **Toolchain.** Xcode-12 clang, `-target arm64-apple-ios14.4`, iPhoneOS SDK.
  gnustep-make's apple-gnu-gnu defines (`-DGNUSTEP -DGNUSTEP_BASE_LIBRARY=1
  -DGNU_GUI_LIBRARY=1 -DNeXT_RUNTIME=1 -D_NATIVE_OBJC_EXCEPTIONS`) plus the
  WM's own `-fobjc-arc -fblocks`.

Results:
- **WindowManager: 39/39 files pass `-fsyntax-only`, and 39/39 compile to
  arm64 objects at `-O2 -Wall`.** The only diagnostic was one unused-variable
  warning. Every symbol the objects leave undefined was then checked against
  the exports of our shipped dylibs (gnustep-base, gnustep-gui, every
  `libc_build/system/*.dylib`, libobjc.A). What is left is exactly the 49
  `xcb_{composite,damage,xfixes,randr,shape,present,icccm,aux,cursor}_*`
  symbols (milestone M1) plus `___NSArray0__` (`main.m:87`, `@[]`). All
  GNUstep, GSTheme, X11 and libc references resolve against what we ship.
- **Menu.app** (extra trial): 35/39 files pass with
  `-fobjc-runtime=gnustep-2.0` dropped. The failures are
  `DBusConnection.m`, `DBusMenuImporter.m` (no `dbus/dbus.h`),
  `DBusMenuShortcutParser.m` (`libintl.h`) and `AppMenuWidget.m:965`
  (`kp.p_comm`, which is `kp.kp_proc.p_comm` on Darwin). All four are
  small fixes.
- **What this does not prove:** that anything links (we have no xcb-util
  dylibs) or runs. The blocks crash in 3.1 is a runtime failure that
  compiles clean.
- **Incidental finding:**
  `tools/userland_staging/libc_build/objc4_public_headers/objc/*` is 54
  symlinks into `/Users/anurodhp/Development/iokit-integrate/...`, a
  worktree that no longer exists. Any GNUstep build that includes
  `<objc/objc-class.h>` from that tree fails with "file not found" until
  `build_objc4.sh` is rerun or the links are repointed. This is the same
  hazard class as
  "worktree removal poisons the kernel build".

## 7. Draft Linear ticket (not created)

**Title:** Gershwin WindowManager as an xdm session choice (path to retiring Window Maker)

**Labels:** `x11gnustep`, `gitrepo` (new fork)

**Description:**

Port gershwin-desktop/gershwin-windowmanager (MIT, ObjC/GNUstep and XCB) as
a second X session next to Window Maker. Feasibility:
docs/gershwin-feasibility.md (branch gershwin-study).

Steps:
1. Blocks as objects on objc4. Build `__NSStackBlock__`, `__NSMallocBlock__`
   and `__NSGlobalBlock__` in place over libclosure's `_NSConcrete*` buffers
   (objc_initializeClassPair). Today any message to a block crashes
   (NSTimer.m:151 ASSIGN; see the Eau fork's -O2 comment). Add a smoketest.
   Export `___NSArray0__` from gnustep-base.
2. Add the libxcb composite/damage/xfixes/randr/shape/present libraries to
   build_libxcb.sh. Pin and build xcb-util, xcb-util-wm, xcb-util-renderutil,
   xcb-util-image and xcb-util-cursor as dylibs, each with
   bind_target_audit.sh.
3. Fork anurodhp/gershwin-windowmanager iokit-port at a pinned commit with
   `-O2 -fno-builtin-sin/cos`. Write build_gnustep_windowmanager.sh and
   install to /System/Library/CoreServices/Applications.
4. Xsession: choose wmaker (default) or `WindowManager -dc` with the
   Terminal/GWorkspace launch, via /etc/x11-session.wm or ~/.xsession.
5. Real-Pi test with Eau titlebars: move/resize/focus/Alt-Tab, plus RSS and
   start time against wmaker (DAR-412 tooling).

Out of scope: Menu.app global menu (Eau's menu client is in place; needs Menu.app
and a dbus stub), Workspace, LoginWindow (keep xdm), compositing on ShadowFB.

Done when the Gershwin session works on real hardware, wmaker is still
selectable, every new image passes bind_target_audit.sh, and everything is
reproducible from build scripts.
