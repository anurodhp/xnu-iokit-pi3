/* Debug repro for the Eighteenth pass's real bash SIGSEGV (docs/roadmap.md):
 * Libc/gen/get_compat.c's compat_mode() has its "mode" stack-local
 * corrupted across a real pthread_once() call, reproducibly, ONLY at real
 * bash's deep call depth -- pthread_smoketest.c's own pthread_once() call
 * (shallow, called right from main()) never showed this. This isolates
 * "does deep recursion + pthread_once alone reproduce it" from every other
 * bash-specific variable (parser, autoconf compat shims, etc).
 *
 * Mirrors compat_mode()'s exact real shape: a function taking a pointer
 * argument, spilling it to its own stack frame, calling real pthread_once()
 * with a real once-guarded init routine, then reloading and using the
 * argument afterward -- the exact "save / call / reload" pattern that
 * corrupts in real bash.
 */
#include "internal.h"

extern int pthread_main_thread_bootstrap(void);
extern long write(int fd, const void *buf, unsigned long n);
extern void _exit(int rc) __attribute__((noreturn));
void exit(int code) { _exit(code); } /* Csu's crt.c calls exit(), not _exit() */

static void put(const char *s) {
    long len = 0;
    while (s[len]) len++;
    register long x0 asm("x0") = 1;
    register long x1 asm("x1") = (long)s;
    register long x2 asm("x2") = len;
    register long x16 asm("x16") = 4; /* write */
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
}

static void putnum(long n) {
    char buf[24];
    int i = 23;
    buf[i--] = '\n';
    if (n == 0) buf[i--] = '0';
    while (n > 0) { buf[i--] = (char)('0' + (n % 10)); n /= 10; }
    write(1, &buf[i + 1], (unsigned long)(23 - i));
}

static void die(const char *s) {
    put(s);
    register long x0 asm("x0") = 1;
    register long x16 asm("x16") = 1; /* exit */
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x16) : "memory", "cc");
    __builtin_unreachable();
}

static pthread_once_t threadsafe = PTHREAD_ONCE_INIT;
static void once_init(void) {
    /* Deliberately empty, like get_compat.c's check_env_var() when
     * COMMAND_MODE is unset (getenv() always returns NULL on this
     * target -- libc_locale_env_stubs.c) -- isolates pthread_once()'s
     * own mechanics from anything check_env_var() itself does. */
}

/* Exact mirror of compat_mode()'s real shape: real pointer arg, real
 * save-call-reload pattern, real strcasecmp on the reloaded value. */
static int compat_mode_repro(const char *mode) {
    pthread_once(&threadsafe, once_init);
    /* mode is a real function argument -- at -O0 the compiler spills it
     * to this function's own stack frame on entry and reloads it here,
     * exactly like real get_compat.c's compat_mode(). */
    if (!mode) {
        die("FAIL: mode corrupted to NULL after pthread_once\n");
    }
    if (mode[0] != 'u' || mode[1] != 'n') {
        put("FAIL: mode corrupted to garbage after pthread_once: ");
        put(mode);
        put("\n");
        die("FAIL: mode was corrupted\n");
    }
    return 0;
}

#define TARGET_DEPTH 1200

static long depth = 0;

static void recurse(void) {
    volatile char buf[4096];
    buf[0] = 0;
    buf[4095] = 0;
    depth++;
    if (depth >= TARGET_DEPTH) {
        put("reached target depth, calling compat_mode_repro...\n");
        putnum(depth);
        compat_mode_repro("unix2003");
        put("PASS: mode survived pthread_once at deep stack\n");
        return;
    }
    recurse();
}

int main(void) {
    put("pthread_once_deep_repro: start\n");
    if (!pthread_main_thread_bootstrap()) {
        die("FAIL: bootstrap\n");
    }
    put("PASS: bootstrap\n");

    /* Shallow call first -- matches pthread_smoketest.c's own passing
     * case, confirms this repro's baseline is sane before going deep. */
    compat_mode_repro("unix2003");
    put("PASS: shallow pthread_once call survived\n");

    recurse();
    put("ALL PASS: pthread_once_deep_repro\n");
    _exit(0);
    return 0;
}
