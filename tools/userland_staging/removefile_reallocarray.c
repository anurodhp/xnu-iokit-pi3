/* removefile_reallocarray.c: reallocarray() as its OWN libremovefile.a
 * member (split out of removefile_syscalls.c, DAR-429). Apple parity:
 * libmalloc owns reallocarray -- third_party/libmalloc/src/malloc.c:2022-2040,
 * carried here by libmalloc.a(libmalloc_dispatch.o) (DAR-424,
 * libmalloc_dispatch.c:202-226). This copy exists only for static
 * consumers of libremovefile.a that link no libmalloc.a (launchd's
 * libc extras, libremovefile.dylib); ld64 pulls an archive member only for
 * an undefined symbol, so links that have libmalloc.a ahead of
 * libremovefile.a never load it.
 */
typedef unsigned long size_t;
/* reallocarray: real Apple libmalloc source (third_party/libmalloc/src/
 * malloc.c) defines this, but that file itself isn't compiled here
 * (build_libmalloc.sh's real, documented scope decision - malloc.c needs
 * dlopen/dyld, this target has neither). Ported directly from that real
 * implementation (same overflow-checked-multiply-then-realloc logic,
 * os_mul_overflow -> the equivalent real __builtin_mul_overflow, a real
 * compiler builtin lowering to a real overflow-checked multiply
 * instruction, not a hand-rolled approximation) rather than reimplemented
 * from scratch - last resort per CLAUDE.md's source-priority rule since
 * the real defining file can't be linked whole. */
extern void *realloc(void *ptr, size_t size);
extern int *__error(void);
#define ENOMEM 12

void *reallocarray(void *in_ptr, size_t nmemb, size_t size) {
    size_t alloc_size;
    if (__builtin_mul_overflow(nmemb, size, &alloc_size)) {
        *__error() = ENOMEM;
        return (void *)0;
    }
    return realloc(in_ptr, alloc_size);
}

