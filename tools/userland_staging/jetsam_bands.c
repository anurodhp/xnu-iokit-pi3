/*
 * jetsam_bands: print every process's jetsam (memorystatus) priority band,
 * lowest first -- the order the kernel picks victims in under memory pressure.
 * Root only.
 *
 *   jetsam_bands          pid, band (number and name), memory limit, name
 *
 * xnu's memorystatus_control(MEMORYSTATUS_CMD_GET_PRIORITY_LIST), syscall 440
 * (bsd/kern/syscalls.master:686), which needs root or an entitlement
 * (bsd/kern/kern_memorystatus.c:7902). libSystem here does not export
 * memorystatus_control, so it is a raw svc, as in kmsg.c. A NULL buffer asks
 * for the list size (memorystatus_cmd_get_priority_list, :6574-6608). The
 * list comes back ordered by band and, within a band, in the kernel's kill
 * order (oldest first, bucket insertion at :2066), so the first row is the
 * next victim.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#define SYS_memorystatus_control            440
#define MEMORYSTATUS_CMD_GET_PRIORITY_LIST  1

/* bsd/sys/kern_memorystatus.h:111-117 */
typedef struct {
	pid_t pid;
	int32_t priority;
	uint64_t user_data;
	int32_t limit;          /* MB */
	uint32_t state;
} priority_entry_t;

static long
raw_memorystatus_control(uint32_t cmd, int32_t pid, uint32_t flags, void *buf, size_t size)
{
	register long x0 __asm__("x0") = cmd;
	register long x1 __asm__("x1") = pid;
	register long x2 __asm__("x2") = flags;
	register long x3 __asm__("x3") = (long)buf;
	register long x4 __asm__("x4") = (long)size;
	register long x16 __asm__("x16") = SYS_memorystatus_control;
	long err = 0;
	/* carry set on return means x0 is an errno */
	__asm__ volatile("svc #0x80\n b.cc 1f\n mov %0, #1\n 1:"
	    : "+r"(err), "+r"(x0)
	    : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x16)
	    : "memory", "cc");
	return err ? -x0 : x0;
}

/* bsd/sys/kern_memorystatus.h:40-66 */
static const char *
band_name(int p)
{
	switch (p) {
	case -2: return "IDLE_HEAD";
	case 0: return "IDLE";
	case 1: return "AGING_BAND1";
	case 2: return "BACKGROUND_OPPORTUNISTIC";
	case 3: return "BACKGROUND";
	case 4: return "MAIL";
	case 5: return "PHONE";
	case 8: return "UI_SUPPORT";
	case 9: return "FOREGROUND_SUPPORT";
	case 10: return "FOREGROUND";
	case 12: return "AUDIO_AND_ACCESSORY";
	case 13: return "CONDUCTOR";
	case 15: return "DRIVER_APPLE";
	case 16: return "HOME";
	case 17: return "EXECUTIVE";
	case 18: return "IMPORTANT/DEFAULT";
	case 19: return "CRITICAL";
	case 21: return "MAX";
	default: return "";
	}
}

static void
proc_name(pid_t pid, char *out, size_t n)
{
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, pid };
	struct kinfo_proc kp;
	size_t len = sizeof(kp);
	out[0] = '\0';
	if (sysctl(mib, 4, &kp, &len, NULL, 0) == 0 && len == sizeof(kp))
		snprintf(out, n, "%s", kp.kp_proc.p_comm);
}

int
main(void)
{
	long size = raw_memorystatus_control(MEMORYSTATUS_CMD_GET_PRIORITY_LIST, 0, 0, NULL, 0);
	priority_entry_t *list;
	long got, i, n;
	if (size < 0) {
		fprintf(stderr, "jetsam_bands: memorystatus_control: %s%s\n", strerror((int)-size),
		    -size == 1 ? " (root only)" : "");
		return 1;
	}
	size += 8 * sizeof(priority_entry_t);   /* processes started in between */
	if ((list = calloc(1, (size_t)size)) == NULL)
		return 1;
	got = raw_memorystatus_control(MEMORYSTATUS_CMD_GET_PRIORITY_LIST, 0, 0, list, (size_t)size);
	if (got < 0) {
		fprintf(stderr, "jetsam_bands: memorystatus_control: %s\n", strerror((int)-got));
		return 1;
	}
	n = got / (long)sizeof(priority_entry_t);
	printf("%6s %4s %-24s %8s  %s\n", "PID", "BAND", "", "LIMIT", "COMMAND");
	for (i = 0; i < n; i++) {
		char name[64];
		char limit[16];
		proc_name(list[i].pid, name, sizeof(name));
		if (list[i].limit > 0)
			snprintf(limit, sizeof(limit), "%d MB", list[i].limit);
		else
			snprintf(limit, sizeof(limit), "-");
		printf("%6d %4d %-24s %8s  %s\n", list[i].pid, list[i].priority, band_name(list[i].priority), limit, name);
	}
	free(list);
	return 0;
}
