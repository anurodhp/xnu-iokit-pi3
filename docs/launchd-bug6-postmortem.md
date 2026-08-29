# launchd PID-1 bug #6 — postmortem (resolved 2026-08-29)

Status: **RESOLVED**. Root cause was a userland *build-flag* bug, not a
kernel bug and not QEMU. Five sessions were spent investigating the
kernel's arm64 context-switch path before the actual cause was found in
already-captured log data, with no new boot required.

Backlog entry: `docs/backlog.md` item d11.
Fix commit: `76ff8cd`. Diagnostic cleanup: `a1678ab`.

## Root cause

`_FORTIFY_SOURCE` self-recursion in `third_party/Libc/secure/*_chk.c`.

Apple builds that entire directory with fortify **off**, for a hard
reason. Each `__X_chk()` implements the "size is unknown/unbounded" case
by falling through to the plain `X()`. But `<stdio.h>`/`<string.h>`'s
fortify inlines rewrite that very call back into `__builtin___X_chk()` —
i.e. into itself.

It is not even a conditional recursion. The rewritten call passes
`__darwin_obsz(dest)`, which is `(size_t)-1` for any pointer whose size
isn't statically known — always true of a `_chk` function's own
parameter — and "unbounded" is precisely the branch that calls the plain
function. So the recursion branch is taken unconditionally, forever.

Confirmed by disassembly rather than inference. `___vsprintf_chk` was
emitting:

```
10004a608  sub  sp, sp, #0x40
10004a60c  stp  x29, x30, [sp, #0x30]
10004a610  add  x29, sp, #0x30
10004a614  stur x0, [x29, #-0x8]     <- faulting instruction (pc = base+0xc)
...
10004a65c  mov  x2, #-0x1            <- dstlen = SIZE_MAX
10004a660  bl   ___vsprintf_chk      <- calls ITSELF (base+0x58)
10004a664                            <- base+0x5c = the observed lr
```

That blew launchd's main-thread stack into its guard page on the very
first real `launchd_syslog()` call — the one that prints
`*** launchd[1] has started up. ***`.

## Why it looked like a kernel bug

The reported fault registers were `sp=0x16e63ffd0`, `fp=0x16e640000`,
`fault_addr=0x16e63fff8`. Because `fp` was exactly page-aligned and sat
~24MB below `USRSTACK64`, it was read as "the main thread is somehow
running on the second thread's `pthread_create()`-allocated stack",
which pointed at a kernel `SP_EL0` context-switch bug. Sessions 2-5 then
ruled out, with real evidence, a series of increasingly deep kernel
theories: `TPIDRRO_EL0` cross-image confusion, duplicate static
`pthread_create`/`__bsdthread_register`, PCB (`machine.contextData`)
aliasing, and kernel-stack aliasing. All of those were genuinely
disproved — they just weren't the bug.

The register values were in fact perfectly self-consistent with the real
cause: `x29 = sp + 0x30 = 0x16e640000` (the reported `fp`), and the
faulting store `[x29, #-0x8]` = `0x16e63fff8` (the reported
`fault_addr`).

## The two clues that were already in the logs

Both were present in captured data and cost nothing to check.

**1. Decode fault result codes exactly.** `handle_user_abort` sets
`codes[0] = result` straight from `vm_fault()`. The logs showed
`code0=0x2` = `KERN_PROTECTION_FAILURE`, meaning `vm_fault()` *found* a
VM entry and refused the write — a **guard page**, i.e. stack
exhaustion. It was read as "page never mapped", which would have been
`0x1` = `KERN_INVALID_ADDRESS`. The whole kernel-side `SP_EL0` theory
rested on that misreading.

The hardware DFSC reporting "translation fault, level 3" is *not*
contradictory: the page simply had no PTE faulted in yet. `vm_fault()`'s
return code is the authoritative one.

**2. Check `lr` vs `pc` for self-recursion.** `lr - pc` was exactly
`0x50` in all five captured boots, across five different kernel builds.
A fixed, small `lr - pc` delta means the return address lies inside the
same function that is faulting in its own prologue — the signature of
direct self-recursion.

## Why it was not QEMU

Three independent reasons:

1. `lr - pc == 0x50` was bit-exact and reproducible across five kernel
   builds. Emulation artifacts do not reproduce like that.
2. The faulting instruction `stur x0, [x29, #-0x8]` matches the reported
   `fp`/`fault_addr` exactly — QEMU reported the fault *correctly*.
3. The self-recursive `bl` is visible **statically in the Mach-O**, with
   no emulator involved at all.

More generally: TCG here is single-core and models no speculation or
cache incoherency, so QEMU makes genuine races *less* likely to surface,
not more. A crash this deterministic is almost always real software. The
one legitimate QEMU-specific gotcha in this project remains the
`cpu-factor=10` watchdog timing workaround documented in `runvm.sh`,
which looks nothing like this.

## The fix

`-D_FORTIFY_SOURCE=0` on every `Libc/secure/*_chk.c` compile, in:

- `build_launchd_core.sh`
- `build_corefoundation.sh`
- `build_libxml2.sh`
- `build_libmalloc.sh`
- `build_dyld_core.sh`

`build_libdispatch_dylib.sh` and `build_libxpc_syscalls2.sh` **already**
did this via their own `CFLAGS_SECURE` arrays — which is exactly why
*their* `_chk` functions were fine while these five scripts' were
broken. The precedent existed in-repo and simply hadn't been applied
everywhere.

Note `-O0` and `-fno-builtin` do **not** protect against this: Apple's
`sys/cdefs.h` enables fortify regardless of optimisation level, and
`__builtin___*_chk` is an explicit builtin that `-fno-builtin` does not
suppress.

## Sweep for the same bug class

Rather than fixing only the function that happened to crash, every built
artifact was swept for `_chk` functions containing a `bl` to themselves.
Two more unconditional, latent landmines were found and fixed:

- `___memmove_chk` — in `launchd.macho` and `libxml2.dylib`
- `___stpncpy_chk` — in `libxml2.dylib`

These very likely explain part of the "crash site keeps moving between
runs" behaviour seen in earlier sessions. Post-fix, the sweep over every
dylib, archive and executable finds zero self-recursing `_chk`
functions.

Reusable check (no boot needed):

```sh
llvm-objdump -d --disassemble-symbols=<sym> <binary> | grep "bl.*<<sym>>"
```

## Verification

- Static: `___vsprintf_chk` now calls `_vsprintf`/`_vsnprintf`; full
  self-recursion sweep clean.
- Boot (`qemu/launchd_boot_test74.log`): the `IOKITDIAG18` fault is gone
  — 0 occurrences, where it was present in *every* prior boot. launchd
  mounts devfs, completes its mutex work, and successfully spawns its
  **third** thread (task reports 3 threads, previously 2 — the
  `update_thread` `pthread_create` that used to trigger the crash).
- Post-cleanup boot (`qemu/launchd_boot_test76.log`): behaviour
  unchanged after removing all instrumentation.

## Current blocker (next piece of work)

launchd now fails **later and differently**: a clean `exit(1)`
(`signal 0`), not a SIGSEGV.

```
pid 1 exited -- no exit reason available -- (signal 0, exit 1)
panic(...): initproc exited ... (signal 0, exit status 1 )
```

It occurs shortly after the third thread's `start_routine` is entered,
alongside `jnl: b(14, 2): flushing fs disk buffer returned 0x2d`. This
is launchd's own logic deciding to give up — considerably more
tractable than a memory fault. Next step is to identify which real
`exit(1)` path in the launchd/PureDarwin runtime is being taken.

## Process lessons

1. **Decode error/result codes exactly before theorising.** One
   misread constant (`KERN_PROTECTION_FAILURE` vs
   `KERN_INVALID_ADDRESS`) redirected several sessions into the kernel.
2. **Re-read captured data before gathering more.** The fix required no
   new boot; the evidence had been sitting in the logs for sessions.
3. **Static analysis beats boot cycles.** `nm`/`llvm-objdump` on the
   built artifacts answered in seconds what boot-and-crash cycles had
   not, and swept for the whole bug class at once.
4. **Suspect the diagnostic itself.** A `kprintf` added inside
   `machine_switch_context()` caused a brand-new, fully deterministic
   crash in `load_init_program_at_path`; removing it restored the
   original behaviour. When new instrumentation changes crash
   behaviour, isolate it before treating the new behaviour as a finding.
5. **When a fix is a build flag, check every consumer.** The correct
   pattern already existed in two scripts; five others lacked it.
