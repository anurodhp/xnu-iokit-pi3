/*
 * Driver for a real, standard USB CDC-ECM (Ethernet Control Model, CDC120
 * spec chapter 3.3/appendix G) Ethernet function - the DWC2-native
 * counterpart to iokit/SMSC9514Ethernet/ for a device class rather than one
 * specific chip.
 *
 * ================================ WHY / SCOPE ============================
 *
 * QEMU's `-device usb-net` model (third_party/qemu-zhuowei/hw/usb/
 * dev-network.c) is dual-mode: it presents both an RNDIS configuration and
 * a genuine standard CDC-ECM configuration, and SMSC9514Ethernet can never
 * match it (it is not a LAN9514 - see that driver's own header). DAR-243
 * tracked getting a real, attached `en0` under QEMU; DAR-244 found the
 * initially-assumed fix (an IOUSBInterface-based personality match against
 * a real ported AppleUSBCDCECM, third_party/AppleUSBCDCDriver) is a dead
 * end - this kernel never vendors IOUSBFamily and publishes no
 * IOUSBDevice/IOUSBInterface nubs for anything to match against (see
 * BCM2837DWC2.h's own scope note). This driver is DAR-244's second, chosen
 * option: the exact "hand-construct/attach/start, no personality match"
 * shape SMSC9514Ethernet/SMSC9514USBTransport already established in this
 * tree, applied to the CDC-ECM class instead of one chip's vendor protocol.
 *
 * Split the same way SMSC9514Ethernet is, and for the same reason:
 *   - CDCECMEthernet (this class): protocol-independent IOEthernetController
 *     glue - medium table, packet filter policy, RX/TX queueing - talking
 *     only to the abstract CDCECMUSBTransport interface below.
 *   - CDCECMUSBTransport: a pure-virtual interface capturing exactly what
 *     this driver needs from the USB layer. DWC2CDCECMTransport
 *     (iokit/BCM2837DWC2/DWC2CDCECMTransport.h) is the one concrete
 *     implementation, over BCM2837DWC2's transfer()/controlTransfer()
 *     primitives - explicitly NOT IOUSBPipe-based, per DAR-244's framing:
 *     this is a genuine rewrite of the I/O layer modeled on real
 *     AppleUSBCDCECM structure/logic (third_party/AppleUSBCDCDriver/
 *     AppleUSBCDCECM/{ControlDriver,DataDriver}), not literal reuse of its
 *     IOUSBFamily-based source.
 *
 * CDC-ECM itself needs no chip-specific register protocol the way SMSC's
 * vendor requests do - configuration selection, interface activation, and
 * discovering the real station address are all real USB standard/CDC-class
 * requests, and DWC2CDCECMTransport does all of that in its own
 * configure() before this driver's start() ever runs (see that file for
 * the wire-level detail, including the non-obvious "QEMU's device
 * enumerates as RNDIS by default, and CDC-ECM has to be explicitly
 * selected" finding).
 */

#ifndef _CDCECM_ETHERNET_H
#define _CDCECM_ETHERNET_H

#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>

class IONetworkMedium;
class IOTimerEventSource;

/*
 * Abstract USB transport CDCECMEthernet talks to - the CDC-ECM analogue of
 * SMSC9514Ethernet.h's SMSCUSBTransport, but the shape of what it needs is
 * different: CDC-ECM defines the station address and the RX filter as
 * standard class requests/descriptors, not chip registers, so there is no
 * readRegister()/writeRegister() pair here at all.
 */
class CDCECMUSBTransport
{
public:
	virtual ~CDCECMUSBTransport() {}

	/*
	 * Bulk IN (RX) / bulk OUT (TX) data-interface transfers. CDC-ECM
	 * framing is just raw 802.3 frames with no header/trailer to add or
	 * strip (unlike SMSC's TX_CMD_A/B and RX_STS words) - a frame boundary
	 * is purely a USB-layer concept: a short packet, or (when the frame
	 * length happens to be an exact multiple of the endpoint's max packet
	 * size) a trailing zero-length packet. The concrete transport owns
	 * emitting/consuming that trailing ZLP; callers here just see whole
	 * frames. Returns actual byte count moved, or 0 on failure - for
	 * bulkIn() specifically, 0 is also the ordinary "nothing queued yet"
	 * outcome of a short poll, not necessarily an error.
	 */
	virtual uint32_t bulkIn(void *buf, uint32_t maxLen) = 0;
	virtual uint32_t bulkOut(const void *buf, uint32_t len) = 0;

	/*
	 * CDC120 6.2.4 SET_ETHERNET_PACKET_FILTER (bRequest 0x43), sent to the
	 * Communications (control) interface. `filterBitmap` is built from the
	 * kPacketType* bits below. Some real devices - and QEMU's emulated
	 * device, confirmed by reading hw/usb/dev-network.c's
	 * usb_net_handle_control(): it handles only the two RNDIS
	 * encapsulated-command requests and STALLs everything else - do not
	 * implement this request at all, so a false return is a real, expected
	 * outcome and callers must not treat it as fatal.
	 */
	virtual bool setPacketFilter(uint16_t filterBitmap) = 0;

	/*
	 * The device's real station address, from the Ethernet Functional
	 * Descriptor's iMACAddress string (CDC120 5.2.3.8) - the only address
	 * CDC-ECM itself defines; there is no register to read it from the way
	 * SMSC9514USBTransport's readRegister() can. Populated once, during
	 * the concrete transport's own configure() step; this is just a copy.
	 */
	virtual void getMACAddress(uint8_t addr[6]) const = 0;

	/*
	 * Opportunistic poll of the interrupt/notification endpoint for a CDC
	 * NETWORK_CONNECTION notification (CDC120 6.3.1). Returns true and
	 * sets *linkUp only when a real notification was received this poll;
	 * returns false (leaving *linkUp untouched) when nothing was queued,
	 * which is the ordinary idle case - and the ONLY case QEMU's emulated
	 * device ever produces in CDC mode, confirmed by reading
	 * dev-network.c's usb_net_handle_statusin(): it answers only from
	 * s->rndis_resp, which nothing ever populates outside RNDIS mode, so
	 * it NAKs this poll unconditionally. See enable()'s header comment for
	 * how this driver copes with a device that will never send one.
	 */
	virtual bool pollNotification(bool *linkUp) = 0;
};

class CDCECMEthernet : public IOEthernetController
{
	OSDeclareDefaultStructors(CDCECMEthernet);

private:
	CDCECMUSBTransport  *fTransport; // not owned
	IOEthernetAddress    fMACAddress;
	IONetworkInterface  *fInterface; // cached from enable(), for inputPacketAvailable()
	IONetworkInterface  *fNetif;     // owned; the interface attachInterface() created in start()
	bool                 fLinkUp;

	// CDC-ECM exposes no negotiated speed/duplex on this driver's own
	// control path (that is an RNDIS-only OID, OID_GEN_LINK_SPEED, not
	// reachable in CDC mode - see dev-network.c's ndis_query(), which
	// nothing calls outside is_rndis()) - so there is exactly one medium,
	// unlike SMSC9514Ethernet's real MII-derived table.
	enum {
		kMediumIndexAuto = 0,
		kMediumCount
	};
	IONetworkMedium     *fMediumTable[kMediumCount];
	IOTimerEventSource  *fLinkTimer;

	IOTimerEventSource  *fRxTimer;
	uint8_t             *fRxBuffer;
	enum {
		// One CDC-ECM bulk-IN read is exactly one 802.3 frame - no
		// multi-frame packing the way SMSC's RX_STS framing allows - so
		// this only needs headroom past one max-size frame (1518 bytes).
		kRxBufferSize = 2048,
	};
	enum {
		// Same short-poll rationale as SMSC9514USBTransport::bulkIn()'s
		// header comment: BCM2837DWC2 has one shared host channel, so an
		// idle poll must not hold up TX or the notification poll behind
		// it on the shared workloop.
		kRxPollIntervalMS = 20,
		kLinkPollIntervalMS = 1000,
	};

	// CDC120 6.2.4 packet-filter bitmap. Real values, not invented - from
	// the spec table, cross-checked against Apple's own real reference
	// implementation (third_party/IOUSBFamily/Examples/USBCDCEthernet/
	// USBCDCEthernet.h).
	enum {
		kPacketTypePromiscuous  = 0x0001,
		kPacketTypeAllMulticast = 0x0002,
		kPacketTypeDirected     = 0x0004,
		kPacketTypeBroadcast    = 0x0008,
		kPacketTypeMulticast    = 0x0010,
	};
	uint16_t fPacketFilter;

	bool createMediumTables(void);
	void updateLinkStatus(void);
	static void linkTimerFired(OSObject *owner, IOTimerEventSource *sender);
	void pollReceive(void);
	static void rxTimerFired(OSObject *owner, IOTimerEventSource *sender);
	void pushPacketFilter(void);

public:
	virtual bool init(OSDictionary *properties, CDCECMUSBTransport *transport);
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free(void) APPLE_KEXT_OVERRIDE;

	// IONetworkController / IOEthernetController required overrides - same
	// set SMSC9514Ethernet overrides (see that header's comment on why
	// enablePacketFilter()/disablePacketFilter() are deliberately NOT
	// among them: IOEthernetController's own real implementation already
	// dispatches into setMulticastMode()/setPromiscuousMode() correctly).
	virtual IOReturn getHardwareAddress(IOEthernetAddress *addrP) APPLE_KEXT_OVERRIDE;
	virtual IOReturn setHardwareAddress(const void *addr, UInt32 addrBytes) APPLE_KEXT_OVERRIDE;
	virtual IOReturn getPacketFilters(const OSSymbol *group, UInt32 *filters) const APPLE_KEXT_OVERRIDE;
	virtual IOReturn setMulticastMode(bool active) APPLE_KEXT_OVERRIDE;
	virtual IOReturn setPromiscuousMode(bool active) APPLE_KEXT_OVERRIDE;
	virtual IOReturn setMulticastList(IOEthernetAddress *addrs, UInt32 count) APPLE_KEXT_OVERRIDE;
	virtual IOReturn enable(IONetworkInterface *interface) APPLE_KEXT_OVERRIDE;
	virtual IOReturn disable(IONetworkInterface *interface) APPLE_KEXT_OVERRIDE;
	virtual UInt32   outputPacket(mbuf_t m, void *param) APPLE_KEXT_OVERRIDE;

	// Called by pollReceive() with each frame's data (see fRxTimer).
	void inputPacketAvailable(void *frameData, uint32_t frameLen);

	// The IONetworkInterface attachInterface() created in start() - see
	// SMSC9514Ethernet::getEthernetInterface()'s identical header comment
	// on why BCM2837DWC2::start() needs this specific accessor.
	IONetworkInterface *getEthernetInterface(void) const { return fNetif; }
};

#endif /* _CDCECM_ETHERNET_H */
