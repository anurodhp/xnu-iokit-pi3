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

**Started 2026-08-25**, ahead of the "only after Phase 6c" sequencing this
section used to recommend -- explicit user override, motivated by
unlocking a future PureDarwin `launchd` port. Phase A and Phase B (below)
are DONE and QEMU-verified; the real dyld port itself (Phase C) is not
started -- see that subsection for scope.

### Phase A + B: DONE 2026-08-25 -- toolchain + kernel plumbing proven

Before starting, the assumption was that both the toolchain's dylib-
producing link path AND the kernel's `LC_LOAD_DYLINKER` handoff were
completely unexercised and possibly needed real kernel work. Turned out
better than expected on both counts, confirmed by actually building and
booting real artifacts rather than reading source alone:

- **Toolchain**: this project's pinned Xcode-12 `ld64` needs zero special
  flags to produce real dynamic-linking load commands -- `-dynamiclib
  -install_name ...` alone gives a real `MH_DYLIB`/`LC_ID_DYLIB`
  (`tools/userland_staging/build_dylib_smoketest.sh`), and any non-`
  -static` link with at least one dylib on the command line automatically
  emits `LC_LOAD_DYLINKER /usr/lib/dyld` + `LC_LOAD_DYLIB`
  (`build_dyld_test_exec.sh`) -- no `-dylinker`/`-execute` flags needed on
  that side (those are for building the dylinker binary itself, see
  below). Existing static scripts (`tools/init_binary/build.py`,
  `tools/pwtool/build.py`, every other `build_*.sh`) are untouched, per
  the "parallel path, not a replacement" framing below.
- **Kernel**: `third_party/xnu-7195/bsd/kern/mach_loader.c`'s
  `load_dylinker()` is stock, unmodified Apple source and needed *no
  changes at all* -- confirmed via a real QEMU boot test with a throwaway
  stand-in dylinker (`tools/userland_staging/build_dyld_stub.sh`, built
  with `-dylinker -dylinker_install_name /usr/lib/dyld`, real `MH_DYLINKER`)
  installed at `/usr/lib/dyld` and a real dynamic test executable
  (`build_dyld_test_exec.sh`) referencing it. The kernel correctly:
  computes an independent ASLR slide for the dylinker separate from the
  main binary's own, maps it via the same `parse_machfile()` recursion
  every other image uses, overwrites the process's thread state with the
  dylinker's entry point, and reaches real user-mode execution there --
  verified end to end (`echo $?` after running the test binary from a
  real login shell returned the stub's own exit code, 42, and its
  `write()` output appeared on console).
- **Real first-exercise bug found and fixed (not a kernel bug)**: the
  first attempt crashed with a userspace `SIGSEGV` at a garbage address
  and zero console output. Root-caused via three rounds of temporary
  `printf` diagnostics in `mach_loader.c`/`kern_exec.c`/
  `bsd/uxkern/ux_exception.c` (added, used, then `git checkout`'d back
  out -- never committed) down to two real, distinct causes:
  1. `bsd/kern/kern_exec.c` only calls `exec_copyout_strings()`/
     `thread_setuserstack()` -- what actually initializes the new
     thread's `SP` -- when `load_result.unixproc` is true, and
     `load_dylinker()` does not propagate the dylinker's own `unixproc`
     onto the shared `result`. The first test executable had no valid
     `LC_MAIN`/`LC_UNIXTHREAD` of its own (an artifact of linking it
     without `third_party/Csu`'s startup code), so `unixproc` stayed
     false and the new thread's `SP` was pure garbage. Fix: give the test
     executable a real `LC_MAIN` by linking Csu's `start.s`/`crt.c` in
     after all (its own entry is never actually reached, but the load
     command's presence is what matters) -- see
     `build_dyld_test_exec.sh`'s header comment. Real Apple executables
     never hit this because they always link real crt1-equivalent glue
     and always carry a valid entry command of their own.
  2. Whenever `load_result.dynlinker` is true, the kernel unconditionally
     pushes an extra `mach_header` pointer *below* argc on the new stack
     -- the real ABI `third_party/dyld/src/dyldStartup.s`'s own
     `__dyld_start` expects and consumes. `third_party/Csu`'s `start.s`
     has no idea that slot exists (ordinary static binaries never get
     it), so reusing it for the throwaway dylinker stub silently misread
     the `mach_header` pointer as argc, corrupting all downstream pointer
     arithmetic. Fix: a small custom entry,
     `tools/userland_staging/dyld_stub_start.s`, that skips the extra
     slot instead of Csu's start.s (the stub's `main()` never uses
     argc/argv, so unlike real dyld it doesn't need to actually parse
     them, just consume the slot).
- New, additive-only build scripts (`build_dylib_smoketest.sh`,
  `build_dyld_stub.sh`, `build_dyld_test_exec.sh`, plus
  `libtest_dylib.c`/`dyld_stub_main.c`/`dyld_stub_start.s`/
  `dyld_test_exec_main.c` sources) live in `tools/userland_staging/`
  alongside every other tier. `tools/init_binary/inject_into_sd_image.sh`
  installs the stub at `/usr/lib/dyld` and the test executable at
  `/bin/dyld_test_exec` when built, matching every other tier's
  if-built-then-install pattern -- run `/bin/dyld_test_exec` from a real
  shell and check `$? == 42` to reproduce.

### Phase C: the real dyld port -- NOT started

- Real `dyld`'s own source (`apple-oss-distributions/dyld`, tag
  `dyld-832.7.3`) is already cloned at `third_party/dyld`, matching
  `setup_third_party.sh`'s pin -- tag-vs-`macos-112`-manifest re-
  confirmation still outstanding (do that first). Full repo is ~195K
  LOC, but the on-device runtime (`src/` + `dyld3/` excluding
  `dyld3/shared-cache/`) is ~53K LOC -- the shared-cache builder (~26K
  LOC) is host-side tooling for building the OS's shared dyld cache and
  doesn't apply here (this port has no shared cache; dyld3's on-device
  closure-builder path, which builds launch closures at process-launch
  time for dylibs *not* in a shared cache, is real existing Apple
  functionality for exactly this situation). `libdyld` (the libSystem-
  side `_dyld_*` shim) lives inside the same repo
  (`dyld3/libdyldEntryVector.*`), no separate pin needed. `libclosure`
  (`libclosure-78`) is already pinned and is a listed prerequisite in
  `build_libc_regex.sh`'s dependency-check pattern -- confirm it's
  already built before starting.
- This unlocks, in order: `libdispatch` as a real `.dylib` (Phase 6.6),
  then CoreFoundation (Phase 9), then everything gated behind either.
- Given the size (~53K LOC of real Apple C++), treat this as its own
  multi-session effort, one dyld subsystem at a time with a real
  incremental build+smoketest per step -- the same methodology as the
  earlier library-sweep phases, not a single push.

**CoreFoundation source options, noted 2026-08-24 (not yet evaluated in
depth -- flagging for whoever picks up Phase 9), found while a background
agent hit this exact wall trying to build `IOKitUser` (its `IOKitLib.c`
needs real `CFDictionaryRef`/`CFMutableDictionaryRef` throughout the API
surface itself, not just internals -- see the real IOKitUser build
script's own header comment)**:
- Apple's own real CF source does exist on `apple-oss-distributions`
  under the name `CF` (confirmed via `git ls-remote` -- NOT
  `CoreFoundation`, that name 404s) -- not yet checked against the
  `macos-112` manifest for an exact tag. Per this project's own source-
  priority rule this should be the first real option, but real Apple CF
  is notoriously tightly coupled to Mach ports/ICU/(in later versions)
  XPC -- expect this to be a substantial port even by this document's
  standards, not a quick pin-and-build.
- **GNUstep's `libs-corebase`** (https://github.com/gnustep/libs-corebase,
  LGPL) is a real, from-scratch, API-compatible CoreFoundation
  reimplementation, explicitly designed to be portable rather than
  Darwin/Mach-coupled -- real header layout mirrors Apple's
  (`Headers/CoreFoundation/CoreFoundation.h`, `CFRuntime.h`, etc,
  confirmed by browsing the repo, not assumed from the name alone). For
  a narrow need like `IOKitUser`'s (just `CFDictionary`/`CFString`/
  `CFArray`/`CFNumber` as matching-dictionary plumbing, not the full CF
  surface), this is plausibly a much more tractable starting point than
  porting real Apple CF's Mach/ICU/XPC entanglements just to unblock one
  narrow consumer. Not yet built or even trial-compiled here -- a real
  next step, not a conclusion.

## Phase 6.6 (libdispatch) -- expanded scope

Also currently a placeholder, blocked behind 6.5 there; still blocked
behind it here, plus:

- **Partially unblocked 2026-08-24**: `pthread_create()` is now real and
  QEMU-verified (a genuine second thread, real contended `pthread_mutex`
  under the real ulock path -- see project memory
  `project_xnu_rpi3_pthread_wireup_2026-08-24`), so the flat "needs
  `psynch_*`" framing below is stale -- this target routes contended
  mutex/cond through `ulock_wait`/`ulock_wake` instead, not `psynch_*`
  (which is still `ENOTSUP` and stays that way). What's still genuinely
  missing for real `dispatch_async`: no `pthread_join`/individual thread
  termination yet (`bsdthread_terminate` still `ENOTSUP`), and no real
  kqueue/`kevent` in userland at all (checked directly, zero hits) for
  kqueue-backed dispatch sources. Until those land, scope is still
  limited to single-thread-honest `dispatch_once`/`dispatch_sync`, but
  the pthread half of the blocker is smaller than this section
  previously stated.
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
