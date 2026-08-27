#!/bin/bash
# Builds libc_build/libpthread_qos.a: real third_party/libpthread/src/
# qos.c (pthread QoS-class/attr subsystem -- pthread_qos_class_encode/
# decode, pthread_attr_get/set_qos_class_np, qos_class_main,
# pthread_qos_max_parallelism, pthread_set_qos_class_self_np, the
# workqueue-override family) -- found 2026-08-27 bringing up
# libdispatch.dylib for real (real, iterative QEMU boot-testing hit
# `symbol not found in flat namespace '__pthread_qos_class_encode'`,
# one of ~16 real symbols in this file this project's dylib needed).
#
# ============================================================
# HONEST SCOPE STATEMENT -- read this first
# ============================================================
# build_libpthread.sh's own header comment already documents qos.c as
# "unreachable per the kernel constraint" and deliberately never builds
# it -- that comment was accurate when written (before Phase 7.5's real
# bsdthread_create/register kernel port), but a real, direct trial
# compile (this script) shows qos.c itself compiles with ZERO errors
# against this project's real toolchain, using the exact same CFLAGS
# build_libpthread.sh already established (just needed 2 more real,
# already-used-elsewhere-in-this-project additions: the bridgeos/
# SPI_AVAILABLE macro overrides every other tier in this directory
# already carries, and -I .../libsyscall/wrappers/spawn for a real,
# already-present spawn_private.h). The REAL constraint that remains is
# narrower and different: qos.c's functions that call the real
# __bsdthread_ctl(2) syscall (#478, NO_SYSCALL_STUB) -- pthread_set_qos_
# class_self_np, pthread_override_qos_class_start/end_np, the workqueue-
# override family, pthread_qos_max_parallelism -- will get a real,
# honest ENOTSUP back at runtime, confirmed directly in this project's
# own third_party/xnu-7195/bsd/kern/bsd_pthread_workq.c's own dated
# header comment (added during the real Phase 7.5 port): "bsdthread_ctl
# (QoS override machinery ...) wasn't in this task's scope" --
# bsd_pthread_stub.c's real bsdthread_ctl implementation still
# unconditionally returns ENOTSUP. This is the exact same "real code,
# real kernel-returned failure, not a fake success" pattern
# build_libpthread.sh's own header already praises for pthread_create()
# itself -- not a new kind of shortcut, the same one already accepted
# here. The PURE encode/decode functions (_pthread_qos_class_encode/
# _decode, qos_class_main, pthread_attr_get/set_qos_class_np) never
# reach that syscall at all -- real, correct bit-packing math against a
# real pthread_priority_t bitmask, fully correct regardless of kernel
# QoS-override support.
#
# Deliberately built as its OWN separate archive, NOT folded into
# libpthread.a: libpthread.a is this project's master, widely-reused
# archive (bash/ncurses/getty/syslogd and more already link it, real,
# QEMU-verified) -- adding a whole new real file to it risks a real
# regression to any of those if qos.c's own real symbol set collides
# with something already relied on elsewhere. libc_build/
# libpthread_qos.a is used ONLY by build_libdispatch_dylib.sh's own
# link, same isolation principle as that script's own existing
# dispatch_dylib_extract staging area.
#
# -DVARIANT_STATIC=1: real, found via a real duplicate-symbol link
# error. qos.c itself (src/qos.c:38-56, `#if !defined(VARIANT_STATIC)`)
# defines its own internally-redirected malloc()/free() upcalls (real
# Apple mechanism: routes through the _pthread_malloc/_pthread_free
# function-pointer vtable for the window before __pthread_init has run)
# -- real, but genuinely redundant here, since this project's own real
# malloc()/free() (libmalloc.a, already linked into every consumer of
# this archive) already exist and there's no real "before __pthread_init"
# window distinct from them to bridge. build_libpthread.sh's own master
# archive would hit this identical conflict if it ever compiled this
# file; this flag makes qos.c skip its own redirect wrappers, matching
# the same real "static build variant" choice apple's own build already
# makes for exactly this situation.
#
# Real, small compat pieces needed beyond qos.c itself (see
# libpthread_qos_compat.c's own header for the full real per-symbol
# trace): _main_qos (real global, pthread.c:134, zero-init is the real,
# honest pre-bootstrap default), _pthread_atomic_xchg_uint32_relaxed
# (real 3-line function, pthread_dependency.c:109, copied verbatim --
# NOT the same as that whole file, which build_libpthread.sh's own
# header already correctly excludes as os_workgroup-adjacent),
# _pthread_malloc/_pthread_free (real global function-pointer variables,
# pthread.c:141-142, initialized directly to this project's own real
# malloc/free -- functionally identical to what real bsdthread_register
# would populate them with), __bsdthread_ctl (real raw syscall wrapper,
# #478), and _os_alloc_once/_os_alloc_once_table (same real mechanism-
# present-but-never-compiled situation as syslogd's own
# syslogd_alloc_once_compat.c, applied to qos.c's own real
# OS_ALLOC_ONCE_KEY_LIBSYSTEM_PTHREAD consumer -- a separate link unit
# from syslogd.macho, so no duplicate-symbol risk reusing the same real
# fix here).
#
# posix_spawnattr_get/set_qos_clamp_np: real, narrow field accessors on
# the real struct _posix_spawnattr (bsd/sys/spawn_internal.h) -- their
# actual defining file, liblaunchd_syscalls.a's posix_spawn.o, bundles
# the real posix_spawn(2) implementation itself (needing the real,
# substantial ___posix_spawn syscall this pass has no reason to wire in
# just for 2 field accessors) -- reproduced narrowly in
# libpthread_qos_compat.c instead, same "copy just the one real
# function, not the whole file" precedent as
# dispatch_workgroup_tsd_compat.c/asl_client_kvdict before it.
set -e
cd "$(dirname "$0")"

PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
PLATFORM="../../third_party/libplatform"
MALLOC="../../third_party/libmalloc"
OUT="libc_build"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi
if [ ! -f "$OUT/libplatform.a" ] || [ ! -f "$OUT/libpthread.a" ] || [ ! -f "$OUT/liblaunchd_syscalls.a" ]; then
    echo "error: run build_libplatform.sh, build_libpthread.sh, and build_launchd_syscalls.sh first" >&2
    exit 1
fi
MIG="$(which mig)"
if [ -z "$MIG" ] || [ ! -x "$MIG" ]; then
    echo "error: mig tool not found (needed to regenerate mach/mach_voucher.h)" >&2
    exit 1
fi

# Real gap found via real QEMU boot-testing: dispatch's own voucher.c
# calls mach_voucher_extract_attr_recipe()/mach_voucher_attr_command()/
# mach_voucher_debug_info() -- real MIG routines this project's already-
# pinned xnu-7195 osfmk/mach/mach_voucher.defs genuinely has (unlike
# host_create_mach_voucher below), just never regenerated here. Same
# real mig-regeneration precedent as build_libmach_ipc.sh's own clock/
# mach_host/task .defs regen.
XNU_ABS="$(cd "$XNU" && pwd)"
MIGOUT="$OUT/mach_voucher_mig"
mkdir -p "$MIGOUT"
"$MIG" -arch arm64 -I"$XNU_ABS/osfmk" \
    -header "$MIGOUT/mach_voucher.h" \
    -user "$MIGOUT/mach_voucherUser.c" -server /dev/null -sheader /dev/null \
    "$XNU_ABS/osfmk/mach/mach_voucher.defs" >/dev/null
echo "mig: regenerated mach_voucher.h/mach_voucherUser.c from real xnu-7195 mach_voucher.defs"

# Real gap: host_get_special_port -- real MIG routine, host_priv.defs
# already regenerated once elsewhere (build_launchd_mig.sh's own
# host_privUser.o, archived in liblaunchd_core.a), but reusing that
# pre-built object directly pulled in a real stack-protector mismatch
# (___chkstk_darwin, not resolved anywhere in this project -- that
# object was compiled under different flags than this script's own
# consistent -fno-stack-protector). Regenerated + recompiled fresh here
# instead, same real .defs, this script's own consistent CFLAGS_MIG.
"$MIG" -arch arm64 -I"$XNU_ABS/osfmk" \
    -header "$MIGOUT/host_priv.h" \
    -user "$MIGOUT/host_privUser.c" -server /dev/null -sheader /dev/null \
    "$XNU_ABS/osfmk/mach/host_priv.defs" >/dev/null
echo "mig: regenerated host_priv.h/host_privUser.c from real xnu-7195 host_priv.defs"

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -Wno-availability -Wno-deprecated-declarations \
    -D__LIBC__ -D__POSIX_LIB__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_NON_CANCELABLE=1 \
    -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 -D__PTHREAD_BUILDING_PTHREAD__=1 -D__PTHREAD_EXPOSE_INTERNALS__ \
    -DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1 -DPRIVATE=1 -DVARIANT_STATIC=1 \
    -D_os_xbs_chrooted=0 \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=ios,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos\(x\)=ios,introduced=x \
    -DSPI_AVAILABLE\(...\)= -D__SPI_AVAILABLE\(...\)= \
    -I . \
    -I "$PTHREAD" -I "$PTHREAD/src" -I "$PTHREAD/src/resolver" -I "$PTHREAD/private" -I "$PTHREAD/include" -I "$PTHREAD/kern" \
    -I "$PLATFORM/internal" -I "$PLATFORM/private" -I "$PLATFORM/include" \
    -I "$OUT/compat_include" \
    -I "$MALLOC/private" \
    -I "$XNU/libsyscall" -I "$XNU/libsyscall/mach" -I "$XNU/bsd" -I "$XNU/libsyscall/wrappers/spawn" \
    -idirafter "$XNU/osfmk" -idirafter "$XNU/libkern")

mkdir -p "$OUT/pthread_qos_obj"
ok=0
fail=0
for src in "$PTHREAD/src/qos.c" libpthread_qos_compat.c libpthread_attr_compat.c; do
    base=$(basename "$src" .c)
    out="$OUT/pthread_qos_obj/${base}.o"
    if "$CLANG" "${CFLAGS[@]}" "$src" -o "$out" 2>"$OUT/pthread_qos_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/pthread_qos_obj/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/pthread_qos_obj/${base}.err")"
    fi
done
CFLAGS_MIG=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$MIGOUT" -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk")
if "$CLANG" "${CFLAGS_MIG[@]}" "$MIGOUT/mach_voucherUser.c" -o "$OUT/pthread_qos_obj/mach_voucherUser.o" 2>"$OUT/pthread_qos_obj/mach_voucherUser.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/pthread_qos_obj/mach_voucherUser.err"
else
    fail=$((fail + 1))
    echo "  gap: mach_voucherUser -- $(grep -m1 'error:' "$OUT/pthread_qos_obj/mach_voucherUser.err")"
fi
if "$CLANG" "${CFLAGS_MIG[@]}" "$MIGOUT/host_privUser.c" -o "$OUT/pthread_qos_obj/host_privUser.o" 2>"$OUT/pthread_qos_obj/host_privUser.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/pthread_qos_obj/host_privUser.err"
else
    fail=$((fail + 1))
    echo "  gap: host_privUser -- $(grep -m1 'error:' "$OUT/pthread_qos_obj/host_privUser.err")"
fi

echo "libpthread_qos: $ok compiled, $fail gaps (see $OUT/pthread_qos_obj/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f "$OUT/libpthread_qos.a"
ar rcs "$OUT/libpthread_qos.a" "$OUT"/pthread_qos_obj/*.o
echo "wrote $OUT/libpthread_qos.a ($(ls "$OUT"/pthread_qos_obj/*.o | wc -l | tr -d ' ') objects)"

echo "=== real remaining undefined-symbol list (informational) ==="
nm -u "$OUT/libpthread_qos.a" 2>/dev/null | sort -u
