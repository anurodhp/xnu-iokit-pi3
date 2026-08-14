/* Minimal, real (not general-purpose) stdio FILE* subset - exactly the 5
 * functions third_party/Libc/locale/FreeBSD/{collate,rune,setrunelocale}.c
 * actually call (fdopen/fread/fileno/rewind/fclose), confirmed opaque
 * (no caller pokes at FILE's internal fields directly - grep-checked
 * before writing this, not assumed) so this project's own minimal FILE
 * struct doesn't need to match real Apple/FreeBSD stdio's internal
 * layout at all, only the public function contracts.
 *
 * Precisely scoped: this is Phase 4's real stdio wall, narrowed down
 * from "real stdio" (fopen/fprintf/etc, a much bigger separate
 * subsystem, genuinely deferred - see docs/roadmap.md) to just the
 * read-only, fd-wrapping subset the locale-file-loading code path
 * actually needs. Not a foundation for general-purpose I/O.
 *
 * Deliberately uses `void *` instead of the real `FILE *` type
 * throughout this file's own signatures: real callers (collate.c etc.)
 * get their `fread`/`fclose`/etc. prototypes from real Apple stdio.h,
 * which declares its own `FILE` as an opaque `struct __sFILE *` this
 * project doesn't define or need to. Redeclaring `FILE`/`struct __sFILE`
 * here to match exactly would risk a real typedef conflict for zero
 * benefit, since nothing outside this file ever dereferences the
 * pointer's contents - only the pointer VALUE crosses the boundary
 * (ABI-identical regardless of the C-level type name each translation
 * unit uses for it, since it's always just a pointer). Function NAMES
 * and parameter/return type WIDTHS match the real signatures exactly
 * (verified against third_party/Libc/include/stdio.h's real
 * declarations - only the FILE-vs-void* spelling differs, not the ABI).
 *
 * Built on this project's own raw syscalls (see libc_locale_syscalls.c
 * for _read/_close's real syscall numbers, cross-checked against
 * syscalls.master) plus a real lseek, added here since rewind() needs it.
 */

typedef unsigned long size_t;
typedef long ssize_t;
typedef long off_t;

extern ssize_t _read(int fd, void *buf, size_t n);
extern int _close(int fd);
extern void *malloc(size_t);
extern void free(void *);

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
        return -x0;
    }
    return x0;
}

/* lseek (bsd/kern/syscalls.master #199, AUE_LSEEK) - real number,
 * cross-checked the same way every other syscall in this project is. */
#define SYS_lseek 199
#define SEEK_SET 0

static off_t sys_lseek(int fd, off_t offset, int whence) {
    return syscall3(SYS_lseek, fd, offset, whence);
}

struct __libc_locale_FILE {
    int fd;
    int eof;
    int error;
};

/* Only the "r"/"rb"-style read-only open real callers here ever use
 * (confirmed by grep: every fdopen() call site in this project's 113
 * locale files passes a mode string starting with 'r') - mode is
 * otherwise ignored, matching what this minimal subset actually needs
 * to support correctly rather than pretending to support write modes it
 * was never asked to. */
void *fdopen(int fd, const char *mode) {
    (void)mode;
    if (fd < 0) {
        return 0;
    }
    /* Placeholder allocator from compat_stubs.c - fine here, these
     * structs are tiny and short-lived (opened, read, closed within one
     * locale-file-load call). */
    struct __libc_locale_FILE *f = (struct __libc_locale_FILE *)malloc(sizeof(*f));
    if (!f) {
        return 0;
    }
    f->fd = fd;
    f->eof = 0;
    f->error = 0;
    return f;
}

size_t fread(void *ptr, size_t size, size_t nmemb, void *stream) {
    struct __libc_locale_FILE *f = (struct __libc_locale_FILE *)stream;
    if (!f || size == 0 || nmemb == 0) {
        return 0;
    }
    size_t total = size * nmemb; /* real callers here pass small, fixed
                                   * struct sizes - no overflow risk in
                                   * practice for this subset's real
                                   * callers, same reasoning already
                                   * applied to compat_stubs.c's calloc */
    unsigned char *dst = (unsigned char *)ptr;
    size_t got = 0;
    while (got < total) {
        ssize_t n = _read(f->fd, dst + got, total - got);
        if (n < 0) {
            f->error = 1;
            break;
        }
        if (n == 0) {
            f->eof = 1;
            break;
        }
        got += (size_t)n;
    }
    return got / size;
}

int fileno(void *stream) {
    struct __libc_locale_FILE *f = (struct __libc_locale_FILE *)stream;
    return f ? f->fd : -1;
}

/* real rewind() is void-returning and clears error/EOF state
 * unconditionally, even on a failed seek - matches real semantics
 * exactly rather than approximating them. */
void rewind(void *stream) {
    struct __libc_locale_FILE *f = (struct __libc_locale_FILE *)stream;
    if (!f) {
        return;
    }
    f->eof = 0;
    f->error = 0;
    sys_lseek(f->fd, 0, SEEK_SET);
}

int fclose(void *stream) {
    struct __libc_locale_FILE *f = (struct __libc_locale_FILE *)stream;
    if (!f) {
        return -1;
    }
    int r = _close(f->fd);
    free(f);
    return r;
}
