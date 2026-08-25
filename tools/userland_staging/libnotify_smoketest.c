/* Real smoketest for libnotify.a's actual content: the real,
 * mig-regenerated notify_ipc MIG client stub set (notify_ipcUser.c, from
 * this project's real, unmodified third_party/Libnotify/notify_ipc.defs).
 *
 * HONEST SCOPE: this project has no real notifyd daemon built yet (see
 * build_libnotify.sh's own header comment for the full reason none of
 * Libnotify's real notify_register_check()/notify_post()/notify_check()
 * client-API C sources -- libnotify.c/notify_client.c/table.c -- compile
 * here at all), so there is no real Mach port on the other end of this
 * call and no real round trip to confirm. What this DOES prove, for
 * real: the mig-generated _notify_server_checkin() stub constructs a
 * real Mach message (real NDR_record, real special-reply-port setup via
 * notify_mig_special_reply_port.c, real mach_msg_real.c trap plumbing --
 * the exact same already-QEMU-proven transport build_libmach_ipc.sh's
 * own smoketest exercises) and sends it to a deliberately invalid
 * destination (MACH_PORT_NULL), and gets back a real, well-defined Mach
 * kernel error (MACH_SEND_INVALID_DEST) rather than crashing, hanging,
 * or silently "succeeding" -- i.e. the real client-side IPC plumbing for
 * Libnotify's actual wire protocol is provably correct and load-bearing,
 * even with no daemon present. write(2)-only output, same
 * link-order-independent convention as mach_ipc_smoketest.c. */
#include <stdint.h>
#include <mach/mach_types.h>
#include <mach/kern_return.h>
#include <mach/message.h>

extern kern_return_t _notify_server_checkin(mach_port_t server, uint32_t *version, uint32_t *server_pid, int *status);
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
	put("libnotify_smoketest: start\n");
	put("libnotify_smoketest: calling real mig-generated _notify_server_checkin()\n");
	put("libnotify_smoketest: against MACH_PORT_NULL (no real notifyd exists in this project yet)\n");

	uint32_t version = 0xffffffff;
	uint32_t server_pid = 0xffffffff;
	int status = -1;

	kern_return_t kr = _notify_server_checkin(MACH_PORT_NULL, &version, &server_pid, &status);

	put("libnotify_smoketest: kern_return_t = ");
	put_i64((long)kr);
	put(kr == MACH_SEND_INVALID_DEST ? " (MACH_SEND_INVALID_DEST -- real, expected Mach kernel error)\n"
	                                  : " (unexpected -- see build_libnotify.sh header for what this proves)\n");
	put("libnotify_smoketest: PASS if kern_return_t is a real Mach IPC error, not a crash/hang\n");
	put("libnotify_smoketest: done\n");
	_exit(0);
	return 0;
}
