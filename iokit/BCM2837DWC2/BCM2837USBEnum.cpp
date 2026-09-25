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
 * Cascaded hubs are walked (hubEnumeratePorts() recurses, up to USB's five
 * tiers), because devices plugged into the Pi's USB ports can themselves be
 * hubs - an Apple wired keyboard is one. Per-device re-enumeration exists
 * (reenumerateDevice(), DAR-373) and so does whole-controller recovery
 * (DAR-352). Hot-plug does not (DAR-353): the bus is walked at boot and on
 * recovery only.
 */

#include "BCM2837DWC2.h"
#include "SMSC9514USBTransport.h"

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

// The table entry at `address`: in use only, or also one detached for
// re-enumeration (its slot keeps the address).
DWC2USBDevice *
BCM2837DWC2::slotForAddress(uint8_t address, bool includeDetached)
{
	for (uint32_t i = 0; i < fNumDevices; i++) {
		if (fDevices[i].address == address && (includeDetached || fDevices[i].inUse)) {
			return &fDevices[i];
		}
	}
	return NULL;
}

DWC2USBDevice *
BCM2837DWC2::deviceByAddress(uint8_t address)
{
	return slotForAddress(address, false);
}

const DWC2USBDevice *
BCM2837DWC2::findDevice(uint8_t address) const
{
	return const_cast<BCM2837DWC2 *>(this)->slotForAddress(address, false);
}

/*
 * The boot-time publish rules (startUSBHostController()), shared with
 * re-enumeration so a device that comes back different is filtered the same
 * way.
 */
bool
BCM2837DWC2::publishable(const DWC2USBDevice *dev) const
{
	if (dev == NULL || !dev->inUse) {
		return false;
	}
	if (dev->deviceClass == 9 /* USB hub class */) {
		IOLog("BCM2837DWC2: skipping hub at address %u (no hub driver "
		      "vendored; enumerateBus already walked its ports)\n", dev->address);
		return false;
	}
	if (fEthernetDriver != NULL &&
	    SMSC9514USBTransport::findEthernetFunction(this) == dev) {
		// Already hand-wired to SMSC9514Ethernet. Publishing a nub for it
		// too would let a second driver open the same endpoints.
		IOLog("BCM2837DWC2: address %u already claimed by SMSC9514Ethernet\n",
		    dev->address);
		return false;
	}
	return true;
}

IOReturn
BCM2837DWC2::detachDevice(uint8_t address, uint32_t recoveryGen)
{
	IOLockLock(fEnumLock);
	IOReturn ret = kIOReturnSuccess;
	DWC2USBDevice *dev = deviceByAddress(address);
	if (recoveryGen != fRecoveryGen) {
		// A controller recovery ran since the caller started: the table was
		// rebuilt and this address may now belong to a new device.
		ret = kIOReturnAborted;
	} else if (dev == NULL) {
		ret = kIOReturnNoDevice;
	} else {
		dev->inUse = false;
	}
	IOLockUnlock(fEnumLock);
	return ret;
}

IOReturn
BCM2837DWC2::reenumerateDevice(uint8_t address, bool extraResetDelay,
    uint32_t recoveryGen, const DWC2USBDevice **devOut)
{
	*devOut = NULL;

	/*
	 * One device at address 0 at a time, like AppleUSBHub's dev-zero lock
	 * (AppleUSBHubPort::AddDevice, AppleUSBHubPort.cpp:728-754). Two
	 * re-enumerations running together would both reset a port and both
	 * leave a device answering at address 0.
	 */
	IOLockLock(fEnumLock);

	// Looked up under the lock, and only if no controller recovery ran
	// since the caller began (see fRecoveryGen): recovery rebuilds the table
	// under this same lock.
	if (recoveryGen != fRecoveryGen) {
		IOLockUnlock(fEnumLock);
		return kIOReturnAborted;
	}
	DWC2USBDevice *dev = slotForAddress(address, true);
	if (dev == NULL) {
		IOLockUnlock(fEnumLock);
		return kIOReturnNoDevice;
	}
	uint8_t parentHubAddr = dev->parentHubAddr;
	uint8_t parentPort = dev->parentPort;
	dev->inUse = false;

	IOReturn ret = kIOReturnNotResponding;
	/*
	 * Retry rather than give up on the first transient error, as
	 * AppleUSBHubPort::AddDevice() does (its reset loop,
	 * AppleUSBHubPort.cpp:855-869; and up to kMaxDevZeroRetries = 4
	 * address-0 descriptor reads, :63, :3800). The count of three here is
	 * this driver's own, the same one doTransfer() uses for XACTERR; it is
	 * not Apple's number.
	 */
	for (int attempt = 0; attempt < 3 && ret != kIOReturnSuccess; attempt++) {
		uint8_t speed = kUSBSpeedFull;
		uint8_t splitHubAddr = 0, splitHubPort = 0;
		if (parentHubAddr == 0) {
			// On the root port. Nothing to do if nothing is connected
			// (AppleUSBHubPort::AddDevice checks the same,
			// AppleUSBHubPort.cpp:741-751). Resetting the root port resets
			// the whole bus, so channel 0's synchronous transfers are held
			// off for its duration (portReset() sleeps ~90 ms).
			if (!(regRead(kDWC2_HPRT0) & kHPRT0_PRTCONNSTS)) {
				ret = kIOReturnNoDevice;
				break;
			}
			IOLockLock(fTransferLock);
			bool ok = portReset();
			IOLockUnlock(fTransferLock);
			if (!ok) {
				continue;
			}
			speed = fRootSpeed;
		} else {
			DWC2USBDevice *hub = deviceByAddress(parentHubAddr);
			if (hub == NULL) {
				ret = kIOReturnNoDevice;
				break;
			}
			if (!resetHubPort(hub, parentPort, &speed, &splitHubAddr, &splitHubPort)) {
				continue;
			}
		}
		if (extraResetDelay) {
			IOSleep(100);
		}

		DWC2USBDevice *out = NULL;
		if (enumerateDevice(speed, parentHubAddr, parentPort, splitHubAddr, splitHubPort,
		    &out, dev)) {
			*devOut = out;
			ret = kIOReturnSuccess;
		} else {
			IOLog("BCM2837DWC2: re-enumeration of device %u (hub %u port %u), "
			    "attempt %d/3 failed\n", address, parentHubAddr, parentPort, attempt + 1);
		}
	}
	if (ret != kIOReturnSuccess && parentHubAddr != 0) {
		/*
		 * The device may be left answering at address 0 on an enabled port,
		 * where it would collide with the next device reset. Disable the
		 * port before releasing the lock, as AppleUSBHubPort::AddDevice()
		 * does on failure (AppleUSBHubPort.cpp:880-889).
		 */
		DWC2USBDevice *hub = deviceByAddress(parentHubAddr);
		if (hub != NULL) {
			hubClearPortFeature(hub, parentPort, kHubFeatPortEnable);
		}
	}
	IOLockUnlock(fEnumLock);
	return ret;
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

/*
 * An endpoint enumeration did not record (it keeps one of each kind, from
 * the first interface) - e.g. the Apple keyboard's second HID interface,
 * ep 0x82. The family hands the UIM the endpoint's type and wMaxPacketSize
 * from the device's own descriptor, and that is all Apple's UIMs build an
 * endpoint from (AppleUSBEHCI_UIM.cpp:2549 UIMCreateInterruptEndpoint() /
 * :1832 UIMCreateBulkEndpoint() take them as arguments).
 */
bool
BCM2837DWC2::makeEndpointFromDescriptor(const DWC2USBDevice *dev, uint8_t epAddr,
    uint32_t epType, uint16_t maxPacket, DWC2Endpoint *ep) const
{
	if (dev == NULL || ep == NULL || epAddr == 0 || maxPacket == 0 ||
	    (epType != kHCCHAR_EPTYPE_BULK && epType != kHCCHAR_EPTYPE_INTR)) {
		return false;
	}
	bzero(ep, sizeof(*ep));
	ep->devAddr = dev->address;
	ep->speed = dev->speed;
	ep->splitHubAddr = dev->splitHubAddr;
	ep->splitHubPort = dev->splitHubPort;
	ep->toggle = kHCPID_DATA0;
	ep->epNum = epAddr & 0x0F;
	ep->epIsIn = (epAddr & 0x80) != 0;
	ep->epType = epType;
	ep->maxPacket = maxPacket;
	return true;
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
                             DWC2USBDevice **outDev, DWC2USBDevice *reuse)
{
	if (reuse == NULL && fNumDevices >= kDWC2MaxDevices) {
		IOLog("BCM2837DWC2: device table full\n");
		return false;
	}
	if (reuse == NULL && fReservedAddress != 0 && fNextAddress == fReservedAddress) {
		fNextAddress++;     // the UIM's root-hub simulation owns it
	}
	if (reuse == NULL && fNextAddress > 127) {
		IOLog("BCM2837DWC2: out of USB addresses\n");
		return false;
	}

	uint8_t buf[kConfigBufferSize];
	uint32_t actual = 0;

	/*
	 * Endpoint-0 max packet size for a device still at address 0.
	 *
	 * This is SPEED-DEPENDENT and getting it wrong is not cosmetic. USB
	 * 2.0 spec 5.5.3 fixes it outright for two of the three speeds: a
	 * high-speed control endpoint MUST be 64 bytes, and a low-speed one
	 * MUST be 8. Only full-speed genuinely varies (8/16/32/64), and only
	 * full-speed therefore needs the 8-byte discovery read below to learn
	 * it. u-boot - which drives this exact silicon in this project's own
	 * boot chain - does exactly this split in usb_setup_descriptor()
	 * (common/usb.c): 8 only for USB_SPEED_LOW, 64 otherwise, and it
	 * performs the discovery read only `if (do_read && dev->speed ==
	 * USB_SPEED_FULL)`.
	 *
	 * This code previously used 8 for every speed. On real hardware that
	 * programmed HCCHAR.MPS=8 for the LAN9514's high-speed Ethernet
	 * function, and every control transfer to it failed with XACTERR -
	 * byte-for-byte identical register state on all three retries,
	 * deterministic rather than transient. (The root-port hub tolerated
	 * the same wrong value, which is why enumeration got as far as it
	 * did and why this looked like a hub-specific problem at first.)
	 */
	uint8_t maxPacket0 = (speed == kUSBSpeedLow) ? 8 : 64;

	DWC2Endpoint ep;
	bzero(&ep, sizeof(ep));
	ep.devAddr = 0;
	ep.epNum = 0;
	ep.epType = kHCCHAR_EPTYPE_CONTROL;
	ep.maxPacket = maxPacket0;
	ep.speed = speed;
	ep.splitHubAddr = splitHubAddr;
	ep.splitHubPort = splitHubPort;

	IOReturn ret;

	if (speed == kUSBSpeedFull) {
		// Full speed is the only case where bMaxPacketSize0 is not
		// already pinned by the spec, so it is the only case worth a
		// round trip to discover. Byte 7 of the device descriptor is
		// bMaxPacketSize0.
		bzero(buf, sizeof(buf));
		ret = controlRequest(&ep,
		    kUSBReqDirIn | kUSBReqTypeStandard | kUSBReqRecipDevice,
		    kUSBReqGetDescriptor, (kUSBDescDevice << 8) | 0, 0, 8, buf, &actual);
		if (ret != kIOReturnSuccess || actual < 8) {
			IOLog("BCM2837DWC2: initial GET_DESCRIPTOR failed (%#x, %u bytes)\n",
			    ret, actual);
			return false;
		}

		maxPacket0 = buf[7];
		if (maxPacket0 != 8 && maxPacket0 != 16 &&
		    maxPacket0 != 32 && maxPacket0 != 64) {
			IOLog("BCM2837DWC2: implausible bMaxPacketSize0 %u, assuming 8\n",
			    maxPacket0);
			maxPacket0 = 8;
		}
		ep.maxPacket = maxPacket0;
	}

	uint8_t newAddress = (reuse != NULL) ? reuse->address : fNextAddress;
	ret = controlRequest(&ep,
	    kUSBReqDirOut | kUSBReqTypeStandard | kUSBReqRecipDevice,
	    kUSBReqSetAddress, newAddress, 0, 0, NULL, NULL);
	if (ret != kIOReturnSuccess) {
		IOLog("BCM2837DWC2: SET_ADDRESS(%u) failed (%#x)\n", newAddress, ret);
		return false;
	}
	if (reuse == NULL) {
		fNextAddress++;
	}

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

	// Filled in completely before it is marked in use (at the end), so a
	// reader never sees a half-written entry as a device.
	DWC2USBDevice *dev = (reuse != NULL) ? reuse : &fDevices[fNumDevices];
	bzero(dev, sizeof(*dev));
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

	dev->inUse = true;
	if (reuse == NULL) {
		fNumDevices++;
	}
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
 * Reset one hub port and report what came up on it: its speed, and the
 * SPLIT routing a device there needs. Split out of hubEnumeratePorts() for
 * DAR-373 so re-enumeration resets a port exactly as first enumeration does.
 * Returns false if the port did not come back enabled.
 */
bool
BCM2837DWC2::resetHubPort(DWC2USBDevice *hub, uint8_t port, uint8_t *speedOut,
    uint8_t *splitHubAddrOut, uint8_t *splitHubPortOut)
{
	uint16_t status = 0, change = 0;

	/*
	 * DAR-373: this also resets a port that is already enabled
	 * (re-enumeration), not only one that has just been connected. So:
	 * nothing to do if nothing is connected (AppleUSBHubPort::AddDevice
	 * checks the same before resetting); clear any stale C_PORT_RESET /
	 * C_PORT_ENABLE so the change bits read after the reset are this
	 * reset's; and remember whether the port was already enabled, because
	 * then "RESET clear and ENABLE set" below proves nothing.
	 */
	if (!hubGetPortStatus(hub, port, &status, &change)) {
		return false;
	}
	if (!(status & kPortStatusConnection)) {
		IOLog("BCM2837DWC2: hub port %u: nothing connected, not resetting\n", port);
		return false;
	}
	if (change & kPortChangeReset) {
		hubClearPortFeature(hub, port, kHubFeatCPortReset);
	}
	if (change & kPortChangeEnable) {
		hubClearPortFeature(hub, port, kHubFeatCPortEnable);
	}
	const bool wasEnabled = (status & kPortStatusEnable) != 0;

	// USB 2.0 spec 7.1.7.3: let the connection settle before reset.
	IOSleep(100);

	if (!hubSetPortFeature(hub, port, kHubFeatPortReset)) {
		IOLog("BCM2837DWC2: hub port %u reset failed\n", port);
		return false;
	}

	// The hub drives reset itself and reports completion via
	// C_PORT_RESET; poll rather than assuming a fixed duration.
	bool resetDone = false;
	for (int i = 0; i < 20; i++) {
		IOSleep(10);
		if (!hubGetPortStatus(hub, port, &status, &change)) {
			break;
		}
		if (change & kPortChangeReset) {
			resetDone = true;
			break;
		}
		if (!wasEnabled && !(status & kPortStatusReset) && (status & kPortStatusEnable)) {
			resetDone = true;
			break;
		}
	}
	if (!resetDone) {
		IOLog("BCM2837DWC2: hub port %u reset did not complete "
		      "(status %#x change %#x)\n", port, status, change);
		return false;
	}

	hubClearPortFeature(hub, port, kHubFeatCPortReset);
	/*
	 * Reset recovery. USB 2.0 spec 7.1.7.5 (TRSTRCY) only mandates
	 * 10ms, but real Linux (drivers/usb/core/hub.c, hub_port_reset())
	 * unconditionally waits 50ms for every hub port it resets, root or
	 * downstream, quirk table or not: `reset_recovery_time = 10 + 40`.
	 * The +40ms is real-world margin Linux itself found the bare spec
	 * minimum insufficient for, not something specific to any one
	 * device. This was 20ms - under half that - and a real LAN9514's
	 * downstream Ethernet function (0424:9514) failed every control
	 * transfer to it, identically, 100% of the time
	 * (docs/backlog.md's real-hardware log: XACTERR, byte-for-byte the
	 * same register state on all 3 retries - deterministic, not the
	 * transient noise a retry loop can paper over). The root port's
	 * own 30ms recovery (BCM2837DWC2::portReset()) is left alone here:
	 * it already enumerates this exact hub successfully, so there is
	 * no evidence it needs raising, and this fix is scoped to what
	 * failed.
	 */
	IOSleep(50);

	if (!hubGetPortStatus(hub, port, &status, &change)) {
		return false;
	}
	if (!(status & kPortStatusEnable)) {
		IOLog("BCM2837DWC2: hub port %u not enabled after reset (%#x)\n",
		    port, status);
		return false;
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
	 * translate, and the host must address it explicitly.
	 *
	 * A full-speed hub cannot translate. It relays at its own speed, so a
	 * device below it shares that hub's own routing: if the full-speed hub
	 * is itself reached through a high-speed hub's transaction translator,
	 * so is everything under it, via the same TT hub and port (USB 2.0
	 * 11.14, transaction translator). Matters now that enumeration walks
	 * hubs behind hubs (hubEnumeratePorts()).
	 */
	uint8_t splitHubAddr = 0, splitHubPort = 0;
	if (hub->speed == kUSBSpeedHigh && speed != kUSBSpeedHigh) {
		splitHubAddr = hub->address;
		splitHubPort = port;
	} else if (hub->speed != kUSBSpeedHigh && hub->splitHubAddr != 0) {
		splitHubAddr = hub->splitHubAddr;
		splitHubPort = hub->splitHubPort;
	}

	IOLog("BCM2837DWC2: hub port %u: status %#x -> speed=%s split=%s\n",
	    port, status,
	    (speed == kUSBSpeedHigh) ? "high" : (speed == kUSBSpeedLow) ? "low" : "full",
	    (splitHubAddr != 0) ? "yes" : "no");

	*speedOut = speed;
	*splitHubAddrOut = splitHubAddr;
	*splitHubPortOut = splitHubPort;
	return true;
}

/*
 * Power up every downstream port, then reset and enumerate whatever is
 * attached. This is the step that reaches the LAN9514's Ethernet function.
 */
bool
BCM2837DWC2::hubEnumeratePorts(DWC2USBDevice *hub, uint32_t tier)
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
		// (kPortChange*, not kPortStatus* - same bit value here, but this
		// is the change field; see the header's note on the two schemes.)
		if (change & kPortChangeConnection) {
			hubClearPortFeature(hub, port, kHubFeatCPortConnection);
		}

		uint8_t speed = kUSBSpeedFull;
		uint8_t splitHubAddr = 0, splitHubPort = 0;
		// Checked before the reset: a reset device answers at address 0,
		// and one that then cannot be given an address is left there.
		if (fNumDevices >= kDWC2MaxDevices) {
			IOLog("BCM2837DWC2: device table full (%u) - not resetting hub %u "
			      "port %u\n", (uint32_t)kDWC2MaxDevices, hub->address, port);
			continue;
		}
		if (!resetHubPort(hub, port, &speed, &splitHubAddr, &splitHubPort)) {
			continue;
		}

		DWC2USBDevice *child = NULL;
		if (!enumerateDevice(speed, hub->address, port,
		        splitHubAddr, splitHubPort, &child)) {
			/*
			 * Capture the state that distinguishes the remaining
			 * explanations for a silent device, so one boot log settles
			 * it instead of a round trip per hypothesis. Real hardware
			 * is the only place this path has ever failed, and each
			 * attempt costs a full flash-and-boot cycle, so it is worth
			 * paying for all of it at once:
			 *
			 *  - port status/change re-read: did the hub drop or disable
			 *    the port under us (C_PORT_ENABLE set, ENABLE clear -
			 *    babble or a disconnect), or does it still believe a
			 *    device is sitting there enabled? The latter means the
			 *    hub relayed and the device stayed silent; the former
			 *    means the port died and no amount of retrying the
			 *    transfer would have helped.
			 *  - HPRT0/GINTSTS: same question one level up, for the root
			 *    port and the core.
			 *  - HFNUM sampled twice across a known 2ms: proves SOFs are
			 *    actually being generated at all, and at what cadence. A
			 *    counter that does not move means the core stopped
			 *    framing the bus, which is a different failure from a
			 *    device that is framed and silent. HFIR is logged
			 *    alongside it because the two are read together: this
			 *    driver deliberately never writes HFIR (see the long
			 *    note in portReset() - writing it mid-operation is what
			 *    broke real-hardware enumeration through this very
			 *    port), so anything other than the core's own value here
			 *    means something put one there.
			 */
			uint16_t afterStatus = 0, afterChange = 0;
			bool haveAfter = hubGetPortStatus(hub, port,
			    &afterStatus, &afterChange);

			uint32_t frameBefore = regRead(kDWC2_HFNUM) & kHFNUM_MAX_FRNUM;
			IOSleep(2);
			uint32_t frameAfter = regRead(kDWC2_HFNUM) & kHFNUM_MAX_FRNUM;

			IOLog("BCM2837DWC2: enumeration failed on hub port %u "
			      "(port after: %s status %#x change %#x; HPRT0=%#x "
			      "GINTSTS=%#x HFIR=%u; HFNUM %u->%u over 2ms)\n",
			    port, haveAfter ? "read ok" : "READ FAILED",
			    afterStatus, afterChange,
			    regRead(kDWC2_HPRT0), regRead(kDWC2_GINTSTS),
			    regRead(kDWC2_HFIR),
			    frameBefore, frameAfter);
			// Leave nothing answering at address 0 on an enabled port, where
			// it would collide with the next device reset; as
			// AppleUSBHubPort::AddDevice() does on failure
			// (AppleUSBHubPort.cpp:880-889).
			hubClearPortFeature(hub, port, kHubFeatPortEnable);
			continue;
		}

		/*
		 * A hub behind a hub: walk its ports too. Before this only the
		 * root device's own ports were walked, so anything behind a second
		 * hub was never enumerated - on a real Pi 3 that included the
		 * keyboard function of an Apple wired keyboard (05ac:1005 is the
		 * keyboard's built-in hub, on LAN9514 port 2). USB 2.0 (4.1.1)
		 * allows at most five hub tiers below the root port.
		 */
		if (child->deviceClass == 9) {
			if (tier < 5) {
				hubEnumeratePorts(child, tier + 1);
			} else {
				IOLog("BCM2837DWC2: hub at address %u is past the 5-tier limit, "
				      "not walking its ports\n", child->address);
			}
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
		if (!hubEnumeratePorts(root, 1)) {
			IOLog("BCM2837DWC2: hub enumeration failed\n");
		}
	} else {
		IOLog("BCM2837DWC2: root-port device is not a hub (class %u); "
		      "on real hardware the LAN9514 should be\n", root->deviceClass);
	}

	IOLog("BCM2837DWC2: enumeration complete, %u device(s)\n", fNumDevices);
	return fNumDevices;
}
