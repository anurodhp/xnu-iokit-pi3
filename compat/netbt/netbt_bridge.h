/*
 * netbt_bridge.h -- plain C interface between an IOKit Bluetooth controller
 * driver (BCM43430Bluetooth) and NetBSD's netbt (bsd/netbt). No NetBSD or
 * mbuf types, so C++ drivers can include it. Implemented in netbt_bridge.c,
 * which provides netbt's struct hci_if (netbt/hci.h) on top of these ops.
 * docs/bluetooth-stack-plan.md S1.
 *
 * Packets cross in H4 framing: the first byte is the packet type
 * (1 command, 2 ACL, 3 SCO, 4 event), exactly what netbt puts in its output
 * mbufs (hci_unit.c hci_send_cmd: p->type = HCI_CMD_PKT) and expects in its
 * input mbufs (hci_event.c reads hci_event_hdr_t, type first).
 */
#ifndef _NETBT_BRIDGE_H_
#define _NETBT_BRIDGE_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct netbt_bridge;

struct netbt_bridge_ops {
	/* hci_if enable/disable: netbt is bringing the unit up or down. 0 = ok. */
	int  (*enable)(void *ctx);
	void (*disable)(void *ctx);
	/*
	 * Queue one H4 packet for the controller. Called with netbt's bt_lock
	 * held (hci_unit.c hci_output_cmd, hci_link.c hci_acl_start), so it must
	 * not sleep. Returns 0 if queued; nonzero means dropped (netbt's
	 * command/ACL credit accounting then recovers by timeout, as with a
	 * lost packet on any transport).
	 */
	int  (*transmit)(void *ctx, const uint8_t *pkt, size_t len);
};

/*
 * Create an HCI unit named `name` (e.g. "bcmbt0"). It starts disabled;
 * userland enables it (btconfig <name> up -> hci_enable -> ops->enable).
 * Returns NULL on failure.
 */
struct netbt_bridge *netbt_bridge_attach(const char *name,
    const struct netbt_bridge_ops *ops, void *ctx);
void netbt_bridge_detach(struct netbt_bridge *);

/*
 * Hand netbt one received H4 packet (type byte first). Copies it; call from
 * thread context (not interrupt level). Returns 0 if accepted, nonzero if
 * dropped (no mbuf, or netbt's queue is full).
 */
int netbt_bridge_input(struct netbt_bridge *, const uint8_t *pkt, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* _NETBT_BRIDGE_H_ */
