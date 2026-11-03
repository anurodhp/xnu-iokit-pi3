/*
 * netbt_compat.h -- NetBSD kernel API on xnu, for NetBSD's Bluetooth stack
 * (sys/netbt, vendored into bsd/netbt by tools/vendor_netbt_into_xnu.sh;
 * docs/bluetooth-stack-plan.md S2). Force-included (-include) into every
 * netbt object, before netbt's own includes.
 *
 * Each mapping names the NetBSD semantics it reproduces and the xnu source
 * it rests on (DAR-313). NetBSD references are to our pin
 * (anurodhp/netbsd-src iokit-port, netbsd-11 @ 4198f5370c18), sys/ unless
 * noted; xnu references are to third_party/xnu-7195.
 */
#ifndef _NETBT_COMPAT_H_
#define _NETBT_COMPAT_H_

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <sys/domain.h>
#include <sys/kauth.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/errno.h>
#include <kern/locks.h>
#include <kern/thread_call.h>
#include <kern/kalloc.h>

/* ------------------------------------------------------------ cdefs */

/* NetBSD kernel code is under _KERNEL; xnu defines KERNEL. */
#ifndef _KERNEL
#define _KERNEL 1
#endif
#define __packed                __attribute__((__packed__))
#ifndef __aligned
#define __aligned(x)            __attribute__((__aligned__(x)))
#endif
/* NetBSD SIMPLEQ is the BSD STAILQ (xnu bsd/sys/queue.h). */
#define SIMPLEQ_HEAD            STAILQ_HEAD
#define SIMPLEQ_HEAD_INITIALIZER STAILQ_HEAD_INITIALIZER
#define SIMPLEQ_ENTRY           STAILQ_ENTRY
#define SIMPLEQ_INIT            STAILQ_INIT
#define SIMPLEQ_FIRST           STAILQ_FIRST
#define SIMPLEQ_NEXT            STAILQ_NEXT
#define SIMPLEQ_EMPTY           STAILQ_EMPTY
#define SIMPLEQ_FOREACH         STAILQ_FOREACH
#define SIMPLEQ_FOREACH_SAFE    STAILQ_FOREACH_SAFE
#define SIMPLEQ_INSERT_HEAD     STAILQ_INSERT_HEAD
#define SIMPLEQ_INSERT_TAIL     STAILQ_INSERT_TAIL
#define SIMPLEQ_INSERT_AFTER    STAILQ_INSERT_AFTER
#define SIMPLEQ_REMOVE_HEAD     STAILQ_REMOVE_HEAD
#define SIMPLEQ_REMOVE          STAILQ_REMOVE

#define __KERNEL_RCSID(n, s)
#define __arraycount(a)         (sizeof(a) / sizeof((a)[0]))
#define __diagused              __unused
#define __predict_false(x)      __builtin_expect(!!(x), 0)
#define __predict_true(x)       __builtin_expect(!!(x), 1)
#ifndef __UNCONST
#define __UNCONST(a)            ((void *)(uintptr_t)(const void *)(a))
#endif
#ifndef UNCONST
#define UNCONST(a)              __UNCONST(a)
#endif
#ifndef SET
#define SET(t, f)               ((t) |= (f))
#endif
#ifndef CLR
#define CLR(t, f)               ((t) &= ~(f))
#endif
#ifndef ISSET
#define ISSET(t, f)             ((t) & (f))
#endif

/*
 * KASSERT: NetBSD compiles these out without DIAGNOSTIC (sys/lib/libkern/
 * libkern.h). Most of netbt's are KASSERT(solocked(so)), and xnu has no
 * boolean "is this mutex mine" query (only the panicking lck_mtx_assert,
 * osfmk/kern/locks.h:881), so they go the same way here.
 */
#undef KASSERT
#define KASSERT(e)              do { } while (0)
#define KASSERTMSG(e, ...)      do { } while (0)

/* ------------------------------------------------------------ printing */

#define aprint_error_dev(dev, ...)      printf(__VA_ARGS__)
#define aprint_normal_dev(dev, ...)     printf(__VA_ARGS__)
#define aprint_verbose_dev(dev, ...)    printf(__VA_ARGS__)
#define aprint_debug_dev(dev, ...)      do { } while (0)
/* netbt's log(LOG_..., ...) calls: xnu's log() exists (bsd/kern/subr_log.c). */

/* ------------------------------------------------------------ threads, creds */

/* NetBSD's struct lwp stands where xnu passes struct proc. */
struct lwp;
#define lwp                     proc
#define curlwp                  current_proc()
#define l_cred                  p_ucred_unused_netbt   /* never dereferenced: see kauth_cred_dup */

/* ------------------------------------------------------------ mutexes */

/*
 * kmutex_t is an xnu lck_mtx_t. NetBSD's MUTEX_DRIVER at an interrupt IPL
 * would be a spin mutex; nothing here takes one at interrupt level (the
 * driver feeds netbt from its worker thread), so a sleep mutex is right.
 */
typedef lck_mtx_t kmutex_t;
extern lck_grp_t *netbt_lck_grp;
#define MUTEX_DEFAULT           0
#define MUTEX_DRIVER            0
#define IPL_NONE                0
#define IPL_SOFTNET             0
#define IPL_TTY                 0
#define IPL_BIO                 0
#define IPL_VM                  0
#define IPL_USB                 0
#define mutex_init(m, t, ipl)   lck_mtx_init((m), netbt_lck_grp, LCK_ATTR_NULL)
#define mutex_destroy(m)        lck_mtx_destroy((m), netbt_lck_grp)
#define mutex_enter(m)          lck_mtx_lock(m)
#define mutex_exit(m)           lck_mtx_unlock(m)
#define mutex_tryenter(m)       lck_mtx_try_lock(m)
/*
 * bt_lock is the Bluetooth domain's own mutex (struct domain dom_mtx), which
 * xnu's socket_lock() takes for every socket of a protocol without pr_lock
 * (bsd/kern/uipc_socket.c socket_lock). That is NetBSD's arrangement too:
 * every netbt socket's so_lock IS bt_lock (l2cap_socket.c:92-95). So the
 * mutex_obj_* reference counting on it is a no-op here.
 */
#define mutex_obj_hold(m)       do { } while (0)
#define mutex_obj_free(m)       do { } while (0)

/* condvars over msleep/wakeup (bsd/kern/kern_synch.c msleep). */
typedef struct { int cv_dummy; } kcondvar_t;
#define cv_init(cv, name)       do { (void)(cv); } while (0)
#define cv_destroy(cv)          do { (void)(cv); } while (0)
#define cv_broadcast(cv)        wakeup((caddr_t)(cv))
#define cv_signal(cv)           wakeup_one((caddr_t)(cv))
int netbt_cv_timedwait_sig(kcondvar_t *, kmutex_t *, int ticks);
#define cv_timedwait_sig(cv, m, t)      netbt_cv_timedwait_sig((cv), (m), (t))

/* ------------------------------------------------------------ memory */

#define M_BLUETOOTH             M_TEMP
#define MALLOC_DECLARE(t)
#undef MALLOC_DEFINE
#define MALLOC_DEFINE(t, s, l)
#define M_NOWAIT_NETBT          M_NOWAIT
/* NetBSD malloc(9)/free(9) with a type (netbt: M_BLUETOOTH). */
#define malloc(size, type, flags)       _MALLOC((size), M_TEMP, (flags))
#define free(addr, type)                _FREE((addr), M_TEMP)
/* kmem(9): sized frees, like kheap_free (osfmk/kern/kalloc.h). */
#define KM_SLEEP                Z_WAITOK
#define KM_NOSLEEP              Z_NOWAIT
#define kmem_alloc(sz, f)       kheap_alloc(KHEAP_DEFAULT, (sz), (f))
#define kmem_zalloc(sz, f)      kheap_alloc(KHEAP_DEFAULT, (sz), (f) | Z_ZERO)
#define kmem_intr_zalloc(sz, f) kheap_alloc(KHEAP_DEFAULT, (sz), (f) | Z_ZERO)
#define kmem_free(p, sz)        kheap_free(KHEAP_DEFAULT, (p), (sz))
#define kmem_intr_free(p, sz)   kheap_free(KHEAP_DEFAULT, (p), (sz))

/* pool(9): fixed-size items; NetBSD pool_get does not zero, this does. */
struct pool {
	size_t pr_size;
	const char *pr_wchan;
};
#define PR_NOWAIT               Z_NOWAIT
#define PR_WAITOK               Z_WAITOK
#define pool_init(pp, sz, al, io, fl, wc, pa, ipl) \
	do { (pp)->pr_size = (sz); (pp)->pr_wchan = (wc); } while (0)
#define pool_get(pp, f)         kheap_alloc(KHEAP_DEFAULT, (pp)->pr_size, (f) | Z_ZERO)
#define pool_put(pp, v)         kheap_free(KHEAP_DEFAULT, (v), (pp)->pr_size)

/* ------------------------------------------------------------ mbufs */

/*
 * M_SETCTX/M_GETCTX (NetBSD sys/mbuf.h: a pointer in the packet header,
 * m_pkthdr._rcvif.ctx). xnu's socket layer never reads m_pkthdr.rcvif
 * (no reference in bsd/kern/uipc_socket*.c), so the pointer lives there.
 */
#define M_SETCTX(m, c)          ((m)->m_pkthdr.rcvif = (struct ifnet *)(void *)(c))
#define M_GETCTX(m, t)          ((t)(void *)(m)->m_pkthdr.rcvif)
/* NetBSD M_PREPEND has no alignment argument (xnu sys/mbuf.h:885 does). */
#undef M_PREPEND
#define M_PREPEND(m, plen, how) do {                                    \
	(m) = m_prepend_2((m), (plen), (how), 0);                       \
} while (0)
/* m_copypacket: a full copy with the header (NetBSD uipc_mbuf.c). */
#define m_copypacket(m, how)    m_copym((m), 0, M_COPYALL, (how))

/* MBUFQ: xnu's sys/mbuf.h:971-1030 has the same queue (shared BSD lineage). */
#ifndef MBUFQ_DRAIN
#define MBUFQ_DRAIN(q)          do {                                    \
	struct mbuf *__m0;                                              \
	while ((__m0 = MBUFQ_FIRST(q)) != NULL) {                       \
	        MBUFQ_FIRST(q) = MBUFQ_NEXT(__m0);                      \
	        m_freem(__m0);                                          \
	}                                                               \
	(q)->mq_last = &MBUFQ_FIRST(q);                                 \
} while (0)
#endif

/* ------------------------------------------------------------ sockets */

/* netbt's PR_LISTEN flag (NetBSD sys/protosw.h); xnu has none, it is advisory. */
#ifndef PR_LISTEN
#define PR_LISTEN               0
#endif

#define solocked(so)            1
#define solock(so)              socket_lock((so), 0)
#define sounlock(so)            socket_unlock((so), 0)

/*
 * sbappendaddr: NetBSD returns 0 and leaves m and control to the caller
 * when there is no room (uipc_socket2.c sbappendaddr); xnu frees them on
 * any failure (bsd/kern/uipc_socket2.c sbappendaddr). netbt callers free on
 * 0, so the wrapper only calls xnu's when the record fits, and reports
 * success if xnu then drops it anyway (it has been freed).
 */
int netbt_sbappendaddr(struct sockbuf *, const struct sockaddr *, struct mbuf *, struct mbuf *);
#define sbappendaddr(sb, sa, m, c)      netbt_sbappendaddr((sb), (sa), (m), (c))
/* NetBSD void; xnu returns int and frees on failure (same ownership). */
#define sbappendrecord(sb, m)           ((void)sbappendrecord((sb), (m)))
#define sbappendstream(sb, m)           ((void)sbappendstream((sb), (m)))
/* NetBSD sonewconn(head, soready); xnu sonewconn(head, connstatus, from). */
#define sonewconn(head, ready)          sonewconn((head), (ready) ? SS_ISCONNECTED : 0, NULL)
#define soroverflow(so)                 do { } while (0)
/* NetBSD sbcreatecontrol takes void *; xnu's takes caddr_t. */
#define sbcreatecontrol(p, sz, t, l)    sbcreatecontrol((caddr_t)(void *)(p), (sz), (t), (l))

/*
 * struct sockopt: NetBSD's is a kernel buffer (sopt_data/sopt_size,
 * sys/socketvar.h) filled by sockopt_get/set; xnu's points at user or
 * kernel memory (sopt_val, sopt_p) copied by sooptcopyin/out
 * (bsd/kern/uipc_socket.c). netbt only uses sopt_level/sopt_name and the
 * accessors below, so the accessors bridge the two.
 */
int netbt_sockopt_get(const struct sockopt *, void *, size_t);
int netbt_sockopt_set(struct sockopt *, const void *, size_t);
int netbt_sockopt_getint(const struct sockopt *, int *);
int netbt_sockopt_setint(struct sockopt *, int);
void netbt_sockopt_init(struct sockopt *, int level, int name, size_t size);
void netbt_sockopt_destroy(struct sockopt *);
#define sockopt_get(s, b, l)            netbt_sockopt_get((s), (b), (l))
#define sockopt_set(s, b, l)            netbt_sockopt_set((s), (b), (l))
#define sockopt_getint(s, v)            netbt_sockopt_getint((s), (v))
#define sockopt_setint(s, v)            netbt_sockopt_setint((s), (v))
#define sockopt_init(s, lv, n, sz)      netbt_sockopt_init((s), (lv), (n), (sz))
#define sockopt_destroy(s)              netbt_sockopt_destroy(s)
#define PRCO_GETOPT             0
#define PRCO_SETOPT             1
#ifdef BLUETOOTH_DEBUG
/* NetBSD sys/protosw.h names, for netbt's DPRINTFs. */
static const char *const prcorequests[] __attribute__((unused)) = { "GETOPT", "SETOPT" };
#endif

/* ------------------------------------------------------------ callout(9) */

/*
 * NetBSD callout semantics over xnu thread_call (osfmk/kern/thread_call.c):
 *   callout_schedule  arm (or re-arm) after N ticks; sets PENDING
 *   callout_stop      disarm; a callout already past its pending stage
 *                     stays INVOKING until its handler calls callout_ack
 *   callout_invoking  fired and the handler has not acked yet (it is
 *                     running or waiting for bt_lock)
 *   callout_destroy   only after !invoking; waits out a handler that was
 *                     dequeued but saw itself stopped
 * See netbt_xnu.c for the state machine.
 */
/* xnu has its own struct callout (bsd/net/pktsched/pktsched.h:86). */
#define callout                 netbt_callout
struct callout {
	thread_call_t c_call;
	void (*c_func)(void *);
	void *c_arg;
	volatile uint32_t c_flags;
	volatile thread_t c_thread;     /* the thread running c_func, if any */
};
typedef struct callout callout_t;
#define CALLOUT_MPSAFE          0
void netbt_callout_init(struct callout *, unsigned int);
void netbt_callout_setfunc(struct callout *, void (*)(void *), void *);
void netbt_callout_schedule(struct callout *, int ticks);
bool netbt_callout_stop(struct callout *);
bool netbt_callout_pending(struct callout *);
bool netbt_callout_invoking(struct callout *);
void netbt_callout_ack(struct callout *);
void netbt_callout_destroy(struct callout *);
#define callout_init(c, f)              netbt_callout_init((c), (f))
#define callout_setfunc(c, fn, a)       netbt_callout_setfunc((c), (fn), (a))
#define callout_schedule(c, t)          netbt_callout_schedule((c), (t))
#define callout_reset(c, t, fn, a)      do { netbt_callout_setfunc((c), (fn), (a)); \
	                                     netbt_callout_schedule((c), (t)); } while (0)
#define callout_stop(c)                 netbt_callout_stop(c)
#define callout_pending(c)              netbt_callout_pending(c)
#define callout_invoking(c)             netbt_callout_invoking(c)
#define callout_ack(c)                  netbt_callout_ack(c)
#define callout_destroy(c)              netbt_callout_destroy(c)

/* ------------------------------------------------------------ softint(9) */

#define SOFTINT_NET             0
#define SOFTINT_SERIAL          0
#define SOFTINT_MPSAFE          0
void *netbt_softint_establish(unsigned int, void (*)(void *), void *);
void netbt_softint_schedule(void *);
void netbt_softint_disestablish(void *);
#define softint_establish(f, fn, a)     netbt_softint_establish((f), (fn), (a))
#define softint_schedule(c)             netbt_softint_schedule(c)
#define softint_disestablish(c)         netbt_softint_disestablish(c)

/* ------------------------------------------------------------ devices */

/*
 * device_t: the controller driver's handle. BCM43430Bluetooth allocates one
 * (struct netbt_device) and passes it to hci_attach_pcb.
 */
struct netbt_device {
	char dv_xname[16];
	void *dv_softc;
};
typedef struct netbt_device *device_t;
#define device_xname(d)                 ((d)->dv_xname)
#define device_private(d)               ((d)->dv_softc)
/* The btbus/bthub autoconf children (bthidev, btsco) are not built here. */
#define config_found(...)               NULL
#define config_detach(d, f)             0
#define DETACH_FORCE                    0

/* ------------------------------------------------------------ kauth */

/*
 * NetBSD's device scope: hci_socket.c authorizes HCI traffic through a
 * KAUTH_SCOPE_DEVICE listener (hci_device_cb) that allows the unprivileged
 * subset and defers the rest to the secmodel, which grants root
 * (secmodel_suser). Here: root (kauth_cred_issuser,
 * bsd/kern/kern_authorization.c), else the listener's answer.
 */
#define KAUTH_SCOPE_DEVICE              "netbt.device"
#define KAUTH_DEVICE_BLUETOOTH_SETPRIV  1
#define KAUTH_DEVICE_BLUETOOTH_SEND     2
#define KAUTH_DEVICE_BLUETOOTH_RECV     3
#define KAUTH_DEVICE_BLUETOOTH_BCSP     4
#define KAUTH_DEVICE_BLUETOOTH_BTUART   5
/* KAUTH_RESULT_* and kauth_listener_t: xnu bsd/sys/kauth.h:470-486. */
#define KAUTH_ARG(a)                    ((void *)(uintptr_t)(a))
/*
 * The listener hci_socket.c registers (hci_device_cb: which HCI commands
 * an unprivileged socket may send and which events it may see) is kept and
 * consulted by netbt_kauth_authorize_device for non-root callers, as
 * NetBSD's secmodel allows root and defers others to the listeners.
 */
typedef int (*netbt_kauth_cb_t)(kauth_cred_t, kauth_action_t, void *,
    void *, void *, void *, void *);
kauth_listener_t netbt_kauth_listen(netbt_kauth_cb_t);
#define kauth_listen_scope(sc, cb, cookie)      netbt_kauth_listen(cb)
#define kauth_unlisten_scope(l)                 do { } while (0)
int netbt_kauth_authorize_device(kauth_cred_t, int, void *, void *, void *, void *);
#define kauth_authorize_device(c, a, a1, a2, a3, a4) \
	netbt_kauth_authorize_device((c), (a), (a1), (a2), (a3), (a4))
/* The pcb keeps a reference to its creator's credential. */
#define kauth_cred_dup(c)               netbt_kauth_cred_dup()
#define kauth_cred_free(c)              netbt_kauth_cred_free(c)
kauth_cred_t netbt_kauth_cred_dup(void);
void netbt_kauth_cred_free(kauth_cred_t);

/* ------------------------------------------------------------ sysctl */

/*
 * NetBSD's dynamic sysctl tree (sysctl_createv) is not built: netbt's
 * net.bluetooth.* tunables keep their compiled defaults.
 */
struct sysctllog;
#define SYSCTL_SETUP(name, desc)        static void __attribute__((unused)) name(struct sysctllog **clog)
#define sysctl_createv(...)             0
#define CTLFLAG_PERMANENT 0
#define CTLFLAG_READWRITE 0
#define CTLFLAG_READONLY 0
#define CTLTYPE_NODE 0
#define CTLTYPE_INT 0
#define CTLTYPE_STRUCT 0
#define CTL_NET 0
#define CTL_CREATE 0
#define CTL_EOL 0
#define SYSCTL_DESCR(s)                 NULL

/* ------------------------------------------------------------ time */

/* NetBSD getmicrotime(9); xnu has microtime and microuptime (bsd/kern/kern_time.c). */
#ifndef getmicrotime
#define getmicrotime(tv)                microtime(tv)
#endif

/* ------------------------------------------------------------ byte order, misc */

#include <libkern/OSByteOrder.h>
#define htole16(x)              OSSwapHostToLittleInt16(x)
#define htole32(x)              OSSwapHostToLittleInt32(x)
#define htole64(x)              OSSwapHostToLittleInt64(x)
#define le16toh(x)              OSSwapLittleToHostInt16(x)
#define le32toh(x)              OSSwapLittleToHostInt32(x)
#define le64toh(x)              OSSwapLittleToHostInt64(x)
#define htobe16(x)              OSSwapHostToBigInt16(x)
#define be16toh(x)              OSSwapBigToHostInt16(x)
static inline void le16enc(void *p, uint16_t v) { uint8_t *b = p; b[0] = v; b[1] = v >> 8; }
static inline void le32enc(void *p, uint32_t v) { uint8_t *b = p; b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24; }
static inline void le64enc(void *p, uint64_t v) { le32enc(p, (uint32_t)v); le32enc((uint8_t *)p + 4, (uint32_t)(v >> 32)); }
static inline uint16_t le16dec(const void *p) { const uint8_t *b = p; return (uint16_t)(b[0] | b[1] << 8); }
static inline uint32_t le32dec(const void *p) { const uint8_t *b = p; return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }
static inline u_int uimin(u_int a, u_int b) { return a < b ? a : b; }
static inline u_int uimax(u_int a, u_int b) { return a > b ? a : b; }
/* NetBSD's "pass to the next ioctl handler" (sys/errno.h); turned into
 * EOPNOTSUPP at the xnu boundary (NETBT_XNU_USRREQS). */
#define EPASSTHROUGH            (-4)
/*
 * M_LINK0: hci_unit.c marks received packets with it for hci_mtap's
 * direction flag (hci_socket.c:877). xnu's M_PROTO1 (sys/mbuf.h) is the
 * protocol-private flag; nothing in the socket layer reads it.
 */
#define M_LINK0                 M_PROTO1

/* ------------------------------------------------------------ pr_usrreqs */

/*
 * NETBT_XNU_USRREQS(p) stands in for each *_socket.c's NetBSD pr_usrreqs
 * table (guarded #ifndef _NETBT_XNU in our fork) and defines `p_usrreqs`
 * as an xnu struct pr_usrreqs (bsd/sys/protosw.h) whose entries call the
 * file's static NetBSD handlers:
 *   - attach: xnu's socreate() calls pru_attach unlocked with a proc,
 *     sonewconn() with the listening socket (same domain mutex) locked
 *     and a NULL proc (bsd/kern/uipc_socket.c:781, uipc_socket2.c:425);
 *     NetBSD's handlers run with bt_lock held, so it is taken only in the
 *     first case.
 *   - accept/peeraddr/sockaddr: NetBSD fills a caller's buffer, xnu wants
 *     an allocated sockaddr (dup_sockaddr, bsd/kern/uipc_socket2.c).
 *   - detach: NetBSD void; xnu frees the socket itself after pru_detach
 *     once so_pcb is NULL (soclose_locked/sofreelastref), which the
 *     NetBSD *_detach_pcb does.
 *   - send: xnu passes PRUS_OOB in flags; NetBSD has a separate sendoob.
 *   - ioctl: xnu's soioctl holds the socket lock -- bt_lock -- around
 *     pru_control (bsd/kern/sys_socket.c soioctl), but NetBSD calls pr_ioctl
 *     unlocked and hci_ioctl takes bt_lock itself (hci_socket.c hci_ioctl),
 *     which deadlocked `btconfig` on QEMU. The wrapper drops the lock for
 *     the call (soioctl's socket_lock(so, 1) keeps the socket referenced).
 *     EPASSTHROUGH (not ours) becomes EOPNOTSUPP.
 * Everything else (sosend, soreceive, sopoll, sense) is xnu's default,
 * filled in by net_add_proto's pru_sanitize (bsd/kern/uipc_socket2.c).
 */
#define NETBT_XNU_USRREQS(p)                                                    \
static int p##_xnu_attach(struct socket *so, int proto, struct proc *pr)       \
{                                                                               \
	int err;                                                                \
	if (pr != NULL) mutex_enter(bt_lock);                                   \
	err = p##_attach(so, proto);                                            \
	if (pr != NULL) mutex_exit(bt_lock);                                    \
	return err;                                                             \
}                                                                               \
static int p##_xnu_detach(struct socket *so)                                   \
{ p##_detach(so); so->so_flags |= SOF_PCBCLEARING; return 0; }                 \
static int p##_xnu_accept(struct socket *so, struct sockaddr **nam)            \
{                                                                               \
	struct sockaddr_bt sa; int err;                                         \
	bzero(&sa, sizeof(sa));                                                 \
	err = p##_accept(so, (struct sockaddr *)&sa);                           \
	if (err == 0) {                                                         \
	        sa.bt_len = sizeof(sa); sa.bt_family = AF_BLUETOOTH;            \
	        *nam = dup_sockaddr((struct sockaddr *)&sa, 1);                 \
	        if (*nam == NULL) err = ENOMEM;                                 \
	}                                                                       \
	return err;                                                             \
}                                                                               \
static int p##_xnu_bind(struct socket *so, struct sockaddr *nam, struct proc *pr) \
{ return p##_bind(so, nam, pr); }                                               \
static int p##_xnu_listen(struct socket *so, struct proc *pr)                  \
{ return p##_listen(so, pr); }                                                  \
static int p##_xnu_connect(struct socket *so, struct sockaddr *nam, struct proc *pr) \
{ return p##_connect(so, nam, pr); }                                            \
static int p##_xnu_connect2(struct socket *so, struct socket *so2)             \
{ return p##_connect2(so, so2); }                                               \
static int p##_xnu_disconnect(struct socket *so) { return p##_disconnect(so); } \
static int p##_xnu_shutdown(struct socket *so) { return p##_shutdown(so); }     \
static int p##_xnu_abort(struct socket *so) { return p##_abort(so); }           \
static int p##_xnu_control(struct socket *so, u_long cmd, caddr_t data,        \
    struct ifnet *ifp, struct proc *pr)                                         \
{                                                                               \
	int err;                                                                \
	socket_unlock(so, 0);                                                   \
	err = p##_ioctl(so, cmd, data, ifp);                                    \
	socket_lock(so, 0);                                                     \
	return err == EPASSTHROUGH ? EOPNOTSUPP : err;                          \
}                                                                               \
static int p##_xnu_peeraddr(struct socket *so, struct sockaddr **nam)          \
{                                                                               \
	struct sockaddr_bt sa; int err;                                         \
	bzero(&sa, sizeof(sa));                                                 \
	err = p##_peeraddr(so, (struct sockaddr *)&sa);                         \
	if (err == 0 && (*nam = dup_sockaddr((struct sockaddr *)&sa, 1)) == NULL) \
	        err = ENOMEM;                                                   \
	return err;                                                             \
}                                                                               \
static int p##_xnu_sockaddr(struct socket *so, struct sockaddr **nam)          \
{                                                                               \
	struct sockaddr_bt sa; int err;                                         \
	bzero(&sa, sizeof(sa));                                                 \
	err = p##_sockaddr(so, (struct sockaddr *)&sa);                         \
	if (err == 0 && (*nam = dup_sockaddr((struct sockaddr *)&sa, 1)) == NULL) \
	        err = ENOMEM;                                                   \
	return err;                                                             \
}                                                                               \
static int p##_xnu_rcvd(struct socket *so, int flags)                          \
{ return p##_rcvd(so, flags, current_proc()); }                                 \
static int p##_xnu_rcvoob(struct socket *so, struct mbuf *m, int flags)        \
{ return p##_recvoob(so, m, flags); }                                           \
static int p##_xnu_send(struct socket *so, int flags, struct mbuf *m,          \
    struct sockaddr *nam, struct mbuf *control, struct proc *pr)                \
{                                                                               \
	if (flags & PRUS_OOB)                                                   \
	        return p##_sendoob(so, m, control);                             \
	return p##_send(so, m, nam, control, pr);                               \
}                                                                               \
struct pr_usrreqs p##_usrreqs = {                                              \
	.pru_attach = p##_xnu_attach,                                           \
	.pru_detach = p##_xnu_detach,                                           \
	.pru_accept = p##_xnu_accept,                                           \
	.pru_bind = p##_xnu_bind,                                               \
	.pru_listen = p##_xnu_listen,                                           \
	.pru_connect = p##_xnu_connect,                                         \
	.pru_connect2 = p##_xnu_connect2,                                       \
	.pru_disconnect = p##_xnu_disconnect,                                   \
	.pru_shutdown = p##_xnu_shutdown,                                       \
	.pru_abort = p##_xnu_abort,                                             \
	.pru_control = p##_xnu_control,                                         \
	.pru_peeraddr = p##_xnu_peeraddr,                                       \
	.pru_sockaddr = p##_xnu_sockaddr,                                       \
	.pru_rcvd = p##_xnu_rcvd,                                               \
	.pru_rcvoob = p##_xnu_rcvoob,                                           \
	.pru_send = p##_xnu_send,                                               \
	.pru_sosend = sosend,                                                   \
	.pru_soreceive = soreceive,                                             \
};

#endif /* _NETBT_COMPAT_H_ */
