/*
 * Concrete CDCECMUSBTransport over the BCM2837 DWC2 host controller. See
 * DWC2CDCECMTransport.cpp for the real wire-level detail this header only
 * summarizes, and CDCECMEthernet.h for why this exists at all (DAR-243/
 * DAR-244).
 *
 * Unlike SMSC9514USBTransport (a straight mapping of an already-known
 * chip-specific vendor protocol onto DWC2 primitives), this class also does
 * real USB configuration selection: BCM2837USBEnum's generic enumeration
 * (BCM2837DWC2::enumerateBus(), which runs before any of this) always
 * leaves a device on its DEFAULT configuration (index 0) - which for
 * QEMU's dual-mode usb-net device is RNDIS, not CDC-ECM (see this file's
 * .cpp for the exact desc_device_net.confs[] citation proving that
 * ordering). configure() is what finds and switches to the real CDC-ECM
 * configuration, if the device offers one.
 */

#ifndef _DWC2_CDCECM_TRANSPORT_H
#define _DWC2_CDCECM_TRANSPORT_H

#include "BCM2837DWC2.h"
#include "CDCECMEthernet.h"

class DWC2CDCECMTransport : public CDCECMUSBTransport
{
public:
	DWC2CDCECMTransport();
	virtual ~DWC2CDCECMTransport() {}

	/*
	 * Locate a CDC-capable composite device among `controller`'s already-
	 * enumerated devices. Matched generically by DEVICE class
	 * (bDeviceClass == 2, USB Communications Device Class - the class this
	 * whole composite device reports at the device-descriptor level, per
	 * dev-network.c's desc_device_net.bDeviceClass = USB_CLASS_COMM), not
	 * by vendor/product ID - a real CDC-ECM dongle would match the same
	 * way a QEMU one does, unlike SMSC9514USBTransport::findEthernetFunction()'s
	 * necessarily vendor-ID-based match (SMSC has no distinguishing device
	 * class of its own).
	 */
	static const DWC2USBDevice *findCDCDevice(const BCM2837DWC2 *controller);

	/*
	 * Re-walks this already-enumerated device's other configuration
	 * descriptor(s) looking for a real CDC-ECM interface pair (Control:
	 * class 2 / subclass 6; Data: class 10, its bulk pair behind whichever
	 * alternate setting actually carries endpoints), switches the device
	 * to it (SET_CONFIGURATION, then SET_INTERFACE to activate the data
	 * interface's alternate setting), and fetches the real station address
	 * from the Ethernet Functional Descriptor's iMACAddress string.
	 * Leaves the device exactly as enumerateBus() left it and returns
	 * false if no CDC-ECM configuration is found or any step fails.
	 */
	bool configure(BCM2837DWC2 *controller, const DWC2USBDevice *device);

	// --- CDCECMUSBTransport ---
	virtual uint32_t bulkIn(void *buf, uint32_t maxLen) APPLE_KEXT_OVERRIDE;
	virtual uint32_t bulkOut(const void *buf, uint32_t len) APPLE_KEXT_OVERRIDE;
	virtual bool setPacketFilter(uint16_t filterBitmap) APPLE_KEXT_OVERRIDE;
	virtual void getMACAddress(uint8_t addr[6]) const APPLE_KEXT_OVERRIDE;
	virtual bool pollNotification(bool *linkUp) APPLE_KEXT_OVERRIDE;

private:
	BCM2837DWC2          *fController;
	const DWC2USBDevice  *fDevice;
	DWC2Endpoint          fControlEP;
	DWC2Endpoint          fBulkInEP;
	DWC2Endpoint          fBulkOutEP;
	DWC2Endpoint          fNotifyEP;
	bool                  fHaveNotifyEP;
	uint8_t               fControlInterfaceNumber;
	uint8_t               fMACAddress[6];

	bool parseCDCConfiguration(const uint8_t *cfg, uint32_t cfgLen,
	    uint8_t *ctrlIfNum, uint8_t *dataIfNum, uint8_t *dataAltSetting,
	    uint8_t *macStringIndex,
	    uint8_t *notifyAddr, uint16_t *notifyMax,
	    uint8_t *bulkInAddr, uint16_t *bulkInMax,
	    uint8_t *bulkOutAddr, uint16_t *bulkOutMax) const;
	bool fetchMACAddressString(uint8_t stringIndex);
};

#endif /* _DWC2_CDCECM_TRANSPORT_H */
