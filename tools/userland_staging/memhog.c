/*
 * memhog: a memory-pressure test aid for the jetsam bands (jetsam_qemu_test.sh).
 *
 *   memhog [-b band] [MB-per-step]
 *
 * Re-execs itself into jetsam band `band` (default 10, JETSAM_PRIORITY_FOREGROUND,
 * the band applications are launched in), then allocates and touches memory
 * MB-per-step (default 16) at a time, printing the running total, until the
 * kernel kills it. The re-exec is posix_spawn with POSIX_SPAWN_SETEXEC and
 * posix_spawnattr_setjetsam_ext, the same unprivileged path NSTask's
 * -setJetsamPriority: uses (xnu-7195 bsd/kern/kern_exec.c:3859-3884), so a
 * memhog killed first under pressure, with the session and the daemons left
 * alone, is what an application using too much memory should get. The
 * pages are written with varying data so the compressor cannot shrink them
 * to nothing.
 */
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* not in the iPhoneOS SDK's headers: xnu-7195
 * libsyscall/wrappers/spawn/spawn_private.h:49 (libsystem_kernel exports it) */
extern int posix_spawnattr_setjetsam_ext(posix_spawnattr_t *attr, short flags, int priority,
    int memlimit_active, int memlimit_inactive);
extern char **environ;

int
main(int argc, char **argv)
{
	int band = 10, step = 16, i = 1;
	size_t total = 0;

	if (i + 1 < argc && strcmp(argv[i], "-b") == 0) {
		band = atoi(argv[i + 1]);
		i += 2;
	}
	if (i < argc)
		step = atoi(argv[i]);
	if (step <= 0)
		step = 16;

	if (getenv("MEMHOG_IN_BAND") == NULL) {
		posix_spawnattr_t attr;
		setenv("MEMHOG_IN_BAND", "1", 1);
		if (posix_spawnattr_init(&attr) == 0 &&
		    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETEXEC) == 0 &&
		    posix_spawnattr_setjetsam_ext(&attr, 0, band, -1, -1) == 0)
			(void)posix_spawn(NULL, argv[0], NULL, &attr, argv, environ);
		fprintf(stderr, "memhog: could not re-exec into band %d; running in the default band\n", band);
	}

	printf("memhog: pid %d, band %d, %d MB per step\n", (int)getpid(), band, step);
	fflush(stdout);
	for (;;) {
		size_t n = (size_t)step << 20, j;
		unsigned char *p = malloc(n);
		if (p == NULL) {
			printf("memhog: malloc failed at %zu MB\n", total >> 20);
			fflush(stdout);
			sleep(1);
			continue;
		}
		for (j = 0; j < n; j += 4096)
			p[j] = (unsigned char)(j * 2654435761u >> 13) ^ (unsigned char)total;
		for (j = 0; j < n; j += 64)
			p[j] ^= (unsigned char)(j >> 6);
		total += n;
		printf("memhog: %zu MB\n", total >> 20);
		fflush(stdout);
		usleep(200000);
	}
}
