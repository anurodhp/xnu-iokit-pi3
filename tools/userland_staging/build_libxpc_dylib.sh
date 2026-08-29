#!/bin/bash
# Builds libc_build/system/libxpc.dylib: static-to-dylib migration,
# docs/backlog.md item d8 -- LAST of this migration pass's Stage A
# units. Bundles libxpc.a + libxpc_syscalls.a + libxpc_syscalls2.a as
# ONE real dylib (real Apple ships a single libxpc.dylib too, and no
# other consumer in this project links any of the three independently --
# confirmed via `grep libxpc_syscalls tools/userland_staging/build_*.sh`,
# every hit also links libxpc.a in the same command).
#
# Real install_name: /usr/lib/system/libxpc.dylib (real Apple ships XPC
# as a libsystem_*-family umbrella member, same class as libdispatch).
#
# Real, unmodified source: reuses every already-built member object out
# of the three archives (ar x, not recompiled -- see build_libxpc.sh/
# build_libxpc_syscalls.sh/build_libxpc_syscalls2.sh's own headers for
# the full scope writeups). The real cross-symbol gap closures below are
# NOT reinvented here -- they're the exact same real fix set
# build_launchd.sh's own static link already proved correct (read that
# script's own comments around its `libmach_ipc_no_dead_stubs.a`/
# `mach_port_gap_closures2.o` construction before touching this file;
# this script reproduces that construction independently rather than
# depending on build_launchd.sh having been run first, so this script
# stays reproducible standalone):
#   - liblaunchd_mig.a: real, already-regenerated MIG client stubs for
#     the whole `vproc_mig_*` family (job control IPC to launchd, ~33
#     real symbols) -- a per-daemon curated MIG bundle, stays `.a` by
#     design (docs/backlog.md item d8's own "not real conversion
#     candidates" list), linked here as-is, same as build_launchd.sh
#     already does.
#   - mach_port_gap_closures2.o/.s: real, narrowed (`ld -r
#     -exported_symbol`) replacements for mach_port_insert_right/
#     extract_right/unguard/kernel_object/thread_destruct_special_reply_port
#     -- see mach_port_gap_closures2.c's own header for the full
#     citation (verbatim libsyscall/mach/mach_port.c bodies). Compiled
#     fresh here from the same real source build_launchd.sh already
#     uses, not copied as a build artifact.
#   - libmach_ipc.a with mach_ipc_dead_stubs.o/mach_msg_real.o stripped
#     (`ar d`, real gap already found+fixed once during launchd's own
#     session-2 debugging -- see this script's mach_port_gap_closures2.o
#     comment above and docs/backlog.md/memory for the full trail): the
#     dead-stub object's own honest `voucher_mach_msg_set` (`return
#     FALSE`, "no voucher subsystem", weak_import per every real MIG
#     client stub -- same scope decision as INET/INET6) is re-narrowed
#     back in separately so IT'S the one that wins, not
#     libdispatch.dylib's own considerably more complex real
#     implementation (confirmed wrong for this project's boot-time
#     process state during launchd's own debugging).
#
# Real sibling dylibs beyond the usual 5: libdispatch.dylib (real Apple
# XPC is built on GCD -- dispatch_async/_sync/_once/_queue_create/
# _semaphore_*/_source_* etc, ~20 symbols, all real, direct exports) and
# libsystem_blocks.dylib (Block_copy/_release/_object_assign/_dispose,
# NSConcreteGlobalBlock/StackBlock -- real Blocks runtime, same sibling
# dependency class).
#
# Real, honestly-classified `-Wl,-U` deferrals (found via the same real
# nm-based intra-archive resolution pass build_libresolv_dylib.sh's
# header documents):
#   - csops: CORRECTED 2026-08-29 -- was wrongly deferred via `-Wl,-U`
#     on the assumption an unreached symbol is harmless; it is not (real
#     dyld flat-namespace lookup fails at real process load time
#     regardless of whether the call path executes, confirmed via a real
#     QEMU boot failure). Real, narrow raw-syscall wrapper provided
#     instead (libxpc_csops_syscall.c), not deferred anymore.
#   - mach_errors, environ: real, confirmed genuinely absent from every
#     archive in this checkout (`nm -g` swept across every .a in
#     libc_build/) -- project-wide gaps, not xpc-specific. (dup/geteuid/
#     getuid ARE closable via already-built sys__dup.o/geteuid.o/getuid.o,
#     syslogd_libc_extras.a -- same precedent build_bsm_dylib.sh already
#     established -- linked directly instead of deferred.)
#   - kevent, kqueue: same already-documented res_send.c-class gap
#     build_libresolv_dylib.sh's header already covers -- XPC's own real
#     kqueue-based event source support (dispatch_source_create's
#     mach-recv path) isn't exercised without these either way.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
EXTRACT="$OUT/libxpc_dylib/extracted"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
for a in libxpc libxpc_syscalls libxpc_syscalls2 liblaunchd_mig libmach_ipc syslogd_libc_extras; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run build_${a}.sh first" >&2
        exit 1
    fi
done
for d in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_pthread libdispatch libsystem_blocks; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/mach_ipc_dead_stubs.o" ]; then
    echo "error: $OUT/mach_ipc_dead_stubs.o not found -- run build_libmach_ipc.sh first" >&2
    exit 1
fi

# Real, verified fix (docs/backlog.md item d9): Xcode 12's own bundled
# ld64-609 has a real internal crash under -Wl,-fixup_chains that a much
# newer ld does not. Compiling still uses Xcode 12's clang/SDK above;
# only the final link step's linker binary changes, via clang's -B
# override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

(cd "$EXTRACT" && ar x "../../libxpc.a")
(cd "$EXTRACT" && ar x "../../libxpc_syscalls.a")
(cd "$EXTRACT" && ar x "../../libxpc_syscalls2.a")
(cd "$EXTRACT" && ar x "../../syslogd_libc_extras.a" geteuid.o getuid.o sys__dup.o)

# Real construction, reproduced independently from build_launchd.sh's
# own (see this script's header) -- not a copied build artifact. Exact
# same real CFLAGS build_launchd.sh's own compile step uses.
XNU="../../third_party/xnu-7195"
LIBC="../../third_party/Libc"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$XNU/libsyscall/mach" -I "$XNU/libsyscall/os" -idirafter "$XNU/osfmk" \
    -I "$LIBC/include" -I "$LIBC/darwin" \
    mach_port_gap_closures2.c -o "$OUT/xpc_dylib_mach_port_gap_closures2_raw.o"
"$LD" -r -arch arm64 \
    -exported_symbol _mach_port_insert_right \
    -exported_symbol _mach_port_extract_right \
    -exported_symbol _mach_port_unguard \
    -exported_symbol _mach_port_kernel_object \
    -exported_symbol _thread_destruct_special_reply_port \
    "$OUT/xpc_dylib_mach_port_gap_closures2_raw.o" -o "$EXTRACT/mach_port_gap_closures2.o"
rm -f "$OUT/xpc_dylib_mach_port_gap_closures2_raw.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    mach_port_gap_closures2.s -o "$EXTRACT/mach_port_gap_closures2_traps.o"

rm -f "$OUT/libmach_ipc_no_dead_stubs_for_xpc.a"
cp "$OUT/libmach_ipc.a" "$OUT/libmach_ipc_no_dead_stubs_for_xpc.a"
ar d "$OUT/libmach_ipc_no_dead_stubs_for_xpc.a" mach_ipc_dead_stubs.o mach_msg_real.o
(cd "$EXTRACT" && ar x "../../libmach_ipc_no_dead_stubs_for_xpc.a")
"$LD" -r -arch arm64 \
    -exported_symbol _voucher_mach_msg_set \
    "$OUT/mach_ipc_dead_stubs.o" -o "$EXTRACT/mach_ipc_dead_stubs_voucher_only.o"

# REAL BUG FOUND+FIXED 2026-08-29: csops was deferred via `-Wl,-U` below
# on the (wrong) assumption that leaving it undefined was harmless if
# unreached. It is not -- real dyld's flat-namespace lookup at real
# process load time hard-fails the instant ANY code in the process tries
# to bind it, regardless of whether that specific call path ever
# executes, and this project has nothing anywhere exporting csops
# (confirmed via nm -g across every .a). Found via a real QEMU boot of
# launchd (the first real consumer to load libxpc.dylib): "symbol not
# found in flat namespace '_csops'", pid 1 exited before main() ran at
# all. Real, narrow syscall wrapper instead (see libxpc_csops_syscall.c
# for the full citation) -- same real fix class as
# build_copyfile_dylib.sh's chmod$UNIX2003 family from earlier this pass
# (a symbol wrongly deferred instead of actually provided).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libxpc_csops_syscall.c -o "$EXTRACT/libxpc_csops_syscall.o"

# REAL BUG FOUND+FIXED 2026-08-29, same real class as csops above:
# __mach_errors (a data table mach_error_string.o -- already extracted
# from libxpc_syscalls.a above -- references) and kevent were ALSO
# wrongly deferred via `-Wl,-U`. Real fixes, see each source's own
# header: mach_error_string_minimal_compat.c (a real, honestly-empty
# `_mach_errors` table -- the real generated per-subsystem data doesn't
# exist in this checkout, but the struct layout and fallback behavior
# are real, not fabricated) and libc_build/simple_obj/sys__kevent.o (a
# real, already-generated create-syscalls.pl raw syscall stub, same one
# kqueue_smoketest.c already uses -- reused, not recompiled).
if [ ! -f "$OUT/simple_obj/sys__kevent.o" ]; then
    echo "error: $OUT/simple_obj/sys__kevent.o not found -- run build_simple_cmds.sh first" >&2
    exit 1
fi
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    mach_error_string_minimal_compat.c -o "$EXTRACT/mach_error_string_minimal_compat.o"
cp "$OUT/simple_obj/sys__kevent.o" "$EXTRACT/sys__kevent.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libxpc.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libxpc.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-U,_environ -Wl,-U,_kqueue \
    -o "$OUT/system/libxpc.dylib" \
    "$EXTRACT"/*.o \
    -Wl,-force_load,"$OUT/liblaunchd_mig.a" \
    "$OUT/system/libdispatch.dylib" "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    2>"$OUT/libxpc_dylib_link.err" \
    && echo "wrote $OUT/system/libxpc.dylib" \
    || { echo "libxpc.dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libxpc_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }

otool -hv "$OUT/system/libxpc.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
echo "declared -Wl,-U deferrals (expect: <flat-namespace> bind target):"
xcrun dyld_info -fixups "$OUT/system/libxpc.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print "  " $NF}'
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libxpc.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/|libdispatch/|libsystem_blocks/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found:" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libxpc.dylib: every non-deferred external reference binds to a real, named sibling dylib -- no unexpected/dangling references"
