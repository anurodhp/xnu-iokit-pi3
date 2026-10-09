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
#   libdyld/threadLocalHelpers.s -- the real arm64 _tlv_get_addr fast
#     path for __thread variables. Added 2026-09-09 (DAR-279, found via DAR-270): it used to
#     be on the "deliberately not built" list below, on the reasoning
#     that no binary here declared a __thread variable, with
#     libdyld_compat_misc.c supplying a `return 0;` stand-in for the
#     address-taken-but-never-called case. That reasoning expired --
#     libSystemConfiguration.dylib carries a real one
#     (SCNetworkInterface.c:214, `static __thread
#     CFMutableDictionaryRef S_interface_cache`), and once en0 existed
#     (DAR-243) configd reached it on every launch and died on a null
#     read of the address the stand-in returned. See that ticket and
#     libdyld_compat_misc.c's own history for the full trail.
# Deliberately NOT built: dyld_process_info.cpp/
# dyld_process_info_notify.cpp (external debugger/Instruments remote
# process introspection -- no consumer on this target, and pulls in a
# real separate remote-memory-read API surface this port doesn't need to
# satisfy dyldMain.cpp's two structural checks).
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

# Real, verified fix (2026-08-28, see docs/backlog.md item d9 for the
# full trail and tools/userland_staging/build_simple_cmds.sh's own
# NEWLD_BINDIR header comment for the isolated repro this is based on):
# Xcode 12's own bundled ld64-609 has a real internal crash under
# -Wl,-fixup_chains that a much newer ld does not. Compiling still uses
# Xcode 12's clang/SDK above (untouched -- that vintage match is a
# separate, still-load-bearing invariant); only the final link step's
# linker binary changes, via clang's -B override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
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
# Real static-vs-dylib architecture fix (docs/backlog.md item d8, Stage
# B of the 2026-08-28 static-linking cleanup pass): this dylib used to
# statically bundle the WHOLE of libcopyfile.a/libremovefile.a/
# libresolv.a, each with its own private copy of any process-global
# state (real, mutable `_res`/`_res_9` resolver state in particular --
# same duplicate-global-state bug class already fixed once for
# mach_task_self_/g_zone, see build_libsystem_malloc.sh's own header).
# Real sibling dylibs now exist for all three (build_libresolv_dylib.sh/
# build_copyfile_dylib.sh/build_removefile_dylib.sh) -- REEXPORTED below
# (`-Wl,-reexport_library`, not a plain link), not bundled.
#
# REAL REGRESSION FOUND+FIXED while making this exact change (2026-08-28):
# a plain link (tried first) broke a real, already-documented, deliberate
# reliance elsewhere in this project -- build_libsystem_c.sh's own
# comment (2026-08-28, right above its own `-Wl,-reexport_library,
# "$OUT/system/libdyld.dylib"` line) states outright that `less.macho`
# (this project's one real fsync(2) consumer) "already binds to
# libdyld.dylib" for `_fsync` at real chained-fixups load time --
# because libdyld.dylib used to statically bundle the whole of
# libremovefile.a (which happens to export a real fsync.o), that symbol
# transitively leaked out through libsystem_c.dylib's own reexport of
# libdyld.dylib. A real QEMU boot test after switching to a plain link
# reproduced exactly this break for real (`Symbol not found: _fsync` /
# `_flsl`, `Expected in: /usr/lib/system/libdyld.dylib`, launchd.macho
# and libsystem_c.dylib respectively) -- confirming the reliance was
# real, not hypothetical. `-Wl,-reexport_library` instead of a plain
# link restores this transitively for any existing consumer, while still
# achieving the real goal (one shared copy of resolv/copyfile/removefile
# state instead of three private ones) -- see this same real regression
# class already documented for `_fdopen$UNIX2003` in docs/backlog.md
# item d9 ("any real consumer resolves dylib imports against old
# exports" if not rebuilt after a sibling's exports change).
# Real gap, found 2026-09-13: this loop's own parameterized error message
# (build_${d}_dylib.sh) is never a literal build_X.sh string in the
# source text, so gen_build_order.py's word-list derivation either drops
# the edge entirely (libcopyfile/libremovefile: real producer scripts are
# build_copyfile_dylib.sh/build_removefile_dylib.sh, dropping the "lib"
# prefix -- "build_libcopyfile.sh"/"build_libremovefile.sh" don't exist)
# or silently derives a WRONG one (libresolv: "build_libresolv.sh" DOES
# exist, but builds the unrelated static libresolv.a, not this real
# dylib -- the real producer is build_libresolv_dylib.sh). Named
# individually below with the real, correct script names instead of
# relying on the loop's word list at all.
if [ ! -f "$OUT/system/libcopyfile.dylib" ]; then
    echo "error: $OUT/system/libcopyfile.dylib not found -- run build_copyfile_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libremovefile.dylib" ]; then
    echo "error: $OUT/system/libremovefile.dylib not found -- run build_removefile_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libresolv.dylib" ]; then
    echo "error: $OUT/system/libresolv.dylib not found -- run build_libresolv_dylib.sh first" >&2
    exit 1
fi
# libc_gen.a/libc_string.a/libc_stdio.a/libc_printf.a/
# libcxx_locale_for_stdio_scratch.a/libcxx_tsd_shim_bootstrap_compat.o no
# longer required here (static-to-dylib migration step 4, docs/
# daemon-roadmap.md) -- real libsystem_c.dylib below instead, same real
# source. The old getenv/pthread_tsd_shim collision fix
# libcxx_tsd_shim_bootstrap_compat.o existed for isn't needed either:
# real libsystem_pthread.dylib now provides pthread_key_init_np for
# real via a genuine LC_LOAD_DYLIB dependency.
if [ ! -f "$OUT/system/libsystem_c.dylib" ]; then
    echo "error: $OUT/system/libsystem_c.dylib not found -- run build_libsystem_c.sh first" >&2
    exit 1
fi
# libplatform.a/libclosure.a no longer required here (static-to-dylib
# migration batch 1, docs/daemon-roadmap.md) -- real libsystem_platform.dylib/
# libsystem_blocks.dylib below instead, same real source.
if [ ! -f "$OUT/system/libsystem_platform.dylib" ] || [ ! -f "$OUT/system/libsystem_blocks.dylib" ]; then
    echo "error: $OUT/system/libsystem_platform.dylib / libsystem_blocks.dylib not found -- run build_libsystem_platform.sh and build_libsystem_blocks.sh first" >&2
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

CXX_FLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O2 \
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
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O2 \
    libdyld_compat_cxa_finalize_ranges.c -o "$OUT/libdyld/cxa_finalize_ranges_compat.o"

# Real gap (see dyld_compat/vproc_priv.h's own comment): no launchd on
# this port; vproc_swap_integer() only ever asks "am I launchd-managed".
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O2 \
    libdyld_vproc_compat.c -o "$OUT/libdyld/vproc_compat.o"

# Real gap: __cxa_atexit/malloc_size/pthread-fork-child/mkdirat -- see
# libdyld_compat_misc.c's own per-function comments for the full
# real-vs-honest-stub breakdown of each.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O2 \
    libdyld_compat_misc.c -o "$OUT/libdyld/compat_misc.o"

# Real arm64 __thread / Thread-Local Variable support (DAR-279).
#
# threadLocalHelpers.s is real, unmodified Apple assembly. Its arm64
# _tlv_get_addr is the fast path every __thread access in every image
# funnels through: read the pthread key out of the TLV descriptor, index
# this thread's TSD array straight off TPIDRRO_EL0, and return
# allocation+offset -- falling back to _instantiateTLVs_thunk
# (libdyldGlue.cpp, already built above) the first time a thread touches
# that image's thread-locals.
#
# Nothing else here was missing: dyld's own TLV machinery is already
# real and already runs (DyldRuntimeState.cpp's RuntimeState::initialize
# walks every loaded image with hasThreadLocalVariables() and calls
# setUpTLVs(), which allocates the pthread key and rewrites each
# descriptor's thunk with libSystemHelpers->getTLVGetAddrFunc()).
# getTLVGetAddrFunc() returns &::tlv_get_addr -- so before this file was
# assembled in, that rewrite pointed every descriptor at
# libdyld_compat_misc.c's `return 0;` stand-in, and dyld installed it
# perfectly. That is exactly why the failure looked nothing like a
# missing symbol: the link was clean, nm was clean, dyld's setup ran
# correctly, and the only symptom was a userland null-pointer read one
# instruction after the descriptor thunk returned.
#
# The header include is real Apple source too: threadLocalHelpers.s pulls
# <System/machine/cpu_capabilities.h>, which on a real SDK is xnu's own
# osfmk/machine/cpu_capabilities.h (plus the arm/ variant it chains to)
# installed under usr/local/include/System. Xcode 12's iPhoneOS SDK does
# not ship them, so stage them from the real xnu source this project
# already has rather than hand-writing a stand-in. Only the x86_64 half
# of the file actually reads anything out of them; the arm64 half needs
# the include to resolve and nothing more.
TLVHDR="$OUT/dyld_compat_tlv_hdrs"
rm -rf "$TLVHDR"
mkdir -p "$TLVHDR/System/machine" "$TLVHDR/System/arm"
cp "$XNU/osfmk/machine/cpu_capabilities.h" "$TLVHDR/System/machine/cpu_capabilities.h"
cp "$XNU/osfmk/arm/cpu_capabilities.h" "$TLVHDR/System/arm/cpu_capabilities.h"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -DPRIVATE=1 -I "$TLVHDR" \
    "$DYLD/libdyld/threadLocalHelpers.s" -o "$OUT/libdyld/threadLocalHelpers.o"
# Guard the one thing that would silently undo this: if _tlv_get_addr
# ever stops being the real assembly (a stub sneaking back in, a
# preprocessor branch excluding the arm64 block) the object still
# assembles and still links. The real one is the ONLY version that reads
# the thread pointer.
otool -tV "$OUT/libdyld/threadLocalHelpers.o" | grep -q "TPIDRRO_EL0" || {
    echo "error: _tlv_get_addr in threadLocalHelpers.o does not read TPIDRRO_EL0 --" >&2
    echo "       the real arm64 fast path did not get assembled (DAR-279)" >&2
    exit 1
}

# os/alloc_once.c, string/generic/bzero.c, string/generic/memset_pattern.c
# used to be compiled here as loose private objects (real source, simply
# never in build_libplatform.sh's own file list) -- now provided by real
# libsystem_platform.dylib instead (build_libsystem_platform.sh, static-
# to-dylib migration batch 1, docs/daemon-roadmap.md), linked below.

echo "libdyld: $ok compiled, $fail gaps (see $OUT/libdyld/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

# libcxx.a/libcxxabi.a/libunwind.a and the whole libunwind_dyld_shim.o
# exclusion dance that used to live here are gone as of static-to-dylib
# migration step 5 (docs/daemon-roadmap.md): real libc++.dylib below
# instead, which already resolves its OWN equivalent
# __dyld_find_unwind_sections/dladdr collision internally (see
# build_libcxx_dylib.sh's own comment) -- nothing left to exclude here.
if [ ! -f "$OUT/system/libc++.dylib" ]; then
    echo "error: $OUT/system/libc++.dylib not found -- run build_libcxx_dylib.sh first" >&2
    exit 1
fi

# libmalloc.a/libpthread.a and the whole libmalloc_dispatch.o
# trim-and-rebuild dance that used to live here are gone as of static-
# to-dylib migration step 3 (docs/daemon-roadmap.md): real
# libsystem_malloc.dylib/libsystem_pthread.dylib below instead. That
# trim dance existed only to resolve a STATIC-ARCHIVE-flattening
# collision (libmalloc_dispatch.o's stale __platform_memset/
# _dyld_get_image_slide stand-ins vs the real implementations linked
# alongside it in the same flat archive) -- real dylibs don't have this
# problem at all (two-level namespace symbol resolution is bound per
# reference at each CONSUMER's own link time, not flattened into one
# shared symbol table the way `ar`-combined archives work), so there is
# nothing left to trim.
if [ ! -f "$OUT/system/libsystem_kernel.dylib" ] || [ ! -f "$OUT/system/libsystem_malloc.dylib" ] || [ ! -f "$OUT/system/libsystem_pthread.dylib" ]; then
    echo "error: libsystem_kernel.dylib / libsystem_malloc.dylib / libsystem_pthread.dylib not found -- run build_libsystem_kernel.sh, build_libsystem_malloc.sh, and build_libsystem_pthread.sh first" >&2
    exit 1
fi

mkdir -p "$OUT/system"
rm -f "$OUT/system/libdyld.dylib"
# Real alias/compat set reused verbatim from build_dyld_link.sh -- same
# curated libc stack, same real Apple header-driven UNIX2003/INODE64/
# DARWIN_EXTSN symbol decoration, not guessed twice.
    # -not_for_dyld_shared_cache: real, newly-surfaced requirement of
    # the daily-driver Xcode's newer ld (NEWLD_BINDIR above) -- it
    # refuses "-undefined dynamic_lookup"/-U on a dylib it classifies
    # as shared-cache eligible (real /usr/lib install path) unless this
    # is set, a real check Xcode 12's ld64-609 never enforced. This
    # project builds no real dyld shared cache at all, so opting every
    # real dylib out is correct, not a workaround (docs/backlog.md item
    # d9's NEWLD_BINDIR sweep, 2026-08-28).
    #
    # -unexported_symbol,dyld_stub_binder: threadLocalHelpers.s also
    # defines dyld_stub_binder (`b __dyld_missing_symbol_abort`) -- real
    # dyld-940 keeps it only so binaries built for classic lazy binding
    # still LINK; calling it aborts. Real Apple exports it from
    # libdyld.dylib; this port deliberately does not, because this
    # port's libdyld never has, and making it appear would convert a
    # loud link-time "symbol dyld_stub_binder not found" (which
    # build_libsystemconfiguration_client.sh has already hit for real
    # and worked around) into a binary that links clean and aborts at
    # first call. On a port with no lazy binding at all, the link-time
    # error is the better failure. Hiding it is scoped to this one
    # symbol and changes nothing about the TLV fix above; flip it
    # deliberately if a real consumer ever needs Apple's exact export
    # set.
    #
    # fsync$UNIX2003/reallocarray$DARWIN_EXTSN aliases REMOVED 2026-08-29:
    # real bug -- their base (_fsync/_reallocarray) stopped being local
    # to THIS link once libcopyfile/libremovefile/libresolv switched from
    # a plain archive link to -Wl,-reexport_library above (Stage B of the
    # static-linking cleanup pass), which broke the alias (real launchd
    # boot failure: "re-export ordinal 0 ... out of range"). Real, working
    # replacements now live in build_removefile_dylib.sh itself, where
    # the base genuinely is local -- resolved transitively through this
    # dylib's own reexport of libremovefile.dylib, no alias needed here.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libdyld.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/system/libdyld.dylib" \
    -Wl,-unexported_symbol,dyld_stub_binder \
    "$OUT/libdyld/libdyldGlue.o" "$OUT/libdyld/LibSystemHelpers.o" \
    "$OUT/libdyld/cxa_finalize_ranges_compat.o" "$OUT/libdyld/vproc_compat.o" \
    "$OUT/libdyld/compat_misc.o" "$OUT/libdyld/threadLocalHelpers.o" \
    "$OUT/system/libsystem_kernel.dylib" "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_pthread.dylib" "$OUT/system/libsystem_c.dylib" \
    "$OUT/system/libc++.dylib" \
    -Wl,-reexport_library,"$OUT/system/libcopyfile.dylib" \
    -Wl,-reexport_library,"$OUT/system/libremovefile.dylib" \
    -Wl,-reexport_library,"$OUT/system/libresolv.dylib" \
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
    -Wl,-alias,_read,'_read$UNIX2003' \
    -Wl,-alias,'_mmap$UNIX2003',_mmap \
    -Wl,-alias,'_munmap$UNIX2003',_munmap \
    -Wl,-alias,_pthread_testcancel,'_pthread_testcancel$UNIX2003'
    # __pthread_cond_wait alias removed 2026-08-28 (see docs/backlog.md
    # item d9's NEWLD_BINDIR sweep): not local to this link either, same
    # "silently dead under ld64-609" class as build_libsystem_c.sh's own
    # matching fix -- plain _pthread_cond_wait is already a real, direct
    # export of the sibling libsystem_pthread.dylib this link already
    # includes, no alias needed.

echo "wrote $OUT/system/libdyld.dylib"
# libunwind.dylib links against libdyld_for_libunwind.tbd, a link-time
# stub, because it has to be built before this dylib (DAR-236). Check that
# the real libdyld still exports what the stub promises; otherwise
# libunwind would link clean and abort at load.
for sym in __dyld_find_unwind_sections __dyld_register_func_for_remove_image _dladdr; do
    nm -gUj "$OUT/system/libdyld.dylib" | grep -qx "$sym" || {
        echo "error: libdyld.dylib no longer exports $sym, which libdyld_for_libunwind.tbd promises libunwind.dylib" >&2
        exit 1
    }
done
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
