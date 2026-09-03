#!/bin/bash
# Builds libc_build/libnotify_client.a: the real, public Libnotify client
# API (notify_post/notify_register_dispatch/notify_register_check/
# notify_cancel/notify_set_state/notify_get_state/etc) -- everything
# build_libnotify.sh's own archive deliberately left out (it only ever
# built the raw MIG transport, notify_ipcUser.o, not the 3 real .c files
# that call it: libnotify.c/notify_client.c/table.c). That script's own
# header comment explains why, as of when it was written: every one of
# those 3 files transitively #includes <xpc/private.h> for a single real
# type, xpc_event_publisher_t, and this project had zero XPC anywhere at
# the time.
#
# That's since changed (Phase 6.8, PureDarwin XPC port -- libxpc.a/
# liblaunchd_mig.a/libdispatch_real.a all real and already built). This
# script uses PureDarwin's own fork of Libnotify's client tree
# (third_party/PureDarwin/src/Libraries/XPC/notify/), not
# third_party/Libnotify -- confirmed identical notify_ipc.defs (byte-for-
# byte diff, zero output) so it links against the exact notify_ipc.h this
# project's build_libnotify.sh already MIG-regenerated, but PureDarwin's
# own tree additionally ships real, already-written compat glue this
# project's needs line up with exactly: pd_notifyd_compat.h (declares
# xpc_event_publisher_t + a real xpc_copy_entitlement_for_token()/
# xpc_event_publisher_fire_noboost() stand-in -- PureDarwin's own choice,
# not this project's; both real call sites are notifyd-server-side-only
# paths, dead code for a pure client, confirmed by reading them) and
# pd_xpc_event_publisher.c (not needed here -- client-only build).
#
# Real remaining gaps found and fixed this pass (each is small/bounded,
# not another xpc/private.h-class wall):
#   - os/alloc_once_private.h: genuinely absent from every pinned repo
#     under that name (grepped, zero hits) -- the real mechanism it
#     wraps (_os_alloc_once/os_alloc_once) IS present, at
#     third_party/libplatform/private/os/alloc_once_impl.h, gated behind
#     a real __OS_ALLOC_INDIRECT__ include-guard macro. Real forwarder:
#     syslog_compat_headers/os/alloc_once_private.h defines that guard
#     macro, includes the real impl header, then also defines the one
#     per-consumer key ID (OS_ALLOC_ONCE_KEY_LIBSYSTEM_NOTIFY) Apple's
#     real header would otherwise centrally enumerate -- safe because
#     nothing else in this project's link currently claims a slot.
#   - dispatch/private.h (and its own further #includes, e.g.
#     dispatch/benchmark.h): Apple's real SDK installs
#     third_party/libdispatch/private/*.h into usr/local/include/
#     dispatch/ via an Xcode copy-headers phase this project has no
#     equivalent of. Real fix: dispatch_compat_headers/dispatch is a
#     symlink straight at third_party/libdispatch/private (all of it,
#     not a one-off single-file forward) -- every sibling private header
#     resolves for real through the same symlink.
#   - mach-o/dyld_priv.h's _dyld_is_memory_immutable(): used exactly
#     once, in _notify_strdup_if_mutable(), to skip a defensive strdup
#     for string literals known to live in an immutable dyld segment.
#     This build is fully static (no dyld at all) -- confirmed by
#     reading the one real call site that "not verified immutable"
#     (i.e. always return false) is always a safe, merely-non-optimal
#     answer, never a correctness bug (worst case: one extra malloc+
#     memcpy). Real implementation: notify_dyld_compat.c.
#   - sys/fileport.h (fileport_t/FILEPORT_NULL/fileport_makeport):
#     genuinely real, present verbatim in
#     third_party/xnu-7195/bsd/sys/fileport.h, just never transitively
#     reachable the normal hosted way notify_client.c expects (Apple's
#     real SDK pulls it in via <bootstrap_priv.h>, which this project has
#     never built). Forced in directly via -include. The real syscalls
#     it declares (fileport_makeport/fileport_makefd, #430/#431 per
#     third_party/xnu-7195/bsd/kern/syscalls.master, cross-checked, not
#     guessed) had no wrapper anywhere in this project yet -- real raw
#     svc-based wrappers added in notify_fileport_syscalls.c, same
#     carry-flag pattern as getty_syscalls.c's revoke/setrlimit/setsid.
#
# Real dependency ordering: needs libc_build/notify_mig/notify_ipc.h
# (build_libnotify.sh's MIG regen) and libc_build/libxpc.a/
# liblaunchd_mig.a/libdispatch_real.a (Phase 6.8 PureDarwin XPC port)
# already built.
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi

XNU="../../third_party/xnu-7195"
DISPATCH="../../third_party/libdispatch"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
LIBC="../../third_party/Libc"
PDNOTIFY="../../third_party/PureDarwin/src/Libraries/XPC/notify"
PD="../../third_party/PureDarwin/src/Libraries/XPC"
AVAIL_VERSIONS="../../build/xnu-latest-build/AvailabilityVersions/dst/usr/local/include"
LAUNCHD_COMPAT="launchd_compat_headers"
DCOMPAT="dispatch_compat_headers"
SCOMPAT="syslog_compat_headers"
OUT="libc_build"
MIGOUT="$OUT/notify_mig"

if [ ! -f "$MIGOUT/notify_ipc.h" ]; then
    echo "error: $MIGOUT/notify_ipc.h not found -- run build_libnotify.sh first" >&2
    exit 1
fi
for a in libxpc liblaunchd_mig libdispatch_real libmach_ipc libmalloc libplatform; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run build_${a}.sh first" >&2
        exit 1
    fi
done

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=ios,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos\(x\)=ios,introduced=x \
    -DSPI_AVAILABLE\(...\)= -D__SPI_AVAILABLE\(...\)= \
    -include "$SCOMPAT/sys/kevent_qos_compat.h" \
    -include "sys/fileport.h" -include "$PDNOTIFY/pd_notifyd_compat.h" \
    -I "$PDNOTIFY" -I "$PDNOTIFY/compat" -I "$MIGOUT" \
    -I "$SCOMPAT" -I "$LAUNCHD_COMPAT" -I "$DCOMPAT" \
    -I "$PD/launchd/System-compat" -I "$PD/libxpc/include" \
    -I "$DISPATCH" -I "$DISPATCH/private" \
    -idirafter "$AVAIL_VERSIONS" \
    -idirafter "$XNU/libsyscall/wrappers/libproc" -idirafter "$XNU/libsyscall/wrappers/spawn" \
    -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libsyscall" -idirafter "$XNU/libsyscall/mach" \
    -idirafter "$PTHREAD/include" -idirafter "$PTHREAD/private" \
    -idirafter "$PLATFORM/private" -idirafter "$PLATFORM/include" -idirafter "$PLATFORM/internal" \
    -idirafter "$XNU/BUILD.hdrs/dst/usr/local/include" -idirafter "$XNU/EXTERNAL_HEADERS" \
    -idirafter "$LIBC/darwin" -idirafter "$LIBC" -idirafter "$XNU/libkern")

mkdir -p "$OUT/notify_client_obj"
ok=0
fail=0
for f in notify_client libnotify table; do
    out="$OUT/notify_client_obj/${f}.o"
    if "$CLANG" "${CFLAGS[@]}" "$PDNOTIFY/$f.c" -o "$out" 2>"$OUT/notify_client_obj/${f}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/notify_client_obj/${f}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $f -- $(grep -m1 'error:' "$OUT/notify_client_obj/${f}.err")"
    fi
done
# This project's own new compat: real fileport syscalls + real, always-
# safe _dyld_is_memory_immutable() (see this script's own header comment).
for f in notify_fileport_syscalls notify_dyld_compat; do
    out="$OUT/notify_client_obj/${f}.o"
    if "$CLANG" "${CFLAGS[@]}" "${f}.c" -o "$out" 2>"$OUT/notify_client_obj/${f}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/notify_client_obj/${f}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $f -- $(grep -m1 'error:' "$OUT/notify_client_obj/${f}.err")"
    fi
done
echo "libnotify_client: $ok compiled, $fail gaps (see $OUT/notify_client_obj/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f "$OUT/libnotify_client.a"
ar rcs "$OUT/libnotify_client.a" "$OUT"/notify_client_obj/*.o
echo "wrote $OUT/libnotify_client.a"

# Real smoketest: links a tiny real notify_post()/notify_register_check()
# consumer against this archive plus libnotify.a (the raw MIG transport)
# and the full XPC/dispatch/mach_ipc stack. No real notifyd exists in
# this project (Phase 6.7's bootstrap-namespace registrar shim isn't
# built yet, see docs/daemon-roadmap.md) -- bootstrap_look_up2() is
# expected to fail fast (NOT hang: sending to an unanswered/invalid
# bootstrap port returns a real Mach send error synchronously, confirmed
# by reading _notify_lib_init_locked()'s own real error-checking code
# directly), so every real notify_* call should just return
# NOTIFY_STATUS_SERVER_NOT_FOUND-class failures. This smoketest only
# proves real static linkage; it does NOT prove real end-to-end
# notification delivery (needs the registrar shim first).
NEED_OK=1
for a in libnotify libxpc liblaunchd_mig libxpc_syscalls libxpc_syscalls2 liblaunchd_syscalls libdispatch_real libmach_ipc libclosure libmalloc libplatform libpthread libc_gen libc_string libc_stdio libc_printf libc_locale_for_stdio; do
    [ -f "$OUT/$a.a" ] || NEED_OK=0
done
if [ "$NEED_OK" -eq 1 ]; then
    # Smoketest main compiled into its OWN subdirectory
    # ($OUT/notify_client_obj/smoketest/, not $OUT/notify_client_obj/
    # itself) -- fixed 2026-09-03 (DAR-229, per-library dedup series).
    # Root cause of a real, confirmed bug: this archive's own `ar rcs`
    # above globs "$OUT"/notify_client_obj/*.o (non-recursive), which on
    # a SECOND run of this script picked up the FIRST run's leftover
    # libnotify_client_smoketest.o (nothing ever cleaned it, and this
    # step necessarily runs after the archive step above) -- a real
    # smoketest main() ends up archived into libnotify_client.a, which
    # build_libsystem_notify.sh then `ar x`s wholesale into the shipped
    # libsystem_notify.dylib, exporting `_main` from it. Confirmed via
    # `nm -m`: libsystem_notify.dylib exported `_main` from exactly this
    # object. A subdirectory the archive step's own `*.o` glob can never
    # match is a permanent fix, not a one-time cleanup.
    mkdir -p "$OUT/notify_client_obj/smoketest"
    rm -f libnotify_client_smoketest.macho
    "$CLANG" "${CFLAGS[@]}" libnotify_client_smoketest.c -o "$OUT/notify_client_obj/smoketest/libnotify_client_smoketest.o"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
        -o libnotify_client_smoketest.macho \
        ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
        "$OUT/notify_client_obj/smoketest/libnotify_client_smoketest.o" \
        -Wl,-force_load,"$OUT/libnotify_client.a" \
        "$OUT/libnotify.a" \
        "$OUT/libxpc.a" "$OUT/liblaunchd_mig.a" \
        "$OUT/libxpc_syscalls.a" "$OUT/libxpc_syscalls2.a" "$OUT/liblaunchd_syscalls.a" \
        "$OUT/libdispatch_real.a" \
        "$OUT/libmach_ipc.a" "$OUT/libclosure.a" "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
        "$OUT/libpthread.a" "$OUT/libc_gen.a" "$OUT/libc_string.a" "$OUT/libc_stdio.a" "$OUT/libc_printf.a" \
        "$OUT/libc_locale_for_stdio.a" \
        "$OUT/simple_obj/sys__kqueue.o" \
        -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
        -Wl,-alias,___kill,'_kill$UNIX2003' \
        -Wl,-alias,___kill,_kill \
        -Wl,-alias,_write,'_write$UNIX2003' \
        -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
        -Wl,-alias,___kdebug_trace,_kdebug_trace \
        -Wl,-alias,_open,'_open$UNIX2003' \
        -Wl,-alias,_close,'_close$UNIX2003' \
        -Wl,-alias,_fcntl,'_fcntl$UNIX2003' \
        -Wl,-alias,_read,'_read$UNIX2003' \
        -Wl,-alias,_fstat,'_fstat$INODE64' \
        -Wl,-alias,_stat,'_stat$INODE64' \
        -Wl,-alias,_lstat,'_lstat$INODE64' \
        -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003',_pthread_rwlock_wrlock \
        -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
        -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
        2>"$OUT/libnotify_client_smoketest_link.err" && echo "wrote libnotify_client_smoketest.macho" || echo "WARNING: smoketest link failed (libnotify_client.a itself is still built -- see $OUT/libnotify_client_smoketest_link.err)" >&2

    if [ -f libnotify_client_smoketest.macho ]; then
        otool -l libnotify_client_smoketest.macho | grep -q LC_LOAD_DYLIB && \
            echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
        otool -l libnotify_client_smoketest.macho | grep -q LC_UNIXTHREAD || \
            echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
        if nm -u libnotify_client_smoketest.macho | grep -q .; then
            echo "WARNING: undefined symbols present:" >&2
            nm -u libnotify_client_smoketest.macho >&2
        fi
    else
        tail -60 "$OUT/libnotify_client_smoketest_link.err" >&2
    fi
else
    echo "skip: libnotify_client_smoketest.macho (missing a prerequisite .a -- run its build_*.sh first)"
fi
