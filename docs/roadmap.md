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

See `docs/historic/roadmap.md` for the full narrative (real bugs found,
exact syscalls/structs involved, per-command QEMU transcripts) behind
every bullet above.

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
- **Every command run from bash printed an empty `err(3)`/`warn(3)` prefix**
  -- FIXED 2026-08-20. Originally suspected to be bash's own `execve()`
  call passing an empty `argv[0]` to the child (found 2026-08-20 while
  QEMU-verifying `cp`/`mv`, Block 1); a real QEMU A/B instrumentation test
  disproved that theory -- both bash's own `args[0]` right before
  `execve()` and the spawned child's own `argv[0]` (read directly in its
  `main()`) were already correct. The real root cause was one layer
  further down: `third_party/Csu/start.s`'s arm64 entry point (this
  project's static/no-dyld target never runs the C `_start()` in
  `crt.c`, which is dead code here -- see that file's own
  `crt_init_program_vars()` comment) never wrote `__progname` (only
  `environ`, added by the earlier Phase 4.6 item 2 fix), so
  `getprogname()` read back an unset global for every process on this
  target, unconditionally -- not a bash-specific bug at all. Fixed by
  adding a real `crt_init_program_vars()` to `crt.c` (computes
  `__progname`/`NXArgc`/`NXArgv` from `argv[0]`, matching Apple's own
  dead `crt_basename()` logic) and calling it from `start.s` right after
  the existing `environ` store. QEMU-verified via a real transcript:
  `cp`/`mv`/`cat` against nonexistent paths now correctly prefix their
  own name (`cp: /nonexistent-src-file: No such file or directory`), zero
  regression (login, `echo`, `pwd` all still work). See
  `docs/historic/roadmap.md`'s Phase 4 entry for the full evidence trail.
- **Also every command in `build_simple_cmds.sh`'s tier (28 commands as
  of 2026-08-19) had a wrong-`getprogname()` bug** (hardcoded
  `"libc_locale_smoketest"` literal) **that was silently masking the
  `__progname`-never-initialized bug fixed above** -- the
  `getprogname()` fix itself landed as part of the same Block 1 pass,
  see `docs/historic/roadmap.md`.
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
  `task_info`/`task_threads`/`thread_info` -- still NOT done (Phase 4.6
  item 5's real Mach IPC infrastructure now exists and is QEMU-verified
  for the simpler clock/semaphore routines, but `task_for_pid`'s own MIG
  RPC to the task subsystem wasn't attempted this pass).
- **`simple_cmds_nanosleep_compat.c`/`simple_cmds_nanosleep_pub_compat.c`:
  DONE 2026-08-21** -- both now real, genuinely blocking `nanosleep(2)`/
  `_nanosleep` via real Mach IPC (`host_get_clock_service` +
  `semaphore_create` + `clock_get_time` + `__semwait_signal`), replacing
  the prior CPU-burning `mach_absolute_time()` spin-wait. See Phase 4.6
  item 5 below for the full writeup.
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
  ownership case** -- this entry's own premise (real ACLs needed even
  for plain `chmod`) turned out false on inspection: real Apple
  `posix1e/acl.c` (`acl_get_file`/`acl_set_file`/etc) was already
  compiled into `libcopyfile.a` back in the `cp`/`mv` block (real
  `copyfile()`'s own ACL-preserving-copy path needed it), so real,
  unmodified `chmod.c`+`chmod_acl.c` link and run against that already-
  built archive with only one small new stand-in
  (`chmod_mbr_compat.c`'s `mbr_identifier_to_uuid()`, reached only by
  the ACL-by-name path, not plain mode bits). QEMU-verified: `chmod
  600`/`755`/`+x` and `chown` by name/numeric-id/nonexistent-user all
  independently confirmed correct. What's still genuinely unverified:
  real ACL-specific flags (`-N`/`+a`) -- HFS+ has no real ACL storage on
  this target, so those need confirming as honest `ENOTSUP`/`EINVAL`
  failures specifically, not assumed to "just work" the way the common
  case does. `<membership.h>` UUID-based lookups remain unbuilt (no
  directory service at all, same gap `copyfile_syscalls.c`'s `mbr_*()`
  stand-ins already document) -- only reachable from ACL-by-name, not
  plain `chmod`/`chown`.
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
- **`dyld`** -- not built or exercised at all; every binary in the project
  is fully static, zero `.dylib` output anywhere (Phase 6.5).
- **`libdispatch`/GCD** -- zero API surface, not attempted at all (Phase
  6.6).
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
     MIG RPC) in userland -- kernel-side implementation was already real
     and present (`third_party/xnu-7195` `osfmk/kern/ipc_*`/`clock.c`),
     never exercised from a userland binary before this pass. The
     smaller-scoped first target (`docs/backlog.md` item 7's own
     suggestion) landed for real: **blocking `nanosleep(2)`/`_nanosleep`
     is DONE** (`simple_cmds_nanosleep_compat.c`/
     `simple_cmds_nanosleep_pub_compat.c`, used by `tail -f`'s poll
     interval and `sleep(1)`) -- **`ps`'s CPU%/RSS/VSZ/thread-count
     columns (`task_for_pid`/`task_info`/`task_threads`/`thread_info`,
     `ps_task_info_stub.c`) is NOT done**, not attempted this pass, still
     printing honest zeros.

     Real chain now built (`tools/userland_staging/build_libmach_ipc.sh`,
     new `libc_build/libmach_ipc.a`): `mach_host_self()` -> real
     `host_get_clock_service()` -> real `semaphore_create()` (once, at
     first use) -> per-call real `clock_get_time()` +
     `__semwait_signal()` (the real, cancelable BSD syscall 334 --
     already linked via `libmalloc.a`'s `sys_semwait_signal.o`, this is
     its first real semaphore-backed caller). `host_get_clock_service`/
     `clock_get_time`/`semaphore_create` client stubs are real,
     `mig`-generated code (`/usr/bin/mig` run fresh each build against
     the real, unmodified `osfmk/mach/{clock,mach_host,task}.defs`, same
     tool/source-regeneration precedent `build_libmalloc.sh`'s own
     `mach_vm_internal.h` step already established) -- not hand-derived
     wire format. Two new real arm64 trap stubs needed generating by
     hand (`mach_ipc_traps.s`, same pattern as `libplatform_mach_traps.s`/
     `libmalloc_vm_traps.s`): `mach_msg_trap` (-31) and `host_self_trap`
     (-29). A handful of genuinely-unreachable-for-these-3-routines MIG
     support symbols (`mach_port_insert_right`/`mach_port_extract_right`/
     `mach_port_unguard`/`voucher_mach_msg_set`) got honest `KERN_FAILURE`/
     `FALSE` stand-ins (`mach_ipc_dead_stubs.c`, same pattern as
     `libmalloc_vm_mig_stubs.c`) -- proven unreachable via each routine's
     own real generated reply-disposition check, not assumed.

     QEMU-verified via a real pty-driven boot + login, three independent
     ways: (1) the real, unmodified `/bin/sleep 3` and `tail -f` binaries
     link with zero undefined symbols and `sleep 3` completes and returns
     the shell to its prompt without hanging; (2) a dedicated smoketest
     (`mach_ipc_smoketest.macho`, calling the exact same real `nanosleep()`
     `sleep(1)` uses) self-measured a real 3000ms requested sleep at
     `ELAPSED_MS=3032` via `mach_absolute_time()` + a real, non-1:1
     `mach_timebase_info()` ratio (numer=125/denom=3) -- accurate to
     within 1%; (3) independently, the QEMU monitor's `info registers`
     was polled (non-invasively, VM kept running) during the sleep window
     and caught the guest PC at `_processor_idle+0x168` -- the real
     kernel idle-loop symbol, confirming genuine blocking/descheduling
     rather than the old `mach_absolute_time()` spin-wait it replaced.
     `task_for_pid`'s real MIG RPC to the task subsystem (`ps` stats) is
     a separate, larger follow-on -- left for a future pass, not
     forgotten.
  6. **[DONE 2026-08-21]** CommonCrypto-shaped digest API for `md5`. Real,
     unmodified `text_cmds/md5/md5.c` (tag `text_cmds-106`) now builds and
     links via a new pin, `third_party/CommonCrypto` (tag
     `CommonCrypto-60178.40.2`, confirmed against the `macos-112` manifest
     the same way as every other pin) -- its real, unmodified
     `CommonDigest.h`/`CommonDigestSPI.h` headers supply the real
     `CCDigestCtx`/`CCDigestAlg`/`CC_MD5_DIGEST_LENGTH` types md5.c's own
     real sibling `commoncrypto.h` expects. Real CommonCrypto's own
     `lib/CommonDigest.c` glue turned out unusable as-is: it `#include`s
     `<corecrypto/ccmd5.h>` etc and calls `ccdigest_init/update/final`,
     and corecrypto's actual digest cores are confirmed NOT open-sourced
     anywhere in this checkout (only 4 headers under
     `xnu-7195/EXTERNAL_HEADERS/corecrypto/`, zero `.c` bodies, and this
     CommonCrypto clone doesn't vendor corecrypto either). No FreeBSD
     libmd precedent existed in-checkout either. Per source priority,
     `tools/userland_staging/md5_commoncrypto_shim.c` implements the real
     `CCDigestInit/Update/Final/OutputSize` + `Digest_End/Data/File`
     surface `commoncrypto.h` declares (blocking `open()`/`read()` instead
     of real `commoncrypto.c`'s `dispatch_io()`/GCD-based version --
     libdispatch is already ruled out project-wide, see Known stubbings);
     the actual MD5 math (`md5_rfc1321_core.c`) is a userland port of
     `xnu-7195`'s own already-proven, already-kernel-boot-verified
     `libkern/crypto/corecrypto_md5.c` (last-resort custom RFC 1321, not
     Apple/FreeBSD source -- that kernel file's own header explains why).
     QEMU-verified via a real pty-driven boot + login: `md5 -x` (built-in
     self-test) printed all 8 RFC 1321 reference vectors as
     `- verified correct`; `md5 -s abc`, `md5 <file containing "abc">`,
     and `printf '' | md5` (stdin filter path) all produced
     `900150983cd24fb0d6963f7d28e17f72` / `d41d8cd98f00b204e9800998ecf8427e`
     respectively, matching this host's own real `/sbin/md5` byte-for-byte.
     `sha1sum`/`shasum`-class follow-on (same shim pattern, wiring in this
     project's existing from-scratch SHA-2 instead) not attempted this
     pass -- `md5` was the item's actual named consumer; left for a future
     pass, not forgotten.
  7. **`posix_spawn(2)`** -- an unbuilt process-creation API; blocks
     `install`, which also needs its own real `strip(1)` subprocess helper
     once spawning works.
  8. **[DONE 2026-08-21]** `sysctl(2)` was already real and general-purpose
     here (`tools/userland_staging/libc_gen_syscalls.c`'s `sysctl()`/
     `sysctlbyname()`, `SYS_sysctl` = 202). `df` and `killall` are now
     both built and QEMU-verified against it -- see Current status above.
     `w`/`who` still need this same `KERN_PROC` MIB plus `utmp`/`wtmp`
     (item 11 below), not filed here.
  9. **Real System V IPC** (`semget`/`shmget`/`msgget` family) -- blocks
     `ipcs`/`ipcrm`. `dd`'s more involved I/O-conversion logic wasn't
     surveyed in depth either; group it here for a first look.
  10. **[DONE 2026-08-20]** zlib compression (deflate side) landed with
      the Tier A sweep's `gzip` (`gz_deflate_impl.c`, real
      `deflateInit2()`/`deflate()` against the same real, unmodified
      zlib core at `third_party/u-boot/lib/zlib` the grep/sed/nl/xargs/ed
      sweep's inflate-only `gz_gzip_impl.c` already used) and `compress`
      (a separate, self-contained LZW codec, no zlib dependency at all).
      Both standalone commands built and QEMU-verified real round-trip
      -- see Current status above / historic archive for detail.
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
  3. **`cron`**: real `at`/`atrun` (task scheduling, cron-adjacent)
     already sit unbuilt at `third_party/system_cmds/at.tproj` and
     `atrun.tproj`, confirmed present. The full `cron` daemon itself is
     likely its own separate apple-oss-distributions repo, not yet
     cloned -- same `setup_third_party.sh` pinning work as `syslogd`
     needed first.
- **Phase 6 (networking)**: **6a DONE 2026-08-21** -- the DWC2 USB host
  controller driver (`iokit/BCM2837DWC2/`), device enumeration and the hub
  protocol subset are implemented and QEMU-verified: a real device
  descriptor is read from the root-port device, and a second device is
  enumerated through a downstream hub port, which is the exact topology the
  LAN9514 presents. See `docs/usb-support-plan.md`'s STATUS section for the
  boot-log evidence and for what is explicitly NOT verified (the SMSC
  register protocol, which QEMU cannot model; interrupt-driven completion,
  deliberately deferred -- this cut polls, as its u-boot reference does;
  split transactions, implemented but never executed).
  Testing USB at all first required backporting a DWC2 model into
  `third_party/qemu-zhuowei` (QEMU 2.12.91 modelled no USB for `-M raspi3`),
  a prerequisite the plan doc had wrongly assumed away.
  Next: 6b, the SMSC LAN9514 Ethernet driver -- chip-level logic was already
  ported and its `SMSCUSBTransport` now has a concrete implementation
  (`iokit/BCM2837DWC2/SMSC9514USBTransport.cpp`, written, compiles, real-
  hardware verification pending). 6b's remaining work is integration:
  compiling `SMSC9514Ethernet` into the kernel, which pulls in
  IONetworkingFamily and revisits the INET/INET6 domain-registration
  decision in `architecture.md` (taken when no network interface existed).
  The INET half is DONE on branch `phase-6b-inet` (IPv4 stack initializes,
  `lo0` attaches, zero panics).

  **IONetworkingFamily integration, scoped 2026-08-22** (measured, not
  estimated -- all 12 `third_party/IONetworkingFamily/*.cpp` were
  trial-compiled individually):
  - **`-D__PRIVATE_SPI__` is required.** `IONetworkController.h:1500` gates
    `outputStart`/`pollInputPackets`/`setInputPacketPollingEnable`/
    `IOMbufServiceClass` behind it while the `.cpp` defines them
    unconditionally, so without it the base class alone fails with
    "out-of-line definition does not match any declaration". Adding it took
    `IONetworkController.cpp` from 4 errors to zero. Apple's own build
    flag, not a workaround.
  - **7 of 12 sources then compile clean** (`IOKernelDebugger`,
    `IOMbufMemoryCursor`, `IONetworkController`, `IONetworkData`,
    `IONetworkMedium`, `IOOutputQueue`, `IOPacketQueue`).
  - Of the rest, **most errors are trial-harness artifacts, not real skew**:
    `io_filter_policy_t` (`IOUserClient.h:170`, behind `#if PRIVATE`),
    `kqueue_id_t` (`bsd/sys/event.h`, same class), and
    `DriverKit/IODMACommand.h`. Those come from compiling standalone with
    `compile_flags.txt`; the real kernel build sets those defines and
    include paths. So do the integration by vendoring into the tree and
    building via `iokit/conf/files` the way `iokit/Storage/` already is,
    rather than fighting the standalone harness.
  - **The genuine vintage skew is a short, bounded list**, all in
    `IONetworkInterface.cpp`/`IOEthernetInterface.cpp` referencing
    `bsd/net/` symbols this xnu vintage does not expose:
    `IFNET_LQM_THRESH_GOOD`, `IFNET_LQM_THRESH_OFF`, `if_bandwidths_t`
    (incomplete type), `DLIL_MODIDLEN`, `DLIL_MODARGLEN`.

  Then 6c: a DHCP client + `ifconfig`/`ping`/`netstat` (FreeBSD's
  `dhclient` recommended over Apple's `bootp`).
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
  blocked on real crypto beyond SHA-2 (Phase 7.5) -- concretely, on
  **AES, which does not exist anywhere in this kernel**: `g_crypto_funcs`
  is never populated (no kext ever loads) and xnu-7195 ships no AES core
  of its own. Scoped as an explicit gate in `docs/backlog.md` P2 item 0a,
  including why the core should come from FreeBSD's `sys/crypto/rijndael/`
  rather than be hand-rolled (corecrypto is confirmed absent from
  apple-oss-distributions, so the "Apple first" rule cannot apply). 7c (self-hosting
  toolchain) recommends Apple's old `gcc_42` over `clang` for feasibility,
  plus `cctools`. 7d (`vim`) is small and can fold into whichever of 7a-7c
  lands first.
- **Phase 7.5 (closed-source ceilings)**: a living list, not a phase to
  "complete" -- real pthread multithreading and real corecrypto-backed
  crypto are permanent gaps short of a from-scratch reimplementation of a
  closed Apple subsystem (see Known Stubbings above).
  - **Real pthread kernel bridge, step 1: real thread creation, DONE +
    QEMU-verified 2026-08-20.** Confirmed independently (not just trusting
    `bsd_pthread_stub.c`'s own header comment) that no
    `pthread_synch.c`/`pthread_shims`-style kernel *implementation* exists
    anywhere in `third_party/xnu-7195` -- only the thin, real, open-source
    shim layer (`bsd/pthread/pthread_shims.c`) that a real closed-source
    pthread.kext would have plugged into via `pthread_kext_register()`.
    One real nuance the original framing undersold:
    `bsd/pthread/pthread_workqueue.c` (3683 lines, real Apple source,
    already in this tree) DOES implement real `workq_open`/
    `workq_kernreturn` workqueue *policy* logic (deciding when to spin up/
    down worker threads, kqueue integration) -- but it still delegates the
    actual thread-creation mechanics to the same closed `pthread_functions`
    vtable (`workq_create_threadstack`/`workq_setup_thread`/
    `workq_destroy_threadstack`/`workq_markfree_threadstack`/
    `workq_handle_stack_events`, all still `bsd_pthread_stub.c` stubs
    returning `ENOTSUP`/doing nothing). Filling in those five hooks for
    real would light up *already-present* real Apple scheduling logic, not
    just extend a hand-rolled stub -- a real, valuable, and narrower-than-
    expected next scope for whoever picks up workqueues, worth knowing
    before assuming that's "more closed-source design work" like
    `bsdthread_create` was.

    The real bridge implemented: `pthread_kern` (the real callback table
    `pthread_shims.c` hands to any pthread.kext, already populated at
    kernel-link-time since this project has no kext-loading boundary --
    `bsd_pthread_stub.c` links directly against the same kernel image) is
    used as designed -- `pthread_kern->thread_create(current_task(), &th)`
    (real `osfmk/kern/thread.c`, leaves a suspended thread with
    `user_stop_count=1`), then `pthread_kern->thread_set_wq_state64(th,
    &state)` (real `osfmk/arm64/status.c`, sets `thread->machine.upcb`'s
    pc/sp/x0 -- the exact same struct `bsd/dev/arm/unix_signal.c`'s real,
    already-QEMU-verified `sendsig_set_thread_state64()` sets for signal
    delivery on this same board), then `pthread_kern->thread_resume(th)`.
    The new thread's very first return to userspace goes through
    `thread_bootstrap_return` -> `arm64_thread_exception_return`
    (`osfmk/arm64/locore.s`) -- the identical real, already-proven-safe
    return-to-EL0 path every `exec()` in this project already exercises,
    not new/invented machine-dependent code. Reference-counting mirrors
    this project's own already-working `fork()` (`kern_fork.c`'s
    "drop the extra references we got during the creation" ->
    `thread_deallocate()` after `thread_resume()`).

    Deliberately NOT implemented (out of scope for this step, per the
    session's own instructions): no real libpthread-ABI trampoline
    (kport/self/flags register conventions), no TSD/dispatch-queue
    offsets, no thread port returned in `*retval` (so nothing in userspace
    can `bsdthread_terminate`/signal the new thread individually yet -- it
    only stops when the whole task exits), `bsdthread_register`/
    `psynch_*`/`workq_*` all remain `ENOTSUP` stubs, untouched.

    Verification: `tools/pthread_kthread_test/` (new, raw-syscall
    freestanding test binary, same build shape as `tools/init_binary/`) --
    a real second Mach thread writes a magic value and spins forever; the
    main thread observes it and calls real `exit(2)`, which for the first
    time in this project has to tear down a task with >1 live thread (real
    `task_terminate_internal()` reaping, structurally untested here until
    now). QEMU-confirmed via two separate real boot+login sessions:
    `pthread_kthread_test` printed "SUCCESS: second real Mach thread ran
    and wrote the magic value" both times, exited 0, and bash remained
    alive and fully functional immediately afterward. Zero-regression
    check (not just the new binary in isolation): in the same two sessions,
    `pwd`/`true`/`false`/pipes/`wc`/`head`/`tail`/`mkdir`/`touch`/output-
    redirection/`grep`/`sed`/`cat`/`cp`/`mv`/`rm`/`which`/`id`/`uname`/
    `hostname`/`stat`/`ps`, plus real login itself, all ran identically to
    before, interleaved before AND after the pthread test in the same
    continuous shell session -- no panic, no hang, no crash, no divergent
    output anywhere in either session's full serial log. Committed as a
    real, working, regression-verified change (see `third_party/xnu-7195`
    commit history for `bsd/kern/bsd_pthread_stub.c`); not pushed.

  - **Real pthread kernel bridge, step 2: real workqueue thread-stack
    hooks, DONE + QEMU-verified 2026-08-21.** Major correction to step 1's
    own framing above (found by actually reading
    `third_party/libpthread/kern/` instead of trusting the inherited
    "never open-sourced" assumption): `workq_create_threadstack`/
    `workq_destroy_threadstack`/`workq_markfree_threadstack`/
    `workq_setup_thread`/`workq_handle_stack_events` -- and
    `bsdthread_register` itself -- all DO have real, unmodified,
    already-open-source Apple implementations. Apple ships pthread.kext's
    kernel-support code as `third_party/libpthread`'s own `kern/`
    subdirectory (`kern_support.c`/`kern_init.c`/`kern_internal.h`,
    pinned `libpthread-454.80.2`, confirmed against
    `distribution-macOS`'s `macos-112` manifest via
    `setup_third_party.sh`'s own established method, same as every other
    pin in this project) -- built as a real kext on real macOS/iOS; this
    project has no kext-loading boundary, so (exactly like every other
    "kext" here) it needed linking directly into the kernel instead, not
    reinventing. A prior pass (`build_libpthread.sh`, Phase 4's userland
    libpthread tier) had already noticed this `kern/` subdirectory but
    filed it as "the KERNEL-side pthread.kext internals bsd_pthread_stub.c
    stands in for" and moved on without checking further -- this session's
    real find was actually reading it.

    What was ported, into a new file
    (`third_party/xnu-7195/bsd/kern/bsd_pthread_workq.c`, ~460 lines):
    real `workq_create_threadstack`/`workq_destroy_threadstack` (real
    `mach_vm_map_external`/`mach_vm_allocate_external` + `mach_vm_protect`
    guard-page setup in the target process's own `vm_map` -- note:
    `mach_vm_map`/`mach_vm_allocate`'s convenience-wrapper names are
    `__x86_64__`-only in this xnu checkout, `osfmk/vm/vm_user.c` confirmed
    by reading it; arm64 needs the real `_external` entry points
    directly, same functions, different names), real
    `workq_markfree_threadstack` (`mach_vm_behavior_set(...,
    VM_BEHAVIOR_REUSABLE)`), real `workq_setup_thread` (real arm64
    register-state setup -- x0=self, x1=kport, x2=stack_bottom,
    x3=kevent_list, x4=upcall_flags, x5=kevent_count, pc=the real
    wqthread entry point, sp=stack top -- via
    `pthread_kern->thread_set_wq_state64`, then a real noreturn jump back
    to userspace via `thread_bootstrap_return()`/`unix_syscall_return()`),
    real `workq_handle_stack_events` (real kevent-list marshalling via
    the already-real `pthread_kern->kevent_workq_internal` callback, this
    kernel's own already-built kqueue plumbing), and real
    `real_bsdthread_register` (ported from `_bsdthread_register`,
    per-process pthread registration via the already-real
    `pthread_kern` `proc_set_threadstart`/`proc_set_wqthread`/
    `proc_set_pthsize`/`proc_set_register`/etc. callback family).
    `bsd_pthread_stub.c` itself only changed to wire these six real
    functions into its `pthread_functions` vtable in place of their old
    `stub_workq_*`/`stub_bsdthread_register` ENOTSUP stand-ins (now
    deleted, not left as dead code) -- `bsdthread_register2`/`psynch_*`/
    `bsdthread_terminate`/`thread_selfid` remain untouched ENOTSUP stubs,
    out of scope.

    One real, evidence-backed (not guessed) wiring correction, found by
    reading both sides rather than assuming: `bsd_pthread_stub.c`'s
    `.version` field changed from `PTHREAD_FUNCTIONS_TABLE_VERSION` (1)
    to `0`. `bsd/pthread/pthread_shims.c`'s real `bsdthread_register()`
    syscall wrapper branches on `pthread_functions->version >= 1` to pick
    between the OLDER `.bsdthread_register` vtable slot and the newer
    `.bsdthread_register2` one; `third_party/libpthread/kern/kern_init.c`'s
    own real `pthread_internal_functions` initializer leaves `.version`
    unset (so it's 0), meaning real Apple's own pairing for this exact
    libpthread vintage uses the OLDER slot -- confirmed by also checking
    that the old slot's real (if stale-named) signature
    (`pthsize`/`dummy_value`/`targetconc_ptr`) matches
    `_bsdthread_register`'s actual parameter types 1:1 (Apple just
    renamed two params to `pthread_init_data`/`pthread_init_data_size` in
    a later revision without changing the vtable slot itself).
    `.bsdthread_register2` stays populated with its old ENOTSUP stub,
    now genuinely unreachable by design rather than by accident. Verified
    only one call site in the whole tree reads `pthread_functions->version`,
    and `bsdthread_create`'s own syscall wrapper never checks it at all --
    so this change carries zero risk to step 1's already-verified
    `real_bsdthread_create` path.

    Verification: `tools/workq_kthread_test/` (new, raw-syscall
    freestanding test binary, same shape as `tools/pthread_kthread_test/`)
    drives the real, full, unmodified userspace-facing API in order: (1)
    real `bsdthread_register(2)` (syscall 366) registers a real wqthread
    entry point; (2) real `workq_open(2)` (syscall 367, real
    `bsd/pthread/pthread_workqueue.c`, allocates this process's real
    `struct workqueue`); (3) real `workq_kernreturn(2)` (syscall 368) with
    `WQOPS_QUEUE_REQTHREADS`, `pp=0x8FF` (a real, hand-derived
    `pthread_priority_t` for `THREAD_QOS_LEGACY` -- verified by tracing
    `_pthread_priority_thread_qos()` back against it, documented in the
    test's own header comment) -- which drives real
    `workq_reqthreads()` -> `workq_schedule_creator()` ->
    `workq_add_new_idle_thread()` (real `workq_create_threadstack()` +
    the pre-existing, unmodified real `osfmk/kern/thread.c`
    `thread_create_workq_waiting()`) -> a real wakeup of the newly idle
    thread, which runs real `workq_setup_thread()` on its own real
    return-to-userspace path, landing at the test's `wq_entry` (real
    arm64 wqthread register convention, not a fabricated one) which
    writes a magic value the main thread observes. QEMU-confirmed via two
    separate full boot + login + 37-command regression-sweep sessions,
    byte-for-byte identical real transcript both times:
    `workq_kthread_test` printed `bsdthread_register OK` / `workq_open OK`
    / `workq_kernreturn(REQTHREADS) OK, waiting for real workqueue
    thread...` / `SUCCESS: real workqueue thread ran via real
    workq_setup_thread and wrote the magic value`, exited 0. Zero
    regression: in the same two sessions, `pthread_kthread_test` (step
    1's own real second-Mach-thread test) ran identically and still
    printed its own real success line immediately before the workqueue
    test in the same continuous shell session; `pwd`/`true`/`false`/
    pipes/`ls`/`mkdir`/`touch`/output-redirection/`grep`/`sed`/`cat`/
    `cp`/`mv`/`rm`/`which`/`id`/`uname`/`hostname`/`stat`/`ps`, plus real
    login itself, all ran identically to before and after both tests --
    no panic, no hang, no crash, no divergent output in either session's
    full serial log (`DONE cmd_idx=37/37` both times). Adversarially
    self-reviewed before commit: confirmed the new `panic()` call inside
    `workq_set_register_state()` (real Apple code, fires if
    `bsdthread_register` was never called for the process) is not
    reachable by any of the 79 already-shipped commands -- none of them
    call `workq_open`/`bsdthread_register`/`workq_kernreturn` (no
    libdispatch/GCD, no pthread-workqueue-consuming program existed in
    this project before this session's own new test binary) -- and swept
    the diff for stray debug instrumentation (none found). Committed as a
    real, working, regression-verified change (see `third_party/xnu-7195`
    commit history for `bsd/kern/bsd_pthread_workq.c` and
    `bsd/kern/bsd_pthread_stub.c`); not pushed.

    Real next step for whoever continues this: `psynch_mutex*`/
    `psynch_cvwait`'s real backing primitive is very likely real Mach
    semaphores/turnstiles already used elsewhere in this kernel
    (`bsd/pthread/pthread_shims.c`'s own `psynch_wait_prepare`/
    `psynch_wait_wakeup` already call real `turnstile_prepare`/
    `waitq_assert_wait64_leeway`/`waitq_wakeup64_thread` -- real,
    open-source, unexplored in depth this session) -- not yet designed in
    detail, flagged rather than guessed at. A real minimal
    libpthread-ABI-compatible userspace `pthread_create()` wrapper is now
    architecturally much closer than it looked before this session (the
    kernel side of workqueue thread creation is real and verified) but
    still needs: real TSD/dispatch-queue-offset wiring end to end (the
    kernel-side plumbing for it, `proc_set_pthread_tsd_offset`, etc., is
    real and exercised by `real_bsdthread_register` above, but no real
    userspace libpthread on this target calls it with real offsets yet --
    `tools/workq_kthread_test/` deliberately passes 0s, see its own header
    comment), and `psynch_*` (previous paragraph). `workq_kernreturn`'s
    other real op codes (`WQOPS_THREAD_RETURN`/`WQOPS_THREAD_KEVENT_RETURN`/
    `WQOPS_QUEUE_REQTHREADS2`/etc.) were not exercised by this pass either
    -- `tools/workq_kthread_test/`'s worker thread just spins forever
    rather than really parking back into the kernel, a real, narrower,
    deliberately-scoped simplification documented in the test's own
    header comment.
- **Phase 8 (`pkg` package management)**: fully planned, not started.
  `freebsd/pkg` is the chosen vehicle (native Mach-O awareness, real
  custom-repo support already documented). Needs `libsqlite`/`libucl`/
  `libelf`/`libyxml`/`libder`/`picosat` ported first (low risk), then
  `libecc` (shares Phase 7.5's/7b's crypto gap) and `libfetch` (needs
  Phase 6), then `liblua`, before a real custom package repo can be built
  and consumed on-device.
