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

// RX status word, from smsc95xx.h:24-25.
#define kSMSCRxStsFrameLenMask   0x3FFF0000
#define kSMSCRxStsFrameLenShift  16
#define kSMSCRxStsErrorSummary   0x00008000

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

	uint32_t actual = 0;
	IOReturn ret = fController->transfer(&fBulkInEP, buf, maxLen, &actual);
	if (ret != kIOReturnSuccess) {
		// A timeout here is the normal "no packet waiting" case once an
		// RX poll loop exists, so this is deliberately not logged.
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

/*
 * Pull one bulk-IN transfer apart into Ethernet frames.
 *
 * The chip packs several frames into one bulk transfer, each preceded by a
 * 32-bit RX status word and followed by its FCS, padded so the next status
 * word starts 4-byte aligned (smsc95xx.c's rx_fixup loop). Callers get the
 * frame bytes only - SMSC9514Ethernet::inputPacketAvailable() documents
 * that it expects the status word already stripped and validated.
 *
 * Every step is bounds-checked against the remaining buffer: a corrupt
 * length field in a status word would otherwise walk straight off the end.
 */
uint32_t
SMSC9514USBTransport::forEachRxFrame(void *buf, uint32_t len,
                                     RxFrameHandler handler, void *context)
{
	if (buf == NULL || handler == NULL) {
		return 0;
	}

	uint8_t *p = (uint8_t *)buf;
	uint32_t offset = 0;
	uint32_t frames = 0;

	while (offset + 4 <= len) {
		uint32_t sts = (uint32_t)p[offset] | ((uint32_t)p[offset + 1] << 8) |
		               ((uint32_t)p[offset + 2] << 16) | ((uint32_t)p[offset + 3] << 24);
		offset += 4;

		uint32_t frameLen = (sts & kSMSCRxStsFrameLenMask) >> kSMSCRxStsFrameLenShift;

		if (frameLen == 0 || offset + frameLen > len) {
			// Either the end of the packed frames or a malformed
			// length. Either way there is nothing more to safely read.
			break;
		}

		if (!(sts & kSMSCRxStsErrorSummary)) {
			/*
			 * frameLen includes the 4-byte FCS, which the network
			 * stack does not want. Guard the subtraction: a runt
			 * claiming fewer than 4 bytes would underflow into a
			 * huge length.
			 */
			if (frameLen > 4) {
				handler(context, p + offset, frameLen - 4);
				frames++;
			}
		}

		// Next status word is 4-byte aligned.
		offset += (frameLen + 3) & ~3U;
	}

	return frames;
}
