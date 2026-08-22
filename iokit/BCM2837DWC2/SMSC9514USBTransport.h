/*
 * Concrete SMSCUSBTransport over the BCM2837 DWC2 host controller. See
 * SMSC9514USBTransport.cpp for the protocol references and, importantly,
 * for its verification status: the layers beneath it are QEMU-verified,
 * this layer is not and cannot be (QEMU models no LAN9514) - it needs real
 * hardware.
 *
 * This deliberately does NOT include SMSC9514Ethernet.h, and does not
 * inherit from SMSCUSBTransport, even though it implements exactly that
 * interface method-for-method. SMSC9514Ethernet.h pulls in
 * IOKit/network/IOEthernetController.h and the rest of IONetworkingFamily,
 * which is not currently compiled into this kernel (SMSC9514Ethernet.cpp is
 * deliberately absent from iokit/conf/files - see tools/sync_iokit_drivers.sh's
 * NOT_BUILT list, and the INET/INET6 domain-registration decision recorded
 * in architecture.md). Making the USB transport depend on that header would
 * drag the whole networking family into the kernel build as a side effect
 * of landing a USB driver.
 *
 * So the signatures below are kept deliberately identical to
 * SMSCUSBTransport's, and the two are joined at the point where Ethernet is
 * actually wired up (Phase 6b), by which time IONetworkingFamily will be in
 * the build for its own reasons. If either side's signature drifts before
 * then, that join will fail to compile - which is the intended outcome, and
 * far better than a silent mismatch.
 */

#ifndef _SMSC9514_USB_TRANSPORT_H
#define _SMSC9514_USB_TRANSPORT_H

#include "BCM2837DWC2.h"

class SMSC9514USBTransport
{
public:
	// Called for each Ethernet frame unpacked from a bulk-IN transfer.
	// `frame`/`frameLen` exclude both the RX status word and the FCS.
	typedef void (*RxFrameHandler)(void *context, void *frame, uint32_t frameLen);

	SMSC9514USBTransport() : fController(NULL), fDevice(NULL) {}
	virtual ~SMSC9514USBTransport() {}

	// Bind to an already-enumerated, already-configured device.
	bool init(BCM2837DWC2 *controller, const DWC2USBDevice *device);

	// --- SMSCUSBTransport interface (signatures must stay in step) ---
	virtual bool readRegister(uint32_t offset, uint32_t *value);
	virtual bool writeRegister(uint32_t offset, uint32_t value);
	virtual uint32_t bulkIn(void *buf, uint32_t maxLen);
	virtual uint32_t bulkOut(const void *buf, uint32_t len);

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
