# Roadmap

This is the short, current-state view of the project's multi-phase plan
(kernel boot -> real SD-rooted, real-command userland -> multi-user ->
networking -> a genuinely usable OS -> package management). For the full
phase-by-phase history -- exact bugs found and fixed, exact commands run,
exact QEMU-verified transcripts -- see `docs/historic/roadmap.md`, which
archives Phases 1-3.5 in full and every completed Phase 4/4.6 sub-item
verbatim; this file only summarizes what's still open. `docs/backlog.md`
remains the authoritative, detailed open-bug tracker (P0 = real-hardware
risk, P1 = correctness bugs, P2 = known gaps, P3 = hygiene) -- the "known
bugs" section below just points at it plus a couple of bugs the roadmap
narrative itself flagged as still-open.

## Current status

Actively on **Phase 4, step 3** ("real commands"): as of 2026-08-16, 20
real Apple/FreeBSD command binaries (`bash`, `uname`, `ps`, plus the Tier
1-4 set -- `true`/`false`/`yes`/`pwd`/`basename`/`dirname`/`echo`/`printf`/
`hostname`/`logname`/`cat`/`wc`/`head`/`tail`/`mkdir`/`touch`/`rm`) build,
link, and run QEMU-verified against the real HFS+ root, using a curated
real Apple/FreeBSD libc stack (string, platform, locale, stdio, gen,
printf/scanf, malloc, pthread, closure, removefile/copyfile, libunwind --
all real, QEMU-verified ports, see `docs/historic/roadmap.md`'s Phase 4 for
the full per-tier build history).

Real static `bash.macho` links clean and, after fixing three real bugs
found the same day (a stale/wrong-format `.dtb`, a test-harness login-line
bug, and a shell-quoting bug that corrupted `CONF_HOSTTYPE` into a garbage
pointer), executes real code. The 2026-08-16 "hangs indefinitely, kernel
provably idle" report was **re-investigated 2026-08-19 and found to be a
misdiagnosis**: `exec /bin/bash` (no `-i`) genuinely works -- it runs
non-interactively (real `isatty(stdin)`/`isatty(stderr)` honestly return
false, since this console has no real tty ioctls), which is *correct*
POSIX behavior, not a bug; it prints no `PS1` prompt by design, then
legitimately blocks on a real, correct `read()` waiting for the next
line -- this is what earlier sessions' PC-sampling correctly saw as
"kernel idle, not crashed". Feeding it a real command after `exec /bin/bash`
(no visible prompt needed) proves it fully alive: `echo` produced real,
correct output. Demand-paging the ~1.2MB binary in for the first time is
also genuinely slow (~90s, all real block reads, not a hang) on this
target's mostly-polled SD read path -- a real, separate perf
characteristic worth profiling later, not a correctness bug.

Forcing real interactivity via `exec /bin/bash -i` (the natural way to get
a visible prompt) does NOT work and exposes a second, different, real,
100%-reproducible bug: bash enters an infinite tight loop printing
`exit\n` via its own EOF-triggered `exit_builtin()` (real bash-3.2
behavior when `read()` on `stdin` reports EOF at an interactive prompt),
never actually terminating the process -- confirmed via multiple clean
QEMU boots showing tens of thousands of identical `exit\n` lines/second
with the guest at ~100% host CPU (genuinely busy, not idle). Source-traced
to bash's interactive input path (`with_input_from_stdin()` ->
`with_input_from_stream(stdin, ...)` -> real libc `getc(stdin)`) -- a
different, never-before-exercised code path from the non-interactive one
(`with_input_from_buffered_stream()`, a raw-fd read) -- combined with
`jump_to_top_level(EXITPROG)` -> `longjmp`/`siglongjmp` (this project's
own hand-rolled arm64 implementation in `bash_syscalls.c`) apparently not
completing a real process termination. Not root-caused to an exact
instruction (live QEMU-monitor PC sampling was attempted but the monitor
socket proved unreliable to reconnect to mid-session); see
`docs/historic/roadmap.md`'s newest Phase 4 entry for the full evidence
trail and concrete next steps. **`-i` must not be used as a workaround --
it trades a non-bug for a real, worse, CPU-spinning one.** This remains
the project's most immediate open item.

Phase 4.6 items 1 (real timezone database) and 2 (real `environ`/`getenv`
plumbing) are DONE and QEMU-verified (2026-08-16). Items 3-5 (POSIX regex,
`setattrlist(2)`, Mach IPC) are explicitly not started.

Separately, Phase 5's custom flat-file account resolver
(`tools/libinfo_custom/`) is done and verified (2026-08-14) and already
in real use by `ps`/`rm` for `getpwnam`/`getgrgid` lookups, even though the
rest of Phase 5 (login/su/PAM) hasn't been started.

## Known bugs

Currently open, real bugs the roadmap narrative flagged as not-yet-fixed
(see `docs/backlog.md` for the full, authoritative list and detail):

- **`bash -i` loops forever printing `exit`, never terminates**, spinning
  at ~100% host CPU -- real bash's own EOF-at-prompt handling
  (`exit_builtin()`) fires repeatedly instead of once. Likely needs (a) a
  fix to why `getc(stdin)` (bash's real interactive-input path, distinct
  from the non-interactive raw-fd path that already works) reports
  immediate EOF, and/or (b) a fix to this project's hand-rolled
  `siglongjmp()` (`tools/userland_staging/bash_syscalls.c`) not completing
  a real process exit via `jump_to_top_level(EXITPROG)` -- this is the
  first time in this project's testing history the *restore* half of that
  hand-rolled `sigsetjmp`/`siglongjmp` is actually exercised (all earlier
  verification only exercised the initial "returns 0" call). Not
  root-caused to an exact instruction yet. **Do not invoke bash with `-i`
  as a workaround for the (non-bug) missing prompt below it.** Plain
  `exec /bin/bash` (no `-i`) genuinely works non-interactively -- see
  Current status above and `docs/historic/roadmap.md`'s newest Phase 4
  entry for the full writeup.
- **`yes` reliably panics the kernel** (`_rtclock_intr` interrupt-processing
  watchdog, under sustained fast syscall load) -- a real kernel bug, not a
  port bug in `yes.c` itself. Ships anyway, flagged. See `docs/backlog.md`
  P0 item 29.

## Known stubbings / stand-ins

Deliberate stand-ins currently in place for a real subsystem, each will
need a real implementation later:

- **`bsd_pthread_stub.c`** (kernel) -- `bsdthread_create`/
  `workq_kernreturn`/`psynch_*` unconditionally return `ENOTSUP`, standing
  in for Apple's never-open-sourced pthread.kext. Blocks all real
  multithreading, project-wide, indefinitely.
- **From-scratch SHA-256/384/512** standing in for corecrypto
  (`g_crypto_funcs` is never registered) -- covers hashing/integrity only;
  no AES/HMAC/RSA/ECDSA/DH/ECDH. Blocks real `sshd` (Phase 7b) and `pkg`'s
  `libecc` signature verification (Phase 8).
- **`cerror_stub.c`** -- minimal errno/`cerror` stand-in used where real
  Apple `errno.c`'s TSD dependency wasn't available yet.
- **Residual no-op `os_once`/`pthread_mutex_*` copies** in
  `libc_stdio_shims.c` -- mostly superseded by the real `libplatform`/
  `libpthread` ports, a couple of harmless duplicates remain where they
  don't collide.
- **`ps_task_info_stub.c`** -- `ps`'s CPU%/RSS/VSZ/thread columns print
  honest zeros instead of real values, standing in for `task_for_pid`/
  `task_info`/`task_threads`/`thread_info` (needs real Mach IPC, Phase 4.6
  item 5).
- **`simple_cmds_nanosleep_compat.c`** -- a CPU-burning spin-wait against
  `mach_absolute_time()` stands in for real blocking `nanosleep(2)` (needs
  `host_get_clock_service`/`semaphore_create`/`clock_get_time`, Phase 4.6
  item 5). Affects `tail -f`'s poll interval and any future `sleep(1)`.
- **`simple_cmds_rpmatch_compat.c`** -- a real but C-locale-only
  `rpmatch()` (used by `rm -i`'s y/n prompt) stands in for real POSIX
  `regcomp`/`regexec`/`regfree`, which is entirely unbuilt (Phase 4.6 item
  3). Also blocks bash's `[[ str =~ pattern ]]` and any future `grep`/`sed`.
- **`simple_cmds_lutimes_compat.c`** -- honest `ENOSYS` for `touch -h`'s
  symlink case, pending real `setattrlist(2)` userland wiring (the kernel
  syscall itself is already implemented; only the userland stub/wrapper is
  missing, Phase 4.6 item 4).
- **`simple_cmds_mmap_compat.c`** -- reimplements `mmap(2)`/`munmap(2)`
  without Apple's optional stack-logging hook (`stack_logging_internal.h`
  doesn't exist in this checkout) -- functionally complete, a narrow
  permanent stand-in rather than a capability gap.
- **`cat_socket_stub.c`** -- honest `ENOSYS` for `cat`'s `AF_UNIX`-socket
  read path; no real networking exists yet (Phase 6).
- **`chmod` not attempted at all** -- needs real POSIX.1e/Darwin ACLs
  (`acl_get_file`/`acl_set_file`) plus `<membership.h>` UUID-based user/
  group lookups, a subsystem not started.
- **Bash's own honest failure stand-ins** (`bash_syscalls.c`): `dlopen`/
  `dlclose`/`dlsym`/`dlerror` always fail (no dyld at all); `tcgetattr`/
  `tcsetattr`/`tcgetpgrp`/`tcsetpgrp`/`ttyname` return `ENOTTY` (no tty
  line-discipline); `alarm`/interval timers are silent no-ops (no
  `SIGALRM` subsystem); readline line-editing and programmable completion
  are documented no-op/failure stubs; real networking (`netopen`/
  `netconn`) is unavailable.
- **INET/INET6 domain registration disabled** in the kernel (along with
  MULTIPATH/flow_divert/IGMP/MLD) -- no network interface exists yet; a
  scope decision documented in `architecture.md`, revisited once Phase 6
  lands.
- **`libmalloc_vm_mig_stubs.c`** -- real but provably-unreachable
  dead-code MIG-fallback bodies (`KERN_FAILURE`) behind `libmalloc`'s real
  Mach-trap fast path; harmless, kept for completeness only.
- **`dyld`** -- not built or exercised at all; every binary in the project
  is fully static, zero `.dylib` output anywhere (Phase 6.5).
- **`libdispatch`/GCD** -- zero API surface, not attempted at all (Phase
  6.6).

## Remaining future work

- **Phase 4 (userland)**: fix bash's `-i` infinite `exit`-print loop (the
  immediate next blocker, see Known bugs) so bash can reach a real visible
  prompt; Phase 4.6 items 3-5 (real POSIX regex, real `setattrlist(2)`
  wiring, real Mach IPC for `ps` stats + blocking sleep) remain not
  started; real Apple `Libinfo` (the actual repo, distinct from the
  working custom flat-file resolver) is still unbuilt; `chmod` needs a
  real ACL subsystem. Separately worth profiling: bash.macho's first-touch
  demand-paging takes ~90s on this target's mostly-polled SD read path --
  slow but not a correctness bug.
- **Phase 5 (multi-user)**: the custom account resolver is done; still
  needed: account-provisioning tools (`pwd_mkdb`/`vipw`/`chpass`/
  `passwd`), `crypt()` password verification, wiring the resolver into a
  real `login`/`getty`/`su`, and eventually PAM (OpenPAM as fallback).
  `sudo` is deliberately out of scope indefinitely.
- **Phase 6 (networking)**: entirely unstarted. Needs, in strict order: a
  DWC2 USB host controller driver (6a -- the largest single remaining
  undertaking in the project, per its own plan doc, `docs/usb-support-plan.md`);
  then the SMSC LAN9514 Ethernet driver (6b -- chip-level logic already
  ported, waiting on 6a); then a DHCP client + `ifconfig`/`ping`/`netstat`
  (6c -- FreeBSD's `dhclient` recommended over Apple's `bootp`).
- **Phase 6.5 (dyld)**: planning-only placeholder, deliberately deferred
  until Phase 6 gives it a real concurrent multi-process workload to
  justify the cost. Needs a real `.dylib`-producing link path and an
  as-yet-unexercised kernel `LC_LOAD_DYLINKER` path.
- **Phase 6.6 (libdispatch/GCD)**: planning-only placeholder, blocked
  behind 6.5. Even once unblocked, real concurrent `dispatch_async` can't
  exist until real threading does (Phase 7.5's pthread ceiling) -- scope
  would be limited to single-thread-honest `dispatch_once`/`dispatch_sync`/
  kqueue-backed dispatch sources.
- **Phase 7 (usable OS)**: none of 7a-7d started. 7a (DNS via `libresolv`)
  is small and clean once Phase 6 lands. 7b (`sshd` via OpenSSH) is
  blocked on real crypto beyond SHA-2 (Phase 7.5). 7c (self-hosting
  toolchain) recommends Apple's old `gcc_42` over `clang` for feasibility,
  plus `cctools`. 7d (`vim`) is small and can fold into whichever of 7a-7c
  lands first.
- **Phase 7.5 (closed-source ceilings)**: a living list, not a phase to
  "complete" -- real pthread multithreading and real corecrypto-backed
  crypto are permanent gaps short of a from-scratch reimplementation of a
  closed Apple subsystem (see Known Stubbings above).
- **Phase 8 (`pkg` package management)**: fully planned, not started.
  `freebsd/pkg` is the chosen vehicle (native Mach-O awareness, real
  custom-repo support already documented). Needs `libsqlite`/`libucl`/
  `libelf`/`libyxml`/`libder`/`picosat` ported first (low risk), then
  `libecc` (shares Phase 7.5's/7b's crypto gap) and `libfetch` (needs
  Phase 6), then `liblua`, before a real custom package repo can be built
  and consumed on-device.
