/*
 * LAN7800USBDriver - matches the Pi 3B+'s LAN7515 Ethernet function, USB
 * 0424:7800 (Linux bcm283x-rpi-lan7515.dtsi "usb424,7800"; FreeBSD
 * if_muge.c's device table, USB_VENDOR_SMC2 / USB_PRODUCT_SMC2_LAN7800_ETH),
 * as an ordinary IOUSBDevice that AppleUSBHub publishes behind the
 * LAN7515's two USB2514 hubs - the same arrangement SMSC9514USBDriver uses
 * for the 3B's LAN9514 (see that header), and the same transport
 * (SMSC9514FamilyTransport, with LAN7800Ethernet's buffer sizes). It
 * configures the device, finds the bulk pipe pair, gives LAN7800Ethernet a
 * workloop of its own and starts it.
 *
 * Needs AppleUSBHub (usb-apple-hub, on by default): the legacy hand-wired
 * BCM2837DWC2 path knows only the LAN9514.
 */
#ifndef _LAN7800_USB_DRIVER_H
#define _LAN7800_USB_DRIVER_H

#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/usb/IOUSBDevice.h>
#include <IOKit/usb/IOUSBInterface.h>
#include <IOKit/usb/IOUSBPipe.h>
#include "SMSC9514USBDriver.h"   // SMSC9514FamilyTransport
#include "LAN7800Ethernet.h"

class LAN7800USBDriver : public IOService
{
	OSDeclareDefaultStructors(LAN7800USBDriver);

public:
	virtual bool init(OSDictionary *properties = NULL) APPLE_KEXT_OVERRIDE;
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free(void) APPLE_KEXT_OVERRIDE;
	virtual IOReturn message(UInt32 type, IOService *provider, void *argument = 0) APPLE_KEXT_OVERRIDE;
	virtual IOWorkLoop *getWorkLoop(void) const APPLE_KEXT_OVERRIDE;

private:
	void teardown(void);

	IOUSBDevice             *fDevice;
	IOUSBInterface          *fInterface;
	IOUSBPipe               *fBulkIn;
	IOUSBPipe               *fBulkOut;
	SMSC9514FamilyTransport *fTransport;
	LAN7800Ethernet         *fEthernet;
	IOWorkLoop              *fWorkLoop;
};

#endif /* _LAN7800_USB_DRIVER_H */
