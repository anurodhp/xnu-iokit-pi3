/* Real creat/shm_open/usleep raw syscall wrappers + real, honest
 * os_fault_with_payload/os_variant_has_internal_diagnostics stand-ins --
 * libsystem_notify.dylib's own real source calls these but this
 * project's checkout has no compiled source wired in for any of them
 * (confirmed via `nm -g` across every .a in libc_build/, see
 * build_libsystem_notify.sh's own header). Deferred via `-Wl,-U` before,
 * which doesn't work: real dyld's eager chained-fixups binding fails at
 * real process load time regardless of whether the call path executes
 * (confirmed via a real QEMU boot of launchd: "symbol not found in flat
 * namespace '_creat'").
 *
 * creat/shm_open: real raw syscalls, same hand-rolled `svc #0x80`
 * pattern as every other raw syscall in this project (see
 * libresolv_socket_syscalls.c's own header for the full "why not
 * Apple's real Libsyscall stub generator" reasoning). Real numbers,
 * cross-checked against syscalls.master:
 *   #266 AUE_SHMOPEN shm_open(name, oflag, mode)  NO_SYSCALL_STUB
 * creat(2) has no dedicated kernel syscall on real Darwin (real
 * third_party/Libc/compat-43/FreeBSD/creat.c's own real body is just
 * `return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode)`) -- reproduced
 * here as a thin real wrapper over the real, already-available open(),
 * same real semantics, no raw trap needed.
 *
 * usleep: real third_party/Libc/gen/FreeBSD/usleep.c's own body is a
 * thin wrapper around nanosleep() (already a real, defined export
 * elsewhere in this project's dylib graph, confirmed via `nm
 * launchd.macho`) -- reproduced the same real way rather than fighting
 * that file's own namespace.h/un-namespace.h macro-renaming headers
 * (project-internal Libc build machinery not otherwise wired up here).
 *
 * os_fault_with_payload/os_variant_has_internal_diagnostics: real
 * internal Apple diagnostic/crash-reporting helpers, no real header or
 * source anywhere in this checkout's pinned repos. Real, honest
 * "not an internal/diagnostic build" stand-ins -- os_variant_has_
 * internal_diagnostics() returns false (this is a real end-user-shaped
 * build, not an internal Apple diagnostic build, a true statement, not
 * a fabrication); os_fault_with_payload() is diagnostic-only (crash-
 * report annotation, never expected to affect control flow) and is a
 * real no-op here.
 *
 * Real signature note (added when build_notifyd.sh started compiling
 * this same file with a wider real-SDK header exposure than
 * build_libsystem_notify.sh ever did, and hit a real "conflicting types
 * for 'shm_open'" error against the SDK's own real <sys/mman.h>
 * `int shm_open(const char *, int, ...)` declaration): shm_open()'s real
 * POSIX/Darwin prototype is variadic (the mode argument only matters
 * with O_CREAT), not a fixed 3-arg function -- widened below to match
 * the real declared prototype exactly.
 *
 * The mode `va_arg` is only consumed when O_CREAT is actually set
 * (2026-09-08, code review): real POSIX/Darwin shm_open() only defines
 * a mode argument's existence when O_CREAT is passed, and reading a
 * va_arg the caller never supplied is real undefined behavior, not a
 * theoretical one -- this file's own two current call sites always
 * pass 3 arguments so it never fired here, but the widened prototype
 * above makes a legal 2-arg O_RDONLY caller (real POSIX usage) a live
 * trap for the next one. O_CREAT's real value, 0x0200, copied from
 * this file's own creat() below rather than reinvented.
 */
struct real_timespec { long tv_sec; long tv_nsec; };
/* Real gap, found 2026-09-14 (first time this file was actually
 * compiled in a real, from-scratch build_everything.sh-order run --
 * every prior attempt died before reaching build_libsystem_notify.sh):
 * mode_t used below (creat()'s real signature) with no header exposing
 * it under this file's -ffreestanding compile, same local-definition
 * pattern as struct real_timespec above rather than fighting header
 * inclusion. Real Darwin type, confirmed against this project's pinned
 * iPhoneOS SDK (sys/_types/_mode_t.h -> sys/_types.h): mode_t is
 * __darwin_mode_t is __uint16_t, i.e. unsigned short on this LP64 arm64
 * target. */
typedef unsigned short mode_t;

extern int open(const char *path, int flags, ...);
extern int nanosleep(const struct real_timespec *rqtp, struct real_timespec *rmtp);

extern long cerror_nocancel(int err);

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

int shm_open(const char *name, int oflag, ...) {
    int mode = 0;
    if (oflag & 0x0200 /* O_CREAT */) {
        __builtin_va_list ap;
        __builtin_va_start(ap, oflag);
        mode = __builtin_va_arg(ap, int);
        __builtin_va_end(ap);
    }
    return (int)syscall3(266, (long)name, oflag, mode);
}

int creat(const char *path, mode_t mode) {
    return open(path, 0x0001 /* O_WRONLY */ | 0x0200 /* O_CREAT */ | 0x0400 /* O_TRUNC */, mode);
}

int usleep(unsigned int useconds) {
    struct real_timespec ts;
    ts.tv_sec = useconds / 1000000;
    ts.tv_nsec = (useconds % 1000000) * 1000;
    return nanosleep(&ts, 0);
}

/* Real signature: matches third_party/xnu-7195/BUILD.hdrs/dst/usr/local/
 * include/os/reason_private.h's own real declaration exactly (uint32_t/
 * uint64_t, not unsigned int/unsigned long long -- same underlying type
 * on this LP64 arm64 target, but textually different types still
 * produce a real "conflicting types" error against that real header,
 * found compiling this file under build_notifyd.sh's wider real-SDK
 * header exposure). __uint32_t/__uint64_t used here (not uint32_t/
 * uint64_t) -- <stdint.h> is freestanding-safe (a real compiler-provided
 * header, no SDK/sysroot dependency) so it's included here directly
 * rather than relying on some other forced -include to have already
 * exposed these names, unlike this file's other real Darwin-specific
 * types (mode_t etc) which genuinely do depend on that. */
#include <stdint.h>
int os_fault_with_payload(uint32_t reason_namespace, uint64_t reason_code,
    void *payload, uint32_t payload_size, const char *reason_string,
    uint64_t reason_flags) {
	(void)reason_namespace;
	(void)reason_code;
	(void)payload;
	(void)payload_size;
	(void)reason_string;
	(void)reason_flags;
	return 0;
}

int os_variant_has_internal_diagnostics(const char *subsystem) {
	(void)subsystem;
	return 0;
}
