/* crashdiag (DAR-416, checked in for DAR-450): a crash probe for binaries
 * that die with only "Failed to send exception EXC_CORPSE_NOTIFY".
 *   DYLD_INSERT_LIBRARIES=/usr/lib/crashdiag.dylib <binary>
 * On SIGTRAP/SEGV/BUS/ILL/ABRT it prints pc/lr/far/esr, a 64-frame fp
 * backtrace with dladdr symbols, and every image's __crash_info message,
 * then re-raises with the default action. SIGUSR2 dumps every thread.
 * Built by build_crashdiag.sh. */
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <stdint.h>
#include <sys/ucontext.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>

static void w(const char *s) {
    register long x0 asm("x0") = 2; register const char *x1 asm("x1") = s;
    register long x2 asm("x2") = (long)strlen(s); register long x16 asm("x16") = 4;
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
}
static uint8_t *crash_info(uint32_t i) {
    const struct mach_header_64 *h = (const void *)_dyld_get_image_header(i);
    const struct load_command *lc = (const void *)(h + 1);
    for (uint32_t c = 0; c < h->ncmds; c++, lc = (const void *)((const char *)lc + lc->cmdsize)) {
        if (lc->cmd != LC_SEGMENT_64) continue;
        const struct segment_command_64 *sg = (const void *)lc;
        const struct section_64 *sc = (const void *)(sg + 1);
        for (uint32_t k = 0; k < sg->nsects; k++)
            if (!strcmp(sc[k].sectname, "__crash_info"))
                return (uint8_t *)(sc[k].addr + _dyld_get_image_vmaddr_slide(i));
    }
    return 0;
}
static void hex(uint64_t v) {
    char b[19]; b[0]='0'; b[1]='x';
    for (int i = 0; i < 16; i++) b[2+i] = "0123456789abcdef"[(v >> (60 - 4*i)) & 15];
    b[18] = 0; w(b);
}
static void sym(const char *tag, uint64_t a) {
    Dl_info di; w(tag); hex(a);
    if (dladdr((void *)a, &di)) {
        w(" "); w(di.dli_fname ? di.dli_fname : "?"); w("`");
        w(di.dli_sname ? di.dli_sname : "?"); w("+"); hex(a - (uint64_t)di.dli_saddr);
        w(" (img+"); hex(a - (uint64_t)di.dli_fbase); w(")");
    }
    w("\n");
}
static void handler(int sig, siginfo_t *si, void *uc_) {
    ucontext_t *uc = uc_;
    w("\n=== crashdiag sig="); hex(sig); w(" pid="); hex(getpid()); w("\n");
    sym(" pc ", uc->uc_mcontext->__ss.__pc);
    sym(" lr ", uc->uc_mcontext->__ss.__lr);
    w(" far "); hex(uc->uc_mcontext->__es.__far); w(" esr "); hex(uc->uc_mcontext->__es.__esr);
    w(" x0 "); hex(uc->uc_mcontext->__ss.__x[0]); w(" x1 "); hex(uc->uc_mcontext->__ss.__x[1]); w("\n");
    {
        uint64_t base; __asm__ volatile("mrs %0, TPIDRRO_EL0" : "=r"(base)); base &= ~7ull;
        uint64_t *self = (uint64_t *)(base - 224);
        w(" tsdbase "); hex(base); w(" self "); hex((uint64_t)self);
        w(" sig "); hex(self[0]); w(" sig^self "); hex(self[0] ^ (uint64_t)self);
        w(" tsd0 "); hex(((uint64_t *)base)[0]); w(" tsd7(munge) "); hex(((uint64_t *)base)[7]); w("\n");
        sym(" selfsym ", (uint64_t)self);
    }
    uint64_t fp = uc->uc_mcontext->__ss.__fp;
    for (int i = 0; i < 64 && fp && (fp & 7) == 0; i++) {
        uint64_t *f = (uint64_t *)fp; sym(" bt ", f[1]); fp = f[0];
    }
    uint32_t n = _dyld_image_count();
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *d = crash_info(i);
        if (d) {
            const char *m = *(const char **)(d + 8);
            if (m) { w(" crash_info "); w(_dyld_get_image_name(i)); w(": "); w(m); w("\n"); }
        }
    }
    signal(sig, SIG_DFL);
    raise(sig);
}
static void threads_dump(int sig, siginfo_t *si, void *uc_) {
    (void)sig; (void)si; ucontext_t *uc = uc_;
    thread_act_array_t th; mach_msg_type_number_t n = 0;
    thread_t me = mach_thread_self();
    if (task_threads(mach_task_self(), &th, &n) != KERN_SUCCESS) { w("task_threads failed\n"); return; }
    w("\n=== crashdiag threads pid="); hex(getpid()); w("\n");
    for (mach_msg_type_number_t i = 0; i < n; i++) {
        thread_basic_info_data_t bi; mach_msg_type_number_t bc = THREAD_BASIC_INFO_COUNT;
        thread_info(th[i], THREAD_BASIC_INFO, (thread_info_t)&bi, &bc);
        w("-- thread "); hex(i); w(" user_s "); hex(bi.user_time.seconds); w(" sys_s "); hex(bi.system_time.seconds);
        w(" cpu "); hex(bi.cpu_usage); w(" run "); hex(bi.run_state); w("\n");
        if (th[i] == me) {
            w("   (self, interrupted context)\n");
            sym("   pc ", uc->uc_mcontext->__ss.__pc); sym("   lr ", uc->uc_mcontext->__ss.__lr);
            uint64_t fp = uc->uc_mcontext->__ss.__fp;
            for (int k = 0; k < 14 && fp && (fp & 7) == 0; k++) { uint64_t *f = (uint64_t *)fp; sym("   bt ", f[1]); fp = f[0]; }
            continue;
        }
        thread_suspend(th[i]);
        arm_thread_state64_t st; mach_msg_type_number_t sc = ARM_THREAD_STATE64_COUNT;
        if (thread_get_state(th[i], ARM_THREAD_STATE64, (thread_state_t)&st, &sc) == KERN_SUCCESS) {
            sym("   pc ", st.__pc); sym("   lr ", st.__lr);
            uint64_t fp = st.__fp;
            for (int k = 0; k < 14 && fp && (fp & 7) == 0; k++) {
                uint64_t *f = (uint64_t *)fp; sym("   bt ", f[1]); fp = f[0];
            }
        }
        thread_resume(th[i]);
    }
}
__attribute__((constructor)) static void init(void) {
    { struct sigaction su; memset(&su, 0, sizeof su); su.sa_sigaction = threads_dump; su.sa_flags = SA_SIGINFO; sigaction(SIGUSR2, &su, 0); }
    {
        uint64_t base; __asm__ volatile("mrs %0, TPIDRRO_EL0" : "=r"(base)); base &= ~7ull;
        uint64_t *self = (uint64_t *)(base - 224);
        w("crashdiag ctor: self "); hex((uint64_t)self); w(" sig "); hex(self[0]);
        w(" sig^self "); hex(self[0] ^ (uint64_t)self); w(" munge "); hex(((uint64_t *)base)[7]); w("\n");
    }
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = handler; sa.sa_flags = SA_SIGINFO;
    sigaction(SIGTRAP, &sa, 0); sigaction(SIGSEGV, &sa, 0);
    sigaction(SIGBUS, &sa, 0); sigaction(SIGILL, &sa, 0); sigaction(SIGABRT, &sa, 0);
}
