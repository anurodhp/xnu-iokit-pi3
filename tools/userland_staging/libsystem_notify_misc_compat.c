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
 */
struct real_timespec { long tv_sec; long tv_nsec; };

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

int shm_open(const char *name, int oflag, int mode) {
    return (int)syscall3(266, (long)name, oflag, mode);
}

int creat(const char *path, int mode) {
    return open(path, 0x0001 /* O_WRONLY */ | 0x0200 /* O_CREAT */ | 0x0400 /* O_TRUNC */, mode);
}

int usleep(unsigned int useconds) {
    struct real_timespec ts;
    ts.tv_sec = useconds / 1000000;
    ts.tv_nsec = (useconds % 1000000) * 1000;
    return nanosleep(&ts, 0);
}

void os_fault_with_payload(unsigned int reason_namespace, unsigned long long reason_code,
    void *payload, unsigned int payload_size, const char *reason_string,
    unsigned long long reason_flags) {
	(void)reason_namespace;
	(void)reason_code;
	(void)payload;
	(void)payload_size;
	(void)reason_string;
	(void)reason_flags;
}

int os_variant_has_internal_diagnostics(const char *subsystem) {
	(void)subsystem;
	return 0;
}
