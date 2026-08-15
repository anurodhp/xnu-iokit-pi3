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
#   - READLINE deliberately undefined (-UREADLINE) at compile time: a
#     real, normal bash configure option (bash historically ships
#     --disable-readline), NOT a hack. This target's console is raw
#     serial UART with no termcap/terminfo database, and real readline's
#     line-editing/display code (rltty.c/display.c) needs real terminal
#     ioctl semantics this project doesn't have. HISTORY stays real and
#     enabled (bash's history mechanism is a genuinely separate library
#     from readline's line editing -- lib/readline/history.c/histexpand.c/
#     histfile.c have no terminal/display dependency, confirmed by
#     grepping their real #include lists).
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
BASHSRC="../../third_party/bash/bash-3.2"
BASHTOP="../../third_party/bash"
OUT="libc_build"
BOUT="bash_build"
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
    -DHAVE_CONFIG_H -DSHELL=1 -DPROGRAM='\"bash\"' -UREADLINE \
    -DCONF_MACHTYPE='\"arm64-apple-darwin20.3.0\"' -DCONF_HOSTTYPE='\"arm64\"' \
    -DCONF_OSTYPE='\"darwin20.3.0\"' -DCONF_VENDOR='\"apple\"' \
    -DPACKAGE='\"bash\"' -DLOCALEDIR='\"/usr/share/locale\"' \
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
    bash_syscalls.c"

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
# part of the shell itself, excluded. complete.c is also excluded: unlike
# bind.c (real #if defined(READLINE) guard around its whole body, compiles
# to an empty TU cleanly), complete.c has NO such guard -- it unconditionally
# implements PROGRAMMABLE_COMPLETION, which is itself unconditionally
# real-readline-dependent (rl_* completion hooks). Real bash's own
# ./configure --disable-readline build genuinely omits this file from the
# link (autoconf-conditional, not an #ifdef inside the file) -- matched
# here by simply not compiling it, consistent with this project's real
# READLINE-disabled scope decision (see this script's own header comment).
BUILTIN_FILES=$(ls $BASHSRC/builtins/*.c | grep -v -e mkbuiltins.c -e psize.c -e complete.c)

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
    if $CLANG $CFLAGS "$BASHSRC/$f" -o "$BOUT/obj/${base}.o" >"$errfile" 2>&1; then
        ok=$((ok + 1)); rm -f "$errfile"
    else
        fail=$((fail + 1)); echo "  gap (core): $f -- $(grep -m1 'error:' "$errfile")"
    fi
done
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
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
        -o bash.macho \
        ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
        "$BOUT"/obj/*.o "$BOUT"/sys_stubs/*.o \
        "$OUT/libc_printf.a" "$OUT/libc_stdio.a" "$OUT/libc_locale.a" "$OUT/libc_gen.a" "$OUT/libc_string.a" \
        "$OUT/libunwind.a" "$OUT/libpthread.a" "$OUT/libclosure.a" "$OUT/libmalloc.a" "$OUT/libplatform.a" \
        2>"$BOUT/link.err" && echo "wrote bash.macho" || { echo "LINK FAILED, see $BOUT/link.err:"; tail -100 "$BOUT/link.err"; exit 1; }
    otool -l bash.macho | grep -q LC_LOAD_DYLIB && echo "WARNING: LC_LOAD_DYLIB present" >&2
    otool -l bash.macho | grep -q LC_UNIXTHREAD || echo "WARNING: no LC_UNIXTHREAD" >&2
    nm -u bash.macho | head -20
fi
