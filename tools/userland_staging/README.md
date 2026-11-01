# userland_staging

Staging area for Phase 4 (docs/roadmap.md: real dyld/Libc userland, real
`bash`/coreutils) build experiments. Binaries produced here are meant to
eventually go on the SD image's HFS+ root (see
`tools/init_binary/inject_into_sd_image.sh` for the injection mechanism).
Most tiers below (locale, stdio, gen, printf, malloc, pthread, closure)
are real-QEMU-boot-verified -- originally via the now-retired
`tools/init_binary/init.c`'s `exec` shell builtin, now via real
`launchd.macho` and its dependency graph -- see docs/roadmap.md's
Phase 4 section for exactly which and the boot logs.

## crt0_smoketest

`build_crt0_smoketest.sh` links a trivial raw-syscall `main()` against
Apple's real, unmodified `third_party/Csu` (`start.s`/`crt.c`, built for
the `-static`/no-`__DYNAMIC__` path -- the same real source Apple ships for
statically-linked programs, not a hand-rolled substitute). Confirms Csu's
`_start -> main() -> exit()` handoff works unmodified on this project's
toolchain/target and produces a structurally-correct freestanding Mach-O
(`LC_UNIXTHREAD`, zero `LC_LOAD_DYLIB`) -- verified via `otool -l` and (on
the host Mac only, not the target kernel) via `nm`/linking cleanly with
zero undefined symbols beyond `_main`/`_exit`.

This deliberately does NOT use any Libc -- it's isolating "does crt0 work"
from "does a full libSystem build work" (the much bigger remaining Phase 4
item). Once real Libc is buildable, this should be replaced by something
that actually calls into it (e.g. a real `printf`) rather than raw
syscalls.

## libc_string / libc_locale / libc_stdio / libc_gen

Four curated real-Apple/FreeBSD Libc tiers, each building on the last
(`build_libc_string.sh` -> `build_libc_locale.sh` -> `build_libc_stdio.sh`
-> `build_libc_gen.sh`), all following the same pattern: compile real
`third_party/Libc` (and, for a handful of genuinely-missing arm64
primitives, real FreeBSD) source directly with `clang`, no `xcodebuild`
(see each script's own header comment for why every compat shim/flag it
adds is real and verified, not guessed). Read `docs/roadmap.md`'s Phase 4
section for the full narrative and exact boot logs; short version:

- `libc_string.a` -- `memcpy`/`memset`/`strlen`/etc, host-link-verified only.
- `libc_locale.a` -- ~113 locale/wide-char/ctype files, real TSD-backed,
  QEMU-boot-verified (`libc_locale_smoketest.macho`).
- `libc_stdio.a` -- real `fopen`/`fclose`/`fread`/`fwrite`/`fgets`/etc
  (the FILE*-infrastructure tier, NOT the `printf`/`scanf` family, see
  `build_libc_stdio.sh`'s own header comment for why that's a separate,
  larger, still-deferred subsystem), QEMU-boot-verified
  (`libc_stdio_smoketest.macho`).
- `libc_gen.a` -- real `opendir`/`readdir`/`closedir`/`getcwd`/`err`/
  `basename`/`dirname`/`uname`/etc, QEMU-boot-verified
  (`libc_gen_smoketest.macho`).

`libc_stdio.a`/`libc_gen.a` supersede the locale tier's own small hand-
rolled `fdopen`/`fread`/`fileno`/`rewind`/`fclose` FILE* shim
(`libc_locale_stdio.c`, written before real stdio existed) -- any build
linking both tiers together should link `libc_locale_for_stdio.a`
(built by `build_libc_stdio.sh`: a copy of `libc_locale.a` with that one
now-redundant object dropped), not `libc_locale.a` directly, or real and
shim `fclose`/etc will collide as duplicate symbols.

## libc_printf

`build_libc_printf.sh` -> `libc_build/libc_printf.a`: the real `printf`/
`scanf` ("xprintf") family `build_libc_stdio.sh` deliberately left out --
`vfprintf`/`vfscanf`/`vfwprintf`/`vfwscanf`, the real xprintf engine and
all built-in conversion-domain handlers, every public `printf*`/`scanf*`/
`wprintf*`/`wscanf*` wrapper, plus real David Gay `gdtoa` for float<->string
conversion (arm64 file selection read from Apple's own
`xcodescripts/libc.xcconfig`, not guessed). QEMU-boot-verified
(`libc_printf_smoketest.macho`: real `snprintf`/`sscanf` round-trip
covering `%d`/`%s`/`%f`/`%x`). See `docs/roadmap.md`'s Phase 4 "Ninth
pass" for the full real-bug list (a genuine SIGSEGV-in-`snprintf` root-
caused to a missing `pthread_tsd_shim_bootstrap_for_libc()` call, a
fail-closed real/not-fake `%n`-format-string-CVE-mitigation stand-in, and
several real dead-code-but-still-linked symbol satisfactions).

Links against `libc_stdio.a` + `libc_locale_for_stdio.a` + `libc_gen.a`
(for real `errx()`) + `libplatform.a` (for real `OSAtomic*`/
`os_unfair_lock*`, now a hard dependency of the stdio/locale tiers as of
the sibling `build_libplatform.sh` tier -- see `docs/roadmap.md`'s
"Eighth pass"). Any smoketest linking this tier MUST call
`pthread_tsd_shim_bootstrap_for_libc()` as the first thing `main()` does,
same as the locale tier's own smoketest -- every real printf/scanf
function goes through `__current_locale()`, which needs it.

## libmalloc

`build_libmalloc.sh` -> `libc_build/libmalloc.a`: a real Apple
magazine-zone heap allocator (`third_party/libmalloc`, tag
`libmalloc-317.40.8`) -- unmodified `magazine_tiny/small/medium/large/
malloc/rack.c`, `vm.c`, `bitarray.c`, `malloc_printf.c`; real
`create_scalable_zone()` does every actual allocation decision.
`libmalloc_dispatch.c` (project-owned, ~180 lines) is the real minimal
replacement for JUST `malloc.c`'s public C-API glue (`malloc`/`free`/
`calloc`/`realloc`/`valloc`/`reallocf`) -- `malloc.c` itself is out of
scope (needs `dlopen` for Malloc Stack Logging, which doesn't exist on
this dyld-less target), same for `nano_malloc.c`/`nanov2_malloc.c` (a
separate, larger future item). QEMU-boot-verified
(`libmalloc_smoketest.macho`: malloc/write/verify, calloc zero-init,
realloc-grow preserving contents, a 200-round alloc/free stress loop
across size classes).

See `docs/roadmap.md`'s Phase 4 "Tenth pass" for the full real-bug list:
a genuine `_FORTIFY_SOURCE` self-recursion SIGSEGV (`__memset_chk`
calling itself), and a second, independent missing-`pthread_tsd_shim_
bootstrap()`-call SIGSEGV inside real `os_unfair_lock`'s TSD-based owner
lookup -- both found via QEMU boot-test bisection with temporary debug
prints in scratch copies of vendor source, not fabricated as passing then
walked back. Also documents the real `mig`-tool usage (`mach_vm.h`/
`mach_vm_internal.h` regeneration, and why `/usr/bin/mig` specifically,
not `xcrun`'s copy) and the `$UNIX2003`/`_platform_memset` real-symbol
gaps this tier closes via `ld -alias` and a thin real wrapper,
respectively.

**Retired `compat_stubs.c`'s placeholder bump allocator** now that this
tier exists -- `build_libc_locale.sh`/`build_libc_stdio.sh`/
`build_libc_gen.sh`/`build_libc_printf.sh` all relink their smoketests
against real `libmalloc.a` instead (each re-verified). Pipeline order is
now `string -> platform -> locale -> stdio -> gen -> printf -> malloc`;
`build_libc_locale.sh`'s own smoketest step degrades to a documented skip
(not a hard failure) when `libmalloc.a` isn't built yet, since
`libmalloc.a` itself depends on this tier's `libc_stdio.a`/
`libc_locale_for_stdio.a` output (real `isatty()` etc) -- re-run
`build_libc_locale.sh` once more after `build_libmalloc.sh` to relink it
for real.

## libpthread (`build_libpthread.sh`)

Real, unmodified `third_party/libpthread` (pinned `libpthread-454.80.2`):
`pthread_tsd.c`/`pthread_mutex.c`/`pthread_cond.c`/`pthread_rwlock.c`/
`pthread_atfork.c`/`pthread_cwd.c`, 6/6 compiled, 0 skipped. Real,
load-bearing constraint: this project's kernel-side pthread.kext
stand-in (`bsd_kern/bsd_pthread_stub.c`) unconditionally returns
`ENOTSUP` for `bsdthread_create`, so real `pthread_create()` can never
spawn a second thread here -- everything single-thread-correct is real
(mutex/cond/rwlock under their real default FIRSTFIT/non-ulock policy,
real TSD keys, real `pthread_once`, real `pthread_atfork`); `pthread.c`
itself isn't compiled (its main-thread-bootstrap/signature-scheme and
trivial-one-liner pieces are faithfully reimplemented in the new
`pthread_main_thread_bootstrap.c`, its real `pthread_create` stack/
mach-vm machinery is dead code here regardless and also hits a real MIG
header conflict); `pthread_create()` itself is real code performing the
real `bsdthread_create(2)` syscall, returning the real kernel `ENOTSUP`
-- honest documented failure, not fabricated. QEMU-boot-verified
(`pthread_smoketest.macho`): bootstrap, `pthread_self`/`pthread_equal`/
`pthread_main_np` consistency, real mutex lock/unlock, real TSD key
create/set/get, real `pthread_once` (runs exactly once), real
`pthread_create` failure. Found+fixed one real bug via QEMU boot-test
bisection (native `ld`/host testing didn't catch it): real libpthread's
`errno` macro dereferences TSD slot 1, never populated by this tier's
bootstrap -- caused a genuine SIGSEGV in `__bsdthread_create`'s own
error path; fixed by pointing TSD slot 1 at `_main_thread`'s real
`err_no` field. Full detail (including an adversarial-review-caught
documentation inaccuracy, fixed pre-commit) in `docs/roadmap.md`'s
Phase 4 "Eleventh pass". Not done: relinking the locale/stdio/gen/printf
tiers' smoketests against this real tier instead of `tsd_bootstrap.c`/
`pthread_tsd_shim*.c` (documented follow-up).

## libclosure (`build_libclosure.sh`)

Real, unmodified `third_party/libclosure` (pinned `libclosure-78`): the
Blocks runtime. `runtime.cpp` (real `Block_copy`/`Block_release`/
`Block_object_assign`/`Block_object_dispose`, refcounting via a real
`__sync_bool_compare_and_swap` builtin -- disassembly-confirmed to lower
to real arm64 `ldaxr`/`stlxr`, no libcall) + `data.c` (the plain
`!HAVE_OBJC`-gated `_NSConcreteStackBlock`/etc symbol table -- the
correct real branch, this project has no ObjC runtime). 3/3 files
compiled, 0 skipped. Also pulls in
`third_party/libplatform/src/string/generic/memmove.c` for real
`_platform_memmove` (compiled WITHOUT `-DVARIANT_STATIC`, confirmed by
testing that flag really does collide with `libc_string.a`'s own
`memcpy`/`memmove`). `os_assumes`/`os_assert` predefined as command-line
macros rather than adding a second `os/assumes.h` compat path.
QEMU-boot-verified (`libclosure_smoketest.macho`): a real `-fblocks`
stack block promoted via `Block_copy`, called, verified, released; a
second block exercises the real `__block`-byref copy/dispose helper
path. Full detail in `docs/roadmap.md`'s Phase 4 "Twelfth pass".

Still not attempted after this tier: `libdispatch` (investigated in
Phase 4's "Thirteenth pass" -- a real monolith, not separable file-by-
file the way every prior tier was; deferred as its own future effort,
see `docs/roadmap.md`), `libunwind`, `Libinfo`(-the-Apple-repo), and
`dyld` itself.

## removefile (`build_removefile.sh`)

Real, unmodified `third_party/removefile` (pinned `removefile-49.40.3`):
all 5 `.c` files, 0 skipped. Real `removefile()` always opens a real
FTS(3) stream even for a single path, so this tier's real new
dependency is `third_party/Libc/gen/fts.c` (the modern, `getattrlistbulk`
(2)-based FTS -- syscall #461, freshly generated via the real Apple Perl
generator, not NO_SYSCALL_STUB) plus real Blocks (`libclosure.a`). Also
wired: real `mkdir`/`geteuid`/`chflags`/`fchdir`/`fsync`/`fstatat64`
(auto-generated), hand-written `unlink`/`rmdir`/`unlinkat`/`rename`
(NO_SYSCALL_STUB), real `qsort`/`qsort_b`/`heapsort`/`heapsort_r`/
`rand`/`random`. Two real last-resort ports (no `.c` source anywhere in
this Libc checkout): `flsl` (via `__builtin_clzl`) and `getiopolicy_np`
(built on the real `iopolicysys(2)` syscall using this project's own
kernel headers for the real struct layout -- its only call site is
gated behind `SF_DATALESS`, never set by this target's HFS+, so real,
linked, but provably unreachable).

## copyfile (`build_copyfile.sh`)

Real, unmodified `third_party/copyfile` (pinned `copyfile-173.40.2`):
`copyfile.c` (5026 lines, one translation unit -- not separable into
files the way removefile was) + `xattr_flags.c`, both compiled whole.
Built with this project's `-target arm64-apple-ios14.4` toolchain, so
`TARGET_OS_IPHONE` is real and true -- copyfile.c's own real `#else
TARGET_OS_IPHONE` branch already provides a complete no-op quarantine
fallback (no `libquarantine.dylib` needed). Real xattr/ACL/clonefile
syscalls all wired (will genuinely return `ENOTSUP`/`EINVAL` at runtime
since real HFS+ here has no ACL/xattr storage -- still had to link,
since C doesn't dead-code-eliminate a runtime `flags &` check). Real
`posix1e` ACL source (all 15 files), `filesec.c`, `chmodx_np.c`/
`statx_np.c`, real FreeBSD `realpath.c`, real `uuid/uuidsrc/{copy,parse,
unparse,pack}.c`. One small real `dispatch_once`/`dispatch_once_t`
compat shim (real public ABI, not internal GCD guts -- doesn't reopen
the libdispatch-is-a-monolith finding). Honest last-resort stand-ins
(never fabricated success) for `getpwnam`/`getpwuid`/`getgrnam`/
`getgrgid`/`mbr_uid_to_uuid`/`mbr_gid_to_uuid`/`mbr_uuid_to_id`/
`_xpc_runtime_is_app_sandboxed` -- all on ACL-text-parsing paths this
tier's own `COPYFILE_DATA` smoketest never reaches.

Both QEMU-boot-verified for real (directory tree built, `removefile`d,
confirmed gone via `stat`; file `copyfile`d, content verified
byte-for-byte). Found+fixed a real, project-wide bug along the way: the
hand-written raw-syscall helpers across several tiers (`libc_gen_
syscalls.c`, `libc_locale_syscalls.c`, `libc_stdio_syscalls.c`,
`libc_locale_stdio.c`, plus this pass's two new files) returned the raw
negative kernel error code on failure instead of setting the real global
`errno` via `cerror_nocancel()` (the real Apple `errno.c` stand-in,
already linked into every binary here) -- every affected wrapper still
correctly signaled failure to a `< 0`/`!= 0` check, so this had never
visibly broken anything, but any real caller checking `errno` after a
failed call got a stale/zero value. Fixed everywhere it was found; the
libpthread-tier syscall files were checked and confirmed to already use
a different, correct convention (manual `errno = (int)-r` per call
site), left alone. Full detail in `docs/roadmap.md`'s Phase 4
"Fourteenth pass".

Real, unmodified `third_party/libunwind` (pinned `libunwind-200.10`),
6/6 files: real DWARF-CFI + Apple compact-unwind `unw_getcontext`/
`unw_init_local`/`unw_step` engine (this project's first real C++
compile, `-fno-exceptions -fno-rtti`, real placement-new declarations
from the Xcode-12 toolchain's own `<new>`, no libc++.dylib dependency).
Real `libunwind_dyld_shim.c` implements `_dyld_find_unwind_sections()`
(last resort -- real libunwind has no fallback once dyld is assumed) by
walking `__mh_execute_header`'s real Mach-O load commands directly.
Found+fixed a real TSD-bootstrap SIGSEGV (same bug class as the printf
tier's) and a real integration gap: libunwind's `RWMutex` is this
project's first genuine caller of `pthread_rwlock_wrlock`'s contended
path, needing real `libpthread.a` linked into the printf/removefile/
copyfile tiers for the first time (retiring `libc_printf_mach_stub.c`'s
old rdlock/unlock no-op stand-ins) plus a real `sched_yield()`/
`swtch_pri` Mach trap (`pthread.c`'s one real one-liner the libpthread
tier didn't compile).

**Real, project-wide structural finding**: ld64 on this project's
pinned Xcode-12 toolchain does not synthesize `__TEXT,__unwind_info`
(or preserve `__eh_frame`/`__compact_unwind`) for any `-nostdlib
-static` link -- confirmed by direct testing, not guessed. Since every
Mach-O this project produces is exactly this kind of static binary,
real DWARF/compact-unwind frame metadata is categorically unavailable
here, on any tier, now or later. The libunwind smoketest was scoped
accordingly: it proves the real engine runs and fails *safely* (a
defined "no unwind info" result, not a crash) rather than claiming a
real backtrace it structurally cannot produce on this target. Full
detail in `docs/roadmap.md`'s Phase 4 "Fifteenth pass".

**`dyld` investigated and scoped OUT for real** (not skipped
preemptively): every Mach-O this project produces is `-nostdlib
-static`, zero `LC_LOAD_DYLIB` -- dyld's whole job has no consumer here.
26 real `third_party/dyld` files touch `csops`/AMFI/shared-cache
directly, its real launch path needs the kernel's separate
`LC_LOAD_DYLINKER`/`load_dylinker()` path (never exercised here), and
even a working dyld would have nothing to load (no library in this
project has ever been built as a real `.dylib`). Full detail in
`docs/roadmap.md`'s Phase 4 "Sixteenth pass".

**Real static `bash` survey started** (`build_bash_survey.sh`, does NOT
yet produce a working `bash.macho`): real, unmodified Apple bash 3.2
(`third_party/bash/bash-3.2`, pinned `bash-123.40.1`), Apple's own real
pre-generated `config.h` copied in, real `y.tab.c` regenerated from
bash's own `parse.y` via host `bison`. **21/33 real core interpreter
files compile clean** against the curated libc stack. 12 real, fully
enumerated remaining gaps (not vague TODOs): `lib/tilde`/`lib/readline`/
`lib/glob` need `-I` paths only (source already present); `get_compat.h`
is a real one-line Autoconf-generated file never generated here;
`CONF_MACHTYPE`/`CONF_HOSTTYPE`/`CONF_OSTYPE`/`CONF_VENDOR`/`PACKAGE`
are real trivial string-literal `-D` flags from `Makefile.in`'s
`SYSTEM_FLAGS`, not yet threaded through. `builtins/` (bash's ~80
built-ins, each real, `mkbuiltins`-generated) not attempted yet. Full
detail in `docs/roadmap.md`'s Phase 4 "Sixteenth pass".

Still not attempted: `Libinfo`(-the-Apple-repo), and the
still-open item of relinking locale/stdio/gen/printf/malloc against real
`libpthread.a`'s mutex/TSD primitives.

**`bash.macho` SIGSEGV: real stack-size bug found+FIXED (kernel-side,
QEMU-verified); a second, separate real bug found+precisely-localized
but not yet fixed.** Full detail in `docs/roadmap.md`'s Phase 4
"Eighteenth pass" -- summary:
- This board's kernel build (`CONFIG_EMBEDDED`, required -- BCM2837 is
  embedded-only upstream) was silently using real iOS-app stack limits
  (1MB) instead of real macOS desktop limits (8MB/64MB) --
  `third_party/xnu-7195/bsd/arm/vmparam.h` now overrides `DFLSSIZ`/
  `MAXSSIZ` to the desktop values `#if defined(BCM2837)`, verified via a
  new diagnostic tool, `stack_probe.c`/`build_stack_probe.sh` (real
  stack-resident recursion + real `write(2)` depth reporting -- crashed
  under 500 frames before the fix, reaches ~2000 (~8MB) after).
- Real bash STILL crashes after that fix -- a separate real bug, now
  precisely localized (not vaguely "somewhere in bash"): real
  `Libc/gen/get_compat.c`'s `compat_mode()`, called from `shell.c`'s
  `main()` right after `setjmp(top_level)`, has its `mode` argument
  (correctly passed by the caller, confirmed via disassembly) corrupted
  on its own stack between being saved and being read back -- the ONLY
  thing that runs in between is a real `pthread_once()` call, strongly
  implicating that function's real stack-frame handling in this
  specific, deep call context. Not yet fixed -- see the roadmap entry
  for the exact next diagnostic step.

**Nineteenth pass: TWO real archive/link bugs found+FIXED (that
`compat_mode()` corruption above turned out to be a symptom of, not
`pthread_once()`'s own logic) -- but real bash STILL crashes, on a
THIRD, different, real, precisely-captured bug.** Full detail in
`docs/roadmap.md`'s Phase 4 "Nineteenth pass" -- summary:
- Bug A (fixed): `libc_locale.a` still bundled the pre-`libpthread`
  `pthread_tsd_shim.o` stand-in, which silently shadowed real
  `libpthread.a`'s `pthread_once`/etc due to how `ld64` resolves static
  archives (real Libc source *inside* `libc_locale.a` itself demands
  `_pthread_getspecific` while that archive is being scanned, satisfying
  it locally before `libpthread.a` is ever consulted, order-independent).
  Fixed with `__attribute__((weak))` on `pthread_tsd_shim.c`'s 4 public
  POSIX function definitions -- real fix, not archive surgery, which was
  tried first and broke every OTHER standalone smoketest that still
  needs that file's other, genuinely load-bearing exports.
- Bug B (fixed): even with Bug A fixed, `libpthread.a`'s real
  `pthread_once` still never entered bash's link, because nothing in
  real bash's own source calls `pthread_main_thread_bootstrap()` or
  references anything else unique to that object file. Fixed via
  `bash_main_wrapper.c` (new) + `shell.c` compiled with
  `-Dmain=bash_shell_real_main` -- a real wrapper `main()` that
  bootstraps for real, then calls into real bash's own renamed `main()`.
  Both verified via disassembly: real bash's `_pthread_once` now
  disassembles to `_os_once`'s real cmpxchg sequence, not the naive
  shim's pattern.
- Bug C (NOT fixed, precisely captured): real bash still crashes --
  `PC=0x100002138 LR=0x0 FAR=0x100002138 ESR=0x82000007`, an instruction-
  fetch translation fault on the VERY FIRST instruction (LR=0, no `bl`
  ever executed), PC/FAR exactly matching bash.macho's own real
  `LC_UNIXTHREAD` entry PC (`otool -l`). The kernel's exec loader never
  maps the entry page at all. Segment structure checked against a known-
  working smoketest via `otool -l` and is near-identical -- the one real
  difference is size: bash.macho (~1.2MB) is by far the largest static
  Mach-O this project has ever exec'd. Real, testable next hypothesis
  for whoever continues: check `bsd/kern/mach_loader.c`'s real
  `__TEXT`-segment-mapping path for a size/page-count limit or
  integer-truncation bug never exercised at this scale before.
- New diagnostic tool: `pthread_once_deep_repro.c`/
  `build_pthread_once_deep_repro.sh` -- proved, via real QEMU boot-test
  (1200 real recursion frames, real `pthread_once` call, linked directly
  against `libpthread.a` alone), that real `pthread_once()`/`_os_once()`
  does NOT corrupt a caller's stack -- isolating the Eighteenth pass's
  `compat_mode()` symptom to Bug A/B (which implementation got linked),
  not to real `pthread_once`'s own logic.
