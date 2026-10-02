/*
 * SMSC9514USBDriver / SMSC9514FamilyTransport (DAR-353 stage 3). See the
 * header for why this exists next to the legacy SMSC9514USBTransport.
 *
 * Register access is the same vendor-request protocol the legacy transport
 * uses and real silicon confirmed (SMSC9514USBTransport.cpp: bRequest 0xA0
 * write / 0xA1 read, wValue 0, wIndex = register offset, 4 little-endian
 * bytes; Linux smsc95xx_write_reg()/smsc95xx_read_reg()). Only the path
 * to the wire changes: IOUSBDevice::DeviceRequest() and IOUSBPipe instead
 * of BCM2837DWC2::controlRequest()/transfer().
 */

#include "SMSC9514USBDriver.h"
#include <IOKit/IOLib.h>

#define super IOService

#define kSMSCVendorWriteRegister 0xA0
#define kSMSCVendorReadRegister  0xA1

// Control requests and TX are bounded like the family's own synchronous
// requests: a device that never answers fails the call instead of hanging
// the caller. The values are this driver's, not Apple's.
static const UInt32 kCtrlNoDataTimeoutMS = 1000;
static const UInt32 kCtrlCompletionTimeoutMS = 2000;
static const UInt32 kTxNoDataTimeoutMS = 1000;
static const UInt32 kTxCompletionTimeoutMS = 2000;

#pragma mark - SMSC9514FamilyTransport

SMSC9514FamilyTransport::SMSC9514FamilyTransport()
	: fDevice(NULL), fBulkIn(NULL), fBulkOut(NULL), fRxMD(NULL), fTxMD(NULL),
	  fLock(NULL), fTxLock(NULL), fRxQueued(false), fRxDone(false), fRxLen(0),
	  fStopping(false), fRxStalled(false), fRxErrors(0),
	  fTxStallEvents(0), fTxFailures(0), fTxZlps(0)
{
}

SMSC9514FamilyTransport::~SMSC9514FamilyTransport()
{
	OSSafeReleaseNULL(fRxMD);
	OSSafeReleaseNULL(fTxMD);
	if (fLock != NULL) {
		IOLockFree(fLock);
	}
	if (fTxLock != NULL) {
		IOLockFree(fTxLock);
	}
}

bool
SMSC9514FamilyTransport::init(IOUSBDevice *device, IOUSBPipe *bulkIn, IOUSBPipe *bulkOut)
{
	if (device == NULL || bulkIn == NULL || bulkOut == NULL) {
		return false;
	}
	fDevice = device;
	fBulkIn = bulkIn;
	fBulkOut = bulkOut;
	fLock = IOLockAlloc();
	fTxLock = IOLockAlloc();
	fRxMD = IOBufferMemoryDescriptor::withCapacity(kRxBufferSize, kIODirectionIn);
	fTxMD = IOBufferMemoryDescriptor::withCapacity(kTxBufferSize, kIODirectionOut);
	if (fLock == NULL || fTxLock == NULL || fRxMD == NULL || fTxMD == NULL ||
	    fRxMD->prepare() != kIOReturnSuccess || fTxMD->prepare() != kIOReturnSuccess) {
		return false;
	}
	return true;
}

void
SMSC9514FamilyTransport::shutdown(void)
{
	IOLockLock(fLock);
	fStopping = true;
	bool queued = fRxQueued;
	IOLockUnlock(fLock);
	if (queued && fBulkIn != NULL) {
		// Returns the outstanding Read with kIOReturnAborted.
		fBulkIn->Abort();
	}
	// Wait for readComplete() to have run, so it never touches a freed
	// transport. Bounded: an aborted read completes promptly.
	for (int i = 0; i < 500; i++) {
		IOLockLock(fLock);
		queued = fRxQueued;
		IOLockUnlock(fLock);
		if (!queued) {
			break;
		}
		IOSleep(10);
	}
	if (queued) {
		IOLog("SMSC9514FamilyTransport: bulk IN read still outstanding after 5 s\n");
	}
}

bool
SMSC9514FamilyTransport::readRegister(uint32_t offset, uint32_t *value)
{
	if (fDevice == NULL || value == NULL) {
		return false;
	}
	UInt8 buf[4] = { 0, 0, 0, 0 };
	IOUSBDevRequest req;
	req.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBVendor, kUSBDevice);
	req.bRequest = kSMSCVendorReadRegister;
	req.wValue = 0;                  // always zero
	req.wIndex = (UInt16)offset;     // the register offset
	req.wLength = sizeof(buf);
	req.pData = buf;
	req.wLenDone = 0;
	IOReturn ret = fDevice->DeviceRequest(&req, kCtrlNoDataTimeoutMS,
	    kCtrlCompletionTimeoutMS);
	if (ret != kIOReturnSuccess || req.wLenDone != sizeof(buf)) {
		IOLog("SMSC9514FamilyTransport: read of reg %#x failed (%#x, %u bytes)\n",
		    offset, ret, (unsigned)req.wLenDone);
		return false;
	}
	*value = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
	    ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
	return true;
}

bool
SMSC9514FamilyTransport::writeRegister(uint32_t offset, uint32_t value)
{
	if (fDevice == NULL) {
		return false;
	}
	UInt8 buf[4] = {
		(UInt8)(value & 0xFF), (UInt8)((value >> 8) & 0xFF),
		(UInt8)((value >> 16) & 0xFF), (UInt8)((value >> 24) & 0xFF),
	};
	IOUSBDevRequest req;
	req.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice);
	req.bRequest = kSMSCVendorWriteRegister;
	req.wValue = 0;
	req.wIndex = (UInt16)offset;
	req.wLength = sizeof(buf);
	req.pData = buf;
	req.wLenDone = 0;
	IOReturn ret = fDevice->DeviceRequest(&req, kCtrlNoDataTimeoutMS,
	    kCtrlCompletionTimeoutMS);
	if (ret != kIOReturnSuccess) {
		IOLog("SMSC9514FamilyTransport: write of reg %#x failed (%#x)\n", offset, ret);
		return false;
	}
	return true;
}

/*
 * RX. SMSC9514Ethernet polls bulkIn() from its own timer and expects an
 * immediate answer - 0 when nothing arrived. The family model is instead a
 * read that stays outstanding until the device has data, completed from the
 * USB workloop; AppleUSBCDCECMData keeps its bulk IN reads queued exactly
 * that way (AppleUSBCDCECMData.cpp:473, re-queued from its completion).
 * This joins the two: one Read is always outstanding, readComplete() parks
 * its result, and the next bulkIn() hands it over and queues the next Read.
 */
void
SMSC9514FamilyTransport::queueRead(void)
{
	if (fRxQueued || fRxDone || fStopping || fBulkIn == NULL) {
		return;
	}
	IOUSBCompletion completion;
	completion.target = this;
	completion.action = &SMSC9514FamilyTransport::readComplete;
	completion.parameter = NULL;
	fRxQueued = true;
	// No timeouts: an idle link simply leaves the read outstanding.
	IOReturn ret = fBulkIn->Read(fRxMD, 0, 0, kRxBufferSize, &completion);
	if (ret != kIOReturnSuccess) {
		fRxQueued = false;
		if (fRxErrors++ < 10) {
			IOLog("SMSC9514FamilyTransport: bulk IN Read failed to queue (%#x)\n", ret);
		}
	}
}

void
SMSC9514FamilyTransport::readComplete(void *target, void *parameter, IOReturn status,
    UInt32 bufferSizeRemaining)
{
#pragma unused (parameter)
	SMSC9514FamilyTransport *me = (SMSC9514FamilyTransport *)target;
	IOLockLock(me->fLock);
	me->fRxQueued = false;
	if (status == kIOReturnSuccess) {
		me->fRxLen = kRxBufferSize - bufferSizeRemaining;
		me->fRxDone = (me->fRxLen != 0);
	} else if (status != kIOReturnAborted) {
		if (me->fRxErrors++ < 10) {
			IOLog("SMSC9514FamilyTransport: bulk IN read failed (%#x)\n", status);
		}
		// Cleared by the next bulkIn(), not here: ClearPipeStall(true)
		// sends a synchronous CLEAR_FEATURE, which the family refuses on
		// the USB workloop this completion runs on. AppleUSBCDCECMData
		// likewise clears from its submitting thread (clearPipeStall()
		// from USBTransmitPacket(), AppleUSBCDCECMData.cpp:2317-2318).
		if (status == kIOUSBPipeStalled) {
			me->fRxStalled = true;
		}
	}
	IOLockUnlock(me->fLock);
}

uint32_t
SMSC9514FamilyTransport::bulkIn(void *buf, uint32_t maxLen)
{
	if (buf == NULL || maxLen == 0) {
		return 0;
	}
	uint32_t len = 0;
	IOLockLock(fLock);
	bool stalled = fRxStalled;
	fRxStalled = false;
	IOLockUnlock(fLock);
	if (stalled && fBulkIn != NULL) {
		fBulkIn->ClearPipeStall(true);
	}
	IOLockLock(fLock);
	if (fRxDone) {
		len = (fRxLen < maxLen) ? fRxLen : maxLen;
		bcopy(fRxMD->getBytesNoCopy(), buf, len);
		fRxDone = false;
		fRxLen = 0;
	}
	queueRead();
	IOLockUnlock(fLock);
	return len;
}

uint32_t
SMSC9514FamilyTransport::bulkOut(const void *buf, uint32_t len)
{
	if (fBulkOut == NULL || buf == NULL || len == 0 || len > kTxBufferSize) {
		return 0;
	}
	IOLockLock(fTxLock);
	bcopy(buf, fTxMD->getBytesNoCopy(), len);
	IOReturn ret = fBulkOut->Write(fTxMD, kTxNoDataTimeoutMS, kTxCompletionTimeoutMS, len);
	/*
	 * A stalled Write leaves the pipe halted, and every later Write then
	 * fails with kIOUSBPipeStalled until the halt is cleared. Clear it
	 * (with the device-side CLEAR_FEATURE) and retry once, as
	 * AppleUSBCDCECMData does for its data pipe (clearPipeStall() then
	 * Write again, AppleUSBCDCECMData.cpp:2317-2319). This runs on the
	 * sender's thread, never the USB workloop, so the synchronous
	 * ClearPipeStall(true) is allowed here.
	 *
	 * DAR-465: a TIMED-OUT Write is cleared but not retried. The UIM fails
	 * a bulk OUT with kIOUSBTransactionTimeout after 5 s without progress
	 * (kDataOutTimeoutMS, BCM2837USBHostController.cpp:191, applied at
	 * :908-921); kIOReturnTimeout
	 * is kept because a real Pi returned it once (2026-09-25). Either way
	 * part of the frame may already be in the LAN9514's TX FIFO. Re-sending
	 * the frame whole on top of that misframes the TX stream, so return
	 * the failure and let SMSC9514Ethernet::outputPacket() restart the TX
	 * path (which flushes the FIFO) before it sends the frame again. That
	 * also stops a timeout from holding the driver's workloop for a second
	 * 5 s attempt.
	 *
	 * Logged on a sample (1st..4th, then every power of two), not per
	 * frame: a device that keeps re-stalling made this a serial flood.
	 * The CLEAR_FEATURE's own result is logged too.
	 */
	bool timedOut = (ret == kIOReturnTimeout || ret == kIOUSBTransactionTimeout);
	if (ret == kIOUSBPipeStalled || timedOut) {
		IOReturn first = ret;
		IOReturn clr = fBulkOut->ClearPipeStall(true);
		if (!timedOut) {
			ret = fBulkOut->Write(fTxMD, kTxNoDataTimeoutMS, kTxCompletionTimeoutMS, len);
		}
		fTxStallEvents++;
		if (fTxStallEvents <= 4 || (fTxStallEvents & (fTxStallEvents - 1)) == 0) {
			IOLog("SMSC9514FamilyTransport: bulk OUT %s %#x (event #%u, %u bytes): "
			    "ClearPipeStall(true) %#x, %s %#x\n",
			    timedOut ? "timed out" : "stalled", first, fTxStallEvents, len, clr,
			    timedOut ? "not retried, returning" : "retry", ret);
		}
	}
	/*
	 * DAR-465: end every transfer with a short packet. A transfer whose
	 * length is an exact multiple of the endpoint's max packet size (512 at
	 * high speed: a 504- or 1016-byte frame plus the 8-byte TX_CMD_A/B)
	 * otherwise ends on a full packet, and the device cannot tell where it
	 * stops. Linux's smsc95xx asks usbnet for exactly this zero-length
	 * packet: FLAG_SEND_ZLP (drivers/net/usb/smsc95xx.c:2013, v6.6), which
	 * usbnet_start_xmit() turns into URB_ZERO_PACKET when
	 * `length % dev->maxpacket == 0` (drivers/net/usb/usbnet.c:1399-1411).
	 * This driver never sent one. HYPOTHESIS (DAR-313), from the first Pi
	 * run: both wedges began with the device STALLing the third, short
	 * packet of a 1522-byte transfer after accepting the first 1024 bytes,
	 * which is what a device that merged the previous, unterminated
	 * transfer into this one and rejected it at the next short packet
	 * would do. BCM2837DWC2's DAR-465 OUT history line, logged at the
	 * first STALL, shows whether the previous transfer needed a ZLP.
	 */
	if (ret == kIOReturnSuccess) {
		UInt16 mps = fBulkOut->GetMaxPacketSize();
		if (mps != 0 && (len % mps) == 0) {
			IOReturn zret = fBulkOut->Write(fTxMD, kTxNoDataTimeoutMS,
			    kTxCompletionTimeoutMS, (IOByteCount)0);
			fTxZlps++;
			if (zret != kIOReturnSuccess || fTxZlps <= 2) {
				IOLog("SMSC9514FamilyTransport: DAR-465 ZLP #%u after a %u-byte "
				    "bulk OUT (max packet %u): %#x\n", fTxZlps, len, mps, zret);
			}
			ret = zret;
		}
	}
	IOLockUnlock(fTxLock);
	if (ret != kIOReturnSuccess) {
		fTxFailures++;
		if (fTxFailures <= 4 || (fTxFailures & (fTxFailures - 1)) == 0) {
			IOLog("SMSC9514FamilyTransport: bulk OUT of %u bytes failed (%#x), "
			    "failure #%u\n", len, ret, fTxFailures);
		}
		return 0;
	}
	return len;
}

#pragma mark - SMSC9514USBDriver

OSDefineMetaClassAndStructors(SMSC9514USBDriver, IOService);

bool
SMSC9514USBDriver::init(OSDictionary *properties)
{
	if (!super::init(properties)) {
		return false;
	}
	fDevice = NULL;
	fInterface = NULL;
	fBulkIn = NULL;
	fBulkOut = NULL;
	fTransport = NULL;
	fEthernet = NULL;
	fWorkLoop = NULL;
	return true;
}

bool
SMSC9514USBDriver::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}
	fDevice = OSDynamicCast(IOUSBDevice, provider);
	if (fDevice == NULL || !fDevice->open(this)) {
		IOLog("SMSC9514USBDriver: could not open the device\n");
		fDevice = NULL;
		return false;
	}

	// Configuration 0's value, then SetConfiguration without starting
	// interface matching: this driver owns the whole function.
	const IOUSBConfigurationDescriptor *cd = fDevice->GetFullConfigurationDescriptor(0);
	if (cd == NULL ||
	    fDevice->SetConfiguration(this, cd->bConfigurationValue, false) != kIOReturnSuccess) {
		IOLog("SMSC9514USBDriver: SetConfiguration failed\n");
		teardown();
		return false;
	}

	IOUSBFindInterfaceRequest ireq = {
		kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare,
		kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare,
	};
	fInterface = fDevice->FindNextInterface(NULL, &ireq);
	if (fInterface == NULL || !fInterface->open(this)) {
		IOLog("SMSC9514USBDriver: no interface to open\n");
		fInterface = NULL;
		teardown();
		return false;
	}
	fInterface->retain();

	IOUSBFindEndpointRequest ereq = { kUSBBulk, kUSBIn, 0, 0 };
	fBulkIn = fInterface->FindNextPipe(NULL, &ereq);
	ereq.type = kUSBBulk;
	ereq.direction = kUSBOut;
	ereq.maxPacketSize = 0;
	ereq.interval = 0;
	fBulkOut = fInterface->FindNextPipe(NULL, &ereq);
	if (fBulkIn == NULL || fBulkOut == NULL) {
		IOLog("SMSC9514USBDriver: no bulk IN/OUT pipe pair\n");
		fBulkIn = fBulkOut = NULL;
		teardown();
		return false;
	}
	fBulkIn->retain();
	fBulkOut->retain();

	fTransport = new SMSC9514FamilyTransport;
	uint32_t idRev = 0;
	if (fTransport == NULL || !fTransport->init(fDevice, fBulkIn, fBulkOut) ||
	    !fTransport->readRegister(0x000 /* ID_REV */, &idRev)) {
		IOLog("SMSC9514USBDriver: transport setup failed\n");
		teardown();
		return false;
	}
	IOLog("SMSC9514USBDriver: %04x:%04x at USB address %u, ID_REV %#010x\n",
	    fDevice->GetVendorID(), fDevice->GetProductID(), fDevice->GetAddress(), idRev);

	/*
	 * SMSC9514Ethernet runs its RX timer and register accesses on
	 * getWorkLoop(). IONetworkController::createWorkLoop() creates none
	 * (IONetworkController.cpp:447-450), so without this the lookup falls
	 * through the provider chain to the USB controller's workloop - where
	 * the family refuses every synchronous request those accesses make
	 * (kIOUSBSyncRequestOnWLThread, IOUSBController_Pipes.cpp:363-367).
	 * Give it a workloop of its own.
	 */
	fWorkLoop = IOWorkLoop::workLoop();
	if (fWorkLoop == NULL) {
		IOLog("SMSC9514USBDriver: no workloop\n");
		teardown();
		return false;
	}

	fEthernet = new SMSC9514Ethernet;
	if (fEthernet == NULL || !fEthernet->init(NULL, fTransport) ||
	    !fEthernet->attach(this)) {
		IOLog("SMSC9514USBDriver: SMSC9514Ethernet init/attach failed\n");
		OSSafeReleaseNULL(fEthernet);
		teardown();
		return false;
	}
	if (!fEthernet->start(this)) {
		IOLog("SMSC9514USBDriver: SMSC9514Ethernet start failed\n");
		fEthernet->detach(this);
		OSSafeReleaseNULL(fEthernet);
		teardown();
		return false;
	}

	registerService();
	return true;
}

// Called with the Ethernet child already stopped (IOKit stops children
// before their provider), or never started.
void
SMSC9514USBDriver::teardown(void)
{
	if (fTransport != NULL) {
		fTransport->shutdown();
	}
	OSSafeReleaseNULL(fBulkIn);
	OSSafeReleaseNULL(fBulkOut);
	if (fInterface != NULL) {
		fInterface->close(this);
		OSSafeReleaseNULL(fInterface);
	}
	if (fDevice != NULL) {
		fDevice->close(this);
		fDevice = NULL;
	}
}

void
SMSC9514USBDriver::stop(IOService *provider)
{
	if (fEthernet != NULL) {
		// Normally already terminated as our child; this covers a stop()
		// that did not come through termination.
		if (!fEthernet->isInactive()) {
			fEthernet->terminate(kIOServiceRequired | kIOServiceSynchronous);
		}
		OSSafeReleaseNULL(fEthernet);
	}
	teardown();
	super::stop(provider);
}

IOWorkLoop *
SMSC9514USBDriver::getWorkLoop(void) const
{
	return (fWorkLoop != NULL) ? fWorkLoop : super::getWorkLoop();
}

void
SMSC9514USBDriver::free(void)
{
	OSSafeReleaseNULL(fWorkLoop);
	// The transport outlives SMSC9514Ethernet, which only borrows it.
	delete fTransport;
	fTransport = NULL;
	super::free();
}

IOReturn
SMSC9514USBDriver::message(UInt32 type, IOService *provider, void *argument)
{
	if (type == kIOMessageServiceIsTerminated && fTransport != NULL) {
		// The device is going away (unplug, or AppleUSBHub re-enumerating
		// it after a DAR-352 recovery): stop RX before the pipes vanish.
		fTransport->shutdown();
	}
	return super::message(type, provider, argument);
}
