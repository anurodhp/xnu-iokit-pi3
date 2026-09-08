/*
 * See DWC2CDCECMTransport.h for the class-level summary.
 *
 * ========================= PROTOCOL, VERIFIED AGAINST SOURCE =============
 *
 * Every wire-level fact this file relies on was verified against a real
 * source, not assumed:
 *
 *  - USB_CLASS_COMM=2 / USB_CLASS_CDC_DATA=0x0a / USB_DT_CS_INTERFACE=0x24 -
 *    third_party/qemu-zhuowei/include/hw/usb.h.
 *  - The CDC-ECM Control interface is class 2 / subclass 6 / protocol 0,
 *    the Data interface is class 10, and the Data interface's bulk pair
 *    sits behind alternate setting 1 (alt 0 is a deliberate zero-endpoint
 *    "off" state) - third_party/qemu-zhuowei/hw/usb/dev-network.c's
 *    desc_iface_cdc[].
 *  - QEMU's device enumerates under configuration INDEX 0 by default, and
 *    that is the RNDIS configuration, not CDC-ECM: dev-network.c's
 *    desc_device_net.confs[] lists desc_iface_rndis first (bConfigurationValue
 *    DEV_RNDIS_CONFIG_VALUE=2) and desc_iface_cdc second
 *    (bConfigurationValue DEV_CONFIG_VALUE=1) - GET_DESCRIPTOR(CONFIGURATION,
 *    wValue index=0) returns confs[0], i.e. RNDIS. This is *the* real,
 *    non-obvious finding that makes CDC-ECM otherwise unreachable: without
 *    explicitly requesting config descriptor INDEX 1 and switching to it,
 *    BCM2837USBEnum's ordinary enumerateBus() leaves this device configured
 *    for RNDIS, which nothing in this tree speaks.
 *  - SET_ETHERNET_PACKET_FILTER (bRequest 0x43) is real (CDC120 6.2.4,
 *    cross-checked against third_party/u-boot/include/linux/usb/cdc.h and
 *    third_party/IOUSBFamily/Examples/USBCDCEthernet/USBCDCEthernet.h) but
 *    QEMU's usb_net_handle_control() does not implement it - only the two
 *    RNDIS encapsulated-command requests are handled; everything else
 *    STALLs (dev-network.c's `fail:` case). setPacketFilter() below is
 *    written to tolerate that.
 *  - The Ethernet Functional Descriptor's iMACAddress string index and its
 *    12-hex-character UTF-16LE encoding (CDC120 5.2.3.8) - both
 *    dev-network.c's own descriptor bytes (STRING_ETHADDR) and
 *    AppleUSBCDCECMControl.cpp's real getFunctionalDescriptors()
 *    (GetStringDescriptor(iMACAddress, ...) then Asciihex_to_binary() pairs)
 *    agree on this encoding.
 *  - CDC-ECM's actual bulk-OUT framing requires an explicit trailing
 *    zero-length packet when a frame's length is an exact multiple of the
 *    endpoint's max packet size - confirmed against dev-network.c's
 *    usb_net_handle_dataout(), which only flushes the accumulated frame to
 *    the network `if (p->iov.size < 64)` (a short USB transfer). See
 *    bulkOut()'s own comment.
 */

#include "DWC2CDCECMTransport.h"

// Real USB-spec/CDC-spec constants (see this file's header comment for
// citations) - kept local rather than added to BCM2837DWC2.h's shared enum,
// same convention SMSC9514USBTransport.cpp already uses for its own
// chip-specific constants.
#define kUSBClassComm            2
#define kUSBClassCDCData         0x0A
#define kCDCSubclassEthernet     0x06
#define kUSBDescCSInterface      0x24
#define kCDCSubtypeEthernet      0x0F
#define kCDCSetEthernetPacketFilter 0x43

static uint8_t
asciiHexToBinary(uint8_t c)
{
	if (c >= '0' && c <= '9') {
		return (uint8_t)(c - '0');
	}
	if (c >= 'A' && c <= 'F') {
		return (uint8_t)(c - 'A' + 10);
	}
	if (c >= 'a' && c <= 'f') {
		return (uint8_t)(c - 'a' + 10);
	}
	return 0;
}

DWC2CDCECMTransport::DWC2CDCECMTransport()
    : fController(NULL), fDevice(NULL), fHaveNotifyEP(false), fControlInterfaceNumber(0)
{
	bzero(&fControlEP, sizeof(fControlEP));
	bzero(&fBulkInEP, sizeof(fBulkInEP));
	bzero(&fBulkOutEP, sizeof(fBulkOutEP));
	bzero(&fNotifyEP, sizeof(fNotifyEP));
	bzero(fMACAddress, sizeof(fMACAddress));
}

const DWC2USBDevice *
DWC2CDCECMTransport::findCDCDevice(const BCM2837DWC2 *controller)
{
	if (controller == NULL) {
		return NULL;
	}
	for (uint32_t i = 0; i < controller->deviceCount(); i++) {
		const DWC2USBDevice *dev = controller->deviceAt(i);
		if (dev == NULL || !dev->inUse) {
			continue;
		}
		if (dev->deviceClass == kUSBClassComm) {
			return dev;
		}
	}
	return NULL;
}

/*
 * Walk one configuration descriptor blob looking for a real CDC-ECM
 * interface pair. Same walking convention as BCM2837USBEnum.cpp's
 * parseConfiguration() (driven by each descriptor's own bLength, zero
 * bLength treated as fatal/truncating) but recording more than that
 * generic parser does: subclass, alternate setting, the class-specific
 * Ethernet Functional Descriptor's iMACAddress index, and the notification
 * endpoint - none of which BCM2837USBEnum's DWC2USBDevice struct has room
 * for, and none of which any other consumer in this tree needs, so this
 * stays local to the one driver that does.
 */
bool
DWC2CDCECMTransport::parseCDCConfiguration(const uint8_t *cfg, uint32_t cfgLen,
    uint8_t *ctrlIfNum, uint8_t *dataIfNum, uint8_t *dataAltSetting,
    uint8_t *macStringIndex,
    uint8_t *notifyAddr, uint16_t *notifyMax,
    uint8_t *bulkInAddr, uint16_t *bulkInMax,
    uint8_t *bulkOutAddr, uint16_t *bulkOutMax) const
{
	if (cfgLen < sizeof(USBConfigDescriptor)) {
		return false;
	}

	const USBConfigDescriptor *cd = (const USBConfigDescriptor *)cfg;
	uint32_t offset = cd->bLength;

	bool haveCtrl = false;
	bool haveEthDesc = false;
	bool haveData = false;
	uint8_t curIfNum = 0xFF, curIfClass = 0, curIfSubClass = 0, curAlt = 0;

	*notifyAddr = 0;
	*bulkInAddr = 0;
	*bulkOutAddr = 0;

	while (offset + 2 <= cfgLen) {
		uint8_t bLength = cfg[offset];
		uint8_t bType = cfg[offset + 1];

		if (bLength == 0 || offset + bLength > cfgLen) {
			IOLog("DWC2CDCECMTransport: malformed descriptor at offset %u "
			      "(bLength=%u, total=%u)\n", offset, bLength, cfgLen);
			break;
		}

		if (bType == kUSBDescInterface && bLength >= sizeof(USBInterfaceDescriptor)) {
			const USBInterfaceDescriptor *id = (const USBInterfaceDescriptor *)(cfg + offset);
			curIfNum = id->bInterfaceNumber;
			curIfClass = id->bInterfaceClass;
			curIfSubClass = id->bInterfaceSubClass;
			curAlt = id->bAlternateSetting;

			if (curIfClass == kUSBClassComm && curIfSubClass == kCDCSubclassEthernet && !haveCtrl) {
				haveCtrl = true;
				*ctrlIfNum = curIfNum;
			}
		} else if (bType == kUSBDescCSInterface && bLength >= 4 &&
		           haveCtrl && curIfClass == kUSBClassComm && curIfNum == *ctrlIfNum) {
			uint8_t subtype = cfg[offset + 2];
			if (subtype == kCDCSubtypeEthernet && bLength >= 4 + 1) {
				// Ethernet Functional Descriptor (CDC120 5.2.3.8):
				// bLength, bDescriptorType, bDescriptorSubType,
				// iMACAddress, bmEthernetStatistics[4], wMaxSegmentSize[2],
				// wNumberMCFilters[2], bNumberPowerFilters - iMACAddress is
				// byte 3 relative to this descriptor's start.
				*macStringIndex = cfg[offset + 3];
				haveEthDesc = true;
			}
		} else if (bType == kUSBDescEndpoint && bLength >= sizeof(USBEndpointDescriptor)) {
			const USBEndpointDescriptor *ed = (const USBEndpointDescriptor *)(cfg + offset);
			bool isIn = (ed->bEndpointAddress & 0x80) != 0;
			uint8_t xferType = ed->bmAttributes & 0x03;
			uint16_t mps = ed->wMaxPacketSize & 0x07FF;

			if (curIfClass == kUSBClassComm && haveCtrl && curIfNum == *ctrlIfNum &&
			    xferType == 0x03 /* interrupt */ && isIn && *notifyAddr == 0) {
				*notifyAddr = ed->bEndpointAddress;
				*notifyMax = mps;
			} else if (curIfClass == kUSBClassCDCData && xferType == 0x02 /* bulk */) {
				// Last-writer-wins is deliberate: if the data interface
				// has a zero-endpoint "off" alt setting followed by the
				// real one (QEMU's layout), only the real one ever
				// reaches here at all, so there is nothing to prefer
				// between - see this file's header comment.
				*dataIfNum = curIfNum;
				*dataAltSetting = curAlt;
				if (isIn) {
					*bulkInAddr = ed->bEndpointAddress;
					*bulkInMax = mps;
				} else {
					*bulkOutAddr = ed->bEndpointAddress;
					*bulkOutMax = mps;
				}
				if (*bulkInAddr != 0 && *bulkOutAddr != 0) {
					haveData = true;
				}
			}
		}

		offset += bLength;
	}

	return haveCtrl && haveEthDesc && haveData;
}

bool
DWC2CDCECMTransport::fetchMACAddressString(uint8_t stringIndex)
{
	if (fController == NULL || stringIndex == 0) {
		return false;
	}

	uint8_t buf[64];
	uint32_t actual = 0;

	// String descriptor 0 is a language-ID list (USB 2.0 spec 9.6.7), not
	// UTF-16 text - only the first supported language ID is used here.
	bzero(buf, sizeof(buf));
	IOReturn ret = fController->controlRequest(&fControlEP,
	    kUSBReqDirIn | kUSBReqTypeStandard | kUSBReqRecipDevice,
	    kUSBReqGetDescriptor, (kUSBDescString << 8) | 0, 0, 4, buf, &actual);
	if (ret != kIOReturnSuccess || actual < 4) {
		IOLog("DWC2CDCECMTransport: GET_DESCRIPTOR(STRING 0) failed (%#x)\n", ret);
		return false;
	}
	uint16_t langID = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);

	bzero(buf, sizeof(buf));
	ret = fController->controlRequest(&fControlEP,
	    kUSBReqDirIn | kUSBReqTypeStandard | kUSBReqRecipDevice,
	    kUSBReqGetDescriptor, (kUSBDescString << 8) | stringIndex, langID,
	    sizeof(buf), buf, &actual);
	// CDC120 5.2.3.8: iMACAddress is exactly 12 UTF-16LE hex characters -
	// a 2-byte descriptor header plus 24 bytes of string data.
	if (ret != kIOReturnSuccess || actual < 2 + 12 * 2) {
		IOLog("DWC2CDCECMTransport: GET_DESCRIPTOR(STRING %u) failed "
		      "(%#x, %u bytes)\n", stringIndex, ret, actual);
		return false;
	}

	for (int i = 0; i < 6; i++) {
		uint8_t hi = asciiHexToBinary(buf[2 + i * 4]);
		uint8_t lo = asciiHexToBinary(buf[2 + i * 4 + 2]);
		fMACAddress[i] = (uint8_t)((hi << 4) | lo);
	}
	IOLog("DWC2CDCECMTransport: MAC address from CDC descriptor: "
	      "%02x:%02x:%02x:%02x:%02x:%02x\n",
	    fMACAddress[0], fMACAddress[1], fMACAddress[2],
	    fMACAddress[3], fMACAddress[4], fMACAddress[5]);
	return true;
}

bool
DWC2CDCECMTransport::configure(BCM2837DWC2 *controller, const DWC2USBDevice *device)
{
	if (controller == NULL || device == NULL) {
		return false;
	}
	fController = controller;
	fDevice = device;

	if (!controller->makeEndpoint(device, 0, &fControlEP)) {
		IOLog("DWC2CDCECMTransport: no control endpoint\n");
		return false;
	}

	// Generous, real bound: no real device has anywhere near this many
	// configurations. GET_DESCRIPTOR fails (STALL/short read) once the
	// index runs past the device's real bNumConfigurations, which is how
	// this loop knows to stop rather than needing that count in advance.
	static const uint32_t kMaxConfigsToScan = 8;
	static const uint32_t kConfigBufferSize = 256; // matches BCM2837USBEnum.cpp's own bound

	uint8_t buf[kConfigBufferSize];
	uint8_t configValue = 0;
	uint8_t ctrlIfNum = 0, dataIfNum = 0, dataAltSetting = 0, macStringIndex = 0;
	uint8_t notifyAddr = 0, bulkInAddr = 0, bulkOutAddr = 0;
	uint16_t notifyMax = 0, bulkInMax = 0, bulkOutMax = 0;
	bool found = false;

	for (uint8_t idx = 0; idx < (uint8_t)kMaxConfigsToScan && !found; idx++) {
		uint32_t actual = 0;
		bzero(buf, sizeof(buf));
		IOReturn ret = controller->controlRequest(&fControlEP,
		    kUSBReqDirIn | kUSBReqTypeStandard | kUSBReqRecipDevice,
		    kUSBReqGetDescriptor, (kUSBDescConfig << 8) | idx, 0,
		    sizeof(USBConfigDescriptor), buf, &actual);
		if (ret != kIOReturnSuccess || actual < sizeof(USBConfigDescriptor)) {
			break; // no more configurations to try
		}

		uint16_t totalLen = ((const USBConfigDescriptor *)buf)->wTotalLength;
		if (totalLen > sizeof(buf)) {
			totalLen = sizeof(buf);
		}

		bzero(buf, sizeof(buf));
		ret = controller->controlRequest(&fControlEP,
		    kUSBReqDirIn | kUSBReqTypeStandard | kUSBReqRecipDevice,
		    kUSBReqGetDescriptor, (kUSBDescConfig << 8) | idx, 0, totalLen, buf, &actual);
		if (ret != kIOReturnSuccess || actual < sizeof(USBConfigDescriptor)) {
			continue;
		}

		uint8_t thisConfigValue = ((const USBConfigDescriptor *)buf)->bConfigurationValue;
		if (parseCDCConfiguration(buf, actual, &ctrlIfNum, &dataIfNum, &dataAltSetting,
		        &macStringIndex, &notifyAddr, &notifyMax,
		        &bulkInAddr, &bulkInMax, &bulkOutAddr, &bulkOutMax)) {
			configValue = thisConfigValue;
			found = true;
		}
	}

	if (!found) {
		IOLog("DWC2CDCECMTransport: device %u (%04x:%04x) offers no CDC-ECM "
		      "configuration\n", device->address, device->idVendor, device->idProduct);
		return false;
	}

	IOLog("DWC2CDCECMTransport: device %u: CDC-ECM found - config %u, ctrl if %u, "
	      "data if %u alt %u, bulk in %#x/%u out %#x/%u, notify %#x/%u, macStr %u\n",
	    device->address, configValue, ctrlIfNum, dataIfNum, dataAltSetting,
	    bulkInAddr, bulkInMax, bulkOutAddr, bulkOutMax, notifyAddr, notifyMax, macStringIndex);

	IOReturn ret = controller->controlRequest(&fControlEP,
	    kUSBReqDirOut | kUSBReqTypeStandard | kUSBReqRecipDevice,
	    kUSBReqSetConfiguration, configValue, 0, 0, NULL, NULL);
	if (ret != kIOReturnSuccess) {
		IOLog("DWC2CDCECMTransport: SET_CONFIGURATION(%u) failed (%#x)\n", configValue, ret);
		return false;
	}

	// Activate the data interface's alternate setting - required per this
	// file's header comment: QEMU's CDC config describes the data
	// interface's default (alt 0) as a zero-endpoint "off" state, and real
	// CDC-ECM devices commonly do the same.
	ret = controller->controlRequest(&fControlEP,
	    kUSBReqDirOut | kUSBReqTypeStandard | kUSBReqRecipInterface,
	    kUSBReqSetInterface, dataAltSetting, dataIfNum, 0, NULL, NULL);
	if (ret != kIOReturnSuccess) {
		IOLog("DWC2CDCECMTransport: SET_INTERFACE(%u, alt %u) failed (%#x)\n",
		    dataIfNum, dataAltSetting, ret);
		return false;
	}

	fControlInterfaceNumber = ctrlIfNum;

	bzero(&fBulkInEP, sizeof(fBulkInEP));
	fBulkInEP.devAddr = device->address;
	fBulkInEP.epNum = bulkInAddr & 0x0F;
	fBulkInEP.epIsIn = true;
	fBulkInEP.epType = kHCCHAR_EPTYPE_BULK;
	fBulkInEP.maxPacket = bulkInMax;
	fBulkInEP.speed = device->speed;
	fBulkInEP.toggle = kHCPID_DATA0;
	fBulkInEP.splitHubAddr = device->splitHubAddr;
	fBulkInEP.splitHubPort = device->splitHubPort;

	bzero(&fBulkOutEP, sizeof(fBulkOutEP));
	fBulkOutEP.devAddr = device->address;
	fBulkOutEP.epNum = bulkOutAddr & 0x0F;
	fBulkOutEP.epIsIn = false;
	fBulkOutEP.epType = kHCCHAR_EPTYPE_BULK;
	fBulkOutEP.maxPacket = bulkOutMax;
	fBulkOutEP.speed = device->speed;
	fBulkOutEP.toggle = kHCPID_DATA0;
	fBulkOutEP.splitHubAddr = device->splitHubAddr;
	fBulkOutEP.splitHubPort = device->splitHubPort;

	fHaveNotifyEP = (notifyAddr != 0);
	if (fHaveNotifyEP) {
		bzero(&fNotifyEP, sizeof(fNotifyEP));
		fNotifyEP.devAddr = device->address;
		fNotifyEP.epNum = notifyAddr & 0x0F;
		fNotifyEP.epIsIn = true;
		fNotifyEP.epType = kHCCHAR_EPTYPE_INTR;
		fNotifyEP.maxPacket = notifyMax;
		fNotifyEP.speed = device->speed;
		fNotifyEP.toggle = kHCPID_DATA0;
		fNotifyEP.splitHubAddr = device->splitHubAddr;
		fNotifyEP.splitHubPort = device->splitHubPort;
	}

	if (!fetchMACAddressString(macStringIndex)) {
		IOLog("DWC2CDCECMTransport: failed to read MAC address string (index %u)\n",
		    macStringIndex);
		return false;
	}

	return true;
}

uint32_t
DWC2CDCECMTransport::bulkIn(void *buf, uint32_t maxLen)
{
	if (fController == NULL || buf == NULL || maxLen == 0) {
		return 0;
	}

	// Short poll - same rationale as SMSC9514USBTransport::bulkIn()'s
	// header comment: a NAK (nothing queued) is the ordinary idle case and
	// must not hold up TX/notification polling on the shared DWC2 channel.
	static const uint32_t kRxPollTimeoutMS = 20;

	uint32_t actual = 0;
	IOReturn ret = fController->transfer(&fBulkInEP, buf, maxLen, &actual, kRxPollTimeoutMS);
	if (ret != kIOReturnSuccess) {
		return 0;
	}
	return actual;
}

uint32_t
DWC2CDCECMTransport::bulkOut(const void *buf, uint32_t len)
{
	if (fController == NULL) {
		return 0;
	}

	if (len > 0) {
		uint32_t actual = 0;
		IOReturn ret = fController->transfer(&fBulkOutEP, (void *)buf, len, &actual);
		if (ret != kIOReturnSuccess || actual != len) {
			IOLog("DWC2CDCECMTransport: bulk OUT of %u bytes failed (%#x, %u sent)\n",
			    len, ret, actual);
			return 0;
		}
	}

	/*
	 * Trailing ZLP when the frame is empty or an exact multiple of the
	 * bulk endpoint's max packet size - BCM2837DWC2::transfer() sends
	 * exactly `len` bytes and never adds this on its own (its doTransfer()
	 * loop simply stops once `done >= len`), so it has to happen here.
	 * See this file's header comment for the dev-network.c citation
	 * proving QEMU's receive side genuinely requires it.
	 */
	if (fBulkOutEP.maxPacket != 0 && (len == 0 || (len % fBulkOutEP.maxPacket) == 0)) {
		uint8_t zlpDummy = 0;
		uint32_t zlpActual = 0;
		IOReturn zret = fController->transfer(&fBulkOutEP, &zlpDummy, 0, &zlpActual);
		if (zret != kIOReturnSuccess) {
			IOLog("DWC2CDCECMTransport: trailing ZLP after %u-byte frame failed (%#x)\n",
			    len, zret);
			return 0;
		}
	}

	return len;
}

bool
DWC2CDCECMTransport::setPacketFilter(uint16_t filterBitmap)
{
	if (fController == NULL) {
		return false;
	}
	IOReturn ret = fController->controlRequest(&fControlEP,
	    kUSBReqDirOut | kUSBReqTypeClass | kUSBReqRecipInterface,
	    kCDCSetEthernetPacketFilter, filterBitmap, fControlInterfaceNumber, 0, NULL, NULL);
	if (ret != kIOReturnSuccess) {
		// Not fatal - see CDCECMUSBTransport::setPacketFilter()'s header
		// comment and this file's header citation of dev-network.c
		// STALLing this request unconditionally.
		IOLog("DWC2CDCECMTransport: SET_ETHERNET_PACKET_FILTER(%#x) failed "
		      "(%#x) - not fatal, some devices don't implement it\n",
		    filterBitmap, ret);
		return false;
	}
	return true;
}

void
DWC2CDCECMTransport::getMACAddress(uint8_t addr[6]) const
{
	for (int i = 0; i < 6; i++) {
		addr[i] = fMACAddress[i];
	}
}

bool
DWC2CDCECMTransport::pollNotification(bool *linkUp)
{
	if (fController == NULL || !fHaveNotifyEP || linkUp == NULL) {
		return false;
	}

	uint8_t buf[16];
	bzero(buf, sizeof(buf));
	uint32_t actual = 0;
	// Short poll - same rationale as bulkIn(): QEMU NAKs this
	// unconditionally in CDC mode (see this file's header comment), so it
	// must not block the shared channel/workloop for long.
	static const uint32_t kNotifyPollTimeoutMS = 20;
	IOReturn ret = fController->transfer(&fNotifyEP, buf, sizeof(buf), &actual, kNotifyPollTimeoutMS);
	if (ret != kIOReturnSuccess || actual < 8) {
		return false;
	}

	// CDC120 6.3: bmRequestType 0xA1, bNotificationCode 0x00 =
	// NETWORK_CONNECTION, wValue 0 = disconnected / 1 = connected.
	uint8_t bmRequestType = buf[0];
	uint8_t bNotificationCode = buf[1];
	uint16_t wValue = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
	if (bmRequestType != 0xA1 || bNotificationCode != 0x00) {
		return false; // some other notification type - ignore, not an error
	}

	*linkUp = (wValue != 0);
	return true;
}
