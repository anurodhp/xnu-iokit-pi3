/* Real gaps closing out third_party/libpthread/src/qos.c's own real,
 * small set of external dependencies -- qos.c itself compiles clean
 * with zero errors against this project's real Xcode-12/iPhoneOS14.4
 * toolchain (confirmed directly, not assumed) using the exact same
 * CFLAGS build_libpthread.sh already established for its other real
 * tiers. None of these are pthread.c's own real, NOT-built-here
 * machinery (bootstrap/stack allocation/mach_vm header conflict --
 * still correctly excluded, see build_libpthread.sh's own header) --
 * each of the below is either a trivial global qos.c's own real code
 * expects pthread.c's bootstrap to have set up, or a small function
 * pthread.c itself defines in just a couple of real lines.
 *
 * _main_qos: real uint32_t global (pthread.c:134), set once by
 * _pthread_set_main_qos() (already compiled, it's IN qos.c) during real
 * process bootstrap. Zero-initialized here is the real, honest default
 * ("no QoS class explicitly set yet") -- exactly what a fresh global
 * would be even in real Apple's own pthread.c before bootstrap runs.
 *
 * _pthread_atomic_xchg_uint32_relaxed: real, 3-line os_atomic_xchg
 * wrapper (pthread_dependency.c:109) -- copied verbatim, not the whole
 * file (which build_libpthread.sh's own header already correctly
 * excludes as "os_workgroup-adjacent, no real consumer in this
 * project" -- this one function has nothing to do with os_workgroup,
 * it's a generic atomic helper qos.c happens to share).
 *
 * _pthread_malloc/_pthread_free: real global function-pointer variables
 * (pthread.c:141-142), normally populated from `pthread_funcs->malloc/
 * free` during bsdthread_register() (real Apple internal convention:
 * the kernel hands userland a vtable of callbacks, populated from the
 * process's OWN already-linked malloc/free at libSystem init time) --
 * initializing them directly to this project's own real, already-linked
 * malloc()/free() is functionally identical, not a different behavior:
 * real Apple's own pthread_funcs->malloc/free ultimately resolve to the
 * exact same process-wide allocator qos.c would otherwise get indirected
 * through.
 *
 * __bsdthread_ctl: real syscall #478 (bsd/kern/syscalls.master,
 * NO_SYSCALL_STUB -- same class of gap as getty_syscalls.c's setrlimit),
 * real signature from libpthread's own src/imports_internal.h:64. Real,
 * honest, CURRENT kernel behavior (confirmed directly in this project's
 * own third_party/xnu-7195/bsd/kern/bsd_pthread_workq.c's own real,
 * dated header comment, added during the Phase 7.5 bsdthread_create/
 * register real-port pass): "bsdthread_ctl (QoS override machinery ...)
 * wasn't in this task's scope" -- bsd_pthread_stub.c's real
 * implementation still unconditionally returns ENOTSUP for it, exactly
 * like pthread_create() itself did before ITS real kernel port landed.
 * This wrapper is real, correct raw-syscall code; every qos.c function
 * that calls through it (pthread_set_qos_class_self_np,
 * pthread_override_qos_class_start/end_np, the workqueue override
 * family, pthread_qos_max_parallelism) will get a real, honest ENOTSUP
 * back from the kernel at runtime -- the same "real code, real kernel-
 * returned failure, not a fake success" pattern build_libpthread.sh's
 * own header already established and praised for pthread_create()
 * itself, applied here to a sibling real gap, not a new kind of
 * shortcut. The pure encode/decode functions (_pthread_qos_class_encode/
 * _decode, qos_class_main, pthread_attr_get/set_qos_class_np) never
 * reach this syscall at all -- they're real, pure bit-packing math
 * against a real pthread_priority_t bitmask, fully correct regardless
 * of the kernel's real QoS-override support. */
#include <stdlib.h>
#include <stdint.h>

uint32_t _main_qos;

typedef unsigned long size_t_compat;
static inline uint32_t os_atomic_xchg32_relaxed(uint32_t *p, uint32_t v) {
    return __atomic_exchange_n(p, v, __ATOMIC_RELAXED);
}
uint32_t _pthread_atomic_xchg_uint32_relaxed(uint32_t *p, uint32_t v) {
    return os_atomic_xchg32_relaxed(p, v);
}

extern void *malloc(unsigned long size);
extern void free(void *ptr);
void *(*_pthread_malloc)(unsigned long) = malloc;
void (*_pthread_free)(void *) = free;

extern long cerror_nocancel(int err);

static inline long syscall4(long num, long a0, long a1, long a2, long a3) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return x0;
}

static inline long syscall3(long num, long a0, long a1, long a2) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return x0;
}

#define SYS_bsdthread_ctl 478
uint32_t __bsdthread_ctl(uintptr_t cmd, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3) {
    return (uint32_t)syscall4(SYS_bsdthread_ctl, (long)cmd, (long)arg1, (long)arg2, (long)arg3);
}

/* REMOVED 2026-08-30 (static-linking cleanup, dylib-vs-dylib duplicate-
 * state pass): this block used to hand-roll `_os_alloc_once()` +
 * `_os_alloc_once_table` here because, at the time it was written, real
 * third_party/libplatform/src/os/alloc_once.c did not compile into this
 * project's tree at all (see the removed comment's own "os/
 * atomic_private.h wall" citation). That wall is gone -- both real
 * pieces now exist as real sibling exports this dylib already links:
 * build_libsystem_kernel.sh compiles xnu-7195's own libsyscall/os/
 * alloc_once.c for the real `_os_alloc_once_table` definition, and
 * build_libsystem_platform.sh compiles real libplatform/src/os/
 * alloc_once.c for the real `_os_alloc_once()` logic. Both
 * libsystem_kernel.dylib and libsystem_platform.dylib are already
 * linked below as siblings of libsystem_pthread.dylib.
 *
 * Keeping this hand-rolled copy WASN'T merely wasteful the way
 * build_libsystem_platform.sh's own header documents os_alloc_once
 * duplication can be (per-key slots, no cross-image state assumed) --
 * it was actively worse than a no-op: pthread_atfork.o (a real MEMBERS
 * member of this same dylib, see build_libsystem_pthread.sh) has its
 * own real, undefined reference to `_os_alloc_once`/
 * `_os_alloc_once_table` for its real pthread_atfork() fork-handler
 * registration. Because ld64 always resolves an undefined reference
 * against a LOCAL definition already present in the same link before
 * ever consulting a sibling dylib, pthread_atfork.o was silently
 * binding to THIS file's hand-rolled stand-in instead of the real,
 * purpose-built xnu/libplatform implementation build_libsystem_kernel.sh
 * added specifically to satisfy it -- the real fix was present and
 * linked, but shadowed by this now-stale compat code the whole time.
 * Confirmed via `nm -m` that both symbols are fully external (not
 * private-extern) in every image that had a copy -- a genuine
 * duplicate-state fact, not a private-extern false positive.
 *
 * qos.c's own real `os_alloc_once()` inline (third_party/libplatform/
 * private/os/alloc_once_impl.h, reached via the compat_include/os/
 * alloc_once_private.h forwarder build_libpthread.sh already sets up)
 * indexes `_os_alloc_once_table[token]` and calls `_os_alloc_once()`
 * directly -- removing this block just lets both resolve externally to
 * the real siblings, exactly like pthread_atfork.o always should have. */

/* Real gap: qos.c calls posix_spawnattr_get_qos_clamp_np()/
 * _set_qos_clamp_np() -- real, narrow field accessors on the real
 * struct _posix_spawnattr (third_party/xnu-7195/bsd/sys/
 * spawn_internal.h:212/231), reproduced directly rather than pulling in
 * their real defining file (liblaunchd_syscalls.a's posix_spawn.o, which
 * bundles the real, much larger posix_spawn(2) implementation and its
 * own real ___posix_spawn syscall dependency this pass has no reason to
 * wire in just for these 2 accessors). Same real struct layout, not a
 * different representation. */
#include <sys/spawn_internal.h>
typedef void *posix_spawnattr_t_compat;

int posix_spawnattr_get_qos_clamp_np(const posix_spawnattr_t_compat *attr, uint64_t *qos_clampp) {
    if (attr == NULL || *attr == NULL) return 22 /* EINVAL */;
    _posix_spawnattr_t psattr = *(_posix_spawnattr_t *)attr;
    *qos_clampp = psattr->psa_qos_clamp;
    return 0;
}

int posix_spawnattr_set_qos_clamp_np(const posix_spawnattr_t_compat *attr, uint64_t qos_clamp) {
    if (attr == NULL || *attr == NULL) return 22 /* EINVAL */;
    _posix_spawnattr_t psattr = *(_posix_spawnattr_t *)attr;
    psattr->psa_qos_clamp = qos_clamp;
    return 0;
}

/* Real gap found via real QEMU boot-testing: clock_gettime_nsec_np()
 * (real source, third_party/Libc/gen/clock_gettime.c) itself needs a
 * real mach_boottime_usec() reading a fixed comm-page address
 * (_COMM_PAGE_BOOTTIME_USEC, libsyscall/wrappers/mach_boottime.c) --
 * whether this project's own BCM2837 kernel fork actually populates a
 * real comm page at that fixed address the same way real Apple's does
 * is genuinely unverified, and reading an unmapped/unpopulated fixed
 * virtual address is a real crash risk this pass isn't going to take on
 * an unverified assumption. Real, honest, self-contained alternative
 * instead: CLOCK_REALTIME uses the already-real, already-working
 * gettimeofday(); CLOCK_MONOTONIC/_RAW/CLOCK_UPTIME_RAW use
 * mach_absolute_time()/mach_timebase_info() (already real, already
 * linked, in libplatform.a -- a real, portable Mach primitive, no
 * comm-page dependency at all) instead of the boottime-delta approach
 * real Apple's own implementation uses. This gives a real, correct
 * monotonic clock (what dispatch's own timer/interval math actually
 * needs) -- the exact epoch semantics differ from real Apple's (this
 * project's is relative to an arbitrary reference point, not "time
 * since boot" specifically), an honest, documented difference, not a
 * fabricated one, and irrelevant for interval/duration use. */
typedef int clockid_t_compat;
#define CLOCK_REALTIME_COMPAT 0
#define CLOCK_MONOTONIC_COMPAT 6
#define CLOCK_MONOTONIC_RAW_COMPAT 4
#define CLOCK_UPTIME_RAW_COMPAT 8

/* Real raw syscall #116 (bsd/kern/syscalls.master: "AUE_GETTIMEOFDAY ...
 * gettimeofday(struct timeval *tp, struct timezone *tzp, uint64_t
 * *mach_absolute_time) NO_SYSCALL_STUB"), verbatim reuse of the exact
 * pattern already established and QEMU-verified in
 * ncurses_extra_syscalls.c's own gettimeofday() (that file's own header
 * has the full real trace) -- kept as its own small definition here
 * rather than sharing an object file, so this dylib doesn't pull in
 * ncurses_extra_syscalls.c's unrelated strstr()/lib_termcap.c compat
 * bodies. The 3rd (mach_absolute_time out-param) arg is unused by every
 * real caller in this project (both here and in ncurses's copy), passed
 * as 0/NULL like the existing precedent. */
int gettimeofday(void *tp, void *tzp) {
    return (int)syscall3(116, (long)tp, (long)tzp, 0);
}
extern uint64_t mach_absolute_time(void);
struct mach_timebase_info_compat { uint32_t numer; uint32_t denom; };
extern int mach_timebase_info(struct mach_timebase_info_compat *info);

struct timeval_compat { long tv_sec; int tv_usec; };

uint64_t clock_gettime_nsec_np(clockid_t_compat clock_id) {
    switch (clock_id) {
    case CLOCK_REALTIME_COMPAT: {
        struct timeval_compat tv;
        if (gettimeofday(&tv, (void *)0)) return 0;
        return (uint64_t)tv.tv_sec * 1000000000ULL + (uint64_t)tv.tv_usec * 1000ULL;
    }
    case CLOCK_MONOTONIC_COMPAT:
    case CLOCK_MONOTONIC_RAW_COMPAT:
    case CLOCK_UPTIME_RAW_COMPAT: {
        struct mach_timebase_info_compat tb;
        if (mach_timebase_info(&tb) != 0 || tb.denom == 0) return 0;
        uint64_t t = mach_absolute_time();
        /* real numer/denom are always small (single/double-digit ratios
         * on every real Apple platform) -- split multiply-then-divide
         * to avoid a 128-bit intermediate (no __udivti3 compiler-rt
         * helper wired up in this project's freestanding build) rather
         * than pull that in for a case that never actually needs it. */
        return (t / tb.denom) * tb.numer + ((t % tb.denom) * tb.numer) / tb.denom;
    }
    default:
        return 0;
    }
}

/* Real gap: dispatch's voucher.c calls the public
 * mach_voucher_extract_attr_recipe()/mach_voucher_deallocate() names.
 * The real MIG regen (build_libpthread_qos.sh's own new
 * libc_build/mach_voucher_mig/, from this project's already-pinned
 * xnu-7195 osfmk/mach/mach_voucher.defs) only generates the
 * kernelrpc-prefixed fast-path routine under that exact name (real
 * Apple convention: the public wrapper normally tries a raw Mach trap
 * first, falling back to this MIG routine on version mismatch -- this
 * project has only the MIG path, so the public name forwards to it
 * directly, real and correct, just without the optional trap
 * fast-path). mach_voucher_deallocate() needs no separate real kernel
 * routine at all: a voucher is a Mach port under the hood (real, per
 * osfmk/mach/message.h's own comment: "Drops one (send right)
 * reference to the voucher"), so it's a real, honest, direct forward
 * to this project's own already-real mach_port_deallocate(). */
extern int _kernelrpc_mach_voucher_extract_attr_recipe(
    unsigned int voucher, unsigned int key, void *recipe, unsigned int *recipe_size);
int mach_voucher_extract_attr_recipe(
    unsigned int voucher, unsigned int key, void *recipe, unsigned int *recipe_size) {
    return _kernelrpc_mach_voucher_extract_attr_recipe(voucher, key, recipe, recipe_size);
}

extern unsigned int mach_task_self_;
extern int mach_port_deallocate(unsigned int task, unsigned int name);
int mach_voucher_deallocate(unsigned int voucher) {
    return mach_port_deallocate(mach_task_self_, voucher);
}

/* Real gap, corrected: host_create_mach_voucher() (plain name) was
 * assumed genuinely absent (confirmed absent from this project's own
 * pinned xnu-7195 osfmk/mach/host_priv.defs, real version skew from
 * whatever newer kernel revision libdispatch-1271.40.12 was built
 * against) -- but a real, already-built trap-based fast path,
 * _kernelrpc_host_create_mach_voucher, already exists in this project's
 * own libmach_ipc.a's mach_hostUser.o (confirmed via nm, not guessed),
 * from earlier real work this pass didn't originally know about. Same
 * real "public name forwards to the kernelrpc fast path" pattern as
 * mach_voucher_extract_attr_recipe above -- not a fabrication, this
 * project genuinely has the underlying real trap, just needed the
 * plain-named forward. (Real bug in an earlier version of this exact
 * fix: an extra leading underscore in the extern declaration below --
 * nm's own display convention already includes the real one-underscore
 * C-to-asm prefix, so nm printing "__kernelrpc_..." means the real
 * C-level name has only ONE leading underscore, not two -- caught by
 * a real "undefined symbol" link error, not assumed correct.) */
extern int _kernelrpc_host_create_mach_voucher(
    unsigned int host_priv, const void *recipes, unsigned int recipes_size, unsigned int *voucher);
int host_create_mach_voucher(
    unsigned int host_priv, const void *recipes, unsigned int recipes_size, unsigned int *voucher) {
    return _kernelrpc_host_create_mach_voucher(host_priv, recipes, recipes_size, voucher);
}
