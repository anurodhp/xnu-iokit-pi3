#!/bin/bash
# Builds su.macho: real, unmodified Apple shell_cmds/su/su.c (DAR-339,
# DAR-329's "user management commands" parent), built against this
# project's now-real OpenPAM static-module support (build_openpam.sh,
# see that script's own header) plus a real pam_unix-equivalent static
# module (su_pam_unix_static.c). Apple's real source used per this
# project's source-priority rule (Apple first, FreeBSD fallback) --
# checked, compiles clean under its own real `#ifdef __APPLE__` branch
# with no login_cap.h dependency (unlike the non-Apple branch).
#
# USE_BSM_AUDIT is deliberately left undefined -- same real,
# already-established precedent as build_login.sh's own identical
# choice (real BSM audit support out of scope, not attempted). Every
# `#ifdef USE_BSM_AUDIT` block in su.c compiles out entirely.
#
# Real PAM policy: openpam_configure.c's own real "default all empty
# facilities to required pam_deny.so" fallback (checked directly) means
# su needs an actual /etc/pam.d/su naming pam_unix.so, or every PAM
# facility fails closed -- written onto the image by
# tools/init_binary/inject_into_sd_image.sh, not by this build script
# (matches sshd_config's own provisioning-time-not-build-time
# precedent).
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
OPENPAM="../../third_party/OpenPAM/openpam"
SU_SRC="../../third_party/shell_cmds/su"
OUT="libc_build"
rm -rf "$OUT/su_obj"
mkdir -p "$OUT/su_obj"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$SU_SRC/su.c" ]; then
    echo "error: $SU_SRC/su.c not found -- confirm third_party/shell_cmds is cloned" >&2
    exit 1
fi

for dep in libplatform.a libmalloc.a libpthread.a libclosure.a libc_gen.a libc_stdio.a libc_printf.a \
           libc_locale_for_stdio.a liblaunchd_syscalls.a libmach_ipc.a libc_regex.a libopenpam.a; do
    if [ ! -f "$OUT/$dep" ]; then
        echo "error: $OUT/$dep not found -- run its build_*.sh first" >&2
        exit 1
    fi
done

# -D__FreeBSD__ + -include su_compat_include/pic_override.h: the SAME
# real fix build_openpam.sh applies to compile openpam_static.c for
# real (see that script's own header) -- su_pam_unix_static.c's own
# `#include <security/openpam.h>` needs the identical preprocessor
# environment for its own PAM_MODULE_ENTRY("pam_unix") to expand to the
# real registration macro instead of openpam.h's real no-op fallback
# branch. Found the hard way: a first build linked clean but the
# resulting su.macho had NO __pam_sm section at all -- PAM_MODULE_ENTRY
# had silently expanded to nothing because this file was missing both
# flags, same real "silent no-op, not an error" class of gap CLAUDE.md's
# own launchd_pid1 postmortem already warns about for a stale-vs-
# recompiled archive.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h -include $OUT/compat_include/stdio_flockfile_compat.h -include su_compat_include/pic_override.h \
    -D__FreeBSD__ \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I su_compat_include \
    -I $OPENPAM/include -I $OPENPAM/lib \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall -I $XNU/libsyscall/wrappers \
    -I ../libinfo_custom \
    -I ../../generated"

ok=0
skip=0
if "$CLANG" $CFLAGS -Dmain=su_cmd_real_main "$SU_SRC/su.c" -o "$OUT/su_obj/su.o" 2>"$OUT/su_obj/su.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/su_obj/su.err"
else
    skip=$((skip + 1))
    echo "  skip: su"
    cat "$OUT/su_obj/su.err" >&2
fi
for f in su_main_wrapper.c su_pam_unix_static.c ps_passwd_compat.c ../libinfo_custom/libinfo_custom.c \
         chpass_fork_compat.c "$LIBC/gen/getusershell.c" "$LIBC/stdlib/FreeBSD/getopt.c" \
         "$LIBC/gen/crypt.c" "$LIBC/gen/FreeBSD/err.c" "$LIBC/gen/sigsetops.c"; do
    base=$(basename "$f" .c)
    out="$OUT/su_obj/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/su_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/su_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/su_obj/${base}.err" >&2
    fi
done

echo "su (real Apple shell_cmds/su/su.c): $ok compiled, $skip skipped"
if [ "$skip" -ne 0 ]; then
    echo "error: su has real compile failures, not building su.macho" >&2
    exit 1
fi

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/su_obj/su_sched_yield_trap.o"
"$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OUT/su_obj/su_sched_yield.o"
"$CLANG" $CFLAGS libc_locale_globals_compat.c -o "$OUT/su_obj/libc_locale_globals_compat.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/table.c" -o "$OUT/su_obj/locale_table.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/none.c" -o "$OUT/su_obj/none.o"

mkdir -p "$OUT/su_glue_extract"
(cd "$OUT/su_glue_extract" && ar x "../libc_printf.a" glue.o)

# _chdir/_getpgrp: real, only exported by libncurses.dylib in this
# project's current build (confirmed via nm -gU across every
# $OUT/system/*.dylib) -- not worth linking ncurses for two syscalls.
# _getpgid/_getpriority: not exported by any dylib built here at all.
# All four are real, plain syscalls.master entries (no NO_SYSCALL_STUB),
# generated the same real way build_libsystem_c.sh's own header
# documents.
TMPGEN="$(mktemp -d)"
trap 'rm -rf "$TMPGEN"' EXIT
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" \
    "$XNU/libsyscall/custom" \
    "$XNU/libsyscall/Platforms" \
    iPhoneOS \
    "$TMPGEN" >/dev/null
MACSDK="$(xcode-select -p)/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
SYSCFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -I $XNU/libsyscall/custom -I $XNU/libsyscall/mach -idirafter $XNU/osfmk -idirafter $MACSDK/usr/include"
for s in _chdir _getpgrp _getpgid _getpriority; do
    "$CLANG" $SYSCFLAGS "$TMPGEN/${s}.s" -o "$OUT/su_obj/sys${s}.o"
done

DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi
LINK_DYLIBS="$OUT/system/libsystem_c.dylib $OUT/system/libsystem_kernel.dylib $OUT/system/libsystem_pthread.dylib $OUT/system/libsystem_malloc.dylib $OUT/system/libsystem_platform.dylib $OUT/system/libdyld.dylib $OUT/system/libcopyfile.dylib $OUT/system/libremovefile.dylib $OUT/system/libbsm.dylib $OUT/system/libSystem.B.dylib"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o su.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/su_obj/*.o \
    "$OUT/libopenpam.a" \
    "$OUT/liblaunchd_syscalls.a" \
    "$OUT/libmach_ipc.a" \
    "$OUT/libc_regex.a" \
    $LINK_DYLIBS \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    -Wl,-alias,_waitpid,'_waitpid$UNIX2003' \
    -Wl,-alias,_setrlimit,'_setrlimit$UNIX2003' \
    -Wl,-alias,_fchmod,'_fchmod$UNIX2003' \
    -Wl,-alias,_stat,'_stat$INODE64' \
    -Wl,-alias,_sigaction,'_sigaction$UNIX2003' \
    -Wl,-alias,_sigprocmask,'_sigprocmask$UNIX2003'
echo "wrote su.macho"

bad=0
otool -l su.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: su: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l su.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: su: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l su.macho | grep -q "cmd LC_MAIN" || {
    echo "error: su: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1

if [ -x ./bind_target_audit.sh ]; then
    ./bind_target_audit.sh su.macho
fi
if [ -x ./dead_stub_audit.sh ]; then
    ./dead_stub_audit.sh su.macho
fi
