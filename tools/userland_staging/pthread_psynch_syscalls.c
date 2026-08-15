/* Real raw syscall wrappers for the psynch_* family (the FAIRSHARE-policy
 * contended mutex/cond/rwlock slow path pthread_mutex.c/pthread_cond.c/
 * pthread_rwlock.c fall back to when NOT using the ulock-based fast
 * path) - real numbers/signatures cross-checked directly against
 * third_party/xnu-7195/bsd/kern/syscalls.master, all NO_SYSCALL_STUB
 * (same class as __ulock_wait/__ulock_wake/__bsdthread_create - Apple
 * hand-writes these in libpthread's own internal sources, not this
 * project's third_party/libpthread checkout's public headers).
 *
 * Real, not faked: this project forces __pthread_mutex_use_ulock=true
 * (see pthread_main_thread_bootstrap.c), so in practice the ulock path
 * is what actually runs; these exist because -O0 keeps the FAIRSHARE
 * slow-path code linked in (never dead-code-eliminated) and it must
 * resolve to something real at link time, not because this target ever
 * exercises real cross-thread contention (it can't - see
 * pthread_main_thread_bootstrap.c's own header on bsdthread_create).
 */

typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;

static inline long syscall5_u(long num, long a0, long a1, long a2, long a3, long a4) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x4 asm("x4") = a4;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n" "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x16)
                 : "memory", "cc");
    if (is_error) { return -x0; }
    return x0;
}

static inline long syscall7_u(long num, long a0, long a1, long a2, long a3, long a4, long a5, long a6) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x4 asm("x4") = a4;
    register long x5 asm("x5") = a5;
    register long x6 asm("x6") = a6;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n" "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x6), "r"(x16)
                 : "memory", "cc");
    if (is_error) { return -x0; }
    return x0;
}

static inline long syscall8_u(long num, long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x4 asm("x4") = a4;
    register long x5 asm("x5") = a5;
    register long x6 asm("x6") = a6;
    register long x7 asm("x7") = a7;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n" "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x6), "r"(x7), "r"(x16)
                 : "memory", "cc");
    if (is_error) { return -x0; }
    return x0;
}

extern int errno;

/* 301 */
uint32_t __psynch_mutexwait(void *mutex, uint32_t mgen, uint32_t ugen, uint64_t tid, uint32_t flags) {
    long r = syscall5_u(301, (long)mutex, (long)mgen, (long)ugen, (long)tid, (long)flags);
    if (r < 0) { errno = (int)-r; return (uint32_t)-1; }
    return (uint32_t)r;
}
/* 302 */
uint32_t __psynch_mutexdrop(void *mutex, uint32_t mgen, uint32_t ugen, uint64_t tid, uint32_t flags) {
    long r = syscall5_u(302, (long)mutex, (long)mgen, (long)ugen, (long)tid, (long)flags);
    if (r < 0) { errno = (int)-r; return (uint32_t)-1; }
    return (uint32_t)r;
}
/* 303 */
uint32_t __psynch_cvbroad(void *cv, uint64_t cvlsgen, uint64_t cvudgen, uint32_t flags, void *mutex, uint64_t mugen, uint64_t tid) {
    long r = syscall7_u(303, (long)cv, (long)cvlsgen, (long)cvudgen, (long)flags, (long)mutex, (long)mugen, (long)tid);
    if (r < 0) { errno = (int)-r; return (uint32_t)-1; }
    return (uint32_t)r;
}
/* 304 */
uint32_t __psynch_cvsignal(void *cv, uint64_t cvlsgen, uint32_t cvugen, int thread_port, void *mutex, uint64_t mugen, uint64_t tid, uint32_t flags) {
    long r = syscall8_u(304, (long)cv, (long)cvlsgen, (long)cvugen, (long)thread_port, (long)mutex, (long)mugen, (long)tid, (long)flags);
    if (r < 0) { errno = (int)-r; return (uint32_t)-1; }
    return (uint32_t)r;
}
/* 305 */
uint32_t __psynch_cvwait(void *cv, uint64_t cvlsgen, uint32_t cvugen, void *mutex, uint64_t mugen, uint32_t flags, int64_t sec, uint32_t nsec) {
    long r = syscall8_u(305, (long)cv, (long)cvlsgen, (long)cvugen, (long)mutex, (long)mugen, (long)flags, (long)sec, (long)nsec);
    if (r < 0) { errno = (int)-r; return (uint32_t)-1; }
    return (uint32_t)r;
}
/* 306 */
uint32_t __psynch_rw_rdlock(void *rwlock, uint32_t lgenval, uint32_t ugenval, uint32_t rw_wc, int flags) {
    long r = syscall5_u(306, (long)rwlock, (long)lgenval, (long)ugenval, (long)rw_wc, (long)flags);
    if (r < 0) { errno = (int)-r; return (uint32_t)-1; }
    return (uint32_t)r;
}
/* 307 */
uint32_t __psynch_rw_wrlock(void *rwlock, uint32_t lgenval, uint32_t ugenval, uint32_t rw_wc, int flags) {
    long r = syscall5_u(307, (long)rwlock, (long)lgenval, (long)ugenval, (long)rw_wc, (long)flags);
    if (r < 0) { errno = (int)-r; return (uint32_t)-1; }
    return (uint32_t)r;
}
/* 308 */
uint32_t __psynch_rw_unlock(void *rwlock, uint32_t lgenval, uint32_t ugenval, uint32_t rw_wc, int flags) {
    long r = syscall5_u(308, (long)rwlock, (long)lgenval, (long)ugenval, (long)rw_wc, (long)flags);
    if (r < 0) { errno = (int)-r; return (uint32_t)-1; }
    return (uint32_t)r;
}
/* 312 */
int __psynch_cvclrprepost(void *cv, uint32_t cvgen, uint32_t cvugen, uint32_t cvsgen, uint32_t prepocnt, uint32_t preposeq, uint32_t flags) {
    long r = syscall7_u(312, (long)cv, (long)cvgen, (long)cvugen, (long)cvsgen, (long)prepocnt, (long)preposeq, (long)flags);
    if (r < 0) { errno = (int)-r; return -1; }
    return (int)r;
}
