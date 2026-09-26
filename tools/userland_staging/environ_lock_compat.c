/* Environ lock for third_party/Libc's real stdlib/FreeBSD/setenv.c, which
 * calls __environ_lock()/__environ_unlock() around its environ-array
 * mutations. Real Apple defines the pair in stdlib/FreeBSD/getenv.c
 * (lines 82-98), which this project does not compile (see
 * build_libsystem_c.sh's getenv notes), so the three functions are
 * reproduced here from those lines.
 *
 * This used to be a no-op pair justified by "no real second-thread caller
 * of setenv/unsetenv/putenv yet". That stopped being true once
 * pthread_create() worked (Phase 7.5), and DAR-130 needs the real lock so
 * the fork child can reset it: a fork() taken while another thread held
 * the lock would otherwise leave the child's copy locked forever.
 *
 * One deviation: os_unfair_lock_lock() instead of real getenv.c's
 * os_unfair_lock_lock_with_options(..., OS_UNFAIR_LOCK_DATA_SYNCHRONIZATION).
 * That option lives in <os/lock_private.h>, which this file's consumers
 * (build_libsystem_c.sh, build_libxpc_syscalls.sh) do not have on their
 * include path; it only tunes how a waiter behaves
 * (third_party/libplatform/private/os/lock_private.h), not mutual
 * exclusion. */
#include <os/lock.h>

static os_unfair_lock __environ_lock_obj = OS_UNFAIR_LOCK_INIT;

void
__environ_lock(void)
{
	os_unfair_lock_lock(&__environ_lock_obj);
}

void
__environ_unlock(void)
{
	os_unfair_lock_unlock(&__environ_lock_obj);
}

/* Called from _libc_fork_child() (third_party/Libc/sys/_libc_fork_child.c). */
void
__environ_lock_fork_child(void)
{
	__environ_lock_obj = OS_UNFAIR_LOCK_INIT;
}
