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
pointer), now executes real code -- but it still does not reach an
interactive prompt: it hangs indefinitely partway through interactive
startup with the kernel provably idle (not crashed). Root cause not yet
found; the leading hypothesis is a blocking syscall in bash's job-control/
tty startup path. This is the project's most immediate open item.

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

- **`bash` hangs partway through interactive startup**, kernel goes idle
  rather than crashing -- most likely a blocking syscall in the job-control/
  tty path (e.g. a raw `ioctl()` call bash makes directly rather than
  through the project's stubbed `tcgetattr`/`tcsetpgrp` wrappers). Not
  root-caused yet.
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

- **Phase 4 (userland)**: fix bash's interactive-startup hang (the
  immediate next blocker); Phase 4.6 items 3-5 (real POSIX regex, real
  `setattrlist(2)` wiring, real Mach IPC for `ps` stats + blocking sleep)
  remain not started; real Apple `Libinfo` (the actual repo, distinct from
  the working custom flat-file resolver) is still unbuilt; `chmod` needs a
  real ACL subsystem.
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
