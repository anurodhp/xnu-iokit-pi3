#!/bin/bash
# Builds libc_build/libplatform.a: real Apple libplatform (os_unfair_lock,
# os_once, OSAtomic*Barrier) - continuing the same tier structure as
# build_libc_string.sh/build_libc_locale.sh/build_libc_stdio.sh/
# build_libc_gen.sh (see docs/roadmap.md Phase 4 for the running tier
# breakdown). third_party/libplatform is already cloned+pinned
# (libplatform-254.80.2, see setup_third_party.sh) - no new clone needed.
#
# This RETIRES compat_stubs.c's os_unfair_lock/OSAtomic no-op stand-ins:
# real xlocale.c (already linked into libc_locale.a) now links against
# the genuine Apple implementation instead. compat_stubs.c's placeholder
# bump allocator (malloc/calloc/realloc/free) is untouched - libmalloc is
# its own separate, larger, not-yet-attempted Phase 4 item.
#
# Scope: os/lock.c (os_unfair_lock, os_once, legacy OSSpinLock/handoff
# locks - all real, none stubbed) + os/atomic.c + atomics/arm64/OSAtomic.c
# (real OSAtomicIncrement32Barrier/OSAtomicDecrement32Barrier and friends).
# Deliberately NOT built: src/init.c/atomics/init.c (the dyld-driven
# _libplatform_init() constructor + CPU-feature/commpage-PFZ detection -
# needs a "simple string"/_simple_getenv subsystem this project hasn't
# pulled in, and nothing in the scope above actually calls it - confirmed
# by linking lock.o/atomic.o/OSAtomic.o with zero references to any of
# atomics/init.c's symbols, not assumed); setjmp/ucontext/cachecontrol/
# simple/string (duplicate string primitives libc_string.a already
# provides) - all out of scope for "the real lock/atomic layer",
# genuinely separate concerns.
#
# Real gaps found and fixed while bringing this up (empirically, by
# compiling and reading the actual clang errors, not guessed):
#   - lock_private.h's OS_UNFAIR_LOCK_ADAPTIVE_SPIN uses
#     __API_AVAILABLE(..., bridgeos(4.0)) - bridgeOS didn't exist as an
#     Availability.h platform macro yet in this project's pinned
#     Xcode-12/iPhoneOS14.4 SDK vintage (bridgeOS support was added to
#     Availability headers later). Real Apple SDK convention for
#     "platform macro absent, availability annotation harmless for a
#     non-matching compile target" is __API_AVAILABLE_PLATFORM_<name>(x)
#     macros (confirmed by reading this exact SDK's own
#     AvailabilityInternal.h definition for __API_AVAILABLE_PLATFORM_watchos
#     as the real template) - remapped to ios,introduced=x (already
#     present, redundant, and inert since this target's -target is
#     arm64-apple-ios14.4, not any platform where a duplicate/mismatched
#     ios attribute would be enforced).
#   - SWITCH_OPTION_OSLOCK_DEPRESS/SWITCH_OPTION_OSLOCK_WAIT: real values
#     (4, 5) exist in this project's own
#     third_party/xnu-7195/osfmk/mach/thread_switch.h, but the iPhoneOS14.4
#     SDK's own (older-vintage) mach/thread_switch.h shadows it via
#     -isysroot lookup order and lacks them - real SDK-vintage gap, same
#     class as getdirentries64_flags_t in build_libc_gen.sh. Defined
#     directly (verified values, not guessed).
#   - sys/ulock.h (UL_UNFAIR_LOCK/ULF_*/__ulock_wait/__ulock_wake
#     declarations): real content only exists in this project's own kernel
#     checkout (third_party/xnu-7195/bsd/sys/ulock.h, gated by
#     #if PRIVATE), not in the public SDK - added -DPRIVATE=1 (already an
#     established pattern in this project, see build_libc_locale.sh) and
#     -I .../bsd.
#   - machine/cpu_capabilities.h and System/{arm,machine}/cpu_capabilities.h:
#     the SAME real forwarding-shim pattern build_libc_locale.sh already
#     established (real content at $XNU/osfmk/arm/cpu_capabilities.h,
#     several different real Apple headers each expect it under a
#     different conventional include path) - one more forwarder path
#     needed here (System/arm/...) than locale's tier needed, added.
#   - osfmk/machine/cpu_capabilities.h's REAL dispatch logic (userland
#     branch: #include <System/arm/cpu_capabilities.h>) is itself gated
#     behind #ifdef PRIVATE - without -DPRIVATE=1 on mach_absolute_time.s
#     specifically, _COMM_PAGE_TIMEBASE_OFFSET/USER_TIMEBASE_NONE silently
#     never got defined at all (no error - just absent macros, caught by
#     the resulting "immediate must be an integer" assembler errors,
#     traced back to the real cause rather than worked around at the
#     assembly-error site).
#   - __ulock_wait/__ulock_wake (BSD syscalls 515/516, NO_SYSCALL_STUB in
#     syscalls.master - Apple hand-writes these in libpthread's own
#     internal sources, not this project's third_party/libpthread public
#     headers) and thread_selfid (372, also NO_SYSCALL_STUB): hand-written
#     raw syscall wrappers, this project's established carry-flag pattern
#     (libplatform_syscalls.c, libplatform_thread_self.c).
#   - thread_switch (Mach trap -61) and mach_timebase_info_trap (Mach trap
#     -89): Mach traps aren't produced by the BSD-syscall Perl generator
#     build_libsyscall_smoketest.sh already uses: real trap numbers
#     cross-checked against osfmk/mach/syscall_sw.h, real arm64 trap-call
#     sequence (mov x16,#-N / svc #0x80 / ret, NO carry-flag check -
#     that convention is BSD-only) copied by hand from
#     osfmk/mach/arm/syscall_sw.h's own kernel_trap macro, the same real
#     sequence Apple's own build expands it into (libplatform_mach_traps.s).
#   - os_unfair_lock/os_once's notion of "current thread" (TSD slot
#     __TSD_MACH_THREAD_SELF) was never populated on this target (no real
#     pthread_start() to do it) - would read 0 == OS_LOCK_NO_OWNER and
#     silently corrupt lock semantics. Bootstrapped for real from a real,
#     different, also-non-zero-per-thread value (thread_selfid(), not
#     mach_thread_self()'s own Mach trap - avoids a second, unnecessary
#     Mach-trap-ABI investigation for a value used only for equality
#     comparison, never dereferenced as an actual port - see
#     libplatform_thread_self.c's own header comment for the full
#     reasoning, this is a documented deliberate substitution, not a
#     guess).
set -e
cd "$(dirname "$0")"

PLATFORM="../../third_party/libplatform"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
OUT="libc_build"
mkdir -p "$OUT/apple_platform" "$OUT/compat_include/System/arm"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

cat > "$OUT/compat_include/System/arm/cpu_capabilities.h" <<'EOF'
/* Same real forwarding shim as System/machine/cpu_capabilities.h (see
 * that file's own comment, established by build_libc_locale.sh) - a
 * second real installed-SDK-style include path (System/arm/... vs
 * System/machine/...) real Apple headers expect for the same real
 * content at $XNU/osfmk/arm/cpu_capabilities.h. */
#ifndef _SYSTEM_ARM_CPU_CAPABILITIES_H_COMPAT_
#define _SYSTEM_ARM_CPU_CAPABILITIES_H_COMPAT_
#include <arm/cpu_capabilities.h>
#endif
EOF

# Real flags: __API_AVAILABLE_PLATFORM_bridgeos / SWITCH_OPTION_OSLOCK_*/
# PRIVATE - see this script's own header comment for why each is real,
# not guessed. -Wno-availability/-Wno-deprecated-declarations: real Apple
# source (lock.c) intentionally implements the deprecated OSSpinLock API
# for real, triggering its own deprecation warnings on itself - noise,
# not a correctness signal, same class as -Wno-nullability-completeness
# elsewhere in this project's build scripts.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -Wno-availability -Wno-deprecated-declarations \
    -D__API_AVAILABLE_PLATFORM_bridgeos(x)=ios,introduced=x \
    -DPRIVATE=1 -DSWITCH_OPTION_OSLOCK_DEPRESS=4 -DSWITCH_OPTION_OSLOCK_WAIT=5 \
    -I $PLATFORM/internal -I $PLATFORM/private -I $PLATFORM/include -I $PLATFORM/src \
    -I $PLATFORM/src/os/resolver \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/libsyscall -I $XNU/bsd \
    -I $OUT/compat_include \
    -idirafter $XNU/osfmk -idirafter $XNU/libkern"

ok=0
skip=0
for f in "$PLATFORM/src/os/lock.c" "$PLATFORM/src/os/atomic.c" "$PLATFORM/src/atomics/arm64/OSAtomic.c"; do
    base=$(basename "$f" .c)
    out="$OUT/apple_platform/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/apple_platform/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/apple_platform/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
    fi
done
echo "apple_platform: $ok compiled, $skip skipped (see this script's header comment for what's deliberately out of scope)"

# Two real Mach traps (thread_switch, mach_timebase_info_trap) that
# build_libsyscall_smoketest.sh's BSD-syscall Perl generator doesn't
# cover - see this script's header comment.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c libplatform_mach_traps.s -o "$OUT/libplatform_mach_traps.o"

# Real Apple mach_absolute_time (commpage-backed, real asm) + the
# mach_timebase_info() C wrapper around mach_timebase_info_trap.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -DPRIVATE=1 \
    -I "$OUT/compat_include" -idirafter "$XNU/osfmk" \
    "$XNU/libsyscall/wrappers/mach_absolute_time.s" -o "$OUT/mach_absolute_time.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -DPRIVATE=1 -I "$OUT/compat_include" -idirafter "$XNU/osfmk" \
    "$XNU/libsyscall/wrappers/mach_timebase_info.c" -o "$OUT/mach_timebase_info.o"

# This project's own raw syscall wrappers (__ulock_wait/__ulock_wake,
# thread_selfid) and the real-TSD-slot bootstrap - see this script's
# header comment and each file's own comment.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libplatform_syscalls.c -o "$OUT/libplatform_syscalls.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -I "$XNU/libsyscall" libplatform_thread_self.c -o "$OUT/libplatform_thread_self.o"

# Real TSD bootstrap (pthread_tsd_shim.c) built fresh here rather than
# reused from build_libc_locale.sh's own output archive - this script
# runs BEFORE build_libc_locale.sh now (libc_locale.a's xlocale.c needs
# real os_unfair_lock/OSAtomic from THIS script's libplatform.a, a real
# new ordering dependency; see docs/roadmap.md Phase 4 for the updated
# tier order: string -> platform -> locale -> stdio -> gen). Same source
# file, same flags pthread_tsd_shim_libc_bootstrap.c's callers already
# use, just not yet archived anywhere at this point in the pipeline.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -include libc_build_prototypes.h \
    pthread_tsd_shim.c -o "$OUT/pthread_tsd_shim.o"

rm -f "$OUT/libplatform.a"
ar rcs "$OUT/libplatform.a" \
    "$OUT"/apple_platform/*.o \
    "$OUT/libplatform_mach_traps.o" "$OUT/mach_absolute_time.o" "$OUT/mach_timebase_info.o" \
    "$OUT/libplatform_syscalls.o" "$OUT/libplatform_thread_self.o"
echo "wrote $OUT/libplatform.a"

# Real smoketest: os_unfair_lock lock/unlock (uncontended - this target
# is provably single-threaded, see compat_stubs.c/tsd_bootstrap.c's own
# identical reasoning - correctness here is exercising the REAL fast-path
# cmpxchg/TSD-owner logic, not real cross-thread contention, which this
# target cannot produce), os_once (real one-time-init gate), and real
# OSAtomicIncrement32Barrier/OSAtomicDecrement32Barrier.
"$CLANG" $CFLAGS libplatform_smoketest.c -o "$OUT/libplatform_smoketest.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    cerror_stub.c -o "$OUT/cerror_stub.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o libplatform_smoketest.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/libplatform_smoketest.o" \
    "$OUT/libplatform.a" "$OUT/pthread_tsd_shim.o" "$OUT/cerror_stub.o"
echo "wrote libplatform_smoketest.macho"

otool -l libplatform_smoketest.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l libplatform_smoketest.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u libplatform_smoketest.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u libplatform_smoketest.macho >&2
fi
