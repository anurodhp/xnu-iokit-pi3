/* Phase 6.5 Phase B (docs/daemon-roadmap.md): NOT real dyld. A throwaway
 * few-instruction stand-in installed at /usr/lib/dyld to prove the kernel's
 * real, unmodified, never-before-exercised load_dylinker()/LC_LOAD_DYLINKER
 * handoff (third_party/xnu-7195/bsd/kern/mach_loader.c) actually reaches a
 * dylinker's entry point on real boot, before any effort goes into porting
 * real dyld (~53K LOC, third_party/dyld). Real dyld's own logic (rebase/
 * bind, closures, symbol resolution) is Phase C, not attempted here.
 *
 * Same raw-syscall pattern as crt0_smoketest.c/dyld_test_exec_main.c (no
 * libc linked in). exit(42) is a deliberately distinctive, unlikely-by-
 * accident code: a real shell reading $? after running the test executable
 * that references this as its dylinker is verification the kernel actually
 * jumped here, not to the test executable's own entry point.
 */
static long syscall3(long num, long a0, long a1, long a2) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x16 asm("x16") = num;
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
    return x0;
}

static const char msg[] = "dyld_stub: kernel reached real /usr/lib/dyld entry\n";

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    syscall3(4 /* SYS_write */, 1, (long)msg, sizeof(msg) - 1);
    return 42;
}

void exit(int code) {
    syscall3(1 /* SYS_exit */, code, 0, 0);
    for (;;) { }
}
