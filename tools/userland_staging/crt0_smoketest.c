/* Minimal smoketest: does apple-oss-distributions' real Csu crt0.o (built
 * for -static, no dyld) actually produce a working entry point on this
 * kernel, same as tools/init_binary/init.c's hand-written LC_UNIXTHREAD
 * asm does? No libc available yet (Phase 4's Libc pull is separate,
 * larger, unfinished work) - main() below issues the same raw syscalls
 * init.c uses, only to prove crt0.o's _start -> main() handoff and the
 * final return-to-crt0 -> exit() path both work, not to be a real program.
 */
typedef unsigned long size_t;

static long syscall3(long num, long a0, long a1, long a2) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x16 asm("x16") = num;
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
    return x0;
}

static const char msg[] = "crt0_smoketest: real Csu crt0.o reached main()\n";

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    syscall3(4 /* SYS_write */, 1, (long)msg, sizeof(msg) - 1);
    return 0;
}

void exit(int code) {
    syscall3(1 /* SYS_exit */, code, 0, 0);
    for (;;) { }
}
