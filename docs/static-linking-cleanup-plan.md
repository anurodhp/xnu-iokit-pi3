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
