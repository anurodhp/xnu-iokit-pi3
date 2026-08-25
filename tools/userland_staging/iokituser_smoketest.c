/* Real smoketest for build_iokituser.sh's libiokituser.a.
 *
 * Scope note: this project's port of third_party/IOKitUser is
 * deliberately narrow (see build_iokituser.sh's own header comment for
 * the full why) -- IOKitLib.c itself (IOServiceGetMatchingService,
 * IOServiceOpen, IOConnectCallMethod, etc) cannot build here because it
 * hard-depends on CoreFoundation/libdispatch/XPC/ASL, none of which this
 * project has ported. The one real, unmodified IOKitUser .c file that
 * compiles cleanly with zero CF/dispatch/xpc dependency is
 * IODataQueueClient.c -- this smoketest exercises it for real:
 *
 *   1. IODataQueueAllocateNotificationPort() -- a REAL round trip to
 *      this project's real, unmodified xnu-7195 kernel
 *      (mach_port_allocate + mach_port_get_attributes +
 *      mach_port_set_attributes, all real MIG client stubs regenerated
 *      fresh from osfmk/mach/mach_port.defs by this same build script).
 *      Confirms the kernel actually services a brand-new Mach IPC
 *      subsystem this project had never exercised before this pass.
 *   2. IODataQueueEnqueue()/IODataQueueDequeue() -- real, unmodified
 *      Apple queue-management logic (wraparound math, head/tail atomics)
 *      exercised against a small queue built by hand in this
 *      translation unit's own static storage (standing in for the
 *      kernel-shared memory region IOConnectMapMemory() would normally
 *      provide -- IOConnectMapMemory lives in IOKitLib.c, which cannot
 *      build here; see above). This does NOT prove the kernel's own
 *      IODataQueue-owning IOUserClient side works end-to-end -- only
 *      that the real client-side parsing/enqueue/dequeue logic is
 *      correct against this project's exact freestanding build flags.
 *
 * write(2)-based output only, matching mach_ipc_smoketest.c's own
 * link-order-independent precedent (no libc_stdio dependency needed).
 */
#include <stdint.h>
#include <IOKit/IODataQueueClient.h>

extern long write(int fd, const void *buf, unsigned long len);
extern void _exit(int code);
/* <string.h> (libsyscall/mach/string.h, pulled in transitively via the
 * -idirafter osfmk search path) only declares memcpy, not memcmp -- same
 * minimal freestanding substitute other smoketests in this project hit.
 * Declared here directly rather than widening that shared header. */
extern int memcmp(const void *a, const void *b, unsigned long n);
extern void *memset(void *b, int c, unsigned long n);

static void put(const char *s)
{
	unsigned long n = 0;
	while (s[n]) {
		n++;
	}
	write(1, s, n);
}

static void put_u32(uint32_t v)
{
	char buf[12];
	int i = 11;
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

/* Fake kernel-shared queue region: header + 256 bytes of queue storage,
 * enough for a couple of small entries plus their DATA_QUEUE_ENTRY_
 * HEADER_SIZE overhead each. Real IODataQueueMemory layout (queueSize/
 * head/tail/queue[]), just backed by ordinary .bss instead of a real
 * IOConnectMapMemory() mapping. */
static struct {
	IODataQueueMemory hdr;
	uint8_t extra[256];
} g_fake_queue_storage;

int main(void)
{
	put("iokituser_smoketest: start\n");

	/* --- Part 1: real Mach IPC round trip to the real kernel --- */
	mach_port_t notifyPort = IODataQueueAllocateNotificationPort();
	if (notifyPort == MACH_PORT_NULL) {
		put("iokituser_smoketest: IODataQueueAllocateNotificationPort FAILED (MACH_PORT_NULL)\n");
	} else {
		put("iokituser_smoketest: IODataQueueAllocateNotificationPort OK, port=");
		put_u32((uint32_t)notifyPort);
		put("\n");
	}

	/* --- Part 2: real Apple enqueue/dequeue logic, local storage --- */
	IODataQueueMemory *dq = &g_fake_queue_storage.hdr;
	memset(&g_fake_queue_storage, 0, sizeof(g_fake_queue_storage));
	dq->queueSize = (UInt32)sizeof(g_fake_queue_storage.extra) + (UInt32)sizeof(dq->queue[0].data) - 4u;
	/* queueSize covers dq->queue[0] (already counted in IODataQueueMemory)
	 * plus the extra bytes appended after it -- matches how a real
	 * kernel-mapped IODataQueueMemory region is sized (header struct's
	 * trailing IODataQueueEntry queue[1] is just the first slot). */
	dq->queueSize = (UInt32)sizeof(g_fake_queue_storage.extra) + (UInt32)sizeof(((IODataQueueEntry *)0)->data);

	const char msg1[] = "hello";
	const char msg2[] = "world!";

	IOReturn kr1 = IODataQueueEnqueue(dq, (void *)msg1, (uint32_t)sizeof(msg1));
	IOReturn kr2 = IODataQueueEnqueue(dq, (void *)msg2, (uint32_t)sizeof(msg2));
	put("iokituser_smoketest: enqueue kr1=");
	put_u32((uint32_t)kr1);
	put(" kr2=");
	put_u32((uint32_t)kr2);
	put("\n");

	char out1[32];
	uint32_t out1size = sizeof(out1);
	IOReturn dr1 = IODataQueueDequeue(dq, out1, &out1size);

	char out2[32];
	uint32_t out2size = sizeof(out2);
	IOReturn dr2 = IODataQueueDequeue(dq, out2, &out2size);

	int ok1 = (dr1 == 0 /* kIOReturnSuccess */ && out1size == sizeof(msg1) && memcmp(out1, msg1, sizeof(msg1)) == 0);
	int ok2 = (dr2 == 0 && out2size == sizeof(msg2) && memcmp(out2, msg2, sizeof(msg2)) == 0);

	put("iokituser_smoketest: dequeue1 dr=");
	put_u32((uint32_t)dr1);
	put(ok1 ? " DATA_MATCH\n" : " DATA_MISMATCH\n");
	put("iokituser_smoketest: dequeue2 dr=");
	put_u32((uint32_t)dr2);
	put(ok2 ? " DATA_MATCH\n" : " DATA_MISMATCH\n");

	put(ok1 && ok2 ? "iokituser_smoketest: PASS\n" : "iokituser_smoketest: FAIL\n");
	put("iokituser_smoketest: done\n");
	_exit(ok1 && ok2 ? 0 : 1);
	return 0;
}
