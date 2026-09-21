/*
 * Concrete SMSCUSBTransport over BCM2837DWC2 - the piece that unblocks
 * iokit/SMSC9514Ethernet/.
 *
 * SMSC9514Ethernet was written with all its chip-level protocol logic
 * complete (reset sequence, MAC/PHY setup, MDIO, packet framing) but
 * deliberately talking only to the abstract 4-method SMSCUSBTransport
 * interface, because no USB host stack existed to implement it. This file
 * is that implementation: it maps those four methods onto the DWC2 driver's
 * control and bulk transfer primitives.
 *
 * ========================= PROTOCOL, VERIFIED ===========================
 *
 * Register access is a vendor control transfer, per Linux's
 * drivers/net/usb/smsc95xx.c __smsc95xx_read_reg/__smsc95xx_write_reg and
 * drivers/net/usb/smsc95xx.h. Values read from that source, not recalled:
 *   READ:  bRequest 0xA1 (USB_VENDOR_REQUEST_READ_REGISTER),
 *          bmRequestType USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
 *          wValue 0, wIndex = register offset, wLength 4, little-endian.
 *   WRITE: bRequest 0xA0 (USB_VENDOR_REQUEST_WRITE_REGISTER),
 *          bmRequestType USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
 *          wValue 0, wIndex = register offset, 4 bytes little-endian.
 * Note wIndex carries the register offset and wValue is always zero - the
 * intuitive assignment is the wrong way round, and getting it backwards
 * would read plausible-looking garbage rather than failing outright.
 *
 * ======================== VERIFICATION STATUS ===========================
 *
 * HONEST STATUS: this file is written against the DWC2 primitives but is
 * NOT end-to-end verified, and cannot be in QEMU. QEMU models no LAN9514
 * (it has no SMSC9514 device at all), so runvm.sh substitutes a generic
 * usb-net RNDIS device - which exercises the DWC2 transfer engine, device
 * enumeration and hub port handling underneath this file for real, but
 * never the SMSC-specific register protocol above. Verifying this layer
 * requires real hardware. Everything below it (BCM2837DWC2.cpp,
 * BCM2837USBEnum.cpp) is QEMU-verified; this file is not, and is marked so
 * rather than being presented as working.
 */

#include "SMSC9514USBTransport.h"

// Vendor requests, from smsc95xx.h:338-339.
#define kSMSCVendorWriteRegister 0xA0
#define kSMSCVendorReadRegister  0xA1

// SMSC's USB vendor ID; both halves of the LAN9514 (hub and Ethernet)
// report it, which is why findEthernetFunction() also requires a bulk pair.
#define kSMSCVendorID            0x0424

bool
SMSC9514USBTransport::init(BCM2837DWC2 *controller, const DWC2USBDevice *device)
{
	if (controller == NULL || device == NULL) {
		return false;
	}

	fController = controller;
	fDevice = device;

	if (!controller->makeEndpoint(device, 0, &fControlEP)) {
		IOLog("SMSC9514USBTransport: no control endpoint\n");
		return false;
	}
	if (device->bulkInEp == 0 || device->bulkOutEp == 0) {
		IOLog("SMSC9514USBTransport: device has no bulk in/out pair "
		      "(in %#x out %#x)\n", device->bulkInEp, device->bulkOutEp);
		return false;
	}
	if (!controller->makeEndpoint(device, device->bulkInEp, &fBulkInEP) ||
	    !controller->makeEndpoint(device, device->bulkOutEp, &fBulkOutEP)) {
		IOLog("SMSC9514USBTransport: bulk endpoint setup failed\n");
		return false;
	}

	IOLog("SMSC9514USBTransport: bound to device %u (%04x:%04x), "
	      "bulk in %#x/%u out %#x/%u\n",
	    device->address, device->idVendor, device->idProduct,
	    device->bulkInEp, device->bulkInMax,
	    device->bulkOutEp, device->bulkOutMax);

	return true;
}

const DWC2USBDevice *
SMSC9514USBTransport::findEthernetFunction(const BCM2837DWC2 *controller)
{
	if (controller == NULL) {
		return NULL;
	}

	for (uint32_t i = 0; i < controller->deviceCount(); i++) {
		const DWC2USBDevice *dev = controller->deviceAt(i);
		if (dev == NULL || !dev->inUse) {
			continue;
		}
		if (dev->idVendor != kSMSCVendorID) {
			continue;
		}
		if (dev->bulkInEp == 0 || dev->bulkOutEp == 0) {
			continue;
		}
		return dev;
	}
	return NULL;
}

/*
 * Prove the vendor-request register protocol works against real silicon.
 *
 * Everything below this file was verifiable under QEMU; this layer never
 * was, because QEMU models no LAN9514 at all. So the first time these
 * control transfers run, they run on hardware, and "it returned some bytes"
 * is not evidence they returned the *right* bytes. Two independent checks,
 * neither of which needs a constant this tree cannot cite from source:
 *
 *  1. Distinct offsets must yield distinct values. This is the specific
 *     failure the .cpp header warns about: wIndex carries the register
 *     offset and wValue is always zero, which is the opposite of the
 *     intuitive assignment. Get them backwards and every read addresses the
 *     same register - returning plausible-looking, self-consistent garbage
 *     rather than failing. Reading several registers and finding them all
 *     identical is exactly that signature.
 *
 *  2. ID_REV must not read as all-zeroes or all-ones, the two values a
 *     dead or absent register file produces.
 *
 * A third check is logged but deliberately NOT fatal: the LAN9514's ID_REV
 * carries the chip ID in its top 16 bits, which is expected to equal the
 * USB product ID this same device enumerated with (0xec00). That is a
 * self-consistent cross-check between two independently-obtained values,
 * needing no hard-coded chip ID - but it is an expectation about the part,
 * not a property of the transport, so a mismatch is reported and not
 * treated as transport failure.
 *
 * Read-only throughout. It runs before any real driver exists, so it must
 * not leave the chip in a state one would have to know about.
 */
bool
SMSC9514USBTransport::probe(void)
{
	static const struct {
		uint32_t    offset;
		const char *name;
	} kProbeRegs[] = {
		{ 0x000, "ID_REV"  },
		{ 0x014, "HW_CFG"  },
		{ 0x020, "PM_CTRL" },
		{ 0x02C, "AFC_CFG" },
		{ 0x100, "MAC_CR"  },
		{ 0x104, "ADDRH"   },
		{ 0x108, "ADDRL"   },
	};
	const uint32_t kNumProbeRegs = sizeof(kProbeRegs) / sizeof(kProbeRegs[0]);

	if (fController == NULL || fDevice == NULL) {
		IOLog("SMSC9514USBTransport: probe called before init\n");
		return false;
	}

	uint32_t values[kNumProbeRegs];
	for (uint32_t i = 0; i < kNumProbeRegs; i++) {
		values[i] = 0;
		if (!readRegister(kProbeRegs[i].offset, &values[i])) {
			IOLog("SMSC9514USBTransport: probe FAILED - could not read %s "
			      "(%#x)\n", kProbeRegs[i].name, kProbeRegs[i].offset);
			return false;
		}
		IOLog("SMSC9514USBTransport:   %-7s (%#05x) = %#010x\n",
		    kProbeRegs[i].name, kProbeRegs[i].offset, values[i]);
	}

	bool allSame = true;
	for (uint32_t i = 1; i < kNumProbeRegs; i++) {
		if (values[i] != values[0]) {
			allSame = false;
			break;
		}
	}
	if (allSame) {
		IOLog("SMSC9514USBTransport: probe FAILED - every register read back "
		      "%#010x, so the offset is not selecting a register. Check that "
		      "wIndex carries the offset and wValue is zero, not the reverse.\n",
		    values[0]);
		return false;
	}

	uint32_t idRev = values[0];
	if (idRev == 0 || idRev == 0xFFFFFFFF) {
		IOLog("SMSC9514USBTransport: probe FAILED - ID_REV reads %#010x, which "
		      "is what an absent or unpowered register file returns\n", idRev);
		return false;
	}

	uint16_t chipID = (uint16_t)(idRev >> 16);
	IOLog("SMSC9514USBTransport: probe OK - ID_REV chip %#06x rev %#06x "
	      "(USB product id %#06x, %s), MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
	    chipID, (uint16_t)(idRev & 0xFFFF), fDevice->idProduct,
	    (chipID == fDevice->idProduct) ? "matches" : "DOES NOT MATCH",
	    (values[6] >> 0) & 0xFF, (values[6] >> 8) & 0xFF,
	    (values[6] >> 16) & 0xFF, (values[6] >> 24) & 0xFF,
	    (values[5] >> 0) & 0xFF, (values[5] >> 8) & 0xFF);

	return true;
}

bool
SMSC9514USBTransport::readRegister(uint32_t offset, uint32_t *value)
{
	if (fController == NULL || value == NULL) {
		return false;
	}

	uint8_t buf[4] = { 0, 0, 0, 0 };
	uint32_t actual = 0;

	IOReturn ret = fController->controlRequest(&fControlEP,
	    kUSBReqDirIn | kUSBReqTypeVendor | kUSBReqRecipDevice,
	    kSMSCVendorReadRegister,
	    0,                    // wValue is always zero
	    (uint16_t)offset,     // wIndex carries the register offset
	    4, buf, &actual);

	if (ret != kIOReturnSuccess || actual != 4) {
		IOLog("SMSC9514USBTransport: read of reg %#x failed (%#x, %u bytes)\n",
		    offset, ret, actual);
		return false;
	}

	// Little-endian on the wire; assembled by hand rather than cast so
	// this does not quietly depend on the host's byte order.
	*value = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
	         ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
	return true;
}

bool
SMSC9514USBTransport::writeRegister(uint32_t offset, uint32_t value)
{
	if (fController == NULL) {
		return false;
	}

	uint8_t buf[4];
	buf[0] = (uint8_t)(value & 0xFF);
	buf[1] = (uint8_t)((value >> 8) & 0xFF);
	buf[2] = (uint8_t)((value >> 16) & 0xFF);
	buf[3] = (uint8_t)((value >> 24) & 0xFF);

	uint32_t actual = 0;
	IOReturn ret = fController->controlRequest(&fControlEP,
	    kUSBReqDirOut | kUSBReqTypeVendor | kUSBReqRecipDevice,
	    kSMSCVendorWriteRegister,
	    0,
	    (uint16_t)offset,
	    4, buf, &actual);

	if (ret != kIOReturnSuccess) {
		IOLog("SMSC9514USBTransport: write of reg %#x failed (%#x)\n", offset, ret);
		return false;
	}
	return true;
}

uint32_t
SMSC9514USBTransport::bulkIn(void *buf, uint32_t maxLen)
{
	if (fController == NULL || buf == NULL || maxLen == 0) {
		return 0;
	}

	/*
	 * SMSC9514Ethernet now drives this from a timer on the shared
	 * IOWorkLoop (see its fRxTimer) rather than transfer()'s 1000ms
	 * default NAK-retry bound. "Nothing queued yet" is the normal, common
	 * case for a poll - BCM2837DWC2 has a single host channel and does
	 * one transfer at a time (BCM2837DWC2.h's "KNOWN LIMITATIONS"), so
	 * blocking that shared gate for up to a full second on every idle
	 * poll would stall TX (outputPacket) and link-status polling behind
	 * it. A short bound here just makes an empty RX FIFO cheap to observe
	 * and retry on the next tick.
	 *
	 * DAR-344: kept equal to SMSC9514Ethernet.h's kRxPollIntervalMS (4ms,
	 * lowered from 20) since the two stack worst-case (see that header's
	 * comment) - a same-switch ping was measuring ~13-21ms RTT off the
	 * old 20/20ms pair.
	 */
	static const uint32_t kRxPollTimeoutMS = 4;

	uint32_t actual = 0;
	IOReturn ret = fController->transfer(&fBulkInEP, buf, maxLen, &actual, kRxPollTimeoutMS);
	if (ret != kIOReturnSuccess) {
		// A timeout here is the normal "no packet waiting" case, so this
		// is deliberately not logged.
		return 0;
	}
	return actual;
}

uint32_t
SMSC9514USBTransport::bulkOut(const void *buf, uint32_t len)
{
	if (fController == NULL || buf == NULL) {
		return 0;
	}

	uint32_t actual = 0;
	// transfer() takes a mutable pointer because it is shared with the IN
	// path; the OUT path only ever reads from it.
	IOReturn ret = fController->transfer(&fBulkOutEP, (void *)buf, len, &actual);
	if (ret != kIOReturnSuccess) {
		IOLog("SMSC9514USBTransport: bulk OUT of %u bytes failed (%#x)\n", len, ret);
		return 0;
	}
	return actual;
}
