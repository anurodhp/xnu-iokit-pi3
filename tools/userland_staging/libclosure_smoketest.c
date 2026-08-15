/* Real smoketest for build_libclosure.sh's libclosure.a - exercises real
 * compiler-emitted Blocks (-fblocks), real Block_copy (stack->heap
 * promotion via the real refcounted _NSConcreteMallocBlock path), a real
 * captured-variable round-trip, and real Block_release (heap teardown).
 * A second block below captures a __block-qualified byref variable,
 * exercising the real BLOCK_HAS_COPY_DISPOSE / _Block_object_assign /
 * _Block_object_dispose path (the compiler-synthesized copy/dispose
 * helpers), not just the simpler no-byref block the first test uses.
 *
 * Freestanding: no libc startup beyond real Csu + real malloc (this
 * tier's real dependency, see build_libclosure.sh) and raw syscalls for
 * output/exit, same pattern as libmalloc_smoketest.c/
 * libplatform_smoketest.c.
 */

#include <Block.h>

typedef unsigned long size_t;
extern long write(int fd, const void *buf, size_t count);
extern void _exit(int) __attribute__((noreturn));

static void put(const char *s) {
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    write(1, s, n);
}

/* Simple block: captures an int by value, no byref/copy-dispose helpers
 * needed (BLOCK_HAS_COPY_DISPOSE not set) - exercises _Block_copy's
 * plain memcpy-and-retarget-isa fast path. */
static int simple_block_test(void) {
    int captured = 42;
    int (^adder)(int) = ^(int x) {
        return captured + x;
    };

    /* Still a stack block here - call it directly first. */
    if (adder(8) != 50) {
        put("FAIL: stack block call\n");
        return 0;
    }

    /* Real Block_copy: promotes to a real malloc'd block, isa retargeted
     * to _NSConcreteMallocBlock, refcount initialized. */
    int (^heapAdder)(int) = Block_copy(adder);
    if (!heapAdder) {
        put("FAIL: Block_copy returned NULL\n");
        return 0;
    }
    if (heapAdder == adder) {
        put("FAIL: Block_copy did not relocate to heap\n");
        Block_release(heapAdder);
        return 0;
    }

    int ok = (heapAdder(8) == 50) && (heapAdder(-42) == 0);
    Block_release(heapAdder);
    put(ok ? "PASS: simple Block_copy/call/release\n" : "FAIL: heap block call\n");
    return ok;
}

/* __block-qualified byref variable: forces the compiler to emit real
 * copy/dispose helpers and route through _Block_object_assign/
 * _Block_object_dispose (BLOCK_FIELD_IS_BYREF), the more complex real
 * path simple_block_test() above doesn't exercise. */
static int byref_block_test(void) {
    __block int counter = 0;

    void (^increment)(int) = ^(int by) {
        counter += by;
    };

    void (^heapIncrement)(int) = Block_copy(increment);
    if (!heapIncrement) {
        put("FAIL: byref Block_copy returned NULL\n");
        return 0;
    }

    heapIncrement(5);
    heapIncrement(7);
    heapIncrement(-2);

    int ok = (counter == 10);
    Block_release(heapIncrement);
    put(ok ? "PASS: __block byref copy/dispose round-trip\n" : "FAIL: byref counter mismatch\n");
    return ok;
}

int main(void) {
    put("libclosure smoketest: start\n");

    int ok = 1;
    ok &= simple_block_test();
    ok &= byref_block_test();

    put(ok ? "ALL PASS: libclosure smoketest\n" : "SOME TESTS FAILED: libclosure smoketest\n");
    _exit(ok ? 0 : 1);
}
