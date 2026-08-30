# launchd PID-1 — postmortem (silent `exit(1)` resolved 2026-08-30, forked children partially)

Companion to `docs/launchd-bug6-postmortem.md`, which covers the crash
*before* this one. That document ends at "launchd reaches a clean
`exit(1)`, 3 threads" — this one picks up exactly there and covers
everything up to the current state: **real launchd boots as PID 1,
reaches `launchd_runtime()`, and really forks daemon children; those
children no longer crash, but do not yet reach `exec`.**

Four distinct real bugs were found and fixed. Two were duplicate static
copies (the 4th and 5th of that class in this project), one was a
missing initializer, one was a call-ordering mistake. None of them was in
the kernel.

---

## Bug A — TSD base clobber (`pthread_tsd_shim.c`)

`pthread_tsd_shim_bootstrap()` guarded on its own **per-copy**
`g_tsd_bootstrapped` flag and then unconditionally set `TPIDRRO_EL0` to
its own private `g_tsd_block`. That file is compiled into more than one
image — `nm` finds `_g_tsd_block` in **both** `libsystem_malloc.dylib`
and `launchd.macho`.

Sequence: `libSystem.B.dylib`'s constructor correctly bootstrapped the
main thread's TSD base; then `_dyld_initializer()`'s first `malloc()`
went through `libmalloc_dispatch.c`'s `get_zone()`, which calls this
function on *its* copy, whose own flag was still clear — and it
overwrote the good base with a zero-filled block.

**Fix**: adopt an already-installed base instead of clobbering.
`pthread_tsd_shim_bootstrap_with_base()`
(`pthread_tsd_platform_bootstrap.c`) stays unconditional on purpose — it
is the authoritative installer.

## Bug B — two munge tokens signing one shared `_main_thread`

`launchd.macho` statically linked `libpthread_no_dup_pthread_create.a`,
giving it a full second copy of the pthread implementation alongside the
real `libsystem_pthread.dylib`.

- `__main_thread` is **external common**, so the two copies coalesced
  into launchd's single struct.
- `__pthread_ptr_munge_token` is **non-external in both**, so it did
  *not* coalesce.

Both bootstraps therefore signed the *same* `_main_thread` struct with
*different* private tokens. Whichever ran second won, and from then on
every `pthread_self()` served by `libsystem_pthread.dylib` — the copy
real CoreFoundation, libdispatch and libc++ actually bind to — failed
`_pthread_validate_signature()` and called `abort_with_reason()`. That
surfaced as `pid 1 exited ... (signal 0, exit 1)` with zero console
output.

**Fix**: remove that archive from `build_launchd.sh`'s link entirely (it
relinks with zero undefined symbols) and delete `launchd.c`'s now-
redundant `pthread_main_thread_bootstrap()` call.

**How it was proven, not guessed**: a kernel-side diagnostic in
`bsd/kern/kern_exit.c`'s `exit()` printed `backtrace_user()`, the TSD
base, and the pthread `sig` field, with load-address anchors from
`kern_exec.c`. The proof was arithmetic: `(sig ^ launchd_token)` equalled
the `pthread_t` exactly, while `(sig ^ libsystem_pthread_token)` did not.
Userland `/dev/console` markers were useless and actively misleading for
this one — the failing process never got far enough to write.

## Bug C — `fork()` did almost nothing real Darwin's does

With A and B fixed, launchd reached `launchd_runtime()` and started
forking daemon children. Those children died with a real
`EXC_CORPSE_NOTIFY` (`qemu/launchd_boot_test95.log`, pids 3 and 4).

This port's `fork()` was a bare `return __fork();`. The justification on
record was that real Libc's `_libSystem_atfork_child()` hook "would
always be a real no-op regardless" since nothing here registers a
`pthread_atfork()` handler. **That reasoning was wrong.** On real Darwin
the atfork child hook is not mainly about user-registered handlers — it
is where libSystem re-establishes the child's Mach, pthread, malloc and
dyld state.

The first repair (2026-08-30, commit `ebfd181`) added `_mach_fork_child()`
plus a hand-written zeroing of TSD slot `__TSD_MIG_REPLY`, and explicitly
flagged the pthread and malloc halves as knowingly missing. That got the
children past a hang inside `bootstrap_init()` — whose very first act is
the MIG RPC `task_get_special_port(task_self_trap(),
TASK_BOOTSTRAP_PORT, ...)`, which needs a reply port valid in the
child's brand-new IPC space — but they then crashed downstream.

**Fix**: implement the real thing. `fork()` is now real
`Libc/sys/fork.c`'s body, dispatching through a real
`libSystem_atfork_prepare` / `_parent` / `_child` triple, in real
Apple's exact order. The order was taken verbatim from **real
Libsystem-1292.50.1's `init.c`** — the macOS 11.2 / Darwin 20.3 vintage
this whole port is pinned to — not reconstructed from comments. That
matters: libpthread's own `pthread_atfork.c` comment claims
`_pthread_atfork_child()` is called "first ... (after
`_dyld_fork_child`)", which is **stale for this vintage**, where
`_mach_fork_child()` is genuinely first and `_dyld_fork_child()` runs
sixth.

Handlers this port really has, all real unmodified Apple source already
built and exported here, reached through `launchd.macho`'s existing
`LC_LOAD_DYLIB`s: the six `_pthread_atfork_*` entry points
(`libsystem_pthread.dylib`), `_mach_fork_child`
(`libsystem_kernel.dylib`), `_malloc_fork_prepare`/`_parent`/`_child`
(newly written for this port, see below), and `dispatch_atfork_*`
(`libdispatch.dylib`). Everything real Apple calls that this port does
not have is named individually, with its reason, in `fork_compat.c`'s
own header comment and in `docs/backlog.md` items **d13** and **d15**.

Two details worth carrying forward:

- The hand-written `__TSD_MIG_REPLY` zeroing is **gone, deliberately**.
  It was not merely redundant — the real handler does strictly more.
  `_pthread_atfork_child()` → `_pthread_main_thread_postfork_init()` →
  `_pthread_main_thread_init()` (`third_party/libpthread/src/pthread.c`)
  allocates a genuinely fresh reply port via `mach_reply_port()` *and*
  refreshes `MACH_THREAD_SELF` and `MACH_SPECIAL_REPLY`, which the
  hand-written version left holding the **parent's** port names. Stale
  `__TSD_MACH_THREAD_SELF` is read on every single `os_unfair_lock`
  acquisition (libplatform's `_os_lock_owner_get_self()`), so that was a
  real correctness problem, not tidiness.
- `_pthread_atfork_child()` **requires** `_pthread_atfork_prepare()` to
  have run in the parent — it consumes `globals->psaved_self`, which
  prepare sets from `pthread_self()`. The prepare/parent halves are not
  optional decoration; a child-only implementation would fault.

`_malloc_fork_prepare`/`_parent`/`_child` did not exist at all: real
`malloc.c` is deliberately out of scope for this port, so its three fork
hooks now live in `tools/userland_staging/libmalloc_dispatch.c`, this
project's replacement for `malloc.c`'s public-API glue. They are the
real per-zone bodies (`introspect->force_lock` / `force_unlock` /
`reinit_lock`, with real `malloc.c`'s own `version < 9` fallback),
reduced to the one zone this dispatcher owns. The `malloc_zones[]` walk
and `MALLOC_LOCK()` have nothing to protect here, and the
`CONFIG_NANOZONE` block is genuinely N/A because `nano_malloc.c` is not
built. See **d16** for the caveat that `launchd.macho` currently has two
heaps, so this reinitializes only its own.

## Bug D — `atexit()` had never worked, and hung silently

This one was uncovered by fixing something else, and is the sharpest
lesson of the session.

`launchd` logged `mkdir("/var/tmp/"): Read-only file system` on every
boot, because `pd_pid1_prepare_legacy_ipc()` ran *before*
`pd_launchd_boot()`, whose first act is the `mount -uw /` equivalent this
port needs (`vfs_mountroot()` hardcodes `MNT_RDONLY|MNT_ROOTFS`). The
remount always succeeded — just after the directories it was needed for.
Moving `pd_launchd_boot()` earlier in `launchd.c` is the faithful fix:
on real Darwin the root filesystem is already read-write by the time
launchd's `/var/tmp` setup runs.

That reorder immediately turned the boot into a **silent hang**, with no
console output, no panic, and no crash. Bisecting with `/dev/console`
progress markers put it inside `ipc_server_init()` (`ipc.c`), then
narrowed it to exactly one statement: `atexit(ipc_clean_up)`.

Root cause: real `atexit_register()` (`Libc/stdlib/FreeBSD/atexit.c`)
opens with `struct atexit *p = __atexit; assert(p);`, and `__atexit` is a
file-static only ever set by `__atexit_init()` — which real Apple calls
from exactly one place, `_libc_initializer()` in `Libc/sys/_libc_init.c`,
**a file this project has never built**. So `__atexit` was `NULL`, the
assert fired, and on this port `assert()`'s `abort()` is
`kill(getpid(), SIGABRT)` with no real signal delivery, so instead of
dying the process spins forever. launchd's `stderr` is `/dev/null`, so
not one byte reached the console.

It had gone unnoticed for months purely because launchd's only
boot-path `atexit()` call site was unreachable: `ipc_server_init()`
bailed out several statements earlier at `mkdir(LAUNCHD_SOCK_PREFIX)`
returning `EROFS`, because the root filesystem was still read-only. **A
correctness fix in one place made a long-latent bug reachable in
another.**

**Fix**: call the real `__atexit_init()` from a real image constructor in
`launchd_syscall_compat_headers/isthreaded_compat.c`. That file because
`__atexit_init` is `__private_extern__` (only a same-image object can
reach it) and because `__isthreaded`, which that file defines, is
referenced by `atexit.c` itself — so the linker pulls the constructor out
of the archive exactly when `atexit.o` is pulled, and never otherwise.
The other seven initializers `_libc_initializer()` runs are still
missing; that is **d14**, and it is the second ad-hoc one-off of this
exact shape (`third_party-patches/Libc.patch` already carries a ctor
calling `__xlocale_init()` for the same reason).

---

## Verification

All by real QEMU boot, not by clean link:

| log | what it shows |
|---|---|
| `qemu/launchd_boot_test95.log` | before: `mkdir("/var/tmp/")` EROFS, pids 3 and 4 both `EXC_CORPSE_NOTIFY` |
| `qemu/launchd_boot_test99_baseline.log` | clean re-run of the same, with every change of this session stashed — proves the delta is real |
| `qemu/launchd_boot_test103_progress.log` | hang localized to `ipc_server_init()` |
| `qemu/launchd_boot_test105_ipc2.log` | hang localized to `atexit(ipc_clean_up)` |
| `qemu/launchd_boot_test106_atexit.log` | `atexit()` proven broken *everywhere*, not just at that call site |
| `qemu/launchd_boot_test107_atexit_fix.log` | atexit fixed; children reach `_dyld_fork_child()` and hang there |
| `qemu/launchd_boot_test108_nodyld.log` | with dyld's atfork trio omitted, children get through `dispatch_atfork_child()` |
| `qemu/launchd_boot_test109_clean.log` | **final, all diagnostics stripped**: launchd boots as PID 1, no `mkdir` EROFS, no corpses, no panic, parent alive and syncing |

## Current state and next work

launchd as PID 1 boots, remounts `/` read-write, mounts devfs, brings up
its IPC socket, loads `/Library/LaunchDaemons`, reaches
`launchd_runtime()`, and forks daemon children that survive the whole
atfork sequence this port implements. **The children still do not reach
`exec`.** The known walls, each filed separately so this document is not
the tracker for them:

- **d13** — dyld's `_dyld_fork_child()` hangs every child; the trio is
  currently not called. Lead (unproven): `takeLockBeforeFork()` locks
  unconditionally while `resetLockInForkChild()` only resets at
  `libSystemHelpers->version() >= 2`, and the reset itself deliberately
  crashes on a lock that does not look held. Check this port's real
  `LibSystemHelpers::version()` first.
- **d14** — `_libc_initializer()`'s other seven initializers.
- **d15** — the remaining absent atfork handlers, notably
  `xpc_atfork_child` (PureDarwin's libxpc has no fork handlers at all,
  and `bootstrap_init()` is libxpc code).
- **d16** — `launchd.macho`'s two heaps limit `_malloc_fork_child()`.

## Process lessons

1. **Get the order from the real source, not from comments about it.**
   libpthread's own comment about where `_pthread_atfork_child()` sits in
   `libSystem_atfork_child()` is wrong for this vintage. Real
   Libsystem-1292.50.1's `init.c` settled it in one fetch.
2. **A "deliberately not implemented, flagged in the commit" gap is still
   a bug.** Bug C's first repair honestly documented the missing
   pthread/malloc halves — and those halves were exactly what the
   children needed next.
3. **Validate the diagnostic channel in the same boot.** The first
   marker pass printed nothing at all, which could mean either "the code
   never ran" or "the markers do not work here". Adding one
   known-reachable `[FK:channel-ok]` marker turned an ambiguous null
   result into a definite one. This is `feedback_diagnostic_channel_must_
   not_share_failure_mode` applied constructively rather than as a
   prohibition.
4. **Bisect against a rebuilt baseline, not against an old log.** The old
   `launchd_boot_test95.log` had been taken against a different SD image
   state. Re-running the baseline with every change stashed was what made
   the comparison trustworthy.
5. **Fixing a real bug can expose a latent one, and the new failure can
   look worse than the old.** The `/var/tmp` reorder is correct and made
   the boot hang. Do not reflexively revert the correct change — find
   what it unblocked.
6. **A stale build artifact hides a broken build script.** `launchctl.macho`
   sat in `libc_build/` for weeks while `build_launchctl.sh` could not
   actually produce it (**d17**). The repo rule that every artifact needs
   a reproducing script exists precisely for this.
