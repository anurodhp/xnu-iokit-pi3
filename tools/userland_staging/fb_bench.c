/*
 * fb_bench: write bandwidth into the live framebuffer (VRAM) through PDGOP,
 * the way the puredarwingop X driver's shadow update writes it (DARW-302).
 * Maps the framebuffer write-combined (as xorg.conf's VRAMCacheMode
 * "writecombine"), reads the current screen once, then writes that same
 * image back several times (so nothing visible changes) with:
 *   - the driver's PDGOPCopyRun loop (aligned 16-byte stores),
 *   - memcpy,
 *   - and, for comparison, the same loops into a malloc'd buffer (RAM).
 * Prints MB/s for each. Usage: fb_bench [reps]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "PDGOP.h"

typedef uint32_t Vec __attribute__((vector_size(16), aligned(16)));

/* puredarwingop_drv.c PDGOPCopyRun, unchanged */
static void
copy_run(uint8_t *dst, const uint8_t *src, size_t bytes)
{
	while (bytes >= 4 && ((uintptr_t)dst & 15)) {
		*(volatile uint32_t *)dst = *(const uint32_t *)src;
		dst += 4; src += 4; bytes -= 4;
	}
	while (bytes >= 64) {
		Vec a, b, c, d;
		memcpy(&a, src, 16);
		memcpy(&b, src + 16, 16);
		memcpy(&c, src + 32, 16);
		memcpy(&d, src + 48, 16);
		((volatile Vec *)dst)[0] = a;
		((volatile Vec *)dst)[1] = b;
		((volatile Vec *)dst)[2] = c;
		((volatile Vec *)dst)[3] = d;
		dst += 64; src += 64; bytes -= 64;
	}
	while (bytes >= 4) {
		*(volatile uint32_t *)dst = *(const uint32_t *)src;
		dst += 4; src += 4; bytes -= 4;
	}
}

static double
now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void
report(const char *what, size_t n, int reps, double t)
{
	printf("%-28s %7.1f MB/s  (%.2f ms per %zu KB)\n", what, (double)n * reps / t / 1e6,
	    t / reps * 1e3, n / 1024);
}

int
main(int argc, char **argv)
{
	int reps = argc > 1 ? atoi(argv[1]) : 10;
	PDGOPFramebuffer fb;
	if (PDGOPOpenWithCacheMode(&fb, PDGOP_MAP_WRITE_COMBINE) != KERN_SUCCESS) {
		printf("PDGOPOpen failed at %s\n", PDGOPLastErrorStage());
		return 1;
	}
	size_t n = (size_t)fb.stride * fb.height;
	printf("framebuffer %ux%u, %u bytes/row, %zu KB, map options %#x\n",
	    fb.width, fb.height, fb.stride, n / 1024, fb.mapOptions);
	uint8_t *vram = (uint8_t *)(uintptr_t)fb.address;
	uint8_t *img = malloc(n), *ram = malloc(n);
	double t = now_s();
	memcpy(img, vram, n);                    /* the current screen */
	t = now_s() - t;
	report("VRAM -> RAM (read)", n, 1, t);

	t = now_s();
	for (int i = 0; i < reps; i++)
		copy_run(vram, img, n);
	report("RAM -> VRAM copy_run", n, reps, now_s() - t);

	t = now_s();
	for (int i = 0; i < reps; i++)
		memcpy(vram, img, n);
	report("RAM -> VRAM memcpy", n, reps, now_s() - t);

	t = now_s();
	for (int i = 0; i < reps; i++)
		copy_run(ram, img, n);
	report("RAM -> RAM copy_run", n, reps, now_s() - t);

	t = now_s();
	for (int i = 0; i < reps; i++)
		memcpy(ram, img, n);
	report("RAM -> RAM memcpy", n, reps, now_s() - t);

	PDGOPClose(&fb);
	return 0;
}
