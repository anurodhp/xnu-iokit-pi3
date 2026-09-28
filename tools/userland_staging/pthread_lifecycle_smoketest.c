/* DAR-416: thread lifecycle smoketest for Apple's real libpthread
 * (libsystem_pthread.dylib) plus libsystem_kernel's posix_spawn.
 *
 * Before DAR-416 this port's hand-written pthread had no pthread_join,
 * pthread_cancel or pthread_kill (pthread_smoketest.c still busy-polls a
 * flag for that reason). Each case below exercises one of those paths:
 *   1. pthread_join returns the thread's return value
 *   2. pthread_exit value is seen by pthread_join
 *   3. pthread_detach'd thread runs to completion
 *   4. pthread_cancel + pthread_testcancel; join sees PTHREAD_CANCELED
 *   5. pthread_cancel of a thread blocked in pthread_cond_wait (a
 *      cancellation point: __pthread_markcancel interrupts the wait)
 *   6. pthread_kill delivers a signal to the target thread
 *   7. posix_spawn + waitpid
 * and the kernel psynch paths Apple's libpthread uses instead of ulock
 * (pthread_mutex.c:1765-1779 limits ulock to NORMAL firstfit mutexes;
 * rwlocks always use psynch), real since DAR-416's bsd_pthread_synch.c:
 *   8. contended PTHREAD_MUTEX_RECURSIVE (psynch_mutexwait/mutexdrop)
 *   9. contended rwlock, readers vs a writer (psynch_rw_*)
 *  10. pthread_cond_wait/signal on an ERRORCHECK mutex (psynch_cvwait)
 * alarm(60) turns a hang into SIGALRM rather than a stuck boot test. */
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int failures;

static void put(const char *s) { write(1, s, strlen(s)); }

static void check(int ok, const char *what)
{
    put(ok ? "  ok   " : "  FAIL ");
    put(what);
    put("\n");
    if (!ok) failures++;
}

static void *ret_arg(void *arg) { return arg; }

static void *exit_arg(void *arg)
{
    pthread_exit(arg);
    return NULL;
}

static volatile int detached_ran;
static void *detached_fn(void *arg)
{
    (void)arg;
    detached_ran = 1;
    return NULL;
}

static volatile int spin_started;
static void *testcancel_fn(void *arg)
{
    (void)arg;
    spin_started = 1;
    for (;;) pthread_testcancel();
    return NULL;
}

static pthread_mutex_t cw_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cw_cond = PTHREAD_COND_INITIALIZER;
static volatile int cw_waiting;
static void unlock_cw(void *m) { pthread_mutex_unlock(m); }
static void *condwait_fn(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&cw_mutex);
    pthread_cleanup_push(unlock_cw, &cw_mutex);
    cw_waiting = 1;
    for (;;) pthread_cond_wait(&cw_cond, &cw_mutex);
    pthread_cleanup_pop(1);
    return NULL;
}

static volatile sig_atomic_t usr1_thread_hit;
static pthread_t usr1_target;
static void on_usr1(int sig)
{
    (void)sig;
    if (pthread_equal(pthread_self(), usr1_target)) usr1_thread_hit = 1;
}
static volatile int kill_ready;
static void *kill_target_fn(void *arg)
{
    (void)arg;
    kill_ready = 1;
    while (!usr1_thread_hit) usleep(1000);
    return NULL;
}

static pthread_mutex_t rec_mutex;
static volatile long rec_counter;
static void *rec_fn(void *arg)
{
    (void)arg;
    for (int i = 0; i < 2000; i++) {
        pthread_mutex_lock(&rec_mutex);
        pthread_mutex_lock(&rec_mutex);        /* recursive re-entry */
        rec_counter++;
        pthread_mutex_unlock(&rec_mutex);
        pthread_mutex_unlock(&rec_mutex);
    }
    return NULL;
}

static pthread_rwlock_t rw = PTHREAD_RWLOCK_INITIALIZER;
static volatile long rw_value, rw_bad;
static void *rw_reader(void *arg)
{
    (void)arg;
    for (int i = 0; i < 2000; i++) {
        pthread_rwlock_rdlock(&rw);
        long a = rw_value; long b = rw_value;
        if (a != b || (a & 1)) rw_bad++;       /* writer holds it odd */
        pthread_rwlock_unlock(&rw);
    }
    return NULL;
}
static void *rw_writer(void *arg)
{
    (void)arg;
    for (int i = 0; i < 1000; i++) {
        pthread_rwlock_wrlock(&rw);
        rw_value++; rw_value++;
        pthread_rwlock_unlock(&rw);
    }
    return NULL;
}

static pthread_mutex_t ec_mutex;
static pthread_cond_t ec_cond = PTHREAD_COND_INITIALIZER;
static volatile int ec_ready;
static void *ec_fn(void *arg)
{
    (void)arg;
    usleep(20000);
    pthread_mutex_lock(&ec_mutex);
    ec_ready = 1;
    pthread_cond_signal(&ec_cond);
    pthread_mutex_unlock(&ec_mutex);
    return NULL;
}

int main(void)
{
    pthread_t t;
    void *rv;

    alarm(60);
    put("pthread_lifecycle_smoketest: start\n");

    check(pthread_create(&t, NULL, ret_arg, (void *)0x1234) == 0 &&
          pthread_join(t, &rv) == 0 && rv == (void *)0x1234,
          "pthread_join returns the thread's return value");

    check(pthread_create(&t, NULL, exit_arg, (void *)0x5678) == 0 &&
          pthread_join(t, &rv) == 0 && rv == (void *)0x5678,
          "pthread_exit value reaches pthread_join");

    int det = pthread_create(&t, NULL, detached_fn, NULL) == 0 && pthread_detach(t) == 0;
    for (int i = 0; i < 5000 && !detached_ran; i++) usleep(1000);
    check(det && detached_ran, "pthread_detach'd thread runs");

    int ok = pthread_create(&t, NULL, testcancel_fn, NULL) == 0;
    while (!spin_started) usleep(1000);
    ok = ok && pthread_cancel(t) == 0 && pthread_join(t, &rv) == 0 && rv == PTHREAD_CANCELED;
    check(ok, "pthread_cancel + pthread_testcancel -> PTHREAD_CANCELED");

    ok = pthread_create(&t, NULL, condwait_fn, NULL) == 0;
    while (!cw_waiting) usleep(1000);
    pthread_mutex_lock(&cw_mutex);   /* waiter is now inside cond_wait */
    pthread_mutex_unlock(&cw_mutex);
    ok = ok && pthread_cancel(t) == 0 && pthread_join(t, &rv) == 0 && rv == PTHREAD_CANCELED;
    check(ok, "pthread_cancel interrupts pthread_cond_wait");

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_usr1;
    sigaction(SIGUSR1, &sa, NULL);
    ok = pthread_create(&usr1_target, NULL, kill_target_fn, NULL) == 0;
    while (!kill_ready) usleep(1000);
    ok = ok && pthread_kill(usr1_target, SIGUSR1) == 0 &&
         pthread_join(usr1_target, NULL) == 0 && usr1_thread_hit;
    check(ok, "pthread_kill delivers SIGUSR1 to the target thread");

    pid_t pid;
    int status = -1;
    char *argv[] = { "/bin/echo", "  posix_spawn child ran", NULL };
    ok = posix_spawn(&pid, "/bin/echo", NULL, NULL, argv, environ) == 0 &&
         waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    check(ok, "posix_spawn /bin/echo + waitpid, exit 0");

    pthread_mutexattr_t ma;
    pthread_mutexattr_init(&ma);
    pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&rec_mutex, &ma);
    pthread_t rt[3];
    ok = 1;
    for (int i = 0; i < 3; i++) ok = ok && pthread_create(&rt[i], NULL, rec_fn, NULL) == 0;
    for (int i = 0; i < 3; i++) pthread_join(rt[i], NULL);
    check(ok && rec_counter == 6000, "contended RECURSIVE mutex, 3 threads x 2000");

    pthread_t rd[3], wr;
    ok = pthread_create(&wr, NULL, rw_writer, NULL) == 0;
    for (int i = 0; i < 3; i++) ok = ok && pthread_create(&rd[i], NULL, rw_reader, NULL) == 0;
    pthread_join(wr, NULL);
    for (int i = 0; i < 3; i++) pthread_join(rd[i], NULL);
    check(ok && rw_value == 2000 && rw_bad == 0, "contended rwlock, 3 readers + 1 writer");

    pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_init(&ec_mutex, &ma);
    ok = pthread_create(&t, NULL, ec_fn, NULL) == 0;
    pthread_mutex_lock(&ec_mutex);
    while (ok && !ec_ready) pthread_cond_wait(&ec_cond, &ec_mutex);
    pthread_mutex_unlock(&ec_mutex);
    pthread_join(t, NULL);
    check(ok && ec_ready, "pthread_cond_wait on an ERRORCHECK mutex");

    put(failures ? "pthread_lifecycle_smoketest: FAIL\n" : "pthread_lifecycle_smoketest: PASS\n");
    return failures ? 1 : 0;
}
