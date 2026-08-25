/* Phase 6.5 Phase B (docs/daemon-roadmap.md): body of dyld_test_exec.macho.
 * This main() is NEVER reached in the test this binary exists for --
 * third_party/xnu-7195/bsd/kern/mach_loader.c's load_dylinker() overwrites
 * the process's initial thread state with the dylinker's entry point
 * (dyld_stub_main.c's main(), once /usr/lib/dyld is the real stub) before
 * this binary's own LC_MAIN entry ever runs. Kept real and buildable
 * (rather than an empty stub) only so this file is a legitimate,
 * independently-inspectable Mach-O -- see build_dyld_test_exec.sh for what
 * is actually being proven (the load-command shape, not this code path).
 */
static long syscall3(long num, long a0, long a1, long a2) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x16 asm("x16") = num;
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
    return x0;
}

static const char msg[] = "dyld_test_exec: own main() ran (dylinker did NOT intercept -- unexpected)\n";

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    syscall3(4 /* SYS_write */, 1, (long)msg, sizeof(msg) - 1);
    return 1;
}

void exit(int code) {
    syscall3(1 /* SYS_exit */, code, 0, 0);
    for (;;) { }
}
