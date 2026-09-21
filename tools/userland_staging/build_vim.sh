#!/bin/bash
# Builds a real, dynamically-linked vim.macho from real, unmodified Apple
# vim source (third_party/vim, tag vim-91.80.1 -- exact macos-112 manifest
# match, see docs/apple-oss-catalog.md). DAR-185 (Phase 7d).
#
# Feature tier: Apple's own checkout ships a PRE-GENERATED src/auto/config.h
# (not run through ./configure at Apple's own build time either -- it is
# checked into the repo, "DO NOT EDIT" per its own header). Using it as-is,
# unmodified, is both the real-Apple-parity choice and the practical one:
# it is written to adapt to whichever arch actually compiles it via
# compiler builtins (`#if __LP64__` for VIM_SIZEOF_LONG, `#if __BIG_ENDIAN__`
# for WORDS_BIGENDIAN, etc), not hardcoded values from whatever host
# Apple's own autoconf ran on. Confirmed real tier: FEAT_NORMAL (not
# SMALL/BIG/HUGE), TERMINFO+HAVE_ICONV on, every scripting-language
# interpreter binding (PERL/PYTHON/RUBY/LUA/TCL/MZSCHEME) and the GUI off
# -- i.e. real Apple's own shipped /usr/bin/vim is already a plain-terminal,
# non-scripted build, matching this project's own stated goals with no
# scope-narrowing needed here. FEAT_JOB_CHANNEL/FEAT_NETBEANS_INTG/
# FEAT_TERMINAL are on, so channel.c/netbeans.c/src/libvterm's real 9 files
# (excluding libvterm's own t/harness.c unit-test driver) are compiled in
# alongside BASIC_SRC's real 96 files -- real Apple's SRC list for ANY
# feature tier, not a GUI-only list -- rather than editing Apple's real
# generated config.h to turn them off.
#
# auto/pathdef.c is ALSO Apple's real checked-in generated file (its own
# header says the same "DO NOT EDIT, change Makefile only") -- reused
# as-is. Its default_vim_dir/default_vimruntime_dir/all_cflags/all_lflags
# are purely informational strings :version prints, with no functional
# effect on the binary; this project does not yet ship vim's runtime/
# (syntax/indent/help files) onto the SD image, tracked as a real,
# separate follow-on gap (same class as DAR-293's terminfo/termcap gap),
# not a build blocker.
#
# Toolchain: Xcode-12's iPhoneOS14.4 SDK, same as every other script in
# this directory -- see CLAUDE.md's "Build commands" section.

set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi

# Same real ld64-609-crash-under-fixup_chains workaround every dynamic
# executable in this directory uses -- see build_syslogd.sh's own header
# comment for the full trail (docs/backlog.md item d9).
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi

XNU="../../third_party/xnu-7195"
LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
NC="../../third_party/ncurses/ncurses"
LIBICONV_ROOT="../../third_party/libiconv/libiconv"
VIM="../../third_party/vim"
OUT="libc_build"

if [ ! -f "$VIM/src/vim.h" ]; then
    echo "error: $VIM not found -- run setup_third_party.sh first" >&2
    exit 1
fi
if [ ! -d "$OUT/ncurses_derived" ]; then
    echo "error: $OUT/ncurses_derived not found -- run build_ncurses.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libncurses.dylib" ]; then
    echo "error: $OUT/system/libncurses.dylib not found -- run build_ncurses_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libiconv.dylib" ]; then
    echo "error: $OUT/system/libiconv.dylib not found -- run build_libiconv_dylib.sh first" >&2
    exit 1
fi
for d in libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld libSystem.B; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run its build_*.sh first" >&2
        exit 1
    fi
done

VOUT="$OUT/vim_obj"
mkdir -p "$VOUT"

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -Wno-implicit-function-declaration \
    -DHAVE_CONFIG_H -D_LIBC_NO_FEATURE_VERIFICATION \
    -I vim_compat_include \
    -I "$VIM/src" -I "$VIM/src/proto" -I "$VIM/src/auto" \
    -I "$NC/include" -I "$OUT/ncurses_derived" \
    -I "$LIBICONV_ROOT/include" \
    -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libsyscall" -idirafter "$XNU/libkern" \
    -idirafter "$LIBC/include" -idirafter "$LIBC/darwin" -idirafter "$LIBC" \
    -idirafter "$PTHREAD/include" -idirafter "$PTHREAD/private" \
    -idirafter "$PLATFORM/private" -idirafter "$PLATFORM/include")

ok=0
fail=0
# DAR-185: src/libvterm/src/{screen,mouse}.c real basenames collide with
# vim's own src/{screen,mouse}.c -- a plain basename-keyed object path
# silently let the second compile() call overwrite the first's real .o
# with no error from either compile (found via nm on the "vim" screen.o
# showing only libvterm's real symbols, none of vim's own). Libvterm
# objects get their own subdirectory so no name can ever collide with a
# top-level vim src object.
mkdir -p "$VOUT/libvterm"
compile() {
    local src="$1"
    shift
    local subdir="${VIM_COMPILE_SUBDIR:-}"
    local base
    base=$(basename "$src" .c)
    local out="$VOUT/${subdir}${base}.o"
    if "$CLANG" "${CFLAGS[@]}" "$@" "$src" -o "$out" 2>"$VOUT/${subdir}${base}.err"; then
        ok=$((ok + 1))
        rm -f "$VOUT/${subdir}${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: ${subdir}${base} -- $(grep -m1 'error:' "$VOUT/${subdir}${base}.err")"
    fi
}

# Real Apple BASIC_SRC list (src/Makefile's own BASIC_SRC variable, 96
# real files -- the real base every feature tier compiles, GUI or not).
for f in arabic arglist autocmd beval blob blowfish buffer change charset \
         cindent clientserver clipboard cmdexpand cmdhist crypt crypt_zip \
         debugger dict diff digraph drawline drawscreen edit eval evalbuffer \
         evalfunc evalvars evalwindow ex_cmds ex_cmds2 ex_docmd ex_eval \
         ex_getln fileio filepath findfile fold getchar hardcopy hashtab \
         highlight if_cscope if_xcmdsrv indent insexpand json list main map \
         mark mbyte memfile memline menu message misc1 misc2 mouse move \
         normal ops option optionstr os_unix popupmenu popupwin profiler pty \
         quickfix regexp register screen scriptfile search session sha256 \
         sign sound spell spellfile spellsuggest syntax tag term terminal \
         testing textformat textobject textprop time typval ui undo usercmd \
         userfunc version vim9compile vim9execute vim9script viminfo window \
         bufwrite; do
    compile "$VIM/src/$f.c"
done
compile "$VIM/src/auto/pathdef.c"

# FEAT_JOB_CHANNEL / FEAT_NETBEANS_INTG are both on in Apple's real
# config.h -- real Apple's own EXTRA_SRC members these two features need.
compile "$VIM/src/channel.c"
compile "$VIM/src/netbeans.c"

# pty.c/channel.c need real grantpt()/posix_openpt()/ptsname()/unlockpt() --
# this project has no libutil port (the real Darwin home for these), but
# real Apple Libc's own stdlib/grantpt.c (already pinned, third_party/Libc)
# implements all four directly. Same real os/assumes.h chain DAR-247
# (build_libsystem_c.sh/build_libresolv.sh's arc4random.c fix) already
# solved -- reusing that exact compat dir rather than re-deriving it.
compile "$LIBC/stdlib/grantpt.c" -I arc4random_compat_include

# ___sprintf_chk/___vsprintf_chk (real Apple _FORTIFY_SOURCE wrappers,
# Libc/secure/) call __chk_fail_overflow()/__chk_fail_overlap(), already
# real+built as $OUT/chk_fail.o by build_libmalloc.sh -- reused at link
# time below rather than recompiled.
#
# -D_FORTIFY_SOURCE=0 here is load-bearing, not hygiene, and this build
# proved it the hard way: without it the fortify inlines in <stdio.h>
# rewrite __vsprintf_chk()'s own internal fall-through call to plain
# vsprintf() back into __vsprintf_chk() -- i.e. into itself, passing
# __darwin_obsz(dest) == (size_t)-1 because dest is a pointer parameter,
# which is exactly the "unbounded, so call the plain function" branch it
# just came from. Infinite recursion -> stack exhaustion -> SIGSEGV. Same
# real root cause as launchd bug #6 (see build_corefoundation.sh's and
# build_launchd_core.sh's own secure/ compiles, which already do this).
# Confirmed in the built object, not assumed: `otool -tV` on the old
# vsprintf_chk.o showed `bl ___vsprintf_chk` at +0x58 inside
# ___vsprintf_chk itself.
#
# It reached vim as a real crash rather than a theoretical one because
# the recursive branch only runs when __builtin_object_size(dest) is
# unknown: main.c's command_line_scan() ends with
# `sprintf(p, ":%s\r", parmp->commands[0])` into a pointer from vim's own
# alloc(), so ANY `vim -c <cmd>` (including `-es -c q`) died right after
# "inits 1", while every fixed-size-buffer sprintf in vim was fine.
compile "$LIBC/secure/sprintf_chk.c" -D_FORTIFY_SOURCE=0
compile "$LIBC/secure/vsprintf_chk.c" -D_FORTIFY_SOURCE=0

# Real utime() (Libc/gen/FreeBSD/utime.c) -- os_unix.c's mch_settime().
compile "$LIBC/gen/FreeBSD/utime.c"

# sigsetjmp()/siglongjmp() (os_unix.c's real SIGSEGV/SIGBUS/SIGILL
# stack-overflow recovery path, HAVE_SIGALTSTACK+HAVE_SIGCONTEXT both on
# in Apple's config.h) -- this project's own real, already-proven
# ed_setjmp_compat.c/ed_setjmp_raw.s (written for text_cmds/ed, see that
# file's own header for why real Apple's setjmp.s can't be used as-is
# here). Reusing rather than re-deriving; $OUT/ed_setjmp_raw.o already
# built, linked in below.
compile ed_setjmp_compat.c

# Real sigaltstack(2) C wrapper: Apple's real Libc/compat-43/sigaltstk.c
# (MINSIGSTKSZ check, then a call to the raw `__sigaltstack` trap).
# syscalls.master marks sigaltstack NO_SYSCALL_STUB, so the raw half is
# Apple's own hand-written libsyscall/custom/__sigaltstack.s, already
# built as $OUT/simple_obj/sys____sigaltstack.o and linked below --
# exactly the pair build_simple_cmds.sh's diff(1) tier already uses for
# the same real function (see its own comment at the sigaltstack block).
compile "$LIBC/compat-43/sigaltstk.c"

# Real sigset(3). os_unixx.h's `#if defined(HAVE_SIGSET) && !defined(signal)
# # define signal sigset` (Apple's config.h defines HAVE_SIGSET) turns
# every real signal() call in os_unix.c into a sigset() call, and the
# pinned third_party/Libc has no sigset.c at all -- see
# vim_sigset_compat.c's own header for the real X/Open semantics it
# implements in terms of real sigaction(2).
compile vim_sigset_compat.c

# rename(2) + Libc signal.c's _sigaction/_sigaction_nobind -- see
# vim_extra_syscalls.c's own header comment for both.
compile vim_extra_syscalls.c

# Real getrlimit(2) raw syscall (os_unix.c's mch_total_mem(), sizing
# vim's own memory limit). NO_SYSCALL_STUB in syscalls.master; reusing
# this project's already-proven single-function xargs_getrlimit_compat.c
# rather than hand-writing a second copy of it.
compile xargs_getrlimit_compat.c

# Real strptime(3) (time.c's f_strptime(), the vimscript strptime()
# builtin). Not exported by ANY dylib this project has built (confirmed
# via nm -gU over libc_build/system/*.dylib), so Apple's real
# stdtime/FreeBSD/strptime.c is compiled here. Its own real
# FreeBSD-locale compile recipe (-D__LIBC__=1 -DXLOCALE_STATE_EXPORTED=1
# + syslimits.h for COLL_WEIGHTS_MAX, which -ffreestanding's <limits.h>
# hides) is build_launchd_syscalls.sh's already-proven
# CFLAGS_LAUNCHD_LIBC set, not re-derived here.
#
# Every one of its undefined symbols resolves to a REAL export of the
# dylibs already on vim's link line (___global_locale, ___locale_key,
# __c_locale, _localtime_r, _gmtime_r, _timegm, _timeoff, _tzset,
# _tzname, the *_l ctype/strto* family -- all libsystem_c.dylib)
# EXCEPT __get_current_time_locale, which real libsystem_c.dylib keeps
# local/non-exported. That one comes from its real owning Apple file,
# stdtime/FreeBSD/timelocal.c, compiled here too -- deliberately NOT
# strftime.c/localtime.c alongside it (build_chrony.sh's own chain):
# those two ARE real dylib exports (_strftime$UNIX2003, _localtime_r),
# and a local static copy of either would give vim a SECOND timezone
# state diverging from the dylib's -- this project's repeatedly-hit
# duplicate-static-copy bug class. timelocal.o's own two definitions
# (__get_current_time_locale, __time_load_locale) are not dylib exports
# at all, so nothing is shadowed; the time-locale part state it loads is
# the real "C" default this project's locale support is limited to
# anyway (libc_locale's own documented ceiling).
FBSD_TIME_INC=(-D__LIBC__=1 -DXLOCALE_STATE_EXPORTED=1 \
    -include "$XNU/bsd/sys/syslimits.h" \
    -I "$LIBC/fbsdcompat" -include "$LIBC/fbsdcompat/_fbsd_compat_.h" \
    -I "$LIBC/stdtime/FreeBSD" -I "$LIBC/locale" -I "$LIBC/locale/FreeBSD" \
    -I "$OUT/compat_include")
compile "$LIBC/stdtime/FreeBSD/strptime.c" "${FBSD_TIME_INC[@]}"
compile "$LIBC/stdtime/FreeBSD/timelocal.c" "${FBSD_TIME_INC[@]}"

# FEAT_TERMINAL is on -- real libvterm (src/libvterm/src/*.c, 9 real
# files; NOT t/harness.c, libvterm's own unit-test driver, no caller here).
# -DINLINE="": real libvterm's own Makefile sets this (not autoconf'd --
# libvterm has no configure script -- a real, fixed build-time constant
# every libvterm consumer, including Apple's, is expected to supply).
VIM_COMPILE_SUBDIR="libvterm/"
for f in encoding keyboard mouse parser pen screen state unicode vterm; do
    compile "$VIM/src/libvterm/src/$f.c" -I "$VIM/src/libvterm/include" -DINLINE=""
done
VIM_COMPILE_SUBDIR=

echo "vim: $ok compiled, $fail gaps (see $VOUT/*.err for full detail)"

if [ "$fail" -eq 0 ]; then
    ar rcs "$OUT/vim_objs.a" "$VOUT"/*.o "$VOUT"/libvterm/*.o
    echo "wrote $OUT/vim_objs.a"
fi

# --- real raw syscall stubs ---
# Four real syscalls vim needs that no prior tier's SYS_STUBS_NEEDED list
# ever generated, all plain (NOT NO_SYSCALL_STUB) entries in
# third_party/xnu-7195/bsd/kern/syscalls.master -- read directly, not
# guessed: fchdir #13 (os_unix.c's mch_FullName(), restoring the cwd
# after resolving a relative path), sync #36 (memfile.c's mf_sync(),
# :preserve/swapfile durability), getsockopt #118 (channel.c's
# channel_connect(), real SO_ERROR completion check on a non-blocking
# connect), setsid #147 (os_unix.c's mch_call_shell_fork()/mch_job_start()
# and if_cscope.c's cs_create_connection(), detaching the child).
# Generated with Apple's own libsyscall/xcodescripts/create-syscalls.pl,
# same invocation build_simple_cmds.sh uses -- real Apple asm, not
# hand-written. Kept in vim's own object dir (not $OUT/simple_obj/) since
# build_simple_cmds.sh guards its whole generation block behind a
# sentinel file and would not regenerate for a newly-added name.
VIM_SYS_STUBS="_fchdir _sync _getsockopt _setsid"
need_stubs=0
for s in $VIM_SYS_STUBS; do
    [ -f "$VOUT/syscalls/sys_${s}.o" ] || need_stubs=1
done
if [ "$need_stubs" -eq 1 ]; then
    # Own subdirectory, deliberately: the `ar rcs ... "$VOUT"/*.o` step
    # above must never pick these up -- they are listed explicitly on the
    # link line, and a second run of this script would otherwise both
    # archive them and list them, for a duplicate-symbol link failure.
    mkdir -p "$VOUT/syscalls" "$OUT/sys_asm_compat/mach/arm64"
    cat > "$OUT/sys_asm_compat/mach/arm64/asm.h" <<'EOF'
/* SYS.h expects the installed-SDK-style path mach/arm64/asm.h; real
 * content is osfmk/arm64/asm.h in this checkout -- same forwarding-shim
 * pattern build_simple_cmds.sh/build_bash.sh already use. */
#include <arm64/asm.h>
EOF
    SYS_TMPGEN="$(mktemp -d)"
    trap 'rm -rf "$SYS_TMPGEN"' EXIT
    ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
        "$XNU/bsd/kern/syscalls.master" \
        "$XNU/libsyscall/custom" \
        "$XNU/libsyscall/Platforms" \
        iPhoneOS \
        "$SYS_TMPGEN" >/dev/null
    for s in $VIM_SYS_STUBS; do
        "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 \
            -I"$SYS_TMPGEN" -I"$OUT/sys_asm_compat" -I"$XNU/libsyscall/custom" \
            -I"$XNU/libsyscall/mach" -I"$XNU/osfmk" \
            "$SYS_TMPGEN/${s}.s" -o "$VOUT/syscalls/sys_${s}.o" \
            || { echo "error: $s raw syscall stub failed" >&2; exit 1; }
    done
    rm -rf "$SYS_TMPGEN"
    trap - EXIT
fi

# --- link ---
# DYNAMIC LINK (DAR-314, 2026-09-15 standing rule): every shipped binary
# is dynamic except tools/pwtool. Same real ld64-609-crash-under-
# fixup_chains workaround, Csu start.s/crt.c entry point, and -B<newer
# ld> as every other dynamic executable in this directory (see
# build_bash.sh's own header comment for the full trail).
if [ "$fail" -eq 0 ]; then
    # signal.o: real Apple Libc/gen/FreeBSD/signal.c, already compiled by
    # build_launchd_syscalls.sh into liblaunchd_syscalls.a -- extracted
    # rather than recompiled (same precedent build_ipconfig.sh uses).
    # Extracted into its own subdirectory for the same reason the generated
    # syscall stubs above get one: the `ar rcs ... "$VOUT"/*.o` step must
    # never pick it up, or a second run of this script would both archive it
    # and list it explicitly on the link line.
    mkdir -p "$VOUT/extern"
    if [ ! -f "$VOUT/extern/signal.o" ]; then
        ( cd "$VOUT/extern" && ar x "../../../$OUT/liblaunchd_syscalls.a" signal.o )
    fi
    rm -f "$VOUT/signal.o"
    for o in "$OUT/simple_obj/sys____sigaltstack.o" "$OUT/ed_setjmp_raw.o" "$OUT/chk_fail.o" \
             "$OUT/simple_obj/sys__dup.o" "$OUT/simple_obj/sys__dup2.o" \
             "$OUT/simple_obj/sys__fsync.o" "$OUT/simple_obj/sys__umask.o" \
             "$OUT/simple_obj/sys__utimes.o"; do
        if [ ! -f "$o" ]; then
            echo "error: $o not found -- run build_simple_cmds.sh (sys____sigaltstack.o/ed_setjmp_raw.o) and build_libmalloc.sh (chk_fail.o) first" >&2
            exit 1
        fi
    done

    LINK_DYLIBS=("$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_pthread.dylib" \
        "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_platform.dylib" \
        "$OUT/system/libsystem_kernel.dylib" "$OUT/system/libdyld.dylib" \
        "$OUT/system/libncurses.dylib" "$OUT/system/libcopyfile.dylib" \
        `# libiconv.dylib, NOT the static libiconv.a: iconv.o references` \
        `# ___locale_key with direct adrp/lo12 addressing, which needs a` \
        `# real LOCAL definition -- linking the archive fails with` \
        `# "fixup error (kind=arm64_adrp_lo12) ... target ___locale_key` \
        `# does not have address". build_libiconv_dylib.sh already solved` \
        `# exactly this (see its own header) by giving the dylib real local` \
        `# __global_locale/__locale_key definitions, so vim gets it for` \
        `# free -- and it is the "prefer real dylibs" direction anyway.` \
        "$OUT/system/libiconv.dylib" \
        "$OUT/system/libinfo.dylib" "$OUT/system/libSystem.B.dylib")
    # Real prerequisite check (2026-09-21), same shape as build_sshd.sh's
    # own -- without it, a from-scratch build hits a bare clang "no such
    # file" pile instead of one clear message, and gen_build_order.py
    # never learns these are real dependencies.
    for d in "${LINK_DYLIBS[@]}"; do
        if [ ! -f "$d" ]; then
            echo "error: $d not found -- run its build_*.sh first" >&2
            exit 1
        fi
    done

    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
        -DCRT_DYNAMIC_LINKING \
        -B"$NEWLD_BINDIR" \
        -Wl,-fixup_chains \
        -o vim.macho \
        ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
        "$OUT/vim_objs.a" "$VOUT/extern/signal.o" \
        "$OUT/simple_obj/sys____sigaltstack.o" "$OUT/ed_setjmp_raw.o" "$OUT/chk_fail.o" \
        `# Already-generated raw syscall stubs from the build_simple_cmds.sh` \
        `# tier (real Apple create-syscalls.pl asm) -- dup/dup2 (os_unix.c's` \
        `# mch_call_shell_fork, if_cscope.c, fileio.c, main.c's read_stdin),` \
        `# fsync (fileio.c's vim_fsync), umask (bufwrite.c/fileio.c/viminfo.c),` \
        `# utimes (the real utime.c compiled above calls it).` \
        "$OUT/simple_obj/sys__dup.o" "$OUT/simple_obj/sys__dup2.o" \
        "$OUT/simple_obj/sys__fsync.o" "$OUT/simple_obj/sys__umask.o" \
        "$OUT/simple_obj/sys__utimes.o" \
        `# ...and vim's own four, generated above.` \
        "$VOUT/syscalls/sys__fchdir.o" "$VOUT/syscalls/sys__sync.o" \
        "$VOUT/syscalls/sys__getsockopt.o" "$VOUT/syscalls/sys__setsid.o" \
        "${LINK_DYLIBS[@]}" \
        `# real libsystem_c.dylib exports _strftime$UNIX2003 but never the` \
        `# bare _strftime (confirmed nm -gU) -- alias the versioned export` \
        `# to the plain name vim's real time.c calls. Same direction (and` \
        `# same real reason) as build_cron.sh's own _strftime alias.` \
        `# No _strptime counterpart: no dylib exports it at all, so this` \
        `# build compiles Apple's real strptime.c itself (see above).` \
        -Wl,-alias,'_strftime$UNIX2003',_strftime \
        2>"$OUT/vim_link.err" && echo "wrote vim.macho" || { echo "LINK FAILED, see $OUT/vim_link.err:"; tail -100 "$OUT/vim_link.err"; exit 1; }

    # Real dynamic-executable assertions (DAR-314) -- same checks every
    # other converted binary in this directory makes.
    bad=0
    otool -l vim.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
        echo "error: vim: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
    otool -l vim.macho | grep -q "cmd LC_LOAD_DYLIB" || {
        echo "error: vim: no LC_LOAD_DYLIB" >&2; bad=1; }
    otool -l vim.macho | grep -q "cmd LC_MAIN" || {
        echo "error: vim: no LC_MAIN" >&2; bad=1; }
    [ "$bad" -eq 0 ] && echo "vim.macho: real dynamic-executable assertions OK"
fi
