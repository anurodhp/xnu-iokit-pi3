# Roadmap

This is the short, current-state view of the project's multi-phase plan
(kernel boot -> real SD-rooted, real-command userland -> multi-user ->
networking -> a genuinely usable OS -> package management). For the full
phase-by-phase history -- exact bugs found and fixed, exact commands run,
exact QEMU-verified transcripts -- see `docs/historic/roadmap.md`, which
archives Phases 1-3.5 in full and every completed Phase 4/4.6 sub-item
verbatim, plus (third archive pass, 2026-08-30) the completed Phase
6a/6b/6.5/6.6/9 and Phase 7.5 step 1-3 writeups; this file only summarizes
what's still open, keeping a one-line status pointer per archived item.
`docs/backlog.md`
remains the authoritative, detailed open-bug tracker (P0 = real-hardware
risk, P1 = correctness bugs, P2 = known gaps, P3 = hygiene) -- the "known
bugs" section below just points at it plus a couple of bugs the roadmap
narrative itself flagged as still-open.

## Current status

**Condensed as of 2026-08-21** (detailed per-pass writeups for everything
below archived to `docs/historic/roadmap.md`'s "Second archive pass"
section -- read that for exact bugs found, exact QEMU-verified transcripts,
and exact files touched per command):

- **77 real userland commands built** (up from 65 at the start of this
  round of sweeps): the Tier A sweep (`ls`, `find`, `date`, `gzip`,
  `compress`), the real POSIX-regex-unlocked `grep`/`sed`/`nl`/`xargs`/`ed`
  sweep, the Tier B sweep (`diff`, `tar`, `whoami`, `less`/`more`),
  `df`/`killall` (Phase 4.6 item 8, sysctl/getfsstat/KERN_PROC MIBs), and
  most recently `md5` (Phase 4.6 item 6, real CommonCrypto-shaped digest
  API shim -- see Known stubbings/Remaining future work below) -- all
  real, unmodified Apple/BSD/GNU source, all QEMU-verified. `getty`
  (Phase 5.5 item 1, real `system_cmds/getty.tproj`) also builds/links/
  runs cleanly and is installed at `/bin/getty`, but is deliberately NOT
  wired into the boot/login path yet (needs a separate `login(1)` binary
  this project doesn't build), so it isn't counted among the 77.
- **Interactive shell is real bash** (default login shell, real GNU
  readline -- tab completion, command history -- real termios/tty, real
  default hostname), with the earlier `bash -i` infinite-`exit`-flood bug
  fixed for real.
- **Real hardware milestone**: first confirmed boot on actual Pi 3 silicon
  (previously QEMU-only) -- u-boot reaches `## Starting XNU`, XNU's real
  kernel boot banner comes through over a real serial adapter. HDMI stays
  dark by design (XNU's console path is hardcoded to serial on non-x86_64
  targets, unrelated to whether the framebuffer handoff itself works) --
  investigated and closed, not an open bug.
- **Command priority is now usage-frequency-driven, not source-directory
  order** (reorganized 2026-08-20, see "Remaining future work" below):
  rarely-used utilities are deliberately deferred until Phase 8's package
  manager exists rather than pre-baked into the base image.
- **Real Apple `Libm` pinned 2026-08-27 (tag `Libm-2026`), retiring this
  project's 3 hand-rolled math shims.** `tools/userland_staging/build_libm.sh`
  builds `libc_build/libm.a` — 62 real objects / 111 real functions from
  Apple's real portable `Source/ARM/*.c` subset, with attributed FreeBSD
  `lib/msun/src` fallbacks (`libm_freebsd_fallback.c`) where Apple has no
  usable portable source. This is the one pin here that is **not**
  manifest-verifiable (Apple stopped releasing `Libm` after Lion, 2011),
  documented honestly as such in `setup_third_party.sh`/
  `docs/apple-oss-catalog.md`. Full writeup (per-consumer verification, the
  three shim files it replaces) archived to `docs/historic/roadmap.md`'s
  "Third archive pass, 2026-08-30".

See `docs/historic/roadmap.md` for the full narrative (real bugs found,
exact syscalls/structs involved, per-command QEMU transcripts) behind
every bullet above.

## Known bugs

Currently open, real bugs the roadmap narrative flagged as not-yet-fixed
(see `docs/backlog.md` for the full, authoritative list and detail):

- **`bash -i` loops forever printing `exit`** -- FIXED 2026-08-19. Real
  interactive bash now works correctly. See `docs/historic/roadmap.md`'s
  "Twenty-third pass".
- **`yes` reliably panics the kernel** (`_rtclock_intr` interrupt-processing
  watchdog, under sustained fast syscall load) -- a real kernel bug, not a
  port bug in `yes.c` itself. Ships anyway, flagged. See `docs/backlog.md`
  P0 item 29.
- **Every command run from bash printed an empty `err(3)`/`warn(3)` prefix**
  -- FIXED 2026-08-20. The obvious theory (bash's `execve()` passing an
  empty `argv[0]`) was disproved by a real QEMU A/B instrumentation test;
  the real root cause was one layer down, `third_party/Csu/start.s` never
  writing `__progname`, so `getprogname()` read an unset global for every
  process on this target. Fixed with a real `crt_init_program_vars()` in
  `crt.c`, called from `start.s`. Full evidence trail archived to
  `docs/historic/roadmap.md`'s "Third archive pass, 2026-08-30".
- **Also every command in `build_simple_cmds.sh`'s tier (28 commands as of
  2026-08-19) had a wrong-`getprogname()` bug** (a hardcoded
  `"libc_locale_smoketest"` literal) **that was silently masking the bug
  above** -- fixed in the same pass, same archive section.
- **`hexdump` prints no data rows** (`freopen`+`getc`/`ungetc` idiom bug)
  -- found 2026-08-20, ships anyway. See `docs/backlog.md` P1 item 3.
- **`pathchk ""` doesn't error per POSIX** -- low impact. See
  `docs/backlog.md` P1 item 4.
- **`rs -T` may drop a row on small input** (unconfirmed as a regression
  vs. upstream heuristic sensitivity). See `docs/backlog.md` P1 item 5.

## Known stubbings / stand-ins

Deliberate stand-ins currently in place for a real subsystem, each will
need a real implementation later:

- **`bsd_pthread_stub.c`/`bsd_pthread_workq.c`** (kernel) -- `psynch_*`
  (userspace mutex/cv/rwlock kernel slow path), `bsdthread_terminate`,
  `thread_selfid`, and `bsdthread_register2` (the newer TSD-offset-aware
  registration SPI, genuinely unreachable by design on this project's
  exact xnu-7195 + libpthread-454.80.2 pairing, see Phase 7.5 below)
  still unconditionally return `ENOTSUP`, standing in for Apple-internal
  pthread.kext logic that genuinely was never open-sourced. `
  bsdthread_create` (2026-08-20) and, as of 2026-08-21,
  `bsdthread_register` + all five real workqueue thread-stack hooks
  (`workq_create_threadstack`/`workq_destroy_threadstack`/
  `workq_markfree_threadstack`/`workq_setup_thread`/
  `workq_handle_stack_events`) are now REAL -- the latter six turned out
  to have real, unmodified, already-open-source Apple implementations
  sitting in `third_party/libpthread/kern/` all along (Apple ships
  pthread.kext's kernel-support code as that repo's own `kern/`
  subdirectory), ported into a new `bsd_pthread_workq.c` rather than
  hand-rolled. `bsdthread_create` creates a genuine second Mach thread
  via `pthread_kern->thread_create`/`->thread_set_wq_state64`/
  `->thread_resume`; the workqueue hooks light up the already-real
  `bsd/pthread/pthread_workqueue.c` policy logic (workqueue creation,
  thread-stack lifecycle, real arm64 wqthread register-state setup and
  return-to-userspace). Both QEMU-verified running real code concurrently
  with the caller. Still NOT implemented: the real userspace libpthread
  ABI side (no TSD/dispatch-queue offsets actually populated by any real
  userspace caller on this target yet, no thread port returned for
  `bsdthread_terminate`/`pthread_kill` to target) and `psynch_*` -- not
  `pthread_create()`-compatible yet, though architecturally closer than
  before this pass. See `tools/pthread_kthread_test/` and the newer
  `tools/workq_kthread_test/` for the real, minimal, verified exercises
  of each path, and the Phase 7.5 entry below for the fuller design
  writeup.
- **Real AES (FreeBSD rijndael) + real SHA-256 (corecrypto) + real
  SHA-384/512/MD5 (FreeBSD)** -- DONE 2026-08-27/28 (`docs/backlog.md`
  items 0a/0b). `g_crypto_funcs` now gets a real
  `register_crypto_functions()` call (AES ecb/cbc/ctr only, gated by a
  boot-time FIPS-197/SP 800-38A self-test), so `tcp_tfo_init`'s TCP Fast
  Open path works for real instead of degrading. Still no HMAC/RSA/ECDSA/
  DH/ECDH via this table. Was the blocker for real `sshd` (Phase 7b);
  `pkg`'s `libecc` signature verification (Phase 8) is a separate,
  still-open gap.
- **`cerror_stub.c`** -- minimal errno/`cerror` stand-in used where real
  Apple `errno.c`'s TSD dependency wasn't available yet.
- **Residual no-op `os_once`/`pthread_mutex_*` copies** in
  `libc_stdio_shims.c` -- mostly superseded by the real `libplatform`/
  `libpthread` ports, a couple of harmless duplicates remain where they
  don't collide.
- **`ps_task_info_stub.c`** -- `ps`'s CPU%/RSS/VSZ/thread columns print
  honest zeros instead of real values, standing in for `task_for_pid`/
  `task_info`/`task_threads`/`thread_info` -- still NOT done (Phase 4.6
  item 5's real Mach IPC infrastructure now exists and is QEMU-verified
  for the simpler clock/semaphore routines, but `task_for_pid`'s own MIG
  RPC to the task subsystem wasn't attempted this pass).
- **`simple_cmds_nanosleep_compat.c`/`simple_cmds_nanosleep_pub_compat.c`:
  DONE 2026-08-21** -- both now real, genuinely blocking `nanosleep(2)`/
  `_nanosleep` via real Mach IPC, replacing the prior CPU-burning
  `mach_absolute_time()` spin-wait. See Phase 4.6 item 5 below.
- **`simple_cmds_rpmatch_compat.c`** -- a real but C-locale-only
  `rpmatch()` (used by `rm -i`'s y/n prompt) stands in for real Apple
  `Libc/gen/FreeBSD/rpmatch.c`'s `regcomp`/`regexec`-backed match against
  `YESEXPR`/`NOEXPR`. Real POSIX regex itself is DONE now (Phase 4.6 item
  3, `libc_build/libc_regex.a`, see below) -- this file's own comment
  already documents why a direct first-character check is the exact same
  real behavior in this project's always-C locale, not a degraded
  approximation; left as-is (not rewired to the new regex engine) since
  there's no real behavior difference to gain here. NOT a blocker for
  bash's `[[ str =~ pattern ]]` any more -- see below.
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
- **`chmod`/`chown`: DONE 2026-08-20 (Block 4) for the common mode-bit/
  ownership case** -- this entry's own original premise (real ACLs needed
  even for plain `chmod`) turned out false on inspection: real Apple
  `posix1e/acl.c` was already compiled into `libcopyfile.a` by the `cp`/
  `mv` block, so real unmodified `chmod.c`/`chmod_acl.c` link against it
  with one small new stand-in (`chmod_mbr_compat.c`, reached only by the
  ACL-by-name path). What's still genuinely unverified: real ACL-specific
  flags (`-N`/`+a`), which need confirming as honest `ENOTSUP`/`EINVAL`
  failures specifically (HFS+ has no real ACL storage on this target), and
  `<membership.h>` UUID-based lookups remain unbuilt (no directory
  service). Full writeup archived to `docs/historic/roadmap.md`'s "Third
  archive pass, 2026-08-30".
- **Bash's own remaining honest failure stand-ins** (`bash_syscalls.c`):
  `dlopen`/`dlclose`/`dlsym`/`dlerror` always fail (no dyld at all);
  `alarm`/interval timers are silent no-ops (no `SIGALRM` subsystem);
  real networking (`netopen`/`netconn`) is unavailable; `~user`/`%group`/service-name tab-completion candidates are
  always empty (`setpwent`/`getpwent`/`setgrent`/`getgrent`/
  `setservent`/`getservent` honest "no enumeration API" stand-ins --
  `~`/literal paths still complete normally). `tcgetattr`/`tcsetattr`/
  `tcgetpgrp`/`tcsetpgrp`/`ttyname`/`isatty`/readline line-editing and
  programmable completion are now REAL (2026-08-19/20, see Current
  status above) -- this line is intentionally NOT listed here anymore.
- **INET/INET6 domain registration disabled** in the kernel (along with
  MULTIPATH/flow_divert/IGMP/MLD) -- no network interface exists yet; a
  scope decision documented in `architecture.md`, revisited once Phase 6
  lands.
- **`libmalloc_vm_mig_stubs.c`** -- real but provably-unreachable
  dead-code MIG-fallback bodies (`KERN_FAILURE`) behind `libmalloc`'s real
  Mach-trap fast path; harmless, kept for completeness only.
- **`dyld`** -- real dyld-940 boots, runs a real dynamically-linked
  program's `main()`, and exits cleanly with no crash (Phase 6.5 Phase
  C+D, fully closed 2026-08-25). Real architecture debt remains:
  `libSystem.B.dylib`/`libdyld.dylib` still bundle part of this project's
  other libc/libpthread stack as flattened static archives rather than
  real, separate per-library dylibs. Migration steps 1-5 are DONE (see
  Phase 6.5 below); see `docs/daemon-roadmap.md`'s "Static-to-dylib
  migration" sections and `docs/backlog.md` items d8/d12 for exactly
  what's left.
- **`libdispatch`/GCD** -- a real `libdispatch.dylib` exists and is
  linkable (Phase 6.6/6.7, 2026-08-26/27; `docs/backlog.md` item 17);
  `syslogd.macho` links it dynamically and runs to completion against it
  with zero undefined symbols and no crash. Real concurrent
  `dispatch_async` still can't exist until real threading does (Phase
  7.5's pthread ceiling), so this remains single-thread-honest in
  practice -- but "zero API surface, not attempted" is no longer accurate.
- **`ed_setjmp_raw.s`/`ed_setjmp_compat.c`** (Phase 4.6 grep/sed/nl/
  xargs/ed sweep) -- real AAPCS64 register-save/restore core for
  `setjmp`/`longjmp`/`sigsetjmp`/`siglongjmp`, standing in for Apple's
  real `libplatform` arm64 asm (which needs a genuinely missing
  `__sigunaltstack` real dependency, not just a missing header -- not
  vendored anywhere in this checkout). Two honest, narrow, deliberate
  omissions from Apple's version: no pointer-authentication ("PAC")
  munging of saved fp/lr/sp (a memory-corruption-hardening step, a
  no-op-equivalent omission on this plain non-PAC arm64 target, not a
  behavior change), and no `sigaltstack(2)` status preserved across
  `siglongjmp` (this project has no `sigaltstack(2)` caller anywhere
  else either). Real signal-mask save/restore for the `sig-` variants
  IS implemented (via the already-real `sigprocmask()`).
- **`ed_popen_system_compat.c`** -- real `popen()`/`pclose()`/`system()`
  via the classic pre-`posix_spawn` `pipe()`+`fork()`+`execve()`
  algorithm, standing in for Apple's real `popen.c`/`system.c` (both
  need `posix_spawn`, an already-deferred whole subsystem -- see
  `install`/`df` above). Functionally complete for `ed`'s own real
  usage (`!cmd`, `r !cmd`, `w !cmd`); bounded to 8 concurrent `popen()`
  handles (a fixed table, real classic BSD implementations used a
  similarly bounded table before switching to a linked list -- more
  than `ed` itself ever needs concurrently).
- **`gz_gzip_impl.c`/`gz_crc32_compat.c`** (grep's `-Z`/`.gz` support) --
  real DEFLATE decompression via the real, unmodified zlib core vendored
  at `third_party/u-boot/lib/zlib`, but only the narrow `gzdopen()`/
  `gzread()` read-side subset grep itself calls; no compression side, no
  general-purpose `zlib.h` public API. See item 10 in "Remaining future
  work" below for what this still blocks (`gzip`/`compress` as
  standalone commands).

## Remaining future work

- **Phase 4 (userland)**: interactive bash, real termios/tty, and real
  readline (tab completion + history) are all DONE (see Current status
  above). 60 real command binaries now build/link/run QEMU-verified
  (step 3), up from 28 as of this same day's earlier "Twenty-fourth
  pass" -- see Current status above for the full added list. Phase 4.6
  item 4 (`setattrlist(2)` userland wiring) is now DONE for real
  (Block 1, cp -p's real `lchmod`/`lchflags` path), not just touch -h's
  narrow `lutimes` case. Remaining: multi-candidate Tab completion and
  vi-mode editing are unverified (not known-broken, just not exercised
  yet); `~user`/`%group`/service-name completion candidates are always
  empty (no real account/group enumeration API, see Known stubbings);
  Phase 4.6 item 3 (real POSIX regex) is now DONE and QEMU-verified
  (2026-08-20, see below); item 5 (real Mach IPC) is **PARTIALLY DONE
  2026-08-21** -- the blocking-`nanosleep(2)` half is real and
  QEMU-verified, the `ps` task-stats half (`task_for_pid`/`task_info`)
  was not attempted this pass, see item 5 below; real Apple `Libinfo` (the
  actual repo, distinct from the working custom flat-file resolver) is
  still unbuilt. Separately worth profiling: bash.macho's first-touch
  demand-paging takes ~90s on this target's mostly-polled SD read path
  -- slow but not a correctness bug.
  **The command-sweep's own natural stopping point (2026-08-20, after
  six blocks/32 new commands in one session)**: every remaining
  `shell_cmds`/`file_cmds`/`text_cmds` candidate surveyed needs a real
  subsystem this project hasn't built at all, not just one or two more
  syscalls. Tracked as discrete Phase 4.6-style work items below (continuing
  that section's existing 1-5 numbering) so each can be picked up as its
  own dedicated pass, matching this file's own standing guidance not to
  rush a real architectural gap into a command-sweep pass:
  5. **[PARTIALLY DONE 2026-08-21]** Real Mach IPC (`mach_msg()` trap +
     MIG RPC) in userland -- the kernel side was already real
     (`osfmk/kern/ipc_*`/`clock.c`), just never exercised from a userland
     binary. **Blocking `nanosleep(2)`/`_nanosleep` is DONE**
     (`tools/userland_staging/build_libmach_ipc.sh` →
     `libc_build/libmach_ipc.a`: real `mach_host_self()` →
     `host_get_clock_service()` → `semaphore_create()` → per-call
     `clock_get_time()` + `__semwait_signal()`, with real `mig`-generated
     client stubs and two hand-written arm64 trap stubs). QEMU-verified
     three independent ways, including catching the guest PC at
     `_processor_idle+0x168` during the sleep window, confirming genuine
     descheduling rather than the spin-wait it replaced.
     **`ps`'s CPU%/RSS/VSZ/thread-count columns (`task_for_pid`/
     `task_info`/`task_threads`/`thread_info`, `ps_task_info_stub.c`) are
     NOT done** -- not attempted, still printing honest zeros;
     `task_for_pid`'s own MIG RPC to the task subsystem is the separate,
     larger follow-on. Full writeup archived to
     `docs/historic/roadmap.md`'s "Third archive pass, 2026-08-30".
  6. **[DONE 2026-08-21]** CommonCrypto-shaped digest API for `md5`. Real,
     unmodified `text_cmds/md5/md5.c` builds and links against a new
     `third_party/CommonCrypto` pin (`CommonCrypto-60178.40.2`, confirmed
     against the `macos-112` manifest). Real CommonCrypto's own
     `CommonDigest.c` glue was unusable — it calls corecrypto digest cores
     that are confirmed NOT open-sourced anywhere in this checkout — so
     `md5_commoncrypto_shim.c` + `md5_rfc1321_core.c` (a userland port of
     this kernel's own already-boot-verified RFC 1321 implementation)
     stand in. QEMU-verified: `md5 -x` printed all 8 RFC 1321 reference
     vectors as verified, and `-s`/file/stdin paths matched this host's
     own `/sbin/md5` byte-for-byte. `sha1sum`/`shasum`-class follow-on not
     attempted. Full writeup archived to `docs/historic/roadmap.md`'s
     "Third archive pass, 2026-08-30".
  7. **`posix_spawn(2)`** -- an unbuilt process-creation API; blocks
     `install`, which also needs its own real `strip(1)` subprocess helper
     once spawning works.
  8. **[DONE 2026-08-21]** `sysctl(2)` was already real and
     general-purpose here (`tools/userland_staging/libc_gen_syscalls.c`'s
     `sysctl()`/`sysctlbyname()`, `SYS_sysctl` = 202); `df` and `killall`
     are now both built and QEMU-verified against it. `w`/`who` still need
     this same `KERN_PROC` MIB plus `utmp`/`wtmp` (item 11 below).
  9. **Real System V IPC** (`semget`/`shmget`/`msgget` family) -- blocks
     `ipcs`/`ipcrm`. `dd`'s more involved I/O-conversion logic wasn't
     surveyed in depth either; group it here for a first look.
  10. **[DONE 2026-08-20]** zlib compression (deflate side) landed with the
      Tier A sweep's `gzip` (`gz_deflate_impl.c`, real
      `deflateInit2()`/`deflate()` against the same vendored zlib core the
      earlier inflate-only `gz_gzip_impl.c` used) and `compress` (a
      separate, self-contained LZW codec, no zlib dependency). Both
      QEMU-verified real round-trip -- see `docs/historic/roadmap.md`.
  11. **Real `utmp`/`wtmp`/`lastlog` accounting file format + writers** --
      blocks `who`/`w`/`lastcomm` (distinct from item 8's `sysctl`
      dependency; both are needed for `who`/`w`).

  **Deprioritized 2026-08-21 (moved down for completeness while items 5/6
  are actively worked by background agents):** none of the remaining
  Phase 4 items below block 5/6 or each other, and none are in progress.
  Revisit after 5/6 land. This is a re-statement/pointer, not a
  duplicate work item list -- see each original item above for detail.
  - Item 7 (`posix_spawn(2)`, blocks `install`/`strip`).
  - Item 9 (real System V IPC, blocks `ipcs`/`ipcrm`; `dd`'s I/O-
    conversion logic ungrouped).
  - Item 11 (`utmp`/`wtmp`/`lastlog`, blocks `who`/`w`/`lastcomm`).
  - Tier B commands (`diff`, `less`/`more`, `tar`, `whoami`) -- needs new
    `setup_third_party.sh` source pins first.
  - Multi-candidate Tab completion and vi-mode editing (bash) --
    unverified, not known-broken.
  - `~user`/`%group`/service-name bash completion -- needs real account/
    group enumeration (no directory service).
  - Real Apple `Libinfo` (the actual repo, distinct from the working
    custom flat-file resolver).
  - `su`'s own design-judgment note (not a subsystem gap -- needs care
    against `init.c`'s existing `do_login()`/`cmd_su()`, not a naive
    port).

  `xargs`/`nl`/`grep`/`sed`/`ed` needed item 3 (POSIX regex), now DONE
  -- all five DONE and QEMU-verified as their own pass 2026-08-20 (see
  Current status below). `sort` additionally needs real pthread
  multithreading, which is Phase 7.5's permanent closed-source-shaped
  ceiling (`bsd_pthread_stub.c`, see Known Stubbings) -- deliberately
  NOT attempted in that pass, still open.
  `su` isn't a subsystem gap at all -- it needs careful design judgment
  against this project's own existing `do_login()`/`cmd_su()` in `init.c`
  rather than a naive port, flagged for whoever picks it up rather than
  filed as a work item.
  **Command priority reorganized 2026-08-20: stop chasing whatever's
  next in a source directory, prioritize by how commonly a real user
  would actually reach for the command, defer genuinely obscure ones
  until Phase 8's package manager exists** (once `pkg` lands, a rarely-
  used utility can be installed on demand rather than needing to be
  pre-baked into the base image forever). The 65 commands built so far
  are NOT being removed -- several (`rs`/`lam`/`colrm`/`vis`/`unvis`/
  `apply`/`jot`/`hexdump`) are genuinely niche and were simply next-in-
  list when built; this reorganization only changes what gets picked up
  *next*, prompted by `ls` -- one of the single most-used Unix commands
  -- turning out to have been skipped entirely across 65 other ports.
  - **Tier A -- [DONE 2026-08-20, see Current status above] `ls`,
    `find`, `date`, `gzip`, and (bonus) `compress`** all now real,
    unmodified source, built, and QEMU-verified (70 total commands).
    `ls` was the top-priority item (real directory listing didn't exist
    in this project at all before this pass, every invocation 404ing as
    a missing command). `gzip` needed a genuine new DEFLATE
    (compression) core (`gz_deflate_impl.c`) to pair with the
    grep/sed/nl/xargs/ed sweep's already-real INFLATE-only
    `gz_gzip_impl.c` -- both sides of real zlib are wired now, not just
    decompression.
  - **Tier B -- common, but no real source cloned into this checkout
    yet:** `diff`, `less`/`more` (a pager -- neither exists in any
    currently-pinned repo, confirmed by grep, not assumed), `tar`,
    `whoami` (no standalone real source found either; `id -un` already
    covers the same information via an already-built command, so this
    is genuinely low priority even within Tier B). Needs the same
    `setup_third_party.sh` pinning work Phase 5.5's `syslogd`/`cron`
    items already need -- confirm the exact apple-oss-distributions repo
    name/tag against the `macos-112` manifest before cloning, not
    guessed.
  - **Deferred to Phase 8 (package manager) or later:** every other
    unbuilt `shell_cmds`/`file_cmds`/`text_cmds` entry not already
    listed as a Phase 4.6 work item above (regex/pthread/crypto/
    `posix_spawn`/`sysctl`-MIB/SysV-IPC/`utmp`-blocked commands stay
    tracked there on their own merits, independent of this
    reprioritization) -- things like `w`/`who`/`lastcomm`/`ipcs`/
    `ipcrm`/`mtree`/`pax`/`shar`/`rmt`/`col`/`ul`/`look`/`banner`/`pr`.
    Not abandoned, just not worth pre-baking into the base image ahead
    of a real package manager existing to install them on demand.
- **Phase 5 (multi-user)**: the custom account resolver, real DES
  `crypt()` password verification, and a real `login`/`su` (wired into
  `tools/init_binary/init.c`'s `do_login()`/`cmd_su()`, real bash now the
  default login shell -- see Current status above) are all done. Still
  needed: account-provisioning tools (`pwd_mkdb`/`vipw`/`chpass`/
  `passwd` -- `pwtool` covers an ad hoc subset of this today, not the real
  tools; real, unmodified Apple source for all four already sits unbuilt
  in `third_party/system_cmds/` -- `pwd_mkdb.tproj`, `vipw.tproj`,
  `chpass.tproj`, `passwd.tproj` -- confirmed present, not yet attempted),
  a setuid-root helper so `su` can reach a different account from a
  non-root session, and eventually PAM (OpenPAM as fallback). `sudo` is
  deliberately out of scope indefinitely.
- **Phase 5.5 (standard system daemons, filed 2026-08-20, deliberately
  scheduled before Phase 6)**: a "genuinely usable OS" needs a handful of
  standard background daemons this project has never planned for, found
  while auditing what's missing beyond individual commands. Scoped for
  *before* networking specifically because all three are purely local/
  single-host concerns -- unlike `mDNSResponder`/`configd`-style network-
  management daemons, which inherently need Phase 6 to exist first and so
  are NOT filed here, they belong with/after Phase 6 instead.
  1. **[DONE 2026-08-21, build/run only]** `getty` (real, unmodified
     Apple source, `third_party/system_cmds/getty.tproj`) now builds,
     links, and runs without crashing (see Current status above for the
     real TSD-bootstrap segfault this pass found and fixed). Still NOT
     wired into the actual boot/login path -- this project's login today
     remains a single console session with `do_login()` baked directly
     into `tools/init_binary/init.c`'s own respawn loop, not a separate
     per-tty daemon. Real `getty` only starts mattering once there's more
     than one real tty to spawn onto (real hardware's serial console
     today, potentially real PTYs later), and wiring it in for real needs
     a separate `login(1)` binary this project doesn't build yet (own
     undertaking, comparable in scope to the still-open `pwd_mkdb`/`vipw`/
     `chpass`/`passwd` item under Phase 5) -- low urgency alone, deferred
     rather than rushed.
  2. **`syslogd`**: no real Apple source cloned yet -- needs adding to
     `setup_third_party.sh`'s pinned list first (confirm the exact
     apple-oss-distributions repo name/tag against the `macos-112`
     manifest, same method every other pin here used, not guessed).
     Right now every diagnostic in this project goes straight to
     `kprintf`/the console; a real syslog would give commands a real
     place to log to.
  3. **`cron`: source pinned, real binaries build, daemon shelved
     2026-08-22 -- a real, unresolved startup crash, not a scoping gap.**
     Real `at`/`atrun` (task scheduling, cron-adjacent) still sit unbuilt
     at `third_party/system_cmds/at.tproj`/`atrun.tproj`, confirmed
     present, untouched by this pass. The full `cron` daemon's real repo
     is pinned (`apple-oss-distributions/cron`, tag `cron-40`) and real,
     fully-static `cron.macho`/`crontab.macho` build cleanly
     (`tools/userland_staging/build_cron.sh`, new) -- but `cron.macho`
     itself SIGSEGVs at startup (real bug, bisected but not root-caused;
     `crontab.macho` has two separate real bugs fixed and
     bisection-confirmed but never got a full clean end-to-end QEMU run).
     Neither binary is installed into the SD image or spawned at boot
     right now -- `tools/init_binary/init.c`'s `spawn_cron()` is written
     and correct but its call site is disabled, and
     `inject_into_sd_image.sh`'s install blocks for both binaries are
     removed. Nothing is lost: `docs/daemon-roadmap.md`'s Phase 5.5 item
     3 entry has the complete bug-by-bug writeup (three real runtime
     bugs found, two fixed, one open) and exact resume instructions for
     whoever picks this back up.
  4. **`ntpd`** (added 2026-08-22, see `docs/daemon-roadmap.md`): real repo
     `apple-oss-distributions/ntp`, also pure BSD/POSIX C, no new
     infrastructure needed beyond Phase 6b's already-landed UDP/INET work.
- **Phase 6 (networking)**: **6a DONE 2026-08-21** -- the DWC2 USB host
  controller driver (`iokit/BCM2837DWC2/`), device enumeration and the hub
  protocol subset are implemented and QEMU-verified (a real descriptor read
  from the root-port device, plus a second device enumerated through a
  downstream hub port -- the exact topology the LAN9514 presents). Testing
  USB at all first required backporting a DWC2 model into
  `third_party/qemu-zhuowei`, a prerequisite the plan doc had wrongly
  assumed away. See `docs/historic/usb-support-plan.md`'s STATUS section.

  **6b DONE 2026-08-22, and CONFIRMED on real hardware the same day** -- in
  three real-hardware-driven passes: (1) a root-port `HFIR` write in
  `portReset()` was silently corrupting the bus cadence for anything
  reached through the hub (invisible to QEMU, whose dwc2 model never reads
  HFIR back) -- removing it let `0424:ec00` enumerate on real silicon for
  the first time; (2) `SMSC9514USBTransport::probe()`, a read-only
  self-test, confirmed the SMSC vendor-request register protocol against
  real silicon -- the one layer QEMU can never verify in any version; (3)
  IONetworkingFamily was vendored into the kernel build for real, and
  `SMSC9514Ethernet::start()` was wired to actually `attachInterface()`
  (previously missing entirely). Result on real hardware: `en0` attaches,
  link comes up, and the interface responds to ICMP echo over a real
  Ethernet link -- real L2/driver plus basic L3 end to end on silicon (no
  DHCP/address assignment; see 6c below). The INET half is DONE on branch
  `phase-6b-inet`, where this work also landed.

  Full writeup -- all three passes, the seven real integration gaps the
  live kernel build surfaced (including the `classq.h`/`flowadv.h`/
  `TargetConditionals.h` finds and the real lesson about broad `-I`
  additions silently shadowing `libkern/os/base.h`), and the measured
  12-file IONetworkingFamily scoping that preceded it (including the
  correction that its own predicted "genuine vintage skew" list was a
  trial-harness artifact) -- archived to `docs/historic/roadmap.md`'s
  "Third archive pass, 2026-08-30".

  **6c, revised 2026-08-22: DHCP deprioritized, not planned for now** --
  manual static IP assignment via `ifconfig` is sufficient for this
  project's actual needs, so a DHCP client (`dhclient` vs. Apple's
  `bootp`/`ipconfigd` -- see `project_xnu_rpi3_dhcp_ipconfiguration_analysis`
  memory for the full analysis of why `dhclient` would've been the pick)
  is shelved as throwaway work until an actual need for it appears.
  `ifconfig` itself is **DONE** (`e92ab20`, real unmodified Apple
  `network_cmds/ifconfig.tproj`, QEMU-verified against `lo0` -- manual
  static-IP assignment on real `en0` not yet separately confirmed but
  uses the same already-working code path). Real Apple source for
  `ping`/`ping6`/`netstat`/`arp`/`route` all sit unbuilt at
  `third_party/network_cmds/{ping,ping6,netstat,arp,route}.tproj` --
  natural next candidates for basic network usability now that manual
  addressing is the plan.
- **Phase 6.5 (dyld): DONE 2026-08-25.** Started that day as an explicit
  user override of the prior "defer until Phase 6 forces it" sequencing,
  motivated by unlocking a future PureDarwin `launchd` port. Phases A/B/C/D
  are all closed: real dyld-940 boots, runs a real dynamically-linked
  program's real `main()` (loading real `libdyld.dylib` +
  `libSystem.B.dylib`), and exits cleanly with no crash. The follow-on
  static-to-dylib migration's steps 1-5 are also DONE and QEMU-verified
  (`libsystem_kernel`, `libsystem_platform` + `libsystem_blocks`,
  `libsystem_malloc` + `libsystem_pthread`, `libsystem_c`, and `libc++`
  with libcxxabi deliberately combined for RTTI-identity correctness) --
  `libdyld.dylib`/`libSystem.B.dylib` now link 7 real dylibs directly (10
  transitively) instead of bundling static archives. Each step's own audit
  caught real duplicate-global-state bugs before they could crash (`errno`;
  libmalloc's heap-zone pointer and libpthread's thread registry; three
  more lurking in an old loose compat file). Real, honestly-scoped
  exclusion: `std::thread`/`std::condition_variable`/`std::timed_mutex`
  stay unusable until real `pthread_join`/`pthread_detach`/
  `pthread_cond_timedwait` exist. See `docs/daemon-roadmap.md`'s Phase 6.5
  + "Static-to-dylib migration" sections and `docs/backlog.md` items d8/d12
  for what's left; full roadmap-side writeup archived to
  `docs/historic/roadmap.md`'s "Third archive pass, 2026-08-30".
- **Phase 6.6 (libdispatch/GCD): DONE 2026-08-26/27** -- real
  `libdispatch.dylib` ported, compiled, converted to a dylib, then had its
  full remaining undefined-symbol gap closed (`3fab93c`/`7629e79`), driven
  by real `syslogd`'s own dispatch-queue usage as the first consumer. Real
  concurrent `dispatch_async` still can't exist until real threading does
  (Phase 7.5's ceiling), exactly as this entry originally predicted. See
  `docs/backlog.md` item 17 and `docs/daemon-roadmap.md`'s Phase 6.6/6.7.
- **Phase 9 (CoreFoundation) + real ICU + real libxml2: DONE
  2026-08-26/27.** Real Apple CF via swift-corelibs-foundation (95 real
  files, up from an original 17 after a real "just try compiling it" sweep
  disproved the assumption that RunLoop/Stream/Socket/MachPort/PlugIn
  needed new infrastructure); real ICU 66.1 through a genuine two-stage
  host + `arm64-apple-ios14.4` cross build (six real build-system gaps
  found and fixed, none requiring faked behavior); real libxml2-34.9 as a
  per-library dylib, closing `CFXMLInterface.c` with zero new gaps. curl
  was investigated and deliberately NOT pursued (needs closed-source
  Secure Transport for TLS plus socket syscalls this project lacks). CF is
  down to four genuine, individually verified exclusions -- see
  `docs/backlog.md` item d7 for those and for the current remaining-gap
  list.
- **Real syscall + export-parity gap-closing on CF's trial link: DONE
  2026-08-27**, 100 → 42 undefined-symbol groups across four passes: the
  BSD socket / `mach_port_*` / `mk_timer_*` / `pthread_attr_*` families
  (100 → 73); real `__proc_info` plus a new `cctools-973.0.1` pin, matched
  against this project's pinned Xcode 12's real `otool --version` rather
  than the `macos-112` manifest (73 → 70); and the single biggest
  structural fix of the whole effort -- `build_libsystem.sh` was
  plain-linking its 7 real per-library sub-dylibs instead of
  `-Wl,-reexport_library`ing them, so `libSystem.B.dylib` exported almost
  nothing through the umbrella (70 → 42, closing the entire pthread
  export-parity bucket in one rebuild). Also confirmed along the way, as a
  real answer rather than a fix: `sysdir_*` was never open-sourced by
  Apple anywhere (full `Libc` git history plus an org-wide GitHub code
  search), same category as ASL. Full per-pass writeups archived to
  `docs/historic/roadmap.md`'s "Third archive pass, 2026-08-30".
- **Phase 6.8 (`launchd`+XPC via PureDarwin): critical-path gap-closing
  pass DONE 2026-08-26**, real port itself still open. Source is
  PureDarwin/PureDarwin's own `next` branch (a current, actively
  maintained monorepo that already boots real launchd on this exact SoC
  -- see `docs/daemon-roadmap.md`'s Phase 6.8 section for the full
  source-discovery story superseding the earlier, frozen
  `PureDarwin/XPC` reference). Real libdispatch ported and converted to
  a dylib; `launchd`'s own daemon source (`core.c`/`launchd.c`) reached a
  real trial-compile with `core.c` down to a handful of distinct errors;
  a dedicated critical-path pass this session closed the userland
  libc/Mach undefined-symbol gap from 183 to 109 (`docs/backlog.md` item
  d6's "FINAL STATUS"). Remaining gaps are all categorized: genuine scope
  walls (ICU, ASL), deprioritized BSM audit, `libSystem.B.dylib`
  export-parity debt, and one confirmed genuinely-unimplemented feature
  (multi-instance job-domain forwarding). Not yet attempted: the actual
  PID-1-replacement daemon logic beyond the client-library trial-compile.
- **Phase 6.7 (Mach IPC completion + `notifyd`), Phase 10 (Tier 2/3
  daemons: real `configd`, `DiskArbitration`, `mDNSResponder`,
  `PowerManagement`) -- scoped 2026-08-22, not started.** User has
  confirmed `dyld`/`launchd`/XPC are real long-term intents. Full daemon
  inventory, phasing rationale, and per-phase caveats live in
  `docs/daemon-roadmap.md`. Immediate actionable next step that needs
  none of this infrastructure: Phase 5.5 items 3-4 (`cron`/`ntpd`) above.
- **Phase 7 (usable OS)**: none of 7a-7d started. 7a (DNS via `libresolv`)
  is small and clean once Phase 6 lands. 7b (`sshd` via OpenSSH)'s kernel
  AES gate is now closed (2026-08-28, `docs/backlog.md` item 0a):
  `g_crypto_funcs` gets a real `register_crypto_functions()` call with a
  FreeBSD `sys/crypto/rijndael/`-backed ecb/cbc/ctr AES, boot-time
  KAT-self-tested (corecrypto's own AES core is confirmed absent from
  apple-oss-distributions, so the "Apple first" rule couldn't apply here
  either way). Whether OpenSSH's own userland crypto needs anything
  further from this kernel-side table (vs. bundling its own) is still
  open. 7c (self-hosting
  toolchain) recommends Apple's old `gcc_42` over `clang` for feasibility,
  plus `cctools`. 7d (`vim`) is small and can fold into whichever of 7a-7c
  lands first.
- **Phase 7.5 (closed-source ceilings)**: a living list, not a phase to
  "complete" -- real pthread multithreading and real corecrypto-backed
  crypto are permanent gaps short of a from-scratch reimplementation of a
  closed Apple subsystem (see Known Stubbings above).
  - **Step 1: real thread creation -- DONE + QEMU-verified 2026-08-20.**
    Confirmed independently that no `pthread_synch.c`-style kernel
    *implementation* exists in `third_party/xnu-7195` — only the thin,
    real, open-source `bsd/pthread/pthread_shims.c` a closed pthread.kext
    would have plugged into. `real_bsdthread_create` uses that
    `pthread_kern` callback table as designed (`thread_create` →
    `thread_set_wq_state64` → `thread_resume`), landing the new thread on
    the same already-proven `thread_bootstrap_return` →
    `arm64_thread_exception_return` path every `exec()` here already uses.
    Verified by `tools/pthread_kthread_test/` across two full boot + login
    + regression-sweep sessions.
  - **Step 2: real workqueue thread-stack hooks + `bsdthread_register` --
    DONE + QEMU-verified 2026-08-21.** A major correction to step 1's own
    framing, found by actually reading `third_party/libpthread/kern/`
    instead of trusting the inherited "never open-sourced" assumption: all
    five `workq_*` hooks *and* `bsdthread_register` have real, unmodified,
    already-open-source Apple implementations there (Apple ships
    pthread.kext's kernel-support code as that repo's own `kern/`
    subdirectory). Ported into `bsd/kern/bsd_pthread_workq.c` rather than
    hand-rolled, lighting up the already-real
    `bsd/pthread/pthread_workqueue.c` policy logic. Verified by
    `tools/workq_kthread_test/` across two full boot + 37-command
    regression sessions, and adversarially self-reviewed before commit.
    `bsdthread_register2`/`psynch_*`/`bsdthread_terminate`/`thread_selfid`
    remain untouched ENOTSUP stubs by design.
  - **Step 3: `ulock_wait`/`ulock_wake` as a real, non-psynch
    synchronization path -- DONE + QEMU-verified 2026-08-24.** Rather than
    reverse-engineering the never-open-sourced psynch wire protocol (step
    2's standing "real next step"), this found a real shortcut:
    `bsd/kern/sys_ulock.c` is real, unmodified, already-open-source Apple
    code, was already compiled into this kernel, and has zero dependency
    on the closed `pthread_functions` vtable — and `third_party/libpthread`
    already ships a matching Apple-documented `PTHREAD_MUTEX_USE_ULOCK=1`
    opt-in fast path. Verified by `tools/ulock_kthread_test/` (two real
    boots), with an adversarial self-review catching a real latent
    32-vs-64-bit `lockword` type mismatch before commit.
    **Still open from step 3**: `psynch_rw_*` (rwlocks) has no such
    shortcut and would still need the original, harder
    reverse-engineer-the-protocol approach; a real minimal
    libpthread-ABI-compatible userspace `pthread_create()` still needs real
    TSD/dispatch-queue-offset wiring end to end (the kernel-side plumbing
    exists and is exercised, but no real userspace caller passes real
    offsets yet); and `workq_kernreturn`'s other real op codes
    (`WQOPS_THREAD_RETURN`/`_KEVENT_RETURN`/`QUEUE_REQTHREADS2`) remain
    unexercised, since the test's worker thread spins rather than really
    parking back into the kernel.

    Full writeups for all three steps -- exact `pthread_kern` call
    sequences, the real `.version` vtable-slot correction, the real arm64
    wqthread register convention, the hand-derived `pthread_priority_t`
    `0x8FF` (`THREAD_QOS_LEGACY`) constant `tools/workq_kthread_test/`
    cites, and every verification transcript -- archived to
    `docs/historic/roadmap.md`'s "Third archive pass, 2026-08-30".
- **Phase 8 (`pkg` package management)**: fully planned, not started.
  `freebsd/pkg` is the chosen vehicle (native Mach-O awareness, real
  custom-repo support already documented). Needs `libsqlite`/`libucl`/
  `libelf`/`libyxml`/`libder`/`picosat` ported first (low risk), then
  `libecc` (shares Phase 7.5's/7b's crypto gap) and `libfetch` (needs
  Phase 6), then `liblua`, before a real custom package repo can be built
  and consumed on-device.
