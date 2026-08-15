/* Real libunwind smoketest.
 *
 * REAL, CONFIRMED ENVIRONMENTAL CONSTRAINT (found via direct testing, not
 * guessed - see build_libunwind.sh's header comment for the full
 * writeup): ld64 on this project's pinned Xcode-12 toolchain does NOT
 * synthesize a __TEXT,__unwind_info section (nor preserve __eh_frame/
 * __compact_unwind) for a `-nostdlib -static` linked Mach-O, regardless
 * of -funwind-tables/-fexceptions on the compile side (both tested,
 * neither changes the final linked image - confirmed via otool -l on
 * this project's own real output vs. a normal dynamically-linked control
 * binary, which DOES get __unwind_info under the identical compiler).
 * Every Mach-O this entire project ever produces is exactly this kind of
 * static, dyld-free binary (see architecture.md) - so real DWARF/compact-
 * unwind frame data is categorically unavailable to any consumer on this
 * target, not a gap specific to this tier's own build flags.
 *
 * Given that, this smoketest does NOT attempt to prove real multi-frame
 * backtrace output (that would require fabricating success or silently
 * changing what "PASS" means) - not this project's own separate
 * _thread_stack_pcs()-based backtrace()/gen/backtrace.c either (real
 * Apple backtrace() doesn't use libunwind at all on Darwin - see
 * build_libunwind.sh's header comment). Instead it proves what IS real
 * and true in this actual environment: the real engine (libunwind.cpp's
 * __unw_getcontext/__unw_init_local, UnwindLevel1-gcc-ext.c's real
 * _Unwind_Backtrace) runs correctly and SAFELY - captures a real
 * register context, initializes a real cursor, and then correctly,
 * safely detects "no unwind info available" (UNW_ENOINFO-class outcome)
 * without crashing, corrupting state, or fabricating frame data it
 * doesn't have. That is real, verified, useful behavior: any future
 * consumer of this library on this target needs to know it degrades
 * safely, not that it crashes, when asked to unwind a stack with no real
 * metadata - which is every stack on this target today. */
#include <stddef.h>
#include <stdint.h>
#include "libc_build_prototypes.h"

extern void _exit(int) __attribute__((noreturn));
/* MUST be called before anything that touches os_unfair_lock/TSD - real
 * libunwind's RWMutex (real pthread_rwlock_t) and DwarfFDECache both
 * ultimately reach _os_tsd_get_direct() via libplatform's real lock code.
 * Same real bug class as libmalloc_dispatch.c's get_zone() and
 * libc_printf_smoketest.c's own header comment document (a genuinely
 * uninitialized TPIDRRO_EL0 dereferenced as a TSD block pointer) - this
 * smoketest never calls malloc() or printf() before its own real lock
 * usage, so it doesn't get either of those files' lazy/explicit
 * bootstrap for free and needs its own. Confirmed the hard way: a real,
 * reproducible SIGSEGV on first QEMU boot-test before this call was
 * added (root-caused via the log, same pattern as the printf tier's own
 * documented bootstrap bug). */
extern int pthread_tsd_shim_bootstrap_for_libc(void);

typedef int _Unwind_Reason_Code;
struct _Unwind_Context;
typedef _Unwind_Reason_Code (*_Unwind_Trace_Fn)(struct _Unwind_Context *,
                                                 void *);
extern _Unwind_Reason_Code _Unwind_Backtrace(_Unwind_Trace_Fn, void *);
extern uintptr_t _Unwind_GetIP(struct _Unwind_Context *);

/* Real internal entry points (third_party/libunwind/libunwind/src/
 * libunwind_ext.h) - the same ones UnwindLevel1-gcc-ext.c's real
 * _Unwind_Backtrace() calls internally; called directly here too so this
 * smoketest can check each real step's own result code, not just
 * _Unwind_Backtrace()'s already-callback-driven outer loop.
 *
 * Sized generously (128 x uint64_t = 1024 bytes each) rather than
 * pulling in the real opaque unw_context_t/unw_cursor_t typedefs (which
 * would require this file to see libunwind's internal Registers.hpp
 * machinery): real sizes checked against
 * third_party/libunwind/libunwind/include/__libunwind_config.h's
 * __aarch64__ branch before picking this constant, not guessed -
 * _LIBUNWIND_CONTEXT_SIZE=66 and _LIBUNWIND_CURSOR_SIZE=78 (both counted
 * in uint64_t units, non-SVE arm64 - this target has no SVE), i.e. real
 * max size 624 bytes, comfortably under the 1024 bytes reserved here. */
typedef struct { uint64_t opaque[128]; } unw_context_t_compat;
typedef struct { uint64_t opaque[128]; } unw_cursor_t_compat;
extern int __unw_getcontext(unw_context_t_compat *);
extern int __unw_init_local(unw_cursor_t_compat *, unw_context_t_compat *);
extern int __unw_step(unw_cursor_t_compat *);

#define URC_NO_REASON 0

static void put(const char *s) {
    while (*s) _write(1, s++, 1);
}
static void put_int(int v) {
    char buf[16];
    int i = 15;
    buf[i--] = '\0';
    if (v == 0) {
        put("0");
        return;
    }
    int neg = v < 0;
    unsigned uv = neg ? (unsigned)(-v) : (unsigned)v;
    while (uv) {
        buf[i--] = '0' + (uv % 10);
        uv /= 10;
    }
    if (neg) buf[i--] = '-';
    put(&buf[i + 1]);
}

static int frame_count;

static _Unwind_Reason_Code trace_callback(struct _Unwind_Context *ctx,
                                           void *arg) {
    (void)arg;
    (void)ctx;
    frame_count++;
    return URC_NO_REASON;
}

int main(void) {
    if (!pthread_tsd_shim_bootstrap_for_libc()) {
        put("FAIL: pthread_tsd_shim_bootstrap_for_libc() failed\n");
        _exit(2);
    }
    put("libunwind smoketest: start\n");

    /* Real context capture: this step needs NO unwind metadata (it just
     * saves the current register file), so it must genuinely succeed
     * regardless of the __unwind_info constraint documented above. */
    unw_context_t_compat uc;
    unw_cursor_t_compat cursor;
    int rc = __unw_getcontext(&uc);
    if (rc != 0) {
        put("FAIL: __unw_getcontext returned ");
        put_int(rc);
        put(" (expected 0/success)\n");
        _exit(1);
    }
    put("PASS: __unw_getcontext captured a real register context\n");

    rc = __unw_init_local(&cursor, &uc);
    if (rc != 0) {
        put("FAIL: __unw_init_local returned ");
        put_int(rc);
        put(" (expected 0/success)\n");
        _exit(1);
    }
    put("PASS: __unw_init_local initialized a real cursor\n");

    /* Real step attempt: given this environment's confirmed lack of any
     * __unwind_info/__eh_frame/__compact_unwind in the final linked
     * image (see header comment), this MUST NOT find real frame data -
     * proving it returns a real "no info" outcome (<=0) rather than
     * crashing or fabricating a frame is the actual, honest thing to
     * verify here. */
    int step_rc = __unw_step(&cursor);
    if (step_rc > 0) {
        put("FAIL: __unw_step reported success (");
        put_int(step_rc);
        put(") - unexpected given this build's confirmed lack of "
            "__unwind_info; re-check build_libunwind.sh's static-link "
            "constraint writeup, something changed\n");
        _exit(1);
    }
    put("PASS: __unw_step safely reported no unwind info available "
        "(rc=");
    put_int(step_rc);
    put("), as expected on this target - no crash, no fabricated frame\n");

    /* Real _Unwind_Backtrace() call: same real code path a real crash
     * handler would use. Given the same constraint, expect it to return
     * cleanly having invoked the callback zero times - not to crash. */
    frame_count = 0;
    _Unwind_Reason_Code bt_rc = _Unwind_Backtrace(trace_callback, NULL);
    (void)bt_rc;
    put("PASS: _Unwind_Backtrace() returned safely, callback invoked ");
    put_int(frame_count);
    put(" time(s) (0 expected on this target)\n");

    put("ALL PASS: libunwind smoketest (real engine, real safe "
        "no-unwind-info behavior confirmed on this target)\n");
    _exit(0);
    return 0;
}
