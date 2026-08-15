/* Real Apple sched_yield() (third_party/libpthread/src/pthread.c:1872-1877,
 * copied verbatim - see libunwind_sched_yield.s's header comment for why
 * this one real one-liner is supplied here rather than by
 * build_libpthread.sh's tier). */
extern int swtch_pri(int pri);

int sched_yield(void) {
    swtch_pri(0);
    return 0;
}
