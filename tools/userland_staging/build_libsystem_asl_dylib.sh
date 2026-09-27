#!/bin/bash
# Builds libc_build/system/libsystem_asl.dylib: real Apple syslog-385
# libsystem_asl.tproj (apple-oss-distributions/syslog, already pinned in
# setup_third_party.sh and cloned at third_party/syslog) -- DAR-121.
#
# Before this, every ASL client in the project resolved asl_open()/
# asl_new()/asl_set()/asl_send()/asl_free()/asl_close() to
# asl_client_compat.c, a no-op sentinel shim compiled into libinfo.dylib
# (CoreFoundation's __CFLogCStringLegacy() bound them as
# `bind libinfo/_asl_*`). That shim's own header gave the reason: real
# asl_open() looks up com.apple.system.logger and there was no bootstrap
# registrar. There is one now -- launchd is PID 1 and syslogd checks that
# service in (see libsystem_dyld_initializer_compat.c). The remaining
# half was syslogd never draining the port: its dbserver.c MIG server was
# a no-op stand-in. build_syslogd.sh now compiles the real dbserver.c, so
# this real client has a real server to talk to.
#
# That ordering matters. _asl_server_message is a MIG `simpleroutine`
# (third_party/syslog/aslcommon/asl_ipc.defs:51) with no timeout, so a
# client sending to a checked-in port nobody receives on blocks forever
# once the port's queue fills. Ship this dylib only alongside a
# build_syslogd.sh that links the real dbserver.c.
#
# Contents: all 14 real files in libsystem_asl.tproj/src/ (Apple's own
# target membership, syslog.xcodeproj: including syslog.c, so syslog(3)/
# openlog(3)/vsyslog(3) are exported here as on real Darwin), plus the
# mig-generated asl_ipc CLIENT stubs from aslcommon/asl_ipc.defs. Two
# local-only stand-ins in libsystem_asl_compat.c (os_log_shim_enabled,
# mbr_check_membership -- see that file's header for the source
# citations) and os_log_with_args_compat.c (unreachable once
# os_log_shim_enabled() is false; see that file's header). All three are
# -unexported_symbol below so this dylib never becomes their canonical
# owner.
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
# Same linker split as build_libinfo.sh: Xcode 12's ld64-609 crashes under
# -fixup_chains, so only the final link uses the daily-driver Xcode's ld.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi
MIG="$(which mig)"
if [ -z "$MIG" ] || [ ! -x "$MIG" ]; then
    echo "error: mig tool not found" >&2
    exit 1
fi

XNU="../../third_party/xnu-7195"
SYSLOG="../../third_party/syslog"
DISPATCH="../../third_party/libdispatch"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
LIBC="../../third_party/Libc"
NOTIFY="../../third_party/Libnotify"
PD="../../third_party/PureDarwin/src/Libraries/XPC"
AVAIL_VERSIONS="../../build/xnu-latest-build/AvailabilityVersions/dst/usr/local/include"
LAUNCHD_COMPAT="launchd_compat_headers"
DCOMPAT="dispatch_compat_headers"
SCOMPAT="syslog_compat_headers"
OUT="libc_build"
OBJ="$OUT/libsystem_asl_obj"
MIGOUT="$OBJ/mig"
rm -rf "$OBJ"
mkdir -p "$OBJ" "$MIGOUT" "$OUT/system"

for d in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_pthread libsystem_blocks; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/system/libsystem_notify.dylib" ]; then
    echo "error: $OUT/system/libsystem_notify.dylib not found -- run build_libnotify.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libdispatch.dylib" ]; then
    echo "error: $OUT/system/libdispatch.dylib not found -- run build_libdispatch_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libxpc.dylib" ]; then
    echo "error: $OUT/system/libxpc.dylib not found -- run build_libxpc_dylib.sh first" >&2
    exit 1
fi
# mbr_uid_to_uuid()/mbr_gid_to_uuid() (asl_core.c/asl_file.c): exported
# by libcopyfile.dylib today (copyfile_syscalls.c), the same owner
# syslogd.macho already binds them to.
if [ ! -f "$OUT/simple_obj/sys__dup2.o" ]; then
    echo "error: $OUT/simple_obj/sys__dup2.o not found -- run build_simple_cmds.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libremovefile.dylib" ]; then
    echo "error: $OUT/system/libremovefile.dylib not found -- run build_removefile_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libdyld.dylib" ]; then
    echo "error: $OUT/system/libdyld.dylib not found -- run build_libdyld.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libcopyfile.dylib" ]; then
    echo "error: $OUT/system/libcopyfile.dylib not found -- run build_copyfile_dylib.sh first" >&2
    exit 1
fi

# Same CFLAGS as build_syslogd.sh (which already compiles 9 of these 14
# files for its own static copy), plus -I os_log_compat_headers so
# <os/log_private.h> resolves to this project's userspace stand-in rather
# than XNU's kernel-internal libkern/os/log_private.h (which needs a
# COMM_PAGE header that does not exist in userspace).
CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=ios,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos\(x\)=ios,introduced=x \
    -DSPI_AVAILABLE\(...\)= -D__SPI_AVAILABLE\(...\)= \
    -include "$SCOMPAT/sys/kevent_qos_compat.h" \
    -I "$MIGOUT" -I os_log_compat_headers \
    -I "$SCOMPAT" -I "$LAUNCHD_COMPAT" -I "$DCOMPAT" \
    -I "$SYSLOG/aslcommon" -I "$SYSLOG/libsystem_asl.tproj/include" \
    -I "$NOTIFY" -I "$PD/launchd/System-compat" -I "$PD/libxpc/include" \
    -I "$DISPATCH" -I "$DISPATCH/private" \
    -idirafter "$AVAIL_VERSIONS" \
    -idirafter "$XNU/libsyscall/wrappers/libproc" -idirafter "$XNU/libsyscall/wrappers/spawn" \
    -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libsyscall" -idirafter "$XNU/libsyscall/mach" \
    -idirafter "$PTHREAD/include" -idirafter "$PTHREAD/private" \
    -idirafter "$PLATFORM/private" -idirafter "$PLATFORM/include" -idirafter "$PLATFORM/internal" \
    -idirafter "$XNU/BUILD.hdrs/dst/usr/local/include" -idirafter "$XNU/EXTERNAL_HEADERS" \
    -idirafter "$LIBC/darwin" -idirafter "$LIBC" -idirafter "$XNU/libkern")

echo "mig: regenerating asl_ipc client stubs"
"$MIG" -arch arm64 -I"$SYSLOG/aslcommon" \
    -header "$MIGOUT/asl_ipc.h" -user "$MIGOUT/asl_ipcUser.c" -server /dev/null -sheader /dev/null \
    "$SYSLOG/aslcommon/asl_ipc.defs"

ok=0
fail=0
compile_one() {
    local src="$1" base
    shift
    base=$(basename "$src" .c)
    if "$CLANG" "${CFLAGS[@]}" "$@" "$src" -o "$OBJ/$base.o" 2>"$OBJ/$base.err"; then
        ok=$((ok + 1))
        rm -f "$OBJ/$base.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OBJ/$base.err")"
    fi
}

echo "compiling real syslog-385 libsystem_asl.tproj..."
for f in asl_client asl_core asl_fd asl_file asl_legacy1 asl_msg asl_msg_list \
         asl_mt_shim asl_object asl_store asl_string asl_util; do
    compile_one "$SYSLOG/libsystem_asl.tproj/src/$f.c"
done
# asl.c and syslog.c use NL_TEXTMAX (SDK <limits.h>:156). -ffreestanding
# makes clang's own <limits.h> skip its #include_next of the SDK header,
# so include the real SDK header directly for these two.
for f in asl syslog; do
    compile_one "$SYSLOG/libsystem_asl.tproj/src/$f.c" -include "$SDK/usr/include/limits.h"
done
compile_one "$MIGOUT/asl_ipcUser.c"
compile_one libsystem_asl_compat.c
compile_one os_log_with_args_compat.c

echo "libsystem_asl: $ok compiled, $fail gaps (see $OBJ/*.err)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f "$OUT/system/libsystem_asl.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libsystem_asl.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -Wl,-unexported_symbol,_os_log_shim_enabled \
    -Wl,-unexported_symbol,_mbr_check_membership \
    -Wl,-unexported_symbol,_os_log_with_args \
    -Wl,-unexported_symbol,__NSGetArgv \
    -Wl,-unexported_symbol,_atexit \
    -Wl,-unexported_symbol,_atoll \
    -Wl,-unexported_symbol,_fls \
    -Wl,-unexported_symbol,__os_activity_current \
    -Wl,-unexported_symbol,_os_activity_get_identifier \
    -Wl,-unexported_symbol,_dup2 \
    -o "$OUT/system/libsystem_asl.dylib" \
    "$OBJ/"*.o \
    `# Raw dup2(2) trap (asl_fd.c:311): no dylib exports _dup2 -- same` \
    `# simple_obj/ raw-syscall object the DAR-314 conversions keep.` \
    "$OUT/simple_obj/sys__dup2.o" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/system/libsystem_notify.dylib" "$OUT/system/libdispatch.dylib" \
    "$OUT/system/libxpc.dylib" "$OUT/system/libcopyfile.dylib" \
    `# rename(2) (asl_store.c:664): libremovefile.dylib is its only` \
    `# exporter, the owner syslogd.macho already binds it to.` \
    "$OUT/system/libremovefile.dylib" \
    `# NXArgv, for _NSGetArgv() in libsystem_asl_compat.c.` \
    "$OUT/system/libdyld.dylib" \
    2>"$OUT/libsystem_asl_dylib_link.err" \
    && echo "wrote $OUT/system/libsystem_asl.dylib" \
    || { echo "libsystem_asl.dylib link failed:" >&2; grep -E '^  "|error' "$OUT/libsystem_asl_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }

otool -hv "$OUT/system/libsystem_asl.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
otool -l "$OUT/system/libsystem_asl.dylib" | grep -q LC_DYLD_CHAINED_FIXUPS || {
    echo "error: no LC_DYLD_CHAINED_FIXUPS -- real dyld here only supports chained fixups" >&2
    exit 1
}
for s in _asl_open _asl_new _asl_set _asl_send _asl_free _asl_close _asl_log _syslog; do
    nm -gU "$OUT/system/libsystem_asl.dylib" | grep -q " $s\$" || {
        echo "error: libsystem_asl.dylib does not export $s" >&2
        exit 1
    }
done
# Hard gate: a clean link does not prove every two-level bind resolves
# at dyld load (DAR-217).
./bind_target_audit.sh "$OUT/system/libsystem_asl.dylib"

# QEMU smoketest (asl_smoketest.c's header says what it proves). Same
# dynamic-executable recipe as build_libinfo.sh's libinfo_smoketest.
# Main object lives in its own subdirectory so the dylib's "$OBJ/"*.o
# glob above can never pick it up (build_libinfo.sh's DAR-229 bug).
CSU="../../third_party/Csu"
mkdir -p "$OBJ/smoketest"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall \
    -Wno-deprecated-declarations -I "$SYSLOG/libsystem_asl.tproj/include" \
    asl_smoketest.c -o "$OBJ/smoketest/asl_smoketest.o"
rm -f asl_smoketest.macho
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o asl_smoketest.macho \
    "$CSU/start.s" "$CSU/crt.c" "$OBJ/smoketest/asl_smoketest.o" \
    "$OUT/system/libsystem_asl.dylib" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib"
./bind_target_audit.sh asl_smoketest.macho
echo "wrote asl_smoketest.macho"
