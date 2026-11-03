/*
 * netbt_xnu.c -- xnu glue for NetBSD's Bluetooth stack (bsd/netbt).
 * Replaces NetBSD's bt_proto.c (domain and protosw table) and implements
 * the NetBSD kernel services netbt_compat.h maps onto xnu.
 * docs/bluetooth-stack-plan.md S2.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/mbuf.h>
#include <kern/thread_call.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>

#include <netbt/bluetooth.h>
#include <netbt/hci.h>
#include <netbt/l2cap.h>
#include <netbt/rfcomm.h>
#include <netbt/sco.h>

lck_grp_t *netbt_lck_grp;
kmutex_t *bt_lock;
static lck_spin_t *netbt_callout_spin;

/* ------------------------------------------------------------ domain */

/*
 * NetBSD bt_proto.c's protocol table, as xnu protosws. pr_ctloutput in
 * NetBSD takes (PRCO_GETOPT|PRCO_SETOPT, so, sopt); xnu passes the
 * direction in sopt_dir (bsd/sys/socketvar.h struct sockopt).
 */
#define BT_CTLOUTPUT(p)                                                 \
static int                                                              \
p##_xnu_ctloutput(struct socket *so, struct sockopt *sopt)             \
{                                                                       \
	return p##_ctloutput(sopt->sopt_dir == SOPT_SET ? PRCO_SETOPT : PRCO_GETOPT, \
	    so, sopt);                                                  \
}
BT_CTLOUTPUT(hci)
BT_CTLOUTPUT(sco)
BT_CTLOUTPUT(l2cap)
BT_CTLOUTPUT(rfcomm)

static void hci_xnu_init(struct protosw *pp, struct domain *dp) { (void)pp; (void)dp; hci_init(); }
static void l2cap_xnu_init(struct protosw *pp, struct domain *dp) { (void)pp; (void)dp; l2cap_init(); }
static void rfcomm_xnu_init(struct protosw *pp, struct domain *dp) { (void)pp; (void)dp; rfcomm_init(); }

static struct protosw btsw[] = {
	{ /* raw HCI commands */
		.pr_type = SOCK_RAW,
		.pr_protocol = BTPROTO_HCI,
		.pr_flags = PR_ADDR | PR_ATOMIC,
		.pr_init = hci_xnu_init,
		.pr_ctloutput = hci_xnu_ctloutput,
		.pr_usrreqs = &hci_usrreqs,
	},
	{ /* HCI SCO data (audio) */
		.pr_type = SOCK_SEQPACKET,
		.pr_protocol = BTPROTO_SCO,
		.pr_flags = PR_CONNREQUIRED | PR_ATOMIC,
		.pr_ctloutput = sco_xnu_ctloutput,
		.pr_usrreqs = &sco_usrreqs,
	},
	{ /* L2CAP connection oriented */
		.pr_type = SOCK_SEQPACKET,
		.pr_protocol = BTPROTO_L2CAP,
		.pr_flags = PR_CONNREQUIRED | PR_ATOMIC,
		.pr_init = l2cap_xnu_init,
		.pr_ctloutput = l2cap_xnu_ctloutput,
		.pr_usrreqs = &l2cap_usrreqs,
	},
	{ /* RFCOMM */
		.pr_type = SOCK_STREAM,
		.pr_protocol = BTPROTO_RFCOMM,
		.pr_flags = PR_CONNREQUIRED | PR_WANTRCVD,
		.pr_init = rfcomm_xnu_init,
		.pr_ctloutput = rfcomm_xnu_ctloutput,
		.pr_usrreqs = &rfcomm_usrreqs,
	},
};

/*
 * Domain init: xnu's init_domain() has already set up dom_mtx
 * (bsd/kern/uipc_domain.c init_domain), which every socket of these
 * protocols is locked with (none has pr_lock). bt_lock IS that mutex, as
 * in NetBSD every netbt socket's so_lock is bt_lock.
 */
static void
bt_xnu_dom_init(struct domain *dp)
{
	unsigned i;

#ifdef BLUETOOTH_DEBUG
	/* netbt's DPRINTF level (bluetooth.h; NetBSD sets it with sysctl
	 * net.bluetooth.debug, which is not built here): boot-arg netbt-debug=N. */
	(void)PE_parse_boot_argn("netbt-debug", &bluetooth_debug, sizeof(bluetooth_debug));
#endif
	netbt_lck_grp = lck_grp_alloc_init("netbt", LCK_GRP_ATTR_NULL);
	netbt_callout_spin = lck_spin_alloc_init(netbt_lck_grp, LCK_ATTR_NULL);
	bt_lock = dp->dom_mtx;
	for (i = 0; i < __arraycount(btsw); i++) {
		net_add_proto(&btsw[i], dp, 1);
	}
}

struct domain btdomain_s = {
	.dom_family = AF_BLUETOOTH,
	.dom_name = "bluetooth",
	.dom_init = bt_xnu_dom_init,
};

/* ------------------------------------------------------------ sockets */

#undef sbappendaddr
int
netbt_sbappendaddr(struct sockbuf *sb, const struct sockaddr *sa, struct mbuf *m,
    struct mbuf *control)
{
	int need = sa->sa_len, err = 0;
	struct mbuf *n;

	for (n = m; n != NULL; n = n->m_next) {
		need += n->m_len;
	}
	for (n = control; n != NULL; n = n->m_next) {
		need += n->m_len;
	}
	if (need > sbspace(sb)) {
		return 0;               /* NetBSD: caller still owns m and control */
	}
	(void)sbappendaddr(sb, __DECONST(struct sockaddr *, sa), m, control, &err);
	return 1;                       /* appended, or dropped and freed by xnu */
}

/*
 * sockopt bridge (netbt_compat.h). A sockopt from a user's [gs]etsockopt
 * has sopt_p set (bsd/kern/uipc_syscalls.c) and goes through
 * sooptcopyin/sooptcopyout (bsd/kern/uipc_socket.c). netbt's own
 * kernel-to-kernel sockopts (sockopt_init: rfcomm asking l2cap for its MTU,
 * rfcomm_dlc.c:243, rfcomm_session.c:194) have sopt_p NULL and a small
 * kernel buffer at sopt_val; sopt_valsize is the current data length.
 */
#define NETBT_SOPT_MAX  64

void
netbt_sockopt_init(struct sockopt *s, int level, int name, size_t size)
{
	bzero(s, sizeof(*s));
	s->sopt_level = level;
	s->sopt_name = name;
	s->sopt_valsize = size <= NETBT_SOPT_MAX ? size : NETBT_SOPT_MAX;
	s->sopt_val = CAST_USER_ADDR_T(kheap_alloc(KHEAP_DEFAULT, NETBT_SOPT_MAX, Z_WAITOK | Z_ZERO));
	s->sopt_p = NULL;
}

void
netbt_sockopt_destroy(struct sockopt *s)
{
	if (s->sopt_p == NULL && s->sopt_val != USER_ADDR_NULL) {
		void *buf = CAST_DOWN(void *, s->sopt_val);
		kheap_free(KHEAP_DEFAULT, buf, NETBT_SOPT_MAX);
		s->sopt_val = USER_ADDR_NULL;
	}
}

int
netbt_sockopt_get(const struct sockopt *s, void *buf, size_t len)
{
	/* NetBSD sockopt_get: the size must match exactly (uipc_socket.c). */
	if (s->sopt_valsize != len) {
		return EINVAL;
	}
	if (s->sopt_p == NULL) {
		if (s->sopt_val == USER_ADDR_NULL) {
			return EINVAL;
		}
		bcopy(CAST_DOWN(void *, s->sopt_val), buf, len);
		return 0;
	}
	/* sooptcopyin only lowers sopt_valsize, here already == len. */
	return sooptcopyin(__DECONST(struct sockopt *, s), buf, len, len);
}

int
netbt_sockopt_set(struct sockopt *s, const void *buf, size_t len)
{
	if (s->sopt_p == NULL) {
		if (s->sopt_val == USER_ADDR_NULL || len > NETBT_SOPT_MAX) {
			return EINVAL;
		}
		bcopy(buf, CAST_DOWN(void *, s->sopt_val), len);
		s->sopt_valsize = len;
		return 0;
	}
	return sooptcopyout(s, __DECONST(void *, buf), len);
}

int
netbt_sockopt_getint(const struct sockopt *s, int *v)
{
	return netbt_sockopt_get(s, v, sizeof(*v));
}

int
netbt_sockopt_setint(struct sockopt *s, int v)
{
	return netbt_sockopt_set(s, &v, sizeof(v));
}

/* ------------------------------------------------------------ condvar */

int
netbt_cv_timedwait_sig(kcondvar_t *cv, kmutex_t *m, int ticks)
{
	struct timespec ts;

	ts.tv_sec = ticks / hz;
	ts.tv_nsec = (long)(ticks % hz) * (1000000000L / hz);
	/* msleep: EWOULDBLOCK on timeout, EINTR/ERESTART on a signal, as
	 * NetBSD cv_timedwait_sig (bsd/kern/kern_synch.c msleep). */
	return msleep((caddr_t)cv, m, PCATCH | PZERO, "netbtcv", &ts);
}

/* ------------------------------------------------------------ callout */

/*
 * States (c_flags), all changed under netbt_callout_lock:
 *   PENDING   scheduled and not stopped
 *   INVOKING  the trampoline took it from PENDING and is about to call, or
 *             is calling, the handler; cleared by the handler's callout_ack
 * The trampoline only calls the handler if PENDING was still set when it
 * took the lock, so a stop that wins the race suppresses the call. netbt
 * frees a callout only after stop when !invoking (l2cap_misc.c:173-176,
 * hci_link.c:999-1002); destroy then waits out a trampoline that is past
 * its dequeue but saw the stop (thread_call_cancel_wait), unless destroy
 * runs inside the handler itself (after its own ack), where waiting would
 * deadlock (osfmk/kern/thread_call.c:1364) and is unnecessary: xnu keeps a
 * reference while the callback runs (thread_call.c:1673), so freeing the
 * thread call from inside it is safe.
 */
#define C_PENDING       0x1
#define C_INVOKING      0x2

/* Set up in bt_xnu_dom_init, before netbt can create a callout. */
#define callout_spin()  netbt_callout_spin

static void
callout_trampoline(thread_call_param_t p0, thread_call_param_t p1)
{
	struct callout *c = p0;
	void (*fn)(void *);
	void *arg;
	(void)p1;

	lck_spin_lock(callout_spin());
	if (!(c->c_flags & C_PENDING)) {
		lck_spin_unlock(callout_spin());
		return;
	}
	c->c_flags = (c->c_flags & ~C_PENDING) | C_INVOKING;
	fn = c->c_func;
	arg = c->c_arg;
	c->c_thread = current_thread();
	lck_spin_unlock(callout_spin());
	fn(arg);
	/*
	 * c may be freed by now (the handler acked and destroyed it), so
	 * c_thread is not cleared. It may later match a pooled thread-call
	 * thread that runs netbt_callout_destroy for this callout; destroy
	 * then skips thread_call_cancel_wait, which is harmless because the
	 * handler is not running at that point.
	 */
}

void
netbt_callout_init(struct callout *c, unsigned int flags)
{
	(void)flags;
	bzero(c, sizeof(*c));
	c->c_call = thread_call_allocate_with_options(callout_trampoline, c,
	    THREAD_CALL_PRIORITY_KERNEL, 0);
}

void
netbt_callout_setfunc(struct callout *c, void (*fn)(void *), void *arg)
{
	c->c_func = fn;
	c->c_arg = arg;
}

void
netbt_callout_schedule(struct callout *c, int ticks)
{
	uint64_t deadline;

	if (c->c_call == NULL) {
		printf("netbt: callout %p scheduled without a thread call\n", c);
		return;
	}
	if (ticks < 1) {
		ticks = 1;
	}
	lck_spin_lock(callout_spin());
	c->c_flags |= C_PENDING;
	lck_spin_unlock(callout_spin());
	clock_interval_to_deadline((uint32_t)ticks, NSEC_PER_SEC / hz, &deadline);
	thread_call_enter_delayed(c->c_call, deadline);
}

bool
netbt_callout_stop(struct callout *c)
{
	bool was;

	lck_spin_lock(callout_spin());
	was = (c->c_flags & C_PENDING) != 0;
	c->c_flags &= ~C_PENDING;
	lck_spin_unlock(callout_spin());
	(void)thread_call_cancel(c->c_call);
	/* NetBSD: true if the callout had already fired (is invoking). */
	return !was && (c->c_flags & C_INVOKING) != 0;
}

bool
netbt_callout_pending(struct callout *c)
{
	return (c->c_flags & C_PENDING) != 0;
}

bool
netbt_callout_invoking(struct callout *c)
{
	return (c->c_flags & C_INVOKING) != 0;
}

void
netbt_callout_ack(struct callout *c)
{
	lck_spin_lock(callout_spin());
	c->c_flags &= ~C_INVOKING;
	lck_spin_unlock(callout_spin());
}

void
netbt_callout_destroy(struct callout *c)
{
	thread_call_t call = c->c_call;

	if (call == NULL) {
		return;
	}
	c->c_call = NULL;
	if (c->c_thread != current_thread()) {
		(void)thread_call_cancel_wait(call);
	} else {
		(void)thread_call_cancel(call);
	}
	(void)thread_call_free(call);
}

/* ------------------------------------------------------------ softint */

/*
 * softint(9) over a thread call: thread_call_enter on an already pending
 * call does not queue a second run (osfmk/kern/thread_call.c
 * thread_call_enter1: returns TRUE if it was already pending), which is
 * softint_schedule's coalescing. hci_intr takes bt_lock itself.
 */
struct netbt_softint {
	thread_call_t si_call;
	void (*si_func)(void *);
	void *si_arg;
};

static void
softint_trampoline(thread_call_param_t p0, thread_call_param_t p1)
{
	struct netbt_softint *si = p0;
	(void)p1;
	si->si_func(si->si_arg);
}

void *
netbt_softint_establish(unsigned int flags, void (*fn)(void *), void *arg)
{
	struct netbt_softint *si;
	(void)flags;

	si = kheap_alloc(KHEAP_DEFAULT, sizeof(*si), Z_WAITOK | Z_ZERO);
	if (si == NULL) {
		return NULL;
	}
	si->si_func = fn;
	si->si_arg = arg;
	si->si_call = thread_call_allocate_with_options(softint_trampoline, si,
	    THREAD_CALL_PRIORITY_KERNEL, 0);
	if (si->si_call == NULL) {
		kheap_free(KHEAP_DEFAULT, si, sizeof(*si));
		return NULL;
	}
	return si;
}

void
netbt_softint_schedule(void *cookie)
{
	struct netbt_softint *si = cookie;
	thread_call_enter(si->si_call);
}

void
netbt_softint_disestablish(void *cookie)
{
	struct netbt_softint *si = cookie;
	(void)thread_call_cancel_wait(si->si_call);
	(void)thread_call_free(si->si_call);
	kheap_free(KHEAP_DEFAULT, si, sizeof(*si));
}

/* ------------------------------------------------------------ kauth */

static netbt_kauth_cb_t netbt_kauth_cb;

kauth_listener_t
netbt_kauth_listen(netbt_kauth_cb_t cb)
{
	netbt_kauth_cb = cb;
	return (kauth_listener_t)(uintptr_t)1;  /* hci_init only checks for NULL */
}

int
netbt_kauth_authorize_device(kauth_cred_t cred, int action, void *a1, void *a2,
    void *a3, void *a4)
{
	if (cred != NOCRED && kauth_cred_issuser(cred)) {
		return 0;       /* secmodel_suser: root may do anything */
	}
	if (netbt_kauth_cb != NULL &&
	    netbt_kauth_cb(cred, action, NULL, a1, a2, a3, a4) == KAUTH_RESULT_ALLOW) {
		return 0;
	}
	return EPERM;
}

kauth_cred_t
netbt_kauth_cred_dup(void)
{
	kauth_cred_t c = kauth_cred_get();
	kauth_cred_ref(c);
	return c;
}

void
netbt_kauth_cred_free(kauth_cred_t c)
{
	kauth_cred_unref(&c);
}
