/*
 * qos_probe (DAR-439): scheduler priority / QoS probe. QEMU test aid,
 * installed as /usr/X11/bin/qos_probe.
 *   qos_probe self [set]   print this thread's QoS class and Mach base/cur
 *                          priority; with "set" call
 *                          pthread_set_qos_class_self_np(USER_INTERACTIVE)
 *                          and print again.
 *   qos_probe pid PID      task_for_pid + task_threads: base/cur priority and
 *                          sched policy of every thread of PID.
 *   qos_probe exec PROG [ARGS]  set USER_INTERACTIVE, then exec PROG (does
 *                          the QoS survive exec? used to A/B an unmodified
 *                          Xvfb/wmaker).
 *   qos_probe slop [set]   200 x usleep(2000): mean/max overshoot in us
 *                          (kernel timer coalescing, tcoal_qos_adjust,
 *                          osfmk/kern/timer_call.c), optionally after set.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <sys/time.h>
#include <mach/mach.h>

static long long now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000LL + tv.tv_usec;
}

static void dump_thread(thread_act_t t, const char *tag)
{
    thread_extended_info_data_t ei;
    mach_msg_type_number_t n = THREAD_EXTENDED_INFO_COUNT;
    if (thread_info(t, THREAD_EXTENDED_INFO, (thread_info_t)&ei, &n) == KERN_SUCCESS)
        printf("%s: ext base_pri=%d cur_pri=%d max_pri=%d policy=%d run_state=%d\n", tag,
            ei.pth_priority, ei.pth_curpri, ei.pth_maxpriority, ei.pth_policy, ei.pth_run_state);
    else
        printf("%s: THREAD_EXTENDED_INFO failed\n", tag);
    policy_timeshare_info_data_t ts;
    n = POLICY_TIMESHARE_INFO_COUNT;
    if (thread_info(t, THREAD_SCHED_TIMESHARE_INFO, (thread_info_t)&ts, &n) == KERN_SUCCESS)
        printf("%s: timeshare base=%d cur=%d max=%d depressed=%d\n", tag, ts.base_priority,
            ts.cur_priority, ts.max_priority, ts.depressed);
}

static void dump_self(const char *tag)
{
    qos_class_t qc = 0;
    int rp = 0;
    int rc = pthread_get_qos_class_np(pthread_self(), &qc, &rp);
    printf("%s: pthread_get_qos_class_np rc=%d class=0x%x relpri=%d\n", tag, rc, qc, rp);
    dump_thread(mach_thread_self(), tag);
}

static void do_set(void)
{
    int rc = pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    printf("pthread_set_qos_class_self_np(USER_INTERACTIVE) rc=%d\n", rc);
}

int main(int argc, char **argv)
{
    const char *m = argc > 1 ? argv[1] : "self";
    int set = argc > 2 && !strcmp(argv[2], "set");
    if (!strcmp(m, "self")) {
        dump_self("before");
        if (set) { do_set(); dump_self("after"); }
        return 0;
    }
    if (!strcmp(m, "pid") && argc > 2) {
        int pid = atoi(argv[2]);
        task_t task;
        kern_return_t kr = task_for_pid(mach_task_self(), pid, &task);
        if (kr != KERN_SUCCESS) { printf("task_for_pid(%d) kr=%d\n", pid, kr); return 1; }
        thread_act_array_t th;
        mach_msg_type_number_t cnt = 0;
        kr = task_threads(task, &th, &cnt);
        if (kr != KERN_SUCCESS) { printf("task_threads kr=%d\n", kr); return 1; }
        for (unsigned i = 0; i < cnt; i++) {
            char tag[32];
            snprintf(tag, sizeof tag, "pid %d thr %u", pid, i);
            dump_thread(th[i], tag);
        }
        return 0;
    }
    if (!strcmp(m, "exec") && argc > 2) {
        do_set();
        execv(argv[2], argv + 2);
        perror("execv");
        return 1;
    }
    if (!strcmp(m, "slop")) {
        if (set) do_set();
        long long sum = 0, mx = 0;
        int n = 200;
        for (int i = 0; i < n; i++) {
            long long t0 = now_us();
            usleep(2000);
            long long d = now_us() - t0 - 2000;
            sum += d;
            if (d > mx) mx = d;
        }
        printf("slop%s: usleep(2000) x%d mean overshoot %lld us max %lld us\n",
            set ? " (after set)" : "", n, sum / n, mx);
        return 0;
    }
    fprintf(stderr, "usage: qos_probe self [set] | pid PID | exec PROG.. | slop [set]\n");
    return 2;
}
