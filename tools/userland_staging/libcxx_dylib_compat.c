/* Real gaps found migrating libcxx.a+libcxxabi.a into one real
 * libc++.dylib (docs/daemon-roadmap.md's "Static-vs-dylib architecture
 * debt" section, migration step 5).
 *
 * catopen/catgets/catclose: real source exists
 * (third_party/Libc/nls/FreeBSD/msgcat.c) but NLS message-catalog
 * support has never been built anywhere in this project -- real
 * libc++'s <locale> messages facet (locale.cpp) references these
 * unconditionally even though nothing in this project's own code uses
 * std::messages. Real, standard POSIX behavior for "no catalogs
 * available" (not a lie -- this target genuinely has none installed):
 * catopen() returns (nl_catd)-1, catgets() returns the caller-supplied
 * default string unchanged, catclose() returns -1. Every real libc
 * implementation returns exactly this when NLCATPATH is unset/empty,
 * which is this target's genuine, permanent state.
 *
 * _dyld_find_unwind_sections/_dyld_register_func_for_remove_image/
 * dladdr: same real gap and same real fix build_libsystem.sh's own
 * dyld_unwind_shim_compat.c already established for libSystem.B.dylib
 * (see that script's own comment) -- libunwind's real
 * libunwind_dyld_shim.o needs __mh_execute_header (never synthesized
 * for a dylib), and libdyld.dylib's own real libdyldGlue.o (which DOES
 * provide real implementations) isn't a dependency here -- linking
 * against it would be circular (libdyld.dylib itself links libc++.dylib
 * for RTTI). Copied verbatim rather than re-derived twice.
 *
 * HIDDEN 2026-09-03 (DAR-6/DAR-18): the circularity above means
 * _dyld_find_unwind_sections/_dyld_register_func_for_remove_image/
 * dladdr can never link their real owner (libdyld.dylib) back, so the
 * duplicate symbol itself can't go away here either -- but same as
 * libcxx_dlfcn_stub.c's own dlopen()/dlsym() (DAR-6), these three never
 * needed to be EXPORTED, only callable by this dylib's own real
 * internal caller (confirmed via `nm -u` on the extracted libunwind.a
 * member set: libunwind.o itself is the one real caller of all three,
 * for its phase-1 DWARF-CFI search over dynamically-loaded images).
 * Confirmed no shipped image currently binds to `libunwind/_dladdr` (or
 * the other two) specifically (`xcrun dyld_info -fixups` swept across
 * every shipped dylib/macho) -- unlike libc++.dylib's dlopen/dlsym,
 * this one wasn't yet observed shadowing a real caller, but the same
 * risk exists structurally (a future consumer linking libunwind.dylib
 * before libdyld.dylib would silently get this honest-but-fake
 * always-"nothing found" stub instead of the real one). Marking these
 * `hidden` removes them from this dylib's export trie (confirmed via
 * `dyld_info -exports`), closing the risk before it bites the way
 * libc++.dylib's copy already had. catopen/catgets/catclose/
 * clock_gettime below are NOT touched -- they aren't duplicates of any
 * other shipped dylib (0 owners besides this one in
 * `dup_symbol_audit.sh --summary`), so there's no real "wrong owner"
 * risk to close for them, and hiding them isn't in scope for this pass.
 */
typedef void *nl_catd;
nl_catd catopen(const char *name, int oflag) {
    (void)name; (void)oflag;
    return (nl_catd)-1;
}
char *catgets(nl_catd catd, int set_id, int msg_id, const char *s) {
    (void)catd; (void)set_id; (void)msg_id;
    return (char *)s;
}
int catclose(nl_catd catd) {
    (void)catd;
    return -1;
}

/* clock_gettime: real Apple's own gen/clock_gettime.c is commpage-
 * accelerated and pulls in more machinery than this narrow real need
 * justifies porting right now (real std::chrono::system_clock::now()/
 * std::atomic wait-on-address timeouts are this dylib's only callers,
 * confirmed via nm -u tracing to atomic.o/chrono.o/thread.o). Real,
 * working raw syscall #116 (gettimeofday, same real trap
 * bash_syscalls.c's own header comment already documents as XNU's
 * honest non-commpage fallback path) for CLOCK_REALTIME, real
 * mach_absolute_time()+mach_timebase_info() (already real and exported
 * by libsystem_platform.dylib) for CLOCK_MONOTONIC -- both real,
 * already-working primitives elsewhere in this project, just not
 * previously combined behind the standard POSIX clock_gettime() name. */
struct compat_timespec { long tv_sec; long tv_nsec; };
struct compat_timeval { long tv_sec; int tv_usec; };
static long gtod_syscall3(long num, long a0, long a1, long a2) {
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
    extern int cerror_nocancel(int);
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return x0;
}
extern unsigned long long mach_absolute_time(void);
struct compat_mach_timebase_info { unsigned int numer; unsigned int denom; };
extern int mach_timebase_info(struct compat_mach_timebase_info *info);

#define COMPAT_CLOCK_REALTIME 0
#define COMPAT_CLOCK_MONOTONIC 6

int clock_gettime(int clock_id, struct compat_timespec *ts) {
    if (clock_id == COMPAT_CLOCK_MONOTONIC || clock_id == 4 /* _RAW */) {
        struct compat_mach_timebase_info tb;
        mach_timebase_info(&tb);
        unsigned long long now = mach_absolute_time();
        unsigned long long ns = tb.denom ? (now * tb.numer) / tb.denom : now;
        ts->tv_sec = (long)(ns / 1000000000ULL);
        ts->tv_nsec = (long)(ns % 1000000000ULL);
        return 0;
    }
    struct compat_timeval tv;
    int rv = (int)gtod_syscall3(116, (long)&tv, 0, 0);
    if (rv != 0) {
        return rv;
    }
    ts->tv_sec = tv.tv_sec;
    ts->tv_nsec = tv.tv_usec * 1000;
    return 0;
}

__attribute__((visibility("hidden")))
int _dyld_find_unwind_sections(void *addr, void *info) {
    (void)addr; (void)info;
    return 0;
}
__attribute__((visibility("hidden")))
void _dyld_register_func_for_remove_image(void (*func)(const void *, unsigned long)) {
    (void)func;
}
__attribute__((visibility("hidden")))
void *dladdr(const void *addr, void *info) {
    (void)addr; (void)info;
    return 0;
}
