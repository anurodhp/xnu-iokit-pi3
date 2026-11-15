/*
 * LAN7800USBDriver - see the header. Structured as SMSC9514USBDriver
 * (DAR-353 stage 3), whose comments explain the workloop and teardown
 * choices; only the chip class and the transport's buffer sizes differ.
 */
#include "LAN7800USBDriver.h"
#include <IOKit/IOLib.h>

#define super IOService

OSDefineMetaClassAndStructors(LAN7800USBDriver, IOService);

bool
LAN7800USBDriver::init(OSDictionary *properties)
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
LAN7800USBDriver::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}
	fDevice = OSDynamicCast(IOUSBDevice, provider);
	if (fDevice == NULL || !fDevice->open(this)) {
		IOLog("LAN7800USBDriver: could not open the device\n");
		fDevice = NULL;
		return false;
	}
	// The LAN7800 has one configuration with one interface: bulk IN, bulk
	// OUT and an interrupt IN this driver does not use (FreeBSD muge_config,
	// MUGE_CONFIG_INDEX 0).
	const IOUSBConfigurationDescriptor *cd = fDevice->GetFullConfigurationDescriptor(0);
	if (cd == NULL ||
	    fDevice->SetConfiguration(this, cd->bConfigurationValue, false) != kIOReturnSuccess) {
		IOLog("LAN7800USBDriver: SetConfiguration failed\n");
		teardown();
		return false;
	}
	IOUSBFindInterfaceRequest ireq = {
		kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare,
		kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare,
	};
	fInterface = fDevice->FindNextInterface(NULL, &ireq);
	if (fInterface == NULL || !fInterface->open(this)) {
		IOLog("LAN7800USBDriver: no interface to open\n");
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
		IOLog("LAN7800USBDriver: no bulk IN/OUT pipe pair\n");
		fBulkIn = fBulkOut = NULL;
		teardown();
		return false;
	}
	fBulkIn->retain();
	fBulkOut->retain();

	fTransport = new SMSC9514FamilyTransport;
	uint32_t idRev = 0;
	if (fTransport == NULL ||
	    !fTransport->init(fDevice, fBulkIn, fBulkOut, LAN7800Ethernet::kRxBufferSize,
	        LAN7800Ethernet::kTxBufferSize, false) ||
	    !fTransport->readRegister(0x000 /* ID_REV */, &idRev)) {
		IOLog("LAN7800USBDriver: transport setup failed\n");
		teardown();
		return false;
	}
	IOLog("LAN7800USBDriver: %04x:%04x at USB address %u, speed %u, ID_REV %#010x\n",
	    fDevice->GetVendorID(), fDevice->GetProductID(), fDevice->GetAddress(),
	    fDevice->GetSpeed(), idRev);

	fWorkLoop = IOWorkLoop::workLoop();
	if (fWorkLoop == NULL) {
		teardown();
		return false;
	}
	fEthernet = new LAN7800Ethernet;
	if (fEthernet == NULL || !fEthernet->init(NULL, fTransport) || !fEthernet->attach(this)) {
		IOLog("LAN7800USBDriver: LAN7800Ethernet init/attach failed\n");
		OSSafeReleaseNULL(fEthernet);
		teardown();
		return false;
	}
	if (!fEthernet->start(this)) {
		IOLog("LAN7800USBDriver: LAN7800Ethernet start failed\n");
		fEthernet->detach(this);
		OSSafeReleaseNULL(fEthernet);
		teardown();
		return false;
	}
	registerService();
	return true;
}

void
LAN7800USBDriver::teardown(void)
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
LAN7800USBDriver::stop(IOService *provider)
{
	if (fEthernet != NULL) {
		if (!fEthernet->isInactive()) {
			fEthernet->terminate(kIOServiceRequired | kIOServiceSynchronous);
		}
		OSSafeReleaseNULL(fEthernet);
	}
	teardown();
	super::stop(provider);
}

IOWorkLoop *
LAN7800USBDriver::getWorkLoop(void) const
{
	return (fWorkLoop != NULL) ? fWorkLoop : super::getWorkLoop();
}

void
LAN7800USBDriver::free(void)
{
	OSSafeReleaseNULL(fWorkLoop);
	delete fTransport;   // outlives LAN7800Ethernet, which only borrows it
	fTransport = NULL;
	super::free();
}

IOReturn
LAN7800USBDriver::message(UInt32 type, IOService *provider, void *argument)
{
	if (type == kIOMessageServiceIsTerminated && fTransport != NULL) {
		fTransport->shutdown();
	}
	return super::message(type, provider, argument);
}
