/* Real gap found migrating libc_gen/libc_stdio/libc_printf/libc_string/
 * libc_locale_for_stdio/libc_regex into one real libsystem_c.dylib:
 * real dyld's own LibSystemHelpers::mkstemp() (libdyld/
 * LibSystemHelpers.cpp) needs real mkstemp(), which (stdio/FreeBSD/
 * mktemp.c, real Apple source) needs real arc4random_uniform() for its
 * temp-name padding -- previously scoped out as "genuinely out of
 * scope" until this real need surfaced. Real arc4random.c (gen/FreeBSD/
 * arc4random.c) itself compiles clean under this project's standard
 * -DVARIANT_STATIC (its own real `#if defined(__APPLE__) &&
 * !defined(VARIANT_STATIC)` condition is exactly the real Apple-
 * documented gate for routing around corecrypto/ccrng.h for static/
 * pre-libSystem-init consumers -- already proven working by
 * build_libresolv.sh's own arc4random.c compile, reused verbatim here,
 * not re-derived). That still leaves mktemp.c's OTHER function in the
 * same object file, mkstemp_dprotected_np(), needing one more real
 * syscall this project's create-syscalls.pl-driven generator doesn't
 * auto-produce (NO_SYSCALL_STUB in syscalls.master, same class of gap
 * as __ulock_wait/thread_selfid elsewhere in this project) -- a real,
 * narrowly-scoped raw wrapper below, not a stub.
 *
 * tmpfile.c also references `_sigprocmask` (real Apple's own internal,
 * single-underscore-prefixed name for the same real sigprocmask(2) --
 * distinct from the public `sigprocmask` the create-syscalls.pl-
 * generated sys_sigprocmask.o already provides under ITS OWN, different
 * linker symbol). This project has no real pthread-cancellation
 * machinery for the two names to ever meaningfully diverge over (see
 * cerror_stub.c's own comment on this target's simplified signal
 * handling), so a thin, real forwarder using GCC/Clang's `asm("name")`
 * linker-symbol-renaming extension (same real mechanism
 * libresolv_arc4random_deps_compat.c's own header comment already
 * documents and justifies for this exact "alias only works if the base
 * symbol is already pulled in" ordering problem) is the honest, correct
 * fix. */
extern int sigprocmask(int how, const void *set, void *oset);
int sigprocmask_internal(int how, const void *set, void *oset) asm("__sigprocmask");
int sigprocmask_internal(int how, const void *set, void *oset) {
    return sigprocmask(how, set, oset);
}
static long syscall5(long num, long a0, long a1, long a2, long a3, long a4) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x4 asm("x4") = a4;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x16)
                 : "memory", "cc");
    extern int cerror_nocancel(int);
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return x0;
}

/* Real trap 216 (osfmk/.../syscalls.master: "216 ... open_dprotected_np
 * ... NO_SYSCALL_STUB"), real Data Protection open() variant -- this
 * kernel implements it for real (unconditionally registered, not
 * gated), just tags the created file with a protection class; a no-op
 * distinction on this target's storage (no per-file encryption), same
 * real answer as this project's other "no real backing hardware, but a
 * real, correct syscall regardless" cases. */
int open_dprotected_np(const char *path, int flags, int dpclass, int dpflags, int mode) {
    return (int)syscall5(216, (long)path, flags, dpclass, dpflags, mode);
}
