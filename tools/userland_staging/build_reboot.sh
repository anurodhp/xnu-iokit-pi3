#!/bin/bash
# Build reboot(8)/halt(8) from Apple's real, unmodified
# third_party/system_cmds/reboot.tproj/reboot.c (system_cmds-880.60.2)
# -> tools/userland_staging/reboot.macho, installed as /sbin/reboot and
# /sbin/halt (DAR-334). One binary: reboot.c picks halt from argv[0].
#
# What it does, as on a Mac: log the request (syslog + a SHUTDOWN_TIME
# utmpx record), sync(), then hand the reboot to launchd and exit
# (reboot.c:262-264). launchd stops every job with SIGTERM and then calls
# reboot(2) itself (PureDarwin launchd core.c:1386-1388). -q skips launchd
# and calls reboot(2) directly (reboot.c:164-167).
#
# Pieces that are not a straight link against the shipped dylibs:
#   - reboot3(): macOS 11 launchd's client call, not open source. Shimmed
#     over PureDarwin libxpc's real reboot2(), same RB_* flags -- see
#     reboot3_compat.c for the citations. reboot3_compat.h declares it.
#   - kextd's reboot lock (reserve_reboot, kextmanager.h) compiles out on
#     this iPhoneOS-SDK target (reboot.c:64-66, :79-80, :157-162).
#   - getlogin(), getopt(), signal(), err(): Libc sources compiled here,
#     the way build_login.sh does; libsystem_c.dylib does not export them
#     in this project (it exports only the __err/__warn internals).
#     getlogin() calls the raw ___getlogin that libsystem_kernel.dylib
#     exports (build_libsystem_kernel.sh, DAR-334).
#   - openlog()/syslog() (Libc gen/oldsyslog.c) and pututxline() (Libc
#     gen/NetBSD/utmpx.c + gen/utmpx-darwin.c + this project's ASL compat
#     files): the same set build_login.sh compiles, for the same reason --
#     no shipped dylib owns them (only libopenpam/libcopyfile carry copies).
#   - reboot(2) and sync(2) bind to libsystem_kernel.dylib, reboot2() to
#     libxpc.dylib.
#
# Toolchain: Xcode 12's iPhoneOS SDK, as every build_*.sh here.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
REBOOT="../../third_party/system_cmds/reboot.tproj"
LAUNCHD_INC="../../third_party/PureDarwin/src/Libraries/XPC/launchd"
XPC_INC="../../third_party/PureDarwin/src/Libraries/XPC/libxpc/include"
ASL_INC="../../third_party/syslog/libsystem_asl.tproj/include"
OUT="libc_build"
OBJ="$OUT/reboot_obj"
rm -rf "$OBJ"
mkdir -p "$OBJ"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi
for d in libsystem_c libsystem_kernel libsystem_pthread libsystem_malloc libsystem_platform \
         libdyld libSystem.B libxpc libinfo libsystem_notify libbsm; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done
for f in libsystem_kernel/sys_writev.o libsystem_kernel/sys___sendto_nocancel.o \
         compat_include/stdio_flockfile_compat.h ed_setjmp_raw.o; do
    if [ ! -f "$OUT/$f" ]; then
        echo "error: $OUT/$f not found -- run build_libsystem_kernel.sh / build_login.sh / build_simple_cmds.sh first" >&2
        exit 1
    fi
done

# Same flags as build_login.sh (see its CFLAGS comment), plus the launchd
# and libxpc headers reboot.c includes (reboot2.h, bootstrap_priv.h) and
# the reboot3() declaration.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h -include $OUT/compat_include/stdio_flockfile_compat.h \
    -include reboot3_compat.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -D__APPLE__=1 \
    -I $LAUNCHD_INC -I $XPC_INC -I $ASL_INC \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/include/NetBSD -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/nbsdcompat \
    -I $LIBC/locale -I $LIBC/locale/FreeBSD -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall -I $XNU/libsyscall/wrappers \
    -I ../libinfo_custom \
    -I ../../generated"

FILES="
$REBOOT/reboot.c
reboot3_compat.c
$LIBC/gen/FreeBSD/getlogin.c
$LIBC/stdlib/FreeBSD/getopt.c
$LIBC/gen/FreeBSD/signal.c
$LIBC/gen/FreeBSD/err.c
$LIBC/gen/oldsyslog.c
$LIBC/gen/NetBSD/utmpx.c
$LIBC/gen/utmpx-darwin.c
asl_client_compat.c
login_asl_extra_compat.c
"
ok=0
fail=0
for f in $FILES; do
    base=$(basename "$f" .c)
    extra=""
    # signal.c calls Apple's internal _sigaction spelling; libsystem_c
    # exports the public sigaction (same un-rename as build_sshd.sh's
    # popen.c/system.c).
    [ "$base" = signal ] && extra="-D_sigaction=sigaction"
    if "$CLANG" $CFLAGS $extra "$f" -o "$OBJ/$base.o" 2>"$OBJ/$base.err"; then
        ok=$((ok + 1)); rm -f "$OBJ/$base.err"
    else
        fail=$((fail + 1)); echo "  fail: $base" >&2; cat "$OBJ/$base.err" >&2
    fi
done
# send(): Libc's own wrapper over __sendto_nocancel, compiled WITHOUT
# VARIANT_CANCELABLE -- same reason and same command as build_login.sh.
if "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$LIBC/fbsdcompat" -I "$LIBC/include" -I "$LIBC/darwin" \
    "$LIBC/net/FreeBSD/send.c" -o "$OBJ/send_real.o" 2>"$OBJ/send_real.err"; then
    ok=$((ok + 1)); rm -f "$OBJ/send_real.err"
else
    fail=$((fail + 1)); echo "  fail: send_real" >&2; cat "$OBJ/send_real.err" >&2
fi
# _NSGetArgv(): oldsyslog.c's vsyslog() uses argv[0] as the default log
# tag. Real Apple ships it from Libc sys/crt_externs.c in libsystem_c; this
# project's libsystem_c does not export it yet, and libdyld exports only the
# underlying _NXArgv. date_compat.c already carries the project's copy
# (return &NXArgv, as crt_externs.c:52-54 does); narrowed to that one
# symbol, as build_libsystem_c.sh narrows _NSGetEnviron. ld -r keeps the
# file's other functions as hidden locals (a stub _pututxline among them),
# so both links below use -dead_strip to drop them: no second, private
# copy of a real libc function ends up in the binary.
if "$CLANG" $CFLAGS date_compat.c -o "$OBJ/nsgetargv_raw.o.tmp" 2>"$OBJ/nsgetargv.err" && \
   "$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld" -r -arch arm64 \
       -exported_symbol __NSGetArgv "$OBJ/nsgetargv_raw.o.tmp" -o "$OBJ/nsgetargv.o" 2>>"$OBJ/nsgetargv.err"; then
    ok=$((ok + 1)); rm -f "$OBJ/nsgetargv.err" "$OBJ/nsgetargv_raw.o.tmp"
else
    fail=$((fail + 1)); echo "  fail: nsgetargv" >&2; cat "$OBJ/nsgetargv.err" >&2
fi
echo "reboot (system_cmds reboot.tproj): $ok compiled, $fail failed"
[ "$fail" -eq 0 ] || exit 1

DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
S="$OUT/system"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains -Wl,-dead_strip \
    -o reboot.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o \
    "$OUT/libsystem_kernel/sys_writev.o" "$OUT/libsystem_kernel/sys___sendto_nocancel.o" \
    "$S/libsystem_kernel.dylib" "$S/libsystem_c.dylib" "$S/libsystem_pthread.dylib" \
    "$S/libsystem_malloc.dylib" "$S/libsystem_platform.dylib" "$S/libdyld.dylib" \
    "$S/libxpc.dylib" "$S/libinfo.dylib" "$S/libsystem_notify.dylib" "$S/libSystem.B.dylib" \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    -Wl,-alias,_writev,'_writev$UNIX2003' \
    -Wl,-alias,_send,'_send$UNIX2003'
echo "wrote reboot.macho"

bad=0
otool -l reboot.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: reboot: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l reboot.macho | grep -q "cmd LC_MAIN" || {
    echo "error: reboot: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
# A clean link does not prove every bind resolves at load (DAR-217).
./bind_target_audit.sh reboot.macho

# ---------------------------------------------------------------------
# shutdown(8) (DAR-334): Apple's real, unmodified
# system_cmds/shutdown.tproj/shutdown.c -> shutdown.macho, /sbin/shutdown.
# -r and -h end in the same reboot3() -> launchd call as reboot
# (shutdown.c:498); every object above except reboot.c itself is shared.
#
#   - kextmanager.h / kextmanager_lock_reboot(): on this iPhoneOS-SDK
#     target Apple's own kextmanager.defs is an empty "subsystem dummy"
#     (shutdown.tproj/kextmanager.defs:2-3; Apple ships no shutdown on
#     iOS), so the client is generated from the real routines it includes
#     on macOS, IOKitUser kext.subproj/kextmanager_mig.defs, with Xcode 12's
#     mig. NO_SECURITY_FRAMEWORK is that file's own non-macOS switch
#     (:9-14); MAXPATHLEN is xnu's (sys/param.h:206 -> syslimits.h:107,
#     1024), which mig's preprocessor cannot get from the C-only import.
#     reserve_reboot() then really asks for kextd; there is none, so
#     bootstrap_look_up2() fails and it allows the reboot, as on a Mac
#     without kextd (shutdown.c:749-755 and its finish: path).
#   - IOPMLib (-s, sleep only): see shutdown_nosleep_compat.c.
#   - audit_shutdown(): real libbsm.dylib (auditon/au_*).
SOBJ="$OBJ/shutdown"
mkdir -p "$SOBJ/inc/IOKit/kext"
IOKITUSER_ABS="$(cd ../../third_party/IOKitUser && pwd)"
ln -sf "$IOKITUSER_ABS/kext.subproj/kextmanager_types.h" "$SOBJ/inc/IOKit/kext/"
MIG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/mig"
"$MIG" -arch arm64 -isysroot "$SDK" -I"$SOBJ/inc" -I"$XNU/osfmk" \
    -DNO_SECURITY_FRAMEWORK -D__DARWIN_64_BIT_INO_T=1 -DMAXPATHLEN=1024 \
    -user "$SOBJ/kextmanagerUser.c" -header "$SOBJ/kextmanager.h" \
    -server /dev/null -sheader /dev/null \
    "$IOKITUSER_ABS/kext.subproj/kextmanager_mig.defs"
for f in libc_build/iokit_headers/IOKit/pwr_mgt/IOPMLib.h libc_build/cf_headers/CoreFoundation/CFBase.h; do
    if [ ! -f "$f" ]; then
        echo "error: $f not found -- run build_configd.sh first (it stages these headers)" >&2
        exit 1
    fi
done
SCFLAGS="$CFLAGS -I $SOBJ -I $SOBJ/inc -I $OUT/iokit_headers -I $OUT/cf_headers \
    -I $XNU/iokit -I $XNU/libkern -I ../../third_party/system_cmds/shutdown.tproj"
sok=0
sfail=0
# gethostname(): Libc's own (libresolv.dylib carries a copy but is not its
# owner). popen()/pclose() for the wall(1) notice (shutdown.c:365-372; only
# reached when /usr/bin/wall exists) and setjmp()/longjmp(): the project's
# ed_popen_system_compat.c and ed_setjmp_compat.c + ed_setjmp_raw.o, as
# ed/awk/less link them -- see those files' headers.
for f in ../../third_party/system_cmds/shutdown.tproj/shutdown.c "$SOBJ/kextmanagerUser.c" \
         shutdown_nosleep_compat.c "$LIBC/gen/FreeBSD/gethostname.c" \
         ed_popen_system_compat.c ed_setjmp_compat.c; do
    base=$(basename "$f" .c)
    extra=""
    # With this project's -I osfmk, <mach/mach.h> is xnu's copy, which does
    # not pull in mach_error_string() (shutdown.c:787); the SDK's
    # <mach/mach_error.h> declares it.
    [ "$base" = shutdown ] && extra="-include $SDK/usr/include/mach/mach_error.h"
    if "$CLANG" $SCFLAGS $extra "$f" -o "$SOBJ/$base.o" 2>"$SOBJ/$base.err"; then
        sok=$((sok + 1)); rm -f "$SOBJ/$base.err"
    else
        sfail=$((sfail + 1)); echo "  fail: $base" >&2; cat "$SOBJ/$base.err" >&2
    fi
done
echo "shutdown (system_cmds shutdown.tproj): $sok compiled, $sfail failed"
[ "$sfail" -eq 0 ] || exit 1
SHARED_OBJS=$(ls "$OBJ"/*.o | grep -v '/reboot\.o$')
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains -Wl,-dead_strip \
    -o shutdown.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    $SHARED_OBJS "$SOBJ"/*.o "$OUT/ed_setjmp_raw.o" \
    "$OUT/libsystem_kernel/sys_writev.o" "$OUT/libsystem_kernel/sys___sendto_nocancel.o" \
    "$S/libsystem_kernel.dylib" "$S/libsystem_c.dylib" "$S/libsystem_pthread.dylib" \
    "$S/libsystem_malloc.dylib" "$S/libsystem_platform.dylib" "$S/libdyld.dylib" \
    "$S/libxpc.dylib" "$S/libinfo.dylib" "$S/libsystem_notify.dylib" "$S/libbsm.dylib" \
    "$S/libSystem.B.dylib" \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    -Wl,-alias,_writev,'_writev$UNIX2003' \
    -Wl,-alias,_send,'_send$UNIX2003'
echo "wrote shutdown.macho"
otool -l shutdown.macho | grep -q "cmd LC_MAIN" || { echo "error: shutdown: no LC_MAIN" >&2; exit 1; }
./bind_target_audit.sh shutdown.macho
