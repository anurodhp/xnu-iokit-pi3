# Static-linking cleanup: plan + handoff state (opened 2026-08-29)

## Why this exists

User directive, 2026-08-29:

> I would like to resolve the `libpthread.a` issue. An earlier iteration
> of this project used static linking and now we have been burned by
> duplicate symbols many times. I would like to cleanly remove all use of
> static linking where it is not appropriate — in libraries in one phase,
> and then all compiled binaries (e.g. included tools) in another phase.
> After several sweeps there still seem to be examples of these that keep
> cropping up.

The "keep cropping up" part is the important half. This bug class has hit
the project at least five separate times, each found the slow way (a real
QEMU boot, a real crash, a multi-session root-cause hunt):

- `mach_task_self_` separately-uninitialized in `libdyld.dylib` vs
  `libSystem.B.dylib` (Phase 6.5 Phase D)
- `g_zone` — two live heap zones in one process
- a duplicate static copy of `mig_reply_port.o`
- a duplicate static copy of `mach_msg_real.o`
- a duplicate static copy of `pthread_create` (launchd bug #6, session 5)

Every one of those was visible from `nm` in seconds. None was found that
way, because nothing ever ran `nm` across the whole shipped set at once.
So step one of this effort was not a fix — it was building the check.

## Current status at handoff

**Nothing is committed. Nothing has been QEMU-verified.** All work is
uncommitted edits in the working tree. See "What must happen next" below —
the very next step is a real boot test, before anything else.

### Files changed (uncommitted)

```
 M tools/userland_staging/build_libcxx_dylib.sh
 M tools/userland_staging/build_libdispatch_dylib.sh
 M tools/userland_staging/build_libsystem_c.sh
 M tools/userland_staging/build_libsystem_kernel.sh
 M tools/userland_staging/build_libsystem_pthread.sh
 M tools/userland_staging/build_libxpc_dylib.sh
?? tools/userland_staging/dup_symbol_audit.sh
?? tools/userland_staging/dup_symbol_baseline.txt
```

`docs/daemon-roadmap.md` is also modified in the working tree — that is a
**pre-existing edit from an earlier session (Phase 6.8 / real-PID-1
notes), not part of this work.** Do not fold it into a static-linking
commit.

### Measured result so far

Duplicate symbols across the 26 shipped dylibs, via
`tools/userland_staging/dup_symbol_audit.sh --summary`:

| stage | total dylib duplicate symbols |
|---|---|
| baseline (start of session) | **924** |
| after libdispatch de-static | 641 |
| after `libmach_ipc.a` → `libsystem_kernel.dylib` | 647 * |
| after libsystem_c consolidation | 611 |
| after libxpc de-static | 611 |
| after libc++ de-static | **262** |

\* the count rose briefly because `libsystem_kernel.dylib` took ownership
of symbols that `libxpc`/`libc++` still had private copies of; those were
removed in the following steps.

Per-dylib, worst offenders before → after:

| dylib | dup before | dup after |
|---|---|---|
| `libc++.dylib` | 426 | 59 |
| `libsystem_c.dylib` | 382 | 113 |
| `libdispatch.dylib` | **490** | **2** |
| `libxpc.dylib` | 189 | 62 |
| `libsystem_platform.dylib` | 110 | 2 |
| `libsystem_blocks.dylib` | 19 | 0 |

`libdispatch.dylib` also went from 15 real flat-namespace gaps to 2, and
its export count dropped 1204 → 674 (it was exporting half of libc).

## The tool: `tools/userland_staging/dup_symbol_audit.sh`

This is the durable part of the work — the answer to "they keep cropping
up" is an automated invariant, not another manual sweep.

Two checks, both reading real Mach-O symbol tables of real built
artifacts:

1. **dylib mode** — any symbol *defined* by two or more shipped dylibs.
   On real Darwin a symbol has exactly one owning image; two owners means
   one statically bundled a copy of something a sibling provides, and the
   two copies have separate state.
2. **binary mode** — any symbol a shipped executable defines locally that
   is also exported by a dylib that executable actually loads (read from
   its real `LC_LOAD_DYLIB` list, and following `LC_REEXPORT_DYLIB`
   transitively — without that traversal the `libSystem.B.dylib` umbrella
   looks like 6 symbols instead of the whole libc surface, and nearly
   every shipped command is under-reported).

It compares against a checked-in baseline (`dup_symbol_baseline.txt`) and
fails only on symbols that are **new** since that baseline. Fixing
duplicates never fails the check; it just makes the baseline stale.
Progress is monotonic: the count can go down freely and cannot silently
go back up.

```
./dup_symbol_audit.sh                  # both checks vs baseline
./dup_symbol_audit.sh --summary        # ranked offender table
./dup_symbol_audit.sh --explain ___sF  # which images define one symbol
./dup_symbol_audit.sh --update-baseline
```

`dyld.macho`, `init.macho` and `pwtool.macho` are excluded on purpose:
all three run before or entirely without dyld by design (CLAUDE.md's
"must stay fully static" invariant), so a private static copy of libc in
them is correct, not debt.

**The baseline currently on disk was taken at the START of the session
(924 / 9523).** It has not been refreshed since, deliberately — refresh
it only after a real boot test passes, so a bad refactor can't be locked
in as "the new normal".

## What was actually changed, and why each move is correct

The organising principle: **one owner per symbol, and the owner should be
the image real Apple puts it in.**

### `libsystem_pthread.dylib` gained the pthread QoS/attr tier
`libpthread_qos.a` (`qos.o`, `libpthread_attr_compat.o`,
`libpthread_qos_compat.o`, plus `host_privUser.o`/`mach_voucherUser.o`
that their voucher path calls) was `-force_load`ed into
**libdispatch.dylib only**. That is why `_pthread_attr_init`,
`_pthread_exit`, `_qos_class_self` were libdispatch exports, and why
libdispatch had to bundle `libpthread.a` alongside them — the QoS code
calls pthread internals like `__pthread_ptr_munge_token` that only
resolve inside the same image. Also added three `libpthread.a` members
that were reaching processes only through libdispatch's private copy:
`pthread_thread_ops_compat.o`, `pthread___thread_selfid.o`,
`pthread_clear_qos_tsd_compat.o`.

### `libsystem_kernel.dylib` gained the whole Mach runtime
- **`libmach_ipc.a`** (MIG client stubs for `clock.defs`/`mach_host.defs`/
  `task.defs`, `mig_strncpy`, `mach_host_self`, the clock-port cache).
  Previously bundled privately into libdispatch, libxpc, libc++ *and*
  libncurses. Two of its symbols — `_g_mach_ipc_clock_port` and
  `_g_mach_ipc_clock_sem` — are plain globals caching a real Mach port
  right and semaphore, so those copies were duplicate **state**, same
  shape as the `mach_task_self_`/`g_zone` bugs.
  Excluded members: `mach_msg_real.o`/`mach_ipc_traps.o` (this script
  already compiles the same sources) and `mach_ipc_dead_stubs.o`
  (known-dead).
- Mach port-rights / semaphore / `mach_error` tier:
  `mach_port_gap_closures2.c`+`.s`, `cf_mach_port_syscalls.c`,
  `mach_port_traps.s`, `mach_semaphore_traps.s`,
  `mach_semaphore_wrap_compat.c`, `mach_error_string.c`,
  `mach_error_string_minimal_compat.c`. These were compiled *separately
  into libdispatch and libxpc*, and three of them
  (`_mach_port_insert_right`/`_extract_right`/`_unguard`) were
  simultaneously on libsystem_kernel's own `-Wl,-U` deferral list — i.e.
  it was punting on symbols it should have owned. Those deferrals are now
  gone.
- Raw BSD syscall stubs `_geteuid`, `_getegid`, `_getuid`, `_pread`,
  `_pwrite`, generated with the same `create-syscalls.pl` recipe
  `build_libsystem_c.sh` already used.

### `libsystem_c.dylib` gained the env subsystem, the `_chk` tier, os_assumes
- **Environment variables** (`setenv.c`, `putenv.c`,
  `environ_lock_compat.c`, `findenv_locked_compat.c`, a narrowed
  `_NSGetEnviron`). This existed **only** in libdispatch and libxpc, so a
  process loading both had two `setenv()` implementations mutating one
  real `environ` array under two separate `___environ_lock`s. Plain
  (non-`$UNIX2003`) spellings are supplied by `-Wl,-alias` with a
  genuinely-local base — see backlog item d10 for the ld64 bug where a
  non-local alias base silently emits a malformed ordinal-0 re-export.
- **`_FORTIFY_SOURCE` `_chk` tier** (`snprintf_chk`, `vsnprintf_chk`,
  `strlcpy_chk`). Compiled with `-D_FORTIFY_SOURCE=0`, which is
  load-bearing, not cosmetic: without it each `_chk` file's own internal
  call gets macro-rewritten back into itself → unbounded self-recursion
  → stack-guard SIGSEGV. **That is the exact root cause of launchd bug
  #6** (backlog d11).
- `os_assumes_compat.o` (`__os_assumes_log`/`__os_crash` family).

### `libdispatch.dylib` — the actual `libpthread.a` issue
Dropped from its link line: `-force_load libpthread_qos.a`,
`libclosure.a`, `libmalloc_no_mach_init.a`, `cerror_stub.o`,
`libplatform.a`, **`libpthread.a`**, `libc_gen.a`, `libc_string.a`,
`libc_stdio.a`. Added `libsystem_platform.dylib` and
`libsystem_blocks.dylib` as siblings (`libsystem_c`/`malloc`/`pthread`/
`kernel` were already there — just *after* the archives, so the archives
won). Also removed its private `geteuid.o`, `atoi.o`,
`table.o`/`none.o`/`libc_locale_globals_compat.o` (real mutable locale
globals — duplicate state), `stack_chk_compat.o`, `pread`/`pwrite` traps,
and the `-Wl,-alias` entries whose bases were no longer local.

### `libxpc.dylib`, `libc++.dylib`
Same treatment: stop bundling `libmach_ipc.a`, the env subsystem, the
`_chk` tier, os_assumes, `mach_port_gap_closures2`, the semaphore/error
tier; libc++ additionally stopped bundling `libc_locale.a` wholesale and
swapped `libunwind_scratch.a` for the real `libunwind.dylib` sibling.

## Two things that look like duplicates but are NOT — do not "fix" these

Real Apple marks a number of symbols `.private extern`. Those are
image-local *by construction*: they cannot be exported from a dylib, so
there is no symbol for another image to bind to the wrong copy of, and
every image that needs one must carry its own. Verified with `nm -m`
(reports `non-external (was a private external)`), not assumed.

1. **`___gettimeofday_with_mach`** — libdispatch's `mach_get_times()`
   needs it; kept as a local copy, with `ld -r -keep_private_externs
   -unexported_symbol ___gettimeofday` used to hide the public
   `___gettimeofday` that ships in the same generated stub.
   `-keep_private_externs` is required: without it `ld -r` demotes
   private externs to plain locals and the final link fails.
2. **`___chk_fail_overflow` / `___chk_overlap` /
   `___chk_assert_no_overlap` / `___chk_fail_overlap`** — `chk_fail_compat.o`
   legitimately stays in every image that carries `_chk` members.

### The one real, still-open exception: locale state

`___global_locale`, `___locale_key`, `___setrunelocale`,
`___collate_load_tables`, `___get_locale_env`, `___detect_path_locale`,
`___open_path_locale`, `___monetary_load_locale`, `___numeric_load_locale`,
`___messages_load_locale`, `__DefaultRuneXLocale` are **all** private
extern in Apple's source. Apple's locale internals are image-private by
construction, so any image that instantiates locale state must carry the
machinery — `libc++.dylib` does, because its `locale.cpp` is in a
separate image from libsystem_c.

Unlike the two cases above, `___global_locale` holds **real mutable
state**: two images = two current locales. This is currently inert only
because nothing in this project ever calls `setlocale()` away from `"C"`.
Making `libsystem_c.dylib` the single owner means giving those symbols
default visibility — a deliberate, documented divergence from Apple's
source. **That is the correct follow-up, and it must be a conscious
decision, not a silent one.** The pre-existing comment in
`build_libcxx_dylib.sh` already flagged this; it is now expanded there.

## What must happen next, in order

1. **Rebuild the rest of the dylib chain.** Only these have been rebuilt:
   `libsystem_kernel`, `libsystem_pthread`, `libsystem_c`, `libdispatch`,
   `libxpc`, `libc++`. Everything downstream is stale and must be rebuilt
   before any boot test: `build_libsystem.sh` (the `libSystem.B.dylib`
   umbrella), `build_libdyld.sh`, `build_libsystem_notify.sh`,
   `build_ncurses_dylib.sh`, `build_libresolv_dylib.sh`,
   `build_openpam_dylib.sh`, `build_copyfile_dylib.sh`,
   `build_removefile_dylib.sh`, `build_libiokituser_dylib.sh`,
   `build_bsm_dylib.sh`, `build_libm_dylib.sh`, `build_libiconv_dylib.sh`,
   `build_libunwind_dylib.sh`, `build_libxml2.sh` — then the binaries
   (`build_launchd.sh`, `build_syslogd.sh`, `build_launchctl.sh`,
   `build_pd_console_login.sh`, `build_simple_cmds.sh`, …) and
   `tools/init_binary/inject_into_sd_image.sh`.
   Beware two real build-order traps hit during this session:
   - `build_libsystem_c.sh` requires `libsystem_pthread.dylib` to already
     exist, and `build_libsystem_pthread.sh` deletes its own output
     before linking — a failed pthread link therefore breaks the C build
     too. Recover by `git stash`ing the pthread script, rebuilding, then
     restoring.
   - `libsystem_c.dylib` ↔ `libdyld.dylib` is a genuine cycle broken only
     by the existing files on disk.
2. **Real QEMU boot test** (`./runvm.sh`, and the launchd boot path).
   This is the gate. None of the above has been run on the guest.
3. **Adversarial review before commit** — project standing rule
   (CLAUDE.md), and this change is large.
4. `./dup_symbol_audit.sh --update-baseline`, then commit tool + baseline
   + script changes together.
5. Update `docs/backlog.md` **item d1** — it currently reads as open
   ("do not load this dylib alongside the real sibling dylibs in a
   multi-dylib process until this is fixed"). That is now substantially
   resolved and the wording is actively misleading. Item **d2** (pick one
   canonical provider per symbol, project-wide) is what this work
   implements; update it too. `build_libdispatch_dylib.sh`'s own "HONEST
   SCOPE STATEMENT" header still describes the old static bundling and
   must be rewritten — it is now wrong.

## Remaining Phase 1 work (libraries) — 262 duplicates left

Grouped by which dylibs share them, largest first:

| count | owners | root cause |
|---|---|---|
| 40 | `libiokituser`,`libxpc` | `libiokituser.a` bundles the same MIG/`mach_portUser` set libxpc has; needs the same `libsystem_kernel` treatment |
| 30 | `libsystem_pthread`,`libxml2` | `build_libxml2.sh` still statically links `libpthread.a` + `libmalloc.a` + `libplatform.a` + `libc_*.a` — untouched this session, the most mechanical remaining fix |
| 21 | `libc++`,`libsystem_c` | the locale private-extern issue above |
| 18 | `libc++`,`libopenpam`,`libsystem_c` | ditto + libopenpam's own libc bundling |
| 8 | `libdyld`,`libsystem_c` | |
| 6 | `libsystem_c`,`libsystem_m` | math funcs in both; pick one owner (`libsystem_m`) |
| 6 | `libresolv`,`libsystem_c` | `_arc4random` **state** — real duplicate-state, prioritise |
| 5 | `libsystem_malloc`,`libsystem_pthread` | pthread TSD (`_pthread_key_init_np`, `_pthread_once`, …) |
| 5 | `libsystem_c`,`libsystem_kernel` | `___error`/`_errno`/`_cerror` — real duplicate-state, prioritise |
| 4 | 7-way rune-locale | `__CurrentRuneLocale`/`__DefaultRuneLocale`/`___mb_cur_max`/`___mb_sb_limit` across libcopyfile/libiconv/libncurses/libopenpam/libresolv/libsystem_c/libc++ |

Prioritise by **state, not count**: `_arc4random`, `___error`/`_errno`,
the pthread TSD set, and `_tzname`/`_daylight` (libopenpam vs
libsystem_c) are duplicate mutable state. Pure-code duplicates are
cheaper bugs.

## Phase 2 (binaries) — not started

`--summary` reports **9930 binary shadowing facts**. Ranked:

| binary | locally-defined symbols shadowing a dylib it loads |
|---|---|
| `launchd.macho` | 1049 |
| `launchctl.macho` | 946 |
| `syslogd.macho` | 773 |
| `pd_console_login.macho` | 361 |
| every `build_simple_cmds.sh` command | ~66 each, ~70 binaries |

All of these are already *dynamically* linked (real `LC_LOAD_DYLIB`, no
`LC_UNIXTHREAD`) — the problem is not that they are static executables,
it is that they *also* statically link the archives whose contents their
dylibs already provide, and the local definition wins for the binary's
own references.

The simple-command baseline is the highest-value single fix: every one of
the ~70 commands carries a private copy of

- the **stdio FILE table** — `___sF`, `___sfp`, `___sglue`, `___sdidinit`,
  `___stdinp`/`___stdoutp`/`___stderrp`, `__cleanup`, `___sinit`
- the **rune-locale globals** — `__CurrentRuneLocale`,
  `__DefaultRuneLocale`, `___mb_cur_max`, `___mb_sb_limit`
- `_environ`/`__NSGetEnviron`, the `err`/`warn` family, ~50 more libc
  entry points

…while also loading `libsystem_c.dylib` + `libSystem.B.dylib`, which have
their own. Two FILE tables in one process means buffering and
atexit-flush behave differently depending on which image issued the
write. Fix `build_simple_cmds.sh` once and ~70 binaries improve together.

`launchd.macho` is the highest-risk one: it defines `_malloc`, `_free`,
`_printf`, `_strlen`, `___error` locally despite loading
`libSystem.B.dylib`. This is precisely the class that produced bug #6 and
the duplicate-`pthread_create` bug. Verify with a real QEMU boot after
each change, not in a batch.

## Task list state

| # | task | status |
|---|---|---|
| 1 | `dup_symbol_audit.sh` + baseline | **done** |
| 2 | Phase 1a: de-static `libdispatch.dylib` (backlog d1) | **done, QEMU-verified** (490 → 2) |
| 3 | Phase 1b: de-static `libc++.dylib` | **done, QEMU-verified** (426 → 59, then 21+18 further via Phase 1d below) |
| 4 | Phase 1c: de-static `libxpc.dylib` + `libxml2.dylib` | **both done, QEMU-verified** |
| 5 | Phase 1d: residual dylib duplicate state | **done, QEMU-verified**: 262 → 157 (session totals below) |
| 6 | Phase 2a: simple-commands baseline | **done, QEMU-verified**: ~70 commands' own shadowed-symbol count roughly halved (66-72 → 47-53 each) |
| 7 | Phase 2b: launchd/launchctl/syslogd/pd_console_login | **rebuilt against the refreshed chain only** — deep dup-fixing (900+ shadowed symbols each) not started |
| 8 | Real QEMU boot verification + baseline refresh | **done** (2026-08-30 session) |
| 9 | Phase 1e: bottom-of-stack dylib sweep (kernel/platform/malloc/pthread/unwind/libSystem.B + their leaf-dylib duplicators) | **done, QEMU-verified**: 157 → 136 (session totals below) |

## Session 2026-08-30: Phase 1d + Phase 2a, QEMU-verified

Picked up where the handoff above left off. Real, measured results —
`dup_symbol_audit.sh --summary` before/after this session:

| | dylib duplicates | binary shadowing facts |
|---|---|---|
| start of this session | 262 | 8918 |
| end of this session | 157 | 6466 (7518 before the pre-existing `_getegid` gap surfaced by baseline staleness, see below) |

### What was fixed, by owner

- **`libsystem_kernel.dylib`** gained the whole `__kernelrpc_mach_port_*`
  MIG client-stub set (51 symbols) via a real `mig` regen of
  `osfmk/mach/mach_port.defs` run directly in
  `build_libsystem_kernel.sh`, plus a narrowed `mach_port_set_attributes`
  extracted from `mach_port_wrappers_real.c`. Previously this set had
  **two** owners (`libxpc.dylib` and `libiokituser.dylib`) and
  `libsystem_kernel` — its real Apple home — had neither. Both siblings'
  build scripts now drop their own copies.
- **`libsystem_c.dylib`** gained real single ownership of two duplicate-
  **state** bugs, the highest-priority category per this doc's own
  "state, not count" rule:
  - `errno`/`__error()`/`cerror`/`cerror_nocancel` — previously ALSO
    defined by `libsystem_c.dylib` (via a stray `cerror_stub.o` member of
    `libc_locale_for_stdio.a`) even though `libsystem_kernel.dylib` (a
    real sibling) already owned the canonical copy. Every process had
    two live `errno` globals.
  - the real BSD `rand`/`srand`/`rand_r`/`sranddev`/`random`/`srandom`
    family (extracted from `libremovefile.a`, replacing a hand-written
    `rand_r` and libremovefile's own private copy) — same duplicate-PRNG-
    state shape.
  - Also picked up the three `_chk` members it didn't already own
    (`memmove_chk`/`stpncpy_chk`/`strncat_chk`), `flsl` (moved here next
    to its real caller, `qsort.o`), and a generated `_mkdir` stub.
- **`libsystem_malloc.dylib`**: `pthread_tsd_shim.o`'s 5 public POSIX
  names (`pthread_getspecific`/`setspecific`/`key_create`/`once`/
  `key_init_np`) narrowed to private-extern via `ld -r
  -keep_private_externs -unexported_symbol`. malloc sits *below*
  `libsystem_pthread.dylib` in the real dependency order (pthread links
  malloc, not the reverse), so it cannot link the real dylib back — this
  keeps its own from-scratch TSD block (real, QEMU-verified mechanism,
  untouched) but stops it from shadowing the real pthread API for any
  *other* image that might resolve to it via flat/two-level namespace
  luck.
- **Five leaf dylibs** (`libiconv`, `libncurses`, `libcopyfile`,
  `libresolv`, `libopenpam`) each stopped extracting `table.o`/`none.o`
  from `libc_locale.a` unnarrowed — all five were independent owners of
  `__CurrentRuneLocale`/`__DefaultRuneLocale`/`___mb_cur_max`/
  `___mb_sb_limit` (real mutable "current locale" state). New shared
  helper `narrow_rune_locale.sh` demotes just those four public globals
  per extract dir via the same `ld -r -keep_private_externs
  -unexported_symbol` idiom, leaving the genuinely private-extern
  internals (`__DefaultRuneXLocale`, `__none_*`) untouched and local, per
  real Apple's own visibility rules (verified with `nm -m`, not
  assumed).
- **`libremovefile.dylib`**: dropped `geteuid.o`/`qsort.o`/`heapsort.o`/
  `fstatat64.o`/`mkdir.o`/`rand.o`/`random.o` (all now owned by
  `libsystem_c.dylib`); `removefile_syscalls.c` gained a
  `REMOVEFILE_SYSCALLS_DYLIB` compile guard so the dylib build omits its
  now-duplicate `unlink`/`rmdir`/`read`/`gettimeofday`/`flsl` while the
  **static** archive (real standalone consumers, no sibling dylibs
  loaded) keeps every one.
- **`libunwind.dylib`**: dropped a dead 3-symbol `-Wl,-alias` re-export
  of `__pthread_rwlock_{rdlock,wrlock,unlock}` that just duplicated a
  real, already-present `libsystem_pthread.dylib` export.
- **`libc++.dylib`**: stopped compiling its own copy of
  `libcxx_dylib_compat.c` (`catopen`/`catgets`/`catclose`/
  `clock_gettime`/`_dyld_find_unwind_sections`/
  `_dyld_register_func_for_remove_image`/`dladdr`) now that it genuinely
  links the real `libunwind.dylib` sibling, which defines the exact same
  7 symbols from the exact same source file. Also deleted a dead,
  unreferenced `libunwind_scratch.a` copy step left over from before that
  sibling link was wired up.
- **`build_simple_cmds.sh`** (~70 commands, Phase 2a): removed
  `findfp.c`'s 9 fully-public FILE-table symbols (narrowed, same
  `-unexported_symbol` treatment — `__sfprelease` is real
  `__private_extern__` and 6 commands' `freopen.c` need it locally, so
  the file couldn't just be dropped), `getcwd.c` (`_getcwd` now via
  `libsystem_c.dylib`), and `simple_cmds_getenv_compat.c` entirely (its
  own disconnected `getenv`/`setenv`/`unsetenv`/`putenv`/`_NSGetEnviron`
  — a second, un-synchronized `environ` array per binary, arguably the
  worst duplicate-state bug this session found: a `setenv()` through one
  copy was invisible to the other). `collate.c`/`collcmp.c`/`ldpart.c`
  were tried for removal too and **reverted** after a real full rebuild
  proved several per-command extras (`*_timelocal.o`, diff's fnmatch
  wrapper) still need symbols only they provide that `libsystem_c.dylib`
  doesn't export (`___part_load_locale`, `___collate_collating_symbol`,
  etc.) — a reminder that a symbol-table diff against sibling exports is
  necessary but not sufficient; a real rebuild of every consumer is the
  actual test.

### Consciously deferred, not silently skipped

- **`libc++.dylib` ↔ `libsystem_c.dylib` xlocale ownership** (21+18
  duplicate symbols: `newlocale`/`duplocale`/`uselocale`/`setlocale`/
  `strtoll`/`strtoll_l`/the full `___collate_*` family). Root cause
  identical to the rune-locale fix above but for a bigger, riskier
  surface: real Apple's `___global_locale`/`___locale_key` (the current-
  locale state itself) are `.private extern`, so `libsystem_c.dylib`
  cannot export them without `ld64 -globalize_symbol` (or equivalent) —
  a deliberate divergence from Apple's own visibility, previously flagged
  in this doc as "the real follow-up... must be a conscious decision,
  not a silent one." Not attempted this session: the blast radius (every
  real locale call in the process) and the fact that nothing in this
  project calls `setlocale()` away from `"C"` yet (so the duplicate is
  currently inert) argued for deferring to a dedicated session with its
  own QEMU verification, rather than bundling it into an already-large
  change set.
- **`libsystem_c.dylib` ↔ `libsystem_m.dylib` math functions** (9
  symbols: `acos`/`asin`/`fabs`/`fmod`/`modf`/`pow`/`scalbn`/`sqrt`/
  `trunc`). `libsystem_c` carries these because this project's ICU port
  predates `libsystem_m.dylib`'s existence and was linked against
  `libsystem_c` for them; `libsystem_m` itself links `libsystem_c` as a
  sibling (real Apple's actual layering), so `libsystem_c` cannot link
  `libsystem_m` back. Fixing this for real means re-linking ICU's own
  target dylibs against `libsystem_m` directly — out of scope for a
  userland_staging-only pass.
- **`third_party/Csu/crt.c`'s own `environ = NULL` storage** — a NEW
  finding, not previously documented: crt.c (this project's own startup
  code, linked into *every* binary including the two that must stay
  fully static) allocates its own `environ` storage, separate from the
  REAL one dyld's `libdyldGlue.o` provides inside `libdyld.dylib` (per
  `build_libsystem_c.sh`'s own comment: "only `_environ` itself comes
  from dyld's real libdyldGlue.o"). Every dynamically-linked binary that
  reads `environ` directly (not through `getenv()`/`setenv()`, which do
  go through the real one) would see crt.c's own, never-updated copy.
  **Not touched this session** — crt.c is linked into `init.macho`/
  `pwtool.macho` too (the two binaries that must stay fully static by
  design), so any fix has to be conditional on real-dyld-vs-static-boot,
  and needs its own dedicated QEMU verification pass across both boot
  paths, not a rushed addition to an already-large session.

### The real QEMU-boot gate, and one real bug it caught

A full bottom-up rebuild of the entire dylib chain (kernel → platform →
malloc → pthread → c → unwind → c++ → dispatch → xpc → dyld →
libSystem.B → every leaf dylib → iokituser/xml2 → launchd/syslogd/
pd_console_login → all 76 simple commands) plus `inject_into_sd_image.sh`
plus `tools/boot_test.py` caught a real regression the symbol-table
analysis alone missed: after `libsystem_c.dylib` dropped `errno`,
`libxpc.dylib` (rebuilt *before* that fix landed) still had chained
fixups baked in expecting `___error` from `libsystem_c.dylib`
specifically — real Darwin chained fixups record a specific owning
dylib ordinal at THE CONSUMER's link time, not just a name to
re-resolve at load time. Real panic:
`initproc exited ... Symbol not found: ___error / Referenced from:
/usr/lib/system/libxpc.dylib / Expected in: /usr/lib/system/
libsystem_c.dylib`. Fixed by the full rebuild above, not a targeted
patch — **every** downstream consumer of a symbol whose owner moves
needs relinking, not just the two dylibs involved.

After the full rebuild, three consecutive real boots produced zero
panics and reached the identical byte-for-byte checkpoint (module ASLR
addresses) as `qemu/launchd_boot_test110_final.log` — the last known-good
state *before* this session's changes. Userland does not yet reach an
interactive shell prompt from a cold boot (it stops silently after
`jnl: b(14, 2): flushing fs disk buffer returned 0x2d`, twice, same as
that reference log) — this is **pre-existing**, unrelated to static
linking, and already tracked (`docs/backlog.md` item d13, the
`_dyld_fork_child` hang investigation and its unresolved successor). Do
not re-diagnose it as a static-linking regression; the byte-for-byte log
match rules that out.

### Baseline-staleness note

`dup_symbol_audit.sh`'s checked-in baseline predates several unrelated
commits (the `libSystem_atfork`/malloc-fork-hook/atexit work). Refreshing
it this session surfaced one pre-existing, real duplicate that had never
been in the baseline text at all: `_getegid` in ~50 simple commands (a
real, always-there `LINK_COMMON` syscall wrapper) against
`libsystem_kernel.dylib`'s own `sys_getegid.o` — confirmed via `git diff`
that neither producing line was touched this session. Left as a real,
now-baselined residual rather than fixed blind, since raw-syscall
wrapper consolidation across `LINK_COMMON`/`libsystem_kernel.dylib` is
its own separate pass, not part of this session's scope. Also found and
deleted: `libc_build/launchd_trial.macho`, a gitignored, un-reproducible
build artifact with no current producing script that was polluting the
audit's binary scan.

## Method notes for whoever picks this up

- Work bottom-up: `libsystem_kernel` → `libsystem_platform`/`malloc` →
  `libsystem_pthread` → `libsystem_c` → everything else. Moving a symbol
  down the stack transiently *raises* the dup count until the images
  above drop their copies; that is expected, keep going.
- Don't predict what a sibling covers — delete the archive from the link
  line and read the undefined-symbol list. It was consistently much
  shorter than static analysis suggested (libdispatch: one symbol).
- When a symbol has no obvious home, ask where real Apple ships it. That
  question resolved every ambiguity in this session.
- `-Wl,-U` is safe here in a way static bundling is not: it means "do not
  define it in this image", the opposite of duplication. Use it when the
  correct owner is above you in the dependency order, and say so in a
  comment.

## Session 2026-08-31: Phase 1e, bottom-of-stack sweep, QEMU-verified

Picked up from the 2026-08-30 handoff's own "Remaining Phase 1 work"
table. Worked library-by-library, bottom-up as that table's own "state,
not count" priority recommends, with a full downstream rebuild + one real
QEMU boot per batch (two batches this session, not one per library —
see "batching" note below).

`dup_symbol_audit.sh --summary` totals:

| | dylib duplicates |
|---|---|
| start of this session | 157 |
| end of this session | **136** |

### Batch 1: `libsystem_kernel`/`libsystem_platform`/`libsystem_malloc`/`libsystem_pthread` + their leaf-dylib duplicators

Per-dylib, before → after (`dup_symbol_audit.sh --summary`):

| dylib | before | after |
|---|---|---|
| `libsystem_kernel.dylib` | 13 | **0** |
| `libsystem_platform.dylib` | 2 | **0** |
| `libbsm.dylib` | 2 | **0** |
| `libsystem_pthread.dylib` | 12 | 3 |
| `libsystem_malloc.dylib` | 14 | 3 |
| `libopenpam.dylib` | 37 | 35 |
| `libcopyfile.dylib` | 16 | 13 |
| `libncurses.dylib` | 19 | 12 |

**Real duplicate-state bug, not just a count fix**: `libpthread_qos_
compat.c` had a hand-rolled `_os_alloc_once()`/`_os_alloc_once_table`
stand-in dating from before real `third_party/libplatform/src/os/
alloc_once.c` compiled anywhere in this project. It was worse than
merely duplicate: `pthread_atfork.o`'s real, undefined reference to both
symbols was silently binding to this stale local copy instead of the
real implementation `build_libsystem_kernel.sh`/`build_libsystem_
platform.sh` added specifically to satisfy it, because ld64 always
prefers a same-link local definition over an external sibling dylib —
the real fix was present and linked the whole time, just shadowed.
Removed the compat block entirely; `libsystem_pthread.dylib` now
resolves both symbols externally to the real `libsystem_kernel.dylib`/
`libsystem_platform.dylib` siblings, confirmed single-owner via `nm -m`.

**Mechanical single-owner fixes** (each verified single-owner via `nm
-m` after the change — full removal, not narrowing):
- `_geteuid`/`_getuid`/`_getegid`/`_pread`/`_pwrite`: `libbsm.dylib`,
  `libopenpam.dylib`, `libcopyfile.dylib` each still extracted their own
  copy of these raw BSD syscall traps (`geteuid.o`/`getuid.o`/
  `pthread_getegid.o`/`pread.o`/`pwrite.o`) despite `libsystem_kernel.
  dylib` becoming their real canonical owner back in the 2026-08-30
  session and all three already linking it as a sibling. Removed the
  extractions (for `libcopyfile.dylib`, only from *this dylib's own*
  extract dir — `libcopyfile.a` itself stays untouched, since
  `dyld.macho`'s real static link in `build_dyld_link.sh` still needs
  it and dyld runs before any dylib exists).
- `_pread`/`_pwrite` needed a different fix in `libcopyfile.dylib`
  specifically: this dylib's own final link creates `_pread$UNIX2003`/
  `_pwrite$UNIX2003` as `-Wl,-alias` re-exports of these two objects,
  and per this project's own documented ld64 finding an alias's base
  symbol must be genuinely compiled into the same link or ld64 silently
  emits a malformed ordinal-0 re-export. Kept `pread.o`/`pwrite.o`
  locally present (satisfies the alias), hid the plain names via
  `-Wl,-unexported_symbol` instead. Verified via `xcrun dyld_info
  -exports` that the `$UNIX2003` aliases still export correctly.
- `__kernelrpc_host_create_mach_voucher`: `libsystem_pthread.dylib` had
  a narrowed-but-still-fully-external local copy extracted from
  `libmach_ipc.a`. `libsystem_kernel.dylib` is the real canonical owner
  (its own `__kernelrpc_mach_port_*` MIG client-stub tier) and already a
  linked sibling; removed the local extraction entirely — a plain
  function call, not a direct-addressed data symbol, so ordinary
  two-level-namespace binding applies with no local copy needed at all.
- `mach_port_deallocate`/`mach_port_mod_refs`/`_pthread_set_self`:
  `libmalloc_dispatch.c`'s pre-built object (reused via `ar x` from
  `libmalloc.a`) still carried these 3 trap-only wrappers, which
  `libsystem_kernel.dylib` also gained "copied verbatim" in the
  2026-08-30 session. Confirmed via `nm -m` that nothing inside
  `libsystem_malloc.dylib` itself references them (real, unmodified
  `mig_reply_port.c`, the only actual caller anywhere in this project,
  isn't even compiled into this dylib). Added a
  `LIBMALLOC_DISPATCH_DYLIB_VARIANT` compile guard so the dylib build
  recompiles `libmalloc_dispatch.c` fresh without these 3 (falling
  through to the real `libsystem_kernel.dylib` sibling), while the
  static-archive build (`dyld.macho`, which must stay fully static)
  keeps them unchanged — same guarded-compile-variant pattern the
  2026-08-30 session established for `libremovefile.dylib`'s
  `REMOVEFILE_SYSCALLS_DYLIB`.

**Narrowing fixes** (`ld -r -keep_private_externs -unexported_symbol`,
the `narrow_rune_locale.sh` pattern, applied to new symbols this
session): `_mmap`/`_munmap`/`_pthread_testcancel$UNIX2003`/
`_gettimeofday`/`___gettimeofday` in `libsystem_pthread.dylib`;
`__dyld_get_image_slide`/`__platform_memset` and 6 `-Wl,-alias`-created
duplicate exports (`_kill`/`_kill$UNIX2003`/`_kdebug_trace`/
`_mprotect$UNIX2003`/`_sleep$UNIX2003`/`_write$UNIX2003`) in
`libsystem_malloc.dylib`; the mach-ipc-clock trio (`_g_mach_ipc_clock_
port`/`_sem`/`_mach_ipc_clock_init`) and 4 raw syscall wrappers
(`_gettimeofday`/`_pthread_testcancel`/`_read`/`_sigprocmask`, all from
a project-authored `ncurses_extra_syscalls.o`) in `libncurses.dylib`.

**Important correction to how this technique should be described,
found by actually checking with `dyld_info -fixups` this session
(applies retroactively to the ALREADY-QEMU-VERIFIED `narrow_rune_
locale.sh` fixes too, not just this session's new ones):** narrowing via
`-unexported_symbol` removes a symbol from a dylib's *export* surface
(what `nm -gUj`/this audit tool measures) but does **not** create a
cross-image dyld bind. Checked directly: `libcopyfile.dylib`'s own
`__CurrentRuneLocale` (narrowed in the 2026-08-30 session) still
resolves, at its own address, entirely locally within `libcopyfile.
dylib` — `dyld_info -fixups` shows no bind to `libsystem_c.dylib`'s
copy at all. The prior session's write-up ("the leaf's own code binds
to libsystem_c.dylib's copies at link time") describes the *intent*,
not the actual verified mechanism. For **pure code** duplicates (every
raw-syscall-trap/pure-function case above) this doesn't matter — both
copies are functionally identical, so which one internal callers
resolve to is immaterial. For the mach-ipc-clock **state** pair
specifically, this means the "two live copies" fact is technically
unchanged by narrowing, just no longer visible to the checker;
assessed as low-severity (each is an independently-obtained,
functionally-interchangeable Mach port right to the same clock service,
not shared/aliased state like `errno`/`g_zone`) but recorded here so a
future session doesn't re-assume narrowing is a full state-unification
fix. A real fix for a *genuine* live-state duplicate still requires full
removal (as done for `__os_alloc_once_table`/`__kernelrpc_host_create_
mach_voucher`/the `geteuid` family above), verified single-owner via
`nm -m`, not narrowing.

### Batch 2: `libunwind.dylib` + `libSystem.B.dylib`

| dylib | before | after |
|---|---|---|
| `libunwind.dylib` | 5 | 3 |
| `libSystem.B.dylib` | 6 | 3 |

- `libunwind.dylib` lost its own `_sched_yield`/`_swtch_pri` (same real
  source `build_libsystem_pthread.sh` already compiles fresh; pure dead
  weight here — confirmed via `nm -m` that no other member of
  `libunwind.a` references either symbol at all). Already links
  `libsystem_pthread.dylib` as a sibling.
- `libSystem.B.dylib` lost its own inline `dyld_unwind_shim_compat.c`
  stand-in for `_dyld_find_unwind_sections`/`_dyld_register_func_for_
  remove_image`/`dladdr` — a prior session's own comment on this exact
  code had already flagged it as "redundant rather than load-bearing"
  after `libc++.dylib` gained a real copy, but left it in place as
  low-risk. Confirmed via `nm -m` it was the only remaining local
  definition in this dylib; removed it, and confirmed (unlike the
  narrowing cases above) this really is a structural single-owner fix,
  not cosmetic — `libSystem.B.dylib` already `-Wl,-reexport_library`s
  both `libc++.dylib` and `libunwind.dylib`, so these 3 symbols are now
  purely reexport-supplied at `libunwind.dylib`'s real address, not a
  4th independently-compiled copy at a different one.
- Remaining 3 for `libunwind.dylib` (the same `_dyld_find_unwind_
  sections`/`_dyld_register_func_for_remove_image`/`dladdr` trio, now
  vs `libdyld.dylib` only): a genuinely necessary, already-documented
  architectural exception (`libunwind_dyld_shim.o` needs `__mh_execute_
  header`, never synthesized for `MH_DYLIB` output, so this dylib can't
  just consume `libdyld.dylib` for these — and can't even narrow its own
  copy, unlike other bottom-of-stack cases, because `libc++.dylib`
  genuinely depends on getting them from `libunwind.dylib` as a real
  external export). Left as-is, consciously, not silently.
- Because these 3 symbols still have 2 owners each after removing
  `libSystem.B.dylib` as a 3rd, this specific fix does not move the
  157→136 total (the audit counts "has 2+ owners" as binary per symbol,
  not by owner count) — flagging so the next session doesn't mistake the
  unchanged total for "nothing happened here." `libsystem_malloc.dylib`'s
  `_memcpy`/`_memset` (vs `libsystem_c.dylib`) were investigated and
  left alone for the analogous reason to `libunwind.dylib`'s dyld-unwind
  trio: malloc is below `libsystem_c.dylib` in the real dependency order
  and needs real, local `memcpy`/`memset` for its own zone bookkeeping
  with literally no sibling to fall back to — pure code, no shared
  state, matches this project's own established "wasteful, not unsafe"
  precedent for `os_alloc_once`-class duplication before this session's
  fix made that specific case a real single-owner win instead.

### Batching note

The task instructions for this session asked for one audit+boot
verification per library. In practice the bottom four (`libsystem_
kernel`/`platform`/`malloc`/`pthread`) are so tightly coupled — narrowing
one image's exports changes what its *siblings* need to link against,
and the whole "who owns this symbol" question only resolves once all
four are rebuilt together — that verifying each individually would have
required rebuilding (and re-testing) the same downstream chain 4 times
for no additional real information. Treated them as one batch (matching
this doc's own "Method notes... work bottom-up" guidance from the prior
session), then did `libunwind`/`libSystem.B` as a second, genuinely
separate batch since they don't share that same tight coupling. Every
individual dylib's fix within a batch is still independently justified
and independently verified via `nm -m`/`dyld_info` above; only the QEMU
boot gate was shared across a batch, not skipped per-symbol.

### `build_launchctl.sh` gap found, NOT this session's regression

Doing the full downstream rebuild surfaced `build_launchctl.sh` failing
to link (`_fnmatch`/`_inet_pton`/`_lchflags`/`_sysctlnametomib`
undefined). Confirmed via `git status` that `build_launchctl.sh` itself
is untouched by this session, none of the 4 missing symbols relate to
anything this session touched, and `launchctl.macho` did not exist on
disk even before this session's changes — a real, pre-existing gap
(already tracked as DAR-132), not a regression. `tools/init_binary/
inject_into_sd_image.sh` never references `launchctl.macho` either, so
it isn't required for the boot-verification gate.

### What's left, prioritized

From the 2026-08-30 handoff's own table, still open:

- `libc++.dylib`/`libsystem_c.dylib` xlocale ownership (21+18 symbols) —
  still deferred, needs its own dedicated `ld64 -globalize_symbol`
  session (DAR-159).
- `libsystem_c.dylib`/`libsystem_m.dylib` math functions (9 symbols) —
  still deferred, needs ICU's own target dylibs relinked against
  `libsystem_m.dylib` directly (DAR-160).
- `libsystem_pthread.dylib`'s remaining 3 (`___thread_selfid`/
  `_sched_yield`/`_swtch_pri`, all vs `libxpc.dylib`) and
  `libsystem_malloc.dylib`'s remaining `___kill` (also vs `libxpc.
  dylib`): `libsystem_pthread.dylib`/`libsystem_malloc.dylib` are the
  correct real owners in every case — the fix belongs in `libxpc`'s own
  build script (drop its private copies, rely on the already-lower
  siblings). Natural first move for whoever picks up `libxpc.dylib`
  (22 dup, the largest still-fully-open dylib after `libsystem_c.dylib`/
  `libc++.dylib`).
- `libopenpam.dylib` (35 remaining), `libcopyfile.dylib` (13 remaining):
  both now almost entirely the same libc++/libsystem_c xlocale/time-
  function family as the bullet above — likely closes together with
  that dedicated session rather than needing its own.
  `libcopyfile.dylib` also has a few `libxpc.dylib`-only leftovers
  (`__xpc_runtime_is_app_sandboxed`).
- `libncurses.dylib` (12 remaining): `_select` (vs `libresolv.dylib`/
  `libxpc.dylib`) and the `___kill`/`_kill`/`_kill$UNIX2003` family (vs
  `libxpc.dylib`) are the same libxpc-shaped fix as above; the rest
  (`_abs`/`_access`/`_atoi`/`_atoi_l`/`_fpathconf`/`_mkdir`/`_nanosleep`/
  `_strstr`/`___darwin_check_fd_set_overflow`) weren't investigated.
- `libdyld.dylib` (28), `libxpc.dylib` (22), `libresolv.dylib` (18),
  `libsystem_notify.dylib` (3), `libdispatch.dylib` (1): not investigated
  this session at all.

## Session 2026-08-31 (continuation): audit-tool false-positive fix + libxpc/libdyld/libresolv/libsystem_notify sweep

Picked up from the 2026-08-31 (Phase 1e) handoff above. `dup_symbol_audit.sh --summary` totals:

| | dylib duplicates |
|---|---|
| start of this session | 136 |
| after `dup_symbol_audit.sh` false-positive fix (no dylib content changed) | 128 |
| end of this session | **102** |

### The most important finding this session: the audit tool itself had a false-positive bug

Investigating `libdyld.dylib`'s 28 flagged duplicates, 17 turned out not to be
duplicates at all. `nm -gUj` (what `dup_symbol_audit.sh`'s dylib-mode pass used)
cannot distinguish "this image really defines this symbol" from "this image's
export trie merely republishes a sibling's own symbol via `-Wl,-alias
realname,aliasname` where `realname` itself comes from a dylib on the link
line" -- both look like a plain external symbol to `nm`. Confirmed via
`xcrun dyld_info -exports`: entries like `_kill$UNIX2003 (___kill from
libsystem_malloc)` are real, valid `[re-export]`s at the SAME address as
the sibling's own copy, not a second independently-compiled definition.

Fixed `dup_symbol_audit.sh` itself: added `defined_syms_dylib_local()`,
which parses `dyld_info -exports` and keeps only lines with a real `0xADDR`
(excluding `[re-export]` lines), used in place of `nm -gUj` for the
dylib-mode pass only (binaries still use plain `nm -gUj` -- they don't
reexport). This dropped the total from 136 to 128 with **zero dylib
content changed** -- a pure measurement-accuracy fix. `libdyld.dylib`
alone went from 28 to 11 flagged duplicates from this one change.

**Important caveat, confirmed empirically, for whoever touches this next:**
`dyld_info -exports` only expands individual `-Wl,-alias`-created
reexports into visible `[re-export]` lines -- it does NOT expand a
wholesale `-Wl,-reexport_library` (e.g. `libSystem.B.dylib`'s reexport of
`libsystem_kernel.dylib`/`libsystem_c.dylib`/etc). Confirmed directly:
`dyld_info -exports libSystem.B.dylib` prints only 6 symbols, despite the
umbrella providing hundreds via real `LC_REEXPORT_DYLIB` entries -- real
dyld only flattens that chain at actual load time. An offline script
trying to verify "does binary X's undefined symbol Y resolve via its
loaded dylib's `dyld_info -exports` output" will report hundreds of false
`SYMBOL_NOT_EXPORTED` failures for anything routed through a wholesale
umbrella reexport (attempted this session while trying to spot-verify the
76 simple commands offline; recognized the false-positive pattern before
trusting it -- do not re-attempt this exact approach without first
replicating `dup_symbol_audit.sh`'s own binary-mode `collect_exports()`
recursive-`LC_REEXPORT_DYLIB`-walk, which is the one already-correct
implementation of this in the codebase).

### `libxpc.dylib`: 22 → 10 (two passes)

First pass (9 members removed from `libxpc_syscalls.a`/`libxpc_syscalls2.a`'s
wholesale extraction in `build_libxpc_dylib.sh`, all confirmed single-owner
via `nm -m` after):
- `dyldgen____thread_selfid.o`, `simple_sched_yield.o`/
  `simple_sched_yield_trap.o` (`___thread_selfid`/`_sched_yield`/
  `_swtch_pri`): real Apple's own `libsystem_pthread.dylib` API, already
  a linked sibling.
- `kill_wrap.o`/`sys____kill.o` (`_kill`/`___kill`): `libsystem_malloc.dylib`
  is the real canonical owner of `___kill`; plain `_kill` had no other
  consumer in this dylib at all.
- `memmove_chk.o`/`stpncpy_chk.o`/`strcpy_chk.o`/`strncpy_chk.o`: duplicated
  `libsystem_c.dylib`'s real `_FORTIFY_SOURCE` `_chk` tier.
- `time.o` (`_time`): duplicated `libsystem_c.dylib`'s real BSD `time()`.

Second pass (real BSD syscalls moved to their actual home,
`libsystem_kernel.dylib` -- see below): `sys__fileport_makefd.o`/
`sys__fileport_makeport.o` removed.

Remaining 10, not investigated: `__xpc_runtime_is_app_sandboxed`/
`_uuid_unparse` family (vs `libcopyfile.dylib`, ambiguous ownership --
neither side is obviously "real Apple's home"), `_connect`/`_socket`/
`_recvmsg`/`_select` (vs `libresolv.dylib`/`libopenpam.dylib`/
`libncurses.dylib` -- see "needs a new home" below), `___darwin_check_
fd_set_overflow` (vs `libncurses.dylib`).

### `libresolv.dylib`: 18 → 10

Real duplicate-**state** bug, this session's highest-priority fix:
`arc4random.o` is a complete, self-contained RC4 PRNG with real internal
state (`_rs`/`_rdat`/`_rs_stired`/`_arc4_count`/`_arc4_lock`, confirmed
`__DATA` storage via `nm -m`, not stateless code) -- `libsystem_c.dylib`
already has its own complete, independently-seeded copy of the exact same
algorithm. Two live PRNG states in one process, arguably worse than
`errno`/`g_zone`: reseeding via `arc4random_stir()`/`arc4random_
addrandom()` through one copy has zero effect on the other's output
stream. Removed `arc4random.o` from this dylib's own extract dir (NOT
from `libresolv.a` itself -- `dyld.macho`'s real static link still needs
it); `res_init.o`'s one real consumer (DNS-query-ID randomization) now
resolves to the single, real, canonical copy in `libsystem_c.dylib`
(already a linked sibling).

Also removed: `libresolv_getentropy_syscall.o` (duplicate raw syscall
trap) and `libresolv_arc4random_deps_compat.o` (its own header explained
it existed to give `arc4random.c`'s fallback path local `_open$UNIX2003`/
`_read$UNIX2003` forwarders under an assumption -- "nothing else in this
build calls plain read()/open() directly" -- that no longer held once
`libsystem_c.dylib` gained real `$UNIX2003` exports; confirmed via `nm -m`
that `_close$UNIX2003`, needed by the exact same `res_init`/`res_query`/
`res_send` consumers, was ALREADY resolving externally with zero local
copy, proving the other two didn't need one either).

Remaining 10: BSD socket syscalls (`_connect`/`_socket`/`_recvmsg`/
`_select`/`_send`/`_writev`, `_chmod`/`_chmod$UNIX2003`) split across
`libresolv`/`libopenpam`/`libxpc`/`libncurses`/`libcopyfile` with **no
dylib currently owning them from `libsystem_kernel.dylib`** -- a "needs a
new home" gap (see below), not a "wrong dylib has the right copy" fix;
`_atoi`/`_atoi_l` (vs `libncurses.dylib`/`libsystem_c.dylib`) not
investigated.

### `libsystem_kernel.dylib`/`libsystem_notify.dylib`: real syscalls moved to their actual home

`_fileport_makefd`/`_fileport_makeport` are real BSD syscalls (#430/#431,
`bsd/kern/syscalls.master`, confirmed by grep) whose real Apple home is
`libsystem_kernel.dylib` -- but this project only ever had them as private
stand-ins in `libnotify_client.a`'s `notify_fileport_syscalls.o` AND,
separately, `libxpc.dylib`'s own `sys__fileport_make{fd,port}.o` -- a
real 2-way duplicate with **neither side being the correct owner**. Added
both to `build_libsystem_kernel.sh`'s existing `create-syscalls.pl`
generator loop (same recipe as `geteuid`/`getegid`; confirmed the
generator really emits `_fileport_makefd.s`/`_fileport_makeport.s`, not
`sys_`-prefixed, by actually running it), removed both dylibs' private
copies. `libsystem_notify.dylib`: 3 → 1. `libxpc.dylib` (see above): the
other 2 of its fixes this session.

This is the general pattern for the "needs a new home" gaps flagged
above (`_connect`/`_socket`/`_recvmsg`/`_select`/`_send`/`_writev`/
`_chmod`) -- check `bsd/kern/syscalls.master` for the real syscall number,
add it to `build_libsystem_kernel.sh`'s generator loop, then remove every
sibling's private copy. Not attempted for the socket family this session
(larger surface, would touch `libresolv`/`libopenpam`/`libxpc`/
`libncurses`/`libcopyfile` simultaneously) -- flagged as the natural next
batch.

### Ticket-closing policy update, applied retroactively this session

Structurally-necessary duplicates (a dylib genuinely cannot link its
"real" owner back due to dependency-order/circularity, verified against
real Apple's architecture where applicable) are now closed with a
documented reason, not left open waiting for a fix that shouldn't happen.
Applied this session: `libsystem_malloc.dylib`'s `_memcpy`/`_memset` (DAR-13,
closed -- malloc is below `libsystem_c.dylib`, no sibling to fall back to,
pure code, no shared state) and `libunwind.dylib`'s `__dyld_find_unwind_
sections`/`__dyld_register_func_for_remove_image`/`_dladdr` (DAR-18,
closed -- `libunwind_dyld_shim.o` needs `__mh_execute_header`, never
synthesized for `MH_DYLIB`, AND `libc++.dylib` genuinely depends on
getting these 3 from `libunwind.dylib` as a real export). `libdyld.dylib`
(DAR-8) has a similar mix -- 8 of its 11 remaining symbols are now
documented as structural in a comment there, but the ticket stays open
because 2 (`___progname` real-state duplicate, `_dlclose` vs
`libopenpam.dylib`) are genuinely still-fixable gaps, not structural.

### Required final step: full `build_simple_cmds.sh` rebuild + its own limits

Per this session's own explicit instruction, ran a full rebuild of all 76
simple commands after the sweep (previously skipped per-library to save
time, per the same instruction) -- **76 built, 0 failed**. This is a real,
meaningful verification: `ld64`'s own real two-level-namespace symbol
resolution at link time is the same mechanism dyld uses at load time, so
zero link failures across all 76 against the fully-updated dylib set is
authoritative for "these will resolve correctly," not merely a build-succeeds
formality.

**What could NOT be verified this session, and why:** an actual interactive
QEMU spot-check (`tools/boot_test.py --login --cmd 'ls /' --cmd 'date' ...`)
was attempted and did not reach a shell prompt -- confirmed via the log's
own tail that the boot reached the exact same known-good checkpoint as
every other boot this session (`jnl: ... flushing fs disk buffer returned
0x2d`, zero panics, zero "Symbol not found"), i.e. this is the **same
pre-existing, already-tracked "userland does not yet reach an interactive
shell prompt from a cold boot" gap** noted in the 2026-08-30 session
write-up above (backlog item, `_dyld_fork_child`-hang class) -- not a
regression from this session's changes. Until that gap closes, the
simple-commands tier's real-world correctness rests on the link-time
verification above plus the fact that none of this session's symbol moves
touched anything the 76 commands' own build scripts needed to newly
resolve (confirmed by their own 0-failure rebuild) -- not a live,
interactive functional test. Flag this explicitly to any session that
assumes "boots clean" implies "every command verified interactively";
it does not, yet.

### Linear

DAR-9 (libxpc), DAR-11 (libresolv), DAR-19 (libsystem_notify) commented
with before/after + verification detail, left open (real remainder in
each). DAR-13 (libsystem_malloc), DAR-18 (libunwind) closed under the new
structural-necessity policy. DAR-8 (libdyld) commented distinguishing its
8 structural-and-effectively-done symbols from its 2 genuinely open ones,
left open.

## Session 2026-08-31 (continuation 2): BSD socket family moved to libsystem_kernel.dylib

`dup_symbol_audit.sh --summary` total: **102 → 98**, QEMU-verified.

Moved the whole `_chmod`/`_connect`/`_recvmsg`/`_select`/`_send`/`_socket`/
`_writev` family (plus real `$UNIX2003` aliases for chmod/connect/recvmsg/
writev) to its real canonical home, `libsystem_kernel.dylib`, reusing
`libxpc.dylib`'s own already-proven-and-QEMU-verified curated source
(`xpc_syscall_wrappers_compat.c`/`xpc_select_compat.c`, real Apple
`legacy/{connect,recvmsg,sendmsg}.c`/`select-base.c` bodies curated to
drop `_NONSTD_SOURCE` scaffolding this SDK target rejects) plus real
unmodified `wrappers/unix03/chmod.c` plus a new
`libsystem_kernel_sendto_compat.c`. Removed the now-redundant private
copies from `libresolv.dylib` (via a new `LIBRESOLV_SOCKET_SYSCALLS_
DYLIB_VARIANT` compile guard on `libresolv_socket_syscalls.c`/
`libresolv_more_socket_syscalls.c` -- static-archive consumers of
`libresolv.a`, incl. `dyld.macho`, keep every one), `libxpc.dylib` (its
own copies were literally the same source, now doubly redundant --
also had to remove the now-dead raw `_nocancel` trap objects those
wrappers called, a real gap the first audit pass after the fix missed
since the audit only flags *duplicated* symbols and these were briefly
unique-but-dead before removal), `libcopyfile.dylib` (`chmod()` only,
same guard-macro name reused), and `libncurses.dylib` (`_select`,
narrowed alongside the 4 symbols already narrowed there in the
2026-08-30 session).

**Real ld64 "-alias nonlocal base" landmine hit twice this batch,
avoided both times**: `sendto()`/`connect()`/`recvmsg()`/`writev()`/
`chmod()` in `libresolv.dylib`/`libcopyfile.dylib` each have (or had) a
local `-Wl,-alias` creating a `$UNIX2003`-suffixed public name. Moving
the *base* function's ownership to `libsystem_kernel.dylib` without
also either (a) keeping a local, now-private-extern copy specifically
for alias purposes (`sendto`'s case: kept local+hidden in both
libsystem_kernel.dylib and libresolv.dylib, since libresolv.dylib's own
`_sendto$UNIX2003` alias needed to keep its real, only, local base) or
(b) moving the alias creation itself to libsystem_kernel.dylib's own
link too (chmod/connect/recvmsg/writev's case, since nothing needed
libresolv/libcopyfile to keep being the alias's home) would have
silently produced the malformed-ordinal-0-re-export bug on the very
first rebuild. Caught the `_connect$UNIX2003` case via a real "link
found real undefined symbols" build failure (a stale libsystem_kernel.
dylib on disk, rebuilt before the alias was added) -- not by reasoning
it out in advance the first time; flagging so the next mover of a
`-Wl,-alias` base checks EVERY dylib that creates an alias off it, not
just the ones already suspected.

**Consciously NOT touched**: `libopenpam.dylib`'s own `_connect`/
`_socket`/`_send`/`_writev` (`cat_socket_stub.c`/`date_compat.c`) are
honest, documented ENOSYS stubs -- load-bearing behavior (their real
job is making `vsyslog()` take its designed LOG_CONS fallback instead
of trying a real socket that has nothing listening on it), not a
duplicate-symbol oversight. Redirecting them to `libsystem_kernel.
dylib`'s now-real implementations would be a real behavior change, out
of scope for this pass. This reopened `libsystem_kernel.dylib`'s own
Linear ticket (DAR-14) at 4/257 -- all 4 in this same accepted
category, re-closed with that reasoning recorded rather than treated as
regression.

Linear: DAR-11 (libresolv, 10→2), DAR-9 (libxpc, 10→6), DAR-10
(libncurses, 11→10), DAR-12 (libcopyfile, 13→11) commented with
before/after, left open. DAR-14 (libsystem_kernel) commented + re-closed
with the openpam-exception reasoning above.

Session paused here per explicit request, before starting the next two
prioritized items (libcopyfile-vs-libxpc `_uuid_unparse`/`__xpc_runtime_
is_app_sandboxed` ownership, and the `___progname` real-duplicate-state
investigation) -- both still open, next in line.

## Session 2026-08-31 (continuation 3): ___progname single-owner fix + libcopyfile/libxpc uuid consolidation

`dup_symbol_audit.sh --summary` total: **98 → 94**, QEMU-verified. Picked
up the two items the previous session flagged as next in line.

### `___progname`: three real storage locations found, two removed

Full investigation, not a guess: `nm -m` found real definitions in
`libdyld.dylib` (`libdyldGlue.o`), `libSystem.B.dylib`
(`libsystem_progname_compat.c`, project-authored), and
`third_party/Csu/crt.c` (compiled directly into every binary, dynamic
or static). Read real, unmodified `third_party/dyld/dyld/dyldMain.cpp`
source to settle ownership: modern dyld's actual mechanism is
`libdyld4Section->defaultVars`, populated from libdyld's own real
`libdyldGlue.cpp` (`const char* __progname = NULL;` + the
`state.vars->__prognamePtr` wiring) -- `libdyld.dylib` is both the real
Apple-canonical owner AND structurally guaranteed to always be co-loaded
(dyldMain.cpp itself `halt()`s if it's missing). Confirmed via
`xcrun dyld_info -fixups` that `libsystem_c.dylib`'s own
`getprogname()`/`setprogname()` already resolve `___progname` via a real
ordinal bind to `libdyld.dylib` specifically (`bind libdyld/___progname`,
not `<flat-namespace>`) -- i.e. every actual caller already, observably,
used libdyld.dylib's copy; `libSystem.B.dylib`'s copy was confirmed pure
dead storage (grepped every object it compiles, neither reads nor writes
it).

Fix: removed `libsystem_progname_compat.c`'s compile step + link-line
entry from `build_libsystem.sh`. `libSystem.B.dylib` no longer defines
`___progname` (`xcrun dyld_info -exports` confirms).
`third_party/Csu/crt.c`'s own per-executable copy is a separate,
Phase-2/binary-mode duplicate (embedded in every dynamically-linked
binary's own link, not a dylib) -- flagged, explicitly NOT touched this
pass.

### `_uuid_unparse`/`_uuid_unparse_lower`/`_uuid_unparse_upper`: real Apple ownership resolved

Neither `libcopyfile.dylib` nor `libxpc.dylib` was real Apple's actual
home -- both independently compiled/extracted the same real
`third_party/Libc/uuid/uuidsrc/{copy,parse,unparse,pack}.c` (genuine
apple-oss-distributions Libc source, i.e. real Apple ships this as part
of `libsystem_c.dylib`). Extracted the same already-built objects out of
`libcopyfile.a` into `build_libsystem_c.sh` (same "pull a verified object
from a sibling archive" pattern already used there for
`localtime.o`/`chk_fail_compat.o`); dropped `copy.o`/`parse.o`/
`unparse.o`/`pack.o` from `build_copyfile_dylib.sh`'s own extraction and
`uuid_unparse.o` from `build_libxpc_dylib.sh`'s (neither archive itself
touched -- `dyld.macho`'s real static link still needs both intact).

**Real, pre-existing chained-fixups bug found during verification, not
introduced this session**: `launchd.macho` (already built, from a prior
session) had a real bind recorded as `libxpc/_uuid_unparse`
(`xcrun dyld_info -fixups`) -- exactly the "consumer bakes in a specific
owning-dylib ordinal at ITS OWN link time" class of bug this doc already
documents (the `___error`/libxpc.dylib incident, 2026-08-30 session).
Swept every built `.macho` (`libc_build/*.macho` + all top-level simple
commands) for a bind to `progname`/`uuid_unparse`/`app_sandboxed` before
declaring done -- only `launchd.macho` had a real bind at risk (the 76
simple commands only ever bind `getprogname()`/`setprogname()` to
`libsystem_c`, untouched by this session). Relinked `launchd.macho`,
`syslogd.macho`, `pd_console_login.macho` (cheap, only 3 binaries, not
part of the excluded 76); `launchd.macho` now correctly binds
`libsystem_c/_uuid_unparse`. `syslogd.macho`'s own `dyld_info -fixups`
output still LABELS its bind as `libdispatch/_uuid_unparse` even after a
clean relink with 0 undefined symbols -- confirmed `libdispatch.dylib`
genuinely has no uuid symbols at all, so this is a `dyld_info` ordinal-
name display quirk (matching this doc's other already-documented
`dyld_info` blind spots), not a real unresolved bind; ld64's own "0
undefined symbols" result at link time is authoritative.

### `__xpc_runtime_is_app_sandboxed`: investigated, left as a documented structural exception

Both `libcopyfile.dylib`'s project-authored `copyfile_xpc_shim.c` and
`libxpc.dylib`'s real `xpc_private.o` (compiled from
`third_party/PureDarwin/src/Libraries/XPC/libxpc/xpc_private.c`) define
this as an unconditional `return 0;` -- confirmed identical behavior,
and confirmed PureDarwin's own real reimplementation independently
arrives at the same `return 0;` (real Apple never open-sourced the
sandbox-aware version, so this project's stand-in already matches the
best available real reference). NOT consolidated: `libcopyfile.dylib` is
reexported wholesale by `libSystem.B.dylib`, which every one of this
project's ~70 simple commands links -- adding `libxpc.dylib` as a new
sibling dependency there would force real dyld to load the entire
libxpc/libdispatch dependency graph into every simple command at
startup, not just XPC-using ones (launchd/syslogd), a real architecture
change wildly disproportionate to deduplicating a one-line, zero-shared-
state stub. Documented in place (`copyfile_xpc_shim.c`'s own header
comment rewritten -- it previously said "this project has no XPC/sandbox
subsystem at all, no dyld", stale since Phase 6.5) as a conscious
structural exception, same shape as `libsystem_malloc.dylib`'s memcpy/
memset (DAR-13) and `libunwind.dylib`'s dyld-unwind trio (DAR-18).

### Real QEMU verification

Rebuilt bottom-up: `libsystem_c.dylib` → `libSystem.B.dylib` →
`libcopyfile.dylib`/`libxpc.dylib` → `launchd.macho`/`syslogd.macho`/
`pd_console_login.macho` → `tools/init_binary/inject_into_sd_image.sh`.
`tools/boot_test.py` run against the real SD image; diffed the resulting
log against the last known-good boot from immediately before this
session (`qemu/static_link_batch6_socket_family_boot.log`, end of the
previous session) with ASLR-varying hex addresses stripped -- identical
apart from expected QEMU/TCG scheduling-order jitter (a few
`_IOConfigThread::main entered` lines reordered, page-count deltas of
~1). Zero panics, zero "Symbol not found". Note: a naive
`--expect-fail 'panic'` regex on the raw log matches a benign,
pre-existing diagnostic field name (`DIAG: first_panic latched=...`,
part of a real, already-existing abort-diagnostic dump for an expected
failed load attempt of `/usr/appleinternal/sbin/launchd.development`
before falling back to the real `/sbin/launchd` -- present byte-for-byte
in the prior session's own known-good log too) -- confirmed via a direct
diff against that reference log before trusting the naive regex match,
not after; flagging so a future session's own boot-test invocation
doesn't mistake this specific benign string for a real panic.

### Pre-existing gap found and baselined, not fixed (out of scope this session)

Running the FULL `dup_symbol_audit.sh` (not just `--summary`) surfaced a
real, pre-existing binary-mode FAIL: `___chmod` shadowed locally in all
~70 simple commands (their own `sys____chmod.o`, part of the long-
standing `LINK_COMMON` raw-syscall tier) vs `libsystem_kernel.dylib`'s
own real `___chmod` (added to `libsystem_kernel.dylib` in the PRIOR,
already-committed "BSD socket family" session, `6ba2b5e`). Confirmed via
file timestamps that every affected `.macho` predates this session
entirely and `build_simple_cmds.sh` is untouched -- not a regression
introduced here, just never previously baselined (same shape as the
`_getegit`-vs-`libsystem_kernel.dylib` residual noted in the 2026-08-30
session). Left unfixed per this sweep's own rule 7 (simple-commands
rebuild happens once at the very end of the whole sweep); baselined via
`--update-baseline` alongside this session's real fixes so it doesn't
block future runs, but it is real, outstanding debt for whoever does the
final `build_simple_cmds.sh` pass.

### Linear

DAR-8 (libdyld, `___progname` fixed, `_dlclose` still open), DAR-9
(libxpc, 6→3), DAR-12 (libcopyfile, 11→8) all commented with full detail
and left open (each has a real remainder). No ticket fully cleared this
session, so none self-closed.

## Session 2026-08-31 (continuation 4): libSystem.B.dylib/libsystem_notify.dylib now-stale pre-dyld stubs replaced with libdyld.dylib's real ones

`dup_symbol_audit.sh --summary` total: **94 → 91**, QEMU-verified.

Picked a coherent theme not yet swept: symbols that were duplicated
because a sibling dylib hand-rolled its own honest stub "back when this
project had no dyld at all" -- reasoning that Phase 6.5's real dyld has
since made stale in at least two concrete, fixable cases (a third,
`libcxx_dlfcn_stub.c`'s `dlopen`/`dlsym`, turned out to still be
structurally necessary once actually checked -- see below).

### `libSystem.B.dylib`'s `_malloc_size`/`_tlv_get_addr`: pure dead duplication, not a second consumer

`build_libsystem.sh` linked `$OUT/libdyld/compat_misc.o` (`libdyld_
compat_misc.c` -- two `libdyld.dylib`-authored stub functions, both
unconditional `return 0`/`NULL`, reused "same combination already
needed for libdyld.dylib" per the script's own old comment) into
`libSystem.B.dylib`'s own link. Checked whether libSystem.B.dylib
actually needed either symbol via `nm -u` on its own one project-
authored object (`dyld_initializer_compat.o`): it does not reference
`malloc_size` or `tlv_get_addr` at all. Only `libdyld.dylib`'s own
`LibSystemHelpers.o` (real, unmodified Apple dyld source) does, and that
object is never compiled into libSystem.B.dylib. The copy here was
therefore pure, unreferenced duplication -- not a second legitimate
consumer needing its own copy the way DAR-13's memcpy/memset genuinely
is. Fix: dropped the dependency check + link-line entry entirely.
`libSystem.B.dylib` now has **zero** local exports -- a pure reexport
umbrella, matching real Apple's own libSystem.dylib architecture even
more closely than before this fix.

### `libsystem_notify.dylib`'s `__dyld_is_memory_immutable`: real dyld now exists, so link it instead of hand-rolling it

`notify_dyld_compat.c` (`libsystem_notify.dylib`, compiled as part of
`libnotify_client.a`) always returned `false` from
`_dyld_is_memory_immutable()`, justified by its own header comment as
"this project's fully-static (no dyld) build". Stale since Phase 6.5:
this project's real `libdyld.dylib` genuinely implements this function
for real (from real, unmodified `libdyldGlue.cpp` Apple source).
Confirmed via `otool -L` that `libdyld.dylib` does not depend on
`libsystem_notify.dylib` (no cycle), so added `libdyld.dylib` as a real
sibling link dependency in `build_libsystem_notify.sh` and excluded
`notify_dyld_compat.o` from this dylib's own `ar x` extraction -- same
treatment `notify_fileport_syscalls.o` already got in an earlier
session, left untouched inside `libnotify_client.a` itself for whatever
other still-static consumer needs it. Real, minor correctness
improvement, not just symbol hygiene: `_notify_strdup_if_mutable()`'s
one call site now gets the real answer instead of always taking the
non-optimal "assume mutable" path.

### `libc++.dylib`'s `_dlopen`/`_dlsym`: investigated, confirmed genuinely structural (not fixed)

Same "no dyld at all" stale-reasoning shape as the notify fix above, but
here the real, current reason turned out to still hold: `libcxx_dlfcn_
stub.c`'s comment was updated to state it correctly, but the code itself
was not changed. Confirmed via `otool -L` that `libdyld.dylib` already
links `libc++.dylib` directly (a real dependency, for C++ runtime
support) -- so `libc++.dylib` linking `libdyld.dylib` back to get its
real `dlopen`/`dlsym` would be a genuine mutual-dependency cycle between
the two dylibs, the same shape already established for `libunwind.
dylib`'s dyld-unwind trio (DAR-18) and `libsystem_malloc.dylib`'s
memcpy/memset (DAR-13). Left as a documented structural duplicate, not
touched further.

### `libdyld.dylib`'s `_vproc_swap_integer` vs `libxpc.dylib`: investigated, deliberately deferred (real behavioral risk, not structural)

`libxpc.dylib`'s copy (`libvproc.c`, real PureDarwin source) is a full,
real Mach-MIG implementation that talks to a real launchd via
`bootstrap_port`; `libdyld.dylib`'s own copy (`libdyld_vproc_compat.c`)
is an honest "never launchd-managed" stub. Unlike the three cases above,
no link cycle blocks consolidating -- `libdyld.dylib` doesn't currently
depend on `libxpc.dylib` and technically could. Not fixed anyway: doing
so would mean dyld's own early process-bootstrap path starts making real
Mach IPC calls toward a bootstrap-port-owning launchd that may not be up
yet this early in launch on this launchd-less-at-boot project -- a real
behavioral-risk question, not a mechanical symbol move, and out of scope
for a duplicate-symbol batch. Flagged for a dedicated session with its
own QEMU verification, same treatment as `libdyld.dylib`'s other real
remainder, `_dlclose` (vs `libopenpam.dylib`, still open, still not
attempted).

### Real QEMU verification

Rebuilt only the two dylibs actually changed (`libSystem.B.dylib`,
`libsystem_notify.dylib` -- `libdyld.dylib` itself was not touched, it
was already the correct real owner in both fixed cases). Swept every
built `.macho` via `xcrun dyld_info -fixups` for a stale bind to
`libSystem.B/_malloc_size`, `libSystem.B/_tlv_get_addr`, or
`libsystem_notify/__dyld_is_memory_immutable` before declaring done --
zero hits, confirming nothing ever actually consumed these symbols from
these specific dylibs (consistent with both being pure, never-called
duplication). No binary needed relinking. Ran `tools/init_binary/
inject_into_sd_image.sh` to refresh the SD image's dylibs, then `tools/
boot_test.py` (log `qemu/static_link_batch7_dyld_helpers_boot.log`).
Diffed against the prior session's own known-good log
(`static_link_batch6_socket_family_boot.log`) with hex addresses
stripped: identical apart from expected QEMU/TCG scheduling-order
jitter (reordered `_IOConfigThread::main entered` lines, a couple-page
vm_page_bootstrap count delta) -- confirmed via a direct grep for
error/fail/panic/"not found"/abort across both sorted logs, zero new
hits. Zero panics (checked past the benign, already-documented
`DIAG: first_panic latched=...` string), zero "Symbol not found".

### Linear

DAR-17 (libSystem.B.dylib, 6→0) and DAR-19 (libsystem_notify.dylib,
3→0, the last 1 of which was this session's fix) both fully cleared and
self-closed with full before/after detail. DAR-8 (libdyld.dylib, 31→7
since ticket creation) commented with the current, fully re-categorized
7: 3 already-structural (DAR-18), 2 newly-confirmed-structural this
session (`_dlopen`/`_dlsym`), and 2 real, still-open, deliberately
deferred remainders (`_dlclose`, `_vproc_swap_integer`) -- left open.

Baseline updated (`dup_symbol_audit.sh --update-baseline`) to lock in
94→91.

## Session 2026-08-31 (continuation 5): `_dlclose` fixed -- libopenpam.dylib now links libdyld.dylib

`dup_symbol_audit.sh --summary` total: **91 → 90**, QEMU-verified. Picked
up the one real, tractable remainder this doc's own prior session flagged
in DAR-8 (`_dlclose` vs `libopenpam.dylib`) -- `_vproc_swap_integer`
stays deliberately deferred, unchanged, per that same session's own real-
behavioral-risk reasoning.

### `_dlclose`: real dead-code stub retired in favor of the real dyld owner

`libopenpam.dylib`'s `openpam_dynamic_compat.c` (this project's own honest
replacement for OpenPAM's real dlopen-based module loader, which is
structurally impossible here -- see that file's own header) defines a
`dlclose()` stub that always returns `-1`. Confirmed genuinely
**unreachable dead code**, not merely unused: `openpam_load.c`'s only
call site (`openpam_release_module()`) is guarded by
`if (module->dlh == NULL) return;` immediately above it, and this
build's `openpam_dynamic()` never returns a module with a non-NULL `dlh`
(`OPENPAM_STATIC_MODULES` is undefined, matching Apple's own real Xcode
config). The stub exists purely to satisfy the linker for source that
can never actually execute that branch.

Real Apple's actual owner is dyld itself: `third_party/dyld/dyld/
DyldAPIs.cpp`'s `APIs::dlclose` (real, unmodified source), already
compiled into this project's own `libdyld.dylib` as of Phase 6.5. Fix:
added `libdyld.dylib` as a real sibling link to
`build_openpam_dylib.sh` (both the dependency-file check and the final
link line), and gated `openpam_dynamic_compat.c`'s `dlclose()` definition
behind a new `OPENPAM_DYNAMIC_COMPAT_DYLIB_VARIANT` compile guard --
same shape as this project's existing `REMOVEFILE_SYSCALLS_DYLIB`/
`LIBMALLOC_DISPATCH_DYLIB_VARIANT` guarded-recompile pattern (recompile
the one source file fresh for the dylib build with the guard defined,
replacing the `ar x`-extracted object; the static archive build,
`build_openpam.sh`, is untouched since `dyld.macho` -- which statically
links `libopenpam.a` and runs before any dylib including `libdyld.dylib`
exists -- still needs its own local copy).

**Confirmed no link cycle before making the change, not after**:
`otool -L libdyld.dylib` does not list `libopenpam.dylib` among its
dependents. **Confirmed zero new dylib enters any process's load set
either**: grepped every built `.macho`'s `otool -L` output for
`libopenpam` -- only `syslogd.macho` loads it, and `syslogd.macho`
already loads `libdyld.dylib` directly regardless (both were already
independently present in that one process).

Verified single-owner via `nm -m libopenpam.dylib` (no local `_dlclose`
definition remains) and `xcrun dyld_info -fixups libopenpam.dylib` (real
`bind libdyld/_dlclose`, not `<flat-namespace>`). Swept every built
`.macho`'s own `dyld_info -fixups` output for a stale bind to
`libopenpam/_dlclose` specifically (the exact `___error`/`_uuid_unparse`-
class chained-fixups gotcha this doc already documents twice) --
`launchd.macho` is the only binary that references `_dlclose` at all, and
it already bound to `libdyld` before this change (it never linked
`libopenpam.dylib` to begin with), so no relink was required anywhere.

### Real QEMU verification

Full downstream refresh: rebuilt `libopenpam.a` (unchanged, confirmed via
`git stash` that its own pre-existing smoketest-link failure -- a real,
unrelated `_mmap` gap in `libpthread.a`'s `pthread_main_thread_bootstrap.o`
-- reproduces identically on a clean checkout, not a regression) then
`libopenpam.dylib`, then `tools/init_binary/inject_into_sd_image.sh`
(refreshes the real SD image's dylibs + all 76 simple commands + `/sbin/
launchd`/`pwtool`/`/etc`). Ran `tools/boot_test.py`
(`qemu/static_link_batch8_dlclose_boot.log`), diffed address-stripped
against the last known-good log (`static_link_batch7_dyld_helpers_boot.log`,
end of the immediately-prior session): tails are **byte-for-byte
identical**, including the benign, already-documented
`DIAG: first_panic latched=125042432 ...` diagnostic string; the only
diff lines in between are expected QEMU/TCG thread-scheduling-order
jitter (`TEMP DIAGNOSTIC` lines reordered, single-digit
`vm_page_bootstrap` free/wired page-count deltas), the same shape every
prior session's boot diff has shown. Zero panics, zero "Symbol not
found".

### Linear

DAR-8 (libdyld.dylib) commented: 7→6, `_dlclose` fixed,
`_vproc_swap_integer` the sole remaining real, deliberately-deferred
symbol -- left open. DAR-7 (libopenpam.dylib) commented: 35→34, same
fix, left open (34 remaining are the already-documented xlocale/time-
function family plus accepted ENOSYS-stub exceptions).

Baseline updated (`dup_symbol_audit.sh --update-baseline`) to lock in
91→90.

## Session 2026-08-31 (continuation 6): libopenpam.dylib's time/gettimeofday/fenv duplicates -- + a real, structural wall found and documented

`dup_symbol_audit.sh --summary` total: **90 → 84**, QEMU-verified. Picked
the next-highest-value item off the summary table: `libsystem_c.dylib`
(70 dup) and `libc++.dylib` (51 dup) were the two largest offenders, but
their overlap is almost entirely the already-deferred xlocale/math
families (docs' own "do not touch" list). `libopenpam.dylib` (34 dup)
was the next largest with real, un-investigated remainder.

### What was fixed: `build_openpam_dylib.sh`'s wholesale `ar x libopenpam.a` never got narrowed

`build_openpam_dylib.sh` extracts every member of `libopenpam.a` with a
single unfiltered `ar x` (unlike every other dylib script's selective
extraction). `build_libsystem_c.sh` separately extracts several of the
same members from that same archive on purpose (its own "Real gap: ICU
also references time/mktime, expected from libSystem" comments) -- but
nobody ever went back to `build_openpam_dylib.sh` to drop its now-
redundant copies once `libsystem_c.dylib` became the real owner. Fixed:
deleted 5 re-extracted objects from `libopenpam.dylib`'s own extract dir
right after the `ar x` -- `time.o` (`_time`), `gettimeofday.o`
(`_gettimeofday`), `ls_fenv_compat.o` (`_fegetenv`/`_fesetenv`),
`ps_commpage_gettimeofday_stub.o` (`___commpage_gettimeofday`),
`simple_cmds_time_compat.o`. The last one turned out to be a bonus sixth
fix: it defines `___gettimeofday`, which was ALSO a genuine public
duplicate with `libsystem_c.dylib`'s own copy (not the already-documented
`___gettimeofday_with_mach`-adjacent private-extern exception -- that one
lives in a different file entirely; confirmed via `nm -m` this specific
copy had no reason to stay public). `libopenpam.dylib` already links
`libsystem_c.dylib` as a sibling, so all 6 symbols' undefined references
now resolve there. Verified via a real trial link (0 undefined symbols,
only the pre-existing documented `-Wl,-U` deferrals in the "declared"
list) and confirmed `syslogd.macho` (the only binary that loads
`libopenpam.dylib`) has no stale `dyld_info -fixups` bind to any of the 6
symbols under the `libopenpam/` ordinal (it was already binding `_time`/
`_gettimeofday` to `libdispatch.dylib`, a third, separate, untouched
owner -- confirms no relink was needed anywhere for this fix).

### What was investigated and found genuinely structural, not fixed: the tzset/localtime/mktime/tzname/daylight family

The obvious next, higher-value target: `localtime.o` (defines
`tzset`/`tzsetwall`/`localtime`/`localtime_r`/`gmtime`/`gmtime_r`/
`ctime`/`ctime_r`/`offtime`/`mktime`/`timegm`/`timeoff`/`time2posix`/
`posix2time`/`timelocal`, PLUS real mutable state `_tzname`/`_daylight`/
`_lcl_rwlock`/`___darwin_altzone`) and `timezone_unix03.o`
(`_timezone`/`__st_get_timezone`/`__st_set_timezone`) are each
independently compiled/extracted THREE times across `libsystem_c.dylib`
(from `liblaunchd_core.a`, deliberately built without
`-D__DARWIN_UNIX03=1` for ICU's sake -- see `build_launchd_core.sh`'s own
comment), `libopenpam.dylib` (wholesale `ar x`, UNIX03-enabled compile),
and `libc++.dylib` (its own fresh compile, also UNIX03-enabled). Real
duplicate STATE (`_tzname`/`_daylight`) made this look like the exact
same bug class as the already-fixed `errno`/`mach_task_self_` cases, and
the fact that ONLY `mktime` differs symbol-name-wise between the two
UNIX03 configurations (confirmed via real
`third_party/Libc/include/time.h`: only `mktime`/`timezone`/`clock`/
`strftime`/`strptime` get `__DARWIN_ALIAS` treatment -- `tzset`/
`localtime`/`gmtime`/`ctime`/`tzname`/`daylight` do not) meant a single
`-Wl,-alias,_mktime,'_mktime$UNIX2003'` on `libsystem_c.dylib` looked
like it would be enough to let both siblings drop their local copies
entirely.

Real trial removal (both from `libc++.dylib`'s compile loop and
`libopenpam.dylib`'s extraction) broke the link identically in both
places: `Undefined symbols ... "__st_get_timezone", referenced from
__st_fmt in strftime.o` (and, once that was worked around, `
"___darwin_altzone", referenced from __st_fmt in strftime.o`). Checked
via `nm -m`, not assumed: both `__st_get_timezone`/`__st_set_timezone`
(in `timezone_unix03.o`) and `___darwin_altzone` (in `localtime.o`) are
real Apple `.private_extern` symbols in the actual source. Private
externs cannot resolve across a dylib boundary by construction -- so
`strftime.o` (kept local in both siblings; see the asctime paragraph
below for why) structurally REQUIRES its own sibling copies of both
objects to stay in the same image as itself. Deleting them the way the 5
objects above were deleted is not an option.

The tempting workaround -- keep the objects linked locally but narrow
just their PUBLIC exports (`tzset`/`tzname`/`daylight`/etc, via the same
`ld -r -keep_private_externs -unexported_symbol` trick
`narrow_rune_locale.sh` already established for the rune-locale globals)
-- was deliberately rejected, not just untried: that trick is only
correct for symbols that are REALLY `.private_extern` in Apple's own
source (verified via `nm -m` showing "was a private external"), used as
a tool to stop them being SEEN as false dylib-boundary conflicts. Using
it here on symbols that are genuinely, intentionally public
(`_tzname`/`_daylight` are real exported globals in Apple's own
`libSystem`) would not fix the duplicate-state bug, it would HIDE it --
the object would still be resident and linkable in the same image, so
any as-yet-unaudited internal caller in that same dylib link (e.g. if
`libc++.dylib`'s own `locale.cpp`/`time_get`/`time_put` facets ever call
`tzset()`/`localtime()` directly rather than only `strftime_l()`) would
silently keep resolving to the now-invisible local copy instead of
`libsystem_c.dylib`'s real one, and `dup_symbol_audit.sh` would no longer
be able to catch it. Chose to leave the real duplicate visible and
documented over trading it for an invisible one -- a real regression
risk for a cosmetic count reduction, the kind of trade this sweep's own
rules (rule 6: only accept a duplicate as deliberate when consolidating
would be a real behavior regression) exist to prevent taking casually.

Flagged for a dedicated future session, same treatment as `libdyld.dylib`'s
`_vproc_swap_integer` (DAR-8): the real fix needs either routing
`%z`/`%Z` timezone formatting through `libsystem_c.dylib`'s own
externally-callable timezone API instead of these two private helpers,
or a deeper source-level restructure of how `strftime.c`'s timezone
lookup works in this project -- not a mechanical symbol move.

### A related, pre-existing exception reconfirmed, not newly created

`_asctime`/`_asctime_r` duplication between `libsystem_c.dylib` and both
`libc++.dylib`/`libopenpam.dylib` is NOT the same bug shape as the
tzset/localtime family above, and was deliberately left alone for a
different reason: `libsystem_c.dylib`'s own `_asctime`/`_asctime_r` is a
project-authored, deliberately temporary stand-in
(`libsystem_c_gap_compat.c`, blocked on the real `strftime_l()`/xlocale
chain -- matches this project's own prior memory note "asctime hand-impl
is deliberately temporary"), while both siblings' copies are the real,
unmodified FreeBSD `stdtime/FreeBSD/asctime.c`. Consolidating onto
`libsystem_c.dylib` today would silently downgrade every libc++/openpam
consumer to the inferior stub -- a real behavior regression, not a
symbol-hygiene win. Documented in place in both `build_openpam_dylib.sh`
and `build_libcxx_dylib.sh` (this session touched neither file's asctime
handling, just added the comment recording why).

### Real QEMU verification

Rebuilt bottom-up: `libsystem_c.dylib` (rebuilt for parity, unchanged),
`libSystem.B.dylib`, `libc++.dylib` (rebuilt for parity, unchanged),
`libopenpam.a` (pre-existing, already-documented `_mmap`-in-
`pthread_main_thread_bootstrap.o` smoketest-link failure reproduced
identically, not a regression -- confirmed the same way the
continuation-5 session did), `libopenpam.dylib`. Ran
`tools/init_binary/inject_into_sd_image.sh` then `tools/boot_test.py`
(`qemu/static_link_batch9_openpam_time_boot.log`, PASS, 191s). Diffed
address-stripped against the last known-good log
(`static_link_batch8_dlclose_boot.log`): only expected QEMU/TCG
scheduling-order jitter (`TEMP DIAGNOSTIC: _IOConfigThread::main
entered`/`registerService`/`configThread` lines reordered around the USB
and SD driver probes, a ~2ms `pmap_startup()` timing delta) -- confirmed
via `grep -a` (the log's own `debug_buf content:` diagnostic line
embeds raw non-UTF8 bytes that make plain `grep`/`sed` silently report 0
matches or error out; `-a` forces text mode) for
`panic`/`Symbol not found`/`undefined`/`fatal`: zero real hits, only the
same benign, already-documented `DIAG: first_panic latched=125042432 ...`
diagnostic string present byte-for-byte in the prior session's log too.

### Linear

DAR-7 (libopenpam.dylib, 34→28) and DAR-5 (libsystem_c.dylib, 70→64)
both commented with the full fix + the structural-wall investigation
writeup, left open (neither is fully cleared -- DAR-7 still has the
tzset/localtime family, asctime exception, and prior sessions'
xlocale/ENOSYS categories; DAR-5 still has its own two big documented
remainders, xlocale and math, untouched this session).

Baseline updated (`dup_symbol_audit.sh --update-baseline`) to lock in
90→84.

## Session 2026-08-31 (continuation 7): leftover plain-libc duplicates in libncurses/libresolv/libopenpam/libcopyfile, + a real regression caught and fixed

`dup_symbol_audit.sh --summary` total: **84 → 76**, QEMU-verified. Picked
the next coherent, safe batch: pure-code plain-libc functions that
several leaf dylibs still bundle their own copy of despite
libsystem_c.dylib already being the real, established, single canonical
owner and an already-linked sibling in every case. Identified by
computing real dylib ownership directly (`xcrun dyld_info -exports`,
the same mechanism `dup_symbol_audit.sh` itself uses) rather than
re-running the full audit's slow binary-scan pass per symbol.

### Fixed (all mechanical single-owner removals, verified via `nm -m`)

- **`libncurses.dylib`** (10→4): removed `______third_party_Libc_stdlib_
  FreeBSD_atoi_c.o` (`_atoi`/`_atoi_l`) and `..._abs_c.o` (`_abs`)
  entirely from the extract dir -- each defines only that symbol, real
  other in-archive consumers (`lib_setup.o`/`lib_termcap.o`,
  `hashmap.o`/`lib_mvcur.o`) already held undefined references, now
  resolved externally. Added `_access`/`_mkdir`/`_strstr` to the
  existing `ncurses_extra_syscalls.o` narrowing list (same
  `ld -r -keep_private_externs -unexported_symbol` technique already
  used there) since they share that object file with syscalls this
  dylib still needs to keep public.
- **`libresolv.dylib`** (2→0, DAR-11 closed): removed `atoi.o` entirely
  (real, unmodified Libc/stdlib/FreeBSD/atoi.c) -- its one real
  in-archive consumer, `res_init.o`, already held an undefined
  reference.
- **`libopenpam.dylib`** (28→27): removed `openpam_syscalls_compat.o`
  (`_access` only) -- this project-authored file's own header already
  explained it exists solely for `libopenpam.a`'s static-archive
  smoketest (no sibling dylib to resolve `access()` from there); the
  dylib build already links libsystem_c.dylib as a sibling, so this
  copy was pure redundancy specific to the dylib link. Not removed from
  `libopenpam.a` itself.
- **`libcopyfile.dylib`** (8→4): removed `ftruncate.o` entirely (defines
  only `_ftruncate`, one real consumer `copyfile.o` already undefined).
  Removed `strtoul.o` entirely (`_strtoul`/`_strtoul_l`) -- confirmed a
  genuine THIRD independently-compiled copy (libc++.dylib/
  libsystem_c.dylib already duplicate these two under the deferred
  xlocale umbrella, DAR-159) with no remaining in-archive consumer
  (`parse.o`, the one real reference, was already dropped from this
  dylib's own extract dir in an earlier session for an unrelated
  uuid-ownership reason) -- doesn't close the audit-flagged duplicate
  (2 owners still remain) but is real, safe dedup. `getcwd.c` stays
  compiled locally (needed for `__private_getcwd`, realpath.c's real
  dependency, no other provider anywhere in this project) but its other
  public symbol, plain `_getcwd`, is now hidden via
  `-Wl,-unexported_symbol,_getcwd` on the final link -- same "keep the
  object for one private-use symbol, hide the other public name" shape
  already established here for `pread`/`pwrite`.

### Investigated and deliberately NOT fixed: `open_dprotected_np()` (real regression caught before it shipped)

Real canonical home is libsystem_c.dylib's own
`libsystem_c_syscall_compat.c` (same syscall #216); guarding
`copyfile_syscalls.c`'s own copy out under the existing
`LIBRESOLV_SOCKET_SYSCALLS_DYLIB_VARIANT` macro (same pattern as the
chmod() guard already there) compiled and linked cleanly. A real
`xcrun dyld_info -fixups` sweep of every built `.macho` -- done BEFORE
declaring this fix done, per this doc's own repeated lesson about
chained-fixups ordinals -- found `ed.macho`/`mktemp.macho` (both part
of the 76 `build_simple_cmds.sh` commands, explicitly out of scope for
a rebuild this pass per this sweep's own standing rules) already carry
a real bind recorded specifically as `libcopyfile/_open_dprotected_np`.
Shipping the guard would have silently broken both binaries' next load
until the deferred end-of-sweep `build_simple_cmds.sh` rebuild happens.
Reverted the guard (kept `open_dprotected_np()` locally defined and
exported in the dylib build, unchanged) and documented the real fix +
the reason it's blocked directly in `copyfile_syscalls.c` -- left open,
not structural, a natural pickup for whoever does that end-of-sweep
rebuild.

### A second real regression caught by the QEMU boot gate itself, not by the offline sweep

The first boot test after this batch's fixes **panicked for real**:
`pid 1 exited ... Symbol not found: _atoi / Referenced from:
/usr/lib/libbsm.dylib / Expected in: /usr/lib/libresolv.9.dylib`.
`libbsm.dylib` (untouched by this session, last touched in the Phase 1e
sweep and already closed as DAR-21) had a real, pre-existing chained-
fixups bind recorded specifically against `libresolv.9.dylib`'s copy of
`_atoi` -- the exact "consumer bakes in a specific owning-dylib ordinal
at its own link time" bug class this doc already documents twice
(`___error`/libxpc, `_uuid_unparse`/libxpc). The offline
`xcrun dyld_info -fixups` sweep run just before this boot missed it
entirely: it only globbed `libc_build/*.macho`/`*.macho`, never
`libc_build/system/*.dylib` -- dylibs can carry chained-fixups binds to
sibling dylibs too, not just executables. Fixed with a plain relink
(`build_bsm_dylib.sh`, no source change), confirmed via
`dyld_info -fixups` it now binds `libsystem_c/_atoi`, and re-verified
with a second real QEMU boot: PASS, byte-for-byte identical (address-
stripped) tail to the last known-good log. **Lesson recorded for the
next session**: the stale-chained-fixups sweep must glob dylibs too,
not just `.macho` binaries -- this project has now hit this exact class
of bug against both binary and dylib consumers.

### Real QEMU verification

Full downstream rebuild: `libncurses.dylib`, `libresolv.dylib`,
`libopenpam.dylib`, `libcopyfile.dylib` (all four independently, each
own trial link showing 0 undefined symbols before moving on) →
`libSystem.B.dylib` (reexports `libcopyfile.dylib`/`libresolv.dylib`
wholesale) → `libbsm.dylib` (the regression fix above) →
`tools/init_binary/inject_into_sd_image.sh` → `tools/boot_test.py`.
First boot caught the real `libbsm.dylib` regression above (not a false
positive -- a real panic, `[boot_test] FAIL`-equivalent). Second boot,
after the fix: `[boot_test] PASS`, 251s. Diffed address-stripped against
the last known-good log (`static_link_batch9_openpam_time_boot.log`,
end of the immediately-prior session): only expected QEMU/TCG
scheduling-order jitter (reordered `TEMP DIAGNOSTIC` lines, single-digit
`vm_page_bootstrap` page-count deltas, a ~1.6ms `pmap_startup()` timing
delta) plus byte-for-byte identical tails (`launchd[1] has started up`
through the same pre-existing `jnl: ... flushing fs disk buffer
returned 0x2d` stopping point, twice). Zero panics (checked past the
benign, already-documented `DIAG: first_panic latched=...` string and
the unrelated `IOPanicPlatform` kext-class-name log line, which also
false-positives a naive `panic` grep), zero "Symbol not found" in the
final log.

### Linear

DAR-11 (libresolv) fully cleared and self-closed. DAR-10 (libncurses,
10→4), DAR-12 (libcopyfile, 8→4), DAR-7 (libopenpam, 28→27) commented
with before/after + verification detail, left open (each has a real
remainder). DAR-21 (libbsm, already Done) commented with the transient
regression + fix detail, not reopened (count is back to 0).

Baseline updated (`dup_symbol_audit.sh --update-baseline`) to lock in
84→76.
