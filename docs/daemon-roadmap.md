# System daemon roadmap

Scope: every background daemon Apple has published as open source under
`apple-oss-distributions`, phased for this project's real constraints as of
2026-08-22 -- no `dyld` (every binary fully static), no `launchd` (a custom
hand-written static PID-1 in `tools/init_binary/` instead), no
`opendirectoryd`, no CoreFoundation, no `libdispatch`/GCD, no XPC, and only
partial Mach IPC (Phase 4.6 item 5 in `docs/roadmap.md`: blocking
`nanosleep(2)` is real and QEMU-verified; `task_for_pid`/full MIG RPC is
not). The user has confirmed the long-term intent is to actually build
`dyld`, `launchd`, and XPC -- this plan treats those as real infrastructure
phases to build toward, not a permanent ceiling the way Phase 7.5's
pthread/corecrypto gaps are.

`docs/roadmap.md` is the authoritative phase tracker; this file is the
detailed scoping doc it points to for the daemon work specifically, same
relationship `docs/usb-support-plan.md` has to Phase 6.

## Why order matters here

Every daemon beyond the simplest few is built against shared infrastructure
this project doesn't have yet. Porting a daemon before its real dependency
exists means either it silently becomes a much bigger project (re-deriving
CoreFoundation or XPC piecemeal, unscoped) or it ships as a gutted
reimplementation that isn't really "the real daemon" any more, contrary to
this project's own source-priority rule. So this plan is infrastructure
phases first, in real dependency order, then daemon phases that consume
each one -- not one big flat daemon list.

## Inventory (verified against apple-oss-distributions, not assumed)

| Daemon | Repo | Real dependencies | Tier |
|---|---|---|---|
| `cron` | [cron](https://github.com/apple-oss-distributions/cron) | none -- pure BSD/POSIX C | 0 |
| `ntpd` | [ntp](https://github.com/apple-oss-distributions/ntp) | none -- pure BSD/POSIX C (standard ntp.org codebase) | 0 |
| `notifyd` | [Libnotify](https://github.com/apple-oss-distributions/Libnotify) | Mach IPC only (notify transport is raw Mach ports, no CF) | 1 |
| `syslogd`/`aslmanager` | [syslog](https://github.com/apple-oss-distributions/syslog) | mostly POSIX C; some CF in newer log-store code -- core daemon can likely be built CF-light | 1 (see note below) |
| `configd` | [configd](https://github.com/apple-oss-distributions/configd) | CoreFoundation (SCDynamicStore is CF-typed), Mach IPC, XPC-adjacent IPC | 2 |
| `mDNSResponder` | [mDNSResponder](https://github.com/apple-oss-distributions/mDNSResponder) | core protocol engine (`mDNSCore`) is portable plain C by design; the Darwin daemon shell adds launchd/XPC/CF glue | 2 (core), 3 (daemon shell) |
| `diskarbitrationd` | [DiskArbitration](https://github.com/apple-oss-distributions/DiskArbitration) | heavy CoreFoundation (CFRunLoop-driven), IOKit notifications | 2 |
| `powerd` | [PowerManagement](https://github.com/apple-oss-distributions/PowerManagement) | heavy CoreFoundation, IOKit power APIs, XPC | 3 |
| `libdispatch`/GCD | [libdispatch](https://github.com/apple-oss-distributions/libdispatch) | Apache-2.0, genuinely portability-oriented (has non-Darwin backends upstream already) -- not a daemon itself, but the realistic first CF/XPC-adjacent piece to port | infra |
| `launchd` + XPC | [PureDarwin/XPC](https://github.com/PureDarwin/XPC) (preferred over plain `apple-oss-distributions/launchd`) | pure BSD/POSIX C `launchd-842.91.1` base + iXsystems's incomplete `libxpc` skeleton -- **same 2014/OS X 10.9.5 vintage** as Apple's own tag, 6+ major versions behind this project's Darwin 20.3/Big Sur userland (`macos-112` manifest); repo archived 2026-07-18, no license stated, several XPC functions are no-ops | infra, see below |

**Confirmed NOT open source anywhere in apple-oss-distributions** (checked
directly, not assumed): `usbmuxd` (Apple's real one -- the widely-known
`usbmuxd` is libimobiledevice's independent reimplementation, unrelated),
`watchdogd`, `distnoted`, `opendirectoryd`, `bootpd`, `WindowServer`,
`loginwindow`, `trustd`, `ocspd`, `amfid`, real XPC (`libxpc` does not
appear to be separately published under that name). `Security`/
`security_certificates` exist but are framework/cert-data, not the daemon
processes. None of these are plannable as ports; skip them entirely unless
a future session decides to write a from-scratch reimplementation (last
resort, not attempted here).

## Phase 5.5 additions (Tier 0 -- no new infra needed, can start now)

`docs/roadmap.md`'s existing Phase 5.5 already lists `cron` as item 3 with
the real repo unconfirmed; that's now resolved:

- **Item 3 (`cron`): source pinned, real binaries build, but the daemon
  crashes at startup -- shelved 2026-08-22, not shipped, not lost.**
  Real repo confirmed and pinned: `apple-oss-distributions/cron` (tag
  `cron-40`, matches macos-112) + `apple-oss-distributions/libutil` (tag
  `libutil-58.40.2`, needed for `cron`/`crontab`'s real `pidfile_open()`/
  `pidfile_write()`/`pidfile_close()`, not previously pinned). One real
  source patch, `third_party-patches/cron.patch`: `do_command.c`'s
  battery-check (real scope decision, no battery on this board) and
  per-user-launchd-bootstrap-switch (real scope decision, no launchd)
  blocks needed headers this project doesn't have -- see the patch's own
  commit message for the full writeup. `tools/userland_staging/
  build_cron.sh` (new) produces real, fully-static, zero-undefined-symbol
  `cron.macho`/`crontab.macho` -- run it directly any time to pick this
  back up, nothing here needs re-deriving.

  Three real, distinct runtime bugs found via QEMU crash + `write(2)`-
  checkpoint bisection (not guessed), all outside this port's own new
  code -- real, pre-existing gaps in this project's Libc/libplatform
  ports that `cron`/`crontab` are simply the first real callers to
  exercise:
  1. **FIXED**: `crontab.c`'s `COMPAT_MODE()` (real
     `Libc/gen/get_compat.c`'s `compat_mode()`) SIGSEGVs inside its own
     `pthread_once()` lazy-init -- real `_os_once()`
     (`libplatform/src/os/lock.c`) calls `_os_lock_owner_get_self()`,
     which needs per-thread TSD state this project's TSD bootstrap
     doesn't fully wire for a freshly-exec'd process's main thread (same
     class of gap `docs/roadmap.md` Phase 7.5 already documents for
     userspace `pthread_create()`). Fixed by hardcoding
     `#define COMPAT_MODE(a,b) (1)`, matching
     `shell_cmds/uname/uname.c`'s own real precedent for the exact same
     situation -- this project always builds `-D__DARWIN_UNIX03=1`, so
     `1` is the real answer regardless, not an approximation.
  2. **FIXED**: both `cron.c` and `crontab.c` call `setlinebuf(stdout)`/
     `setlinebuf(stderr)` at startup (real Apple source, unconditional
     under the real, always-defined `BSD` version macro from
     `sys/param.h`). Real `setlinebuf()` -> `setvbuf(..., _IOLBF, 0)`
     SIGSEGVs the first time its buffered-mode path is actually
     exercised anywhere in this project (every prior tier only ever hit
     `_IONBF`, which short-circuits before the crashing code). Root
     cause not fully chased to ground -- likely `FLOCKFILE()`/
     `_flockfile()` or `__swhatbuf()`, both real per-`FILE` machinery
     never exercised via this exact call shape before -- but the fix is
     real either way: both calls removed, stdout/stderr stay fully
     buffered (real data still reaches the terminal/log on every
     `fflush()`/exit, just not after every newline).
  3. **NOT FIXED, real blocker**: with both of the above fixed,
     `cron.c`'s daemon *still* crashes -- now inside `open_pidfile()`'s
     `snprintf(pidfile, sizeof(pidfile), PIDFILE, PIDDIR)` call, which
     happens immediately after this same process's own
     `signal(SIGCHLD` (or `SIGCLD)`, ...)` + `signal(SIGHUP, ...)`
     registrations succeed. Bisected further: a **trivial**
     `snprintf(buf, size, "%s", "hello")` inserted right before the real
     `PIDFILE`/`PIDDIR` call **also** crashes, which rules out anything
     PIDFILE/PIDDIR-macro-specific -- something about program state at
     that exact point is the real suspect. Leading hypothesis, not
     confirmed: this project's own new `_sigaction()`/`__sigtramp` signal-
     delivery plumbing (`tools/userland_staging/cron_extra_syscalls.c`,
     written this session to make `signal()`/`sigaction()` link and work
     at all -- and its own `signal()` *calls* do complete successfully,
     confirmed via checkpoints immediately before the crash) leaves
     stack/register state that only misbehaves on the *next* real
     function call, not at the point signal()/sigaction() itself
     returns. Not confirmed by disassembly or register capture -- this
     project's one-shot kernel "first abort" diagnostic dump is already
     consumed earlier in every boot (a real, unrelated kernel-side page
     fault), so no register state was ever captured for this specific
     userspace SIGSEGV. A future pass should either add a similar
     one-shot userspace-SIGSEGV register dump, or bisect the
     `__sigtramp`/`_sigaction`/`__platform_sigaction` chain itself with
     the same real write(2)-checkpoint method used to find the first two
     bugs.

  **Current state, ready for a future pass to pick up exactly here:**
  `tools/init_binary/init.c`'s `spawn_cron()` is written, real, and
  correct (fork+execve, fire-and-forget, matches launchd's
  `com.vix.cron.plist` role) but its call site is commented out with a
  full writeup at the top of `init_main()` -- crashing on every boot is
  worse than not running at all. `tools/init_binary/
  inject_into_sd_image.sh`'s `cron.macho`/`crontab.macho` install blocks
  are removed (not commented -- see that script's own note at the same
  location) so a normal SD image build doesn't ship either binary until
  bug 3 above is fixed and `crontab`'s own fixes get a real end-to-end
  QEMU confirmation (blocked this session by an unrelated host-side
  disk-image-journal issue on the test image, not by the code). To
  resume: re-run `tools/userland_staging/build_cron.sh` (still produces
  real working binaries right now), re-add the two install blocks to
  `inject_into_sd_image.sh` (removed, not deleted from git history --
  `git show` the removing commit for the exact original block), and
  re-enable `spawn_cron()`'s call site once bug 3 is actually fixed --
  don't re-enable it before then, a crashing daemon at boot is a real
  regression, not a neutral no-op.
- **New item 4: `ntpd`**. Real repo `apple-oss-distributions/ntp`, same
  pinning process. Also pure BSD/POSIX C. Needs this project's UDP socket
  syscalls (Phase 6b's INET work, already landed on `phase-6b-inet`) and a
  settable system clock path (`settimeofday(2)` -- confirm it's wired in
  `libc_gen_syscalls.c` before starting; if not, that's the real narrow
  blocker, not ntpd itself).

Both are genuinely startable today, independent of everything else in this
document -- they don't need dyld, launchd, Mach IPC, or CF. Do these first;
they're small, real, and immediately useful (scheduled tasks, correct
clock) without waiting on any of the infrastructure phases below.

## Phase 6.5 (dyld) -- expanded scope

`docs/roadmap.md` currently carries this as a planning-only placeholder.
Real scope, now that it's needed for more than its own sake:

- A real `.dylib`-producing link path (this project's toolchain currently
  only ever produces static Mach-O -- `tools/init_binary/build.py` and
  `tools/pwtool/build.py` explicitly assert full staticness, `otool -l`
  zero `LC_LOAD_DYLIB`; that invariant needs a *parallel* dynamic path, not
  a replacement -- the existing static binaries should keep building
  static).
- The kernel `LC_LOAD_DYLINKER`/`execve()` path that hands control to
  `dyld` before `main()` -- currently unexercised at all (this project's
  own crt0 comment already flags real `_start()`/`crt.c` as dead code on
  this static-only target -- see `docs/roadmap.md`'s `bash_syscalls.c`
  entry).
- Real `dyld`'s own source (`apple-oss-distributions/dyld` -- not yet
  fetched/inventoried by this document, do that first) needs the same
  tag-vs-`macos-112`-manifest confirmation as every other pin before
  cloning.
- This unlocks, in order: `libdispatch` as a real `.dylib` (Phase 6.6),
  then CoreFoundation (Phase 9), then everything gated behind either.

Recommend tackling this only after Phase 6c (DHCP/`ping`/`netstat`) and the
Tier 0 daemons above -- `dyld` is a genuine architecture shift touching the
kernel's exec path and this project's whole build toolchain, worth doing
with a stable, well-tested static baseline behind it first.

## Phase 6.6 (libdispatch) -- expanded scope

Also currently a placeholder, blocked behind 6.5 there; still blocked
behind it here, plus:

- Blocked behind Phase 7.5's pthread ceiling too, same as
  `docs/roadmap.md` already notes -- real concurrent `dispatch_async`
  needs real `pthread_create()`, which needs `psynch_*` (still open, see
  Phase 7.5's own "Real next step" writeup). Until then, scope is limited
  to single-thread-honest `dispatch_once`/`dispatch_sync`/kqueue-backed
  dispatch sources, exactly as already noted.
- Real upside once unblocked: `libdispatch` is Apache-2.0 and genuinely
  portability-oriented (existing non-Darwin backends upstream), making it
  the single most realistic "port this mostly as-is" piece among
  everything in this document -- unlike CoreFoundation or XPC, it wasn't
  designed Darwin-only.
- Likely a prerequisite for `launchd`+XPC (Phase 6.8) -- real Apple XPC
  dispatches onto GCD queues architecturally, though this hasn't been
  confirmed against PureDarwin's specific `libxpc` skeleton (verify when
  that phase starts, don't assume). Also a prerequisite for
  `mDNSResponder`'s/`configd`'s real daemon shells, which do use GCD
  directly, not just CF.

## Phase 6.7 (Mach IPC completion + `notifyd`) -- new

Builds on the already-real (but partial) Phase 4.6 item 5 work.

1. Finish the userland Mach IPC surface: `task_for_pid`/`task_info`/
   `task_threads`/`thread_info` (unblocks `ps`'s real CPU%/RSS/VSZ columns,
   `ps_task_info_stub.c`'s stand-in), and general MIG-stub generation
   robustness (`build_libmach_ipc.sh`'s `mig`-regeneration pattern already
   established, extend it rather than hand-deriving new wire formats).
2. Port `notifyd` (`apple-oss-distributions/Libnotify`) -- Tier 1, no CF/
   XPC/dyld dependency, just Mach IPC (already real for the routines it
   needs: port allocation, `mach_msg`). This is worth doing early and
   deliberately even before dyld/launchd exist: `notify_post()`/
   `notify_register_*()` are called pervasively by real Apple code this
   project will want to port later (including, per the inventory above,
   pieces of `configd` and `DiskArbitration`), so having a real `notifyd`
   in place removes a recurring blocker rather than deferring it into
   every later daemon port individually.
3. `notifyd` needs a way to *run* as a background service -- before real
   `launchd` exists (Phase 6.8), extend the custom PID-1
   (`tools/init_binary/`) to spawn it once during boot, matching the
   existing precedent of `getty` being built-but-not-yet-wired-in
   (`docs/roadmap.md` Phase 5.5 item 1). Don't block this phase on 6.8.

## Phase 6.8 (`launchd` + XPC via PureDarwin) -- revised 2026-08-22

Originally scoped as two separate phases (plain old `launchd`, then a
custom XPC shim as a last resort, since real Apple XPC is not open
source). Corrected after checking a real, specific alternative: **the
PureDarwin project publishes a combined `launchd`+`libxpc` source tree**
at [PureDarwin/XPC](https://github.com/PureDarwin/XPC) ("XPC and
Friends"). Verified directly against the repo, not assumed from its name:

- It's still built on `launchd-842.91.1` -- essentially the *same*
  2014/OS X 10.9.5-vintage base as `apple-oss-distributions/launchd`
  (`842.92.1`, one patch version apart). **This does not solve the
  vintage-skew gap** flagged in the original scoping below -- it's the
  same old launchd, just with XPC added on top, not a newer one.
- The `libxpc` half is **iXsystems's third-party "skeleton"
  reimplementation**, not Apple's real (unpublished) XPC internals. The
  repo's own README states it plainly: "the code in this repo compiles
  and runs but is currently untested. In particular, the implementation
  of xpc is incomplete, with several functions being no-ops." Connection-
  interruption/invalid-connection error handling is also called out as
  unimplemented, and audit logging is disabled.
- Architecturally, `liblaunch.dylib` becomes a thin proxy and
  `libxpc.dylib` carries the real logic -- a real, coherent design, just
  an incomplete implementation of it.
- **No explicit license is stated in the repo.** Confirm this before
  building anything on top of it -- Apple's own `launchd` portion carries
  Apple's APSL license regardless, but the added iXsystems `libxpc`
  skeleton's terms need checking directly (contact iXsystems/PureDarwin
  or find a LICENSE file in the actual clone) before treating it as
  freely portable, same diligence this project already applies to every
  other third-party source pin.
- **Repository archived 2026-07-18** (read-only, no further upstream
  fixes will ever land) -- whatever's there today is permanently the
  final state; any bugs found while porting it are this project's own to
  fix, not something to wait on upstream for.

Net effect on the plan: still recommended as the starting point over
writing a from-scratch XPC shim (per source-priority -- a real, if
incomplete and unmaintained, implementation beats inventing one), and it
collapses the old two-phase split (launchd, then separately XPC) into
one combined port, since the two are already integrated in this source.
But go in with accurate expectations, not the assumption that "has XPC"
means a complete or vintage-matched one:

1. **License check first** -- before cloning/building anything, confirm
   PureDarwin/XPC's actual license terms (the WebFetch pass that found
   this didn't surface one on the repo's main page).
2. **Port `launchd`+`libxpc` together** from this source rather than
   plain `apple-oss-distributions/launchd` alone -- same vintage-skew
   risk as originally scoped (expect real ABI/behavior mismatches against
   this project's much newer Darwin 20.3 kernel/libSystem, the same class
   of surprise `docs/roadmap.md`'s IONetworkingFamily entry already
   documents one instance of), now scoped as one migration instead of two
   sequential ones.
3. **Expect to finish real work `libxpc` left as no-ops** -- this project
   will likely need to complete some of what the skeleton stubbed out
   (error handling on interrupted/invalid connections was explicitly
   called out) for any daemon that actually depends on that behavior, not
   just link against it and assume it works.
4. **Fallback if the skeleton proves too incomplete for what's needed**:
   the original 6.9b plan (a narrow, purpose-built `xpc_*` shim scoped to
   exactly what one specific blocked daemon needs) is still available,
   now clearly a fallback rather than the default path.

Either way, this replaces `tools/init_binary/`'s current role as PID-1 --
a significant, carefully-staged migration, not a drop-in. Keep the custom
PID-1 as a fallback boot path during the transition (matches this
project's general caution around destructive/hard-to-reverse changes).

## Phase 9 (CoreFoundation) -- new

Unlocks every Tier 2 daemon (`configd`, `DiskArbitration`,
`mDNSResponder`'s daemon shell). Real repo:
[apple-oss-distributions/CF](https://github.com/apple-oss-distributions/CF).
Flagged by this document's own research pass as comparable in scope to
everything this project has built so far, combined -- CFRunLoop, the
whole CF type system (CFString/CFDictionary/CFArray/CFNumber/etc. with
their toll-free-bridging assumptions), property-list serialization, and
CFRunLoop's own real integration with Mach ports (`CFRunLoopSource`'s
Mach-port-backed variant) and `dyld`/`libdispatch` (CFRunLoop and GCD's
main queue are intertwined in real Darwin). Do not start this without
Phase 6.5 (dyld) and Phase 6.6 (libdispatch, at least the single-thread-
honest subset) already real -- CF assumes both exist underneath it.

No further sub-scoping attempted here; this needs its own dedicated
planning pass once Phases 6.5/6.6 are real and CF's actual source has
been surveyed the way this document surveyed the daemons themselves (real
tag confirmation against `macos-112`, real dependency-by-file audit, not
assumed from reputation).

## Phase 10 (Tier 2/3 daemons) -- new

Once Phase 9 (CF) is real, in rough dependency/value order:

1. **`configd`, real implementation.** Replaces the current hand-rolled
   kernel-side shortcut (`IONetworkStack` manually instantiated in
   `BCM2837DWC2::start()`, `docs/roadmap.md` Phase 6b) with the real
   userspace daemon -- real `SCDynamicStore`, real `InterfaceNamer`
   behavior (the exact piece this project's `IORegisterNetworkInterface()`
   call currently reimplements a sliver of by hand, see the commit history
   on branch `phase-6b-inet`), and real DHCP via the `IPConfiguration`
   bundle inside `configd` itself -- this is the direct, real fix for the
   "no DHCP client, `en0` has no IP" gap flagged when this document's
   research began. The current kernel-side `IONetworkStack` hack can
   likely stay as a bootstrap fallback (matches this project's general
   preference for keeping a working fallback during risky migrations)
   until real `configd` is confirmed working end-to-end.
2. **`DiskArbitration`/`diskarbitrationd`.** CFRunLoop + IOKit-notification
   heavy; real value once removable/USB storage matters beyond the current
   direct SD-card mount path.
3. **`mDNSResponder`'s Darwin daemon shell.** Its core (`mDNSCore`) is
   portable plain C by design (used in non-Darwin ports already) and could
   plausibly be exercised in a minimal non-CF host loop *before* Phase 9
   is fully done, if someone wants to investigate that shortcut -- flagged
   as a possible re-ordering worth a dedicated look, not committed to
   here since it wasn't verified this pass.
4. **`PowerManagement`/`powerd`.** Tier 3 (also wants XPC per the
   inventory) -- lowest real value for a Raspberry Pi target with no
   battery/sleep states to manage; deprioritize below the other three
   unless a specific need emerges.

## Explicitly out of scope (not open source, not planned)

`launchd`'s real modern (XPC-based) architecture, `opendirectoryd`,
`WindowServer`, `loginwindow`, the real `usbmuxd`, `watchdogd`,
`distnoted`, `bootpd`, `trustd`/`ocspd`/`amfid`. None of these have any
Apple-published source to port from. If ever wanted, each would need a
from-scratch reimplementation -- last resort per this project's source-
priority rule, not attempted or scoped here.

## Suggested immediate next step

`cron` (Tier 0, above) was attempted 2026-08-22: source pinned, real
binaries build, but the daemon itself has a real, unresolved startup
crash (SIGSEGV in `snprintf()` immediately after `signal()`
registration) -- see the item 3 writeup above for the full bug-by-bug
breakdown and exact resume point. `ntpd` remains fully untouched and
startable today with zero new infrastructure -- the more likely actual
next concrete task out of this document now, independent of every other
phase here and of cron's open bug.
