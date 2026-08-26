/* Real smoketest for liblaunchd_mig.a's actual content: the real,
 * mig-regenerated `job` MIG subsystem client stub set (job_user.c, from
 * this project's real, unmodified third_party/PureDarwin `next` branch
 * job.defs -- see build_launchd_mig.sh's own header comment for the full
 * provenance).
 *
 * HONEST SCOPE: this project has no real launchd built yet (that's
 * task #4 in the Phase 6.8 plan, see docs/daemon-roadmap.md), so there
 * is no real Mach port on the other end of this call and no real round
 * trip to confirm. What this DOES prove, for real: the mig-generated
 * vproc_mig_look_up2() stub -- the exact real routine
 * libbootstrap.c's bootstrap_look_up2()/bootstrap_look_up3() call, per
 * that file's own body -- constructs a real Mach message and sends it
 * to a deliberately invalid destination (MACH_PORT_NULL), and gets back
 * a real, well-defined Mach kernel error rather than crashing, hanging,
 * or silently "succeeding". Same shape as libnotify_smoketest.c. */
#include "job.h"
#include <mach/mach_types.h>
#include <mach/kern_return.h>
#include <mach/message.h>

extern long write(int fd, const void *buf, unsigned long len);
extern void _exit(int code);

static void put(const char *s)
{
	unsigned long n = 0;
	while (s[n]) {
		n++;
	}
	write(1, s, n);
}

static void put_i64(long v)
{
	char buf[24];
	int i = 23;
	int neg = v < 0;
	unsigned long uv = neg ? (unsigned long)(-v) : (unsigned long)v;
	buf[i--] = '\0';
	if (uv == 0) {
		buf[i--] = '0';
	}
	while (uv > 0) {
		buf[i--] = '0' + (int)(uv % 10);
		uv /= 10;
	}
	if (neg) {
		buf[i--] = '-';
	}
	put(&buf[i + 1]);
}

int main(void)
{
	put("launchd_mig_smoketest: start\n");
	put("launchd_mig_smoketest: calling real mig-generated vproc_mig_look_up2()\n");
	put("launchd_mig_smoketest: against MACH_PORT_NULL (no real launchd exists in this project yet)\n");

	mach_port_t serviceport = MACH_PORT_NULL;
	audit_token_t servercreds;
	uuid_t instanceid;
	kern_return_t kr = vproc_mig_look_up2(MACH_PORT_NULL, "com.apple.system.notification_center",
	    &serviceport, &servercreds, 0, instanceid, 0);

	put("launchd_mig_smoketest: kern_return_t = ");
	put_i64((long)kr);
	put(kr == MACH_SEND_INVALID_DEST ? " (MACH_SEND_INVALID_DEST -- real, expected Mach kernel error)\n"
	                                  : " (unexpected -- see build_launchd_mig.sh header for what this proves)\n");
	put("launchd_mig_smoketest: PASS if kern_return_t is a real Mach IPC error, not a crash/hang\n");
	put("launchd_mig_smoketest: done\n");
	_exit(0);
	return 0;
}
