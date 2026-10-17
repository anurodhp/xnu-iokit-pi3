/*
 * sample_prof.dylib: a sampling CPU profiler to inject into any process.
 *
 *   DYLD_INSERT_LIBRARIES=/usr/local/lib/sample_prof.dylib <program> ...
 *
 * A SIGPROF timer (ITIMER_PROF, every SAMPLE_PROF_US microseconds of CPU
 * time, default 1000) records the interrupted thread's pc; at exit the
 * functions with the most samples are printed to stderr (or to the file
 * SAMPLE_PROF_OUT), named with dladdr(): "image`symbol+offset", the
 * offset dropped when grouping. Symbols are what the images export or
 * keep in their symbol tables; a static function shows up as the nearest
 * exported symbol before it, so read the table as "around here". The main
 * thread's samples are also listed on their own (a GUI program draws
 * there). A pc in a system-call stub (__gettimeofday, mach_msg_trap, ...)
 * is time in that call.
 *
 * SAMPLE_PROF_WALL=1: wall-clock samples of the main thread alone instead
 * (ITIMER_REAL; SIGALRM is blocked in every other thread, which this
 * library's pthread_create interposer arranges), each with an 8-frame
 * frame-pointer backtrace, reported as the most common stacks. That is the
 * mode for "where does this thread wait": a blocked call shows as its
 * system-call stub with its callers.
 *
 * Built by build_sample_prof.sh.
 */
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/ucontext.h>

#define MAXS (1 << 18)
#define DEPTH 16

static uintptr_t samples[MAXS];
static uint8_t on_main[MAXS];
static volatile uint32_t nsamples;
static int wall;
static uintptr_t stacks[MAXS / 8][DEPTH];       /* wall mode: pc + 7 callers */

static void
on_prof(int sig, siginfo_t *si, void *uc)
{
	(void)sig;
	(void)si;
	uint32_t i = __atomic_fetch_add(&nsamples, 1, __ATOMIC_RELAXED);
	if (i < MAXS) {
		samples[i] = (uintptr_t)((ucontext_t *)uc)->uc_mcontext->__ss.__pc;
		on_main[i] = (uint8_t)pthread_main_np();
		if (wall && i < MAXS / 8) {
			/* walk the frame-pointer chain: fp[0] = caller's fp, fp[1] = lr */
			uintptr_t *fp = (uintptr_t *)((ucontext_t *)uc)->uc_mcontext->__ss.__fp;
			stacks[i][0] = samples[i];
			stacks[i][1] = (uintptr_t)((ucontext_t *)uc)->uc_mcontext->__ss.__lr;
			for (int d = 2; d < DEPTH; d++) {
				if (fp == NULL || ((uintptr_t)fp & 7) != 0) {
					break;
				}
				uintptr_t *next = (uintptr_t *)fp[0];
				stacks[i][d] = fp[1];
				if (next <= fp) {
					break;
				}
				fp = next;
			}
		}
	}
}

/* wall mode: other threads must not take SIGALRM */
struct tramp {
	void *(*fn)(void *);
	void *arg;
};

static void *
tramp_main(void *p)
{
	struct tramp t = *(struct tramp *)p;
	sigset_t set;
	free(p);
	sigemptyset(&set);
	sigaddset(&set, SIGALRM);
	pthread_sigmask(SIG_BLOCK, &set, NULL);
	return t.fn(t.arg);
}

static int
my_pthread_create(pthread_t *th, const pthread_attr_t *attr, void *(*fn)(void *), void *arg)
{
	struct tramp *t = malloc(sizeof(*t));
	if (t == NULL) {
		return pthread_create(th, attr, fn, arg);
	}
	t->fn = fn;
	t->arg = arg;
	return pthread_create(th, attr, tramp_main, t);
}

__attribute__((used)) static struct {
	const void *replacement, *replacee;
} interposers[] __attribute__((section("__DATA,__interpose"))) = {
	{ (const void *)my_pthread_create, (const void *)pthread_create },
};

static const char *
name_of(uintptr_t pc, char *buf, size_t len)
{
	Dl_info info;
	if (pc == 0) {
		return "";
	}
	if (dladdr((void *)pc, &info) == 0 || info.dli_sname == NULL) {
		snprintf(buf, len, "%#lx", (unsigned long)pc);
		return buf;
	}
	snprintf(buf, len, "%s", info.dli_sname);
	return buf;
}

/* wall mode: the most common stacks, by symbol names */
static void
stack_table(FILE *fp, uint32_t n)
{
	if (n > MAXS / 8) {
		n = MAXS / 8;
	}
	char **keys = calloc(n, sizeof(*keys));
	unsigned *counts = calloc(n, sizeof(*counts));
	unsigned nk = 0;
	for (uint32_t i = 0; i < n; i++) {
		char key[4096] = "", b[256];
		for (int d = 0; d < DEPTH; d++) {
			const char *nm = name_of(stacks[i][d], b, sizeof(b));
			if (*nm == 0) {
				break;
			}
			strlcat(key, d ? " < " : "", sizeof(key));
			strlcat(key, nm, sizeof(key));
		}
		unsigned k;
		for (k = 0; k < nk && strcmp(keys[k], key) != 0; k++) {
		}
		if (k == nk) {
			keys[nk++] = strdup(key);
		}
		counts[k]++;
	}
	fprintf(fp, "sample_prof: %u wall-clock samples of the main thread, by stack\n", n);
	for (unsigned shown = 0; shown < (getenv("SAMPLE_PROF_ROWS") ? (unsigned)atoi(getenv("SAMPLE_PROF_ROWS")) : 30); shown++) {
		unsigned best = 0, bi = 0;
		for (unsigned k = 0; k < nk; k++) {
			if (counts[k] > best) {
				best = counts[k];
				bi = k;
			}
		}
		if (best == 0) {
			break;
		}
		fprintf(fp, "%6u %5.1f%%  %s\n", best, 100.0 * best / (n ? n : 1), keys[bi]);
		counts[bi] = 0;
	}
}

struct entry {
	const char *image, *sym;
	unsigned count;
};

static int
by_count(const void *a, const void *b)
{
	return (int)((const struct entry *)b)->count - (int)((const struct entry *)a)->count;
}

static void table(FILE *fp, uint32_t n, int main_only);

static void
report(void)
{
	struct itimerval off = { { 0, 0 }, { 0, 0 } };
	setitimer(ITIMER_PROF, &off, NULL);
	uint32_t n = nsamples < MAXS ? nsamples : MAXS;
	const char *path = getenv("SAMPLE_PROF_OUT");
	FILE *fp = path ? fopen(path, "w") : stderr;
	if (fp == NULL) {
		fp = stderr;
	}
	if (wall) {
		stack_table(fp, n);
		table(fp, n, 1);
	} else {
		table(fp, n, 0);
		table(fp, n, 1);
	}
	if (fp != stderr) {
		fclose(fp);
	}
}

static void
table(FILE *fp, uint32_t n, int main_only)
{
	struct entry *e = calloc(n + 1, sizeof(*e));
	unsigned ne = 0;
	uint32_t taken = 0;
	for (uint32_t i = 0; i < n; i++) {
		Dl_info info;
		if (main_only && !on_main[i]) {
			continue;
		}
		taken++;
		const char *image = "?", *sym = "?";
		if (dladdr((void *)samples[i], &info) != 0) {
			image = info.dli_fname ? (strrchr(info.dli_fname, '/') ? strrchr(info.dli_fname, '/') + 1 : info.dli_fname) : "?";
			sym = info.dli_sname ? info.dli_sname : "?";
		}
		unsigned k;
		for (k = 0; k < ne && !(e[k].sym == sym && e[k].image == image); k++) {
		}
		if (k == ne) {
			e[ne].image = image;
			e[ne].sym = sym;
			ne++;
		}
		e[k].count++;
	}
	qsort(e, ne, sizeof(*e), by_count);
	fprintf(fp, "sample_prof: %u samples%s\n", taken, main_only ? " on the main thread" : ", all threads");
	for (unsigned k = 0; k < ne && k < 40; k++) {
		fprintf(fp, "%6u %5.1f%%  %s`%s\n", e[k].count, 100.0 * e[k].count / (taken ? taken : 1), e[k].image, e[k].sym);
	}
	free(e);
}

__attribute__((constructor)) static void
start(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = on_prof;
	sa.sa_flags = SA_SIGINFO | SA_RESTART;
	wall = getenv("SAMPLE_PROF_WALL") != NULL;
	sigaction(wall ? SIGALRM : SIGPROF, &sa, NULL);
	long us = getenv("SAMPLE_PROF_US") ? atol(getenv("SAMPLE_PROF_US")) : 1000;
	struct itimerval it = { { 0, (int)us }, { 0, (int)us } };
	setitimer(wall ? ITIMER_REAL : ITIMER_PROF, &it, NULL);
	atexit(report);
}
