#!/bin/bash
# Builds libc_build/system/libsystem_malloc.dylib: static-to-dylib
# migration batch 2a (docs/daemon-roadmap.md's "Static-vs-dylib
# architecture debt" section).
#
# THIS ONE IS A REAL CORRECTNESS FIX, NOT JUST ARCHITECTURE CLEANUP.
# Audited before writing this script (same duplicate-global-state check
# already caught mach_task_self_ and errno): libmalloc_dispatch.c's
# `static malloc_zone_t *g_zone` is the live heap zone pointer every
# malloc/free/realloc call in this project's port goes through
# (create_scalable_zone() is only ever called once, lazily, the first
# time get_zone() sees g_zone == NULL). Until this script, libdyld.dylib
# and libSystem.B.dylib EACH bundled their own private static copy of
# the entire magazine allocator, including their own separate g_zone --
# meaning any pointer malloc'd via code executing in one dylib and
# free'd via code executing in the other would be operating on a
# completely different heap's metadata. That is real, silent heap
# corruption territory (not merely "wrong value" the way a duplicated
# errno is) -- the most serious instance of this bug class found so far
# in this migration. This single, real, shared libsystem_malloc.dylib
# (real Apple's own architecture) closes it for good: one process, one
# zone, matching every other real macOS process.
#
# Real, unmodified Apple source: same file set build_libmalloc.sh
# already established for static libmalloc.a (its own header comment has
# the full scope/gap writeup, not re-derived here) -- reused by
# extracting the already-built, already-verified-correct member objects
# out of libc_build/libmalloc.a directly (ar x) rather than recompiling
# from scratch a second time, EXCLUDING the 10 objects that now live in
# separate, real dylibs instead (6 in libsystem_kernel.dylib as of
# migration step 1, 4 generic string primitives in
# libsystem_platform.dylib as of this step -- see that script's own
# comment). The remaining ~25 objects (the actual magazine allocator:
# bitarray/magazine_*/vm/malloc_printf, plus this project's own
# libmalloc_dispatch.c/libmalloc_sleep_shim.c, plus real Apple
# fortify-source memcpy_chk/memset_chk/chk_fail + the real FreeBSD
# memcpy/memset primitives, plus pthread_tsd_shim.o and a handful of
# real BSD syscall stubs build_libmalloc.sh generated for its own
# private use) have no comparable process-global state of their own
# (per-zone metadata all hangs off g_zone itself, which every one of
# these objects reaches through the same single copy once linked into
# one dylib) -- carried over unchanged.
#
# Unlike the earlier libmalloc_dispatch.o exclusion dance
# build_libdyld.sh/build_libsystem.sh used to need (hiding its stale
# __platform_memset/_dyld_get_image_slide stand-ins behind a real
# implementation linked into the SAME flat static archive): that trick
# doesn't apply here. Real dylibs resolve symbols via two-level
# namespace, bound per-reference at the CONSUMER's link time, not a
# flattened single symbol table the way `ar`-combined static archives
# work -- so libmalloc_dispatch.o's two stand-ins living inside this
# dylib don't collide with libsystem_platform.dylib's real
# __platform_memset or libdyld.dylib's own real _dyld_get_image_slide;
# nothing outside this dylib ever binds to this dylib's copies of either
# symbol.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
mkdir -p "$OUT/libsystem_malloc/extracted"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/libmalloc.a" ]; then
    echo "error: $OUT/libmalloc.a not found -- run build_libmalloc.sh first (this script reuses its already-built member objects)" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libsystem_kernel.dylib" ] || [ ! -f "$OUT/system/libsystem_platform.dylib" ]; then
    echo "error: libsystem_kernel.dylib / libsystem_platform.dylib not found -- run build_libsystem_kernel.sh and build_libsystem_platform.sh first" >&2
    exit 1
fi

MEMBERS="bitarray.o magazine_large.o magazine_malloc.o magazine_medium.o magazine_rack.o \
    magazine_small.o magazine_tiny.o malloc_printf.o vm.o simple_string_io.o \
    memcpy_chk.o memset_chk.o chk_fail.o memcpy.o memset.o \
    libmalloc_sleep_shim.o libmalloc_dispatch.o pthread_tsd_shim.o \
    sys_getpid.o sys_mprotect.o sys_kill.o sys_write.o sys_madvise.o sys_kdebug_trace.o \
    sys_exit.o sys_semwait_signal.o"
rm -rf "$OUT/libsystem_malloc/extracted"
mkdir -p "$OUT/libsystem_malloc/extracted"
(cd "$OUT/libsystem_malloc/extracted" && ar x "../../libmalloc.a" $MEMBERS)

mkdir -p "$OUT/system"
rm -f "$OUT/system/libsystem_malloc.dylib"
# Real UNIX2003/cancelable-variant symbol aliases the magazine
# allocator's own real source calls by these exact names -- same real
# aliases build_libdyld.sh/build_libsystem.sh's own link lines already
# apply at the outer dylib (needed there too, for everything else that
# calls these), now also needed HERE since these particular raw syscall
# stubs (sys_kill.o/sys_write.o/sys_mprotect.o/sys_kdebug_trace.o/
# libmalloc_sleep_shim.o, all in MEMBERS above) live inside this dylib,
# not the outer one, and real dylib symbol resolution doesn't flatten
# across image boundaries the way static-archive linking did.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libsystem_malloc.dylib \
    -Wl,-fixup_chains \
    -Wl,-U,_getenv -Wl,-U,_getprogname -Wl,-U,_isatty \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,___kill,'_kill$UNIX2003' \
    -Wl,-alias,___kdebug_trace,_kdebug_trace \
    -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
    -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
    -Wl,-alias,_write,'_write$UNIX2003' \
    -o "$OUT/system/libsystem_malloc.dylib" \
    $(for m in $MEMBERS; do echo "$OUT/libsystem_malloc/extracted/$m"; done) \
    "$OUT/system/libsystem_kernel.dylib" "$OUT/system/libsystem_platform.dylib"

echo "wrote $OUT/system/libsystem_malloc.dylib"
otool -hv "$OUT/system/libsystem_malloc.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
if nm -u "$OUT/system/libsystem_malloc.dylib" | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u "$OUT/system/libsystem_malloc.dylib" >&2
else
    echo "libsystem_malloc.dylib: fully self-contained (beyond libsystem_kernel.dylib/libsystem_platform.dylib), zero undefined symbols"
fi
