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
relationship `docs/historic/usb-support-plan.md` (archived — USB/Ethernet
are done and real-hardware-verified) had to Phase 6.

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
| `notifyd` | [Libnotify](https://github.com/apple-oss-distributions/Libnotify) | Mach IPC only (notify transport is raw Mach ports, no CF); client-side `notify_register_*()` needs bootstrap-namespace resolution -- see ASL row below, same real dependency | 1 |
| ASL (`libsystem_asl` client + `syslogd`'s `dbserver.c`) | [syslog](https://github.com/apple-oss-distributions/syslog)'s `libsystem_asl.tproj/` | **REVISED 2026-08-27, was wrong**: not "mostly POSIX C" -- real `asl_open()` (`libsystem_asl.tproj/src/asl.c:271`) calls `bootstrap_look_up2(bootstrap_port, ASL_SERVICE_NAME, ...)`, the *same* bootstrap-namespace mechanism `notifyd` needs (see Phase 6.7 below), not just Mach IPC in the abstract. `syslogd`'s `dbserver.c` (the live-client-facing ASL service) additionally uses real XPC (`xpc_copy_entitlement_for_token` etc). Genuinely fundamental: `syslog(3)` itself is a thin wrapper over `asl_open()`/`asl_log()` (`libsystem_asl.tproj/src/syslog.c`), so anything calling plain `syslog()` -- including this project's own existing `ifconfig_syslog_compat.c`/cron's syslog fallback -- is really depending on ASL, not a separate simpler thing. **Correction**: this project's `docs/backlog.md` (CF/launchd port items) and `tools/userland_staging/ifconfig_syslog_compat.c`'s own header comment both previously said the real ASL client library was "confirmed never open-sourced" -- wrong; it's real, Apple-licensed, 14,555 lines, bundled inside this same `syslog` repo pin, just not under a repo named "ASL". | 1, shares Phase 6.7's bootstrap-registrar prerequisite with `notifyd` |
| `syslogd`/`aslmanager` (legacy compat path only) | [syslog](https://github.com/apple-oss-distributions/syslog)'s `syslogd.tproj/` | `bsd_in.c` (Unix-domain-socket `/var/run/syslog` listener, real `dispatch_source_t`-based, no Mach dependency) + `udp_in.c`/`klog_in.c`/`bsd_out.c` are genuinely lighter and Mach-free -- a real, useful legacy-only `syslogd` (no live ASL query service, no `dbserver.c`/`remote.c`) is a smaller, more tractable first cut than the full ASL daemon. See Phase 6.7 below for the real scoped plan. | 1 |
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

### Phase C, part 1: the real dyld port -- prerequisites (libcxx/libcxxabi)
### and the dyld-940 retarget -- STARTED 2026-08-25 (see "Phase C: CLOSED"
### below for how this phase actually finished)

Real `dyld` source (`apple-oss-distributions/dyld`, tag `dyld-832.7.3`)
is cloned at `third_party/dyld`, tag-vs-`macos-112`-manifest confirmed
exact (`git ls-tree macos-112 dyld` == commit `59e07dd...` ==
`third_party/dyld`'s own HEAD, confirmed via `git rev-parse HEAD`).

**Real re-scoping finding (2026-08-25, from reading dyld's own source, not
guessed)**: `gUseDyld3` (`dyld3/AllImages.cpp:66`, `src/dyld2.cpp:354`)
defaults to `false` -- the classic dyld2 `ImageLoader`-based runtime is
what actually runs unless `DYLD_USE_CLOSURES=1` is explicitly set or a
shared cache turns it on automatically. This port has no shared cache and
won't set that env var, so **the real, default runtime path for this
target is dyld2 (`src/dyld2.cpp` + `src/ImageLoader*.cpp`), not dyld3's
closure-builder** (`dyld3/ClosureBuilder.cpp`, `dyld3/MachOAnalyzer.cpp`,
`dyld3/AllImages.cpp`, `dyld3/APIs*.cpp` -- all dyld3-only, not needed for
the default path). This cuts the realistic target scope from ~53K LOC
(the full on-device runtime) down to roughly `src/dyld2.cpp` (7163 LOC) +
`src/ImageLoader*.cpp` (~9.4K LOC across 4 files) + `src/dyld*`
support files (`dyldInitialization.cpp`, `dyldNew.cpp`,
`dyldLibSystemGlue.c`, `glue.c`, `dyldExceptions.c`,
`threadLocalVariables.c`, `dyldLock.cpp`) + whatever subset of shared
`dyld3::MachOFile`/`MachOLoaded`/`Diagnostics`/`Logging`/`BootArgs`
parsing utilities dyld2-era code in this version still depends on --
confirm the exact dependency edges when actually compiling `dyld2.cpp`,
don't assume from grep alone. `libdyld` (the libSystem-side `_dyld_*`
shim) lives inside the same repo (`dyld3/libdyldEntryVector.*`), no
separate pin needed. `libclosure` (`libclosure-78`) is already pinned and
built.

**Real, larger-than-expected prerequisite found and closed (2026-08-25)**:
dyld's C++ source needs a working `libc++`/`libc++abi` to compile at all
(`std::string`, `std::vector`, `<iostream>`, etc. -- confirmed via grep
before assuming). Neither is open-sourced by Apple as a standalone repo
for this OS era -- confirmed directly, not assumed: `git ls-remote` 404s
on both `apple-oss-distributions/libcxx` and `.../libcxxabi`; PureDarwin's
org only has `libcppabi` (the ABI/RTTI support library, not the STL
itself). Per this project's source-priority rule, extended this session
to cover this case (see `CLAUDE.md`): the real fallback is **LLVM
upstream's own `libcxx`/`libcxxabi`** (Apache-2.0 w/ LLVM exception) --
the actual codebase Apple's shipped libc++ is downstream of, just not
packaged as its own apple-oss-distributions repo. Pinned
`llvmorg-11.1.0` (matching Xcode 12's Apple clang 1200.0.32.2), cloned via
a dedicated sparse-checkout in `setup_third_party.sh` (`libcxx`/`libcxxabi`
subdirs only -- `llvm-project` is a multi-GB monorepo, a plain full clone
would be senseless here).

**`libc++abi` DONE and QEMU-verified (2026-08-25,
`tools/userland_staging/build_llvm_libcxxabi.sh`)**: all 19 real, unmodified
`libcxxabi/src/*.cpp` files compile clean (`-std=c++14` -- C++17 mode
removes `std::unexpected_handler`, which `cxa_exception.h`/`cxa_handlers.h`
still use; RTTI must stay on, `private_typeinfo.cpp` IS the RTTI
implementation). Links against this project's own already-real, already-
built `libunwind.a` (`build_libunwind.sh`) for the underlying `unw_*`/
`_Unwind_*` engine -- not LLVM's own libunwind subproject, deliberately
not even cloned, per source-priority (Apple's real libunwind already
covers this). Real gap found and fixed along the way: this project's
custom minimal `malloc`/`free`/... dispatcher
(`tools/userland_staging/libmalloc_dispatch.c`, real Apple `malloc.c`'s
public-API-glue portion is deliberately not built wholesale, see
`build_libmalloc.sh`) never had a `posix_memalign()` -- needed by
libc++abi's C++17 aligned-`operator new` support, which the real headers
enable unconditionally for an iOS 14.4 deployment target regardless of
`-std=`. Added as a real, verbatim port of Apple `malloc.c`'s own
`posix_memalign()` body, dispatching through the same real zone vtable
(`malloc_zone_t`) every other function in that file already uses --
required manually extending that file's own partial/offset-preserving
struct redeclaration up through the real `memalign` field (see that file's
own comment) since it's a hand-trimmed ABI-sensitive view of the real
header, not the genuine one.

QEMU-verified real dynamic_cast/RTTI/`operator new`+`delete` all work
(`libcxxabi_smoketest.macho` -- real `Base`/`Derived` polymorphic
dynamic_cast, real heap allocation through the real magazine allocator).
Real, **honestly-scoped, not-yet-closed gap**: an actual `throw`/`catch`
in the same function does not currently unwind successfully on this
target's `-nostdlib -static` link -- `libc++abi: terminating with
uncaught exception of type int` is genuinely printed (proving `cxa_throw`,
the personality routine, and `cxa_demangle` all really run), but
`_Unwind_RaiseException`'s phase-1 search never finds the local catch.
This is a **different, deeper gap** than the already-documented
`build_libunwind.sh` static-link limitation (that one is ld64 dropping
`__compact_unwind`/`__unwind_info` entirely for static links with no real
`__eh_frame` either) -- this smoketest's binary genuinely has real,
non-empty `__eh_frame` (0x1d0 bytes) and `__gcc_except_tab` (0x24c bytes)
sections (`otool`-verified), so something in the actual DWARF-CFI parsing/
evaluation path is still wrong, not yet root-caused further. Not treated
as a blocker for this pass (see `llvm_libcxxabi_smoketest.cpp`'s own
header comment for the full writeup and why the smoketest is scoped the
way it is) -- a real, distinct follow-up for whoever picks up unwinding
next, worth root-causing before dyld code that actually relies on
exception-driven error handling (rare in dyld2 itself, which mostly uses
`Diagnostics`-style error returns, but present in some STL container
paths).

**libcxx: DONE, QEMU-verified (2026-08-25,
`tools/userland_staging/build_llvm_libcxx.sh`)**: all 36 real,
unmodified top-level `libcxx/src/*.cpp` files this project's target class
needs (matches `CMakeLists.txt`'s own non-Windows/non-Solaris file list
exactly) compile clean with the same `-std=c++14`/RTTI-on/
`-funwind-tables` recipe as libc++abi, plus `-DLIBCXX_BUILDING_LIBCXXABI`
(routes `exception.cpp`/`new.cpp` to defer to real libc++abi's
`cxa_default_handlers.o`/`cxa_handlers.o` instead of defining duplicate
`std::terminate`/`set_new_handler`/etc themselves). Explicitly out of
scope, matching this phase's own dyld2-vs-dyld3 shape of real, documented
gap rather than papered-over one: `std::filesystem`
(`filesystem/*.cpp`) and `experimental/memory_resource.cpp` -- dyld2
doesn't use either.

Three real, narrow gaps found and closed while linking the first
consumer that actually touches `std::string`/`std::shared_ptr` (none of
libc++abi's own smoketest exercised these):
- `strtoll`/`strtoul`/`strtoull` (real FreeBSD source,
  `third_party/Libc/stdlib/FreeBSD`) needed the same xlocale-adjacent
  include recipe `build_libc_locale.sh` already worked out for
  `strtol.c` -- that tier just never attempted these three siblings.
  Bundled straight into `libcxx.a` alongside a real, narrowly-scoped
  `wcstod`/`wcstof`/`wcstold` wrapper (`libcxx_wcstof_compat.c`) that
  converts the (always-ASCII) numeric prefix and delegates to the
  already-real gdtoa `strtod`/`strtof`/`strtold` -- real Apple/FreeBSD
  source for these three specifically needs `_simple.h`, missing from
  this checkout entirely (same already-documented gap
  `build_libc_locale.sh` itself skips these files for).
- `dlopen`/`dlsym`: only referenced by `refstring.h`'s optional
  libstdc++-COW-string interop probe; a real, correct "not available"
  answer on a target with no dyld at this tier (`libcxx_dlfcn_stub.c`).
- A genuine new duplicate-symbol collision: this is the first consumer
  to link both real `libpthread.a` (libc++abi's `cxa_exception_storage.o`
  needs real `pthread_key_create` for its thread-local exception-globals
  storage) AND `libc_locale_for_stdio.a` (needed for `libc_printf.a`'s
  wide-`printf` `mbrtowc_l`/`wcrtomb_l`) at the same time --
  `libc_locale_for_stdio.a`'s own now-superseded `pthread_tsd_shim.o`
  still defines `pthread_key_init_np`, colliding with real
  `libpthread.a(pthread_tsd.o)`. Same class of duplicate
  `pthread_tsd_platform_bootstrap.c` already documents and
  `build_libpthread.sh` already fixed once for `libmalloc.a` via `ar d`
  -- applied the identical fix to a **local scratch copy** of
  `libc_locale_for_stdio.a` (never the shared, committed archive; other
  consumers still need that member until the broader tier reconciliation
  that file's header comment flags as separate work actually happens),
  plus a tiny real no-op `pthread_tsd_shim_bootstrap()` compat
  (`libcxx_tsd_shim_bootstrap_compat.c`) for the one other symbol that
  member provided -- correct because the smoketest now calls the real
  `pthread_main_thread_bootstrap()` first, which already does the actual
  TPIDRRO_EL0 work idempotently.
- Separately, closed two small **pre-existing, real gaps in the already-
  committed `libpthread.a` tier itself** (`build_libpthread.sh`), found
  because this was the first consumer to actually need them:
  `___ulock_wait2` (syscall #544, real trap already *generated* by the
  Perl syscall-stub generator this script already runs, just never
  compiled+archived) and `pthread_mach_thread_np`/`pthread_testcancel`
  (`pthread_guard_compat.c` -- real, narrowly-scoped implementations for
  exactly libc++abi's `cxa_guard.cpp` call pattern, not full ports of the
  deliberately-excluded `pthread.c`/`pthread_cancelable.c`; see that
  file's own header comment for why each is correct as written, not a
  stand-in).

Real smoketest (`llvm_libcxx_smoketest.cpp`): `std::vector`+`std::sort`,
`std::string` concatenation, `std::shared_ptr` (`make_shared`, copy,
`use_count`) -- all QEMU-confirmed working (`libcxx_smoketest.macho`,
exit code 0). Both this and `libcxxabi_smoketest.macho` are now injected
by `tools/init_binary/inject_into_sd_image.sh` (previously ad hoc/
undocumented for the libc++abi one -- fixed to follow this project's
"every built artifact needs a reproducing script" rule for real).

**Core dyld2/ImageLoader source: DONE compiling, QEMU/link NOT yet
attempted (2026-08-25, `tools/userland_staging/build_dyld_core.sh`)**:
all 12 real files -- `src/dyld2.cpp` (7163 LOC), the 5
`src/ImageLoader*.cpp` files (~11.4K LOC), and 6 support files
(`dyldInitialization.cpp`, `dyldLock.cpp`, `dyldExceptions.c`,
`dyldLibSystemGlue.c`, `glue.c`, `threadLocalVariables.c`) -- compile
clean against the real libc++/libc++abi stack, ~20.7K LOC total, real
`libc_build/dyld_core.a` archive produced. Only one line of real Apple
source needed changing (`third_party-patches/dyld.patch`, see below);
every other gap closed via small, honest compat headers/real header
relocation, matching this phase's established pattern exactly. Full
flag/header derivation preserved in the script's own header comment so
it isn't re-derived next session.

**Real, confirmed bug in Apple's own dyld-832.7.3 snapshot, patched
(`third_party-patches/dyld.patch`, registered in `setup_third_party.sh`)**:
`ImageLoader.cpp`'s `VECTOR_NEVER_DESTRUCTED_IMPL(ImagePair)` references
a type declared nowhere in this checkout, with no matching
`_EXTERN` declaration either (unlike the real `InterposeTuple` line
right above it) -- an orphaned macro invocation from an upstream
rename/removal, confirmed via a full-checkout grep, not guessed. Dead
code, zero behavioral effect; removed with a comment, not silently
dropped.

**Real compat gaps found and closed, all narrowly scoped and documented
in `build_dyld_core.sh`'s own header comment** (none touch real Apple
source beyond the one patch line above): `dyld_compat/libamfi.h` and
`dyld_compat/sandbox/private.h` (this target has neither an AMFI kext
nor a sandbox subsystem -- real enum/signatures only, real honest no-op
implementations); `dyld_compat/CrashReporterClient.h` (no CrashReporter
daemon -- real no-op); `dyld_compat/corecrypto/*.h` (glue.c includes
three corecrypto headers but calls zero symbols from any of them --
confirmed dead includes, empty placeholders); `dyld_compat/
dyld_deny_list_compat.h` (`inDenyList()` is referenced once by
dyld2.cpp but not declared/defined anywhere in this snapshot at all --
a real omission, not a search-path miss; a from-scratch port has no
legacy per-app compatibility exceptions to encode, so always-`false` is
the correct real answer); a real, targeted SDK-mismatch override
(`sys/event.h`+`sys/proc_info.h`, real xnu-7195 versions substituted for
the iPhoneOS SDK's own mismatched pair -- the SDK's private
`proc_info.h` references `struct kevent_qos_s`, which its own public
`event.h` never defines at all, a genuine macOS/iOS SDK-content skew,
not a port gap); 19 real MIG headers regenerated fresh from xnu-7195
`.defs` (needed `-DLIBSYSCALL_INTERFACE=1` for `PREFIX()`/
`KERNEL_SERVER_SUFFIX()` to produce plain routine names like
`vm_allocate` instead of `_kernelrpc_vm_allocate`, and `-DKERNEL_PRIVATE`
for `memory_object_control.defs`/`upl.defs`'s `upl_t` family); real
`-DPRIVATE=1` (needed for `_COMM_PAGE_DYLD_FLAGS`/`mremap_encrypted`).
Every XNU/libplatform/libpthread include path in this script is
`-idirafter`, never `-I` -- an earlier pass silently broke `<string.h>`/
`<cstring>` content project-wide by letting a libsyscall-internal
minimal `string.h` shadow the SDK's real one; found and fixed before it
could cause a much more confusing failure downstream.

**dyld3 support code: DONE compiling (2026-08-25, same
`build_dyld_core.sh`)** -- all real gaps closed by real trial-link
undefined-symbol tracking, not guessed up front. 15 more real files
compiled clean on the first attempt (no new compat gaps at all):
`Diagnostics.cpp`, `BootArgs.cpp`, `Tracing.cpp`, `RootsChecker.cpp`,
`MachOFile.cpp`, `MachOAnalyzer.cpp`, `MachOLoaded.cpp`,
`shared-cache/FileUtils.cpp`, `Closure.cpp`, `ClosureWriter.cpp`,
`ClosureBuilder.cpp` (3974 LOC, the single largest file in the whole
port), `ClosureFileSystemPhysical.cpp`, `shared-cache/
DyldSharedCache.cpp`, `SharedCacheRuntime.cpp`, `Loading.cpp`,
`PathOverrides.cpp`, `MachOAnalyzerSet.cpp`. Plus one genuinely
functional (non-dead) corecrypto consumer, `MachOLoaded.cpp`'s
`forEachCDHashOfCodeSignature()`: real SHA-1/256/384 implemented against
corecrypto's real, public `ccdigest_info` API contract
(`dyld_ccdigest_compat.cpp`) -- algorithm ported from this project's own
already-verified kernel-side `corecrypto_sha2.c` (SHA-256/384) plus a
fresh, standard SHA-1 (not needed kernel-side). Real corecrypto's actual
`.c` driver implementations exist in this xnu-7195 checkout too but need
internal-only headers (`cc_priv.h`, `ccdigest_priv.h`,
`cc_runtime_config.h`, `fipspost_trace.h`) that were never
open-sourced -- confirmed by attempting the real compile first.
Total real, unmodified (bar the one `ImagePair` line) Apple dyld source
compiling clean against this project's own libc++/libc++abi:
**~31K LOC, 30 files, one real archive (`libc_build/dyld_core.a`)**.

**Real, confirmed, PERMANENT wall found via real trial-link
undefined-symbol tracking (not more compat-header grinding -- this one
doesn't have a compat-header answer)**: `dyld2.cpp`'s real closure-mode
launch path (`~dyld2.cpp:5963`) constructs a `dyld3::Loader` on the
stack and calls its methods (`addImage`, `completeAllDependents`,
`dtraceUserProbesEnabled`, `findImage`, `forEachImage`,
`mapAndFixupAllImages`, `resolveTarget`) plus needs
`dyld3::MachOAnalyzerSet`'s vtable. Neither is declared or defined
ANYWHERE in this checkout. Checked the FULL git history of every dyld
tag from `dyld-832.7.3` through the latest available (`dyld-1378`) --
`dyld3::Loader` never existed in any released apple-oss-distributions
dyld snapshot. Starting at `dyld-940`, Apple replaced the entire dyld2/
dyld3/ImageLoader architecture (everything ported in this phase) with a
fully-rewritten, incompatible "dyld4" engine (`dyld/Loader.cpp`,
`JustInTimeLoader.cpp`, `PrebuiltLoader.cpp` -- different namespace,
different object model) -- that newer `Loader` is not a drop-in despite
the similar name (compared the two APIs directly, not assumed).
`dyld3::Loader` in this exact dyld-832.7.3 snapshot appears to be a
transitional, never-publicly-released class from whatever internal
pre-release Apple build this tag was cut from.

**User decision (2026-08-25): retarget to `dyld-940`.** Presented as a
real fork -- (1) from-scratch `Loader`/`MachOAnalyzerSet` reimplementation
from known call-site signatures only (genuinely new engineering, real
risk of subtly-wrong semantics, no source reference exists), vs.
(2) retarget to `dyld-940`+, dropping the ~31K LOC of dyld2/ImageLoader
source ported above and porting the dyld4-era tree fresh. User chose
(2). `setup_third_party.sh`'s dyld pin is now `dyld-940` (see that
script's own header comment for the full justification -- a real,
documented exception to this project's usual macos-112-manifest-exact
rule, since that manifest's own pin is provably unbuildable).
`third_party-patches/dyld.patch` (the `ImagePair` fix) is retired --
`ImageLoader.cpp` doesn't exist in the dyld4 architecture at all.

**dyld-940 retarget: DONE compiling AND real-trial-link-verified with
NO permanent wall (2026-08-25, `build_dyld_core.sh` rewritten from
scratch)**. Before writing a single line, read the REAL Xcode project
file (`dyld.xcodeproj/project.pbxproj`) directly to find the exact,
authoritative file list for the real `dyld` `PBXNativeTarget`
(`F9ED4C970630A76000DF4E74`) rather than guessing from directory
listings -- this matters because dyld-940's repo also contains
`libdyld.dylib`, `dsc_extractor`, and other real targets sharing the
same `dyld/`/`common/` directories, and several files that LOOK relevant
(`LibSystemHelpers.cpp`, `PathOverrides.cpp`, `dyld_process_info*.cpp`,
`ClosureFileSystemPhysical.cpp`) turned out to belong to those OTHER
targets, not `dyld` itself. The real, complete list is exactly 22 files:

- `dyld/`: `dyldStartup.s` (real entry, `__dyld_start`), `dyldMain.cpp`,
  `DyldProcessConfig.cpp`, `DyldRuntimeState.cpp`, `Loader.cpp`,
  `JustInTimeLoader.cpp`, `PrebuiltLoader.cpp`, `DyldDelegates.cpp`,
  `DebuggerSupport.cpp`, `SharedCacheRuntime.cpp`, `DyldAPIs.cpp`,
  `PrebuiltObjC.cpp`, `Tracing.cpp`, `glue.c` -- the real dyld4 engine
  (`Loader`/`JustInTimeLoader`/`PrebuiltLoader` fully replace dyld2/
  dyld3/ImageLoader's whole object model; no legacy closure-file format
  survives into this architecture at all).
- `common/`: `OptimizerSwift.cpp`, `Allocator.cpp`, `DyldSharedCache.cpp`,
  `MachOFile.cpp`, `MachOLoaded.cpp`, `MachOAnalyzer.cpp`,
  `Diagnostics.cpp`, `PerfectHash.cpp` -- the shared Mach-O parsing
  engine, real source shared with (but not exclusive to) `dyld`.

All 22 compile clean, plus `dyld_ccdigest_compat.cpp` (see below) --
**23 real files, one real archive**. A real trial link (`-e
__dyld_start` against this archive plus the project's already-real
libc++/libc++abi/libc/libpthread stack) confirms **there is no
`dyld3::Loader`-class wall this time** -- the only 39 remaining
undefined symbols are ordinary real syscalls/mach traps
(`mach_msg`, `socket`/`connect`, `mkdir`/`rename`/`unlink`, `chmod`/
`fchmod`, `pread`/`pwrite`, `getxattr`/`fsetxattr`, `arc4random`,
`vm_copy`, `uuid_is_null`, `strstr`, etc) this project's curated libc
tier just hasn't generated stubs for yet -- the exact same bounded,
well-understood "wire up the existing curated libc stack" work every
prior smoketest in this project has already done (see
`build_libc_regex.sh`'s template), not a new architectural gap.

Real, narrowly-scoped compat gaps found and closed this round (full
detail in `build_dyld_core.sh`'s own header comment, same "real source
first" standard as every prior tier): `vproc_priv.h` (dead include,
launchd never open-sourced), `System/atexit.h` (real, standard Itanium
C++ ABI `__cxa_range_t` -- same layout in every implementation, not
Apple-internal), `PrebuiltLoader_version.h` (real Apple build computes
this as an md5 self-consistency check that never needs to match real
Apple's own dyld here), `PLATFORM_IOSMAC` (real historical alias for
`PLATFORM_MACCATALYST`, same value, this project's pinned iOS 14.4 SDK
only has the newer name), `task_dyld_process_info_notify_get` (real
mach trap added to a newer xnu than this project's pinned xnu-7195; no
external-debugger-attach consumer exists on this target anyway), a real
`AvailabilityInternalPrivate.h` vintage mismatch (patched on a COPY of
the real, separately-versioned `AvailabilityVersions` repo output, never
the original -- see below), and real Apple's own `dyld.h`/`dyld_priv.h`
flat-copy build step replicated exactly (not guessed).

**Real, reused dependency**: `<dyld/VersionMap.h>` (the real
`dyld3::sVersionMap` OS-version-compatibility data table) comes from
`AvailabilityVersions`, a genuinely separate apple-oss-distributions
repo this project's own kernel build (`tools/build_xnu_7195.sh`) already
clones+builds while constructing a local SDK -- `build_dyld_core.sh`
reuses that real, already-generated output rather than re-vendoring it,
and fails fast with a clear message if the kernel hasn't been built yet.

**Real, complete, fully-static dyld.macho: DONE (2026-08-25,
`build_dyld_link.sh`, new script)** -- the real ~15 missing syscalls
were closed (real `create-syscalls.pl`-generated stubs for
`sigprocmask`; real `libplatform/src/string/generic/{strstr,strncmp}.c`
compiled with the real `-DVARIANT_STATIC=1` Apple build flag for the
public wrapper names; a real, honest `voucher_mach_msg_set()` fallback
matching real Apple's own NULL-function-pointer behavior when no
libdispatch ever registers real voucher callbacks), which surfaced the
REAL remaining problem: dyld's own source is a genuinely self-sufficient
runtime (confirmed directly -- `dyld_core.a` references zero `malloc`/
`free`/`calloc`/`realloc` symbols at all; `Allocator.cpp` is dyld's own
allocator, `glue.c` provides dyld's own `abort()`/`exit()`/stdout/
stderr/`std::__throw_bad_alloc()`/block-runtime), so linking it into the
same binary as this project's general-purpose curated libc/libmalloc
stack (genuinely still needed -- dyld really does call real `printf`/
`pread`/`pwrite`/`statfs`/etc) produces real duplicate-symbol collisions
between dyld's intentional self-sufficient copies and the shared ones.
Resolved via real `ld -r -unexported_symbols_list` trims on **scratch
copies only** (glue.o, libmalloc_dispatch.o, libc_printf_mach_stub.o,
mach_vm.o) plus one real `ar d` (libmach_ipc.a's already-documented
"honest dead stub" member, superseded by dyld_core.a's own real,
complete MIG output) -- the shared, committed archives are never
touched. Full real reasoning for which side wins each collision is in
`build_dyld_link.sh`'s own header comment.

**Result: `dyld.macho` -- fully static (`NOUNDEFS`), real
`LC_UNIXTHREAD`, zero `LC_LOAD_DYLIB`, zero undefined symbols, built
end-to-end from a clean run of `build_dyld_core.sh` + `build_dyld_link.sh`,
no manual steps.** This is a real, complete, working dyld-940 binary --
not a stub, not a partial link.

**First real QEMU boot test (2026-08-25, earlier pass)**: installed as
`/usr/lib/dyld`, paired with `dyld_test_exec.macho` + `libtest.dylib`.
Kernel handoff succeeded but the process was killed partway through
(`Killed: 9`, exit 137) with no diagnostic output -- tracked as the next
step, not resolved in that pass.

### Phase C: CLOSED 2026-08-25 (same day, later session) -- real dyld
### boots and reaches a real dylib's dependency-loading logic

Six real, distinct bugs found and fixed, each via the same "temporary
kernel/dyld `printf` diagnostic, root-cause, fix permanently, revert the
diagnostic" cycle (every temporary diagnostic was reverted via `git
checkout --` before its containing commit; `third_party/xnu-7195` and
`third_party/dyld` are both real, unmodified-except-where-documented
forks with clean `git status` as of every commit below):

1. **MH_DYLINKER filetype** -- `dyld.macho` was linked as plain
   `MH_EXECUTE`; `mach_loader.c`'s `parse_machfile()`/`load_dylinker()`
   branch on this filetype in several places (e.g.
   `note_all_image_info_section()` is skipped entirely for non-dylinker
   files). Fixed in `build_dyld_link.sh` with `-Wl,-dylinker
   -Wl,-dylinker_install_name,/usr/lib/dyld`.
2. **Missing `-fno-exceptions`** -- real Apple `dyld.xcconfig` sets
   `GCC_ENABLE_CPP_EXCEPTIONS = NO`; without the matching compiler flag
   (the `-D_LIBCPP_NO_EXCEPTIONS=1` macro alone doesn't stop landing-pad
   codegen), dyld's own code pulled in real `_Unwind_Resume` machinery
   transitively through libunwind's `libunwind_dyld_shim.o`, which
   itself needs `__mh_execute_header` -- never synthesized for
   `MH_DYLINKER` output. Fixed with the real compiler flag in
   `build_dyld_core.sh`, plus excluding `libunwind_dyld_shim.o` on a
   scratch copy of `libunwind.a` (real, honestly-scoped 3-function
   fallback for the genuinely circular "dyld asks itself about its own
   unwind sections" case -- dyld's own code never throws).
3. **Missing TSD bootstrap** -- dyld's own `__dyld_start` never sets up
   `TPIDRRO_EL0` itself (real Apple systems get this from
   `libSystem.dylib`'s initializer, which this port didn't have until
   Phase D below) -- the first real MIG call
   (`_mig_get_reply_port`) dereferenced `TPIDRRO_EL0+0x10` while NULL.
   Fixed with a new entry trampoline
   (`tools/userland_staging/dyld_tsd_entry_trampoline.s`,
   `__dyld_start_tsd_bootstrap`) that runs this project's existing
   `platform_syscall`-based TSD bootstrap (`pthread_tsd_shim.c`'s real,
   QEMU-verified mechanism) before branching into real, unmodified
   `__dyld_start`.
4. **Missing rebase/fixup-chains on dyld itself** -- the link used
   `-static`, which suppresses ALL rebase info; the kernel still
   ASLR-slides dyld's own mapped segments (real, unmodified
   `mach_loader.c` behavior), but had nothing to fix up dyld's own
   internal absolute-address globals against, causing a
   stale-link-time-pointer crash inside `dyld4::halt()`. Fixed by
   dropping `-static` and adding `-Wl,-fixup_chains` (dyld has zero
   `LC_LOAD_DYLIB` dependents regardless, so this doesn't reintroduce
   any real dynamic-linking requirement for dyld itself).
5. **Wrong side of a real link-time symbol conflict** -- `dyld_core.a`'s
   own MIG-generated `vm_allocate`/`vm_deallocate`/`vm_map`/`vm_protect`/
   etc (from `vm_map.defs`) go through the legacy `vm_map_subsystem`,
   which real, unmodified xnu-7195 **never registers** in its kobject
   dispatch table (`osfmk/kern/ipc_kobject.c` only registers
   `mach_vm_subsystem`) -- confirmed via a temporary diagnostic in the
   generated `vm_map_server.c` showing the per-routine check is never
   even reached (`kr=-304`, `MIG_BAD_ARGUMENTS`). This is real, general
   Apple xnu behavior on 64-bit-only modern Darwin, not
   project-specific. An earlier pass of `build_dyld_link.sh` had hidden
   `libmalloc.a`'s real `mach_vm.o` (built from real
   `libsyscall/mach/mach_vm.c`, which calls the always-live
   `_kernelrpc_vm_allocate_trap` fast path) in favor of `dyld_core.a`'s
   dead-end MIG stubs -- backwards. Flipped: `vm_mapUser.o`'s 7
   overlapping names are hidden instead, keeping its other real routines
   (`vm_copy`, `vm_region`, `vm_map_64`, etc) that `mach_vm.c` doesn't
   provide.
6. **Non-variadic `fcntl()`** -- this project's own public `fcntl()`
   (`tools/userland_staging/libc_gen_syscalls.c`) and its private
   `_fcntl()` (`libc_stdio_syscalls.c`) were both declared with a fixed
   third parameter, while every real caller (including real dyld)
   compiles against the SDK's actual variadic `<fcntl.h>` and pushes
   extra call-site arguments on the **stack**, per Apple's real arm64
   ABI rule for variadic calls -- a non-variadic callee reading its
   third argument from a register got garbage. Hit twice before the
   general fix: first as `F_GETPATH` returning an empty path for a
   dylib that genuinely existed (kernel-side `build_path()`/
   `vn_getpath()` computed the right path internally, confirmed via a
   temporary `kern_descrip.c` diagnostic, then `copyout()`'d it to
   userspace address 0), then as `F_CHECK_LV` (`kr` argument address
   literally became `98`, the command value itself) rejecting every
   dylib as failing library validation. First attempt widened a
   hardcoded "which commands take a vararg" whitelist (`F_SETFL` ->
   `+F_GETPATH`/`F_GETPATH_NOFIRMLINK`) and immediately re-hit the same
   bug on `F_CHECK_LV` -- the real, general fix removes the whitelist
   entirely and always reads+forwards the vararg (safe on this ABI: a
   caller passing only 2 arguments just leaves stale register content
   the kernel's arg-less-command handlers never read anyway, confirmed
   by reading every real `kern_descrip.c` case).

**Net result of Phase C**: `dyld_test_exec.macho` + `libtest.dylib`
(built `-nostdlib`, since a plain `-dynamiclib` link auto-adds an
`LC_LOAD_DYLIB` on `/usr/lib/libSystem.B.dylib` that didn't exist yet at
this point) progresses through TSD bootstrap, real Mach VM allocation via
dyld's own heap allocator, rebasing, file lookup, and code-signature
validation, then correctly hits dyld's own real, unconditional
requirement (`dyldMain.cpp`) that every launched process link against
both `/usr/lib/system/libdyld.dylib` and `/usr/lib/libSystem.B.dylib` --
which is Phase D, below.

### Phase D: real `libdyld.dylib` + `libSystem.B.dylib` -- DONE
### 2026-08-25, dyld reaches a real program's real `main()`

`dyldMain.cpp` unconditionally halts (`halt("program does not link with
libSystem.B.dylib")`, and separately for `libdyld.dylib`) unless the
process loaded both, matched by exact install-name string in
`DyldRuntimeState.cpp`'s `RuntimeState::add()`. Both built for the first
time:

- **`tools/userland_staging/build_libdyld.sh`** (new): real,
  unmodified dyld-940 source
  (`third_party/dyld/libdyld/libdyldGlue.cpp` + `LibSystemHelpers.cpp`
  -- `build_dyld_core.sh`'s own header comment already correctly
  identified these as belonging to the separate `libdyld.dylib` Xcode
  target, not `dyld` itself) compiled with `BUILDING_DYLD=0` (the
  shared `dyld`/`common` headers' own branch for the libdyld side, vs.
  `=1` for the `dyld` tool), linked into a real `/usr/lib/system/
  libdyld.dylib` with a correct `__dyld4` section and zero undefined
  symbols.
- **`tools/userland_staging/build_libsystem.sh`** (new): this project's
  existing curated static libc_gen/libc_string/libc_stdio/libpthread/
  libmalloc/libplatform/libcxx/libcxxabi archives bundled into a real
  `/usr/lib/libSystem.B.dylib`, reusing `build_libdyld.sh`'s own scratch
  symbol-collision fixes.

**This is the "simplified" architecture flagged as a real, structural
problem going forward -- see the new "Static-vs-dylib architecture debt"
section immediately below.** In short: real Apple's `libSystem.B.dylib`
is a thin re-export umbrella over ~9 separate real sub-dylibs
(`libsystem_c`, `libsystem_malloc`, `libsystem_pthread`,
`libsystem_platform`, `libsystem_blocks`, `libsystem_kernel`,
`libdispatch`, `libxpc`, `libcompiler_rt`), each with its own real
Mach-O load-time init sequence. This project instead flattened its
EXISTING static archives (built for a static-linking-only world back in
Phase 4) into one dylib -- functional enough to satisfy dyld's
structural checks, but missing the real per-dylib initializer glue real
Apple's architecture provides for free, so every touchpoint has to be
discovered and hand-patched one at a time (see the `_dyld_initializer()`
gap below for a concrete example of exactly this failure mode).

Real gaps found+closed building both dylibs (each documented in its own
compat file, not summarized further here -- read the file comments
before re-deriving): `__cxa_finalize_ranges` (this project's pinned
upstream libcxxabi never grew Apple's own dlclose-time extension), a
minimal real `__cxa_atexit`/`__cxa_finalize`, `vproc_swap_integer` (no
launchd -- honestly always "not managed"), `malloc_size` (real,
documented "not found" behavior for a pointer this port's simplified
libmalloc never registers in a zone table), pthread's post-fork re-init
pair (real dead code -- nothing here calls `fork()` after loading a
dylib), TLV support (no `__thread` consumer exists yet), `mkdirat` (real
syscall #475, never wired before), and two real pre-existing
`libplatform` source files (`os/alloc_once.c`,
`string/generic/memset_pattern.c`) that had real source in the pinned
repo but were never added to `build_libplatform.sh`'s file list.

`build_dyld_test_exec.sh` links `dyld_test_exec.macho` directly against
both new `.dylib` files (embeds their own real `LC_ID_DYLIB`
install-names regardless of local build path, same mechanism
`libtest.dylib` already used). `inject_into_sd_image.sh` installs both
to their real paths.

**Net result, QEMU-verified: `dyld_test_exec`'s own real `main()` runs.**
Real dyld loads all three dylibs, passes both of `dyldMain.cpp`'s hard
requirement checks, runs all real initializers, and jumps to the real
program -- the original Phase 6.5 goal (real dyld booting a real
dynamically-linked executable) is achieved.

### Phase D: CLOSED for real 2026-08-25 (later session) -- clean exit,
### `dyld_test_exec` runs to completion with no crash

The NULL-dispatch bug below and a second, previously-undiscovered bug
(also below) are both fixed and QEMU-verified. Real, reproducible
boot-test result (`qemu/dyld_real_boot_cmds.txt` via
`tools/init_binary/_pty_test_phase5.py`, `/bin/dyld_test_exec` from a
real login shell):

```
dyld_test_exec: own main() ran (real dyld loaded deps then jumped here -- expected)
EXITCODE:1
```

No crash, no `Trace/BPT trap`, exit code 1 is `dyld_test_exec_main.c`'s
own real `return 1`. This is the original Phase 6.5 goal fully closed:
real dyld loads real per-library dylibs and a real main executable, runs
real initializers, jumps to the real program, and the process exits
cleanly on return.

**Bug 1 (the NULL `LibSystemHelpers` dispatch documented below): fixed**
by `tools/userland_staging/libsystem_dyld_initializer_compat.c`'s
constructor, with one correction beyond what was scoped when it was
written mid-session: the `-Wl,-U,__dyld_initializer` symbol name needed
the compiler's own extra leading underscore (C symbol `_dyld_initializer`
mangles to linker symbol `__dyld_initializer`) -- the single-underscore
version silently left the symbol fully undefined instead of marking it
resolve-at-runtime, so the very first build after this fix was written
failed to link at all. Found immediately on resuming this work by just
trying to build it.

**Bug 2 (new, found resuming this session): a second real
Static-vs-dylib architecture-debt crash, one layer deeper.** After bug 1
was fixed, `dyld_test_exec` still crashed -- but earlier and differently
(`Trace/BPT trap: 5`, no `EXITCODE`, not even `main()`'s own message
printed). Root-caused via the same real-diagnostic method used all
through Phase C (temporary `handle_ux_exception` PC/register capture in
`bsd/uxkern/ux_exception.c`, temporary `printf` in
`osfmk/ipc/mach_kernelrpc.c`'s `_kernelrpc_mach_vm_map_trap`, a temporary
`RuntimeState::add()` load-address log in `third_party/dyld` -- all
reverted via `git checkout --` once root-caused, per this project's
standing convention):

- Crash PC landed exactly on real, unmodified `libplatform/src/os/
  alloc_once.c`'s `_os_alloc_slow()` -- specifically its own
  `__LIBPLATFORM_INTERNAL_CRASH__` trap, hit when its
  `mach_vm_map(mach_task_self(), ...)` call returns a non-success
  `kern_return_t` (captured directly from the register the crash-site
  disassembly showed still held it: `x10 = 5 = KERN_INVALID_ADDRESS`).
- The kernel-side `_kernelrpc_mach_vm_map_trap` diagnostic proved this
  specific call never even reached the kernel trap, which combined with
  `mach_task_self()` being a plain `#define mach_task_self()
  mach_task_self_` macro (real `libsyscall/mach/mach/mach_init.h`)
  pointed at `mach_task_self_` itself being `MACH_PORT_NULL` -- and real
  `libsyscall/mach/mach_init.c` has an explicit real comment confirming
  exactly this failure mode is possible: "mach_init() must be called
  explicitly in static executables (including dyld)." On real macOS this
  never comes up because `libsystem_kernel.dylib` is one shared image
  and dyld calls `__libkernel_init()` once, process-wide. This project's
  `libdyld.dylib` and `libSystem.B.dylib` each bundle their OWN private
  static copy of `mach_init.c` (same flattened-static-archive
  architecture the whole "Static-vs-dylib architecture debt" section
  below is about), so each has its own **separate, independently
  never-initialized** `mach_task_self_` global.
- First fix attempt: a plain `__attribute__((constructor))` in a new
  `tools/userland_staging/libdyld_mach_init_compat.c` calling
  `mach_init()`, linked into `libdyld.dylib` itself. Built and linked
  clean, but a temporary raw-`write()`-syscall diagnostic at the
  function's own entry/exit (no allocator dependency, safe this early)
  proved it **never ran** -- real dyld does not run
  `/usr/lib/system/libdyld.dylib`'s own per-image constructors at all
  (it's handled specially via `RuntimeState::setDyldLoader()`, not as an
  ordinary client dependency; real Apple's own libdyld.dylib ships with
  no C++ static initializers of its own for the same structural reason).
- **Real, correct, QEMU-verified fix**: `libdyld_mach_init_compat.c`'s
  function is a plain exported symbol (`libdyld_mach_init_compat_run()`,
  no constructor attribute) instead. `libsystem_dyld_initializer_compat.c`
  -- the constructor already proven to run, since it's what fixed bug 1
  -- now calls three things, **in this exact order**: `mach_init()`
  directly (for `libSystem.B.dylib`'s own private copy), then
  `libdyld_mach_init_compat_run()` (an `-Wl,-U` cross-dylib call into
  `libdyld.dylib`, resolved at runtime the same way `_dyld_initializer`
  already is, initializing libdyld's separate private copy), *then*
  `_dyld_initializer()` last. Order is load-bearing, not stylistic: an
  earlier attempt called `_dyld_initializer()` first and still crashed,
  because `_dyld_initializer()` itself (real, unmodified dyld source)
  does real VM/allocator work registering the `LibSystemHelpers` vtable,
  reaching `os_alloc_once`/`mach_vm_map` before a fix placed after it
  ever got a turn to run.
- `build_libsystem.sh` no longer links `$OUT/libdyld/mach_init_compat.o`
  directly (an earlier intermediate version of this fix did) -- linking
  that object gives `libSystem.B.dylib` its own local, statically-bound
  definition of `libdyld_mach_init_compat_run`, which would silently
  defeat the `-Wl,-U` cross-dylib call (it'd just call back into
  `libSystem.B.dylib`'s own copy instead of reaching `libdyld.dylib`'s).

Net effect: two real, independent instances of the same
"flattened-static-archive-into-one-dylib loses Apple's real per-dylib
load-time behavior" failure mode, found and fixed in the same debugging
session, on top of the one from Phase D's first pass (`_dyld_initializer`
itself). Concrete, first-hand evidence for the "Static-vs-dylib
architecture debt" section immediately below, not just a theoretical
concern -- every flattened dylib that bundles its own private copy of
process-global bootstrap state (`mach_task_self_` here; there will be
others) needs this same kind of independently-discovered, per-symbol fix
until the real migration to separate, Apple-shaped per-library dylibs
happens.

### Historical: `main()` returning triggers a NULL virtual-dispatch SIGSEGV
### (bug 1 above, kept for the original root-cause narrative)

Symptom: `main()` prints its message and returns normally (doesn't call
`exit()` itself); the process then `SIGSEGV`s (`subcode=0x0`, a NULL
dereference) instead of exiting cleanly.

**Root-caused** (via a temporary `handle_ux_exception` PC/LR/SP
diagnostic plus a temporary `RuntimeState::add()` diagnostic used to
compute dyld's real runtime load address and disassemble the exact crash
site): real `dyldbootstrap::start()` calls the main executable's entry,
then dispatches its return value through a **real C++ virtual call** --
`state.libSystemHelpers->exit(result)`, i.e. a vtable call through
`RuntimeState`'s `LibSystemHelpers*` field. That pointer is NULL because
nothing in this port's `libSystem.B.dylib` ever calls libdyld's real
`_dyld_initializer()` (`libdyldGlue.cpp:133`, which registers the real
concrete `LibSystemHelpers` instance via `gDyld.apis->
_libdyld_initialize(&sHelpers)`). Real `_dyld_initializer()`'s own
comment says exactly when it's supposed to run: "called during
libSystem.dylib initialization" -- on real macOS, real Libc's
`init.c`/`_libc_initializer()` is libSystem.dylib's own real
Mach-O-constructor (`S_MOD_INIT_FUNC_POINTERS`, which dyld genuinely
runs automatically for every loaded image) and calls
`_dyld_initializer()` explicitly as one of its own setup steps. This
project's flattened `libSystem.B.dylib` has no such real Libc-side
initializer glue at all -- exactly the "Static-vs-dylib architecture
debt" failure mode: a real per-dylib load-time behavior that Apple's
actual modular structure provides for free had to be independently
discovered here.

**Fix, built and QEMU-verified** (see "Phase D: CLOSED for real" above
for the full closure story, including a second bug this fix's first
build attempt uncovered): `tools/userland_staging/
libsystem_dyld_initializer_compat.c` (new file) adds a real
`__attribute__((constructor))` function to `libSystem.B.dylib` that
calls `_dyld_initializer()` directly -- the same real Mach-O mechanism
real Libc's own `init.c` uses, just without the rest of Libc's
additional setup this port doesn't need. First attempt tried linking
`libSystem.B.dylib` directly against `libdyld.dylib` to resolve the
cross-dylib `_dyld_initializer` reference at build time -- this backfired:
once ld64 sees a `.dylib` on the link line, it stops pulling several
syscall symbols (`_open`/`_read`/`_write`/`_close`/`_fstat`/etc) from
this project's own static archives (since it now considers them
"satisfiable via a dependency"), and they didn't bind cleanly that way,
producing ~30 new undefined symbols. Corrected: removed the
`libdyld.dylib` link input, added `-Wl,-U,__dyld_initializer` to
`build_libsystem.sh`'s link line instead (note the double leading
underscore -- see "Phase D: CLOSED for real" above; the single-underscore
version silently failed to link) to mark specifically that one symbol as
an intentional dynamic lookup, resolved at runtime once dyld has also
loaded `libdyld.dylib` into the same process, without disturbing
anything else's resolution.

### Static-vs-dylib architecture debt -- new project convention going
### forward (user directive, 2026-08-25)

This project's libc/libpthread/libmalloc/libplatform/etc. were built as
static archives back in Phase 4, when there was no dyld and static
linking was the only option. Phase D above needed *something* real and
loadable to exist at `/usr/lib/libSystem.B.dylib`, and took the fast
path: bundle those same static archives into one flat dylib rather than
restructuring them into dylibs matching real Apple's actual modular
`libsystem_*.dylib` architecture.

**This was the wrong tradeoff, repeated all session**: every real
per-dylib behavior Apple's actual structure provides automatically (a
dylib's own load-time constructor running its own init code, correct
two-level-namespace symbol ownership, `_dyld_initializer()`-style
handshakes with dyld) had to be independently discovered as a crash,
then hand-patched with a small compat shim -- the `_dyld_initializer()`
gap immediately above is the clearest example, but the whole session's
"add archive, hit undefined symbol, add scratch-exclusion fix, repeat"
cycle is the same pattern at a smaller scale.

**User directive: going forward, the convention on this project is real
per-library `.dylib`s, not static archives + temporary shims.** Concretely,
for whoever picks this up next:
- Do not add new static-archive build scripts for anything that could
  plausibly be a real dylib consumer of dyld (which, as of Phase C/D
  above, is now anything -- dyld is real and works).
- Existing static archives (`libc_gen.a`, `libc_string.a`, `libc_stdio.a`,
  `libc_printf.a`, `libpthread.a`, `libmalloc.a`, `libplatform.a`,
  `libclosure.a`, `libcxx.a`, `libcxxabi.a`, `libunwind.a`, and the
  smaller ones) should be migrated to real, individually-loadable
  `.dylib`s over time -- not necessarily matching Apple's exact
  `libsystem_*` sub-dylib boundaries 1:1, but real, separate Mach-O
  images with their own real load-time init (constructors run
  automatically by dyld) rather than one flattened bundle.
  `tools/init_binary/build.py`/`tools/pwtool/build.py`'s existing
  fully-static builds (no dyld at all, PID-1 and password-tool
  specifically) are the one legitimate exception -- they predate dyld
  and have a real reason to stay static (bootstrap-before-dyld-exists
  concerns) -- everything else built via `tools/userland_staging/
  build_*.sh` is now a real dylib-conversion candidate.
- This is a real, multi-session restructuring effort, not a single
  patch -- not attempted yet as of this write-up. Whoever picks it up
  next should scope it phase-by-phase the same way Phase C's dyld port
  itself was scoped (one library/subsystem at a time, real source
  first, QEMU-verify each step), starting from a stable, exit-cleanly
  baseline (i.e. land the `_dyld_initializer()` fix above first).

**Phase D is now closed for real** (clean exit, no crash -- see "Phase D:
CLOSED for real" above) -- the stable, exit-cleanly baseline this section
asks for is in place. This unlocks, in order: the static-to-dylib
migration above (**first real step done, see immediately below**), then
`libdispatch` as a real `.dylib` (Phase 6.6), then CoreFoundation (Phase
9), then everything gated behind either.

### Static-to-dylib migration, step 1: `libsystem_kernel.dylib` -- DONE
### 2026-08-25, QEMU-verified

First real library extracted per this section's own directive, chosen
deliberately as the Mach-bootstrap/VM-trap layer specifically -- the
exact surface (`mach_init.c`'s `mach_task_self_`) that caused two real,
independent bugs closing Phase D (see "Phase D: CLOSED for real" above),
so this step both starts the migration and permanently retires the
compat-shim workaround that closure needed.

`tools/userland_staging/build_libsystem_kernel.sh` (new) builds a real,
separate `/usr/lib/system/libsystem_kernel.dylib` from real, unmodified
Apple source: `mach_init.c`, `mach_vm.c` (the trap-first/MIG-fallback
`mach_vm_allocate`/`_deallocate`/`_protect`/`_map` wrappers),
`mig_reply_port.c`, and `wrappers/init_cpu_capabilities.c` (a real
`mach_init.c` dependency) -- plus this project's own
`libmalloc_vm_traps.s` (the hand-written Mach-trap stubs
`create-syscalls.pl`'s BSD-syscall-only generator doesn't cover, already
the sole real definition of these traps anywhere in the project, just
promoted from a `libmalloc`-private file to the shared dylib) and
`libmalloc_vm_mig_stubs.c` (real, provably-unreachable MIG-fallback dead
code `mach_vm.c`'s own source unconditionally references). A small new
`libsystem_kernel_compat.c` supplies the same two narrowly-scoped
stand-ins `libmalloc_dispatch.c` already established for its own,
separate static-archive consumers (`pthread_set_self()` no-op,
`mach_port_deallocate`/`_mod_refs` trap-only wrappers) rather than
linking that whole file's unrelated malloc-specific glue into the
kernel-trap dylib. Deliberately NOT included yet: the BSD syscall
surface (`open`/`read`/`write`/etc) -- those still work correctly as
static-archive copies today (no observed bug from that duplication, and
they're scattered across `libc_gen.a`'s own syscall wrappers rather than
one clean unit), so migrating them is separate, later work, not bundled
into this narrower step.

`build_libdyld.sh` and `build_libsystem.sh` both now: `ar d` the 6 real
object files above out of their private `libmalloc_scratch.a` copies
(the shared, committed `libmalloc.a` itself is untouched -- every other,
still-fully-static consumer keeps building against it unchanged), and
link a real `LC_LOAD_DYLIB` against `libsystem_kernel.dylib` directly
(confirmed via `otool -l`, not the `-Wl,-U` dynamic-lookup-per-symbol
mechanism `_dyld_initializer` needed -- safe here specifically because
`libsystem_kernel.dylib` is narrowly scoped and shares zero symbol names
with the static archives still linked alongside it, confirmed by a real
`nm` diff before making this change; the earlier `-Wl,-U` requirement
for linking directly against `libdyld.dylib` was caused by that
specific dylib ALSO re-exporting `_open`/`_read`/`_write`/etc since it
bundles the same curated static libc stack internally, not a general
"any `.dylib` on the link line breaks static resolution" rule).
`tools/init_binary/inject_into_sd_image.sh` installs the new dylib at
its real path -- required even though `dyld_test_exec.macho` itself
never references it directly, since dyld resolves `LC_LOAD_DYLIB`
dependencies recursively through `libdyld.dylib`/`libSystem.B.dylib`.

This also let two compat-shim layers retire, not just move: the
`libdyld_mach_init_compat.c` cross-dylib `-Wl,-U` call
(`libdyld_mach_init_compat_run()`) `libsystem_dyld_initializer_compat.c`
needed to separately initialize libdyld.dylib's own private copy is
gone -- deleted, not just unused -- because there is only ONE real,
shared `mach_task_self_` now (in `libsystem_kernel.dylib`'s own data
section), the same real Apple architecture, so a single `mach_init()`
call from the one proven-to-run constructor initializes it for every
consumer at once.

**QEMU-verified**: `dyld_test_exec` still prints its own `main()`'s
message and exits with `EXITCODE:1`, no crash, now loading 4 real
dylibs (`libtest.dylib`, `libdyld.dylib`, `libSystem.B.dylib`,
`libsystem_kernel.dylib`) instead of 3.

### Static-to-dylib migration, step 2: `libsystem_platform.dylib` +
### `libsystem_blocks.dylib` -- DONE 2026-08-25, QEMU-verified

Same day, continuing the batch-of-2 pace (user directive: "do 2 library
at a time and finish all"). `build_libsystem_platform.sh` (new): real,
unmodified Apple `os/lock.c` (`os_unfair_lock`/`os_once`)/`os/atomic.c`/
`atomics/arm64/OSAtomic.c`, this project's own `libplatform_mach_traps.s`/
`libplatform_syscalls.c`/`libplatform_thread_self.c` (same real files
`build_libplatform.sh` already established, see that script's own header
comment), linked as `/usr/lib/system/libsystem_platform.dylib` against
`libsystem_kernel.dylib`. Audited first for the same duplicate-global
risk that caused Phase D's bugs: none found -- `os_unfair_lock`/`OSAtomic`
operate on caller-provided memory, TSD reads `TPIDRRO_EL0` directly, not
an image-local global.

**A second real, independent instance of the mach_task_self_ bug class
found in the same audit, fixed in the same step**: `cerror_stub.c`'s
`int errno` (this project's own deliberate single-plain-global
simplification of real Apple's TSD-based errno -- see that file's own
comment) was a LOOSE object linked directly into every dylib/binary
that needs it, including `build_libdyld.sh` and `build_libsystem.sh`
separately -- two private copies of one process-global, never yet hit by
an actual crash only because no test here had exercised a syscall
failure in one dylib whose errno a caller then reads from another.
Moved into `libsystem_kernel.dylib` (the architecturally correct home --
real Apple's own errno/cerror machinery lives in libsystem_kernel.dylib
too) as the one real, shared copy. Static, single-image binaries (bash,
ls, cron, etc) were never affected -- one process, one image, one errno,
no duplication possible there in the first place.

Also folded real `os/alloc_once.c`/`string/generic/bzero.c`/
`string/generic/memset_pattern.c`/`string/generic/memmove.c` into
`libsystem_platform.dylib` -- these used to be compiled as loose private
objects by `build_libdyld.sh`/`build_libsystem.sh` (and, for `memmove.c`
specifically, ALSO independently by `build_libclosure.sh`'s own static
`libclosure.a`) purely because `build_libplatform.sh`'s own file list
never included them (real source, simply never added -- see that
script's own header comment). No correctness bug here (each `os_alloc_once`
slot is its own independent bump region regardless of which copy of the
code runs), just real architectural cleanup -- one less duplicate.

`build_libsystem_blocks.sh` (new): real, unmodified Apple `data.c` +
`runtime.cpp` (same file list/flags `build_libclosure.sh` already
established, including the real `-fno-exceptions`/`-fno-rtti` reasoning
-- `runtime.cpp` genuinely throws nothing, confirmed by grep, not
assumed), linked as `/usr/lib/system/libsystem_blocks.dylib` against
`libsystem_platform.dylib` for `_platform_memmove` (matching real
Apple's actual `libsystem_blocks.dylib` -> `libsystem_platform.dylib`
dependency) instead of bundling its own redundant copy the way
`libclosure.a` did. **One real, temporary gap flagged, not silently
worked around**: `_malloc`/`_free` (real `Block_copy`/`Block_release`
heap-promotion calls) aren't resolvable against a real dylib yet --
`libmalloc.a` isn't migrated until step 3 -- so they're marked
`-Wl,-U` (dyld flat-namespace dynamic lookup, same mechanism
`_dyld_initializer` already uses) rather than bundling yet another
private static copy. Resolves correctly today via whichever loaded
image currently exports them (`libdyld.dylib`/`libSystem.B.dylib`'s own
still-static `libmalloc.a`), but should become a real `LC_LOAD_DYLIB`
dependency on `libsystem_malloc.dylib` once step 3 lands -- flagged
explicitly in that script's own comment so this doesn't get forgotten.

`build_libdyld.sh`/`build_libsystem.sh`: `libplatform.a`/`libclosure.a`/
`cerror_stub.o` removed from both their dependency-check lists and link
lines entirely (real `LC_LOAD_DYLIB`s on the two new dylibs instead);
the loose `alloc_once.o`/`bzero.o`/`memset_pattern.o` compiles removed
too (now provided by `libsystem_platform.dylib`). The shared, committed
`libplatform.a`/`libclosure.a` archives themselves are untouched -- every
other still-fully-static consumer keeps building against them
unchanged. `tools/init_binary/inject_into_sd_image.sh`'s hand-listed
per-dylib install block was generalized into a loop over
`libc_build/system/*.dylib` (excluding `libdyld.dylib`/`libSystem.B.dylib`,
which have their own non-`/usr/lib/system/` install paths) so each new
migration batch's dylib is picked up automatically without a repeat edit
here.

**QEMU-verified**: `dyld_test_exec` still runs its own `main()` and
exits with `EXITCODE:1`, no crash, now loading 6 real dylibs
(`libtest.dylib`, `libdyld.dylib`, `libSystem.B.dylib`,
`libsystem_kernel.dylib`, `libsystem_platform.dylib`,
`libsystem_blocks.dylib`) instead of 4. Full boot-to-login-shell sequence
in the same test run also unaffected (reached `Welcome, root.` and a
working prompt before the `dyld_test_exec` invocation, same as every
prior pass).

### Static-to-dylib migration, step 3: `libsystem_malloc.dylib` +
### `libsystem_pthread.dylib` -- DONE 2026-08-25, QEMU-verified

Same day, continuing the batch-of-2 pace (user directive: "do 2 library
at a time and finish all"). **The most serious instance of the
duplicate-global-state bug class found in this whole migration --
real, silent heap-corruption risk, not just a wrong-value bug like
`mach_task_self_`/`errno` were.**

`libmalloc_dispatch.c`'s `static malloc_zone_t *g_zone` is the live heap
zone every `malloc`/`free`/`realloc` call in this port goes through
(`create_scalable_zone()` runs once, lazily, the first time `get_zone()`
sees `g_zone == NULL`). Until `build_libsystem_malloc.sh`,
`libdyld.dylib` and `libSystem.B.dylib` EACH bundled their own private
static copy of the entire magazine allocator, including their own
separate `g_zone` -- meaning a pointer `malloc`'d by code executing in
one dylib and `free`'d by code executing in the other would be
operating on a completely different heap's metadata. Fixed the same way
as `mach_task_self_`: one real, shared `libsystem_malloc.dylib` (real
Apple's own architecture), extracting the already-built, already-
verified member objects out of `libc_build/libmalloc.a` (`ar x`, same
approach as `libsystem_kernel.dylib`) rather than the 6 objects already
migrated to `libsystem_kernel.dylib` in step 1 or the 4 generic string
primitives now in `libsystem_platform.dylib` (added this step:
`strcmp.c`/`strlen.c`/`strchr.c`, needing a `-DLONG_BIT=64` flag
`strlen.c` specifically requires and the other three didn't -- found by
just trying it and reading the real compiler error, not guessed). Real
UNIX2003/cancelable-variant `-Wl,-alias` flags (`_kill`, `_kdebug_trace`,
`_mprotect$UNIX2003`, etc) had to be re-applied at this dylib's own link
line -- real dylib symbol resolution doesn't flatten across image
boundaries the way `ar`-combined static archives did, so each dylib now
needs its own copy of whichever aliases its own real source calls.
`getenv`/`getprogname`/`isatty` resolve via a temporary `-Wl,-U` (libc
isn't migrated yet) -- flagged in that script's own comment as a
follow-up once it is. The static-archive-flattening trim dance
`build_libdyld.sh`/`build_libsystem.sh` used to need for
`libmalloc_dispatch.o`'s stale `__platform_memset`/`_dyld_get_image_slide`
stand-ins is gone entirely, not just moved -- real dylibs don't have that
collision class at all (see `build_libsystem_malloc.sh`'s own comment).

`libsystem_pthread.dylib`, same real bug, arguably *more* central:
`_pthread_head`/`_pthread_list_lock` (the real, process-wide linked list
of every live `pthread_t`) and `_main_thread`/`_main_thread_ptr` are
plain BSS globals with the identical shape. Two private per-dylib copies
meant a thread `pthread_create()`d from code in one dylib would be
completely invisible to the other dylib's own copy of the thread
registry. This was never caught by this project's existing
`pthread_create()`/contended-mutex QEMU verification (see project
memory) because that test ran inside a single, fully static binary
(pre-dyld) -- a single image can't hit this duplication by construction,
so it's a real gap only this specific migration could have found.

One real, honest gap found and closed while building this dylib, not
guessed around: real dyld's own `_dyld_initializer()` unconditionally
calls `pthread_atfork()` to register its own fork handlers. First
attempt excluded `pthread_atfork.o` entirely (its real
`_pthread_atfork()` needs a real `_os_alloc_once_table` instance nothing
in this project's build ever defined, and nothing in the old fully-static
world ever pulled that member out of the archive either) -- turned out to
be a real, live gap, not a safe skip, the moment this dylib was actually
linked against `libdyld.dylib`. Fixed at the source of the gap: real,
unmodified Apple source for the missing table
(`third_party/xnu-7195/libsyscall/os/alloc_once.c` -- a *different* file
from `libplatform/src/os/alloc_once.c` already in
`libsystem_platform.dylib`; that file's own comment says "Keep in sync
with libplatform alloc_once.c", real Apple's own definition split across
the two repos) is now built into `libsystem_kernel.dylib`. A second,
smaller real gap surfaced right after: `pthread_atfork.c`'s child-side
handler calls two real `pthread.c` functions
(`__pthread_bsdthread_init`/`__pthread_main_thread_postfork_init`) this
project's `build_libpthread.sh` deliberately never compiles (real
pthread.c is out of scope, ported piecemeal elsewhere instead). Both are
only reachable from the child side of an actual `fork()` inside a
pthread-using process -- this project's only `fork()` caller is
`tools/init_binary/init.c`'s PID-1, fully static, no pthread/dyld
involvement at all -- so a small, honestly-documented no-op compat file
(`libsystem_pthread_atfork_compat.c`) stands in; a real fork()-from-a-
dylib-process consumer would need real `pthread.c` ported first.
`libsystem_blocks.dylib`'s temporary `-Wl,-U,_malloc`/`-Wl,-U,_free`
from step 2 is now a real `LC_LOAD_DYLIB` dependency on
`libsystem_malloc.dylib`, as flagged.

`build_libdyld.sh`/`build_libsystem.sh`: `libmalloc.a`/`libpthread.a`
and the entire `libmalloc_dispatch.o` trim-and-rebuild dance removed
from both scripts' dependency checks and link lines; real
`LC_LOAD_DYLIB`s on the two new dylibs instead. The shared, committed
`libmalloc.a`/`libpthread.a` archives themselves are untouched -- every
other still-fully-static consumer (bash, ls, cron, etc) keeps building
against them unchanged, and was never exposed to either duplication bug
in the first place (one process, one image, one heap/one thread
registry, by construction).

**QEMU-verified**: `dyld_test_exec` still runs its own `main()` and
exits with `EXITCODE:1`, no crash, now loading 8 real dylibs total
(`libtest.dylib`, `libdyld.dylib`, `libSystem.B.dylib`,
`libsystem_kernel.dylib`, `libsystem_platform.dylib`,
`libsystem_blocks.dylib`, `libsystem_malloc.dylib`,
`libsystem_pthread.dylib`). Full boot-to-login-shell sequence in the
same test run also unaffected. A pre-existing, unrelated fragility was
noticed (not fixed, out of scope) while rebuilding from scratch:
`build_libpthread.sh`'s own `pthread_smoketest.macho` link step fails
with several undefined symbols (`_getenv`/`_getkill`/etc) when run in
certain rebuild orderings -- happens AFTER `ar rcs "$OUT/libpthread.a"`
already succeeds, so `libpthread.a` itself is unaffected and this
migration's own scripts never hit it, but it's a real, separate
build-script ordering fragility someone should eventually chase down.

### Static-to-dylib migration, step 4: `libsystem_c.dylib` -- DONE
### 2026-08-25, QEMU-verified

Same day. Combines `libc_string.a` + `libc_gen.a` + `libc_stdio.a` +
`libc_printf.a` + `libc_locale_for_stdio.a` + `libc_regex.a` into ONE
real dylib -- a deliberate departure from the original "one migration
step per this project's own `.a` file" pattern, because real Apple does
not split these into separate dylib images either (real macOS has one
`libsystem_c.dylib`, not a `libsystem_string.dylib` +
`libsystem_stdio.dylib` + ...). This project's own `.a` split is purely
a build-pipeline convenience (see `docs/roadmap.md` Phase 4's tier
breakdown), and splitting it into separate DYLIB images specifically
would recreate the exact `mach_task_self_`/`errno`/`g_zone` bug shape a
third time: `libc_stdio`'s real `__stdinp`/`__stdoutp`/`__stderrp` (the
actual `FILE*` globals every `printf`/`fprintf`/`fflush` call goes
through) are plain BSS globals -- splitting stdio from printf (or either
from anything else touching those three `FILE*`s) would give each image
independently-buffered, divergent stdin/stdout/stderr state. Bundling
avoids ever creating that split, rather than fixing it after the fact.

**Real, substantial correction made while writing this script**: a
first attempt linked the 5 (later 6) archives directly rather than
`ar x`-extracting them, hoping `ld`'s normal selective archive-member
scanning would just replicate today's already-working static link. It
compiled -- but left the dylib missing obviously-needed real exports
(`_getenv`, `_memchr`, `_memmove`, `_snprintf`, `_strcmp`, `_strlcpy`,
`_vfprintf`, `_fprintf`, `_mkstemp`), confirmed via `nm -g` showing them
absent. Root cause: selective archive scanning only pulls a member when
something *already in that same link* needs it -- in the old monolithic
static build these get pulled because `libcxx.a`/`libcxxabi.a`/
`libunwind.a` are linked in the *same* invocation and need them, but a
real shared dylib has to export a real, complete API surface for *any*
future consumer, not just whatever else happens to be present today.
Fixed by extracting every member (`ar x`, same approach every dylib
since `libsystem_malloc.dylib`) and closing the real gaps that surfaces
one at a time, same discipline `pthread_atfork.o` already established --
this is the fourth and by far largest application of that lesson.

**Real gaps found and closed extracting everything**, real source or
real narrow compat in every case, no fabricated stubs:
- `sys_siglist`/`strvisx`: real FreeBSD source
  (`gen/FreeBSD/siglist.c`/`vis.c`) simply never in any tier's file list
  before -- compiled fresh.
- 7 real BSD syscalls this project's `create-syscalls.pl` pipeline had
  never generated before (`access`/`ftruncate`/`mkdirat`/`sigprocmask`/
  `read`/`rmdir`/`unlink` -- the last two are real cancelable syscalls
  needing the same triple-underscore-raw-name-plus-public-alias shape
  `___kill`/`_kill` already established; `fstatat`'s real implementation
  turned out to be `_fstatat64.s`, not the empty-placeholder
  `_fstatat.s` the naive per-name loop first tried, discovered by
  actually reading the generator's real output instead of guessing the
  filename).
- `___progname`: a real per-PROCESS global that genuinely lives in the
  MAIN EXECUTABLE's own `third_party/Csu/crt.c` (set from `argv[0]` at
  startup), not something `libsystem_c.dylib` itself should define --
  matches real Apple's own architecture. Marked `-Wl,-U` (same real
  dynamic-lookup mechanism `_dyld_initializer` already uses), resolving
  at runtime against whichever process image actually defines it.
- **A real, live gap discovered only once linked against `libdyld.dylib`
  for real** (same shape as `pthread_atfork.o`'s discovery in step 3):
  real dyld's own `LibSystemHelpers::mkstemp()` (`libdyld/
  LibSystemHelpers.cpp`) needs a real, working `mkstemp()`. Real
  `mktemp.c`'s `mkstemp()` needs real `arc4random_uniform()` --
  previously assumed out of scope (corecrypto-coupled), but real
  `gen/FreeBSD/arc4random.c` already compiles clean under this
  project's standard `-DVARIANT_STATIC` (its own real `#if
  defined(__APPLE__) && !defined(VARIANT_STATIC)` condition is exactly
  Apple's documented gate for routing around `corecrypto/ccrng.h` for
  static/pre-libSystem-init consumers) -- already proven by
  `build_libresolv.sh`'s own identical compile for `res_randomid()`,
  reused verbatim, not re-derived. Closing this cascaded through several
  more callers needing the same fix (`tmpfile.o`, `tmpnam.o`,
  `tempnam.o`), plus two more small real gaps: `mkstemp_dprotected_np()`
  needs real trap 216 (`open_dprotected_np`, `NO_SYSCALL_STUB` in
  `syscalls.master`, same class of hand-written-raw-syscall gap as
  `__ulock_wait`/`thread_selfid` elsewhere in this project -- a real,
  narrowly-scoped wrapper, not a stub, this kernel genuinely implements
  the trap, tagging-only since there's no real per-file encryption on
  this target's storage), and `tmpfile.c`'s own real, internal
  `_sigprocmask` (real Apple's internal single-underscore-prefixed name
  for the same real `sigprocmask(2)`, distinct from the public symbol
  the syscall generator emits under a different linker name) -- a real,
  narrow `asm("__sigprocmask")`-renamed forwarder, correct because this
  project has no real pthread-cancellation machinery for the two names
  to ever diverge over.
- `libc_regex.a` added as a 6th archive for real `regcomp`/`regexec`/
  `regfree` (`rpmatch.c`'s own real dependency) -- already built and
  working via `build_libc_regex.sh` (CLAUDE.md's own reference template
  for this project's whole port methodology), just never previously
  linked into this specific combination.

**Real, honestly-scoped exclusions** (confirmed unreachable in the
current, already-working static link too -- not a behavior change,
documented in `build_libsystem_c.sh`'s own header comment in full):
`machdep_ldisQ.o` (real gdtoa Quad/128-bit-precision `long double`
support, genuinely inapplicable to arm64 where `long double` ==
`double`), and the whole wide-char `strftime`/`wscanf` family
(`wcsftime.o`, `wscanf.o`/`vwscanf.o`/`fwscanf.o`/`swscanf.o`/
`vswscanf.o`/`vfwscanf.o`, all cascading to the same real but
substantial `wcstod_l`/`wcstof_l`/`wcstold_l` locale-integration
undertaking this project's locale work has repeatedly deferred before
-- a real follow-up, not a permanent ceiling).

One real, pre-existing overlap to get right (not a new bug): 38 string
primitives (`strlen.o`, `memcpy.o`, etc) exist in BOTH `libc_string.a`
and `libc_locale_for_stdio.a`. The current static link lists
`libcxx_locale_for_stdio_scratch.a` (== `libc_locale_for_stdio.a` minus
`pthread_tsd_shim.o`) before `libc_string.a`, so locale's copies are
what's actually linked today -- extraction order replicates this
(locale extracted after string, so its copies overwrite on disk, same
outcome as `ld`'s left-to-right archive scan giving the earlier-listed
archive priority).

**QEMU-verified**: `dyld_test_exec` still runs its own `main()` and
exits with `EXITCODE:1`, no crash, now loading 9 real dylibs total.
Full boot-to-login-shell sequence unaffected. A second pre-existing,
unrelated build-script ordering fragility was noticed (not fixed, out
of scope, same class as `build_libpthread.sh`'s own from step 3):
`build_libc_gen.sh`'s own smoketest fails with undefined
`pthread_mutex_*` symbols in certain rebuild orderings, but `ar rcs
"$OUT/libc_gen.a"` already succeeds before that point, so `libc_gen.a`
itself is unaffected and this migration's own scripts never hit it.

### Static-to-dylib migration, step 5: `libc++.dylib` -- DONE
### 2026-08-25, QEMU-verified

Same day, by far the largest and most involved step of this migration.
Combines `libcxx.a` + `libcxxabi.a` into ONE real dylib
(`/usr/lib/libc++.1.dylib`, real Apple's actual install path -- NOT
under `/usr/lib/system/` like every other migration-step dylib so far,
confirmed via `otool -l`, needed a real special case in
`inject_into_sd_image.sh`). `libunwind.a` stays a static archive linked
into it for now (real EH/RTTI personality-routine dependency, same
`libunwind_dyld_shim.o` exclusion `build_dyld_link.sh`/`build_libdyld.sh`
already established) -- not yet its own dylib, that's still batch 6.

**Real correctness reason for combining libcxx+libcxxabi, not just
convenience**: C++ `typeid` equality is commonly a POINTER comparison
of `type_info` objects (confirmed in `private_typeinfo.o`'s
`is_equal()`). Splitting the two into separate dylib images would risk
two independent copies of "the same" type's RTTI data, which could make
`dynamic_cast`/`catch` silently fail across that boundary for logically
identical types -- sidestepped entirely by not creating the split.

**Real gaps found and closed** (each confirmed via `nm -u` tracing to
the exact needing file, not guessed): `catopen`/`catgets`/`catclose`
(real POSIX "no NLS catalogs installed" behavior); `_dyld_find_unwind_sections`/
`_dyld_register_func_for_remove_image`/`dladdr` (same real stub
`build_libsystem.sh`'s own `dyld_unwind_shim_compat.c` already
established -- linking against `libdyld.dylib`'s real implementations
instead would be circular); `___progname` (same real cross-image
`-Wl,-U` lookup `libsystem_c.dylib` already established); `clock_gettime`
(not a raw BSD syscall at all -- combined two already-real primitives,
`gettimeofday`+`mach_absolute_time`/`mach_timebase_info`, behind the
standard POSIX name); `nanosleep` (reused this project's own real,
QEMU-verified Mach-IPC implementation verbatim, Phase 4.6 item 5, plus
its real `libmach_ipc.a` dependency); `strftime_l`/timezone family (real
`locale.o` needs it directly, not just through the already-excluded
wide-char `wcsftime.o` -- real, unmodified `localtime.c`/
`timezone_unix03.c`/`asctime.c`/`strftime.c`/`timelocal.c` compiled
fresh, same real recipe `build_simple_cmds.sh` already established);
`__global_locale`/`__locale_key` (real `locale.cpp` needs Apple's
xlocale "current locale" TSD state directly -- `libsystem_c.dylib`
already has a copy but compiles it hidden/unreachable across the dylib
boundary, so `libc_locale.a`'s own copy is bundled here instead: **a
real, honestly-flagged, currently-inert duplication**, same shape as
`mach_task_self_`/`errno`/`g_zone` but lower-risk since this project
never calls `setlocale()` away from "C" anywhere today); `__pthread_cond_wait`/
rwlock/`pthread_mutexattr_destroy` public aliases (real gaps in
`libsystem_pthread.dylib` itself, step 3 -- real implementations already
existed under real `$UNIX2003`/double-underscore-internal names, just
never got a public alias until `libc++.dylib` needed them, fixed at the
source in `build_libsystem_pthread.sh`, not a new file).

**`libdyld_compat_misc.c` cleanup, found auditing `__cxa_atexit`'s new
home**: this file also carried `_os_alloc_once_table` (duplicate with
`libsystem_kernel.dylib`'s real one from step 3),
`_pthread_bsdthread_init`/`_pthread_main_thread_postfork_init`
(duplicate with `libsystem_pthread_atfork_compat.c` from step 3), and
`mkdirat` (duplicate with `libsystem_c.dylib`'s real syscall from step
4) -- **three more real instances of the exact duplicate-global-state
bug class this whole migration exists to close**, missed until this
specific audit. All three removed; `__cxa_atexit`/`__cxa_finalize`
moved to a new `libsystem_c_cxa_atexit_compat.c` (real Apple
architecture: `__cxa_atexit` genuinely lives in Libc, not libc++abi),
now needed by `libc++.dylib` too.

**Real, honestly-scoped exclusions** (full detail in
`build_libcxx_dylib.sh`'s own header comment, confirmed unreachable in
the current static link too): `thread.o` (needs real `pthread_join`/
`pthread_detach`, neither exists anywhere in this project --
`std::thread` unusable until they do); `condition_variable.o` (needs
real `pthread_cond_timedwait`, only the untimed variant exists --
`std::condition_variable` unusable); `mutex.o` (bundles
`std::timed_mutex`/`std::recursive_timed_mutex` in the same object as
plain `std::mutex`, and the timed variants need `condition_variable.o`
-- **but** its real `__call_once` function, needed pervasively including
by `locale.o`, only needs the already-real untimed primitives, so it's
preserved as a real, verbatim, separately-compiled copy,
`libcxx_call_once_compat.cpp`); `future.o`/`shared_mutex.o` (both real
`condition_variable` consumers, cascading from the same gap);
`cxa_thread_atexit.o` (needs real arm64 TLV support, zero `__thread`
consumers exist); `wcsftime.o` (already-documented wide-char/locale
gap); `debug.o` (real libc++ debug-mode container checking, never
defined by this project's build, confirmed dead code);
`cxa_noexception.o` (real, mutually-exclusive alternate to
`cxa_exception.o` for a `-fno-exceptions`-wide build this project
doesn't use -- a real duplicate-symbol error, confirmed the static
archive never needed both).

**QEMU-verified**: `dyld_test_exec` still runs its own `main()` and
exits with `EXITCODE:1`, no crash, now loading 10 real dylibs total
(7 direct `LC_LOAD_DYLIB`s from `libdyld.dylib`/`libSystem.B.dylib`).
Full boot-to-login-shell sequence unaffected.

**Next candidates**: `libunwind` as its own real dylib (would let
`libc++.dylib` drop its remaining static-archive input), and the
smaller archives (`libcopyfile`/`libremovefile`/`libresolv`) -- batch 6.
The BSD syscall surface for the remaining scattered per-consumer
syscall-stub files (`iokit_trap.s`, `notify_mig_special_reply_port_trap.s`,
`mach_port_extra_traps.s`, `mach_ipc_traps.s` -- confirmed via `grep -rl
"svc #0x80"` across `tools/userland_staging/*.s`; `libunwind_sched_yield.s`
is now also duplicated into `libsystem_pthread.dylib`, real but
redundant, not a correctness risk) is lower priority now that
`libsystem_c.dylib`/`libc++.dylib` cover the vast majority of real
syscall traffic. Once batch 6 lands, `libSystem.B.dylib`/`libdyld.dylib`
should be down to just their own real glue code plus real
`LC_LOAD_DYLIB`s, no bundled static archives left at all. Whoever
continues this should re-run that same `grep -rl "svc #0x80" *.s` scan
first, and should re-audit each new library for the same duplicate-
global-state risk before assuming "just relink it as a dylib" is safe
(three real instances of that exact bug class found so far --
`mach_task_self_`, `errno`, `g_zone`/the pthread registry -- plus a real
but lower-risk fourth, `libc++.dylib`'s own xlocale duplication, and
three more instances of the SAME bug found lurking in
`libdyld_compat_misc.c` this same step, missed until this specific
audit -- worth a deliberate sweep of every remaining loose compat file
for the same pattern before assuming there aren't more).

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
- **kqueue/kevent: real, QEMU-verified 2026-08-25**
  (`tools/userland_staging/kqueue_smoketest.c` +
  `build_kqueue_smoketest.sh`) -- a real `kqueue()`+`kevent()`
  `EVFILT_TIMER` register/wait round-trip passes end to end
  (`qemu/hfs_root_test.img`, real `PASS` output, exit 0). The raw
  `_kevent`/`_kqueue` syscall stubs `build_simple_cmds.sh` already
  compiled and linked defensively into every command (never previously
  called) work correctly the first time they're actually exercised --
  no kernel-side gap here, `third_party/xnu-7195/bsd/kern/kern_event.c`
  is stock, unmodified real kqueue support. Real, separate, still-open
  gap found while verifying this: `printf()` crashes (real `SIGSEGV`)
  under this smoketest's own minimal link recipe -- bisected with raw
  `write(2)` diagnostics (bypassing stdio buffering) to confirm the
  crash is inside `printf()` itself, not `kqueue()`/`kevent()`; not yet
  root-caused (build_libc_printf.sh's own working
  `libc_printf_smoketest.macho` uses a different link recipe and
  presumably doesn't hit this). `pthread_join`/`bsdthread_terminate`
  itself was scoped precisely the same day by reading
  `third_party/libpthread/kern/kern_support.c`'s real `_bsdthread_create`
  -- the real x0-x5 register ABI and `proc_get_threadstart()`/thread-port
  requirements are now understood (this project's `real_bsdthread_create`
  currently only implements a deliberately simplified subset); not yet
  implemented.
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

## Phase 6.7 (Mach IPC completion + `notifyd`/`syslogd`/ASL) -- new

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

**Real current status, 2026-08-27**: `build_libnotify.sh` already builds
a real `libnotify.a` client library, and the `notify` MIG subsystem is
already regenerated both directions (`notify_ipcUser.c` client-side,
`notifyServer.c` server-side, via `build_launchd_mig.sh`), with a passing
smoketest (`libnotify_smoketest.macho`) -- see `tools/userland_staging/`.
So `notifyd` itself (Tier 1, Mach IPC only) is NOT the blocker; it never
was. **The real, single remaining blocker is Mach bootstrap-namespace
resolution**: every one of Libnotify's ~9 public entry points calls
`bootstrap_look_up2(bootstrap_port, NOTIFY_SERVICE_NAME, ...)`
(`third_party/Libnotify/notify_client.c:894`) before it can even reach a
live `notifyd`, and nothing in this project currently holds
`TASK_BOOTSTRAP_PORT` and answers that lookup.

**This does NOT require porting full `launchd`.** Confirmed via
`libbootstrap.c`'s own real source (see this file's own "Real finding 1"
under Phase 6.8's RE-SCOPED section below): `bootstrap_look_up2()`/
`bootstrap_check_in3()` are thin wrappers over `job.defs`'s
`vproc_mig_look_up2`/`vproc_mig_check_in2` MIG routines -- there is no
separate bootstrap protocol to invent or source. The real server-side
code for those routines (`jobServer.c`) is *already generated* by
`build_launchd_mig.sh` and sitting unused (deliberately not compiled yet
-- it needs launchd-internal `job_t`/`launchd.h`/`core.h` types no client
should need, per that script's own header comment). Full `launchd` also
needs a real, working `dispatch_main()` event loop (Phase 6.8's own
"Real finding 2") and 12,162 lines of `core.c` job-management logic that
`notifyd` has zero use for.

**Fastest real path specifically for `notifyd`**: a small, standalone
bootstrap-namespace registrar -- hold `TASK_BOOTSTRAP_PORT`, answer
`job_mig_check_in2`/`job_mig_look_up2` for a handful of well-known
service names (starting with just `NOTIFY_SERVICE_NAME`), reusing
`jobServer.c`'s already-generated code rather than launchd's full job
table. Spawn it from PID-1 before `notifyd` itself, same as item 3 above.
This is a few-hundred-line shim, not a launchd port -- don't wait on
Phase 6.8 to unblock `notifyd`. Not yet implemented; scoped here so the
bootstrap-namespace requirement (and that it's smaller than "needs
launchd") isn't rediscovered from scratch next time this is picked up.
The same shim unblocks real `syslog(3)`/ASL too -- see below, they share
the identical dependency.

### `syslogd` / ASL -- source pinned 2026-08-27, legacy-scope recon DONE (10/10 clean)

**Provenance note, checked directly across `Libc`'s tag history (not
assumed)**: ASL source used to live inside `apple-oss-distributions/Libc`
itself (`gen/asl*.c` + `include/asl.h`) -- confirmed present through
`Libc-825.40.1` (~Lion/Mountain Lion era), confirmed **absent** starting
at `Libc-997.90.3` and every later tag checked, including this project's
own pinned `Libc-1439.40.11` (Darwin 20.3/Big Sur). Apple split it out
into the standalone `syslog` repo sometime in that window. So "ASL is
part of Libc" is real history, just not for this project's actual target
vintage -- the pin below (`syslog-385`) is the correct real source for
Darwin 20.3, independently confirmed against the `macos-112` manifest.

**Real source pinned**: `apple-oss-distributions/syslog` tag
`syslog-385`, confirmed matching the `macos-112` manifest directly
(`git ls-tree macos-112 -- syslog` == commit
`19b7828778996ac0d6d8c6c13d01c816677c5f03` == `syslog-385^{}`, not just
cited from the earlier `apple-oss-catalog.md` sweep). Cloned to
`third_party/syslog`. Real layout, read directly rather than assumed
from the repo name: `syslogd.tproj/` (the daemon, 8552 lines across
`syslogd.c`/`daemon.c`/`bsd_in.c`/`bsd_out.c`/`udp_in.c`/`klog_in.c`/
`asl_action.c`/`dbserver.c`/`remote.c`), `libsystem_asl.tproj/src/`
(the real ASL **client** library -- `asl.c`/`asl_client.c`/`syslog.c`/
etc, 14,555 lines, this is real `asl_open`/`asl_log`/`syslog(3)` source,
not a stub), `aslcommon/` (shared `asl_ipc.defs` MIG interface +
`asl_memory.c`), `aslmanager.tproj/` (log rotation/archival), `util.tproj/`
(the `syslog(1)` CLI).

**Real finding, corrects an earlier wrong assumption**: this is NOT the
old-style Unix-socket-only BSD syslogd from memory -- it's the full
Big Sur-era ASL daemon. Confirmed directly: `libsystem_asl.tproj/src/
asl.c:271`'s `asl_open()` calls `bootstrap_look_up2(bootstrap_port,
ASL_SERVICE_NAME, ...)` unconditionally (checked, no Unix-domain-socket
fallback exists anywhere in the client library -- grepped `asl.c`/
`asl_client.c`/`asl_core.c` for `AF_UNIX`/`/var/run/syslog`/`sun_path`,
zero hits). `syslog(3)` itself (`libsystem_asl.tproj/src/syslog.c`) is a
thin wrapper over `asl_open()`/`asl_log()`, so it inherits the same
dependency. `syslogd.tproj/dbserver.c` (the live ASL query/ingest
service real `asl_log()` clients actually reach) additionally uses real
XPC (`xpc_copy_entitlement_for_token` etc, confirmed via grep) for
per-client entitlement checks. **Same real blocker as `notifyd`
above: bootstrap-namespace resolution, not a separate gap.**

**But a real, smaller, Mach-free legacy path exists inside the same
pin, verified by reading it, not assumed**: `bsd_in.c` (172 lines) is a
plain `AF_UNIX`/`sockaddr_un` listener on `/var/run/syslog` (real
`recvfrom()`, dispatched via a real `dispatch_source_t`/`dispatch_queue_t`
-- this project already has a working `libdispatch.dylib`, see Phase
6.6), receiving old-style datagram `syslog()` calls from processes that
don't link the ASL client at all. `udp_in.c` (network syslog receiver)
and `klog_in.c` (kernel message reader) are similarly small and
Mach-free. `daemon.c`/`syslogd.c` are the real event-loop glue tying
input modules to output modules (`bsd_out.c` writing to real log files
being the relevant one for a first cut).

**Real scoped recommendation, not yet implemented**: build a
**legacy-only `syslogd`** first -- `syslogd.c` + `daemon.c` + `bsd_in.c`
+ `udp_in.c` + `klog_in.c` + `bsd_out.c`, explicitly excluding
`dbserver.c` (real ASL live-query Mach/XPC service) and `remote.c`
(network log forwarding, not needed yet). This gives every command a
real place to log to (`docs/roadmap.md`'s original Phase 5.5 motivation
for this item) via the Unix-domain-socket path, with zero bootstrap
dependency -- and can replace this project's existing
`ifconfig_syslog_compat.c` stderr-only `syslog()` fallback for real,
once wired. The full ASL client library (`libsystem_asl.tproj`, real
`asl_open`/`asl_log`/`syslog()`) and `dbserver.c` both wait on the same
bootstrap-namespace registrar shim scoped above for `notifyd` -- build
that once, it unblocks both. `aslmanager.tproj`/`util.tproj` are lower
priority (log rotation, the `syslog(1)` query CLI) and depend on
whichever of the above lands first.

**Also corrects two now-stale "confirmed never open-sourced" claims**
found while investigating this (both filed during earlier CF/launchd
work, before this pin existed): `docs/backlog.md`'s item d6/d7 ASL
notes, and `tools/userland_staging/ifconfig_syslog_compat.c`'s own
header comment -- fixed in the former, the latter still functions
correctly as a fallback and doesn't need code changes yet, just flagging
that its comment is now stale (the real replacement exists, just isn't
wired in).

**Real recon DONE 2026-08-27 -- `tools/userland_staging/
build_syslogd_recon.sh` (new): 10/10 legacy-scope files compile clean,
more than expected.** Real `-fsyntax-only` trial compile (same
methodology as `build_launchd_recon.sh`) of the full legacy-only file
set: `klog_in.c`/`bsd_in.c`/`bsd_out.c`/`udp_in.c` (Mach-free input/
output modules), `daemon.c`/`syslogd.c` (event-loop glue -- real
exercises this project's actual `libdispatch`, not just a syntax
formality: `daemon.c`'s tail calls `database_server()` then
`dispatch_main()`), `aslcommon/asl_common.c`/`asl_memory.c` (shared
support code), and -- a real surprise, both had been provisionally
scoped as "probably needs XPC/CF, defer" based on their `#include` lines
alone -- `asl_action.c` and `remote.c`, neither of which turned out to
have any real `xpc_*`/CoreFoundation call site once actually compiled
against real headers. Only `dbserver.c` itself (the live ASL Mach/XPC
query service) remains unattempted, needing a real `asl_ipc.h` MIG
regen from `aslcommon/asl_ipc.defs` first (bounded, same pattern as
`notify_ipc.defs`, just not done yet).

Three real, narrow compat headers were needed (all in
`tools/userland_staging/syslog_compat_headers/`, each documented in its
own header comment): `sys/kevent_qos_compat.h` (real `struct
kevent_qos_s`/`kqueue_id_t`, verbatim from `xnu-7195`'s kernel-internal
`bsd/sys/event.h` -- confirmed absent from this project's pinned SDK's
exported `<sys/event.h>` even under `-DPRIVATE=1`, needed because
`daemon.c` directly `#include <libproc.h>` -> `<sys/proc_info.h>`, which
has a real `PRIVATE`-gated struct field of this exact type);
`os/transaction_private.h` (real `os_transaction_t` SPI has no source
anywhere in this project -- honest no-op, since this project's PID-1 has
no idle-exit lifecycle for spawned daemons to hook into regardless);
`configuration_profile.h` (real MDM/enterprise-profile API, confirmed
never open-sourced as its own repo -- honest no-op, both real call sites
already null-check the return, confirmed by reading them before writing
the stub). `servers/bootstrap.h` (`syslogd.c`'s own real `#include`)
resolves for real via PureDarwin's already-pinned libxpc port, not a new
gap.

**Concrete next steps, in priority order**: (1) real `asl_ipc.defs` MIG
regen to unblock `dbserver.c` (bounded, mechanical); (2) compile the
10 clean files to real `.o`s and archive them -- this recon script only
proves compilability, not link-readiness, same next-step shape
`build_launchd_recon.sh` had before `build_launchd_core.sh`; (3) a real
trial link against this project's existing libc/libpthread/libdispatch/
libmach_ipc stack to find the real remaining undefined-symbol list; (4)
wire the resulting `syslogd.macho` into PID-1's spawn list (matching
`getty`'s built-but-not-yet-wired-in precedent) once it links and runs
clean under QEMU.

## Phase 6.8 (`launchd` + XPC via PureDarwin) -- RE-SCOPED 2026-08-25,
## real source swap + two real prerequisite findings

**Real source swap**: everything below this point that cites
`PureDarwin/XPC` (the standalone, archived, frozen-at-2014-vintage repo)
is superseded. Found and verified 2026-08-25: **PureDarwin/PureDarwin's
own `next` branch** (their active-development branch -- `main` is frozen
releases per their own README, `git ls-remote` confirmed `next` really
exists and is the actively-pushed one) is a full, current, actively
maintained monorepo that **already boots real launchd to a shell on this
project's exact SoC** -- `nix/arm64.nix`/`nix/image-contents.nix` build
real `kernel-arm64-bcm2837`/`puredarwin-rpi3.img` targets, confirmed by
direct file read, not assumed from the repo name. This is a categorically
better real reference than the frozen 2014 base this section originally
scoped around. Sparse-cloned (`git sparse-checkout`, `src/Libraries/XPC`
+ `src/Libraries/CrashReporterClient` + `cmake/` only, 7.9MB vs. the full
monorepo's 514MB) as `third_party/PureDarwin`, pinned in
`setup_third_party.sh` (see that script's own header comment). Real
directory layout found there: `src/Libraries/XPC/{launchd,libxpc,notify,
libinfo,libnv,launchctl,logd}` -- notably this also includes a real
`notify`/`notifyd` port, a second real reference for Phase 6.7's item 2
alongside the already-pinned `apple-oss-distributions/Libnotify`.
PureDarwin's own new code in this tree (the `pd_*.c` files --
`pd_launchd_main.c`, `pd_launchd_boot.c`, `pd_launchd_plist.c`,
`pd_console_login.c`, `pd_sandbox_check.c`, `pd_xpc_object.c`,
`pd_xpc_object_class.m`, `pd_xpc_event_publisher.c`, `pd_notifyd_compat.h`
-- exact names, confirmed by directory listing) is real, already-written,
already-working compat glue for precisely the CF-free/sandbox-free/no-
real-XPC-internals gaps this project's own research already anticipated
-- a real reference implementation to adapt from, not a blank page.
License: same real per-file `@APPLE_APACHE_LICENSE_HEADER_START@`
(Apache 2.0) on Apple-originated files as the old source, plus
PureDarwin's own permissive BSD-style `@PUREDARWIN_LICENSE_HEADER_START@`
header on their new files -- both confirmed by direct file read, neither
blocking.

**Real finding 1 (corrects this doc's own "confirm whether
`bootstrap.defs` exists" open question above): no separate
`bootstrap.defs` MIG file exists anywhere, and none is needed.** Modern
(10.5+) launchd never implements the classic NeXT-era bootstrap protocol
this doc's "newly-surfaced prerequisite gap" framing assumed --
`bootstrap_look_up2()`/`bootstrap_check_in3()`/etc (the exact client API
`Libnotify/notify_client.c:894` calls, confirmed) are thin wrappers in
`libxpc/libbootstrap.c`, implemented entirely in terms of `job.defs`'s
`vproc_mig_look_up2`/`vproc_mig_check_in2`/`vproc_mig_lookup_per_user_
context`/etc MIG routines -- confirmed by reading `libbootstrap.c`'s own
body, not assumed from headers. `job.defs`/`job_types.defs`/
`job_forward.defs`/`job_reply.defs`/`notify.defs`/`internal.defs`/
`domain.defs` (the newer XPC-domain concept, PureDarwin's tree has one
more `.defs` file than the old archived source did) are all real,
present, complete MIG interface files already in this checkout -- the
real prerequisite work is regenerating client+server stubs from these
(same `mig`-regeneration pattern `build_libmach_ipc.sh`/
`build_iokituser.sh`/`build_libnotify.sh` already established), not
inventing or sourcing a missing `.defs` file. Kernel-side
`TASK_BOOTSTRAP_PORT` special-port plumbing (`task_get_special_port`/
`task_set_special_port`) is real, stock, already-general xnu-7195
behavior, confirmed via `libbootstrap.c`'s own `task_get_special_port
(task_self_trap(), TASK_BOOTSTRAP_PORT, &bootstrap_port)` call, which any
process on this target can already issue today -- what's still genuinely
missing is a real process holding the *receive* right and answering
`job_mig_*` requests, i.e. launchd itself.

**Real finding 2, CORRECTED same day after a deeper check (original claim
was wrong -- flagging the correction rather than quietly editing it away,
per this project's own verify-before-trusting convention): launchd needs
neither CoreFoundation nor real ObjC-backed XPC internals, but it DOES
need a real, working libdispatch main-queue engine -- the original
"link-graph-only" check below was real but insufficient.** The
`CMakeLists.txt` link-graph read (`XPC_libxpc_static`'s only declared
deps are `CrashReporterClient`, `libsystem_kernel_static`,
`libc_features`; `XPC_launchd_static` adds `XPC_launchd_mig_static`/
`XPC_libxpc_static`/`XPC_libinfo_static`, still zero explicit CF/
libdispatch) is accurate as far as it goes, but a follow-up grep for
actual `dispatch_*` call sites (not just the `dispatch_queue_t`/
`dispatch_source_t` struct fields checked before) found 45 real, live
call sites across `libxpc/{pd_xpc_object,libvproc,xpc_connection,
xpc_type}.c` and, decisively, `launchd/{core,log,runtime}.c` -- i.e.
launchd's own real main-loop code, not just optional XPC-connection
plumbing. `runtime.c`'s `launchd_runtime()` -- the actual function
`launchd.c`'s `main()` calls to run forever -- is `launchd_runtime2(...)`
(real kqueue event processing) followed unconditionally by
`dispatch_main()`, which by real Apple/libdispatch semantics never
returns and drains the default GCD queue forever. So real launchd's
architecture does genuinely depend on a working dispatch engine at
runtime; the link graph just doesn't show it explicitly because
PureDarwin's own monorepo satisfies it at a later, whole-image link
stage (their own real `src/Libraries/libSystem/libdispatch` port, not yet
sparse-cloned into this project's `third_party/PureDarwin` -- only
`src/Libraries/XPC` + `CrashReporterClient` + `cmake` are, see that pin's
own header comment) rather than per-static-library. Revised, honest
scope: a header-only `<dispatch/dispatch.h>` (real Apple header, already
in this project's cloned-but-unbuilt `third_party/libdispatch`) is enough
to get launchd/libxpc to *compile*, but a real, minimal
`dispatch_main()`/`dispatch_async()`/`dispatch_source_*` implementation
-- not necessarily full multi-thread GCD, this project's existing
single-thread-honest framing elsewhere may still be enough for a first
cut, since `launchd_runtime2()` appears to do the real kqueue work itself
and `dispatch_main()`'s job may just be "don't return, run whatever gets
`dispatch_async`'d" -- is genuinely needed for launchd to *run*, not just
link. This softens but doesn't fully retract the original conclusion:
re-sequenced: attempt Phase 6.8 directly against a header-only dispatch
shim, treat full Phase 6.6 as a later, independent upgrade (real GCD
semantics for whichever specific XPC code paths turn out to need them at
actual link/run time, discovered the same real-trial-link way every
other tier in this project has been, not guessed up front).

**Step (1) DONE 2026-08-25 (`tools/userland_staging/build_launchd_mig.sh`,
new)**: all 7 real subsystems (`job`, `job_forward`, `job_reply`,
`internal`, `domain`, `notify`, `mach_exc`) regenerate cleanly (zero
`mig` errors/warnings) against this project's real xnu-7195 `osfmk`
headers. The 5 real CLIENT-side files (`job_user.c`,
`job_forward_user.c`, `job_replyUser.c`, `internal_user.c`,
`domain_user.c` -- `notify`/`mach_exc` are server-only subsystems, no
client side exists) compile clean and their entire undefined-symbol
surface (`mach_msg`, `mach_msg_destroy`, `mig_strncpy`,
`mig_strncpy_zerofill`, `voucher_mach_msg_set`, `mig_get_reply_port`,
`mig_put_reply_port`, `mig_dealloc_reply_port`, `NDR_record`) is already
satisfied by this project's existing `libmach_ipc.a`/`libmalloc.a` --
zero new compat code needed. One real, narrow header gap closed:
`bsm/audit_record.h` (pulled in transitively via `vproc_internal.h` ->
launchd's own `runtime.h` -> the SDK's real `bsm/libbsm.h`) isn't in
`third_party/BSM` (that repo only ships the userland half,
`bsm/lib/libbsm.h`) -- found real and already present in
`third_party/xnu-7195/bsd/bsm/audit_record.h` (the other, xnu-published
half of the real BSM header split), no fabrication needed. Output:
`libc_build/liblaunchd_mig.a`, real smoketest
(`launchd_mig_smoketest.macho`, fully static, verified via `otool -l`)
calling the real `vproc_mig_look_up2()` -- the exact routine
`libbootstrap.c`'s `bootstrap_look_up2()` calls -- against
`MACH_PORT_NULL`, not yet QEMU-run (no real Mach round trip to confirm
without a real launchd on the other end, same honest-scope caveat
`libnotify_smoketest.c` documents for its own analogous test). The 5
real SERVER-side files (`jobServer.c`, `job_forwardServer.c`,
`job_replyServer.c`, `internalServer.c`, `domainServer.c`, plus
`notifyServer.c`/`mach_excServer.c`) are generated but deliberately NOT
compiled by this script -- confirmed by real trial compile that they
need launchd's own internal `job_t` type (from `launchd.h`/`core.h`,
launchd-internal headers no client should ever need) and hit one other
real, narrow, deferred gap of their own (`memchr()` has no prototype in
this project's minimal `libsyscall/mach/string.h` shim) -- left for
whoever picks up step (5) below, compiled directly alongside launchd's
own sources.

**Step (4) DONE 2026-08-25 (`tools/userland_staging/build_libdispatch_min.sh`
+ `dispatch_min_compat.c`, new)**: NOT a header-only shim after all --
Real finding 2 above was itself corrected the same session (see its own
"CORRECTED same day" note) after a deeper grep found 45 real
`dispatch_*` call sites including `launchd/runtime.c`'s unconditional
`dispatch_main()` tail call, so this step became a real, working,
honestly single-thread engine instead: `dispatch_once`/`dispatch_once_f`
(real one-time-init), `dispatch_async`/`dispatch_sync`/their `_f`
variants (execute immediately, synchronously -- single-thread-honest),
`dispatch_main()` (a REAL `kqueue(2)`/`kevent(2)` wait loop, this
project's own already-QEMU-verified syscalls), `dispatch_source_create`/
`_resume`/`_suspend`/`_cancel` for exactly the 2 real source types
third_party/PureDarwin's actual source calls
(`DISPATCH_SOURCE_TYPE_PROC`/`DISPATCH_SOURCE_TYPE_MACH_RECV`, confirmed
via grep, not guessed), and `dispatch_semaphore_create` (real, wraps
already-built `semaphore_create`). Real compat details worth knowing for
whoever touches this next: build with `-DOS_OBJECT_USE_OBJC=0` (real
Apple opt-out for non-ObjC targets, NOT
`-D__DISPATCH_BUILDING_DISPATCH__`, which was tried first and found to
strip the public typedefs this file needs); `dispatch_get_main_queue()`
is baked as an inline function directly into the real public header, so
this file defines the real backing global (`_dispatch_main_q`) it reads
rather than defining the function itself (would collide);
`dispatch_once`/`dispatch_once_f` need an `#undef` first for the same
real reason (the header's own inline fastpath macro-renames the public
name to `_dispatch_once`/`_dispatch_once_f`, which would otherwise
rewrite this file's real implementations' names out from under them);
`dispatch_object_t` is a real transparent_union, accessed via its common
`._do` member. Two real, honest, NOT-yet-closed gaps (documented in the
script's own header comment, not fabricated over):
`semaphore_signal`/`semaphore_wait` (the real Mach traps -- a genuinely
different primitive than `simple_cmds_nanosleep_compat.c`'s
`__semwait_signal`, checked directly, can't reuse that existing work)
have no real implementation anywhere in this project yet; `kevent`/
`kqueue` do (real, QEMU-verified, `libc_build/simple_obj/sys__kevent.o`/
`sys__kqueue.o`) but need linking in directly at final-executable-link
time, not from this script's own archive.

**Step (3) DONE 2026-08-25, and much smaller than scoped -- turned out to
be almost entirely self-resolving, not a header-writing task at all.**
Real finding: `core.c`'s own `config.h` gates all 4 headers behind
`__has_include`/`TARGET_OS_EMBEDDED` checks, not unconditional includes
(confirmed by reading both files directly). On this project's real
build target (`-target arm64-apple-ios14.4`, confirmed
`TARGET_OS_EMBEDDED=1` via a real trial preprocess): `HAVE_SYSTEMSTATS`
and `HAVE_LIBAUDITD` both resolve to `0` automatically (both gated
`!TARGET_OS_EMBEDDED`) -- systemstats/libauditd code paths simply don't
compile in, zero stub needed. `HAVE_QUARANTINE`/`HAVE_RESPONSIBILITY`
also resolve to `0` -- confirmed via real `find` that this project's
pinned Xcode-12 iPhoneOS14.4 SDK has neither `quarantine.h` nor
`responsibility.h` at all. Only `HAVE_SANDBOX` resolves to `1` (the SDK
DOES have a real `sandbox.h`) -- and PureDarwin already has a complete,
real, honest implementation for exactly this one case:
`launchd/pd_sandbox_check.c` (47 lines -- `sandbox_check()`/
`sandbox_init()`/`sandbox_check_by_audit_token()` all real, documented
"no Sandbox.kext MAC policy exists, so 'allowed' is the correct,
documented no-profile behavior" no-ops, not fabricated stand-ins) plus
`launchd/System-compat/sandbox.h` (real type/macro compat that itself
`#include_next`s the SDK's real `sandbox.h` when present). Net: zero new
header-writing work needed for this step at all -- just remember to put
`System-compat/` ahead of the SDK on the include path at trial-link time
(step (5)) so PureDarwin's own compat header wins, and compile
`pd_sandbox_check.c` into the launchd archive.

**Step (5) DONE for its libxpc half, 2026-08-25
(`tools/userland_staging/build_libxpc.sh`, new) -- real, major result:
all 12 real `libxpc/*.c` files + `helper_user.c`/`helperServer.c` (from
`libxpc/helper.defs`) + all 3 real `libnv/*.c` files (nvlist/nvpair/
libsbuf, the real name/value-list library XPC's wire format is built on)
compile CLEAN against this project's real toolchain -- zero fabrication
beyond two small, real, narrowly-scoped compat headers
(`tools/userland_staging/launchd_compat_headers/os/{workgroup,assumes}.h`
-- see that script's own header comment for exactly why each is real and
correct, not a stand-in for missing logic). NOT compiled:
`pd_xpc_object_class.m` (real Objective-C -- this project has no ObjC
runtime; PureDarwin's own real, working non-ObjC retain/release path in
`pd_xpc_object.c` is what's actually exercised instead, a deliberate,
narrower, documented scope choice).**

A real trial link (this script's own smoketest -- genuinely calls
`xpc_dictionary_create`/`xpc_connection_create`/`bootstrap_look_up`, not
an empty `main()`, which was tried first and found to prove nothing:
static linking only pulls in archive members actually referenced
transitively, so an empty `main()` gave a meaningless "0 undefined
symbols" result) against this project's full curated stack
(`libmach_ipc`/`libclosure`/`libmalloc`/`libplatform`/`libc_string`/
`libc_locale_for_stdio`/`libc_stdio`/`libc_gen`/`libc_printf`/
`libpthread` + this phase's own `liblaunchd_mig.a`/`libdispatch_min.a`)
finds exactly **47 real, bounded, categorized undefined symbols** -- a
real, major de-risking of this whole phase, not a full close-out yet.
Rough categories (full list in `build_libxpc.sh`'s own header comment):
real BSD syscalls never wired (`connect`/`dup`/`kill`/`recvmsg`/
`select`/`sendmsg`/`socket`/`unsetenv`/`time` -- same
create-syscalls.pl-stub pattern every prior tier already used, some
siblings like `getuid`/`geteuid`/`dup` already exist in
`libc_build/simple_obj/`, just need the missing ones added); real Mach
routines never wired (`mach_error_string`/`mach_msg_send`/
`mach_msg_server_once`/`mach_port_allocate`/`mach_port_get_context`/
`mach_port_set_context`/`mig_deallocate`/`semaphore_signal`/
`semaphore_wait` -- the last pair is `build_libdispatch_min.sh`'s own
already-flagged gap); `os_log`'s real logging engine (`os_log_create`/
`os_log_type_enabled`/`_os_log_impl` -- genuinely unbuilt, no logging
daemon exists here); `gCRAnnotations` (CrashReporterClient.c's own
global -- the real 13-line source is already sparse-cloned at
`third_party/PureDarwin/src/Libraries/CrashReporterClient`, just isn't
compiled into this specific smoketest's link line yet); `proc_get_dirty`/
`proc_set_dirty`/`proc_track_dirty`/`task_name_for_pid` (real libproc
routines); `pthread_rwlock_rdlock`/`_unlock` (check `libpthread.a`'s
exact exported name before assuming missing -- not yet checked);
`fileport_makefd`/`fileport_makeport`; the `_chk`-suffixed hardening
routines (`__memmove_chk` etc -- likely need a small real
`_FORTIFY_SOURCE`-shim, same class of gap other tiers may have already
solved, not yet checked); and the one genuinely structural item, real
`_OBJC_CLASS_$_OS_xpc_connection`/`_OBJC_CLASS_$_OS_xpc_object` symbols
(`pd_xpc_object.c`'s `OS_OBJECT_OBJC_CLASS_DECL()` declares these
unconditionally even though its own real retain/release logic never
calls `objc_msgSend` against them -- confirmed by reading that file's
own body; likely closeable with a minimal placeholder data symbol rather
than standing up a real ObjC runtime, not yet attempted).

**47-symbol list: 28 of 47 CLOSED, 2026-08-25
(`tools/userland_staging/build_libxpc_syscalls.sh`, new)**. Real Apple/
FreeBSD source, compiled as-is or curated the same way every other tier
in this directory already does (each new compat file's own header
comment has the specific real provenance) -- not fabricated. Closed:
`mach_error_string`/`mig_deallocate`/`gCRAnnotations` (compile as-is);
`unsetenv`/`setenv`/`putenv` (real FreeBSD `setenv.c`, needed a real
honest no-op `__environ_lock`/`__environ_unlock` pair -- missing from
this project's pinned Libc entirely -- and a curated `__findenv_locked`);
`proc_get_dirty`/`proc_set_dirty`/`proc_track_dirty`/`task_name_for_pid`'s
libproc siblings (real, unmodified `libproc.c`, pulling in
`__process_policy`/`__thread_selfid`/`proc_rlimit_control` -- satisfied
by real stubs already sitting in `libc_build/dyld_syscall_gen/` from
this project's own dyld port, reused rather than regenerated);
`select`/`connect`/`recvmsg`/`sendmsg`/`kill`/`socket`/
`fileport_makefd`/`fileport_makeport` (real Apple logic curated without
the `_NONSTD_SOURCE`/`NO_SYSCALL_LEGACY` preprocessor scaffolding those
specific wrapper files use, confirmed via real trial compile to be
incompatible with this project's LP64/arm64-only
`__DARWIN_ONLY_UNIX_CONFORMANCE` target -- real Apple's own arm64/iOS
libsyscall build excludes these exact files per-arch for the same
reason); `mach_msg_send`/`mach_port_allocate` (real bodies curated from
`mach_msg.c`/`mach_port.c`, two new real trap stubs);
`semaphore_signal`/`semaphore_wait` (real bodies curated from
`semaphore.c`, two new real trap stubs -- `semaphore_signal_trap`/
`semaphore_wait_trap`, -33/-36). The `$UNIX2003`-suffixed symbols were
never a real gap -- this project's own established `-Wl,-alias` link
pattern, now included in the trial-link step.

**19 real symbols still open**, honestly documented (not fabricated
over) in `build_libxpc_syscalls.sh`'s own header comment: the `_chk`-
suffixed hardening routines (real `_FORTIFY_SOURCE`-shim source not yet
located); `os_log_create`/`os_log_type_enabled`/`_os_log_impl` (real
logging engine, genuinely unbuilt); `mach_port_get_context`/
`mach_port_set_context` (real MIG routines, not simple traps -- need an
actual `mach_port.defs` regen, a bigger lift than a curation);
`mach_msg_server_once`/`dispatch_data_create_map`/`kdebug_trace`/
`__mach_errors` (that last one is `error_codes.c`'s own data table --
needs `libkern/OSReturn.h`, not yet vendored) -- none of these 4
investigated yet; `task_name_for_pid` (real kernel-side implementation
confirmed in `bsd/vm/vm_unix.c`, but genuinely has NO
`syscalls.master` entry in this pinned xnu-7195 -- the real userspace
call mechanism isn't a simple missing trap, not yet identified);
`uuid_unparse_upper` (the only real source found,
`libkern/uuid/uuid.c`, is genuinely kernel-only -- needs
`read_random`/`nanotime`/`mount_t`; the real userland Libc equivalent
hasn't been located); and the two real
`_OBJC_CLASS_$_OS_xpc_connection`/`_OBJC_CLASS_$_OS_xpc_object` symbols
(likely closeable with a minimal placeholder data symbol, not yet
attempted -- see build_libxpc_syscalls.sh's own header comment for why
a full ObjC runtime isn't actually needed here).

**Remaining concrete next steps for whoever picks this up**: (2) real
BSM (`third_party/BSM`, tag `bsm-13`, now pinned, already confirmed
working in step (1)'s own real compile) for `bsm/auditd_lib.h`/
`bsm/libbsm.h`; close the remaining 19-symbol list above; then attempt
the actual launchd DAEMON side
(`core.c` -- ~12K LOC, `ipc.c`, `launchd.c`, `log.c`, `runtime.c`, plus
the 5 real SERVER-side MIG files `build_launchd_mig.sh` already generated
but didn't compile -- `jobServer.c` etc, which need launchd's own
internal `job_t` type from `core.h`/`launchd.h`), which this pass did
NOT attempt at all -- the libxpc client-library work above only proves
the CLIENT half of Phase 6.8, not the actual PID-1-replacement daemon
logic itself, which is where most of the remaining real unknowns are.

### `build_libxpc_syscalls2.sh` -- DONE 2026-08-26, closes 14 of the 19
### still-open symbols above

Second pass over the 19-symbol list, real Apple/FreeBSD source
throughout (see the script's own HONEST SCOPE STATEMENT header for full
per-symbol provenance). Real findings worth recording:

- The six `_chk`-suffixed `_FORTIFY_SOURCE` routines
  (`__memmove_chk`/`__strcpy_chk`/`__stpcpy_chk`/`__stpncpy_chk`/
  `__strlcpy_chk`/`__strncpy_chk`/`__vsnprintf_chk` -- 7, not 6, one more
  turned up than the original list named) all have real, unmodified
  source at `third_party/Libc/secure/*.c`, each just `#include
  "secure.h"` (the real declaring header, same directory) -- the
  original "not yet located" note in the 19-symbol list was simply
  because nobody had looked in `Libc/secure/` yet, not a real gap.
  `secure.h`'s own 4 declared symbols (`__chk_fail_overflow`/
  `__chk_fail_overlap`/`__chk_overlap`/`__chk_assert_no_overlap`) are
  curated from `Libc/secure/chk_fail.c` -- real algorithm, `os_crash()`
  swapped for plain `abort()` since this target has no crash-reporter/
  exception-port pipeline (the file's own `PR_13085474_CHECK` compat
  shim already compiles out to nothing here, `TARGET_OS_OSX` being 0 on
  this iOS-target build -- same as real Apple's own iOS build of this
  file).
- `__darwin_check_fd_set_overflow` -- real bounds-check logic curated
  from `xnu-7195/libsyscall/wrappers/terminate_with_reason.c`, same
  `os_fault_with_payload()`-to-`abort()` swap for the same reason.
- `uuid_unparse_upper` -- the 19-symbol list's own note ("only real
  source is kernel-only `libkern/uuid/uuid.c`") was an incomplete
  search: the real userland implementation is
  `third_party/Libc/uuid/uuidsrc/unparse.c`, small, self-contained,
  compiles unmodified (needs `-DHAVE_STDINT_H=1` so its own `uuidP.h`
  takes the `<stdint.h>` branch instead of a `<uuid/uuid_types.h>` this
  project doesn't have) -- also gives `uuid_unparse`/`uuid_unparse_lower`
  for free, same file.
- `task_name_for_pid` -- confirmed genuinely absent from
  `syscalls.master` (it's a Mach trap, not a BSD syscall);
  `osfmk/mach/syscall_sw.h` has `kernel_trap(task_name_for_pid,-44,3)`,
  hand-copied into a 3-instruction stub the same way every other raw
  Mach trap in this project already is. `task_for_pid` (trap -45)
  deliberately not stubbed -- no real call site exists yet.
- `time()` -- real, unmodified `third_party/Libc/gen/FreeBSD/time.c`,
  a thin `gettimeofday()` wrapper; wasn't in the original 19-symbol
  list's own writeup at all (an omission in that list, not a new find).
- `mach_msg_server_once` -- curated from `mach_msg.c`'s real body (same
  file this project's `mach_msg_send_compat.c` was already curated
  from). Real, deliberate scope note: the original body's
  `voucher_mach_msg_adopt()`/`voucher_mach_msg_revert()` calls go to
  `libdispatch/src/voucher.c`, a large file with its own internal
  voucher-table refcounting machinery this project has not ported --
  replaced with honest local no-ops reporting "unchanged" (the real,
  correct behavior for a message carrying no voucher, the only case this
  target's single-thread server loop can hit right now).
- `mach_port_get_context`/`mach_port_set_context` -- turned out to be a
  real MIG regen, not a raw trap, but a cheap one: `osfmk/mach/
  mach_port.defs` has its own `UserPrefix _kernelrpc_;` pragma gated
  `#if !KERNEL && !LIBSYSCALL_INTERFACE`, so a plain client-side mig
  regen of that file (no special flags needed) produces
  `_kernelrpc_mach_port_get_context`/`_kernelrpc_mach_port_set_context`
  directly -- exactly the names `mach_port.c`'s own real
  `mach_port_get_context`/`mach_port_set_context` wrapper bodies already
  call. That regen's other ~25 client routines in the same subsystem
  (`mach_port_allocate`, `mach_port_deallocate`, etc.) come along for
  free at zero extra cost, since MIG client stubs are self-contained.
- The two `_OBJC_CLASS_$_OS_xpc_connection`/`_OBJC_CLASS_$_OS_xpc_object`
  symbols -- confirmed via grep across all of `third_party/PureDarwin`
  that both are only ever used as `isa` sentinel pointers, compared by
  address, never dereferenced as real classes. Closed with two minimal,
  distinct real data symbols under the exact raw linker names (`__asm__`
  symbol renaming), not a functional stand-in.

**5 real symbols still open, honestly narrowed down** (not fabricated
over -- see `build_libxpc_syscalls2.sh`'s own header comment):
`os_log_create`/`os_log_type_enabled`/`_os_log_impl` (os_log's real
engine -- confirmed via `find`/`grep` across every pinned repo in this
project that Apple has never open-sourced this as a standalone
component anywhere in this project's source universe; no logging daemon
exists on this target either, so even a real port would have nothing to
talk to yet); `dispatch_data_create_map` (real source exists,
`libdispatch/src/data.c`, but its `dispatch_data_t` is a fully
refcounted object built on real libdispatch's object/base machinery --
incompatible with this project's `libdispatch_min.a`, a deliberately
minimal single-thread shim with its own, different `dispatch_data_t`
representation; the one real call site,
`xpc_data_create_with_dispatch_data()` in `libxpc/xpc_type.c`, is a
narrow data-conversion helper, not on launchd's own startup path);
`__mach_errors` (the string table `mach_error_string.c` indexes into --
confirmed genuinely absent from every pinned Apple source in this
project; real Apple generates it at build time from a private,
not-open-sourced err_codes table). `kdebug_trace` was on the original
19-symbol list but turned out not to be a real gap at all -- it's
already handled by this project's own established `-Wl,-alias,
___kdebug_trace,_kdebug_trace` pattern (same as the `$UNIX2003`
aliases), the raw trap (`libc_build/sys_kdebug_trace.o`) already exists.

### Real `libdispatch` port -- STARTED 2026-08-26, 14/14 attempted core
### files syntax-clean; not yet compiled to real .o/archived/linked

User-directed course correction 2026-08-26: `dispatch_min.a` (the
393-line hand-written single-thread shim referenced just above) is
exactly the class of gap this project's own conventions treat as a last
resort, not a design goal -- real Apple libdispatch (~28K LOC) is
already cloned at `third_party/libdispatch` (tag
`libdispatch-1271.40.12`). A background survey (prompted by "are there
other shims like this that could be resolved by building a library")
found exactly one more real candidate beyond this one and `os/
assumes.h`'s stub (see below): the kernel's from-scratch SHA-256 could
be wired to real, already-compiled-in corecrypto source instead --
filed as `docs/backlog.md` P2 item 0b, not yet acted on (separate,
kernel-side, security-relevant change; different risk profile than a
userland library port).

`build_libdispatch_real.sh` (new) is a real, reproducible
`clang -fsyntax-only` trial-compile harness -- see that script's own
HONEST SCOPE STATEMENT for full per-fix detail. Result after this pass:
**14 of 14 attempted real core dispatch files compile with zero errors**
(`once.c`, `semaphore.c`, `apply.c`, `time.c`, `allocator.c`, `init.c`,
`queue.c`, `object.c`, `source.c`, `voucher.c`, `io.c`, `mach.c`,
`eventlink.c`, `transform.c`). `workgroup.c` is deliberately excluded
(confirmed zero real `os_workgroup_*` call sites in PureDarwin's actual
source, same finding that justified `os/workgroup.h`'s own empty stub);
`block.cpp`/`src/firehose/`/`src/swift/` are out of scope (no ObjC++, no
os_log engine, no Swift runtime here).

Real, non-obvious findings this pass (each documented in more depth in
the relevant compat file's own header comment):
- `DISPATCH_USE_THREAD_LOCAL_STORAGE` must be left **completely
  undefined**, not defined-as-`0` -- `shims/tsd.h`'s real TSD accessors
  are gated on *definedness* (`#ifndef`) in one branch and *truthiness*
  (`#if`) in the other, so `=0` silently satisfies neither and
  undeclares `_dispatch_thread_port`/`_dispatch_thread_getspecific`/etc
  with no warning, just a much-later, confusingly-unrelated-looking
  implicit-declaration error.
- A real, recurring "needed regardless of this project's SPI flags" gap:
  `firehose_activity_id_t`, `VOUCHER_CURRENT`, `OS_FIREHOSE_SPI_VERSION`
  are all referenced unconditionally by `voucher_internal.h`/`queue.c`/
  `init.c` even though their real declaring headers are gated behind
  `OS_VOUCHER_ACTIVITY_SPI`/`OS_FIREHOSE_SPI` (both forced off, no logd
  exists on this target) -- closed via a forced `-include` prefix header
  rather than re-enabling either SPI flag.
- Two real upstream source bugs/gaps, fixed via this project's
  established patch mechanism (`third_party-patches/libdispatch.patch`,
  applied via `git am`, same as `bash.patch`): (1) a genuine
  used-before-declared typedef in `queue_internal.h` (illegal in C
  regardless of platform, not a portability issue); (2) `voucher.c`'s
  real ~410-line firehose ring-buffer "voucher_activity_t" section,
  confirmed zero real callers anywhere in this project's actual source,
  disabled via `#if 0` rather than porting the ~1600-line firehose engine
  it depends on.
- Real SDK-vintage shadow gaps for `mach_get_times()`/
  `thread_get_special_reply_port()`/`<mach/mach_vm.h>` -- this project's
  pinned iPhoneOS14.4 SDK ships its own older, same-named headers found
  first via the normal sysroot search, so `-idirafter` (which only wins
  when the SDK has *no* file of that name at all) can't override them;
  worked around with narrow forward declarations / a small dedicated
  compat header instead of fighting search-path precedence.

**Concrete next steps, in order**: (1) compile the 14 clean files to
real `.o`s and archive them (this script currently only proves
compilability via `-fsyntax-only`, not link-readiness); (2) a real trial
link against this project's existing libc/libpthread/libplatform/
libmach_ipc stack -- expect at minimum `_firehose_task_buffer`/
`_firehose_task_buffer_pred` (declared `extern` in `voucher_internal.h`,
now genuinely undefined since their only real definition lived inside
the `#if 0`-disabled block) to need a small real compat definition, not
a re-enable; (3) rewire `build_libxpc.sh`/`build_libnotify.sh`/etc from
`libdispatch_min.a` onto the new real archive once it links cleanly; (4)
only after that, decide whether `dispatch_min.a` can be deleted outright
or should stay as a documented fallback during the migration.

### Real `libdispatch` port continued, same day -- compiled+archived+
### linked; `build_libxpc.sh`/`build_libxpc_syscalls.sh`/`2.sh` swapped
### from `libdispatch_min.a` onto the real archive

All 3 concrete next steps above are DONE: `build_libdispatch_real.sh`
now compiles 19 real files (the original 14 plus `data.c` and the real
`event/` kqueue backend -- both turned out fully compatible with the
real object model, `firehose_task_buffer`'s extern didn't even need a
compat definition since nothing in the compiled set references it
outside the already-`#if 0`-disabled voucher.c block) plus `block.cpp`
(real C++11, Blocks-runtime only, no ObjC) and `shims/lock.c` (the real
semaphore/gate/unfair-lock slow paths), archives `libc_build/
libdispatch_real.a` (23 objects), and a real functionally-exercising
trial link narrows the gap to 53 real symbols -- see that script's own
updated HONEST SCOPE STATEMENT for the full, categorized breakdown
(overwhelmingly libpthread's own missing standard POSIX/QoS API, not
libdispatch's own code). Three more real upstream fixes landed in
`libdispatch.patch` along the way (a real parameter typo in
`event_kevent.c`, two missing global TSD keys in `init.c`, an explicit
cast `_dispatch_thread_port()` needed once C++ entered the build).

`libdispatch_min.a` references replaced with `libdispatch_real.a` in all
three: `build_libxpc.sh`, `build_libxpc_syscalls.sh`,
`build_libxpc_syscalls2.sh`. Real, verified result: all three still
compile with **zero errors** (the archive swap only affects linking, not
compiling libxpc's own sources), and the combined trial link
(`build_libxpc_syscalls2.sh`'s own, the fullest one: `libxpc.a` +
`liblaunchd_mig.a` + `libdispatch_real.a` + both syscalls tiers) now
shows **81 undefined symbols**, up from the pre-swap 5
(`os_log_create`/`os_log_type_enabled`/`_os_log_impl`/
`dispatch_data_create_map`/`__mach_errors`) -- the ~76 new ones are the
same real libpthread-centric gap `build_libdispatch_real.sh` already
documents (standard `pthread_attr_*`/QoS-override/workqueue API missing
from this project's curated libpthread, plus a handful of raw traps
this project hasn't generated stubs for yet: `mmap`/`munmap`/`kqueue`/
`kevent`/`kevent_qos`/`sigsuspend`/`clock_gettime_nsec_np`/
`kpersona_get`, plus real Mach voucher MIG routines needing a
`mach_voucher.defs`/`host_priv.defs` regen this project hasn't done).
`dispatch_min.a` itself is left in place (not deleted) as the documented
fallback per the original plan -- nothing currently references it except
its own build script.

**This does not block starting on launchd's actual daemon source**
(`core.c`/`ipc.c`/`launchd.c`/`log.c`/`runtime.c`) -- that work needs its
own real trial-compile pass regardless of this gap, and can surface
which of these 81 symbols (if any) launchd's own code actually
exercises vs. which stay purely theoretical (referenced by libxpc/
libdispatch internals launchd's own real startup path never reaches, the
same "real gap, but is it on a path that matters" question this project
has answered narrowly for every prior gap rather than closing
everything unconditionally).

### Real libdispatch dylib conversion + first launchd daemon-source
### reconnaissance, same day (2026-08-26)

`libdispatch_real.a` corrected to a real `libdispatch.dylib`
(`build_libdispatch_dylib.sh`) per this project's own standing
dylib-over-static directive -- confirmed real `MH_DYLIB`/`LC_ID_DYLIB`
via `otool`, 89 legitimately-deferred `-Wl,-U` imports (a real dylib is
allowed undefined symbols; a static executable is not). Getting there
surfaced real duplicate-symbol conflicts between archives built at
different points in this project's history and a confirmed gap in this
project's curated libpthread (standard `pthread_attr_*`/QoS/workqueue
API missing entirely) -- filed as `docs/backlog.md`'s new P2 "d1"-"d6"
library-debt items rather than silently patched over.

First real reconnaissance pass over launchd's own daemon source
(`build_launchd_recon.sh`, new, `-fsyntax-only` only -- not a real build
script yet): **7 of 8 small files were already clean with zero new
fixes** (`pd_launchd_boot.c`, `kill2.c`, `pd_sandbox_check.c`,
`pd_console_login.c`, `pd_launchd_plist.c`, `log.c`, `ipc.c`).
`launchd.c` (the real PID-1 entry point) needed 3 small, already-
established-pattern fixes and is now **fully clean**. `core.c`
(12,162 real lines) needed those same 3 fixes plus 2 more (a real,
separately-pinned `third_party/Libinfo` repo turned out to have a header
PureDarwin's own bundled copy is missing) and is down to **7 real,
distinct, uninvestigated errors** -- including a genuinely interesting
one: `ld2xpc`/`xpc2ld` are called but confirmed (via `git ls-tree`
across PureDarwin's entire `next` branch, not just this project's sparse
checkout) to never be defined anywhere in that real upstream repo at
this commit -- real incompleteness in PureDarwin's own branch, not a
gap on this project's side. See `docs/backlog.md`'s P2 "d6" item for
the full, current list and `build_launchd_recon.sh`'s own header
comment for the complete real-fix writeup.

## Phase 6.8 -- original 2026-08-22 scoping (partially superseded above,
## kept for the parts still accurate: license/vintage caution, the
## "expect real work left in the XPC skeleton" expectation, and the
## overall "replaces tools/init_binary/'s PID-1 role" migration framing)

**Real, newly-surfaced prerequisite gap, found 2026-08-25 while re-reading
`build_libnotify.sh`'s own already-committed research (not new
investigation -- that script's header comment already documented this,
it just hadn't been promoted into this phase's own scoping yet)**: this
project has **zero Mach bootstrap-namespace support at all** -- no
`bootstrap_look_up2()`/`bootstrap_port`, no `servers/bootstrap.h`, no
`bootstrap.defs` anywhere in `third_party/xnu-7195`
(`osfmk/mach/bootstrap.h` is Apple's own literal empty placeholder,
"obsolete: header included only for compatibility"). This is not a
peripheral gap -- the bootstrap namespace IS what `launchd` implements
(every `xpc_connection_t`/Mach-service lookup a client does ultimately
resolves through it), so a real `launchd` port needs this working
underneath it, not the other way around. Confirmed concretely via
`third_party/Libnotify/notify_client.c:894`'s
`bootstrap_look_up2(bootstrap_port, NOTIFY_SERVICE_NAME, ...)`, called
unconditionally by every one of Libnotify's ~9 real public entry points
-- this project's own already-ported Libnotify MIG transport is real and
correct (`build_libnotify.sh`) but cannot reach a live `notifyd` for
exactly this reason, independent of that script's separate, larger XPC
wall. Real follow-up work before/alongside Phase 6.8 itself: (1) confirm
whether xnu-7195's kernel-side `TASK_BOOTSTRAP_PORT` special-port
plumbing is real/stock (likely yes -- unmodified XNU always initializes a
task's special ports at creation; what's missing is a userland process
that actually registers itself as the holder and answers lookups), (2)
real `bootstrap.defs` MIG generation + client stub, same
regenerate-every-build pattern `build_libmach_ipc.sh`/
`build_iokituser.sh`/`build_libnotify.sh` already established, (3) decide
whether `tools/init_binary/` (current PID-1) needs to become a minimal
real bootstrap server before a full `launchd` port lands, or whether this
folds entirely into the `launchd` port itself.

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
- **License, resolved 2026-08-25 (real repo/file check, not assumed)**:
  no repo-level LICENSE file (`license: null` via the GitHub API), but
  both halves carry real, permissive per-file headers, checked directly
  -- `src/launchd/launchd.c`/`core.c` (and, per grep pattern, the rest of
  Apple's own `launchd` files) carry `@APPLE_APACHE_LICENSE_HEADER_START@`
  / **Apache License 2.0**, correcting this section's earlier "carries
  Apple's APSL" assumption; iXsystems's `libxpc` files (e.g.
  `src/libxpc/xpc_connection.c`) carry an explicit
  **2-clause-BSD-style header**, `Copyright 2014-2015 iXsystems, Inc.`,
  "Redistribution and use in source and binary forms... AS IS". Net: not
  blocked -- both real licenses are permissive -- but cite the per-file
  headers specifically rather than "no license stated" going forward.
- **Repository archived 2026-07-18** (`pushed_at: 2024-10-06` per the
  GitHub API -- read-only, no further upstream fixes will ever land) --
  whatever's there today is permanently the final state; any bugs found
  while porting it are this project's own to fix, not something to wait
  on upstream for. Checked for a fresher fork (2026-08-25): all 7 forks
  found (barracuda156, TheArchitectEngineer, killvxk/XPC-PureDarwin,
  dariaphoebe, redvelvetcake11, ivanagui2,
  dothanthitiendiettiende/launchd-and-libxpc) are stale, oldest activity
  2018, none post-date the origin's own last push -- this really is the
  most current version available anywhere.
- **Real file/dependency survey, 2026-08-25** (real repo structure +
  actual `#include` lists fetched directly, not guessed): top level is
  `headers/` (a large vendored `CoreFoundation.framework`/
  `IOKit.framework`/`Kernel.framework` header tree -- build-time only,
  confirmed NOT a real runtime dependency, see below), `libxpc.xcodeproj/`
  (the ONLY build system present -- a real 2013/2014-vintage Xcode
  project, no Makefile; needs the same "hand-extract the real file list
  from `project.pbxproj`, reimplement as a shell script" treatment as
  every other tier here, e.g. `build_libc_regex.sh`'s template), `src/`
  (`launchd/`: `core.c`, `ipc.c`, `job*.defs`/`.c` MIG files, `launchd.c`,
  `log.c`, `runtime.c`, `launchproxy.c`, `kill2.c`, `ktrace.c`,
  `wait4path.c`, plus `SystemStarter/`/`init/`/`rc/` subdirs; `libxpc/`:
  `xpc_array.c`, `xpc_connection.c`, `xpc_dictionary.c`, `xpc_debug.c`,
  `xpc_error.c`, `xpc_misc.c`, `xpc_private.c`, `xpc_type.c`, `classes.m`,
  `libbootstrap.c`, `liblaunch.c`, `libvproc.c`; plus `liblaunch/`,
  `libnv/`, `man/`, `plists/`), `tests/` (only 2 files, thin). Real,
  **CORRECTING** finding against this section's own dependency framing:
  **no CoreFoundation include was found in any launchd/libxpc source file
  actually fetched** (`launchd.c`, `core.c`, `xpc_connection.c`,
  `xpc_internal.h` all checked directly) -- the vendored CF header tree
  under `headers/` appears to just be carried along for other PureDarwin
  consumers, not a real dependency of this specific source. Real
  dependencies actually found: `bsm/auditd_lib.h`/`bsm/libbsm.h` (BSM
  audit, not yet ported here), `bootstrap.h`/`servers/bootstrap.h`
  (confirms Phase 6.8 prereq above), `vproc.h`/`vproc_priv.h`,
  `launch.h`/`launch_internal.h`; `xpc_internal.h` has no
  `dispatch.h` include but its structs reference `dispatch_source_t`/
  `dispatch_queue_t` directly -- confirms a real, if partially deferrable,
  libdispatch dependency, consistent with this doc's existing
  Phase-6.6-before-6.8 sequencing. One real NEW gap this survey surfaced,
  not previously in this project's inventory anywhere: `core.c` also
  includes `sandbox.h`, `quarantine.h`, `responsibility.h`, and
  `systemstats/systemstats.h` -- private Apple frameworks with no current
  port in this project and not mentioned in PureDarwin/XPC's own README
  dependency list (which only names libSystem/libcoreservices/libedit/
  libbsm) -- scope these (real vs. stubbable no-op, matching this
  project's usual per-header judgment call) when Phase 6.8 actually
  starts, don't assume from the README alone.

Net effect on the plan: still recommended as the starting point over
writing a from-scratch XPC shim (per source-priority -- a real, if
incomplete and unmaintained, implementation beats inventing one), and it
collapses the old two-phase split (launchd, then separately XPC) into
one combined port, since the two are already integrated in this source.
But go in with accurate expectations, not the assumption that "has XPC"
means a complete or vintage-matched one:

1. **License check: DONE 2026-08-25**, see above -- real per-file Apache
   2.0 (`launchd`)/BSD-2-clause (`libxpc`) headers, not blocking.
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

## Phase 9 (CoreFoundation) -- DONE 2026-08-26

Real repo used: [swift-corelibs-foundation](
https://github.com/apple/swift-corelibs-foundation), not
apple-oss-distributions/CF -- that repo was confirmed dead/stale (last
real CF commit predates this project's macos-112 manifest era), so this
followed the project's own "Apple first, real equivalent upstream
second" rule. 25+ real Apple CF source files compiled and archived
(`Base`/`Collections`/`NumberDate`/`String`/`Parsing`/`Error`/`URL`
subprojects), one small upstream patch (`CFPriv.h` bool/Boolean prototype
conflict, applied via `third_party-patches/swift-corelibs-foundation.patch`),
wired into launchd's real dynamic trial link. GNUstep fallback was never
needed. ICU confirmed NOT required for launchd's actual CF usage (checked
`pd_launchd_plist.c` line by line -- it never touches CFLocale/
CFCharacterSet/collation/CFNumberFormatter). ~150 honest remaining gaps
(ICU's CFUniChar/CFCharacterSet/collation/CFNumberFormatter family, ASL
logging -- confirmed never open-sourced by Apple, `libSystem.B.dylib`
export-parity debt) are real, categorized scope decisions, not unstarted
work -- full detail in `docs/backlog.md` item d7.

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
