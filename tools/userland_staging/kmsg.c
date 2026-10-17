/*
 * kmsg: print the kernel message buffer (what dmesg(8) shows). Root only.
 *
 * xnu's proc_info(PROC_INFO_CALL_KERNMSGBUF), syscall 336, the call libproc's
 * proc_kmsgbuf() makes (bsd/kern/proc_info.c proc_kernmsgbuf). Made as a raw
 * svc so it needs nothing beyond libsystem_c. Found the V3D contiguous
 * allocation failures and the IOAudioEngine::getStatus log flood on a real Pi
 * (2026-10-17), which nothing else on the image could show over ssh.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SYS_proc_info               336
#define PROC_INFO_CALL_KERNMSGBUF   4

static long
raw_proc_info(int callnum, int pid, int flavor, uint64_t arg, void *buf, int size)
{
	register long x0 __asm__("x0") = callnum;
	register long x1 __asm__("x1") = pid;
	register long x2 __asm__("x2") = flavor;
	register long x3 __asm__("x3") = (long)arg;
	register long x4 __asm__("x4") = (long)buf;
	register long x5 __asm__("x5") = size;
	register long x16 __asm__("x16") = SYS_proc_info;
	long err = 0;
	/* carry set on return means x0 is an errno */
	__asm__ volatile("svc #0x80\n b.cc 1f\n mov %0, #1\n 1:"
	    : "+r"(err), "+r"(x0)
	    : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x16)
	    : "memory", "cc");
	return err ? -x0 : x0;
}

int
main(void)
{
	const int size = 1 << 20;
	char *buf = calloc(1, size);
	if (buf == NULL)
		return 1;
	long n = raw_proc_info(PROC_INFO_CALL_KERNMSGBUF, 0, 0, 0, buf, size);
	if (n < 0) {
		fprintf(stderr, "kmsg: proc_info failed (errno %ld)\n", -n);
		return 1;
	}
	fwrite(buf, 1, (size_t)n, stdout);
	return 0;
}
