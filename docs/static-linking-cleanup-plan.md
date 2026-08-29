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
| 2 | Phase 1a: de-static `libdispatch.dylib` (backlog d1) | **done, unverified** (490 → 2) |
| 3 | Phase 1b: de-static `libc++.dylib` | **done, unverified** (426 → 59) |
| 4 | Phase 1c: de-static `libxpc.dylib` + `libxml2.dylib` | libxpc **done** (189 → 62); **libxml2 not started** |
| 5 | Phase 1d: residual dylib duplicate state | not started (262 left, table above) |
| 6 | Phase 2a: simple-commands baseline | not started |
| 7 | Phase 2b: launchd/launchctl/syslogd/pd_console_login | not started |

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
