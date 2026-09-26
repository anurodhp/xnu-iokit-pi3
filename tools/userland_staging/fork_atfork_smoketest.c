/* DAR-130 smoketest: fork()'s child side runs the real _libc_fork_child()
 * (third_party/Libc/sys/_libc_fork_child.c), called from libsystem_c.dylib's
 * fork() (libsystem_c_fork_compat.c).
 *
 *  1. arc4random: _arc4_fork_child() reseeds the child, so parent and child
 *     draw different streams after fork(). Without it they are identical.
 *  2. environ lock: a second thread holds __environ_lock() across the
 *     fork(). The child's setenv() then only succeeds because
 *     __environ_lock_fork_child() reset its copy of the lock.
 *  3. _init_clock_port(): aborts on failure, so the child merely reaching
 *     its checks shows it succeeded.
 *
 * Prints one PASS/FAIL line for tools/boot_test.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>

extern void __environ_lock(void);
extern void __environ_unlock(void);

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t c = PTHREAD_COND_INITIALIZER;
static int holding, release_it;

static void *
holder(void *arg)
{
	(void)arg;
	__environ_lock();
	pthread_mutex_lock(&m);
	holding = 1;
	pthread_cond_signal(&c);
	while (!release_it)
		pthread_cond_wait(&c, &m);
	pthread_mutex_unlock(&m);
	__environ_unlock();
	return NULL;
}

int
main(void)
{
	int fds[2];
	uint32_t parent[4], child[4];
	pthread_t t;

	(void)arc4random(); /* make sure the parent's generator is seeded */
	if (pipe(fds) != 0 || pthread_create(&t, NULL, holder, NULL) != 0) {
		printf("fork_atfork_smoketest: FAIL setup\n");
		return 1;
	}
	pthread_mutex_lock(&m);
	while (!holding)
		pthread_cond_wait(&c, &m);
	pthread_mutex_unlock(&m);

	pid_t pid = fork();
	if (pid == 0) {
		for (int i = 0; i < 4; i++)
			child[i] = arc4random();
		/* Would block forever on the inherited, held lock without the reset. */
		int ok = setenv("DAR130", "child", 1) == 0 &&
		    strcmp(getenv("DAR130"), "child") == 0;
		child[0] ^= ok ? 0 : 0xffffffff; /* poison on failure */
		write(fds[1], child, sizeof(child));
		write(fds[1], &ok, sizeof(ok));
		_exit(0);
	}
	for (int i = 0; i < 4; i++)
		parent[i] = arc4random();

	pthread_mutex_lock(&m);
	release_it = 1;
	pthread_cond_signal(&c);
	pthread_mutex_unlock(&m);

	int ok = 0, status = -1;
	ssize_t n = read(fds[0], child, sizeof(child));
	n += read(fds[0], &ok, sizeof(ok));
	waitpid(pid, &status, 0);

	int same = memcmp(parent, child, sizeof(parent)) == 0;
	printf("fork_atfork_smoketest: child_read=%zd exit=%d setenv_in_child=%d "
	    "arc4random_same_as_parent=%d\n", n, status, ok, same);
	if (n != (ssize_t)(sizeof(child) + sizeof(ok)) || status != 0 || !ok || same) {
		printf("fork_atfork_smoketest: FAIL\n");
		return 1;
	}
	printf("fork_atfork_smoketest: PASS\n");
	return 0;
}
