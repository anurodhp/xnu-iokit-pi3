/* Honest stand-in for real third_party/xnu-7195/libsyscall/wrappers/
 * __commpage_gettimeofday.c: that real file needs
 * new_commpage_timeofday_data_t, a private commpage layout type not
 * visible from the headers this build includes (a different commpage
 * region/struct than the one build_libunwind.sh's real mach_absolute_time
 * work already proved reachable -- not the same gap, not yet chased
 * down). Real third_party/Libc/sys/gettimeofday.c's own calling
 * convention already has a real, designed fallback for exactly this:
 * `if (__commpage_gettimeofday(tp)) { use the real __gettimeofday(2)
 * syscall instead }` -- i.e. a non-zero return means "commpage
 * unavailable, don't trust tp". This stub honestly always reports
 * unavailable (never claims to have filled tp with real commpage-sourced
 * data it didn't), forcing the real syscall path
 * (ps_extra_syscalls.c's __gettimeofday) every time -- correct, just not
 * the fast path. */
struct timeval;
int __commpage_gettimeofday(struct timeval *tp) {
    (void)tp;
    return 1;
}
