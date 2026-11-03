/*
 * netbt_bridge.c -- netbt's struct hci_if (netbt/hci.h) on top of the plain
 * C ops in netbt_bridge.h, so an IOKit driver can be an HCI unit without
 * seeing NetBSD types. Modeled on NetBSD's own H4 UART driver
 * (sys/dev/bluetooth/btuart.c in our pin): output mbufs arrive with the
 * H4 type byte first and are freed once handed to the transport
 * (btuartstart), input packets go to hci_input_event/hci_input_acl with
 * the type byte first (btuart_input).
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>

#include <netbt/bluetooth.h>
#include <netbt/hci.h>

#include "netbt_bridge.h"

struct netbt_bridge {
	struct netbt_device nb_dev;     /* device_t points here (netbt_compat.h) */
	const struct netbt_bridge_ops *nb_ops;
	void *nb_ctx;
	struct hci_unit *nb_unit;
	kmutex_t nb_statlock;
	struct bt_stats nb_stats;
	bool nb_enabled;
};

#define BRIDGE(dev)     ((struct netbt_bridge *)device_private(dev))
#define H4_MAX          (1 + 4 + 1021)  /* type + ACL header + max ACL payload (kBtPacketMax) */

static int
bridge_enable(device_t dev)
{
	struct netbt_bridge *nb = BRIDGE(dev);
	int err = nb->nb_ops->enable(nb->nb_ctx);

	if (err == 0) {
		nb->nb_enabled = true;
	}
	return err;
}

static void
bridge_disable(device_t dev)
{
	struct netbt_bridge *nb = BRIDGE(dev);

	nb->nb_enabled = false;
	nb->nb_ops->disable(nb->nb_ctx);
}

/*
 * The transport takes a linear packet: copy the chain out (at most one ACL
 * packet, 1026 bytes) and free it, as btuartstart frees after sending.
 */
static void
bridge_output(device_t dev, struct mbuf *m, uint32_t *counter)
{
	struct netbt_bridge *nb = BRIDGE(dev);
	uint8_t buf[H4_MAX];
	int len = m->m_pkthdr.len;
	bool ok = false;

	if (nb->nb_enabled && len > 0 && len <= (int)sizeof(buf)) {
		m_copydata(m, 0, len, buf);
		ok = nb->nb_ops->transmit(nb->nb_ctx, buf, (size_t)len) == 0;
	}
	m_freem(m);
	mutex_enter(&nb->nb_statlock);
	if (ok) {
		(*counter)++;
		nb->nb_stats.byte_tx += (uint32_t)len;
	} else {
		nb->nb_stats.err_tx++;
	}
	mutex_exit(&nb->nb_statlock);
}

static void
bridge_output_cmd(device_t dev, struct mbuf *m)
{
	bridge_output(dev, m, &BRIDGE(dev)->nb_stats.cmd_tx);
}

static void
bridge_output_acl(device_t dev, struct mbuf *m)
{
	bridge_output(dev, m, &BRIDGE(dev)->nb_stats.acl_tx);
}

/*
 * SCO: on the Pi 3 the controller routes SCO to its PCM pins, not the UART
 * (docs/bluetooth-stack-plan.md S6), so there is no SCO transport. netbt
 * expects every SCO mbuf back through hci_complete_sco (btuartstart); a
 * NULL context means nobody is waiting for it, else complete it as sent so
 * the sco pcb's accounting does not stall.
 */
static void
bridge_output_sco(device_t dev, struct mbuf *m)
{
	struct netbt_bridge *nb = BRIDGE(dev);

	mutex_enter(&nb->nb_statlock);
	nb->nb_stats.err_tx++;
	mutex_exit(&nb->nb_statlock);
	if (M_GETCTX(m, void *) == NULL) {
		m_freem(m);
	} else {
		(void)hci_complete_sco(nb->nb_unit, m);
	}
}

static void
bridge_get_stats(device_t dev, struct bt_stats *dest, int flush)
{
	struct netbt_bridge *nb = BRIDGE(dev);

	mutex_enter(&nb->nb_statlock);
	memcpy(dest, &nb->nb_stats, sizeof(*dest));
	if (flush) {
		memset(&nb->nb_stats, 0, sizeof(nb->nb_stats));
	}
	mutex_exit(&nb->nb_statlock);
}

static const struct hci_if bridge_hci_if = {
	.enable = bridge_enable,
	.disable = bridge_disable,
	.output_cmd = bridge_output_cmd,
	.output_acl = bridge_output_acl,
	.output_sco = bridge_output_sco,
	.get_stats = bridge_get_stats,
	.ipl = IPL_TTY,
};

struct netbt_bridge *
netbt_bridge_attach(const char *name, const struct netbt_bridge_ops *ops, void *ctx)
{
	struct netbt_bridge *nb;

	if (ops == NULL || ops->enable == NULL || ops->disable == NULL || ops->transmit == NULL) {
		return NULL;
	}
	nb = kheap_alloc(KHEAP_DEFAULT, sizeof(*nb), Z_WAITOK | Z_ZERO);
	if (nb == NULL) {
		return NULL;
	}
	strlcpy(nb->nb_dev.dv_xname, name, sizeof(nb->nb_dev.dv_xname));
	nb->nb_dev.dv_softc = nb;
	nb->nb_ops = ops;
	nb->nb_ctx = ctx;
	mutex_init(&nb->nb_statlock, MUTEX_DRIVER, IPL_TTY);
	nb->nb_unit = hci_attach_pcb(&bridge_hci_if, &nb->nb_dev, 0);
	if (nb->nb_unit == NULL) {
		mutex_destroy(&nb->nb_statlock);
		kheap_free(KHEAP_DEFAULT, nb, sizeof(*nb));
		return NULL;
	}
	printf("%s: Bluetooth HCI unit attached (netbt)\n", name);
	return nb;
}

void
netbt_bridge_detach(struct netbt_bridge *nb)
{
	if (nb == NULL) {
		return;
	}
	hci_detach_pcb(nb->nb_unit);    /* disables the unit first */
	mutex_destroy(&nb->nb_statlock);
	kheap_free(KHEAP_DEFAULT, nb, sizeof(*nb));
}

int
netbt_bridge_input(struct netbt_bridge *nb, const uint8_t *pkt, size_t len)
{
	struct mbuf *m;
	bool ok = false;

	if (nb == NULL || len < 1 || len > H4_MAX || !nb->nb_enabled) {
		return EINVAL;
	}
	m = m_gethdr(M_DONTWAIT, MT_DATA);
	if (m != NULL) {
		m->m_pkthdr.len = m->m_len = 0;
		m_copyback(m, 0, (int)len, pkt);
		if (m->m_pkthdr.len != (int)len) {
			m_freem(m);     /* m_copyback could not extend the chain */
			m = NULL;
		}
	}
	if (m == NULL) {
		mutex_enter(&nb->nb_statlock);
		nb->nb_stats.err_rx++;
		mutex_exit(&nb->nb_statlock);
		return ENOBUFS;
	}
	switch (pkt[0]) {
	case HCI_EVENT_PKT:
		ok = hci_input_event(nb->nb_unit, m);   /* consumes m either way */
		break;
	case HCI_ACL_DATA_PKT:
		ok = hci_input_acl(nb->nb_unit, m);
		break;
	case HCI_SCO_DATA_PKT:
		ok = hci_input_sco(nb->nb_unit, m);
		break;
	default:
		m_freem(m);
		break;
	}
	mutex_enter(&nb->nb_statlock);
	if (ok) {
		if (pkt[0] == HCI_EVENT_PKT) {
			nb->nb_stats.evt_rx++;
		} else if (pkt[0] == HCI_ACL_DATA_PKT) {
			nb->nb_stats.acl_rx++;
		} else {
			nb->nb_stats.sco_rx++;
		}
		nb->nb_stats.byte_rx += (uint32_t)len;
	} else {
		nb->nb_stats.err_rx++;
	}
	mutex_exit(&nb->nb_statlock);
	return ok ? 0 : ENOBUFS;
}

