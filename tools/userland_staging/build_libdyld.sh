#!/bin/bash
# Builds libc_build/system/libdyld.dylib: real, UNMODIFIED Apple dyld-940
# source from third_party/dyld/libdyld/ -- the companion target to
# build_dyld_core.sh's `dyld` tool itself. Phase 6.5 Phase D
# (docs/daemon-roadmap.md): real dyld now boots and runs a trivial
# dependency-free test dylib to completion (see build_dyld_link.sh's own
# header comment for that story), but dyldMain.cpp has a real,
# unconditional, hardcoded requirement that every launched process link
# against BOTH /usr/lib/system/libdyld.dylib and /usr/lib/libSystem.B.dylib
# (matched by exact install_name string in DyldRuntimeState.cpp's
# RuntimeState::add()) -- this script builds the first of those two.
#
# Real files (build_dyld_core.sh's own header comment already identifies
# these as belonging to the separate `libdyld.dylib` Xcode target, not
# the `dyld` tool -- not guessed here):
#   libdyld/libdyldGlue.cpp -- the real _dyld_*() public API surface
#     (dyld_get_image_*, _dyld_register_*, dlopen/dlsym/dladdr plumbing,
#     TLV instantiation thunk) that every process calls into via
#     libdyld.dylib, forwarding into dyld itself through the real
#     gDyld.apis vtable-like pointer dyld installs at launch
#     (dyldMain.cpp, DyldProcessConfig.h's LibdyldDyld4Section).
#   libdyld/LibSystemHelpers.cpp -- the real concrete implementation of
#     the LibSystemHelpers virtual interface (DyldAPIs.h's abstract
#     class) that dyld calls BACK into libSystem/libdyld for
#     malloc/free/vm_allocate/pthread_key_create/os_unfair_lock/etc,
#     since dyld itself only has its own minimal bump allocator early on.
# Deliberately NOT built: dyld_process_info.cpp/
# dyld_process_info_notify.cpp (external debugger/Instruments remote
# process introspection -- no consumer on this target, and pulls in a
# real separate remote-memory-read API surface this port doesn't need to
# satisfy dyldMain.cpp's two structural checks), threadLocalHelpers.s
# (real arm64 TLV/__thread support -- dyld_test_exec has no __thread
# variables; add only if/when a real consumer needs one).
#
# Real linking model, deliberately simplified from Apple's own (see
# configs/libdyld.xcconfig): real macOS links libdyld.dylib -upward-
# against a half-dozen SEPARATE libsystem_*.dylib/libdispatch.dylib
# sub-images (its own umbrella architecture). This project has never
# built libSystem as separate sub-dylibs -- every other build_*.sh here
# produces ONE flat static archive per curated library. Reproducing
# Apple's internal sub-dylib modularity is out of scope (dyld does not
# care how libSystem's insides are organized, only that
# /usr/lib/system/libdyld.dylib and /usr/lib/libSystem.B.dylib exist at
# those exact install_names and export the right symbols) -- this
# script links directly against this project's already-built curated
# static libc_gen/libc_string/libc_stdio/libpthread/libmalloc/
# libplatform archives instead, the same real objects every other
# static binary in this project already uses.
set -e
cd "$(dirname "$0")"

DYLD="../../third_party/dyld"
XNU="../../third_party/xnu-7195"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
LIBC="../../third_party/Libc"
LIBCXX="../../third_party/llvm-project/libcxx"
LIBCXXABI="../../third_party/llvm-project/libcxxabi"
AVAIL_VERSIONS="../../build/xnu-latest-build/AvailabilityVersions/dst/usr/local/include"
OUT="libc_build"
mkdir -p "$OUT/libdyld"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANGXX="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANGXX" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

# Real prerequisite: reuses build_dyld_core.sh's already-generated MIG
# headers ($OUT/dyld_mig) and compat directories instead of duplicating
# that whole regeneration step -- both scripts compile against the exact
# same real dyld/xnu-7195 header graph.
for f in "$OUT/dyld_mig/mach/task.h" "$OUT/dyld_compat_derived/dyld_priv.h" \
         "$OUT/dyld_compat_avail/AvailabilityInternalPrivate.h" \
         "$OUT/dyld_compat_sys_override/sys/event.h" "$OUT/dyld_compat_sys/System/sys/codesign.h"; do
    if [ ! -f "$f" ]; then
        echo "error: $f not found -- run build_dyld_core.sh first (libdyld shares its generated MIG headers/compat dirs)" >&2
        exit 1
    fi
done
for a in libc_gen libc_string libc_stdio libc_printf libpthread libmalloc libplatform libclosure libcxx libcxxabi \
         libunwind libcopyfile libremovefile libresolv; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run its build_$a.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/libcxx_locale_for_stdio_scratch.a" ] || [ ! -f "$OUT/libcxx_tsd_shim_bootstrap_compat.o" ]; then
    echo "error: $OUT/libcxx_locale_for_stdio_scratch.a / libcxx_tsd_shim_bootstrap_compat.o not found -- run build_llvm_libcxx.sh first (same real getenv/pthread_tsd_shim collision fix build_dyld_link.sh already needs)" >&2
    exit 1
fi
if [ ! -f "$OUT/cerror_stub.o" ]; then
    echo "error: $OUT/cerror_stub.o not found -- run build_libplatform.sh first" >&2
    exit 1
fi
RTLIB="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/12.0.0/lib/darwin/libclang_rt.ios.a"
if [ ! -f "$RTLIB" ]; then
    echo "error: $RTLIB not found (real LLVM compiler-rt, provides ___chkstk_darwin)" >&2
    exit 1
fi

MIGOUT="$OUT/dyld_mig"

INCLUDES="-I $DYLD/dyld -I $DYLD/common -I $DYLD/include -I $DYLD/libdyld -I $DYLD/cache-builder \
    -I dyld_compat -I dyld_compat/sandbox \
    -I $OUT/dyld_compat_derived \
    -I $OUT/dyld_compat_avail \
    -I $OUT/dyld_compat_sys_override \
    -I $OUT/dyld_compat_sys \
    -I $MIGOUT -I $MIGOUT/mach \
    -idirafter $AVAIL_VERSIONS \
    -idirafter $XNU/libsyscall/wrappers/libproc \
    -idirafter $XNU/bsd -idirafter $XNU/osfmk -idirafter $XNU/libsyscall -idirafter $XNU/libsyscall/mach \
    -idirafter $PTHREAD/include -idirafter $PTHREAD/private \
    -idirafter $PLATFORM/private -idirafter $PLATFORM/include -idirafter $PLATFORM/internal \
    -idirafter $XNU/BUILD.hdrs/dst/usr/local/include \
    -idirafter $XNU/EXTERNAL_HEADERS \
    -idirafter $LIBC/darwin"

# BUILDING_DYLD=0 (not 1): these shared dyld/common headers (DyldAPIs.h,
# DyldProcessConfig.h, DyldRuntimeState.h) branch on this exact macro to
# select the libdyld.dylib side of the real, shared Apple source instead
# of the dyld-tool side build_dyld_core.sh selects with =1 -- confirmed
# by reading the #if/#else pairs directly, not guessed.
DEFS="-DBUILDING_DYLD=0 -DDYLD_VERSION=940 -DPRIVATE=1 -D_LIBCPP_NO_EXCEPTIONS=1 \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=ios,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos(x)=ios,introduced=x \
    -DSPI_AVAILABLE(...)= -D__SPI_AVAILABLE(...)= \
    -include dyld_compat/dyld_deny_list_compat.h \
    -include dyld_compat/dyld_platform_compat.h"

CXX_FLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -std=c++17 -fno-exceptions $DEFS \
    -nostdinc++ -isystem $LIBCXX/include -I $LIBCXXABI/include \
    $INCLUDES \
    -Wno-macro-redefined -Wno-nullability-completeness -Wno-deprecated-declarations -Wno-availability"

ok=0
fail=0
for base in libdyldGlue LibSystemHelpers; do
    out="$OUT/libdyld/${base}.o"
    if "$CLANGXX" $CXX_FLAGS "$DYLD/libdyld/${base}.cpp" -o "$out" 2>"$OUT/libdyld/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/libdyld/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/libdyld/${base}.err")"
    fi
done
# Real gap (see dyld_compat/System/atexit.h's own comment): this
# project's pinned upstream libcxxabi never grew Apple's own
# `__cxa_finalize_ranges` dlclose-time extension.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libdyld_compat_cxa_finalize_ranges.c -o "$OUT/libdyld/cxa_finalize_ranges_compat.o"

# Real gap (see dyld_compat/vproc_priv.h's own comment): no launchd on
# this port; vproc_swap_integer() only ever asks "am I launchd-managed".
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libdyld_vproc_compat.c -o "$OUT/libdyld/vproc_compat.o"

# Real gap: __cxa_atexit/malloc_size/pthread-fork-child/TLV/mkdirat --
# see libdyld_compat_misc.c's own per-function comments for the full
# real-vs-honest-stub breakdown of each.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libdyld_compat_misc.c -o "$OUT/libdyld/compat_misc.o"

# Real source (third_party/libplatform), simply never added to
# build_libplatform.sh's own file list (that script's own header comment
# only documents skipping src/init.c/atomics/init.c -- os/alloc_once.c
# and string/generic/bzero.c are separate, real, unrelated files it never
# needed until libdyld's own pthread_atfork()/bzero() calls needed them).
# Compiled here as extra objects rather than editing the shared,
# already-widely-depended-on libplatform.a.
PLATFORM_CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -Wno-availability -Wno-deprecated-declarations \
    -D__API_AVAILABLE_PLATFORM_bridgeos(x)=ios,introduced=x \
    -DPRIVATE=1 -DSWITCH_OPTION_OSLOCK_DEPRESS=4 -DSWITCH_OPTION_OSLOCK_WAIT=5 \
    -I $PLATFORM/internal -I $PLATFORM/private -I $PLATFORM/include -I $PLATFORM/src \
    -I $PLATFORM/src/os/resolver \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/libsyscall -I $XNU/bsd \
    -I $OUT/compat_include \
    -idirafter $XNU/osfmk -idirafter $XNU/libkern"
"$CLANG" $PLATFORM_CFLAGS "$PLATFORM/src/os/alloc_once.c" -o "$OUT/libdyld/alloc_once.o"
"$CLANG" $PLATFORM_CFLAGS "$PLATFORM/src/string/generic/bzero.c" -o "$OUT/libdyld/bzero.o"
"$CLANG" $PLATFORM_CFLAGS "$PLATFORM/src/string/generic/memset_pattern.c" -o "$OUT/libdyld/memset_pattern.o"

# Real gap: see libdyld_mach_init_compat.c's own comment. libdyld.dylib's
# own private static copy of mach_init.c (bundled in via libmalloc_scratch.a
# below) never gets its mach_task_self_ initialized otherwise -- the first
# real call into os_alloc_once's mach_vm_map() reads MACH_PORT_NULL and
# fails.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libdyld_mach_init_compat.c -o "$OUT/libdyld/mach_init_compat.o"

echo "libdyld: $ok compiled, $fail gaps (see $OUT/libdyld/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

# Same real fix as build_dyld_link.sh (see that script's own header
# comment for the full story): libcxxabi.a is a shared, general-purpose
# archive compiled WITH exceptions, so linking against it here pulls in
# its real cxa_exception.o/cxa_personality.o/cxa_demangle.o (typeid/
# dynamic_cast/vtable machinery LibSystemHelpers' virtual class needs),
# which in turn want libunwind's real C++ unwinder -- which itself wants
# libunwind_dyld_shim.o's __dyld_find_unwind_sections et al, needing
# __mh_execute_header (never synthesized for a dylib, only MH_EXECUTE).
# UNLIKE dyld itself, libdyld.dylib's own real libdyldGlue.o ALREADY
# provides real, correct, non-circular implementations of all three
# (that's genuinely its job -- serving OTHER images' unwind-section/
# dladdr/dyld queries) -- so here the fix is simply excluding libunwind's
# OWN conflicting copy, no fallback needed.
SCRATCH="$OUT/libdyld_scratch"
rm -rf "$SCRATCH"
mkdir -p "$SCRATCH"
cp "$OUT/libunwind.a" "$SCRATCH/libunwind_scratch.a"
ar d "$SCRATCH/libunwind_scratch.a" libunwind_dyld_shim.o

# Real, separate collision: libmalloc.a's own libmalloc_dispatch.o
# carries stale hollow stand-ins for __platform_memset/
# _dyld_get_image_slide (added back when this project's libmalloc port
# needed SOME definition and neither real libplatform's bzero.o nor real
# dyld/libdyld existed yet). Now that both real, correct implementations
# exist (bzero.o's real __platform_memset above; libdyldGlue.o's real
# _dyld_get_image_slide), hide libmalloc_dispatch.o's stale copies on a
# scratch copy -- never the shared, committed libmalloc.a, which other
# binaries still build against successfully as-is.
mkdir -p "$SCRATCH/extracted"
(cd "$SCRATCH/extracted" && ar x "../../libmalloc.a" libmalloc_dispatch.o)
cat > "$SCRATCH/unexp_malloc_dispatch.txt" << 'EOF'
__platform_memset
__dyld_get_image_slide
EOF
LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
"$LD" -r -arch arm64 -unexported_symbols_list "$SCRATCH/unexp_malloc_dispatch.txt" \
    "$SCRATCH/extracted/libmalloc_dispatch.o" -o "$SCRATCH/libmalloc_dispatch_trimmed.o"
cp "$OUT/libmalloc.a" "$SCRATCH/libmalloc_scratch.a"
ar d "$SCRATCH/libmalloc_scratch.a" libmalloc_dispatch.o
ar r "$SCRATCH/libmalloc_scratch.a" "$SCRATCH/libmalloc_dispatch_trimmed.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libdyld.dylib"
# Real alias/compat set reused verbatim from build_dyld_link.sh -- same
# curated libc stack, same real Apple header-driven UNIX2003/INODE64/
# DARWIN_EXTSN symbol decoration, not guessed twice.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libdyld.dylib \
    -Wl,-fixup_chains \
    -o "$OUT/system/libdyld.dylib" \
    "$OUT/libdyld/libdyldGlue.o" "$OUT/libdyld/LibSystemHelpers.o" \
    "$OUT/libdyld/cxa_finalize_ranges_compat.o" "$OUT/libdyld/vproc_compat.o" \
    "$OUT/libdyld/compat_misc.o" "$OUT/libdyld/alloc_once.o" "$OUT/libdyld/bzero.o" "$OUT/libdyld/memset_pattern.o" \
    "$OUT/libdyld/mach_init_compat.o" \
    "$OUT/libcxx.a" "$OUT/libcxxabi.a" "$SCRATCH/libunwind_scratch.a" \
    "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libcxx_locale_for_stdio_scratch.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" \
    "$OUT/libc_printf.a" "$OUT/libc_string.a" \
    "$OUT/libcxx_tsd_shim_bootstrap_compat.o" \
    "$OUT/libpthread.a" "$OUT/libclosure.a" "$SCRATCH/libmalloc_scratch.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
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

echo "wrote $OUT/system/libdyld.dylib"
otool -hv "$OUT/system/libdyld.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
otool -l "$OUT/system/libdyld.dylib" | grep -A2 "LC_ID_DYLIB" | grep -q "/usr/lib/system/libdyld.dylib" || {
    echo "error: LC_ID_DYLIB is not /usr/lib/system/libdyld.dylib" >&2
    exit 1
}
otool -l "$OUT/system/libdyld.dylib" | grep -q "sectname __dyld4" || {
    echo "error: no __dyld4 section -- dyldMain.cpp's real gDyld lookup (findSectionContent __DATA,__dyld4) will fail" >&2
    exit 1
}
if nm -u "$OUT/system/libdyld.dylib" | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u "$OUT/system/libdyld.dylib" >&2
else
    echo "libdyld.dylib: fully static (beyond itself), zero undefined symbols"
fi
