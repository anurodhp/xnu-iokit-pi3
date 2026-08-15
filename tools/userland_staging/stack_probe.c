/* Diagnostic tool, not a smoketest with a pass/fail: recurses with a real
 * stack-resident buffer per frame and reports real depth reached via
 * real write(2) before a genuine stack-overflow SIGSEGV, so depth-at-
 * crash is directly comparable against DFLSSIZ/MAXSSIZ (bsd/arm/
 * vmparam.h) and this target's actual exec-time stack allocation.
 *
 * Built this way (real evidence, not guessed): originally written to
 * test the Seventeenth-pass bash SIGSEGV's "stack too small" hypothesis
 * (docs/roadmap.md). Confirmed real and fixed: this board (BCM2837)
 * builds the kernel with CONFIG_EMBEDDED (BCM2837 is embedded-only
 * upstream, see CLAUDE.md), which collapses DFLSSIZ/MAXSSIZ to real iOS-
 * app values (1MB soft/hard) instead of real macOS desktop values
 * (8MB/64MB) -- before the vmparam.h fix, this probe crashed with ZERO
 * "depth" output at all (well under 500 frames); after, it reaches
 * ~2000 frames (~8MB, matching the fixed DFLSSIZ exactly). Real bash
 * itself turned out to crash from a SEPARATE bug (real stack-frame
 * corruption around its first pthread_once() call, see docs/roadmap.md's
 * Eighteenth pass) -- kept as a standing regression check for the real
 * stack-size fix, not because it was bash's actual root cause.
 *
 * Same minimal Csu+libsyscall-stub link as libsyscall_smoketest.c, not
 * the full curated stack -- isolates "does deep recursion alone crash"
 * from every other variable. See build_stack_probe.sh. */
extern long write(int fd, const void *buf, unsigned long n);
extern void _exit(int rc) __attribute__((noreturn));
void exit(int code) { _exit(code); } /* Csu's crt.c calls exit(), not _exit() */

static void putnum(long n) {
    char buf[24];
    int i = 23;
    buf[i--] = '\n';
    if (n == 0) buf[i--] = '0';
    while (n > 0) { buf[i--] = (char)('0' + (n % 10)); n /= 10; }
    write(1, &buf[i + 1], (unsigned long)(23 - i));
}

static long depth = 0;

static void recurse(void) {
    volatile char buf[4096];
    buf[0] = 0;
    buf[4095] = 0;
    depth++;
    if (depth % 500 == 0) putnum(depth);
    recurse();
}

int main(void) {
    const char msg[] = "stack_probe: start\n";
    write(1, msg, sizeof(msg) - 1);
    recurse();
    _exit(0);
    return 0;
}
