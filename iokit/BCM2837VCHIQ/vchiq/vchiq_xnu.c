/*
 * The out-of-line half of vchiq_xnu.h: allocation, kernel threads, the
 * RCU grace period. See that header for the model.
 */
#include <sys/proc.h>    /* before vchiq_xnu.h: its min()/max() macros */
#include "vchiq_xnu.h"
#include "vchiq_xnu_platform.h"

/*
 * Linux's kfree() takes no size, IOFree() needs one: keep it in front of
 * the block. 16 bytes keep the caller's pointer 16-byte aligned like
 * kmalloc()'s.
 */
#define VCHIQ_XNU_HDR 16

void *
vchiq_xnu_alloc(size_t size, bool zero)
{
	uint8_t *p;

	if (size > SIZE_MAX - VCHIQ_XNU_HDR) {
		return NULL;
	}
	p = zero ? IOMallocZero(size + VCHIQ_XNU_HDR) : IOMalloc(size + VCHIQ_XNU_HDR);
	if (p == NULL) {
		return NULL;
	}
	*(size_t *)(void *)p = size + VCHIQ_XNU_HDR;
	return p + VCHIQ_XNU_HDR;
}

void
vchiq_xnu_free(const void *ptr)
{
	uint8_t *p;

	if (ptr == NULL) {
		return;
	}
	p = (uint8_t *)(uintptr_t)ptr - VCHIQ_XNU_HDR;
	IOFree(p, *(size_t *)(void *)p);
}

volatile int vchiq_xnu_rcu_readers;

/*
 * Waits until no thread is inside rcu_read_lock(). Readers never sleep in
 * there (as on Linux), so this is short; poll rather than build a wakeup.
 */
void
vchiq_xnu_synchronize_rcu(void)
{
	while (__atomic_load_n(&vchiq_xnu_rcu_readers, __ATOMIC_SEQ_CST) != 0) {
		IODelay(10);
	}
}

static void
vchiq_xnu_kthread_main(void *arg, wait_result_t wr)
{
	struct task_struct *t = arg;

	(void)wr;
	/* Before fn runs: vchiq_xnu_is_current() must already see this thread. */
	t->thread = current_thread();
	thread_set_thread_name(current_thread(), t->name);
	t->fn(t->data);
	thread_terminate(current_thread());
}

struct task_struct *
kthread_create(int (*fn)(void *), void *data, const char *name)
{
	struct task_struct *t = kzalloc(sizeof(*t), GFP_KERNEL);

	if (t == NULL) {
		return ERR_PTR(-ENOMEM);
	}
	t->fn = fn;
	t->data = data;
	strlcpy(t->name, name, sizeof(t->name));
	return t;
}

void
wake_up_process(struct task_struct *t)
{
	thread_t th;

	if (t->started) {
		return;
	}
	t->started = true;
	/*
	 * Kernel threads run at BASEPRI_KERNEL, above every user thread; the
	 * core asks Linux for nice -19/-20 (set_user_nice) for the same reason:
	 * a decoder waiting on the VPU's messages must not wait behind user work.
	 */
	if (kernel_thread_start(vchiq_xnu_kthread_main, t, &th) != KERN_SUCCESS) {
		printf("vchiq: could not start thread %s\n", t->name);
		return;
	}
	thread_deallocate(th);  /* the thread keeps its own reference while it runs */
}

bool
vchiq_xnu_is_current(struct task_struct *t)
{
	return t != NULL && !IS_ERR(t) && t->thread == current_thread();
}

/* Linux's current->pid is the thread id, current->tgid the process id. */
int
vchiq_xnu_current_tid(void)
{
	return (int)thread_tid(current_thread());
}

int
vchiq_xnu_current_pid(void)
{
	return proc_selfpid();
}

void
vchiq_xnu_compat_init(void)
{
}
