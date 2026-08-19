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

Actively on **Phase 4, step 3** ("real commands"): as of 2026-08-19, 21
real Apple/FreeBSD command binaries (`bash`, `uname`, `ps`, plus the Tier
1-4 set -- `true`/`false`/`yes`/`pwd`/`basename`/`dirname`/`echo`/`printf`/
`hostname`/`logname`/`cat`/`wc`/`head`/`tail`/`mkdir`/`touch`/`rm`/`env`)
build, link, and run QEMU-verified against the real HFS+ root, using a
curated real Apple/FreeBSD libc stack (string, platform, locale, stdio,
gen, printf/scanf, malloc, pthread, closure, removefile/copyfile,
libunwind -- all real, QEMU-verified ports, see
`docs/historic/roadmap.md`'s Phase 4 for the full per-tier build history).

`env.macho` (real Apple `shell_cmds/env/env.c`+`envopts.c`) had existed on
disk since Phase 4.6 item 2 (2026-08-16) but was never actually
boot-tested that day -- confirmed for real 2026-08-19: rebuilt clean off
today's tree (byte-identical output, only the same benign
`$UNIX2003`/`$DARWIN_EXTSN` alias link warnings as before) and QEMU-verified
in all three real modes via `exec /bin/env` at the shell: bare `env`
prints the real `PATH=/bin:/usr/bin`/`HOME=/`/`USER=root`/`TZ=UTC` table
`init.c`'s `build_envp()` sets; `env FOO=barbaz /bin/env` adds `FOO` on
top of the inherited table before exec'ing; `env -i /bin/env` and
`env -i FOO=onlyfoo /bin/env` correctly clear the environment first (the
former printing nothing, the latter printing only `FOO=onlyfoo`). No bug
found or fix needed -- the environ/getenv plumbing landed 2026-08-16
already covered `env`(1) correctly.

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

**`bash -i`'s infinite `exit`-flood bug is FIXED and QEMU-verified for real
as of 2026-08-19** (Twenty-third pass) -- real interactive bash now boots
to a genuine `bash-3.2# ` `PS1` prompt, reads and executes multiple real
typed commands in a row with correct output, and exits cleanly (once,
not in a loop) on an explicit `exit`. Two independent, real, compounding
bugs were found and fixed, both root-caused via a live QEMU boot with
temporary instrumentation (removed before commit) rather than guessed:

1. **`sigsetjmp`/`siglongjmp` double-frame corruption**
   (`tools/userland_staging/bash_syscalls.c`): the previous implementation
   split the work into a C wrapper function (`sigsetjmp()`/`siglongjmp()`)
   that called a separate raw-asm register-save/restore helper. Because a
   real C function always gets its own compiler-generated stack frame,
   that asm helper was being invoked one level too deep -- it captured
   x29/x30 as they stood *inside* `sigsetjmp()`'s own frame (return
   address = back into `sigsetjmp()`, not into `sigsetjmp()`'s real
   caller, `reader_loop()` in `eval.c`), and spilled the `env` parameter
   into that same frame. The instant the initial `sigsetjmp()` call
   returned 0, that frame was popped and its memory became fair game for
   every function bash called afterward (parsing, `read_command()`, ...);
   by the time bash's own EOF handling called `siglongjmp()` many stack
   frames deeper, `sigsetjmp()`'s old frame -- including its own saved
   x29/x30 needed to eventually return to `reader_loop()` -- had long
   since been overwritten. Fixed by rewriting `sigsetjmp`/`siglongjmp` as
   the raw asm entry points directly (no intervening C wrapper, hence no
   doomed extra frame) -- register capture is the very first thing that
   runs on entry, before any nested call, so x29/x30/sp captured are
   genuinely the real caller's, exactly like a real libc implementation.
2. **`READLINE` silently forced on** (`third_party/bash/config.h`): this
   vendored config.h unconditionally `#define`d `READLINE 1`, silently
   overriding `build_bash.sh`'s deliberate `-UREADLINE` compile flag (a
   `#define` inside a header included by every translation unit wins over
   an earlier command-line `-U`). With `READLINE` still effectively on,
   `y.tab.c`'s interactive input path compiled to `yy_readline_get()`
   (calling the real `readline()` API) instead of the intended plain-stdio
   `yy_stream_get()`/`getc_with_restart()` path -- and since real GNU
   readline's line-editing engine is deliberately not built for this
   target (no termcap/terminfo, raw serial UART), `bash_syscalls.c`'s
   stand-in `readline()` always returns NULL, which `yy_readline_get()`
   treats as immediate EOF on *every* call, with zero real console reads
   ever happening. That's what fed `handle_eof_input_unit()` ->
   `exit_builtin()` every time `bash -i` tried to read its very first
   interactive line. Fixed by disabling `READLINE` in config.h itself
   (matching the project's own documented scope decision), which in turn
   exposed one small, genuine pre-existing gap in this exact bash-3.2
   checkout under a real `--disable-readline`-style build (never
   previously exercised, since Apple's own build always ships readline
   on): `variables.c` used `no_line_editing`/`rl_prefer_env_winsize`
   unconditionally but only declared them inside `#if defined (READLINE)`
   -- fixed by un-guarding just those two declarations.

Either bug alone would have caused *a* problem, but together they explain
the exact observed symptom precisely: bug 2 made every attempted
interactive read return EOF immediately and unconditionally (explaining
the *high rate* of the flood -- no real blocking read ever happened), and
bug 1 made the resulting `jump_to_top_level(EXITPROG)` fail to actually
unwind back to `reader_loop()`'s loop-exit check (explaining why it never
terminated -- control was landing on stomped register state that
happened, deterministically, to unwind back into another read/EOF/exit
cycle instead). See `docs/historic/roadmap.md`'s newest Phase 4 entry
(search "Twenty-third pass") for the full evidence trail, including the
exact instrumented QEMU transcripts that pinned down each bug in turn.
**`-i` is now the correct, working way to get a real interactive
prompt** -- the earlier "don't use `-i`" guidance is retired.

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

- **`bash -i` loops forever printing `exit`** -- FIXED 2026-08-19 (see
  Current status above and `docs/historic/roadmap.md`'s "Twenty-third
  pass"). Real interactive bash now works correctly.
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
