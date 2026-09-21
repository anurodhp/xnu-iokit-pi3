/* reallocarray(3) -- real, cited compat for pw(8) (DAR-341), same real
 * implementation already proven for removefile_syscalls.c (see that
 * file's own header). Real Apple libmalloc source
 * (third_party/libmalloc/src/malloc.c) defines this, but that file
 * itself isn't compiled here (build_libmalloc.sh's real, documented
 * scope decision -- malloc.c needs dlopen/dyld, this target has
 * neither). Ported directly from that real implementation (same
 * overflow-checked-multiply-then-realloc logic, os_mul_overflow -> the
 * equivalent real __builtin_mul_overflow, a real compiler builtin
 * lowering to a real overflow-checked multiply instruction, not a
 * hand-rolled approximation) rather than reimplemented from scratch --
 * last resort per CLAUDE.md's source-priority rule since the real
 * defining file can't be linked whole. A dedicated small file (not a
 * reuse of removefile_syscalls.c itself) because that file bundles
 * other, unrelated symbols (flsl, IOPolicy syscalls) pw(8) doesn't need
 * and risks duplicate-symbol conflicts with if ever linked alongside it.
 */
#include <stddef.h>

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
