/* objc_throwdiag (DAR-450): logs every Objective-C exception throw.
 *   DYLD_INSERT_LIBRARIES=/usr/lib/objc_throwdiag.dylib <objc binary>
 * Interposes libobjc's objc_exception_throw (dyld __DATA,__interpose, which
 * dyld applies for inserted images) and prints the exception's class and a
 * 40-frame fp backtrace with dladdr symbols to fd 2 before re-throwing. Used
 * to find an exception thrown while objc4 holds a SideTable lock. Built by
 * build_crashdiag.sh. */
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>

extern void objc_exception_throw(void *exc) __attribute__((noreturn));
extern const char *object_getClassName(void *obj);

static void w(const char *s) {
    register long x0 asm("x0") = 2; register const char *x1 asm("x1") = s;
    register long x2 asm("x2") = (long)strlen(s); register long x16 asm("x16") = 4;
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
}
static void hex(uint64_t v) {
    char b[19]; b[0]='0'; b[1]='x';
    for (int i = 0; i < 16; i++) b[2+i] = "0123456789abcdef"[(v >> (60 - 4*i)) & 15];
    b[18] = 0; w(b);
}
static void sym(uint64_t a) {
    Dl_info di; w(" bt "); hex(a);
    if (dladdr((void *)a, &di)) {
        w(" "); w(di.dli_sname ? di.dli_sname : "?"); w("+"); hex(a - (uint64_t)di.dli_saddr);
    }
    w("\n");
}
static void traced_throw(void *exc) __attribute__((noreturn));
static void traced_throw(void *exc) {
    w("=== objc_throwdiag: throw "); w(exc ? object_getClassName(exc) : "nil"); w("\n");
    uint64_t fp = (uint64_t)__builtin_frame_address(0);
    for (int i = 0; i < 40 && fp && (fp & 7) == 0; i++) {
        uint64_t *f = (uint64_t *)fp; sym(f[1]); fp = f[0];
    }
    objc_exception_throw(exc);
}
__attribute__((used, section("__DATA,__interpose")))
static struct { const void *replacement, *original; } interposers[] = {
    { (const void *)traced_throw, (const void *)objc_exception_throw },
};
