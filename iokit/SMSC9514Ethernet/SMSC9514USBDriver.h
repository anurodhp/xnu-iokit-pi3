/*
 * SMSC9514USBDriver -- SMSC9514Ethernet on an IOUSBFamily-published device
 * (DAR-353 stage 3).
 *
 * With usb-apple-hub=1, Apple's AppleUSBHub enumerates the LAN9514 and
 * publishes its Ethernet function as an ordinary IOUSBDevice (0424:ec00).
 * This driver matches that device, the way a vendor's USB Ethernet driver
 * would on a Mac, configures it through the family, and starts
 * SMSC9514Ethernet on top of SMSC9514FamilyTransport - an SMSCUSBTransport
 * that talks through the device's pipes instead of BCM2837DWC2's own
 * channel API (SMSC9514USBTransport, the legacy hand-wired path).
 *
 * Because the device is family-owned, this driver is also what makes the
 * Ethernet survive a DAR-352 controller recovery: AppleUSBHub removes and
 * re-enumerates the device, IOKit terminates this instance with it, and a
 * new one matches the new device (DAR-352's "SMSC9514 re-bind" item).
 */

#ifndef _SMSC9514_USB_DRIVER_H
#define _SMSC9514_USB_DRIVER_H

#include <IOKit/IOService.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/usb/IOUSBDevice.h>
#include <IOKit/usb/IOUSBInterface.h>
#include <IOKit/usb/IOUSBPipe.h>
#include "SMSC9514Ethernet.h"

class SMSC9514FamilyTransport : public SMSCUSBTransport
{
public:
	SMSC9514FamilyTransport();
	virtual ~SMSC9514FamilyTransport();

	bool init(IOUSBDevice *device, IOUSBPipe *bulkIn, IOUSBPipe *bulkOut);
	// Abort the outstanding read and wait for its completion. Must run
	// before the pipes go away.
	void shutdown(void);

	virtual bool readRegister(uint32_t offset, uint32_t *value) APPLE_KEXT_OVERRIDE;
	virtual bool writeRegister(uint32_t offset, uint32_t value) APPLE_KEXT_OVERRIDE;
	virtual uint32_t bulkIn(void *buf, uint32_t maxLen) APPLE_KEXT_OVERRIDE;
	virtual uint32_t bulkOut(const void *buf, uint32_t len) APPLE_KEXT_OVERRIDE;

private:
	enum {
		kRxBufferSize = 4096,   // SMSC9514Ethernet's own kRxBufferSize
		kTxBufferSize = 2048,   // one frame + TX_CMD_A/B (SMSC9514Ethernet::outputPacket)
	};

	static void readComplete(void *target, void *parameter, IOReturn status,
	    UInt32 bufferSizeRemaining);
	void queueRead(void);       // with fLock held

	IOUSBDevice              *fDevice;
	IOUSBPipe                *fBulkIn;
	IOUSBPipe                *fBulkOut;
	IOBufferMemoryDescriptor *fRxMD;
	IOBufferMemoryDescriptor *fTxMD;
	IOLock                   *fLock;       // fRx* state, shared with readComplete()
	IOLock                   *fTxLock;     // fTxMD
	bool                      fRxQueued;   // a Read is outstanding
	bool                      fRxDone;     // it completed; fRxLen bytes wait in fRxMD
	uint32_t                  fRxLen;
	bool                      fStopping;
	bool                      fRxStalled;  // set by readComplete(), cleared by bulkIn()
	uint32_t                  fRxErrors;
	// DAR-465: bulk OUT stall/timeout events and failed sends, for
	// sampled logging.
	uint32_t                  fTxStallEvents;
	uint32_t                  fTxFailures;
};

class SMSC9514USBDriver : public IOService
{
	OSDeclareDefaultStructors(SMSC9514USBDriver);

public:
	virtual bool init(OSDictionary *properties = NULL) APPLE_KEXT_OVERRIDE;
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free(void) APPLE_KEXT_OVERRIDE;
	virtual IOReturn message(UInt32 type, IOService *provider, void *argument = 0) APPLE_KEXT_OVERRIDE;
	// SMSC9514Ethernet's workloop (it creates none; see the .cpp).
	virtual IOWorkLoop *getWorkLoop(void) const APPLE_KEXT_OVERRIDE;

private:
	void teardown(void);

	IOUSBDevice             *fDevice;
	IOUSBInterface          *fInterface;
	IOUSBPipe               *fBulkIn;
	IOUSBPipe               *fBulkOut;
	SMSC9514FamilyTransport *fTransport;
	SMSC9514Ethernet        *fEthernet;
	IOWorkLoop              *fWorkLoop;
};

#endif /* _SMSC9514_USB_DRIVER_H */
