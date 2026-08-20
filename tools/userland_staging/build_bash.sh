#!/bin/bash
# Builds a real, statically-linked bash.macho from real, unmodified Apple
# bash 3.2 source (third_party/bash/bash-3.2, tag bash-123.40.1) against
# this project's curated libc_build/*.a stack -- the same Csu-static
# pattern every other tier in this directory uses, NOT dyld (this
# project's entire architecture is static; see architecture.md and every
# prior tier's zero-LC_LOAD_DYLIB verification). Promoted from
# build_bash_survey.sh (which only proved 21/33 core files compile) --
# see docs/roadmap.md for that survey's findings and this script's own
# additions on top of it:
#
#   - get_compat.h/.c: NOT an autoconf-generated file as the survey
#     guessed -- it's real, self-contained Apple Libc source
#     (third_party/Libc/gen/get_compat.{h,c}), just never added to the
#     include/compile list. Used by shell.c's is_rootless_restricted_
#     environment() via a real csops(2) syscall.
#   - compat_include/System/{sys/codesign.h,kern/cs_blobs.h}: forwarding
#     shims to the real content at third_party/xnu-7195/bsd/sys/
#     codesign.h and osfmk/kern/cs_blobs.h (same pattern as
#     build_libc_locale.sh's System/machine/cpu_capabilities.h shim) --
#     bash's real source expects the installed-SDK-style path Apple's
#     Xcode build produces via a symlink farm this non-Xcode build never
#     generates.
#   - -I $BASHSRC/lib: tilde/glob/readline header-not-found gaps were
#     never missing source, just missing this one include path (bash's
#     own bundled lib/tilde, lib/glob, lib/readline subdirectories are
#     real, present source).
#   - -DCONF_MACHTYPE/-DCONF_HOSTTYPE/-DCONF_OSTYPE/-DCONF_VENDOR/
#     -DPACKAGE/-DLOCALEDIR: real string values Makefile.in's
#     SYSTEM_FLAGS/LOCALE_DEFS derive from `configure`'s host-triple
#     substitution (Machine=@host_cpu@, OS=@host_os@, VENDOR=@host_vendor@,
#     MACHTYPE=@host@, from configure.in's AC_INIT([bash], ...) for
#     PACKAGE) -- this project's real target triple is arm64-apple-
#     darwin20.3.0 (Darwin 20.3.0 per CLAUDE.md), not guessed.
#   - READLINE is now genuinely ON (config.h's own #define, see that
#     file's 2026-08-19 header comment) -- it used to be forced off here
#     (-UREADLINE, historically real bash's own --disable-readline
#     option) on the belief that this target's console had no real tty
#     ioctl semantics for readline's raw-mode terminal handling. That
#     belief was never tested and turned out to be wrong: a disposable
#     ioctl(TIOCGETA/TIOCSETA) probe (see tools/userland_staging/
#     libc_gen_syscalls.c's ioctl/_ioctl header comment) proved this
#     target's real BSD tty stack is already fully wired. The one real
#     remaining gap -- no termcap/terminfo database -- is handled
#     honestly via bash_readline_termcap_compat.c (tgetent() reports "no
#     database", triggering lib/readline/terminal.c's own real,
#     documented dumb-terminal fallback, not a fabrication). HISTORY was
#     always real/enabled regardless (a genuinely separate library from
#     readline's line editing -- lib/readline/history.c/histexpand.c/
#     histfile.c have no terminal/display dependency, confirmed by
#     grepping their real #include lists) and needed no change here.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
BASHSRC="../../third_party/bash/bash-3.2"
BASHTOP="../../third_party/bash"
OUT="libc_build"
BOUT="bash_build"
# rm -rf "$BOUT/obj" first (2026-08-19, real bug caught the hard way):
# the final link step globs `"$BOUT"/obj/*.o` -- a stale .o left over
# from a PREVIOUS run whose source file list has since changed (e.g.
# lib/readline/shell.c and xmalloc.c, removed from EXTRA_FILES below
# once bash's own general.c/variables.c/lib/sh/shquote.c/xmalloc.c were
# found to already define the same sh_get_env_value/sh_single_quote/
# xmalloc/etc symbols) silently lingers and gets globbed in anyway,
# producing a real duplicate-symbol link failure that looks like a
# fresh bug but is actually just build-directory staleness. Same root
# cause as build_libc_gen.sh's own apple_gen/*.o glob hit earlier this
# same session (isatty.o lingering after isatty.c was removed from
# GEN_FILES) -- fixed there by removing the stale file by hand; fixed
# here for good by never letting the directory accumulate stale output
# in the first place.
rm -rf "$BOUT/obj"
mkdir -p "$BOUT/obj" "$BOUT/compat_include/System/sys" "$BOUT/compat_include/System/kern"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
for a in libc_string libplatform libc_locale libc_stdio libc_gen libc_printf libmalloc libpthread libclosure; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run its build_*.sh first" >&2
        exit 1
    fi
done

cp "$BASHTOP/config.h" "$BASHSRC/config.h"
if [ ! -f "$BASHSRC/y.tab.c" ]; then
    command -v bison >/dev/null 2>&1 || { echo "error: bison not found" >&2; exit 1; }
    ( cd "$BASHSRC" && bison -y -d parse.y -o y.tab.c )
fi
# Real host-build-only generator (same class as builtins/mkbuiltins,
# already-generated in this checkout) -- mksyntax.c itself IS real,
# unmodified Apple source; only its OUTPUT (syntax.c, the real
# sh_syntaxtab/sh_syntabsiz character-attribute table) isn't pre-shipped
# in this checkout the way builtins.c was, so it's built+run here.
if [ ! -f "$BASHSRC/syntax.c" ]; then
    MKSYNTAX_BIN="$PWD/$BOUT/mksyntax"
    cc -I "$BASHSRC" -I "$BASHSRC/include" -DHAVE_CONFIG_H "$BASHSRC/mksyntax.c" -o "$MKSYNTAX_BIN"
    ( cd "$BASHSRC" && "$MKSYNTAX_BIN" -o syntax.c syntax.h )
fi

cat > "$BOUT/compat_include/System/sys/codesign.h" <<'EOF'
#include <sys/codesign.h>
EOF
cat > "$BOUT/compat_include/System/kern/cs_blobs.h" <<'EOF'
#include <kern/cs_blobs.h>
EOF
cat > "$BOUT/pthread_rwlock_internal_alias.h" <<'EOF'
/* Real Apple private pthread headers declare the internal non-cancellable
 * rwlock names (_pthread_rwlock_rdlock/_wrlock/_unlock, used by
 * Libc/stdtime/FreeBSD/localtime.c's _RWLOCK_* macros) as real functions
 * distinct from the public pthread_rwlock_* API -- but libpthread.a as
 * this project builds it (with the same real -D__DARWIN_UNIX03=1 flag
 * every tier uses) only ever EXPORTS the $UNIX2003-suffixed symbol
 * (confirmed via nm: pthread_rwlock_rdlock$UNIX2003 exists, plain
 * pthread_rwlock_rdlock does not) -- real Apple's own pthread_rwlock.c
 * is built the exact same versioned-alias way internally. Real thin
 * wrappers in bash_syscalls.c (not a macro, since the target symbol
 * needs an explicit asm-name to reach the suffixed real one) provide
 * these -- see that file for the actual definitions.
 */
#ifndef _BASH_PTHREAD_RWLOCK_INTERNAL_ALIAS_H_
#define _BASH_PTHREAD_RWLOCK_INTERNAL_ALIAS_H_
extern int _pthread_rwlock_rdlock(void *);
extern int _pthread_rwlock_wrlock(void *);
extern int _pthread_rwlock_unlock(void *);
#endif
EOF

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h -include $BOUT/pthread_rwlock_internal_alias.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -DHAVE_CONFIG_H -DSHELL=1 -DPROGRAM=\"bash\" \
    -DCONF_MACHTYPE=\"arm64-apple-darwin20.3.0\" -DCONF_HOSTTYPE=\"arm64\" \
    -DCONF_OSTYPE=\"darwin20.3.0\" -DCONF_VENDOR=\"apple\" \
    -DPACKAGE=\"bash\" -DLOCALEDIR=\"/usr/share/locale\" \
    -I $BASHSRC -I $BASHSRC/include -I $BASHSRC/builtins -I $BASHSRC/lib -I $BASHTOP \
    -I $BOUT/compat_include -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall"

CORE_FILES="general.c variables.c shell.c subst.c eval.c execute_cmd.c \
    copy_cmd.c dispose_cmd.c make_cmd.c print_cmd.c error.c expr.c \
    flags.c jobs.c mailcheck.c trap.c input.c unwind_prot.c pathexp.c \
    sig.c test.c version.c alias.c array.c arrayfunc.c braces.c \
    bashhist.c list.c stringlib.c locale.c findcmd.c redir.c xmalloc.c \
    y.tab.c hashcmd.c hashlib.c siglist.c"

EXTRA_FILES="$LIBC/gen/get_compat.c \
    $BASHSRC/lib/tilde/tilde.c \
    $BASHSRC/lib/glob/glob.c $BASHSRC/lib/glob/strmatch.c $BASHSRC/lib/glob/smatch.c $BASHSRC/lib/glob/xmbsrtowcs.c \
    $BASHSRC/lib/readline/history.c $BASHSRC/lib/readline/histexpand.c $BASHSRC/lib/readline/histfile.c $BASHSRC/lib/readline/mbutil.c \
    $BASHSRC/lib/readline/histsearch.c \
    $BASHSRC/lib/sh/mailstat.c \
    $LIBC/stdlib/FreeBSD/heapsort.c $LIBC/stdtime/timezone_unix03.c \
    $LIBC/sys/sigaction.c \
    ../../third_party/libplatform/src/setjmp/generic/sigaction.c \
    $LIBC/sys/sigaction.c $LIBC/gen/confstr.c \
    $LIBC/compat-43/FreeBSD/killpg.c \
    $LIBC/stdlib/FreeBSD/qsort.c $LIBC/stdlib/FreeBSD/atoi.c $LIBC/stdlib/FreeBSD/atol.c \
    $LIBC/stdlib/FreeBSD/strtoul.c $LIBC/stdlib/FreeBSD/abs.c \
    $LIBC/stdtime/FreeBSD/localtime.c $LIBC/stdtime/FreeBSD/strftime.c $LIBC/stdtime/FreeBSD/asctime.c \
    $LIBC/gen/FreeBSD/siglist.c \
    $BASHSRC/syntax.c $BASHSRC/lib/sh/shmatch.c \
    bash_syscalls.c bash_readline_termcap_compat.c \
    $BASHSRC/lib/readline/readline.c $BASHSRC/lib/readline/vi_mode.c \
    $BASHSRC/lib/readline/funmap.c $BASHSRC/lib/readline/keymaps.c \
    $BASHSRC/lib/readline/parens.c $BASHSRC/lib/readline/search.c \
    $BASHSRC/lib/readline/rltty.c $BASHSRC/lib/readline/complete.c \
    $BASHSRC/lib/readline/bind.c $BASHSRC/lib/readline/isearch.c \
    $BASHSRC/lib/readline/display.c $BASHSRC/lib/readline/signals.c \
    $BASHSRC/lib/readline/util.c $BASHSRC/lib/readline/kill.c \
    $BASHSRC/lib/readline/undo.c $BASHSRC/lib/readline/macro.c \
    $BASHSRC/lib/readline/input.c $BASHSRC/lib/readline/callback.c \
    $BASHSRC/lib/readline/terminal.c $BASHSRC/lib/readline/text.c \
    $BASHSRC/lib/readline/nls.c $BASHSRC/lib/readline/misc.c \
    $BASHSRC/lib/readline/savestring.c $BASHSRC/lib/readline/compat.c \
    $BASHSRC/pcomplete.c $BASHSRC/pcomplib.c $BASHSRC/bashline.c $BASHSRC/bracecomp.c \
    simple_cmds_getenv_compat.c"
# pcomplete.c + pcomplib.c: real bash's own programmable-completion
# engine, split across two real files -- pcomplete.c has
# gen_compspec_completions()/programmable_completions()/the progcomp_*
# hash-table walkers, but compspec_create()/compspec_dispose()/
# compspec_copy() themselves (the COMPSPEC struct's own alloc/free/copy)
# live in the separate pcomplib.c (confirmed via grep + Makefile.in's
# own LIBOBJS list, not guessed -- a first attempt at just pcomplete.c
# alone left those three genuinely undefined). builtins/complete.c (now
# compiled, see BUILTIN_FILES below) needs both.
# bashline.c: real bash's own readline-integration glue (command-aware
# completion, custom keybindings, `bind`/`read -e` support) -- 3199
# lines, real unmodified Apple source, same -I paths (<readline/...>,
# <glob/...>) every other lib/ subdirectory already resolves via
# `-I $BASHSRC/lib`.
# simple_cmds_getenv_compat.c: real, already-proven-correct setenv/
# getenv/unsetenv/putenv/_NSGetEnviron (tools/userland_staging/
# build_simple_cmds.sh's own tier, Phase 4.6 item 2) -- bash needed a
# real setenv() for real lib/readline/shell.c's sh_set_lines_and_columns()
# (exports COLUMNS/LINES after a window resize) and had NONE at all
# before (confirmed via nm: no libc_*.a bash already links defines
# setenv/putenv/unsetenv anywhere). Reused directly rather than
# reimplemented -- this file's own getenv() also takes priority over
# libc_locale.a's much older always-NULL getenv() stand-in (a real,
# separate, PRE-EXISTING bug this incidentally fixes for bash too: since
# object files listed directly on a link line always resolve a symbol
# before the linker ever needs to pull in an archive member offering the
# same name, libc_locale.a's own broken libc_locale_env_stubs.o member
# is simply never pulled into bash's link once this file provides real
# getenv/setenv/unsetenv/putenv directly).
# The lib/readline/*.c files above are lib/readline/Makefile.in's own
# real OBJECTS list (READLINE re-enabled 2026-08-19, see config.h's own
# header comment for why) MINUS shell.c and xmalloc.c -- both real, but
# deliberately excluded, confirmed the hard way (a real duplicate-symbol
# link error, not guessed): bash's own top-level general.c/variables.c/
# lib/sh/shquote.c and xmalloc.c already provide the exact same
# sh_get_env_value/sh_set_lines_and_columns/sh_get_home_dir/
# sh_single_quote/sh_unset_nodelay_mode/xmalloc/xfree/xrealloc symbols
# lib/readline/shell.c and xmalloc.c also define -- those two
# lib/readline files exist for libreadline's STANDALONE build (where
# nothing else provides those hooks), not for embedding inside bash
# itself, which supplies its own real versions instead (matching real
# bash's own Makefile, which excludes them from bash's own link for the
# identical reason). emacs_keymap.c/vi_keymap.c are real too but
# deliberately NOT listed here either (also matching Makefile.in):
# keymaps.c #includes them directly as static data tables, real bash
# never compiles them as separate translation units.

# Real bash-3.2's own bundled lib/sh/*.c portable helpers (zread/zwrite/
# xstrchr/stringvec/timeval/itos/etc) -- real Apple's own build compiles
# these unconditionally (they are bash-specific utilities, not libc
# fallback shims -- confirmed by checking they're always in Makefile.in's
# SHOBJ list, not gated behind any HAVE_* autoconf conditional). mktime/
# strftime/rename/strcasecmp/snprintf/wcsdup/inet_aton/netconn/netopen/
# mailstat overlap with real Libc equivalents already linked elsewhere in
# this project or genuinely unneeded (no real network stack -- Phase 6
# scope) -- excluded to avoid duplicate-symbol collisions; kept the rest.
SH_FILES=""
for f in clktck clock eaccess fmtullong fmtulong fmtumax getcwd itos \
         makepath oslib pathcanon pathphys setlinebuf shquote shtty \
         spell strindex stringlist stringvec strnlen strpbrk strtrans \
         times timeval tmpfile vprint winsize xstrchr zcatfd zread zwrite; do
    SH_FILES="$SH_FILES $BASHSRC/lib/sh/${f}.c"
done

# Real, already-mkbuiltins-generated builtin command implementations --
# this Apple bash-123.40.1 checkout ships builtins/*.c pre-generated from
# the real .def files (confirmed: builtins.c's own header says "This file
# is manufactured by ./mkbuiltins, and should not be edited by hand" and
# is already present, along with builtext.h) -- no need to run mkbuiltins
# ourselves. mkbuiltins.c/psize.c are host-build-only helper tools, not
# part of the shell itself, excluded.
#
# builtins/complete.c (the `complete`/`compgen`/`compopt` builtin
# commands -- NOT lib/readline/complete.c, readline's own internal
# tab-completion engine, added separately above) USED to be excluded
# here too: it has no `#if defined (READLINE)` guard around its body the
# way e.g. lib/readline/bind.c does (that one compiles to a clean empty
# TU when READLINE is off), so it unconditionally needs real rl_*
# completion hooks. Real bash's own `./configure --disable-readline`
# build genuinely omits it from the link the same way (autoconf-
# conditional, not an #ifdef). Now that READLINE is genuinely on (see
# config.h's own header comment for why), this file belongs in the
# build for real -- re-added.
BUILTIN_FILES=$(ls $BASHSRC/builtins/*.c | grep -v -e mkbuiltins.c -e psize.c)

# Real Apple-generated raw syscall stubs (Perl generator, same pattern as
# build_libsyscall_smoketest.sh) for the real, non-NO_SYSCALL_STUB
# syscalls bash needs that no prior tier had a caller for yet.
SYS_STUB_NAMES="setuid getuid geteuid getppid getegid getgid sigprocmask \
    execve umask getgroups getpgrp setpgid setgid chdir dup dup2 \
    getrusage csops readlink pathconf"
mkdir -p "$BOUT/sys_stubs" "$BOUT/sys_asm_compat/mach/arm64"
cat > "$BOUT/sys_asm_compat/mach/arm64/asm.h" <<'EOF'
/* SYS.h expects the installed-SDK-style path mach/arm64/asm.h (a real
 * symlink Apple's SDK build produces); real content is osfmk/arm64/asm.h
 * in this checkout -- same forwarding-shim pattern as every other
 * System/* compat header in this project. */
#include <arm64/asm.h>
EOF
TMPGEN="$(mktemp -d)"
trap 'rm -rf "$TMPGEN"' EXIT
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" \
    "$XNU/libsyscall/custom" \
    "$XNU/libsyscall/Platforms" \
    iPhoneOS \
    "$TMPGEN" >/dev/null
for n in $SYS_STUB_NAMES; do
    f="$TMPGEN/_${n}.s"
    [ -f "$f" ] || { echo "error: expected generated stub _${n}.s not found" >&2; exit 1; }
    cp "$f" "$BOUT/sys_stubs/"
done

ok=0
fail=0
for f in $CORE_FILES; do
    base=$(basename "$f" .c)
    errfile="$BOUT/obj/${base}.err"
    # shell.c specifically: real bash's own main() never calls this
    # project's pthread_main_thread_bootstrap() (see bash_main_wrapper.c's
    # header for the full real bug this fixes -- real bash's link never
    # pulled in the real pthread_once/etc implementation at all, and even
    # if it had, real pthread_tsd.c's pthread_getspecific() has no
    # defensive self-bootstrap the way the old shim did). Renaming main
    # via -D lets bash_main_wrapper.c be the real process entry point
    # instead, bootstrapping for real before calling into real bash.
    if [ "$base" = "shell" ]; then
        FCFLAGS="$CFLAGS -Dmain=bash_shell_real_main"
    else
        FCFLAGS="$CFLAGS"
    fi
    if $CLANG $FCFLAGS "$BASHSRC/$f" -o "$BOUT/obj/${base}.o" >"$errfile" 2>&1; then
        ok=$((ok + 1)); rm -f "$errfile"
    else
        fail=$((fail + 1)); echo "  gap (core): $f -- $(grep -m1 'error:' "$errfile")"
    fi
done
errfile="$BOUT/obj/bash_main_wrapper.err"
if $CLANG $CFLAGS bash_main_wrapper.c -o "$BOUT/obj/bash_main_wrapper.o" >"$errfile" 2>&1; then
    ok=$((ok + 1)); rm -f "$errfile"
else
    fail=$((fail + 1)); echo "  gap (wrapper): bash_main_wrapper.c -- $(grep -m1 'error:' "$errfile")"
fi
for f in $EXTRA_FILES; do
    # Unique per-file object naming (not just basename): two real Apple
    # checkouts both happen to ship a file literally named sigaction.c
    # (Libc/sys/sigaction.c and libplatform/src/setjmp/generic/
    # sigaction.c) -- a basename-only scheme silently overwrote one
    # with the other's .o, a real bug caught via a genuine link failure
    # (undefined ___platform_sigaction) before this fix, not by inspection.
    base=$(echo "$f" | sed 's/[\/.]/_/g')
    errfile="$BOUT/obj/${base}.err"
    if $CLANG $CFLAGS "$f" -o "$BOUT/obj/${base}.o" >"$errfile" 2>&1; then
        ok=$((ok + 1)); rm -f "$errfile"
    else
        fail=$((fail + 1)); echo "  gap (extra): $f -- $(grep -m1 'error:' "$errfile")"
    fi
done
SHCFLAGS="$CFLAGS -I $BASHSRC/lib/sh"
for f in $SH_FILES; do
    base=$(basename "$f" .c)
    errfile="$BOUT/obj/sh_${base}.err"
    if $CLANG $SHCFLAGS "$f" -o "$BOUT/obj/sh_${base}.o" >"$errfile" 2>&1; then
        ok=$((ok + 1)); rm -f "$errfile"
    else
        fail=$((fail + 1)); echo "  gap (lib/sh): $f -- $(grep -m1 'error:' "$errfile")"
    fi
done
BCFLAGS="$CFLAGS -I $BASHSRC/builtins"
for f in $BUILTIN_FILES; do
    base=$(basename "$f" .c)
    errfile="$BOUT/obj/builtin_${base}.err"
    if $CLANG $BCFLAGS "$f" -o "$BOUT/obj/builtin_${base}.o" >"$errfile" 2>&1; then
        ok=$((ok + 1)); rm -f "$errfile"
    else
        fail=$((fail + 1)); echo "  gap (builtin): $f -- $(grep -m1 'error:' "$errfile")"
    fi
done
echo "bash core+extra+builtins: $ok compiled clean, $fail gaps"

SYSCLANG_FLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -arch arm64 -I$BOUT/sys_asm_compat -I$XNU/libsyscall/custom -I$XNU/libsyscall/mach -I$XNU/osfmk"
for n in $SYS_STUB_NAMES; do
    $CLANG $SYSCLANG_FLAGS "$BOUT/sys_stubs/_${n}.s" -o "$BOUT/sys_stubs/${n}.o"
done

if [ "$fail" -eq 0 ]; then
    # Two real bugs found+fixed via QEMU debugging of the Eighteenth
    # pass's real bash SIGSEGV (docs/roadmap.md's Nineteenth pass has the
    # full writeup with real evidence -- summary only here):
    #
    # Bug A: libc_locale.a bundles pthread_tsd_shim.o (the pre-libpthread
    # stand-in), which exports the same 4 public POSIX names
    # (pthread_once/pthread_key_create/pthread_setspecific/
    # pthread_getspecific) as libpthread.a's real pthread_tsd.o/
    # pthread_main_thread_bootstrap.o. ld64 scans static archives in a
    # single left-to-right pass; real Libc source compiled INTO
    # libc_locale.a itself (strsignal.c's real thr_once/thr_keycreate)
    # demands _pthread_getspecific WHILE libc_locale.a is being scanned,
    # so it's satisfied locally from pthread_tsd_shim.o regardless of
    # where libpthread.a sits on the link line (confirmed empirically --
    # reordering alone did not change which _pthread_once got linked).
    # Since pthread_tsd_shim.o bundles all 4 names together, that pulls in
    # its OLD naive pthread_once too, with zero duplicate-symbol error
    # (libpthread.a's real pthread_main_thread_bootstrap.o was never
    # independently referenced by anything, so never loaded at all).
    # Fixed in pthread_tsd_shim.c itself: its 4 public POSIX function
    # DEFINITIONS are now __attribute__((weak)) -- a consumer linking only
    # libc_locale.a still gets these as before (zero regression); a
    # consumer that ALSO links libpthread.a (this one) gets the real
    # strong ones instead. Archive-member removal (the same pattern
    # already used for libmalloc.a in build_libpthread.sh) was tried
    # first and rejected: libc_locale.a's copy is genuinely load-bearing
    # for OTHER real symbols (pthread_tsd_shim_bootstrap, needed by real
    # libmalloc.a's _get_zone; the underscore-prefixed aliases needed by
    # real strsignal.c/localtime.c) that every OTHER standalone smoketest
    # (stdio/gen/printf/malloc) still depends on and doesn't separately
    # link libpthread.a to get elsewhere -- removing the member outright
    # broke all of them, confirmed by hitting exactly that regression.
    #
    # Bug B: even with Bug A fixed, libpthread.a's real
    # pthread_main_thread_bootstrap.o still never entered the link at
    # all -- nothing in bash's own code (real, unmodified Apple source)
    # calls pthread_main_thread_bootstrap() or references any other
    # symbol unique to that object file, so Bug A's weak-symbol fix had
    # no strong definition to override with. Fixed via bash_main_wrapper.c
    # (new, in $BOUT/obj/*.o below) + shell.c compiled with
    # -Dmain=bash_shell_real_main above: the wrapper's real main() calls
    # pthread_main_thread_bootstrap() first (forcing that object's real
    # pthread_once/etc into the link AND actually running the bootstrap,
    # which real pthread_tsd.c's pthread_getspecific() needs -- unlike
    # the old shim, it has no defensive self-bootstrap), then calls real
    # bash's own main() under its new name.
    #
    # Both verified via disassembly: bash.macho's real _pthread_once now
    # disassembles to _os_once's real cmpxchg sequence (not the naive
    # shim's cmp x8,#0x1 pattern), and real _main correctly calls
    # _pthread_main_thread_bootstrap before _bash_shell_real_main. Real
    # bash STILL crashes after both fixes, on a separate, real, precisely
    # captured bug (Nineteenth pass) -- not yet fixed, see docs/roadmap.md.
    # -Wl,-alias,_pthread_testcancel,... (2026-08-19): real termios.c
    # (build_libc_gen.sh, real tcgetattr/tcsetattr for real job control -
    # see that script's own header comment) pulls its whole .o member in
    # the moment bash's real job.c calls tcgetattr, which drags
    # __tcdrain's real pthread_testcancel() call along for the ride even
    # though nothing calls tcdrain(3) itself - same real gap, same real
    # fix (libpthread.a only exports the $UNIX2003-suffixed symbol) as
    # build_ps.sh already found for the identical real termios.c.
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
        -o bash.macho \
        ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
        "$BOUT"/obj/*.o "$BOUT"/sys_stubs/*.o \
        "$OUT/libc_printf.a" "$OUT/libc_stdio.a" "$OUT/libc_locale.a" "$OUT/libc_gen.a" "$OUT/libc_string.a" \
        "$OUT/libunwind.a" "$OUT/libpthread.a" "$OUT/libclosure.a" "$OUT/libmalloc.a" "$OUT/libplatform.a" \
        -Wl,-alias,_pthread_testcancel,'_pthread_testcancel$UNIX2003' \
        -Wl,-alias,_setenv,'_setenv$UNIX2003' \
        -Wl,-alias,_select,'_select$1050' \
        -Wl,-alias,_fopen,'_fopen$UNIX2003' \
        2>"$BOUT/link.err" && echo "wrote bash.macho" || { echo "LINK FAILED, see $BOUT/link.err:"; tail -100 "$BOUT/link.err"; exit 1; }
    otool -l bash.macho | grep -q LC_LOAD_DYLIB && echo "WARNING: LC_LOAD_DYLIB present" >&2
    otool -l bash.macho | grep -q LC_UNIXTHREAD || echo "WARNING: no LC_UNIXTHREAD" >&2
    nm -u bash.macho | head -20
fi
