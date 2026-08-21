/*
 * USB device enumeration and the hub-protocol subset, for BCM2837DWC2.
 *
 * Split out from BCM2837DWC2.cpp because it is a different layer: nothing
 * here touches a DWC2 register. It is written entirely in terms of
 * controlRequest()/transfer() and the USB 2.0 specification, so it would
 * work unchanged over a different host controller.
 *
 * ========================= WHY HUB SUPPORT IS HERE =======================
 *
 * A plain "enumerate the device on the root port" stack would be enough for
 * a USB Ethernet dongle, and is NOT enough for this board. The Pi 3's
 * SMSC LAN9514 is a combo chip: it presents as a hub on the root port, and
 * its Ethernet function hangs off one of that hub's own internal downstream
 * ports. So reaching Ethernet requires hub descriptor parsing, per-port
 * power, per-port reset and port-status polling - not optional extras, the
 * only path to the wire (docs/usb-support-plan.md's "hub-in-the-path" note,
 * which flagged this precisely so it would not be rediscovered mid-way).
 *
 * Scope is deliberately one hub deep. Cascaded hubs, hot-plug and
 * re-enumeration are not implemented: this board has exactly one hub,
 * soldered down, present at boot.
 */

#include "BCM2837DWC2.h"

// Enough for a full configuration descriptor with several interfaces and
// endpoints; the LAN9514's is far smaller.
#define kConfigBufferSize 256

#pragma mark - Helpers

const DWC2USBDevice *
BCM2837DWC2::deviceAt(uint32_t index) const
{
	if (index >= fNumDevices) {
		return NULL;
	}
	return &fDevices[index];
}

const DWC2USBDevice *
BCM2837DWC2::findDeviceByInterfaceClass(uint8_t interfaceClass) const
{
	for (uint32_t i = 0; i < fNumDevices; i++) {
		if (fDevices[i].inUse && fDevices[i].interfaceClass == interfaceClass) {
			return &fDevices[i];
		}
	}
	return NULL;
}

bool
BCM2837DWC2::makeEndpoint(const DWC2USBDevice *dev, uint8_t epAddr, DWC2Endpoint *ep) const
{
	if (dev == NULL || ep == NULL) {
		return false;
	}

	bzero(ep, sizeof(*ep));
	ep->devAddr = dev->address;
	ep->speed = dev->speed;
	ep->splitHubAddr = dev->splitHubAddr;
	ep->splitHubPort = dev->splitHubPort;
	ep->toggle = kHCPID_DATA0;

	if (epAddr == 0) {
		ep->epNum = 0;
		ep->epIsIn = false; // set per-stage by controlTransfer
		ep->epType = kHCCHAR_EPTYPE_CONTROL;
		ep->maxPacket = dev->maxPacket0;
		return true;
	}

	ep->epNum = epAddr & 0x0F;
	ep->epIsIn = (epAddr & 0x80) != 0;

	if (epAddr == dev->bulkInEp) {
		ep->epType = kHCCHAR_EPTYPE_BULK;
		ep->maxPacket = dev->bulkInMax;
	} else if (epAddr == dev->bulkOutEp) {
		ep->epType = kHCCHAR_EPTYPE_BULK;
		ep->maxPacket = dev->bulkOutMax;
	} else if (epAddr == dev->intrInEp) {
		ep->epType = kHCCHAR_EPTYPE_INTR;
		ep->maxPacket = dev->intrInMax;
	} else {
		return false;
	}

	return ep->maxPacket != 0;
}

#pragma mark - Configuration descriptor parsing

/*
 * Walk the concatenated descriptor blob a GET_DESCRIPTOR(CONFIGURATION)
 * returns and pick out the endpoints this stack can use.
 *
 * Scans ALL interfaces, not just the first. An earlier version stopped at
 * interface 0, which a real boot test immediately proved wrong: QEMU's
 * usb-net is an RNDIS device whose interface 0 (class 2, Communications)
 * carries only an interrupt endpoint, with the bulk IN/OUT pair living on
 * interface 1 (class 10, CDC Data). That version reported "bulk in 0/0 out
 * 0/0" for it - a device with no usable data path, silently. The LAN9514
 * happens to be single-interface vendor-specific so it would not have been
 * caught there, which is exactly why it was worth fixing on the evidence
 * rather than after.
 *
 * The interface whose class is reported as the device's is the first one
 * that actually carries a bulk IN+OUT pair (that is the data interface, and
 * the one a consumer cares about); failing that, the first interface. Only
 * alternate setting 0 is considered - nothing here selects alt settings, so
 * taking endpoints from a non-default one would describe a configuration
 * that was never activated.
 *
 * The walk is driven entirely by each descriptor's own bLength, and treats
 * a zero bLength as fatal rather than stepping over it - a malformed or
 * truncated blob would otherwise spin here forever.
 */
bool
BCM2837DWC2::parseConfiguration(DWC2USBDevice *dev, const uint8_t *cfg, uint32_t cfgLen)
{
	if (cfgLen < sizeof(USBConfigDescriptor)) {
		return false;
	}

	const USBConfigDescriptor *cd = (const USBConfigDescriptor *)cfg;
	dev->configValue = cd->bConfigurationValue;

	uint32_t offset = cd->bLength;
	bool haveInterface = false;
	bool inAltSetting = false;   // inside a non-default alt setting: skip its endpoints
	bool haveBulkPair = false;   // a data interface has already been committed
	uint8_t firstInterfaceClass = 0;
	uint8_t currentInterfaceClass = 0;

	while (offset + 2 <= cfgLen) {
		uint8_t bLength = cfg[offset];
		uint8_t bType = cfg[offset + 1];

		if (bLength == 0 || offset + bLength > cfgLen) {
			IOLog("BCM2837DWC2: malformed descriptor at offset %u "
			      "(bLength=%u, total=%u)\n", offset, bLength, cfgLen);
			break;
		}

		if (bType == kUSBDescInterface && bLength >= sizeof(USBInterfaceDescriptor)) {
			const USBInterfaceDescriptor *id =
			    (const USBInterfaceDescriptor *)(cfg + offset);

			inAltSetting = (id->bAlternateSetting != 0);
			if (!inAltSetting) {
				currentInterfaceClass = id->bInterfaceClass;
				if (!haveInterface) {
					firstInterfaceClass = id->bInterfaceClass;
					haveInterface = true;
				}
			}
		} else if (bType == kUSBDescEndpoint && bLength >= sizeof(USBEndpointDescriptor) &&
		           haveInterface && !inAltSetting) {
			const USBEndpointDescriptor *ed =
			    (const USBEndpointDescriptor *)(cfg + offset);
			uint8_t xferType = ed->bmAttributes & 0x03;
			bool isIn = (ed->bEndpointAddress & 0x80) != 0;
			// wMaxPacketSize's low 11 bits are the size; bits 11-12 are
			// the high-bandwidth multiplier, which only applies to
			// periodic high-speed endpoints and is not used here.
			uint16_t mps = ed->wMaxPacketSize & 0x07FF;

			if (xferType == 0x02 && !haveBulkPair) { // bulk
				if (isIn && dev->bulkInEp == 0) {
					dev->bulkInEp = ed->bEndpointAddress;
					dev->bulkInMax = mps;
				} else if (!isIn && dev->bulkOutEp == 0) {
					dev->bulkOutEp = ed->bEndpointAddress;
					dev->bulkOutMax = mps;
				}
				if (dev->bulkInEp != 0 && dev->bulkOutEp != 0) {
					// This is the data interface; stop taking bulk
					// endpoints from any later one, and report its
					// class rather than interface 0's.
					haveBulkPair = true;
					dev->interfaceClass = currentInterfaceClass;
				}
			} else if (xferType == 0x03) { // interrupt
				if (isIn && dev->intrInEp == 0) {
					dev->intrInEp = ed->bEndpointAddress;
					dev->intrInMax = mps;
				}
			}
		}

		offset += bLength;
	}

	if (!haveBulkPair) {
		dev->interfaceClass = firstInterfaceClass;
	}

	return haveInterface;
}

#pragma mark - Device enumeration

/*
 * Address and configure whatever device has just been reset and is
 * therefore answering at address 0. Follows the standard USB 2.0 chapter-9
 * sequence.
 */
bool
BCM2837DWC2::enumerateDevice(uint8_t speed, uint8_t parentHubAddr, uint8_t parentPort,
                             uint8_t splitHubAddr, uint8_t splitHubPort,
                             DWC2USBDevice **outDev)
{
	if (fNumDevices >= kDWC2MaxDevices) {
		IOLog("BCM2837DWC2: device table full\n");
		return false;
	}
	if (fNextAddress > 127) {
		IOLog("BCM2837DWC2: out of USB addresses\n");
		return false;
	}

	uint8_t buf[kConfigBufferSize];
	uint32_t actual = 0;

	// A device answering at address 0 still needs a plausible endpoint-0
	// max packet size to talk to at all. 8 bytes is the minimum every
	// full/low-speed device must support and is legal for high speed too,
	// so the first 8-byte fetch below works regardless - and its 8th byte
	// (bMaxPacketSize0) is exactly what tells us the real value.
	DWC2Endpoint ep;
	bzero(&ep, sizeof(ep));
	ep.devAddr = 0;
	ep.epNum = 0;
	ep.epType = kHCCHAR_EPTYPE_CONTROL;
	ep.maxPacket = 8;
	ep.speed = speed;
	ep.splitHubAddr = splitHubAddr;
	ep.splitHubPort = splitHubPort;

	bzero(buf, sizeof(buf));
	IOReturn ret = controlRequest(&ep,
	    kUSBReqDirIn | kUSBReqTypeStandard | kUSBReqRecipDevice,
	    kUSBReqGetDescriptor, (kUSBDescDevice << 8) | 0, 0, 8, buf, &actual);
	if (ret != kIOReturnSuccess || actual < 8) {
		IOLog("BCM2837DWC2: initial GET_DESCRIPTOR failed (%#x, %u bytes)\n",
		    ret, actual);
		return false;
	}

	uint8_t maxPacket0 = buf[7];
	if (maxPacket0 != 8 && maxPacket0 != 16 && maxPacket0 != 32 && maxPacket0 != 64) {
		IOLog("BCM2837DWC2: implausible bMaxPacketSize0 %u, assuming 8\n", maxPacket0);
		maxPacket0 = 8;
	}
	ep.maxPacket = maxPacket0;

	uint8_t newAddress = fNextAddress;
	ret = controlRequest(&ep,
	    kUSBReqDirOut | kUSBReqTypeStandard | kUSBReqRecipDevice,
	    kUSBReqSetAddress, newAddress, 0, 0, NULL, NULL);
	if (ret != kIOReturnSuccess) {
		IOLog("BCM2837DWC2: SET_ADDRESS(%u) failed (%#x)\n", newAddress, ret);
		return false;
	}
	fNextAddress++;

	// USB 2.0 spec 9.2.6.3: the device gets 2ms to start answering at its
	// new address. Nothing may be sent to it in that window.
	IOSleep(5);

	ep.devAddr = newAddress;

	// Now the full 18-byte device descriptor, at the real address and
	// real endpoint-0 size.
	bzero(buf, sizeof(buf));
	ret = controlRequest(&ep,
	    kUSBReqDirIn | kUSBReqTypeStandard | kUSBReqRecipDevice,
	    kUSBReqGetDescriptor, (kUSBDescDevice << 8) | 0, 0,
	    sizeof(USBDeviceDescriptor), buf, &actual);
	if (ret != kIOReturnSuccess || actual < sizeof(USBDeviceDescriptor)) {
		IOLog("BCM2837DWC2: full GET_DESCRIPTOR failed (%#x, %u bytes)\n", ret, actual);
		return false;
	}

	const USBDeviceDescriptor *dd = (const USBDeviceDescriptor *)buf;

	DWC2USBDevice *dev = &fDevices[fNumDevices];
	bzero(dev, sizeof(*dev));
	dev->inUse = true;
	dev->address = newAddress;
	dev->speed = speed;
	dev->maxPacket0 = maxPacket0;
	dev->idVendor = dd->idVendor;
	dev->idProduct = dd->idProduct;
	dev->deviceClass = dd->bDeviceClass;
	dev->splitHubAddr = splitHubAddr;
	dev->splitHubPort = splitHubPort;
	dev->parentHubAddr = parentHubAddr;
	dev->parentPort = parentPort;

	IOLog("BCM2837DWC2: device %u: %04x:%04x class %u, ep0 mps %u, %s-speed"
	      " (hub %u port %u)\n",
	    newAddress, dd->idVendor, dd->idProduct, dd->bDeviceClass, maxPacket0,
	    (speed == kUSBSpeedHigh) ? "high" : (speed == kUSBSpeedFull) ? "full" : "low",
	    parentHubAddr, parentPort);

	// Configuration descriptor: header first, to learn wTotalLength, then
	// the whole thing.
	bzero(buf, sizeof(buf));
	ret = controlRequest(&ep,
	    kUSBReqDirIn | kUSBReqTypeStandard | kUSBReqRecipDevice,
	    kUSBReqGetDescriptor, (kUSBDescConfig << 8) | 0, 0,
	    sizeof(USBConfigDescriptor), buf, &actual);
	if (ret != kIOReturnSuccess || actual < sizeof(USBConfigDescriptor)) {
		IOLog("BCM2837DWC2: GET_DESCRIPTOR(config header) failed (%#x)\n", ret);
		return false;
	}

	uint16_t totalLen = ((const USBConfigDescriptor *)buf)->wTotalLength;
	if (totalLen > kConfigBufferSize) {
		IOLog("BCM2837DWC2: config descriptor %u bytes, truncating to %u\n",
		    totalLen, kConfigBufferSize);
		totalLen = kConfigBufferSize;
	}

	bzero(buf, sizeof(buf));
	ret = controlRequest(&ep,
	    kUSBReqDirIn | kUSBReqTypeStandard | kUSBReqRecipDevice,
	    kUSBReqGetDescriptor, (kUSBDescConfig << 8) | 0, 0, totalLen, buf, &actual);
	if (ret != kIOReturnSuccess || actual < sizeof(USBConfigDescriptor)) {
		IOLog("BCM2837DWC2: GET_DESCRIPTOR(config) failed (%#x)\n", ret);
		return false;
	}

	if (!parseConfiguration(dev, buf, actual)) {
		IOLog("BCM2837DWC2: no usable interface in config descriptor\n");
		return false;
	}

	ret = controlRequest(&ep,
	    kUSBReqDirOut | kUSBReqTypeStandard | kUSBReqRecipDevice,
	    kUSBReqSetConfiguration, dev->configValue, 0, 0, NULL, NULL);
	if (ret != kIOReturnSuccess) {
		IOLog("BCM2837DWC2: SET_CONFIGURATION(%u) failed (%#x)\n",
		    dev->configValue, ret);
		return false;
	}

	IOLog("BCM2837DWC2:   configured %u, ifclass %u, bulk in %#x/%u out %#x/%u,"
	      " intr in %#x/%u\n",
	    dev->configValue, dev->interfaceClass,
	    dev->bulkInEp, dev->bulkInMax, dev->bulkOutEp, dev->bulkOutMax,
	    dev->intrInEp, dev->intrInMax);

	fNumDevices++;
	if (outDev != NULL) {
		*outDev = dev;
	}
	return true;
}

#pragma mark - Hub protocol

bool
BCM2837DWC2::hubGetDescriptor(DWC2USBDevice *hub, USBHubDescriptor *desc)
{
	DWC2Endpoint ep;
	if (!makeEndpoint(hub, 0, &ep)) {
		return false;
	}

	uint32_t actual = 0;
	bzero(desc, sizeof(*desc));
	IOReturn ret = controlRequest(&ep,
	    kUSBReqDirIn | kUSBReqTypeClass | kUSBReqRecipDevice,
	    kUSBReqGetDescriptor, (kUSBDescHub << 8) | 0, 0,
	    sizeof(USBHubDescriptor), desc, &actual);

	// A hub descriptor is variable length; anything short of the fixed
	// header is unusable, but a hub returning only the header is fine.
	if (ret != kIOReturnSuccess || actual < 7) {
		IOLog("BCM2837DWC2: GET_HUB_DESCRIPTOR failed (%#x, %u bytes)\n", ret, actual);
		return false;
	}
	return true;
}

bool
BCM2837DWC2::hubSetPortFeature(DWC2USBDevice *hub, uint8_t port, uint16_t feature)
{
	DWC2Endpoint ep;
	if (!makeEndpoint(hub, 0, &ep)) {
		return false;
	}
	return controlRequest(&ep,
	    kUSBReqDirOut | kUSBReqTypeClass | kUSBReqRecipOther,
	    kUSBReqSetFeature, feature, port, 0, NULL, NULL) == kIOReturnSuccess;
}

bool
BCM2837DWC2::hubClearPortFeature(DWC2USBDevice *hub, uint8_t port, uint16_t feature)
{
	DWC2Endpoint ep;
	if (!makeEndpoint(hub, 0, &ep)) {
		return false;
	}
	return controlRequest(&ep,
	    kUSBReqDirOut | kUSBReqTypeClass | kUSBReqRecipOther,
	    kUSBReqClearFeature, feature, port, 0, NULL, NULL) == kIOReturnSuccess;
}

bool
BCM2837DWC2::hubGetPortStatus(DWC2USBDevice *hub, uint8_t port,
                              uint16_t *status, uint16_t *change)
{
	DWC2Endpoint ep;
	if (!makeEndpoint(hub, 0, &ep)) {
		return false;
	}

	uint8_t buf[4];
	uint32_t actual = 0;
	bzero(buf, sizeof(buf));

	IOReturn ret = controlRequest(&ep,
	    kUSBReqDirIn | kUSBReqTypeClass | kUSBReqRecipOther,
	    kUSBReqGetStatus, 0, port, 4, buf, &actual);
	if (ret != kIOReturnSuccess || actual < 4) {
		return false;
	}

	// wPortStatus then wPortChange, both little-endian on the wire.
	*status = (uint16_t)(buf[0] | (buf[1] << 8));
	*change = (uint16_t)(buf[2] | (buf[3] << 8));
	return true;
}

/*
 * Power up every downstream port, then reset and enumerate whatever is
 * attached. This is the step that reaches the LAN9514's Ethernet function.
 */
bool
BCM2837DWC2::hubEnumeratePorts(DWC2USBDevice *hub)
{
	USBHubDescriptor desc;
	if (!hubGetDescriptor(hub, &desc)) {
		return false;
	}

	uint8_t numPorts = desc.bNbrPorts;
	// bPwrOn2PwrGood is in 2ms units (USB 2.0 spec 11.23.2.1).
	uint32_t powerGoodMS = (uint32_t)desc.bPwrOn2PwrGood * 2;
	if (powerGoodMS < 20) {
		powerGoodMS = 20;
	}

	IOLog("BCM2837DWC2: hub at address %u has %u ports (power-good %ums)\n",
	    hub->address, numPorts, powerGoodMS);

	if (numPorts == 0 || numPorts > 15) {
		IOLog("BCM2837DWC2: implausible hub port count %u\n", numPorts);
		return false;
	}

	for (uint8_t port = 1; port <= numPorts; port++) {
		if (!hubSetPortFeature(hub, port, kHubFeatPortPower)) {
			IOLog("BCM2837DWC2: hub port %u power-on failed\n", port);
			continue;
		}
	}
	IOSleep(powerGoodMS);

	for (uint8_t port = 1; port <= numPorts; port++) {
		uint16_t status = 0, change = 0;

		if (!hubGetPortStatus(hub, port, &status, &change)) {
			IOLog("BCM2837DWC2: hub port %u status read failed\n", port);
			continue;
		}
		if (!(status & kPortStatusConnection)) {
			continue; // nothing plugged into this port
		}

		// Acknowledge the connect event before resetting, so the change
		// bit we look at after the reset is the reset's own.
		if (change & kPortStatusConnection) {
			hubClearPortFeature(hub, port, kHubFeatCPortConnection);
		}

		// USB 2.0 spec 7.1.7.3: let the connection settle before reset.
		IOSleep(100);

		if (!hubSetPortFeature(hub, port, kHubFeatPortReset)) {
			IOLog("BCM2837DWC2: hub port %u reset failed\n", port);
			continue;
		}

		// The hub drives reset itself and reports completion via
		// C_PORT_RESET; poll rather than assuming a fixed duration.
		bool resetDone = false;
		for (int i = 0; i < 20; i++) {
			IOSleep(10);
			if (!hubGetPortStatus(hub, port, &status, &change)) {
				break;
			}
			if (change & (1U << kHubFeatCPortReset)) {
				resetDone = true;
				break;
			}
			if (!(status & kPortStatusReset) && (status & kPortStatusEnable)) {
				resetDone = true;
				break;
			}
		}
		if (!resetDone) {
			IOLog("BCM2837DWC2: hub port %u reset did not complete "
			      "(status %#x change %#x)\n", port, status, change);
			continue;
		}

		hubClearPortFeature(hub, port, kHubFeatCPortReset);
		// Reset recovery (USB 2.0 spec 7.1.7.5).
		IOSleep(20);

		if (!hubGetPortStatus(hub, port, &status, &change)) {
			continue;
		}
		if (!(status & kPortStatusEnable)) {
			IOLog("BCM2837DWC2: hub port %u not enabled after reset (%#x)\n",
			    port, status);
			continue;
		}

		uint8_t speed = kUSBSpeedFull;
		if (status & kPortStatusHighSpeed) {
			speed = kUSBSpeedHigh;
		} else if (status & kPortStatusLowSpeed) {
			speed = kUSBSpeedLow;
		}

		/*
		 * SPLIT transactions are needed only when this hub runs at high
		 * speed but the device below it does not - then the hub has to
		 * translate, and the host must address it explicitly. A
		 * full-speed hub relays at its own speed and needs no splits.
		 */
		uint8_t splitHubAddr = 0, splitHubPort = 0;
		if (hub->speed == kUSBSpeedHigh && speed != kUSBSpeedHigh) {
			splitHubAddr = hub->address;
			splitHubPort = port;
		}

		DWC2USBDevice *child = NULL;
		if (!enumerateDevice(speed, hub->address, port,
		        splitHubAddr, splitHubPort, &child)) {
			IOLog("BCM2837DWC2: enumeration failed on hub port %u\n", port);
			continue;
		}
	}

	return true;
}

#pragma mark - Bus walk

uint32_t
BCM2837DWC2::enumerateBus(void)
{
	fNumDevices = 0;
	fNextAddress = 1;

	if (!fRootConnected) {
		IOLog("BCM2837DWC2: enumerateBus: nothing on the root port\n");
		return 0;
	}

	DWC2USBDevice *root = NULL;
	// Directly on the root port: no hub in the path, so no split.
	if (!enumerateDevice(fRootSpeed, 0, 0, 0, 0, &root)) {
		IOLog("BCM2837DWC2: root-port enumeration failed\n");
		return 0;
	}

	// Hub class is 9 (USB 2.0 spec 11.23). On this board the root-port
	// device is expected to BE a hub - that is what the LAN9514 presents.
	if (root->deviceClass == 9) {
		if (!hubEnumeratePorts(root)) {
			IOLog("BCM2837DWC2: hub enumeration failed\n");
		}
	} else {
		IOLog("BCM2837DWC2: root-port device is not a hub (class %u); "
		      "on real hardware the LAN9514 should be\n", root->deviceClass);
	}

	IOLog("BCM2837DWC2: enumeration complete, %u device(s)\n", fNumDevices);
	return fNumDevices;
}
