#!/bin/bash
# Builds pd_console_login.macho: real dynamic Mach-O for PureDarwin's
# src/Libraries/XPC/launchd/pd_console_login.c. This is launchd's own
# real getty/login-equivalent helper -- launchd opens StandardInPath with
# O_NOCTTY, so a bare LaunchDaemon has fds for /dev/console but no
# controlling tty; this helper does the classic getty dance (open
# non-blocking to avoid wedging on carrier, CLOCAL, TIOCSCTTY) then
# exec()s a real login shell. Kept as its own executable rather than
# folded into liblaunchd_core.a because its own real main() hard-conflicts
# with launchd.c's (see build_launchd_core.sh's own header comment) --
# launchd supervises it as a separate process (org.puredarwin.console-login
# LaunchDaemon, see inject_into_sd_image.sh), doesn't link into it.
#
# Plain standalone C (termios/ioctl/setsid/execv/setenv, no launchd-
# internal headers), so unlike build_launchd_core.sh it needs none of
# that script's compat-header/-include machinery -- just the same real
# dynamic-executable recipe as build_dyld_test_exec.sh, linked against
# the same already-built static archives (real syscalls/libc pieces
# launchd.macho itself already proved sufficient for a clean link) plus
# libdyld.dylib/libSystem.B.dylib (dyldMain.cpp's unconditional
# requirement, see build_dyld_test_exec.sh's own header comment).
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi

DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

PD="../../third_party/PureDarwin/src/Libraries/XPC"
OUT="libc_build"

for a in liblaunchd_syscalls.a libxpc_syscalls.a cf_libc_extras.a \
         libc_gen.a libc_string.a libc_stdio.a libc_printf.a libc_locale_for_stdio.a \
         libmalloc.a libplatform.a libpthread.a; do
    if [ ! -f "$OUT/$a" ]; then
        echo "error: $OUT/$a not found -- run build_launchd_core.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/cerror_stub.o" ]; then
    echo "error: $OUT/cerror_stub.o not found -- run build_launchd_core.sh first" >&2
    exit 1
fi
for d in libdyld.dylib libSystem.B.dylib; do
    if [ ! -f "$OUT/system/$d" ]; then
        echo "error: $OUT/system/$d not found -- run build_libdyld.sh/build_libsystem.sh first" >&2
        exit 1
    fi
done

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding \
    -fno-builtin -fno-stack-protector -O0 -Wall \
    "$PD/launchd/pd_console_login.c" -o "$OUT/pd_console_login.o"

rm -f "$OUT/pd_console_login.macho"
# -DCRT_DYNAMIC_LINKING (DAR-161, 2026-08-31): pd_console_login.macho is a
# real, genuinely dynamically-linked executable (libdyld.dylib/
# libSystem.B.dylib below). Without this flag, Csu/crt.c's own
# NXArgc/NXArgv/environ/__progname global storage silently shadowed the
# real, canonical copies libdyld.dylib's libdyldGlue.o exports -- any
# direct (non-getenv/setenv) read of `environ` etc in this binary would
# have seen a permanently-stale local copy instead of dyld's real one.
# This is a NEW, project-owned macro, not real Apple's `__DYNAMIC__` --
# this project's pinned Xcode-12/iPhoneOS SDK toolchain predefines
# `__DYNAMIC__=1` unconditionally for every arm64-apple-ios* compile
# regardless of -static (confirmed via `clang -dM -E -`), so reusing it
# would have silently broken every existing static Csu consumer too; see
# crt.c's own header comment on those four globals for the full
# first-attempt-caught-it-first writeup.
# launchd_libc_extras.a REMOVED from this link (2026-09-01, DAR-164
# side-fix): a real duplicate-symbol error (`_kill` defined in BOTH
# libxpc_syscalls.a's kill_wrap.o AND launchd_libc_extras.a's
# ncurses_extra_syscalls.o -- confirmed via a real trial link, not
# guessed) started failing this script; pd_console_login.c's own real
# source (tty ioctl/termios/setsid/exec, no ncurses/sigaction surface)
# never actually needed anything from launchd_libc_extras.a -- a real
# trial link with it removed entirely succeeds clean, confirming it was
# a redundant link input, not a real dependency.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/pd_console_login.macho" \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/pd_console_login.o" \
    "$OUT/liblaunchd_syscalls.a" "$OUT/libxpc_syscalls.a" \
    "$OUT/cf_libc_extras.a" \
    `# libc_gen.a/libc_string.a/libc_stdio.a/libc_printf.a/` \
    `# libc_locale_for_stdio.a/libmalloc.a/libplatform.a/libpthread.a` \
    `# REMOVED 2026-09-03 (dynamicfix sweep): all of them are merged into` \
    `# dylibs reached through the libSystem.B.dylib this link already` \
    `# loads, so the static copies were private duplicate malloc/pthread/` \
    `# stdio state. Same "one owner per symbol" class as build_launchd.sh.` \
    "$OUT/cerror_stub.o" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib" \
    2>"$OUT/pd_console_login_link.err" \
    || { echo "error: real link failed -- see $OUT/pd_console_login_link.err" >&2; tail -60 "$OUT/pd_console_login_link.err" >&2; exit 1; }

echo "wrote $OUT/pd_console_login.macho"
otool -hv "$OUT/pd_console_login.macho" | grep -q " EXECUTE " || echo "WARNING: not a real MH_EXECUTE" >&2
otool -hv "$OUT/pd_console_login.macho" | grep -q NOUNDEFS || echo "WARNING: NOUNDEFS not set -- real undefined symbols may remain" >&2
echo "linked against:"
otool -L "$OUT/pd_console_login.macho" 2>/dev/null | tail -n +2
