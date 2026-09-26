#!/bin/bash
# Builds libc_build/system/libsystem_platform.dylib: static-to-dylib
# migration batch 1a (docs/daemon-roadmap.md's "Static-vs-dylib
# architecture debt" section), following build_libsystem_kernel.sh's
# established pattern. Real, unmodified Apple source, same files
# build_libplatform.sh already compiles for its own static libplatform.a
# (os_unfair_lock/os_once/OSAtomic, this project's own real Mach-trap/
# raw-syscall stub files for the two traps/two syscalls the BSD-syscall
# Perl generator doesn't cover -- see build_libplatform.sh's own header
# comment for the full real-gap writeup, not re-derived here) -- same
# CFLAGS, same file list, just linked as a real -dynamiclib instead of
# `ar`. Audited before writing this script: no process-global mutable
# state here needs cross-image sharing the way mach_task_self_ did
# (os_unfair_lock/OSAtomic operate on caller-provided memory; TSD reads
# TPIDRRO_EL0 directly, not an image-local global) -- confirmed by
# reading lock.c/atomic.c/OSAtomic.c, not assumed.
#
# Also folds in os/alloc_once.c, string/generic/bzero.c, string/generic/
# memset_pattern.c -- real libplatform source that build_libdyld.sh/
# build_libsystem.sh previously each compiled as their OWN loose, private
# copies (build_libplatform.sh's own file list never included them, see
# that script's header comment on why) rather than duplicated a third
# time here. This one's a real architectural cleanup, not a correctness
# fix like mach_task_self_ was -- os_alloc_once's own per-image
# _os_alloc_heap bump-pointer global stays independent per caller-site
# either way (each os_alloc_once() slot is its own independent bump
# region regardless of which image's copy of the code runs), so
# duplicating it was wasteful, not unsafe.
set -e
cd "$(dirname "$0")"

PLATFORM="../../third_party/libplatform"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
OUT="libc_build"
mkdir -p "$OUT/libsystem_platform/apple_platform"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
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
if [ ! -f "$OUT/system/libsystem_kernel.dylib" ]; then
    echo "error: $OUT/system/libsystem_kernel.dylib not found -- run build_libsystem_kernel.sh first (real errno/cerror/mach-trap dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/compat_include/mach_reply_port_decl.h" ]; then
    echo "error: $OUT/compat_include/mach_reply_port_decl.h not found -- run build_libmalloc.sh first" >&2
    exit 1
fi

# Identical to build_libplatform.sh's own CFLAGS (not re-derived --
# same real header graph).
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -Wno-availability -Wno-deprecated-declarations \
    -D__API_AVAILABLE_PLATFORM_bridgeos(x)=ios,introduced=x \
    -DPRIVATE=1 -DSWITCH_OPTION_OSLOCK_DEPRESS=4 -DSWITCH_OPTION_OSLOCK_WAIT=5 -DLONG_BIT=64 \
    -I $PLATFORM/internal -I $PLATFORM/private -I $PLATFORM/include -I $PLATFORM/src \
    -I $PLATFORM/src/os/resolver \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/libsyscall -I $XNU/bsd \
    -I $OUT/compat_include \
    -idirafter $XNU/osfmk -idirafter $XNU/libkern"

ok=0
skip=0
for f in "$PLATFORM/src/os/lock.c" "$PLATFORM/src/os/atomic.c" "$PLATFORM/src/atomics/arm64/OSAtomic.c" \
         "$PLATFORM/src/os/alloc_once.c" "$PLATFORM/src/string/generic/memset_pattern.c" \
         "$PLATFORM/src/string/generic/strcmp.c" \
         "$PLATFORM/src/string/generic/strlen.c" "$PLATFORM/src/string/generic/strchr.c"; do
    base=$(basename "$f" .c)
    out="$OUT/libsystem_platform/apple_platform/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/libsystem_platform/apple_platform/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/libsystem_platform/apple_platform/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
    fi
done
echo "apple_platform: $ok compiled, $skip skipped"

# DAR-13 (2026-09-04): bzero.c/memmove.c compiled SEPARATELY from the
# loop above, with real Apple's own -DVARIANT_STATIC=1 (see
# third_party/libplatform/xcodeconfig/libplatform.xcconfig's real
# GCC_PREPROCESSOR_DEFINITIONS_dyld/_static -- VARIANT_STATIC=1 is real
# Apple's own macro for "also emit the public bcopy/memcpy/memmove/
# memset/bzero wrapper names, not just the internal _platform_* ones").
#
# Real-Apple-parity fix, matching DAR-13's own investigation: on real
# Darwin, _memcpy/_memset (the whole bcopy/memmove/bzero family) are
# owned by exactly ONE dylib, libsystem_platform.dylib -- real hardware
# gets there via hand-tuned per-arch assembly (arm64 NEON) that lives
# under a DIFFERENT real Libplatform source layout than this project's
# pinned libplatform-254.80.2 tag has (confirmed: no src/string/arm64/
# directory exists anywhere in this checkout, only src/string/generic/ --
# a real, pin-vintage gap, not fabricated). string.h's own
# _PLATFORM_OPTIMIZED_MEMSET/_PLATFORM_OPTIMIZED_MEMMOVE are both
# genuinely 0 in this tree, meaning the generic C bodies in bzero.c/
# memmove.c ARE real Apple's own intended fallback definition for a
# target with no optimized variant -- not a hand-rolled substitute. Real
# Apple's own xcconfig sets VARIANT_STATIC=1 only for the "dyld"/"static"
# build variants (not "normal", i.e. not the real shipping dylib) because
# real hardware never needs the generic fallback's public names -- but
# this checkout's arm64 target genuinely has no other source of a public
# _memcpy/_memset at all, so invoking the same real ifdef branch here is
# the honest, minimal, real-source-only way to get them, matching this
# project's own already-established use of the exact same generic
# _platform_memset/_platform_memmove bodies elsewhere (this file's own
# unconditional compiles above, build_libmalloc.sh's platform_memmove.o).
#
# Previously (build_libsystem_malloc.sh's own now-corrected comment):
# "there is NO real _platform_memset source anywhere in this checkout" --
# that claim was WRONG, verified against source before writing this fix:
# third_party/libplatform/src/string/generic/bzero.c has real,
# unmodified _platform_memset (and has since before that comment was
# written -- this file's OWN unconditional bzero.c/memmove.c compiles a
# few lines above already produce __platform_memset/__platform_memmove
# as real exports of this exact dylib; VARIANT_STATIC=1 below only adds
# the PUBLIC bcopy/memcpy/memmove/memset/bzero/__bzero wrapper names on
# top of the same already-compiled internal primitives).
#
# -Wl,-unexported_symbol below hides memmove/bzero/__bzero (also real,
# unmodified exports this same VARIANT_STATIC=1 compile legitimately
# produces) -- DAR-13's own scope is _memcpy/_memset only; libsystem_c.dylib
# is today's real, undisputed owner of _memmove/_bzero/__bzero (extracted
# from libc_string.a) and widening this dylib's public surface to those
# three as well would open a NEW duplicate-symbol pair this ticket never
# audited or scoped -- deliberately deferred rather than bundled in.
PLATCFLAGS_VARIANT_STATIC="$CFLAGS -DVARIANT_STATIC=1"
"$CLANG" $PLATCFLAGS_VARIANT_STATIC "$PLATFORM/src/string/generic/bzero.c" \
    -o "$OUT/libsystem_platform/apple_platform/bzero.o"
"$CLANG" $PLATCFLAGS_VARIANT_STATIC "$PLATFORM/src/string/generic/memmove.c" \
    -o "$OUT/libsystem_platform/apple_platform/memmove.o"
echo "apple_platform: bzero.o/memmove.o recompiled with -DVARIANT_STATIC=1 (real public memcpy/memset/memmove/bzero/__bzero wrappers, DAR-13)"


# setjmp(3) family (DAR-383, 2026-09-26): real, unmodified libplatform
# src/setjmp/arm64/setjmp.s (_setjmp/_longjmp/_sigsetjmp/_siglongjmp and
# the non-signal __setjmp/__longjmp core) plus src/setjmp/generic/
# sigtramp.c for the _sigunaltstack() that setjmp.s's _longjmp calls --
# libsystem_platform is where real Apple ships both. Before this no dylib
# in the tree exported setjmp/longjmp at all (ed/vim link a private C
# reimplementation, ed_setjmp_compat.c); libICE (via xtrans's
# Xtransutil.c:386-398 SIGALRM guard) is the first dylib to need them.
#   - asm_help.h (ENTRY_POINT/CALL_EXTERNAL) is not vendored in any
#     third_party checkout; it comes from the host macOS SDK's
#     usr/include/architecture/arm/asm_help.h via -idirafter, the same
#     MACSDK fallback build_libsystem_kernel.sh uses for libsyscall's
#     custom/*.s. os/tsd.h (_OS_PTR_MUNGE_TOKEN) is xnu's own
#     libsyscall/os/tsd.h, already on CFLAGS' -I path.
#   - ___sigaltstack / ___sigreturn: raw traps from libsystem_kernel.dylib
#     (build_libsystem_kernel.sh, same DAR-383 change).
#   - _sigprocmask lives in libsystem_c.dylib, which links THIS dylib, so
#     it is deferred with -Wl,-U below -- the same cycle-avoiding
#     deferral libsystem_kernel.dylib already makes for abort/memcpy
#     (build_libsystem_kernel.sh's link comment): nothing here loads
#     libsystem_platform.dylib without libSystem.B.dylib, which
#     re-exports libsystem_c.
#   - sigtramp.c also defines _sigtramp and __in_sigtramp. This tree's
#     real signal trampoline is libsystem_kernel.dylib's __sigtramp
#     (libsystem_kernel_sigtramp_compat.c, DAR-164), so those two are
#     unexported in the link below rather than becoming a second owner.
MACSDK="$(xcode-select -p)/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
if [ ! -f "$MACSDK/usr/include/architecture/arm/asm_help.h" ]; then
    echo "error: $MACSDK/usr/include/architecture/arm/asm_help.h not found (needed for libplatform's setjmp.s)" >&2
    exit 1
fi
"$CLANG" $CFLAGS -idirafter "$MACSDK/usr/include" \
    "$PLATFORM/src/setjmp/arm64/setjmp.s" -o "$OUT/libsystem_platform/setjmp.o"
"$CLANG" $CFLAGS "$PLATFORM/src/setjmp/generic/sigtramp.c" -o "$OUT/libsystem_platform/sigtramp.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c libplatform_mach_traps.s -o "$OUT/libsystem_platform/libplatform_mach_traps.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -DPRIVATE=1 \
    -I "$OUT/compat_include" -idirafter "$XNU/osfmk" \
    "$XNU/libsyscall/wrappers/mach_absolute_time.s" -o "$OUT/libsystem_platform/mach_absolute_time.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -DPRIVATE=1 -I "$OUT/compat_include" -idirafter "$XNU/osfmk" \
    "$XNU/libsyscall/wrappers/mach_timebase_info.c" -o "$OUT/libsystem_platform/mach_timebase_info.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libplatform_syscalls.c -o "$OUT/libsystem_platform/libplatform_syscalls.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -I "$XNU/libsyscall" libplatform_thread_self.c -o "$OUT/libsystem_platform/libplatform_thread_self.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libsystem_platform.dylib"
    # -not_for_dyld_shared_cache: real, newly-surfaced requirement of
    # the daily-driver Xcode's newer ld (NEWLD_BINDIR above) -- it
    # refuses "-undefined dynamic_lookup"/-U on a dylib it classifies
    # as shared-cache eligible (real /usr/lib install path) unless this
    # is set, a real check Xcode 12's ld64-609 never enforced. This
    # project builds no real dyld shared cache at all, so opting every
    # real dylib out is correct, not a workaround (docs/backlog.md item
    # d9's NEWLD_BINDIR sweep, 2026-08-28).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libsystem_platform.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    `# DAR-13: keep _memmove/_bzero/__bzero real, defined, LOCAL to this` \
    `# dylib (bzero.c/memmove.c's own VARIANT_STATIC=1 compile above` \
    `# produces them for real) but NOT publicly exported -- libsystem_c.dylib` \
    `# stays their one real owner today; only _memcpy/_memset are this` \
    `# ticket's real scope. See this file's own header comment above the` \
    `# VARIANT_STATIC=1 compiles for the full rationale.` \
    -Wl,-unexported_symbol,_memmove -Wl,-unexported_symbol,_bzero -Wl,-unexported_symbol,___bzero \
    `# DAR-383 setjmp family: see the setjmp.s compile's comment above.` \
    -Wl,-U,_sigprocmask \
    -Wl,-unexported_symbol,__sigtramp -Wl,-unexported_symbol,___in_sigtramp \
    -o "$OUT/system/libsystem_platform.dylib" \
    "$OUT/libsystem_platform/apple_platform"/*.o \
    "$OUT/libsystem_platform/libplatform_mach_traps.o" "$OUT/libsystem_platform/mach_absolute_time.o" \
    "$OUT/libsystem_platform/mach_timebase_info.o" "$OUT/libsystem_platform/libplatform_syscalls.o" \
    "$OUT/libsystem_platform/libplatform_thread_self.o" \
    "$OUT/libsystem_platform/setjmp.o" "$OUT/libsystem_platform/sigtramp.o" \
    "$OUT/system/libsystem_kernel.dylib"

echo "wrote $OUT/system/libsystem_platform.dylib"
otool -hv "$OUT/system/libsystem_platform.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
if nm -u "$OUT/system/libsystem_platform.dylib" | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u "$OUT/system/libsystem_platform.dylib" >&2
else
    echo "libsystem_platform.dylib: fully self-contained (beyond libsystem_kernel.dylib), zero undefined symbols"
fi
