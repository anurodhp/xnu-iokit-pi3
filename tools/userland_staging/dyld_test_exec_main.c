/* Phase 6.5 Phase B (docs/daemon-roadmap.md): body of dyld_test_exec.macho.
 * Against the throwaway kernel-plumbing-only stub dylinker
 * (dyld_stub_main.c, /usr/lib/dyld built via build_dyld_stub.sh), this
 * main() was never reached -- the stub never actually loads the real
 * executable, it just proves the kernel's own load_dylinker() handoff.
 *
 * Against REAL dyld (build_dyld_link.sh's dyld.macho, Phase C/D), this
 * main() running to completion IS the real, correct, expected outcome:
 * real dyld's job is exactly to load dependencies then jump to the main
 * executable's own real entry point. Phase D closed 2026-08-25 with this
 * message printing and the process exiting cleanly with this file's own
 * `return 1` as its real exit code -- confirmed via a real QEMU boot
 * test (see docs/daemon-roadmap.md's Phase D section), not the stub-era
 * "unexpected" case the message text used to describe. */
static long syscall3(long num, long a0, long a1, long a2) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x16 asm("x16") = num;
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
    return x0;
}

static const char msg[] = "dyld_test_exec: own main() ran (real dyld loaded deps then jumped here -- expected)\n";

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    syscall3(4 /* SYS_write */, 1, (long)msg, sizeof(msg) - 1);
    return 1;
}

void exit(int code) {
    syscall3(1 /* SYS_exit */, code, 0, 0);
    for (;;) { }
}
