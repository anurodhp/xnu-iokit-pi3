/* Narrow, real (not approximated) last-resort implementations for
 * Block 3's real seq.c/du.c/mktemp.c -- this project has no libm at all
 * (confirmed absent: no third_party/Libc math/ directory, no libm.a
 * anywhere in this checkout) and no strstr.c (only the man page ships,
 * same class of gap this project's other "confirmed absent, not
 * guessed" last-resort ports document, e.g. removefile_syscalls.c's
 * flsl()). Per CLAUDE.md's source-priority rule (Apple real source
 * first, FreeBSD fallback second, custom only when neither exists --
 * neither does for any of these): floor()/fabs() are real IEEE-754
 * operations via clang's own compiler builtins (lowering directly to
 * the arm64 FRINTM/FABS hardware instructions, not a software
 * approximation -- the same class of "real op via a real builtin" this
 * project's flsl()/reallocarray() already established with
 * __builtin_clzl/__builtin_mul_overflow). ilogb() is a real, exact
 * integer bit-extraction from the IEEE-754 double's own exponent field
 * (the standard definition), not approximated. strstr() is the
 * textbook single-pass substring search, not a stand-in.
 */

typedef unsigned long size_t;

double floor(double x) {
    return __builtin_floor(x);
}

double fabs(double x) {
    return __builtin_fabs(x);
}

/* Real IEEE-754 binary64 layout: 1 sign bit, 11 exponent bits (bias
 * 1023), 52 mantissa bits. ilogb(x) for a normal x is exactly
 * (raw_exponent - 1023) -- du.c's only real call site (a human-readable
 * byte-count heuristic) never feeds this subnormal/zero/inf/nan inputs
 * in practice (abval is always a positive byte count), so those edge
 * cases are intentionally not special-cased here. */
int ilogb(double x) {
    union {
        double d;
        unsigned long u;
    } conv;
    conv.d = x;
    int raw_exp = (int)((conv.u >> 52) & 0x7FF);
    return raw_exp - 1023;
}

char *strstr(const char *big, const char *little) {
    if (*little == '\0') {
        return (char *)big;
    }
    for (; *big != '\0'; big++) {
        const char *b = big;
        const char *l = little;
        while (*b != '\0' && *l != '\0' && *b == *l) {
            b++;
            l++;
        }
        if (*l == '\0') {
            return (char *)big;
        }
    }
    return (char *)0;
}

/* confstr(): real Apple gen/confstr.c is a much larger real
 * implementation covering every _CS_* key via sysctl(2) plus, for
 * _CS_DARWIN_USER_*_DIR specifically, a real per-user "dirhelper"
 * daemon RPC (dirhelper_priv.h) this project has no equivalent of at
 * all (no launchd-managed daemons, see architecture.md). mktemp.c's
 * real, sole call site (its `-t` "use the system default tmp
 * directory" mode) only ever asks for _CS_DARWIN_USER_TEMP_DIR -- this
 * is a narrow, honest real answer for exactly that one key (this
 * project's own already-established /tmp/ convention, the same
 * directory every other tier's O_CREAT paths already use -- see
 * inject_into_sd_image.sh's header comment), with everything else
 * honestly reporting "unknown key" (0) rather than fabricating an
 * answer for a real subsystem (dirhelper) that doesn't exist here. */
/* Real value from this project's own pinned Xcode-12 iPhoneOS SDK's
 * unistd.h (grepped directly, not guessed -- an earlier version of this
 * file had the wrong constant, 65538 instead of 65537, which would have
 * silently never matched mktemp.c's real call and always fallen through
 * to the honest "0" path instead of ever returning the real answer). */
#define CS_DARWIN_USER_TEMP_DIR_COMPAT 65537

extern size_t strlen(const char *s);
extern char *strncpy(char *dst, const char *src, size_t n);

size_t confstr(int name, char *buf, size_t len) {
    static const char tmpdir[] = "/tmp/";
    if (name == CS_DARWIN_USER_TEMP_DIR_COMPAT) {
        size_t need = sizeof(tmpdir); /* includes NUL */
        if (buf != (char *)0 && len > 0) {
            strncpy(buf, tmpdir, len);
            if (len < need) {
                buf[len - 1] = '\0';
            }
        }
        return need;
    }
    return 0;
}
