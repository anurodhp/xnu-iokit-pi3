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

**Next candidates for step 3** (in progress): `libmalloc`/`libpthread`
as real dylibs -- would also let `libsystem_blocks.dylib`'s temporary
`-Wl,-U,_malloc`/`-Wl,-U,_free` above become a real dependency. The BSD
syscall surface (would retire the remaining scattered per-consumer
syscall-stub files -- `iokit_trap.s`, `libunwind_sched_yield.s`,
`notify_mig_special_reply_port_trap.s`, `mach_port_extra_traps.s`,
`mach_ipc_traps.s` -- confirmed via `grep -rl "svc #0x80"` across
`tools/userland_staging/*.s`, not yet audited individually for which
specific consumers still need each one) is a separate, later candidate,
lower priority since no active bug points at it yet the way
`mach_task_self_`/`errno` did. Whoever continues this should re-run that
same `grep -rl "svc #0x80" *.s` scan first -- it's the fastest way to
re-derive the current scattered-trap-file inventory rather than trusting
this list to stay current, and should re-audit each new library for the
same duplicate-global-state risk before assuming "just relink it as a
dylib" is safe (two real instances of that exact bug found so far, both
by deliberately checking for it first).

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
