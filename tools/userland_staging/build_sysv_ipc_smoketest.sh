#!/bin/bash
# Builds libc_build/sysv_ipc_smoketest.macho (DAR-407): the QEMU check that
# the kernel's SysV shm/sem/msg (config/MASTER.arm64.bcm2837's BSD_BASE,
# sysv_sem/sysv_msg/sysv_shm) and libsystem_kernel.dylib's wrappers for
# them (build_libsystem_kernel.sh's DAR-407 stub-loop entries plus
# libsyscall/wrappers/varargs_wrappers.s) work end to end -- see
# sysv_ipc_smoketest.c's header. Installed at /bin/sysv_ipc_smoketest by
# tools/init_binary/inject_into_sd_image.sh (QEMU images only).
#
# Link recipe: build_pthread_smoketest.sh's (Csu start.s/crt.c with
# -DCRT_DYNAMIC_LINKING, daily-driver ld64 for -fixup_chains, the same
# LC_LOAD_DYLINKER/LC_MAIN assertions and bind_target_audit.sh gate).
# libsystem_kernel.dylib is listed first because it is the real owner of
# every SysV call and ___error; fork/waitpid/printf/exit are libsystem_c's
# (the child uses exit(), not _exit(): here only libsystem_malloc.dylib
# exports __exit, and binding the smoketest to that would be misleading).
set -euo pipefail
cd "$(dirname "$0")"
OUT="libc_build"
CSU="../../third_party/Csu"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi
if [ ! -d "$CSU" ]; then
    echo "error: $CSU not found -- run ./setup_third_party.sh first" >&2
    exit 1
fi
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi

for f in system/libsystem_kernel.dylib system/libsystem_c.dylib \
         system/libsystem_pthread.dylib system/libsystem_malloc.dylib \
         system/libsystem_platform.dylib system/libdyld.dylib system/libSystem.B.dylib; do
    if [ ! -f "$OUT/$f" ]; then
        echo "error: $OUT/$f not found -- run the libSystem tier build scripts first" >&2
        exit 1
    fi
done
KERNEL_EXPORTS="$(nm -gU "$OUT/system/libsystem_kernel.dylib")"
for s in _shmget _shmat _shmdt _shmctl _semget _semop _semctl _msgget _msgsnd _msgrcv _msgctl \
         _semsys _msgsys _shmsys; do
    grep -q " $s\$" <<<"$KERNEL_EXPORTS" || {
        echo "error: libsystem_kernel.dylib does not export $s -- rerun build_libsystem_kernel.sh" >&2
        exit 1
    }
done

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin \
    -fno-stack-protector -O0 -D_FORTIFY_SOURCE=0 \
    sysv_ipc_smoketest.c -o "$OUT/sysv_ipc_smoketest.o"

M="$OUT/sysv_ipc_smoketest.macho"
rm -f "$M"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$M" \
    "$CSU/start.s" "$CSU/crt.c" \
    "$OUT/sysv_ipc_smoketest.o" \
    "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib"

otool -l "$M" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: no LC_LOAD_DYLINKER /usr/lib/dyld -- not a real dynamic executable" >&2
    exit 1
}
otool -l "$M" | grep -q "cmd LC_MAIN" || {
    echo "error: no LC_MAIN -- see build_simple_cmds.sh's LINK_DYLIBS comment" >&2
    exit 1
}
./bind_target_audit.sh "$M" || {
    echo "error: $M has an unresolvable two-level bind -- it would abort at dyld load" >&2
    exit 1
}
echo "wrote $M ($(wc -c < "$M") bytes, real dynamic executable)"
