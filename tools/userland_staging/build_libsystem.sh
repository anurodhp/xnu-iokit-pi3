#!/bin/bash
# Builds libc_build/system/libSystem.B.dylib: the second (of two) real,
# unconditional, hardcoded install_name dyldMain.cpp requires every
# launched process to link against (RuntimeState::add() in
# DyldRuntimeState.cpp matches "/usr/lib/libSystem.B.dylib" exactly).
# Phase 6.5 Phase D (docs/daemon-roadmap.md), companion to
# build_libdyld.sh.
#
# Real, deliberately simplified architecture (see build_libdyld.sh's own
# header comment for the full reasoning): real macOS's libSystem.B.dylib
# is itself a thin re-export umbrella over a half-dozen separate real
# libsystem_*.dylib/libdispatch.dylib sub-images. This project has never
# built libSystem as separate sub-dylibs -- every build_*.sh here
# produces ONE flat static archive per curated library, the same real
# objects every static binary in this project already links directly.
# This script bundles that exact same already-built, already-tested
# curated stack into ONE real dylib instead, reusing the identical
# archive list + scratch symbol-collision fixes build_libdyld.sh already
# had to solve for this same combination (libcxxabi's real
# cxa_exception.o pulling libunwind's real C++ unwinder wanting
# libunwind_dyld_shim.o's __mh_execute_header; libmalloc_dispatch.o's
# stale __platform_memset/_dyld_get_image_slide hollow stand-ins) -- not
# re-derived, reused verbatim.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
mkdir -p "$OUT/libsystem"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
for a in libc_gen libc_string libc_stdio libc_printf libpthread libmalloc \
         libcxx libcxxabi libunwind libcopyfile libremovefile libresolv; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run its build_$a.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/libcxx_locale_for_stdio_scratch.a" ] || [ ! -f "$OUT/libcxx_tsd_shim_bootstrap_compat.o" ]; then
    echo "error: $OUT/libcxx_locale_for_stdio_scratch.a / libcxx_tsd_shim_bootstrap_compat.o not found -- run build_llvm_libcxx.sh first" >&2
    exit 1
fi
# libplatform.a/libclosure.a/cerror_stub.o no longer required here
# (static-to-dylib migration batch 1, docs/daemon-roadmap.md) -- real
# libsystem_kernel.dylib/libsystem_platform.dylib/libsystem_blocks.dylib
# below instead, same real source.
if [ ! -f "$OUT/system/libsystem_platform.dylib" ] || [ ! -f "$OUT/system/libsystem_blocks.dylib" ]; then
    echo "error: $OUT/system/libsystem_platform.dylib / libsystem_blocks.dylib not found -- run build_libsystem_platform.sh and build_libsystem_blocks.sh first" >&2
    exit 1
fi
RTLIB="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/12.0.0/lib/darwin/libclang_rt.ios.a"
if [ ! -f "$RTLIB" ]; then
    echo "error: $RTLIB not found (real LLVM compiler-rt, provides ___chkstk_darwin)" >&2
    exit 1
fi

# Real __cxa_atexit/malloc_size compat this same combination already
# needed for libdyld.dylib -- reused, not re-derived. pthread-fork-child/
# TLV/mkdirat pieces in that same file are harmless here too (same real
# archive combination, same real gaps).
if [ ! -f "$OUT/libdyld/compat_misc.o" ]; then
    echo "error: $OUT/libdyld/compat_misc.o not found -- run build_libdyld.sh first" >&2
    exit 1
fi

# Same real scratch fixes as build_libdyld.sh -- see that script's own
# header comments for the full story on each.
SCRATCH="$OUT/libsystem_scratch"
rm -rf "$SCRATCH"
mkdir -p "$SCRATCH"
cp "$OUT/libunwind.a" "$SCRATCH/libunwind_scratch.a"
ar d "$SCRATCH/libunwind_scratch.a" libunwind_dyld_shim.o
cat > "$SCRATCH/dyld_unwind_shim_compat.c" << 'EOF'
int _dyld_find_unwind_sections(void *addr, void *info) {
    (void)addr; (void)info;
    return 0;
}
void _dyld_register_func_for_remove_image(void (*func)(const void *, unsigned long)) {
    (void)func;
}
void *dladdr(const void *addr, void *info) {
    (void)addr; (void)info;
    return 0;
}
EOF
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c "$SCRATCH/dyld_unwind_shim_compat.c" -o "$SCRATCH/dyld_unwind_shim_compat.o"

# Unlike build_libdyld.sh, this dylib links no libdyldGlue.o -- there is
# no better real implementation of _dyld_get_image_slide to prefer here,
# so only __platform_memset (real conflict with libsystem_platform.dylib's
# own, linked below) is hidden; libmalloc_dispatch.o's own
# _dyld_get_image_slide stays as the sole, real provider.
mkdir -p "$SCRATCH/extracted"
(cd "$SCRATCH/extracted" && ar x "../../libmalloc.a" libmalloc_dispatch.o)
cat > "$SCRATCH/unexp_malloc_dispatch.txt" << 'EOF'
__platform_memset
EOF
"$LD" -r -arch arm64 -unexported_symbols_list "$SCRATCH/unexp_malloc_dispatch.txt" \
    "$SCRATCH/extracted/libmalloc_dispatch.o" -o "$SCRATCH/libmalloc_dispatch_trimmed.o"
cp "$OUT/libmalloc.a" "$SCRATCH/libmalloc_scratch.a"
ar d "$SCRATCH/libmalloc_scratch.a" libmalloc_dispatch.o
ar r "$SCRATCH/libmalloc_scratch.a" "$SCRATCH/libmalloc_dispatch_trimmed.o"

# Same static-to-dylib migration step as build_libdyld.sh (see that
# script's own comment): the Mach-bootstrap/VM-trap layer now lives in
# real, separate libsystem_kernel.dylib, shared with libdyld.dylib,
# instead of each dylib bundling its own private (and, for
# mach_task_self_, independently-never-initialized) static copy.
if [ ! -f "$OUT/system/libsystem_kernel.dylib" ]; then
    echo "error: $OUT/system/libsystem_kernel.dylib not found -- run build_libsystem_kernel.sh first" >&2
    exit 1
fi
ar d "$SCRATCH/libmalloc_scratch.a" mach_init.o mach_vm.o mig_reply_port.o \
    init_cpu_capabilities.o libmalloc_vm_traps.o libmalloc_vm_mig_stubs.o

# Real global storage only this dylib needs (see that file's own
# comment) -- libdyld.dylib already provides it itself.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libsystem_progname_compat.c -o "$OUT/libsystem/progname_compat.o"

# Real gap: see libsystem_dyld_initializer_compat.c's own comment. Real
# libSystem.dylib's Libc/init.c calls _dyld_initializer() as part of its
# own real constructor; this project's simplified libSystem.B.dylib has
# no such glue, so a small real constructor calling it directly is the
# minimal correct fix. As of the libsystem_kernel.dylib migration above,
# this constructor's mach_init() call also populates the ONE real, now-
# shared mach_task_self_ both this dylib and libdyld.dylib bind to (see
# that file's own updated comment) -- no more per-dylib duplicate.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libsystem_dyld_initializer_compat.c -o "$OUT/libsystem/dyld_initializer_compat.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libSystem.B.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libSystem.B.dylib \
    -Wl,-fixup_chains \
    -Wl,-U,__dyld_initializer \
    -o "$OUT/system/libSystem.B.dylib" \
    "$SCRATCH/dyld_unwind_shim_compat.o" "$OUT/libsystem/progname_compat.o" \
    "$OUT/libsystem/dyld_initializer_compat.o" \
    "$OUT/libdyld/compat_misc.o" \
    "$OUT/system/libsystem_kernel.dylib" "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/libcxx.a" "$OUT/libcxxabi.a" "$SCRATCH/libunwind_scratch.a" \
    "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libcxx_locale_for_stdio_scratch.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" \
    "$OUT/libc_printf.a" "$OUT/libc_string.a" \
    "$OUT/libcxx_tsd_shim_bootstrap_compat.o" \
    "$OUT/libpthread.a" "$SCRATCH/libmalloc_scratch.a" \
    "$OUT/libcopyfile.a" "$OUT/libremovefile.a" "$OUT/libresolv.a" \
    "$RTLIB" \
    -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003',_pthread_rwlock_wrlock \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
    -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
    -Wl,-alias,___kill,'_kill$UNIX2003' \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,_write,'_write$UNIX2003' \
    -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
    -Wl,-alias,___kdebug_trace,_kdebug_trace \
    -Wl,-alias,_open,'_open$UNIX2003' \
    -Wl,-alias,_close,'_close$UNIX2003' \
    -Wl,-alias,_fcntl,'_fcntl$UNIX2003' \
    -Wl,-alias,_fstat,'_fstat$INODE64' \
    -Wl,-alias,_lstat,'_lstat$INODE64' \
    -Wl,-alias,_stat,'_stat$INODE64' \
    -Wl,-alias,_fsync,'_fsync$UNIX2003' \
    -Wl,-alias,_read,'_read$UNIX2003' \
    -Wl,-alias,_reallocarray,'_reallocarray$DARWIN_EXTSN' \
    -Wl,-alias,'_mmap$UNIX2003',_mmap \
    -Wl,-alias,'_munmap$UNIX2003',_munmap \
    -Wl,-alias,__pthread_cond_wait,_pthread_cond_wait \
    -Wl,-alias,_pthread_testcancel,'_pthread_testcancel$UNIX2003'

echo "wrote $OUT/system/libSystem.B.dylib"
otool -hv "$OUT/system/libSystem.B.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
otool -l "$OUT/system/libSystem.B.dylib" | grep -A2 "LC_ID_DYLIB" | grep -q "/usr/lib/libSystem.B.dylib" || {
    echo "error: LC_ID_DYLIB is not /usr/lib/libSystem.B.dylib" >&2
    exit 1
}
if nm -u "$OUT/system/libSystem.B.dylib" | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u "$OUT/system/libSystem.B.dylib" >&2
else
    echo "libSystem.B.dylib: fully static (beyond itself), zero undefined symbols"
fi
