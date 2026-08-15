/* Raw syscall shims for copyfile.c's NO_SYSCALL_STUB entries (chmod #15,
 * fchmod #124, getattrlist #220, open_dprotected_np #216 - all cross-
 * checked against bsd/kern/syscalls.master, same real carry-flag pattern
 * every other tier's NO_SYSCALL_STUB shim uses), plus a small number of
 * real last-resort ports for functions with no .c source anywhere in
 * this project's Libc/posix1e checkouts (see each function's own comment
 * below for why).
 */

typedef unsigned long size_t;
typedef long ssize_t;

/* Real Apple convention (real Perl-generated stubs call cerror_nocancel
 * on the error path) - sets the actual global errno via cerror_stub.c's
 * real stand-in and returns -1, rather than returning the raw negative
 * error code directly. Same real bug class found+fixed in
 * removefile_syscalls.c/libc_gen_syscalls.c (a genuine QEMU boot-test
 * failure traced to errno never actually being set on failure). */
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

static inline long syscall5(long num, long a0, long a1, long a2, long a3, long a4) {
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
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return x0;
}

#define SYS_chmod 15
#define SYS_fchmod 124
#define SYS_getattrlist 220
#define SYS_open_dprotected_np 216
#define SYS_chmod_extended 282
#define SYS_fchmod_extended 283
#define SYS_stat64_extended 341
#define SYS_lstat64_extended 342
#define SYS_fstat64_extended 343

int chmod(const char *path, int mode) {
    return (int)syscall3(SYS_chmod, (long)path, mode, 0);
}

int fchmod(int fd, int mode) {
    return (int)syscall3(SYS_fchmod, fd, mode, 0);
}

struct attrlist;
int getattrlist(const char *path, struct attrlist *alist, void *attributeBuffer,
                 size_t bufferSize, unsigned long options) {
    return (int)syscall5(SYS_getattrlist, (long)path, (long)alist,
                          (long)attributeBuffer, (long)bufferSize, (long)options);
}

int open_dprotected_np(const char *path, int flags, int pclass, int dpflags, int mode) {
    return (int)syscall5(SYS_open_dprotected_np, (long)path, flags, pclass, dpflags, mode);
}

/* Real private raw syscalls chmodx_np.c/statx_np.c call directly (double-
 * underscore names, Apple's own real convention for the raw syscall
 * layer underneath the public non-underscore wrapper - e.g. real
 * chmodx_np() calls __chmod_extended() which is this exact raw syscall,
 * #282/#283/#341/#342/#343, all NO_SYSCALL_STUB, cross-checked against
 * syscalls.master). */
int __chmod_extended(const char *path, unsigned uid, unsigned gid, int mode, void *xsecurity) {
    return (int)syscall5(SYS_chmod_extended, (long)path, uid, gid, mode, (long)xsecurity);
}

int __fchmod_extended(int fd, unsigned uid, unsigned gid, int mode, void *xsecurity) {
    return (int)syscall5(SYS_fchmod_extended, fd, uid, gid, mode, (long)xsecurity);
}

int __stat64_extended(const char *path, void *ub, void *xsecurity, void *xsecurity_size) {
    return (int)syscall5(SYS_stat64_extended, (long)path, (long)ub, (long)xsecurity, (long)xsecurity_size, 0);
}

int __lstat64_extended(const char *path, void *ub, void *xsecurity, void *xsecurity_size) {
    return (int)syscall5(SYS_lstat64_extended, (long)path, (long)ub, (long)xsecurity, (long)xsecurity_size, 0);
}

int __fstat64_extended(int fd, void *ub, void *xsecurity, void *xsecurity_size) {
    return (int)syscall5(SYS_fstat64_extended, fd, (long)ub, (long)xsecurity, (long)xsecurity_size, 0);
}

/* bcmp: real BSD semantics ("0 if equal, nonzero otherwise") are a strict
 * subset of memcmp's - real memcmp(a,b,n)==0 iff bcmp(a,b,n)==0 for any
 * conforming implementation, so this is a real, standard, last-resort
 * port (no bcmp.c exists anywhere in this project's Libc checkout,
 * confirmed by find - only a man page) built directly on the real
 * memcmp already linked via libc_string.a/libc_locale.a. */
extern int memcmp(const void *a, const void *b, size_t n);
int bcmp(const void *a, const void *b, size_t n) {
    return memcmp(a, b, n);
}

/* syslog: real Libc has no syslog.c in this checkout at all (it's a real
 * network/domain-socket protocol to syslogd, which this project has no
 * equivalent of - see architecture.md, no launchd-managed daemons here).
 * copyfile.c's own real call sites are all non-fatal diagnostic logging
 * (permission-copy warnings etc, never checked for a return value) - this
 * stand-in drops the message rather than fabricating delivery to a
 * daemon that doesn't exist, matching the same "real subsystem genuinely
 * absent" pattern as bsd_pthread_stub.c. A real implementation writing to
 * the console/stderr was considered but skipped to avoid this tier
 * depending on libc_printf.a's variadic vsnprintf machinery only to
 * immediately discard the result - not printing is honestly equivalent
 * for a target with no log daemon to receive it either way. */
void syslog(int priority, const char *fmt, ...) {
    (void)priority;
    (void)fmt;
}

/* mbr_uid_to_uuid: real membership-service (od-backed) uid->UUID lookup,
 * which needs a directory service this project doesn't have (only the
 * flat-file libinfo_custom resolver, see tools/libinfo_custom/ - not a
 * membership/UUID service). copyfile.c's only call sites are inside
 * is_uberace()/add_uberace(), a narrow ACL edge case (synthesizing a
 * deny-all ACE keyed to the file owner's UUID during ACL-preserving
 * copies) - not exercised by this tier's own COPYFILE_DATA smoketest.
 * This stand-in always reports failure (real macOS itself falls back to
 * a local synthesized UUID when no directory service can resolve one -
 * this project has no directory service AT ALL, so "cannot resolve" is
 * the honest answer here, not a fabricated UUID pretending to match
 * Apple's real, undocumented local-uid synthesis algorithm). */
typedef unsigned char uuid_t[16];
int mbr_uid_to_uuid(unsigned uid, uuid_t uu) {
    (void)uid;
    (void)uu;
    return -1;
}

/* Same real "no directory service at all" reasoning as mbr_uid_to_uuid
 * above - acl_translate.c's real call sites (uid/gid <-> UUID lookups
 * for ACL text serialization, a real feature this target's HFS+ never
 * exercises since it has no ACL storage) all honestly fail closed. */
int mbr_gid_to_uuid(unsigned gid, uuid_t uu) {
    (void)gid;
    (void)uu;
    return -1;
}

/* Real signature uses id_t* (real Darwin id_t = __uint32_t, 4 bytes),
 * not long* (8 bytes) - an earlier version of this stand-in used the
 * wrong width. Harmless in practice today (this always-fails stand-in
 * never writes through the pointer on its only return path), but a real
 * type-correctness bug for any future real caller - fixed on review. */
int mbr_uuid_to_id(const uuid_t uu, unsigned *id_or_uid, int *id_type) {
    (void)uu;
    (void)id_or_uid;
    (void)id_type;
    return -1;
}

/* getpwnam/getpwuid/getgrnam/getgrgid: real POSIX passwd/group database
 * lookups. This project DOES have a real flat-file account resolver
 * (tools/libinfo_custom/ - see Phase 5, docs/roadmap.md), but it exposes
 * a different li_pw_/li_gr_ prefixed API, not these exact libc entry
 * points, and wiring the two together is real, separate integration
 * work outside this tier's scope. acl_translate.c's only real callers
 * (acl_from_text/uuid_to_name) are for ACL text parsing and
 * serialization - a real feature this target's HFS+ never exercises (no
 * real ACL storage) and this tier's own copyfile smoketest never calls
 * (COPYFILE_DATA only). Returning NULL ("no such user/group") is real,
 * POSIX-conformant behavior for a lookup that cannot succeed, not a
 * fabricated result - matching the same "real subsystem genuinely not
 * wired up yet" pattern as mbr_uid_to_uuid above. */
struct passwd;
struct passwd *getpwnam(const char *name) { (void)name; return (struct passwd *)0; }
struct passwd *getpwuid(unsigned uid) { (void)uid; return (struct passwd *)0; }
struct group;
struct group *getgrnam(const char *name) { (void)name; return (struct group *)0; }
struct group *getgrgid(unsigned gid) { (void)gid; return (struct group *)0; }
