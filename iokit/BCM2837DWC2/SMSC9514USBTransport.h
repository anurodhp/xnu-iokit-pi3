/*
 * Concrete SMSCUSBTransport over the BCM2837 DWC2 host controller. See
 * SMSC9514USBTransport.cpp for the protocol references and, importantly,
 * for its verification status: the layers beneath it are QEMU-verified;
 * this layer needed real hardware and got it - see probe()'s real-silicon
 * confirmation recorded in project history.
 *
 * Formally inherits from SMSCUSBTransport (SMSC9514Ethernet.h) - this is
 * the join this file used to describe as deferred: an earlier version of
 * this header deliberately did NOT include SMSC9514Ethernet.h or inherit
 * from SMSCUSBTransport, because that header pulls in
 * IOKit/network/IOEthernetController.h and the rest of IONetworkingFamily,
 * which at the time was not compiled into this kernel (SMSC9514Ethernet.cpp
 * was excluded from iokit/conf/files - see tools/sync_iokit_drivers.sh's
 * NOT_BUILT list, then). It kept every method's signature deliberately
 * identical to SMSCUSBTransport's so that whenever IONetworkingFamily did
 * land, the two could be joined by adding one base-class clause - which is
 * exactly what happened once IONetworkingFamily was vendored
 * (iokit/Network/, iokit/IOKit/network/) and both SMSC9514Ethernet.cpp and
 * SMSC9514USBTransport.cpp became unconditional entries in
 * iokit/conf/files. The old deferred-dependency rationale no longer
 * applies: IONetworkingFamily is now always part of this kernel build, not
 * something a USB driver could still avoid dragging in.
 */

#ifndef _SMSC9514_USB_TRANSPORT_H
#define _SMSC9514_USB_TRANSPORT_H

#include "BCM2837DWC2.h"
#include "SMSC9514Ethernet.h"

class SMSC9514USBTransport : public SMSCUSBTransport
{
public:
	// Called for each Ethernet frame unpacked from a bulk-IN transfer.
	// `frame`/`frameLen` exclude both the RX status word and the FCS.
	typedef void (*RxFrameHandler)(void *context, void *frame, uint32_t frameLen);

	SMSC9514USBTransport() : fController(NULL), fDevice(NULL) {}
	virtual ~SMSC9514USBTransport() {}

	// Bind to an already-enumerated, already-configured device.
	bool init(BCM2837DWC2 *controller, const DWC2USBDevice *device);

	// --- SMSCUSBTransport interface ---
	virtual bool readRegister(uint32_t offset, uint32_t *value) APPLE_KEXT_OVERRIDE;
	virtual bool writeRegister(uint32_t offset, uint32_t value) APPLE_KEXT_OVERRIDE;
	virtual uint32_t bulkIn(void *buf, uint32_t maxLen) APPLE_KEXT_OVERRIDE;
	virtual uint32_t bulkOut(const void *buf, uint32_t len) APPLE_KEXT_OVERRIDE;

	// Unpack the frames packed into one bulk-IN transfer. Returns the
	// number of good frames handed to `handler`.
	uint32_t forEachRxFrame(void *buf, uint32_t len,
	                        RxFrameHandler handler, void *context);

	/*
	 * Locate the LAN9514's Ethernet function among the devices an
	 * enumerateBus() left behind: SMSC's vendor ID with a bulk IN/OUT pair.
	 * Matched on vendor + endpoints rather than on the product ID so a
	 * LAN9512 (same driver, same protocol, different PID) is not excluded
	 * for no reason; the hub half of the same chip shares the vendor ID but
	 * has no bulk pair, so it cannot be picked up by mistake.
	 */
	static const DWC2USBDevice *findEthernetFunction(const BCM2837DWC2 *controller);

	/*
	 * Read-only bring-up self-test. Verifies that the vendor-request
	 * register protocol actually works against real silicon, which nothing
	 * has ever done - see the verification status note in the .cpp. Writes
	 * nothing, so it cannot disturb chip state or a later real driver.
	 */
	bool probe(void);

private:
	BCM2837DWC2         *fController;
	const DWC2USBDevice *fDevice;
	DWC2Endpoint         fControlEP;
	DWC2Endpoint         fBulkInEP;
	DWC2Endpoint         fBulkOutEP;
};

#endif /* _SMSC9514_USB_TRANSPORT_H */
