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

**Real hardware milestone, 2026-08-21**: first-ever confirmed boot on
actual Pi 3 silicon (previously QEMU-only). `u-boot` reached
`## Starting XNU at ...` on its HDMI console, and XNU's own real kernel
boot banner (BSD copyright text via `kprintf`/the real `pi3_uart` mini-
UART driver) came through cleanly over a real serial adapter -- first
confirmation that `bootxnu`'s Mach-O loader/entry-point fix and XNU's
real serial console path both work on real silicon, not just in
QEMU-derived reasoning. Two real bugs found and fixed getting there:
`tools/make_sd_image.sh`'s boot-partition population never actually
copied the kernel file onto the card despite claiming "real-hardware
parity" (fixed); and `sdcard/config.txt` had no `core_freq=250` pin, so
the real Pi 3 firmware's clock scaling shifted the mini-UART's actual
baud away from the nominal 115200 `pe_serial.c`'s fixed divisor assumes
-- symptom was garbled-but-present serial output, not blank (fixed).
Full writeup in `sdcard/README.md` (gitignored by the user's own global
config -- not tracked in this repo, but present on disk). Not yet
confirmed: whether XNU's own boot console draws on the HDMI framebuffer
via the documented handoff (only u-boot's pre-jump text was observed
there so far); everything past boot-to-idle (SD/GPIO/USB/Ethernet) is
untested on real hardware, matching this project's own QEMU-side
milestone at this same stage.

**Phase 4.6 item 3 (real POSIX regex) DONE and QEMU-verified, 2026-08-20.**
Real, unmodified Apple source (`third_party/Libc/regex/TRE/lib/*.c`, the
TRE engine, + `third_party/Libc/regex/FreeBSD/regerror.c`) now builds
clean into `libc_build/libc_regex.a` via the new, permanent
`tools/userland_staging/build_libc_regex.sh` (10/10 real source files
compile with zero gaps against the existing curated locale/malloc/
platform stack). Three real, narrow build-time gaps found and fixed, all
documented in that script's own header comment: (1) TRE's own
`TRE_REGEX_T_FIELD` macro (which field of `regex_t` TRE privately stores
its compiled-program pointer in) is only defined in TRE's top-level
`config.h`, never force-included by any individual `.c` file on its own
(real Apple Xcode project injects it project-wide; this checkout's
non-Xcode build didn't) -- fixed by force-including that real
`config.h`, which also supplies the `HAVE_MBRTOWC`/`HAVE_ISWCTYPE`/etc
autoconf macros TRE's locale-aware wide-char matching needs (real
`mbrtowc_l`/`wctype_l`/`iswctype_l` already existed in `libc_locale.a`
from earlier tiers -- only their prototypes were unreachable, gated
behind this project's own `-D_USE_EXTENDED_LOCALES_`, also added here).
(2) Apple's real `xlocale/_regex.h` marks the xlocale-suffixed regex
entry points (`regcomp_l`/`regncomp_l`/etc) `__IPHONE_NA` -- a real
App-Store-review-time "macOS only" policy annotation, not a functional
restriction -- which trips a hard compile error the moment TRE's
`regcomp.c` *defines* a function under one of those names while building
under this project's uniform `-target arm64-apple-ios14.4`; fixed via a
new, narrowly-scoped compat shim (`regex_availability_override.h`) that
neutralizes just the `__IPHONE_NA` sentinel's expansion, leaving every
other real availability annotation intact. (3) `regcomp(3)` (and only
`regcomp` -- not `regexec`/`regfree`) is `__DARWIN_ALIAS`'d to the
`$UNIX2003`-suffixed symbol name by this checkout's own vendored
`sys/cdefs.h` even on this LP64 arm64 target (confirmed by reading the
real macro chain, not guessed) -- real callers built against these
project headers (bash's own `lib/sh/shmatch.c`, unmodified Apple source)
therefore reference `_regcomp$UNIX2003`, while TRE's `regcomp.c` itself
only ever exports plain `_regcomp`; bridged via a small trampoline in
`bash_syscalls.c` (see that file's own updated comment for the full
mechanism).

Wired into real bash as the end-to-end consumer: `bash_syscalls.c`'s old
narrow `regcomp`/`regexec`/`regfree` stand-ins (`REG_BADPAT`/
`REG_NOMATCH` always) are gone -- object files linked directly on the
command line always win over an archive member offering the same symbol
(this project's own documented archive-shadowing rule), so simply
deleting those fake definitions lets `libc_regex.a`'s real ones resolve
instead once the archive is added to `build_bash.sh`'s link line. Real,
unmodified `lib/sh/shmatch.c` (already compiled into `bash.macho`,
implements bash's `[[ str =~ pattern ]]`) now performs a genuine TRE
match. QEMU-verified via a real interactive login + shell session
(`tools/userland_staging/regex_qemu_test_cmds.txt`): `[[ hello =~
^h.*o$ ]]` correctly matches, `[[ goodbye =~ ^h.*o$ ]]` correctly does
not, `[[ abc123 =~ [0-9]+ ]]` correctly matches, `[[ abcxyz =~
^[0-9]+$ ]]` correctly does not -- four real regex results, all correct.
`bash.macho` remains a clean static Mach-O after relinking (`otool -l`:
`LC_UNIXTHREAD` present, zero `LC_LOAD_DYLIB`; `nm -u`: zero undefined
symbols). `simple_cmds_rpmatch_compat.c` (`rm -i`'s y/n prompt) was left
untouched -- its own header comment already documents why a direct
first-character check is exactly equivalent real behavior in this
project's always-C locale, not a degraded approximation, so there was no
real behavior to gain by rewiring it; unaffected by this change either
way (not rebuilt, not relinked).

**Phase 4.6's `grep`/`sed`/`nl`/`xargs`/`ed` sweep DONE and QEMU-verified,
2026-08-20 (session immediately following item 3).** All five real
commands now build, link, and run against the real POSIX regex engine
(`libc_regex.a`). Highlights (full per-command narrative, every new raw
syscall, every real-source dependency chased down, in
`docs/historic/roadmap.md`): `grep` needed the most new plumbing --
`WITHOUT_FASTMATCH`/`WITHOUT_LZMA`/`WITHOUT_BZIP2`/`WITHOUT_NLS` (real,
upstream-supported build knobs disabling optional subsystems this
project doesn't have), real `fts_*`/`fnmatch()`, and a genuinely real
(not fabricated) `gzdopen()`/`gzread()` for `-Z`/`.gz` support --
real DEFLATE decompression via the real, unmodified zlib core vendored
at `third_party/u-boot/lib/zlib`, glued in with a real standalone
CRC-32 (`gz_crc32_compat.c`) since even u-boot's own `crc32.c` was too
u-boot-board-specific to vendor directly. `sed` and `nl` were both
close to drop-in once `libc_regex.a` was linkable. `xargs` was the
first `simple_cmds.sh` binary to spawn real child processes (real
`vfork()`/`waitpid()`/`execvp()`) and, along the way, surfaced and fixed
a real project-wide bug: `sysconf(_SC_ARG_MAX)` unconditionally failed
because the *only* `sysconf()` anywhere in this project's built libc
archives was a narrow `_SC_STREAM_MAX`-only stdio-bootstrap stand-in
silently shadowing every other case -- fixed narrowly (a real
`sysctl(CTL_KERN, KERN_ARGMAX)` call added to that same stand-in,
root-caused via a standalone QEMU-run diagnostic first) rather than
swapping in the real general-purpose `sysconf.c`, which would have
touched every other already-verified command's stdio-bootstrap path
too. `ed` was the deepest port of the five: real `setjmp`/`longjmp`/
`sigsetjmp`/`siglongjmp` did not exist anywhere in this project before
it (Apple's real arm64 `libplatform` asm needs a genuinely missing
`__sigunaltstack` real dependency, not just a missing header, so this
project now carries its own real AAPCS64 register-save/restore core
instead, deliberately scoped narrower than Apple's -- no
pointer-authentication munging, no sigaltstack-status preservation,
both honestly documented as real, narrow gaps, not silently dropped);
real `popen()`/`pclose()`/`system()` (Apple's real source needs
`posix_spawn`, an already-deferred whole subsystem -- worked around
with the classic pre-`posix_spawn` `pipe()`+`fork()`+`execve()`
algorithm); and a real `pipe(2)` (Apple's own hand-written arm64 raw
syscall, `libsyscall/custom/__pipe.s`, compiled directly). QEMU-verified
with real extended/basic regex patterns for each (`grep -E
'^[0-9]+$'`, `sed -E 's/[0-9]+/NUM/'` plus a real `-i` in-place edit,
`nl -b p'[0-9]'`, `xargs grep -l` + `xargs -n1`, and `ed`'s
`g/[0-9][0-9]*/s//NUM/`, a real global regex substitute, plus a real
`w` write-back verified via a follow-up `cat`) -- not just
literal-string happy paths.

Actively on **Phase 4, step 3** ("real commands"): as of 2026-08-20
(end of day, "command-sweep" session, plus the grep/sed/nl/xargs/ed
sweep immediately after), **65** real Apple/FreeBSD command
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

Phase 4.6 items 1 (real timezone database), 2 (real `environ`/`getenv`
plumbing), and 3 (real POSIX regex) are DONE and QEMU-verified
(2026-08-16, 2026-08-16, 2026-08-20 respectively). Item 4
(`setattrlist(2)`) is also done, see below. Item 5 (Mach IPC) is
explicitly not started.

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

**Real default hostname, 2026-08-20.** Nothing previously set one --
real XNU's `kern.hostname` global stayed empty every boot. `init.c`'s
new `set_default_hostname()` reads `/etc/hostname` (matching real
FreeBSD/NetBSD/OpenBSD's rc-script convention, adapted since this
project has no rc scripts -- init.c IS the boot sequence) and calls the
same real `sysctl(2)` `{CTL_KERN, KERN_HOSTNAME}` MIB the real
`hostname(1)` command uses, falling back to a compiled-in `"strudel"`
default only if the file is missing. `inject_into_sd_image.sh` now
provisions a real `/etc/hostname` (`strudel`) the same way it already
provisions `/etc/master.passwd` -- edit that file and reboot to rename,
no rebuild needed. QEMU-verified: `hostname` at a real logged-in prompt
prints `strudel`.

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

- **`bsd_pthread_stub.c`** (kernel) -- `workq_kernreturn`/`psynch_*` still
  unconditionally return `ENOTSUP`, standing in for Apple's never-open-
  sourced pthread.kext. `bsdthread_create` itself is now REAL as of
  2026-08-20 (Phase 7.5's first step, see below) -- it creates a genuine
  second Mach thread via `pthread_kern->thread_create`/
  `->thread_set_wq_state64`/`->thread_resume` (the same real callback
  table + real `osfmk/kern/thread.c`/`osfmk/arm64/status.c` primitives
  Apple's real pthread.kext would have used), QEMU-verified running real
  code concurrently with the caller and exiting cleanly via the task's
  normal multi-thread teardown. It does NOT implement the real pthread
  ABI (no libpthread trampoline/TSD/dispatch-queue wiring, no thread port
  returned for `bsdthread_terminate`/`pthread_kill` to target) -- not
  `pthread_create()`-compatible yet. Real userspace-pthread scheduling
  (workqueues, `psynch_mutex*`/`psynch_cvwait` userspace synchronizers)
  remains unimplemented and still blocks all real `pthread_create()`-based
  multithreading project-wide -- see `tools/pthread_kthread_test/` for the
  real, minimal, verified exercise of the new path, and the Phase 7.5 entry
  below for the fuller design writeup.
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
  (2026-08-20, see below); item 5 (real Mach IPC for `ps` stats + blocking
  sleep) remains not started; real Apple `Libinfo` (the
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
  6. **CommonCrypto-shaped digest API** -- `md5` (and any future
     `sha1sum`/`shasum`-class command) needs a real userland digest API
     surface distinct from this project's existing from-scratch SHA-2
     (that's a low-level replacement for corecrypto's kernel-side
     `g_crypto_funcs` registration, not a userland `CC_MD5`/`CC_SHA*`-shaped
     API).
  7. **`posix_spawn(2)`** -- an unbuilt process-creation API; blocks
     `install`, which also needs its own real `strip(1)` subprocess helper
     once spawning works.
  8. **[CORRECTED 2026-08-20]** `sysctl(2)` is actually already real and
     general-purpose here, not unbuilt as this item originally claimed --
     `tools/userland_staging/libc_gen_syscalls.c`'s `sysctl()`/
     `sysctlbyname()` are a real, general raw-syscall wrapper (`SYS_sysctl`
     = 202, forwards all 6 args via `syscall6`), already proven end-to-end
     by the real `hostname(1)` command's `gethostname(3)`/`sethostname(3)`
     (`{CTL_KERN, KERN_HOSTNAME}`). What's still genuinely unexercised:
     the specific MIBs `df` (filesystem stats) and `killall`/`w`/`who`
     (`KERN_PROC` process enumeration) need -- likely just a normal
     command-port pass (wire `df.c`/`killall.c` against the existing
     `sysctl()`, see what's missing for real), not a whole unbuilt
     subsystem. Re-scope downward whenever this is picked up.
  9. **Real System V IPC** (`semget`/`shmget`/`msgget` family) -- blocks
     `ipcs`/`ipcrm`. `dd`'s more involved I/O-conversion logic wasn't
     surveyed in depth either; group it here for a first look.
  10. **zlib** -- real inflate-side decompression now exists (the
      grep/sed/nl/xargs/ed sweep's `gz_gzip_impl.c`, genuine DEFLATE via
      the real, unmodified zlib core at `third_party/u-boot/lib/zlib`),
      but only the narrow `gzdopen()`/`gzread()` subset grep's own
      `-Z`/`.gz` support needed -- no compression (deflate) side, no
      `gzip`(1)/`compress`(1) CLI wrapper. Still blocks `compress`/
      `gzip` as standalone commands.
  11. **Real `utmp`/`wtmp`/`lastlog` accounting file format + writers** --
      blocks `who`/`w`/`lastcomm` (distinct from item 8's `sysctl`
      dependency; both are needed for `who`/`w`).

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
  - **Tier A -- do next, real source already sitting in this checkout,
    unbuilt:** `ls` (`third_party/file_cmds/ls/` -- top priority, real
    directory listing does not exist in this project at all today,
    every `ls` invocation currently 404s as a missing command); `find`
    (`third_party/shell_cmds/find/`); `date`
    (`third_party/shell_cmds/date/`); `gzip`/`compress`
    (`third_party/file_cmds/gzip/`, `file_cmds/compress/` -- meaningfully
    easier now than when item 10 above was scoped, since the
    grep/sed/nl/xargs/ed sweep already pulled in a real, working zlib
    inflate core, `gz_gzip_impl.c` -- only the deflate/compress side and
    a CLI wrapper are still missing, not the whole library).
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
  1. **`getty`** (real, unmodified Apple source already unbuilt at
     `third_party/system_cmds/getty.tproj`, confirmed present) -- this
     project's login today is a single console session with `do_login()`
     baked directly into `tools/init_binary/init.c`'s own respawn loop,
     not a separate per-tty daemon. Real `getty` only starts mattering
     once there's more than one real tty to spawn onto (real hardware's
     serial console today, potentially real PTYs later) -- low urgency
     alone, but natural to land alongside the other two below.
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

    Real next step for whoever continues this: either (a) the narrower
    workqueue-mechanics scope surfaced above (fill in
    `workq_create_threadstack`/`workq_setup_thread`/
    `workq_destroy_threadstack`/`workq_markfree_threadstack`/
    `workq_handle_stack_events` to light up the already-real
    `pthread_workqueue.c` policy logic), or (b) a real minimal libpthread-
    ABI-compatible `bsdthread_create` (thread port + real register
    convention) as a prerequisite for any real userspace `pthread_create()`
    wrapper. `psynch_mutex*`/`psynch_cvwait`'s real backing primitive is
    very likely real Mach semaphores/turnstiles already used elsewhere in
    this kernel (`bsd/pthread/pthread_shims.c`'s own
    `psynch_wait_prepare`/`psynch_wait_wakeup` already call real
    `turnstile_prepare`/`waitq_assert_wait64_leeway`/
    `waitq_wakeup64_thread` -- real, open-source, unexplored in depth this
    session) -- not yet designed in detail, flagged rather than guessed at.
- **Phase 8 (`pkg` package management)**: fully planned, not started.
  `freebsd/pkg` is the chosen vehicle (native Mach-O awareness, real
  custom-repo support already documented). Needs `libsqlite`/`libucl`/
  `libelf`/`libyxml`/`libder`/`picosat` ported first (low risk), then
  `libecc` (shares Phase 7.5's/7b's crypto gap) and `libfetch` (needs
  Phase 6), then `liblua`, before a real custom package repo can be built
  and consumed on-device.
