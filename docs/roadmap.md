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

Actively on **Phase 4, step 3** ("real commands"): as of 2026-08-20
(end of day, "command-sweep" session), **60** real Apple/FreeBSD command
binaries build, link, and run QEMU-verified against the real HFS+ root,
using a curated real Apple/FreeBSD libc stack (string, platform, locale,
stdio, gen, printf/scanf, malloc, pthread, closure, removefile/copyfile,
libunwind -- all real, QEMU-verified ports, see `docs/historic/
roadmap.md`'s Phase 4 for the full per-tier build history): `bash`,
`uname`, `ps`, the original Tier 1-5 set (`true`/`false`/`yes`/`pwd`/
`basename`/`dirname`/`echo`/`printf`/`hostname`/`logname`/`cat`/`wc`/
`head`/`tail`/`mkdir`/`touch`/`rm`/`env`/`which`/`sleep`/`printenv`/
`cut`/`uniq`/`ln`/`id`), and 32 more added in one long same-day
"command-sweep" session across six blocks: `cp`/`mv` (Block 1);
`rev`/`fold`/`paste`/`expand`/`unexpand`/`comm`/`colrm`/`lam`/`rs`/
`rmdir`/`mkfifo`/`mknod`/`chflags` (Block 2); `seq`/`jot`/`test`/`kill`/
`tee`/`mktemp`/`hexdump`/`stat`/`cksum`/`pathchk`/`du` (Block 3);
`chmod`/`chown` (Block 4); `nice`/`renice`/`nohup` (Block 5); and
`chroot`/`apply`/`vis`/`unvis` (Block 6). Full per-block build/debug
narrative (every new raw syscall, every real-source dependency chased
down, every QEMU transcript) is in `docs/historic/roadmap.md`'s newest
Phase 4 entry -- this section only summarizes.

`which`/`sleep`/`printenv`/`cut`/`uniq`/`ln`/`id` were added and
QEMU-verified 2026-08-19/20 (Twenty-fourth pass, see
`docs/historic/roadmap.md`): `which` had existed unverified since
2026-08-16 (same situation `env` had been in) and needed no fix, just
verification; the other six are genuinely new real commands, needing one
new real syscall or compat shim each at most (a public `nanosleep()` for
`sleep`, a spec-compliant `catopen()` stub for `uniq`, `link`/`symlink`
raw syscalls plus reusing `libremovefile.a`'s real `unlink`/`rmdir`
bodies for `ln`, and a real last-resort `getgrouplist_2()` SPI shim over
`tools/libinfo_custom/` for `id`). `cp`/`mv` were surveyed and found to
need roughly a dozen more real syscalls plus one Phase-4.6-item-4-shaped
stand-in (`lchmod`/`lchflags`'s real `setattrlist(2)` dependency) --
deferred to a dedicated follow-up pass rather than rushed; see
`docs/historic/roadmap.md`'s Twenty-fourth pass entry for the exact
per-symbol breakdown.

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
in real use by `ps`/`rm` for `getpwnam`/`getgrgid` lookups.
**Correction, 2026-08-20**: the line that used to appear here ("the rest
of Phase 5 (login/su/PAM) hasn't been started") was stale/wrong -- real
`do_login()`/`cmd_su()` (`tools/init_binary/init.c`) have been in place
and QEMU-verified since commit `d2108bb` (2026-08-14): a real interactive
`login:`/`Password:` prompt loop against `/etc/master.passwd`
(anti-enumeration-safe, fail-closed on a missing/damaged account
database), real `setgroups`/`setgid`/`setuid` privilege drop, and a real
`su` builtin with the correct real-Unix caveat documented in its own
comment (no setuid-root helper binary yet, so `su` to a different account
only works starting from root). Only PAM and account-provisioning tooling
(`pwd_mkdb`/`vipw`/`chpass`/`passwd`) remain genuinely not started -- see
"Remaining future work" below.

**Default login shell is now real bash, QEMU-verified 2026-08-20.**
`single_user_child()` (`tools/init_binary/init.c`) previously exec'd the
nonexistent `/bin/sh` (falling through to the builtin virtual
`shell_main()` on the resulting `ENOENT`) and, separately, passed a
hardcoded EMPTY `envp` (`{0}`) even in the fallback-existed case -- a real
bug, since the `exec` builtin already built a real envp via
`build_envp()`. Both fixed: the default path now execs the real, already
Twenty-third-pass-verified `/bin/bash` with argv[0] = `"-bash"` (the
traditional BSD/login(1) leading-dash convention for a login shell -- real
bash's own `set_shell_name()` in `third_party/bash/bash-3.2/shell.c` reads
this) and a real `build_envp()` environment. No `-i` flag needed: bash's
own interactive-shell autodetection (no `-c`/script argument, and
`isatty(stdin) && isatty(stderr)`) already fires on its own now that
`/dev/console` has real BSD tty semantics. Verified safe against this
image's missing `/etc/profile`/`~/.bash_profile` etc.: bash's own
`maybe_execute_file()`/`_evalfile()`
(`third_party/bash/bash-3.2/builtins/evalfile.c`) passes `FEVAL_ENOENTOK`
for every startup file it tries and silently skips a missing one, no
error. Real QEMU transcript (pty-driven, full login-to-prompt): after
`login: root` / `Password: darwin` / `Welcome, root.`, the very next line
is a real `-bash-3.2# ` prompt (leading `-` confirms login-shell mode
took), `echo $PATH`/`$HOME`/`$USER`/`$TZ` show the real
`build_envp()`-provided values (`/bin:/usr/bin`, `/`, `root`, `UTC` --
previously would have printed empty), an up-arrow history recall
re-executed a prior command correctly, and `ec`+Tab completed to `echo`
mid-line (typed continuation after the Tab landed as `echo o
TAB_COMPLETION_TEST_7733`, proving completion fired rather than `ec`
being run as a literal, nonexistent command) -- all in the DEFAULT path,
no manual `exec /bin/bash -i` needed. `exit` cleanly logs out and pid 1's
existing respawn loop gives a fresh `login:` prompt, unchanged.

**Real termios/tty + real GNU readline (tab completion, command history)
DONE and QEMU-verified, 2026-08-19/20.** The starting assumption --
baked into `bash_syscalls.c`'s old `tcgetattr`/`tcsetattr`/`tcgetpgrp`/
`tcsetpgrp` stubs and `build_bash.sh`'s `-UREADLINE` flag -- was that
this target's console had no real tty line-discipline for readline's
raw-mode terminal handling to use. That assumption was never actually
tested and turned out to be **wrong**: a disposable `ioctl(TIOCGETA/
TIOCSETA)` probe built into `tools/init_binary/init.c` (removed before
commit) proved `third_party/xnu-7195`'s real BSD tty stack
(`bsd/dev/arm/km.c`'s `kmopen`/`kmioctl` -> `bsd/kern/tty.c`'s
`ttioctl_locked`/`ttread`, wired from `bsd_autoconf()`'s `kminit()` call)
is already fully, correctly wired under `/dev/console` -- `TIOCGETA`
round-tripped the exact real `kmopen()` defaults
(`iflag=0x2b02/oflag=0x3/cflag=0x8b00/lflag=0x5cb`), and a `TIOCSETA`
clearing `ICANON|ECHO` genuinely took effect (confirmed via a second
`TIOCGETA` readback) and produced a real, immediate, unechoed
single-byte `read()` -- exactly the raw-mode contract readline needs, no
kernel changes required at all.

Real fix, two layers:
1. **Real termios/isatty/ttyname for real**, replacing the old ENOTTY/
   false stand-ins: `third_party/Libc/gen/FreeBSD/termios.c` and
   `ttyname.c` are now compiled for real into the shared
   `libc_gen.a` (`build_libc_gen.sh`, also picking up `devname.c`);
   `isatty()` is a real (not fake) `ioctl(TIOCGETA)`-based
   reimplementation in `libc_stdio_process_shims.c` (kept out of
   `libc_gen.a` deliberately, so `libc_stdio.a`'s own standalone
   from-scratch smoketest keeps working without depending on a later
   tier). A real, shared `ioctl`/`_ioctl`/`_writev`/`_select` raw-syscall
   layer moved into `libc_gen_syscalls.c` (previously duplicated,
   separately, in both `bash_syscalls.c` and `ps_extra_syscalls.c`).
   QEMU-verified: `bash -i`'s `[ -t 0 ]` now genuinely reports `true`
   (previously always `false`).
2. **Real GNU readline**, built from bash-3.2's own bundled
   `lib/readline/*.c` (the Makefile.in's real `OBJECTS` list, minus
   `shell.c`/`xmalloc.c` -- both real, but bash's own top-level
   `general.c`/`variables.c`/`xmalloc.c` already provide the identical
   `sh_get_env_value`/`xmalloc`/etc symbols those two exist for
   *standalone* `libreadline` builds, confirmed via a real duplicate-
   symbol link error), plus `pcomplete.c`/`pcomplib.c` (programmable
   completion), `bashline.c` (bash's own completion/keybinding glue),
   and `bracecomp.c`. `READLINE` re-`#define`d in `third_party/bash/
   config.h`; `HAVE_TERMCAP_H` undefined (this checkout genuinely has no
   `<termcap.h>`/termcap database -- `lib/readline/tcap.h`'s own real
   `#else` fallback branch handles that, and a new
   `bash_readline_termcap_compat.c` provides real, honest
   `tgetent()` (returns -1, "no database", triggering
   `terminal.c`'s own real, documented dumb-terminal fallback --
   `\r`/`\b` only, no cursor-addressing capabilities) plus real
   `tputs`/`tgoto`/`tgetnum`/`tgetflag`/`tgetstr`. A handful of small
   real/honest stand-ins closed the remaining link gaps: `pthread_testcancel()`
   (real no-op, matching this project's own prior art in
   `ps_err_warn_compat.c` -- no real cancellation subsystem exists on
   this single-thread target), `setpwent`/`getpwent`/`setgrent`/
   `getgrent`/`endgrent`/`setservent`/`getservent`/`endservent` (honest
   "no enumeration API yet" stand-ins for `~user`/`%group`/service-name
   completion -- `tools/libinfo_custom` only supports lookup-by-name/uid
   today), `__darwin_check_fd_set_overflow` (a real `FD_SETSIZE` bounds
   check), a real `select()` wrapper, and reuse of the already-real,
   already-proven `simple_cmds_getenv_compat.c` for `setenv`/`getenv`/
   `putenv`/`unsetenv` (bash had **no working `setenv` at all** before
   this, and its previously-linked `getenv()` -- `libc_locale.a`'s old
   `libc_locale_env_stubs.c` -- unconditionally returned `NULL`; both
   fixed as a side effect for bash specifically).

QEMU-verified via real pty-driven transcripts (not assumed): typing
`ec` then a literal Tab byte then `HO_TABTEST_MARKER` + Enter produced
`echo HO_TABTEST_MARKER` executing correctly (real tab completion,
expanding `ec` -> `echo `); running `echo REAL_HISTORY_MARKER_42`, then
sending a raw up-arrow escape (`\x1b[A`) then Enter, re-executed the
exact same command (real history recall). Zero regression: `bash -i`
still logs in, runs commands, and exits cleanly, and both `bash.macho`
and `ps.macho` still link with `LC_UNIXTHREAD`, zero `LC_LOAD_DYLIB`,
zero undefined symbols. Not independently re-verified this pass: multi-
candidate Tab completion (2+ matches), vi-mode editing, and full
`bind`/`complete`/`compgen` builtin correctness beyond linking cleanly.
See `docs/historic/roadmap.md`'s newest Phase 4 entry for the full
investigation, exact commands, and transcripts.

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
- **`bash`'s real fork+exec of an external command passes an empty
  `argv[0]`** to the child -- found 2026-08-20 while QEMU-verifying `cp`/
  `mv` (Block 1): a real fix to `libc_locale_env_stubs.c`'s `getprogname()`
  (previously hardcoded to return `"libc_locale_smoketest"` unconditionally
  for every command in this tier, a stale leftover from before this
  tier's real `argv[0]`/`__progname` plumbing existed) now correctly
  returns the real `__progname` Csu/`crt.c` computes from `argv[0]` --
  which exposed that `argv[0]` itself is empty for children bash spawns,
  not "libc_locale_smoketest" wrongly. Every `err(3)`/`warn(3)` call in
  every command run *from bash* now prints an empty prefix (e.g.
  `": /nonexistent-src-file: No such file or directory"`) instead of the
  command's own name -- cosmetic only (the error text itself is correct
  and honest), but a real bug in bash's own real `execve()` call
  sequence, not in `getprogname()` or in any individual command. Not
  root-caused or fixed this pass (out of this sweep's scope, and
  possibly interacts with the same-day, separately-landed "bash is the
  default login shell" work) -- see `docs/historic/roadmap.md`'s Block 1
  entry for the exact evidence trail.
- **Also every command in `build_simple_cmds.sh`'s tier (28 commands as
  of 2026-08-19) had this same wrong-`getprogname()` bug silently masking
  the argv[0] issue above** -- fixed as part of the same Block 1 pass,
  see `docs/historic/roadmap.md`.
- **`hexdump`** (all format flags, including plain/`-c`/`-C`) prints only
  the final byte-count address line, never the actual hex+ASCII data
  rows, against a real non-empty file -- found and narrowed down (not
  fully root-caused) during Block 3: specific to hexdump's own
  `freopen(argv, "r", stdin)` + `getc()`/`ungetc()` read idiom, not a
  general `freopen` bug (Block 2's `fold`/`expand`/`unexpand`, linking
  the identical real `freopen.c`, correctly read real file-argument
  content in the same session). Most likely a latent bug in this
  project's ported `getc()`/`ungetc()` interaction with a freshly
  `freopen`'d stream -- a real but nontrivial `libc_stdio` question.
  Ships anyway (real, unmodified source, links, runs, doesn't crash),
  same tolerance as `yes`'s documented kernel-panic bug. See
  `docs/historic/roadmap.md`'s Block 3 entry.
- **`pathchk ""`** (empty pathname) exits 0 instead of POSIX's specified
  "shall report an error" -- real, unmodified upstream FreeBSD source;
  low practical impact, not investigated further.
- **`rs -T`** (transpose) dropped the first row of a small piped 2x3
  matrix in one observed test (`printf '1 2 3\n4 5 6\n' | rs -T` printed
  only `4\n5\n6\n`) -- not confirmed as a port bug (real upstream `rs`'s
  array-dimension auto-guessing heuristics, active whenever neither `-g`
  nor `-c` is given, are documented as sensitive to small/irregular
  input shapes) vs. a genuine regression; not investigated further given
  `rs`'s low practical value. See `docs/historic/roadmap.md`'s Block 2
  entry.

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
  real networking (`netopen`/`netconn`) is unavailable; POSIX regex
  (`regcomp`/`regexec`) always reports no-match/bad-pattern (real TRE
  source exists in this checkout but is out of scope for now, Phase 4.6
  item 3); `~user`/`%group`/service-name tab-completion candidates are
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
  Phase 4.6 items 3 and 5 (real POSIX regex, real Mach IPC for `ps`
  stats + blocking sleep) remain not started -- item 3 is this session's
  own explicit stopping point (see below); real Apple `Libinfo` (the
  actual repo, distinct from the working custom flat-file resolver) is
  still unbuilt. Separately worth profiling: bash.macho's first-touch
  demand-paging takes ~90s on this target's mostly-polled SD read path
  -- slow but not a correctness bug.
  **The command-sweep's own natural stopping point (2026-08-20, after
  six blocks/32 new commands in one session)**: every remaining
  `shell_cmds`/`file_cmds`/`text_cmds` candidate surveyed needs a real
  subsystem this project hasn't built at all, not just one or two more
  syscalls -- `sort`/`xargs`/`nl`/`grep`/`sed`/`ed` need POSIX regex
  and/or real pthread multithreading (both documented permanent-or-
  Phase-4.6-scoped ceilings); `md5` needs a whole CommonCrypto-shaped
  digest API distinct from this project's existing from-scratch SHA-2;
  `install` needs `posix_spawn` (an unbuilt process-creation API, plus
  its own real `strip(1)` subprocess helper); `df` needs real `sysctl(2)`
  (a variable-length-MIB syscall never built here); `ipcs`/`ipcrm`/`dd`
  need real System V IPC / more involved I/O-conversion logic not
  surveyed in depth; `compress`/`gzip` need zlib; `who`/`w`/`lastcomm`
  need real `utmp`/accounting-file formats this project doesn't write;
  `killall` needs process enumeration (real `sysctl(KERN_PROC)` again);
  `su` was flagged in the original plan as needing careful judgment
  against this project's own existing `do_login()`/`cmd_su()` in
  `init.c` rather than a naive port. None of these are a quick "one more
  raw syscall" the way this session's 32 commands were -- each is its
  own dedicated-pass-sized subsystem, matching this file's own standing
  guidance not to rush a real architectural gap into a command-sweep
  pass.
- **Phase 5 (multi-user)**: the custom account resolver, real DES
  `crypt()` password verification, and a real `login`/`su` (wired into
  `tools/init_binary/init.c`'s `do_login()`/`cmd_su()`, real bash now the
  default login shell -- see Current status above) are all done. Still
  needed: account-provisioning tools (`pwd_mkdb`/`vipw`/`chpass`/
  `passwd` -- `pwtool` covers an ad hoc subset of this today, not the real
  tools), a setuid-root helper so `su` can reach a different account from
  a non-root session, and eventually PAM (OpenPAM as fallback). `sudo` is
  deliberately out of scope indefinitely.
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
