/*
 * The Linux kernel API that the vendored VCHIQ core (vchiq_core.c,
 * vchiq_arm.c, vchiq_dev.c: Linux drivers/staging/vc04_services, dual
 * GPL-2.0 / BSD-3-Clause, see README) is written against, implemented on
 * XNU. Only what those three files use is here, with Linux's semantics:
 *
 *   - mutex, semaphore, completion, wait queue: sleep with assert_wait()/
 *     thread_block() on the object's address, woken by thread_wakeup().
 *     Every one is usable zero-initialised, because the core declares them
 *     statically (DEFINE_SPINLOCK, DEFINE_SEMAPHORE) and inside a memset()
 *     state. The "interruptible" waits use THREAD_ABORTSAFE (what BSD
 *     msleep(PCATCH) uses), so a signal or a dying process ends them with
 *     -ERESTARTSYS as on Linux.
 *   - spinlock_t / rwlock_t: a real spin with preemption disabled; no
 *     section the core runs under one sleeps (Linux forbids it there too),
 *     and complete()/wake_up_all() only call thread_wakeup(), which is safe
 *     there and at interrupt level (the doorbell handler).
 *   - RCU: the core only uses it to look services up while another thread
 *     may free them (kfree_rcu in service_release()). rcu_read_lock()
 *     counts readers; kfree_rcu() waits for the count to reach zero before
 *     freeing. Linux forbids sleeping inside rcu_read_lock() as well, so a
 *     reader always finishes promptly.
 *   - kthread: kernel_thread_start(), started by wake_up_process() as on
 *     Linux.
 */
#ifndef VCHIQ_XNU_H
#define VCHIQ_XNU_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <libkern/libkern.h>
#include <libkern/copyio.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <kern/debug.h>
#include <IOKit/IOLib.h>

/* XNU's own definitions of these names are not Linux's. */
#undef min
#undef max
#undef kfree
#undef kalloc

__BEGIN_DECLS

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef uint64_t dma_addr_t;
typedef long long loff_t;

/* Kconfig: this is a Raspberry Pi; the character device is BCM2837VCHIQUserClient. */
#define IS_ENABLED(option)              (option)
#define CONFIG_RASPBERRYPI_FIRMWARE     1
#define CONFIG_VCHIQ_CDEV               0

#define __user
#define __iomem
#define __rcu
#define __init
#define __exit
#define __must_check
#define EXPORT_SYMBOL(x)
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)   (sizeof(a) / sizeof((a)[0]))
#endif
#define BIT(n)          (1UL << (n))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define container_of(ptr, type, member) \
	((type *)(void *)((char *)(ptr) - offsetof(type, member)))
#define min_t(t, a, b)  ((t)(a) < (t)(b) ? (t)(a) : (t)(b))
#define max_t(t, a, b)  ((t)(a) > (t)(b) ? (t)(a) : (t)(b))
/* Linux's are type-generic; libkern's min()/max() are u_int functions. */
#define min(a, b)       ({ __typeof__(a) __a = (a); __typeof__(b) __b = (b); __a < __b ? __a : __b; })
#define max(a, b)       ({ __typeof__(a) __a = (a); __typeof__(b) __b = (b); __a > __b ? __a : __b; })
#define upper_32_bits(n) ((u32)((u64)(n) >> 32))
#define lower_32_bits(n) ((u32)(n))
#define fallthrough     __attribute__((fallthrough))

#undef static_assert
#define static_assert(e, ...) _Static_assert(e, #e)
#define BUILD_BUG_ON(e) _Static_assert(!(e), #e)
#define BUILD_BUG_ON_NOT_POWER_OF_2(n) _Static_assert(((n) & ((n) - 1)) == 0, #n)

/* Linux-only errno: an interrupted wait, restarted by the syscall layer. */
#define ERESTARTSYS     EINTR

/* BSD's ioctl encoding (sys/ioccom.h): group in bits 8-15, number in 0-7. */
#define _IOC_NR(cmd)    ((cmd) & 0xff)
#define _IOC_TYPE(cmd)  (((cmd) >> 8) & 0xff)

/* ------------------------------------------------------------- logging */
#define KERN_INFO       ""
#define printk(...)     printf(__VA_ARGS__)
#define pr_err(...)     printf("vchiq: " __VA_ARGS__)
#define pr_warn(...)    printf("vchiq: " __VA_ARGS__)
#define pr_notice(...)  printf("vchiq: " __VA_ARGS__)
#define pr_info(...)    printf("vchiq: " __VA_ARGS__)

#define WARN(cond, ...) ({ \
	int __w = !!(cond); \
	if (__w) printf("vchiq: WARNING %s:%d: ", __func__, __LINE__), printf(__VA_ARGS__); \
	__w; })
#define WARN_ON(cond) ({ \
	int __w = !!(cond); \
	if (__w) printf("vchiq: WARN_ON(%s) %s:%d\n", #cond, __func__, __LINE__); \
	__w; })
#define WARN_ON_ONCE(cond) WARN_ON(cond)
#define BUG()           panic("vchiq: BUG at %s:%d", __func__, __LINE__)
#define BUG_ON(cond)    do { if (cond) BUG(); } while (0)

/* ------------------------------------------------------------- barriers */
/*
 * The shared slots are mapped Normal non-cacheable; the VPU reads them
 * through the 0xC0000000 (L2-uncached) bus alias. Linux arm64's mb/rmb/wmb
 * are dsb sy/ld/st (arch/arm64/include/asm/barrier.h), and the core's
 * explicit dsb(sy) is the same instruction.
 */
#define dsb(opt)        __asm__ volatile ("dsb " #opt ::: "memory")
#define mb()            __asm__ volatile ("dsb sy" ::: "memory")
#define rmb()           __asm__ volatile ("dsb ld" ::: "memory")
#define wmb()           __asm__ volatile ("dsb st" ::: "memory")

/* ------------------------------------------------------------- atomics */
typedef struct {
	volatile int counter;
} atomic_t;

#define atomic_read(v)          __atomic_load_n(&(v)->counter, __ATOMIC_RELAXED)
#define atomic_set(v, i)        __atomic_store_n(&(v)->counter, (i), __ATOMIC_RELAXED)
#define atomic_inc(v)           ((void)__atomic_add_fetch(&(v)->counter, 1, __ATOMIC_SEQ_CST))
#define atomic_add(i, v)        ((void)__atomic_add_fetch(&(v)->counter, (i), __ATOMIC_SEQ_CST))
#define atomic_xchg(v, i)       __atomic_exchange_n(&(v)->counter, (i), __ATOMIC_SEQ_CST)
static inline int
atomic_cmpxchg(atomic_t *v, int old, int new_)
{
	__atomic_compare_exchange_n(&v->counter, &old, new_, false,
	    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return old;
}

struct kref {
	atomic_t refcount;
};
#define kref_init(k)    atomic_set(&(k)->refcount, 1)
#define kref_read(k)    atomic_read(&(k)->refcount)
#define kref_get(k)     atomic_inc(&(k)->refcount)
static inline int
kref_get_unless_zero(struct kref *k)
{
	int c = atomic_read(&k->refcount);
	while (c != 0) {
		if (__atomic_compare_exchange_n(&k->refcount.counter, &c, c + 1, false,
		    __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)) {
			return 1;
		}
	}
	return 0;
}
static inline int
kref_put(struct kref *k, void (*release)(struct kref *))
{
	if (__atomic_sub_fetch(&k->refcount.counter, 1, __ATOMIC_SEQ_CST) == 0) {
		release(k);
		return 1;
	}
	return 0;
}

/* ------------------------------------------------------------- memory */
#define GFP_KERNEL      0
void *vchiq_xnu_alloc(size_t size, bool zero);
void vchiq_xnu_free(const void *p);
#define kmalloc(size, gfp)              vchiq_xnu_alloc((size), false)
#define kzalloc(size, gfp)              vchiq_xnu_alloc((size), true)
#define kcalloc(n, size, gfp)           vchiq_xnu_alloc((size_t)(n) * (size_t)(size), true)
#define kmalloc_array(n, size, gfp)     vchiq_xnu_alloc((size_t)(n) * (size_t)(size), false)
#define devm_kzalloc(dev, size, gfp)    vchiq_xnu_alloc((size), true)
#define kfree(p)                        vchiq_xnu_free(p)

#define IS_ERR(p)       ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p)      ((long)(intptr_t)(p))
#define ERR_PTR(e)      ((void *)(intptr_t)(e))

/* scnprintf() is XNU's own (osfmk/kern/misc_protos.h), same contract. */
extern int scnprintf(char *, size_t, const char *, ...) __printflike(3, 4);

#define strscpy(d, s, n) strlcpy((d), (s), (n))
/* XNU has no sprintf(); the core only sprintf()s into char arrays. */
#define sprintf(buf, ...) snprintf((buf), sizeof(buf), __VA_ARGS__)

/* ------------------------------------------------------------- user copy */
#define copy_from_user(dst, src, n)     copyin((user_addr_t)(uintptr_t)(src), (dst), (n))
#define copy_to_user(dst, src, n)       copyout((src), (user_addr_t)(uintptr_t)(dst), (n))
#define get_user(x, ptr) ({ \
	__typeof__(*(ptr)) __gu; \
	int __gr = copyin((user_addr_t)(uintptr_t)(ptr), &__gu, sizeof(__gu)); \
	if (__gr == 0) (x) = __gu; \
	__gr ? -EFAULT : 0; })
#define put_user(x, ptr) ({ \
	__typeof__(*(ptr)) __pu = (x); \
	copyout(&__pu, (user_addr_t)(uintptr_t)(ptr), sizeof(__pu)) ? -EFAULT : 0; })

/* ------------------------------------------------------------- sleeping */
/*
 * Waits on `ev` until `cond` holds. THREAD_ABORTSAFE: returns -ERESTARTSYS
 * when interrupted. The condition is re-checked after assert_wait(), so a
 * wakeup between the first check and thread_block() is never lost.
 */
#define vchiq_xnu_wait(ev, cond, interruptible) ({ \
	int __r = 0; \
	for (;;) { \
		if (cond) \
			break; \
		assert_wait((event_t)(ev), (interruptible) ? THREAD_ABORTSAFE : THREAD_UNINT); \
		if (cond) { \
			clear_wait(current_thread(), THREAD_AWAKENED); \
			break; \
		} \
		if (thread_block(THREAD_CONTINUE_NULL) == THREAD_INTERRUPTED) { \
			__r = -ERESTARTSYS; \
			break; \
		} \
	} \
	__r; })

/* A counting semaphore; the base of struct completion and struct semaphore. */
struct vchiq_xnu_sema {
	volatile int count;
};

static inline int
vchiq_xnu_sema_trydown(struct vchiq_xnu_sema *s)
{
	int c = __atomic_load_n(&s->count, __ATOMIC_ACQUIRE);
	while (c > 0) {
		if (__atomic_compare_exchange_n(&s->count, &c, c - 1, false,
		    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
			return 1;
		}
	}
	return 0;
}

static inline int
vchiq_xnu_sema_down(struct vchiq_xnu_sema *s, bool interruptible)
{
	return vchiq_xnu_wait(s, vchiq_xnu_sema_trydown(s), interruptible);
}

static inline void
vchiq_xnu_sema_up(struct vchiq_xnu_sema *s, int n)
{
	__atomic_add_fetch(&s->count, n, __ATOMIC_ACQ_REL);
	thread_wakeup((event_t)s);
}

struct completion {
	struct vchiq_xnu_sema s;
};
#define init_completion(c)      __atomic_store_n(&(c)->s.count, 0, __ATOMIC_RELEASE)
#define reinit_completion(c)    __atomic_store_n(&(c)->s.count, 0, __ATOMIC_RELEASE)
#define complete(c)             vchiq_xnu_sema_up(&(c)->s, 1)
#define complete_all(c)         vchiq_xnu_sema_up(&(c)->s, 0x3fffffff)
#define try_wait_for_completion(c)              vchiq_xnu_sema_trydown(&(c)->s)
#define wait_for_completion(c)                  ((void)vchiq_xnu_sema_down(&(c)->s, false))
#define wait_for_completion_interruptible(c)    vchiq_xnu_sema_down(&(c)->s, true)
#define wait_for_completion_killable(c)         vchiq_xnu_sema_down(&(c)->s, true)

struct semaphore {
	struct vchiq_xnu_sema s;
};
#define DEFINE_SEMAPHORE(name, n)       struct semaphore name = { { (n) } }
#define sema_init(sem, n)               __atomic_store_n(&(sem)->s.count, (n), __ATOMIC_RELEASE)
#define down(sem)                       ((void)vchiq_xnu_sema_down(&(sem)->s, false))
#define down_interruptible(sem)         vchiq_xnu_sema_down(&(sem)->s, true)
#define up(sem)                         vchiq_xnu_sema_up(&(sem)->s, 1)

/* A sleeping mutex: a binary semaphore that starts available. */
struct mutex {
	volatile int locked;
};
#define DEFINE_MUTEX(name)      struct mutex name = { 0 }
#define mutex_init(m)           __atomic_store_n(&(m)->locked, 0, __ATOMIC_RELEASE)
static inline int
vchiq_xnu_mutex_trylock(struct mutex *m)
{
	return __atomic_exchange_n(&m->locked, 1, __ATOMIC_ACQUIRE) == 0;
}
#define mutex_lock(m)           ((void)vchiq_xnu_wait((m), vchiq_xnu_mutex_trylock(m), false))
/* "killable" on Linux only ends the wait for SIGKILL; the core treats a
 * failure as -EAGAIN. Uninterruptible here: the holders never block long. */
#define mutex_lock_killable(m)  (mutex_lock(m), 0)
static inline void
mutex_unlock(struct mutex *m)
{
	__atomic_store_n(&m->locked, 0, __ATOMIC_RELEASE);
	thread_wakeup_one((event_t)m);
}

/* Spin locks: disable preemption, spin. */
typedef struct {
	volatile int locked;
} spinlock_t;
#define DEFINE_SPINLOCK(name)   spinlock_t name = { 0 }
#define spin_lock_init(l)       ((l)->locked = 0)
extern void _disable_preemption(void);
extern void _enable_preemption(void);
static inline void
spin_lock(spinlock_t *l)
{
	_disable_preemption();
	while (__atomic_exchange_n(&l->locked, 1, __ATOMIC_ACQUIRE) != 0) {
		while (__atomic_load_n(&l->locked, __ATOMIC_RELAXED) != 0) {
			__asm__ volatile ("yield");
		}
	}
}
static inline void
spin_unlock(spinlock_t *l)
{
	__atomic_store_n(&l->locked, 0, __ATOMIC_RELEASE);
	_enable_preemption();
}

typedef spinlock_t rwlock_t;
#define rwlock_init(l)          spin_lock_init(l)
#define read_lock_bh(l)         spin_lock(l)
#define read_unlock_bh(l)       spin_unlock(l)
#define write_lock_bh(l)        spin_lock(l)
#define write_unlock_bh(l)      spin_unlock(l)

/* Wait queues: the queue is just an address to wait on. */
typedef struct {
	int unused;
} wait_queue_head_t;
#define init_waitqueue_head(wq)                 ((void)(wq))
#define wait_event_interruptible(wq, cond)      vchiq_xnu_wait(&(wq), (cond), true)
#define wake_up_all(wq)                         thread_wakeup((event_t)(wq))

/* RCU: reader counting, see the header comment. */
extern volatile int vchiq_xnu_rcu_readers;
#define rcu_read_lock()         ((void)__atomic_add_fetch(&vchiq_xnu_rcu_readers, 1, __ATOMIC_SEQ_CST))
#define rcu_read_unlock()       ((void)__atomic_sub_fetch(&vchiq_xnu_rcu_readers, 1, __ATOMIC_SEQ_CST))
#define rcu_dereference(p)      __atomic_load_n(&(p), __ATOMIC_ACQUIRE)
#define rcu_access_pointer(p)   __atomic_load_n(&(p), __ATOMIC_RELAXED)
#define rcu_assign_pointer(p, v) __atomic_store_n(&(p), (v), __ATOMIC_RELEASE)
#define rcu_pointer_handoff(p)  (p)
struct rcu_head {
	void *unused;
};
void vchiq_xnu_synchronize_rcu(void);
#define kfree_rcu(p, field)     do { vchiq_xnu_synchronize_rcu(); kfree(p); } while (0)

/* ------------------------------------------------------------- threads */
struct task_struct {
	thread_t thread;
	bool started;
	int (*fn)(void *);
	void *data;
	char name[32];
	int pid;
};
struct task_struct *kthread_create(int (*fn)(void *), void *data, const char *name);
void wake_up_process(struct task_struct *t);
#define kthread_stop(t)         ((void)(t))     /* only on a failed init_state; the threads are never started then */
#define set_user_nice(t, n)     ((void)(t))
#define flush_signals(t)        ((void)0)
#define fatal_signal_pending(t) 0
bool vchiq_xnu_is_current(struct task_struct *t);
int vchiq_xnu_current_tid(void);
int vchiq_xnu_current_pid(void);

#define msleep(ms)              IOSleep(ms)
#define usleep_range(lo, hi)    IOSleep(((lo) + 999) / 1000)

/* ------------------------------------------------------------- lists */
struct list_head {
	struct list_head *next, *prev;
};
#define INIT_LIST_HEAD(l)       do { (l)->next = (l); (l)->prev = (l); } while (0)
static inline void
list_add(struct list_head *n, struct list_head *head)
{
	n->next = head->next;
	n->prev = head;
	head->next->prev = n;
	head->next = n;
}
static inline void
list_del(struct list_head *e)
{
	e->prev->next = e->next;
	e->next->prev = e->prev;
	e->next = e->prev = NULL;
}
#define list_entry(ptr, type, member)   container_of(ptr, type, member)
#define list_for_each_entry(pos, head, member) \
	for (pos = list_entry((head)->next, __typeof__(*pos), member); \
	    &pos->member != (head); \
	    pos = list_entry(pos->member.next, __typeof__(*pos), member))
#define list_for_each_entry_safe(pos, n, head, member) \
	for (pos = list_entry((head)->next, __typeof__(*pos), member), \
	    n = list_entry(pos->member.next, __typeof__(*pos), member); \
	    &pos->member != (head); \
	    pos = n, n = list_entry(n->member.next, __typeof__(*n), member))

/* The core keeps a struct device pointer it never dereferences. */
struct device;

__END_DECLS

#endif /* VCHIQ_XNU_H */
