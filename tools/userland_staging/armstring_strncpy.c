/* strncpy and stpncpy over the fast primitives (build_armstring.sh compiles this twice, -DWANT_STRNCPY and -DWANT_STPNCPY, into the libc members strncpy.o and stpncpy.o; the libc ones are byte loops, FreeBSD's too).
 * Same semantics as the standard: copy at most n bytes of src, then pad dst with NULs up to n; stpncpy returns a pointer to the first
 * NUL written (or dst + n). The operands must not overlap (restrict), as the standard requires. Written for this project; MIT.
 * strnlen, memcpy and memset are the Arm routines in the same library. */
#include <stddef.h>

size_t strnlen(const char *, size_t);
void *memcpy(void *__restrict, const void *__restrict, size_t);
void *memset(void *, int, size_t);

#ifdef WANT_STRNCPY
char *strncpy(char *__restrict dst, const char *__restrict src, size_t n)
{
	size_t l = strnlen(src, n);

	memcpy(dst, src, l);
	if (l < n)
		memset(dst + l, 0, n - l);
	return dst;
}
#endif

#ifdef WANT_STPNCPY
char *stpncpy(char *__restrict dst, const char *__restrict src, size_t n)
{
	size_t l = strnlen(src, n);

	memcpy(dst, src, l);
	if (l < n)
		memset(dst + l, 0, n - l);
	return dst + l;
}
#endif
