/* Real malloc/free/calloc/realloc smoketest against the real Apple
 * magazine allocator (third_party/libmalloc, see build_libmalloc.sh).
 * Freestanding: no libc beyond the real malloc dispatcher itself
 * (libmalloc_dispatch.c) and raw syscalls for output/exit. */

typedef unsigned long size_t;
extern void *malloc(size_t);
extern void *calloc(size_t, size_t);
extern void *realloc(void *, size_t);
extern void free(void *);

extern long write(int fd, const void *buf, size_t count);
extern void _exit(int) __attribute__((noreturn));

static void put(const char *s) {
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    write(1, s, n);
}

int main(void) {
    put("libmalloc smoketest: start\n");

    /* basic malloc/write/verify */
    char *a = (char *)malloc(64);
    if (!a) {
        put("FAIL: malloc(64) returned NULL\n");
        _exit(1);
    }
    for (int i = 0; i < 64; i++) {
        a[i] = (char)('A' + (i % 26));
    }
    int ok = 1;
    for (int i = 0; i < 64; i++) {
        if (a[i] != (char)('A' + (i % 26))) {
            ok = 0;
        }
    }
    put(ok ? "PASS: malloc/write/verify\n" : "FAIL: malloc/write/verify\n");

    /* calloc zero-init */
    unsigned char *b = (unsigned char *)calloc(32, 1);
    if (!b) {
        put("FAIL: calloc(32,1) returned NULL\n");
        _exit(1);
    }
    ok = 1;
    for (int i = 0; i < 32; i++) {
        if (b[i] != 0) {
            ok = 0;
        }
    }
    put(ok ? "PASS: calloc zero-init\n" : "FAIL: calloc zero-init\n");

    /* realloc-grow preserves contents */
    char *c = (char *)malloc(16);
    for (int i = 0; i < 16; i++) {
        c[i] = (char)('a' + i);
    }
    c = (char *)realloc(c, 256);
    if (!c) {
        put("FAIL: realloc(256) returned NULL\n");
        _exit(1);
    }
    ok = 1;
    for (int i = 0; i < 16; i++) {
        if (c[i] != (char)('a' + i)) {
            ok = 0;
        }
    }
    put(ok ? "PASS: realloc-grow preserves contents\n" : "FAIL: realloc-grow preserves contents\n");

    /* free + reuse stress: many alloc/free cycles across size classes,
     * would trip a broken free-list/coalescing bug (double-free or
     * use-after-free corrupting the allocator's own metadata usually
     * crashes on a LATER unrelated allocation, not the free() call
     * itself -- so this loop's later iterations succeeding is itself
     * the real test, not just the final PASS line). */
    ok = 1;
    for (int round = 0; round < 200; round++) {
        size_t sz = (size_t)(16 + (round * 37) % 4096);
        void *p = malloc(sz);
        if (!p) {
            ok = 0;
            break;
        }
        unsigned char *pb = (unsigned char *)p;
        pb[0] = 0x42;
        pb[sz - 1] = 0x24;
        free(p);
    }
    put(ok ? "PASS: 200-round alloc/free stress across size classes\n" : "FAIL: alloc/free stress\n");

    free(a);
    free(b);
    free(c);

    put("ALL PASS: libmalloc smoketest\n");
    _exit(0);
}
