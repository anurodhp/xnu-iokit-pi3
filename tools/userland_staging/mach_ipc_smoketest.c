/* Real smoketest for Phase 4.6 item 5's Mach IPC work: exercises the
 * exact real chain simple_cmds_nanosleep_pub_compat.c's real nanosleep()
 * calls (mach_host_self -> host_get_clock_service -> semaphore_create,
 * once, then clock_get_time + __semwait_signal per call), printing
 * mach_absolute_time() before/after a real 3-second nanosleep() (long
 * enough to be unambiguous against QEMU/TCG's own known timing jitter,
 * and to leave a comfortable window for monitor-based PC sampling during
 * the wait) so a QEMU boot-test can confirm real IPC-backed blocking
 * happened (elapsed time
 * in the right ballpark) rather than either the old bug (spins, but
 * elapsed time still roughly matches -- not distinguishing) or a
 * silently-broken semaphore wait (returns instantly, elapsed time near
 * zero) or a hang (never returns at all, this print never appears).
 * write(2)-based output only (no libc_stdio dependency needed for a
 * link-order-independent smoketest, matching pthread_tsd_shim_smoketest.c's
 * own precedent). */
#include <stdint.h>
#include <time.h>

extern uint64_t mach_absolute_time(void);
extern int nanosleep(const struct timespec *req, struct timespec *rem);
extern long write(int fd, const void *buf, unsigned long len);
extern void _exit(int code);
struct mach_timebase_info_data { unsigned int numer; unsigned int denom; };
extern int mach_timebase_info(struct mach_timebase_info_data *info);

static void put(const char *s)
{
	unsigned long n = 0;
	while (s[n]) {
		n++;
	}
	write(1, s, n);
}

static void put_u64(uint64_t v)
{
	char buf[24];
	int i = 23;
	buf[i--] = '\0';
	if (v == 0) {
		buf[i--] = '0';
	}
	while (v > 0) {
		buf[i--] = '0' + (int)(v % 10);
		v /= 10;
	}
	put(&buf[i + 1]);
}

int main(void)
{
	put("mach_ipc_smoketest: start\n");

	uint64_t before = mach_absolute_time();

	struct timespec req = {3, 0}; /* 3s -- long enough to be unambiguous
	                                  against QEMU/TCG timing jitter, and
	                                  to leave a wide window for the
	                                  monitor-PC-sampling variant of this
	                                  test. */
	struct timespec rem = {0, 0};
	int ret = nanosleep(&req, &rem);

	uint64_t after = mach_absolute_time();

	struct mach_timebase_info_data tb = {1, 1};
	mach_timebase_info(&tb);
	/* elapsed_ms = (after-before) * numer / denom / 1e6, done in integer
	 * math avoiding overflow for realistic tick counts/durations here. */
	uint64_t elapsed_ns = (after - before) * (uint64_t)tb.numer / (uint64_t)tb.denom;
	uint64_t elapsed_ms = elapsed_ns / 1000000ULL;

	put("mach_ipc_smoketest: nanosleep() returned ");
	put_u64((uint64_t)(long)ret);
	put("\n");
	put("mach_ipc_smoketest: mach_absolute_time before=");
	put_u64(before);
	put(" after=");
	put_u64(after);
	put(" delta_ticks=");
	put_u64(after - before);
	put("\n");
	put("mach_ipc_smoketest: timebase numer=");
	put_u64(tb.numer);
	put(" denom=");
	put_u64(tb.denom);
	put(" ELAPSED_MS=");
	put_u64(elapsed_ms);
	put("\n");
	put("mach_ipc_smoketest: done\n");
	_exit(0);
	return 0;
}
