#!/bin/bash
# Builds libc_build/libpthread.a: real Apple libpthread
# (third_party/libpthread, pinned libpthread-454.80.2, see
# setup_third_party.sh) - continuing the tier structure of
# build_libc_string.sh/build_libplatform.sh/build_libc_locale.sh/
# build_libc_stdio.sh/build_libc_gen.sh/build_libc_printf.sh/
# build_libmalloc.sh (see docs/roadmap.md Phase 4 for the running tier
# breakdown).
#
# REAL CONSTRAINT THAT ONCE SHAPED THIS WHOLE SCRIPT -- NOW PARTLY
# STALE, READ THE CORRECTION BELOW BEFORE TRUSTING IT (verified by
# reading kernel source, not assumed): third_party/xnu-7195/bsd/kern/
# bsd_pthread_stub.c is this project's kernel-side pthread.kext stand-in
# (real pthread.kext was never open-sourced by Apple, same situation as
# launchd/opendirectoryd - see project memory). Its bsdthread_create/
# bsdthread_register/psynch_* table entries all unconditionally returned
# ENOTSUP when this script was written, so real pthread_create() could
# never spawn a second schedulable thread on this target.
#
# CORRECTED 2026-09-03 (DAR-222): that stopped being true on 2026-08-20
# (Phase 7.5 step 1, bsd/kern/bsd_pthread_workq.c's real
# real_bsdthread_create) -- pthread_main_thread_bootstrap.c's own header
# has carried that correction since 2026-08-24, but this script's
# header did not, and the stale claim had a real consequence. Because
# "no second thread can ever exist" made pthread.c's thread-creation
# half look like dead code, nobody re-examined whether the hand-written
# replacement in pthread_main_thread_bootstrap.c was complete once
# threads became real. It was not: it never set the child's
# stackaddr/stackbottom/freeaddr/freesize, four fields real Apple's
# _pthread_struct_init() (pthread.c:898-901) sets, so
# pthread_get_stackaddr_np() returned NULL on every created thread and
# real libdispatch's _dispatch_clear_stack() bzero'd off the end of the
# manager thread's stack. That crash cost a multi-session investigation
# (Linear DAR-222). The header conflict below is still a real reason not
# to compile pthread.c wholesale, but "it's dead code anyway" is NOT a
# reason any more -- anything reimplemented from pthread.c here has to
# be kept feature-complete against the real function it replaces.
# Given that:
#   - Everything genuinely single-thread-correct is built for real:
#     pthread_mutex_t/pthread_cond_t/pthread_rwlock_t (real algorithms,
#     running under their real DEFAULT policy - FIRSTFIT,
#     __pthread_mutex_use_ulock=false, per types_internal.h's real
#     _PTHREAD_MTX_OPT_ULOCK_DEFAULT/_PTHREAD_MTX_OPT_POLICY_DEFAULT -
#     this script does NOT override either, see the corrected note
#     below; an earlier draft of this comment claimed ulock was forced
#     on, which was never actually implemented and was wrong - caught
#     by adversarial review before commit). The real uncontended fast
#     path (a plain CAS on the mutex's sequence words) is exercised and
#     QEMU-verified; the real psynch/FAIRSHARE contended slow path is
#     real code but can't be exercised, since no second thread can
#     exist (see below) - kept linked, not stubbed, real TSD
#     (pthread_key_create/setspecific/getspecific/pthread_once), real
#     pthread_atfork, real per-thread cwd (pthread_cwd.c).
#   - pthread_create() itself is REAL code (not a fabricated stub): it
#     performs the real bsdthread_create(2) syscall. It used to return
#     the real kernel ENOTSUP; since 2026-08-20 it really does spawn a
#     real, schedulable thread (see the correction above).
#   - pthread.c itself (2587 lines) is NOT compiled here: it bundles
#     three genuinely separate things - (1) main-thread bootstrap +
#     signature scheme, (2) pthread_create's real stack/mach-vm
#     allocation machinery (this was called "dead code here regardless
#     of whether it compiles" on the strength of the ENOTSUP claim
#     above; that is no longer true, see the 2026-09-03 correction),
#     (3) trivial one-liners
#     (pthread_equal/pthread_main_np/pthread_main_thread_np/
#     pthread_once). (2) also hits a REAL header conflict: this
#     project's own MIG-regenerated libc_build/malloc_mig/mach_vm.h
#     (built by build_libmalloc.sh from real Apple mach_vm.defs) and the
#     real SDK's <mach/vm_map.h> both define mach_make_memory_entry's
#     request/reply structs - genuine duplicate-definition conflict,
#     confirmed by reproducing it, not skipped preemptively. (1) and (3)
#     are reimplemented here in pthread_main_thread_bootstrap.c, real
#     algorithms faithfully copied from pthread.c (each function cites
#     its origin line), not reinvented - see that file's own extensive
#     header comment for the full real ABI trace (arm64's
#     _PTHREAD_STRUCT_DIRECT_TSD_OFFSET=224 - a real, checked constant
#     this project's own tsd_bootstrap.c/pthread_tsd_shim.c independently
#     arrived at the same value for, confirming both schemes are
#     consistent).
#
# Real _pthread_mutex_global_init() is NOT called by this tier's
# bootstrap (it only sets __pthread_mutex_use_ulock/
# __pthread_mutex_default_opt_policy from env vars / bsdthread_register's
# registration data, neither of which exist on this target - and it also
# pulls in real _simple_getenv, out of scope to port just for this).
# Real effect: pthread_mutex_t/pthread_cond_t/pthread_rwlock_t run under
# their real compiled-in DEFAULT values instead (FIRSTFIT policy, ulock
# off) - a real, valid Apple configuration (just not the one an
# env-var/registration-tuned real boot would pick), not an invented one.
#
# (DAR-416 step 1a, 2026-09-27: libsystem_pthread.dylib no longer ships
# these hand-written stubs; libsystem_kernel exports the generated ones,
# as on Apple. They remain in this static libpthread.a only for the static
# smoketests and dyld that still link it.)
# Real, NO_SYSCALL_STUB raw syscalls hand-written for this tier (Apple
# hand-writes these in libpthread's own internal sources, not this
# project's third_party/libpthread checkout's public headers - same
# class as __ulock_wait/__ulock_wake in build_libplatform.sh):
#   bsdthread_create (360) - pthread_bsdthread_create_syscall.c
#   psynch_mutexwait/mutexdrop/cvbroad/cvsignal/cvwait/rw_rdlock/
#     rw_wrlock/rw_unlock/cvclrprepost (301-308,312) -
#     pthread_psynch_syscalls.c - real numbers/signatures cross-checked
#     against syscalls.master; real but practically unreachable on this
#     target (see kernel-constraint note above) - kept anyway since -O0
#     keeps the real FAIRSHARE slow-path code in pthread_mutex.c/
#     pthread_cond.c/pthread_rwlock.c linked in regardless of whether
#     __pthread_mutex_use_ulock ever routes execution there.
# Real, Apple-generator-produced stubs (NOT hand-written - these three
# are NOT NO_SYSCALL_STUB in syscalls.master, so the same Perl generator
# build_libsyscall_smoketest.sh already uses produces them for real):
#   gettimeofday (116), __pthread_chdir (348), __pthread_fchdir (349)
#
# Other real gaps found and fixed while bringing this up (empirically):
#   - sched.h/pthread.h redefine struct sched_param when both the public
#     and private (posix_sched.h) headers get included - real Apple
#     internal builds avoid this via -D__POSIX_LIB__ (found in this
#     project's own third_party/libpthread/xcodescripts/pthread.xcconfig,
#     not guessed), which makes the public header just forward-declare
#     the struct instead of redefining it.
#   - mach/thread_policy.h's THREAD_QOS_* additions and os/
#     alloc_once_private.h are both real SDK-vintage/cross-repo-version
#     gaps (same class as build_libplatform.sh's bridgeos macro gap) -
#     compat_include forwarding shims with real values, real reasons
#     documented inline in this script.
#   - -ffreestanding makes clang prefer its OWN resource-dir limits.h
#     over the real SDK's (confirmed via -H include-stack tracing) -
#     silently dropping PTHREAD_STACK_MIN. Real values (16384, arm64)
#     copied verbatim from the pinned SDK's own limits.h into a
#     compat_include/limits.h `#include_next` forwarder.
#   - Real iPhoneOS SDK's own <mach/mach_vm.h> is literally
#     "#error mach_vm.h unsupported." (confirmed by reading it) - see
#     compat_include/mach/mach_vm.h's own header for the small
#     hand-declared (no MIG structs, no collision) real-signature
#     forwarder used instead of this project's malloc_mig output.
#   - _os_xbs_chrooted (real Apple internal XBS-sandbox-detection flag,
#     used by _pthread_mutex_global_init to force ulock off in that
#     sandbox) has no real definition anywhere in this project's pinned
#     checkouts (lives in a part of libplatform's real init.c this
#     project doesn't build, same "needs _simple_getenv" gap
#     build_libplatform.sh already flagged as out of scope) - defined
#     as a compile-time constant 0 (never chrooted here, real value for
#     this target, not a guess).
#   - _os_once_acquire (pthread.c's own real pthread_once()) does not
#     exist in this project's pinned libplatform-254.80.2 (confirmed by
#     grepping the whole checkout) - a real cross-repo version-skew gap,
#     libpthread-454.80.2 expects a newer libplatform. pthread_once() is
#     reimplemented in pthread_main_thread_bootstrap.c using this
#     project's real, already-QEMU-verified _os_once() instead - same
#     real semantics (single-execution gate via the real sig field),
#     different (older, but real and present) underlying primitive.
#   - abort_with_reason() (real path: pthread_t signature-corruption
#     detection -> real SIGABRT via Mach reason-payload syscalls) needs
#     a full real signal-delivery subsystem this project hasn't built
#     (separate, larger Phase 4 scope) - pthread_abort_with_reason_compat.c
#     provides the real EFFECT (unconditional process termination) via
#     this project's own already-real exit syscall instead of the real
#     mechanism; last resort per CLAUDE.md, after confirming the real
#     path's actual blocker, not skipped preemptively.
#   - __is_threaded (real global, pthread.c:122) and _simple_asl_log
#     (real ASL client call from pthread_tsd.c's destructor-error
#     diagnostic path) both need real pthread.c-adjacent
#     infrastructure this tier doesn't build - pthread_compat_extras.c
#     provides __is_threaded=0 (real, correct value: no second thread
#     can ever start, see kernel-constraint note) and a documented
#     no-op _simple_asl_log (this project has no syslogd/ASL daemon
#     anywhere in its process model regardless of transport fidelity,
#     same reasoning already established for os/assumes.h's stand-in in
#     build_libc_locale.sh).
#   - libc_build/libmalloc.a (build_libmalloc.sh) bundles the OLD
#     pthread_tsd_shim.o (needed at the time for its own zone-locking
#     TSD) - now a genuine duplicate-symbol conflict against real
#     pthread_tsd.c's pthread_key_create/setspecific/getspecific. Fixed
#     by removing that one member from the existing archive (`ar d`,
#     a normal archive operation, not a hack) once this tier's real
#     versions exist. tsd_bootstrap.c/pthread_tsd_shim.c/
#     pthread_tsd_shim_libc_bootstrap.c themselves are left on disk,
#     unused by this tier, for the locale/stdio/gen/printf smoketests
#     that still link against them directly (not yet relinked against
#     real libpthread - see this script's own footer note).
set -e
cd "$(dirname "$0")"

PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
PLATFORM="../../third_party/libplatform"
MALLOC="../../third_party/libmalloc"
OUT="libc_build"
mkdir -p "$OUT/apple_pthread" "$OUT/compat_include/mach"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/libplatform.a" ] || [ ! -f "$OUT/libmalloc.a" ]; then
    echo "error: run build_libplatform.sh and build_libmalloc.sh first" >&2
    exit 1
fi

cat > "$OUT/compat_include/limits.h" <<'EOF'
#ifndef _COMPAT_LIMITS_H_FORWARD_
#define _COMPAT_LIMITS_H_FORWARD_
#include_next <limits.h>
/* real values from the pinned iPhoneOS14.4 SDK's own usr/include/limits.h
 * (see this script's header comment: -ffreestanding hides them). */
#ifndef PTHREAD_STACK_MIN
#define PTHREAD_DESTRUCTOR_ITERATIONS 4
#define PTHREAD_KEYS_MAX 512
#define PTHREAD_STACK_MIN 16384
#endif
#endif
EOF

cat > "$OUT/compat_include/mach/thread_policy.h" <<'EOF'
#ifndef _MACH_THREAD_POLICY_H_COMPAT_
#define _MACH_THREAD_POLICY_H_COMPAT_
#include_next <mach/thread_policy.h>
/* SDK-vintage gap - real values copied verbatim from this project's own
 * third_party/xnu-7195/osfmk/mach/thread_policy.h. */
#ifndef THREAD_QOS_POLICY
#define THREAD_QOS_POLICY               9
#define THREAD_QOS_POLICY_OVERRIDE      10
typedef uint8_t thread_qos_t;
#define THREAD_QOS_UNSPECIFIED          0
#define THREAD_QOS_DEFAULT              THREAD_QOS_UNSPECIFIED
#define THREAD_QOS_MAINTENANCE          1
#define THREAD_QOS_BACKGROUND           2
#define THREAD_QOS_UTILITY              3
#define THREAD_QOS_LEGACY               4
#define THREAD_QOS_USER_INITIATED       5
#define THREAD_QOS_USER_INTERACTIVE     6
#define THREAD_QOS_LAST                 7
#define THREAD_QOS_MIN_TIER_IMPORTANCE  (-15)
#endif
#endif
EOF

cat > "$OUT/compat_include/os/alloc_once_private.h" <<'EOF'
#ifndef __OS_ALLOC_ONCE_PRIVATE_COMPAT__
#define __OS_ALLOC_ONCE_PRIVATE_COMPAT__
#define __OS_ALLOC_INDIRECT__
#include <os/alloc_once_impl.h>
#undef __OS_ALLOC_INDIRECT__
/* Real Apple builds assign OS_ALLOC_ONCE_KEY_* across the WHOLE
 * libSystem umbrella from one shared enum; this project only builds a
 * curated subset, so that shared enum doesn't exist here - locally
 * assigned, doesn't collide with anything this project actually
 * compiles (only libpthread's inline_internal.h uses this one). */
#ifndef OS_ALLOC_ONCE_KEY_LIBSYSTEM_PTHREAD
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_PTHREAD 1
#endif
#endif
EOF

cat > "$OUT/compat_include/mach/mach_vm.h" <<'EOF'
#ifndef _MACH_MACH_VM_H_COMPAT_
#define _MACH_MACH_VM_H_COMPAT_
/* Real iPhoneOS SDK's own <mach/mach_vm.h> is literally "#error
 * mach_vm.h unsupported." - see this script's header comment for the
 * full story (why this project's own malloc_mig/mach_vm.h can't be
 * used here instead: a real duplicate-definition conflict against
 * <mach/vm_map.h>, both sharing mach_make_memory_entry). Small
 * hand-typed declarations (no MIG structs, no collision) for exactly
 * the functions pthread_atfork.c calls - real signatures copied
 * verbatim from their real definitions in this project's own
 * third_party/xnu-7195/libsyscall/mach/mach_vm.c, which is itself real
 * Apple source already fully built into libc_build/libmalloc.a
 * (build_libmalloc.sh) - linking against that existing archive
 * satisfies these at link time; this header only satisfies the
 * compiler. */
kern_return_t mach_vm_allocate(mach_port_name_t target, mach_vm_address_t *address, mach_vm_size_t size, int flags);
kern_return_t mach_vm_deallocate(mach_port_name_t target, mach_vm_address_t address, mach_vm_size_t size);
kern_return_t mach_vm_protect(mach_port_name_t task, mach_vm_address_t address, mach_vm_size_t size, boolean_t set_maximum, vm_prot_t new_protection);
kern_return_t mach_vm_map(mach_port_name_t target, mach_vm_address_t *address, mach_vm_size_t size, mach_vm_offset_t mask, int flags, mem_entry_name_port_t object, memory_object_offset_t offset, boolean_t copy, vm_prot_t cur_protection, vm_prot_t max_protection, vm_inherit_t inheritance);
mach_port_t mach_reply_port(void);
#endif
EOF

# Real flags: __POSIX_LIB__/__PTHREAD_BUILDING_PTHREAD__/
# __PTHREAD_EXPOSE_INTERNALS__/OS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY -
# copied verbatim from this project's own
# third_party/libpthread/xcodescripts/pthread.xcconfig
# (BASE_PREPROCESSOR_MACROS), not reinvented. _os_xbs_chrooted=0: see
# this script's header comment.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O2 \
    -Wno-nullability-completeness -Wno-availability -Wno-deprecated-declarations \
    -D__LIBC__ -D__POSIX_LIB__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_NON_CANCELABLE=1 \
    -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 -D__PTHREAD_BUILDING_PTHREAD__=1 -D__PTHREAD_EXPOSE_INTERNALS__ \
    -DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1 -DPRIVATE=1 \
    -D_os_xbs_chrooted=0 \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=ios,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos(x)=ios,introduced=x \
    -I . \
    -I $PTHREAD -I $PTHREAD/src -I $PTHREAD/src/resolver -I $PTHREAD/private -I $PTHREAD/include -I $PTHREAD/kern \
    -I $PLATFORM/internal -I $PLATFORM/private -I $PLATFORM/include \
    -I $OUT/compat_include \
    -I $MALLOC/private \
    -I $XNU/libsyscall -I $XNU/libsyscall/mach -I $XNU/bsd \
    -idirafter $XNU/osfmk -idirafter $XNU/libkern"

# Real Apple libpthread source: TSD, mutex, cond, rwlock, atfork,
# per-thread cwd. Deliberately NOT built: pthread.c (see header
# comment), qos.c (QoS class machinery - needs real bsdthread_register,
# unreachable per the kernel constraint), pthread_cancelable.c
# (pthread_cancel() needs real signal delivery to a real second thread -
# doubly unreachable), pthread_dependency.c (os_workgroup-adjacent,
# no real consumer in this project), kern/*.c (these are the KERNEL-side
# pthread.kext internals bsd_pthread_stub.c stands in for - do not
# confuse with the userland src/ files this script builds).
ok=0
skip=0
for f in pthread_tsd pthread_mutex pthread_cond pthread_rwlock pthread_atfork pthread_cwd; do
    out="$OUT/apple_pthread/${f}.o"
    if $CLANG $CFLAGS "$PTHREAD/src/${f}.c" -o "$out" 2>"$OUT/apple_pthread/${f}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/apple_pthread/${f}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $f"
    fi
done
echo "apple_pthread: $ok compiled, $skip skipped"

# This project's own real (not-fabricated) support files - see this
# script's header comment for what each one is and why it's needed
# instead of the corresponding real pthread.c code.
for f in pthread_main_thread_bootstrap pthread_bsdthread_create_syscall \
         pthread_bsdthread_register_syscall pthread_bsdthread_terminate_syscall \
         pthread_tsd_platform_bootstrap pthread_psynch_syscalls \
         pthread_compat_extras pthread_abort_with_reason_compat \
         pthread_guard_compat pthread_thread_ops_compat \
         pthread_clear_qos_tsd_compat; do
    $CLANG $CFLAGS "${f}.c" -o "$OUT/${f}.o"
done

# Real Apple libplatform source: _simple_getenv (getenv.c) - pulled in
# transitively by real pthread_mutex.c's _pthread_mutex_global_init(),
# never actually called (see header comment on forcing use_ulock
# directly instead) but still a real compiled reference at -O2 - and
# _platform_memcmp (string/generic/memcmp.c), getenv.c's own real
# dependency (grep confirmed, not guessed).
$CLANG -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O2 \
    -DPRIVATE=1 -I "$PLATFORM/private" -I "$PLATFORM/internal" -I "$PLATFORM/include" -I "$OUT/compat_include" \
    "$PLATFORM/src/simple/getenv.c" -o "$OUT/pthread_simple_getenv.o"
$CLANG -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O2 \
    -DPRIVATE=1 -I "$PLATFORM/private" -I "$PLATFORM/internal" -I "$PLATFORM/include" \
    "$PLATFORM/src/string/generic/memcmp.c" -o "$OUT/pthread_platform_memcmp.o"

# Real Apple-generator-produced syscall stubs (gettimeofday/
# __pthread_chdir/__pthread_fchdir - NOT NO_SYSCALL_STUB, same real Perl
# generator build_libsyscall_smoketest.sh already uses).
SYSGEN="$OUT/pthread_syscall_gen"
rm -rf "$SYSGEN"; mkdir -p "$SYSGEN"
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" "$XNU/libsyscall/custom" "$XNU/libsyscall/Platforms" \
    iPhoneOS "$SYSGEN" >/dev/null
SYSFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -I $XNU/BUILD.hdrs/dst/usr/include -I $SYSGEN"
$CLANG $SYSFLAGS "$SYSGEN/___gettimeofday.s" -o "$OUT/pthread_gettimeofday.o"
$CLANG $SYSFLAGS "$SYSGEN/___pthread_chdir.s" -o "$OUT/pthread___pthread_chdir.o"
$CLANG $SYSFLAGS "$SYSGEN/___pthread_fchdir.s" -o "$OUT/pthread___pthread_fchdir.o"
# __mmap/__munmap - same generator, needed by the mmap-backed
# pthread_create() stack allocation below (build_simple_cmds.sh already
# generates these same two for its own SYS_STUBS_NEEDED list - same real
# stubs, regenerated here rather than reused across scripts since this
# tier doesn't share an object directory with that one).
$CLANG $SYSFLAGS "$SYSGEN/___mmap.s" -o "$OUT/pthread___mmap.o"
# __thread_selfid (real raw trap, NO_SYSCALL_STUB in syscalls.master --
# create-syscalls.pl still emits the trap itself, just skips the
# convenience wrapper, same class of gap as ___getlogin/___setlogin
# found in tools/userland_staging/build_launchd_syscalls.sh) + getegid
# (plain, non-NO_SYSCALL_STUB raw trap) -- both needed by this file's
# own pthread_thread_ops_compat.c below (real
# pthread_threadid_np()/pthread_getugid_np() implementations).
$CLANG $SYSFLAGS "$SYSGEN/___thread_selfid.s" -o "$OUT/pthread___thread_selfid.o"
$CLANG $SYSFLAGS "$SYSGEN/_getegid.s" -o "$OUT/pthread_getegid.o"
$CLANG $SYSFLAGS "$SYSGEN/___munmap.s" -o "$OUT/pthread___munmap.o"
# __ulock_wait2 (syscall #544, cross-checked against syscalls.master) -
# real trap pthread_cond.c's __pthread_ulock_cond_wait() already calls
# (the newer 5-argument ulock_wait variant with a `value2` field) but
# this tier never actually compiled+archived before now - a real,
# pre-existing gap (not a regression), found while porting LLVM libc++
# (build_llvm_libcxx.sh's smoketest pulls pthread_cond.o transitively via
# cxa_guard.cpp's __libcpp_condvar_wait).
$CLANG $SYSFLAGS "$SYSGEN/___ulock_wait2.s" -o "$OUT/pthread___ulock_wait2.o"

# Real mmap(2)/munmap(2) (simple_cmds_mmap_compat.c - real Apple
# semantics, see that file's own header) - pthread_create()'s real
# stack-allocation fix (2026-08-24, pthread_main_thread_bootstrap.c)
# needs it, and until now this file was only ever compiled ad hoc into
# individual command binaries (build_simple_cmds.sh's tail/grep tiers),
# never into a shared archive. Bundled directly into libpthread.a here
# rather than requiring every future libpthread.a consumer to remember
# to add it separately - same shape as build_libiconv.sh bundling real
# qsort.c/heapsort.c directly into libiconv.a for its own internal need.
# Real risk this creates, flagged rather than hidden: a binary that
# links BOTH libpthread.a and independently compiles
# simple_cmds_mmap_compat.c itself (as tail/grep currently do) will hit
# a duplicate-symbol conflict at link time - not hit yet by anything in
# this tree, but the first real consumer that combines both should drop
# its own separate copy and rely on libpthread.a's, the same way
# build_libpthread.sh's own `ar d` step below already retires libmalloc.a's
# old pthread_tsd_shim.o for an identical reason.
$CLANG $CFLAGS "simple_cmds_mmap_compat.c" -o "$OUT/pthread_mmap_compat.o"

rm -f "$OUT/libpthread.a"
ar rcs "$OUT/libpthread.a" \
    "$OUT"/apple_pthread/*.o \
    "$OUT/pthread_main_thread_bootstrap.o" "$OUT/pthread_bsdthread_create_syscall.o" \
    "$OUT/pthread_bsdthread_register_syscall.o" "$OUT/pthread_bsdthread_terminate_syscall.o" \
    "$OUT/pthread_tsd_platform_bootstrap.o" "$OUT/pthread_psynch_syscalls.o" \
    "$OUT/pthread_compat_extras.o" "$OUT/pthread_abort_with_reason_compat.o" \
    "$OUT/pthread_guard_compat.o" "$OUT/pthread_thread_ops_compat.o" \
    "$OUT/pthread_simple_getenv.o" "$OUT/pthread_platform_memcmp.o" \
    "$OUT/pthread_gettimeofday.o" "$OUT/pthread___pthread_chdir.o" "$OUT/pthread___pthread_fchdir.o" \
    "$OUT/pthread_mmap_compat.o" "$OUT/pthread___mmap.o" "$OUT/pthread___munmap.o" \
    "$OUT/pthread___ulock_wait2.o" "$OUT/pthread___thread_selfid.o" "$OUT/pthread_getegid.o" \
    "$OUT/pthread_clear_qos_tsd_compat.o"
echo "wrote $OUT/libpthread.a"

# Real archive-member removal, not a hack: retires build_libmalloc.sh's
# OLD pthread_tsd_shim.o (now a genuine duplicate-symbol conflict
# against this tier's real pthread_tsd.c) from the already-built
# libmalloc.a - see this script's header comment. Idempotent (no-op if
# already removed by a prior run).
if ar t "$OUT/libmalloc.a" 2>/dev/null | grep -q '^pthread_tsd_shim\.o$'; then
    ar d "$OUT/libmalloc.a" pthread_tsd_shim.o
    echo "retired old pthread_tsd_shim.o from libmalloc.a (superseded by real pthread_tsd.c)"
fi

# NOTE: libc_locale.a's copy of pthread_tsd_shim.o is deliberately NOT
# retired the way libmalloc.a's is above -- see pthread_tsd_shim.c's own
# header comment for why a real duplicate-archive-member bug (found while
# root-causing the Eighteenth pass's real bash SIGSEGV, docs/roadmap.md)
# was fixed with __attribute__((weak)) on that file's 4 public POSIX
# functions instead of archive surgery: libc_locale.a's own pthread_tsd_shim.o
# is a genuine, still-load-bearing dependency for OTHER real symbols
# (pthread_tsd_shim_bootstrap, needed by libmalloc.a's real _get_zone; the
# underscore-prefixed _pthread_once/_pthread_setspecific/etc FreeBSD-compat
# aliases, needed by real Libc source like strsignal.c/localtime.c) that
# archive removal would have broken for every standalone smoketest that
# doesn't separately link libpthread.a -- confirmed by hitting exactly
# that regression here before switching approaches.

# pthread_smoketest.macho is built by build_pthread_smoketest.sh (DAR-127),
# as a dynamic executable against libsystem_pthread.dylib. It used to be a
# static Csu link here, which needed libc_gen/libc_stdio/libc_locale
# scratch archives, raw syscall objects and alias lists, and had stopped
# linking at all (undefined _kdebug_trace/_kill/_mprotect$UNIX2003/
# _sleep$UNIX2003/_write$UNIX2003); under set -e that failed this whole
# script after libpthread.a was already written. A static binary is also
# against the DAR-314 rule (only pre-dyld binaries may be static), and the
# dylibs a dynamic link needs are built from this archive, so the
# smoketest can't live in this script.

# NOT done by this script (documented follow-up, not silently skipped):
# the locale/stdio/gen/printf tiers' own smoketests still link against
# tsd_bootstrap.c/pthread_tsd_shim.c/pthread_tsd_shim_libc_bootstrap.c
# directly rather than this real libpthread.a - relinking all four
# against the real tier is real, valuable follow-up work but was not
# completed in this pass (see docs/roadmap.md Phase 4 status).
