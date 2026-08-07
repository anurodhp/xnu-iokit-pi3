/*
 * Driver for the SMSC LAN9514 USB-to-Ethernet+hub chip on the RPi3's
 * onboard USB/network combo (Linux: drivers/net/usb/smsc95xx.c). This is
 * NOT reachable yet - see "Blocking dependency" below.
 *
 * ================================ SCOPE ================================
 * This chip is a USB device, not a memory-mapped one: every register
 * access and every packet TX/RX goes over USB control/bulk transfers to
 * the DWC2 host controller (BCM2835_USB, VC offset 0x980000 per
 * bcm283x.dtsi - not yet in devicetrees/rpi3.dts, not yet matched by any
 * driver). NO IOKit USB host controller driver exists for DWC2 anywhere
 * in this project or in apple-oss-distributions/IOUSBFamily (that family
 * only ships EHCI/OHCI/UHCI/XHCI - real Synopsys DesignWare/DWC2 support
 * was never open-sourced by Apple, since Apple's actual embedded/iPhone
 * equivalent driver for this IP block is closed-source). Writing that
 * host controller driver - USB device/config descriptor parsing, hub and
 * endpoint enumeration, transfer scheduling - is a materially larger,
 * separate undertaking than everything in this repo so far; not
 * attempted here.
 *
 * Given that, this driver is deliberately split in two:
 *   - SMSC9514Ethernet (this class): all of the chip-specific protocol
 *     logic (reset sequence, MAC/PHY setup, RX/TX packet framing) ported
 *     from smsc95xx.c, talking only to the abstract SMSCUSBTransport
 *     interface below - zero DWC2/USB-host-specific code.
 *   - SMSCUSBTransport: a pure-virtual interface capturing exactly the
 *     two USB operations smsc95xx.c actually needs (vendor-specific
 *     register read/write control transfers, and bulk IN/OUT for packet
 *     data). Whatever eventually implements DWC2 host + USB device
 *     enumeration provides a concrete subclass of this and hands it to
 *     SMSC9514Ethernet::start().
 *
 * This means SMSC9514Ethernet's *logic* is real, verifiable, and can be
 * exercised by a test/stub transport today, but genuinely cannot pass a
 * single real packet until the DWC2 work above exists. Not stubbed here
 * to avoid overstating readiness.
 *
 * Register offsets/bits below are from Linux's drivers/net/usb/smsc95xx.h
 * (SCSRs at 0x00-0x3F, MAC-level CSRs at 0x100+, both accessed through the
 * same generic USB_VENDOR_REQUEST_READ/WRITE_REGISTER control transfer -
 * there is no separate indirect-addressing scheme, unlike some other MAC
 * register sets).
 */

#ifndef _SMSC9514_ETHERNET_H
#define _SMSC9514_ETHERNET_H

#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/network/IOGatedOutputQueue.h>

/*
 * Abstract USB transport SMSC9514Ethernet talks to. UNIMPLEMENTED - see
 * header comment above. A real subclass needs a working DWC2 host
 * controller driver plus USB device/endpoint enumeration underneath it.
 */
class SMSCUSBTransport
{
public:
	virtual ~SMSCUSBTransport() {}

	// USB_VENDOR_REQUEST_READ_REGISTER / WRITE_REGISTER control transfers.
	// `offset` is any SCSR (0x00-0x3F) or MAC CSR (0x100+) address from
	// smsc95xx.h. Returns false on any USB transfer failure/stall.
	virtual bool readRegister(uint32_t offset, uint32_t *value) = 0;
	virtual bool writeRegister(uint32_t offset, uint32_t value) = 0;

	// Bulk IN (RX) / bulk OUT (TX) endpoint transfers, framed per
	// smsc95xx's RX_STS/TX_CMD_A/TX_CMD_B word conventions (see .cpp).
	// Returns actual byte count transferred, or 0 on failure.
	virtual uint32_t bulkIn(void *buf, uint32_t maxLen) = 0;
	virtual uint32_t bulkOut(const void *buf, uint32_t len) = 0;
};

class SMSC9514Ethernet : public IOEthernetController
{
	OSDeclareDefaultStructors(SMSC9514Ethernet);

private:
	SMSCUSBTransport    *fTransport; // not owned
	IOEthernetAddress    fMACAddress;
	IOGatedOutputQueue  *fOutputQueue;
	IONetworkInterface  *fInterface; // cached from enable(), for inputPacketAvailable()
	bool                 fLinkUp;

	// SCSRs (System Control and Status Registers), byte offsets.
	enum {
		kRegIdRev   = 0x00,
		kRegIntSts  = 0x08,
		kRegRxCfg   = 0x0C,
		kRegTxCfg   = 0x10,
		kRegHwCfg   = 0x14,
		kRegPmCtl   = 0x20,
		kRegAfcCfg  = 0x2C,
		kRegE2pCmd  = 0x30,
		kRegE2pData = 0x34,
	};

	// MAC-level CSRs, same flat register space at higher offsets.
	enum {
		kRegMacCr   = 0x100,
		kRegAddrH   = 0x104,
		kRegAddrL   = 0x108,
		kRegMiiAddr = 0x114,
		kRegMiiData = 0x118,
	};

	enum {
		kHwCfgLRst = 1U << 3, // Soft Lite Reset
		kHwCfgSRst = 1U << 0, // Soft Reset

		kPmCtlPhyRst = 1U << 4,

		kMacCrMcPas = 1U << 19, // Pass All Multicast
		kMacCrPrmS  = 1U << 18, // Promiscuous Mode
		kMacCrTxEn  = 1U << 3,  // MAC Transmitter Enable
		kMacCrRxEn  = 1U << 2,  // MAC Receiver Enable

		kTxCfgOn = 1U << 2,

		kMiiAddrBusy = 1U << 0,
		kMiiAddrWrite = 1U << 1,
	};

	bool mdioWait(void);
	bool mdioRead(uint8_t phyAddr, uint8_t regAddr, uint16_t *value);
	bool mdioWrite(uint8_t phyAddr, uint8_t regAddr, uint16_t value);
	bool resetChip(void);
	bool programMACAddress(const IOEthernetAddress *addr);

public:
	virtual bool init(OSDictionary *properties, SMSCUSBTransport *transport);
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free(void) APPLE_KEXT_OVERRIDE;

	// IONetworkController / IOEthernetController required overrides.
	virtual IOReturn getHardwareAddress(IOEthernetAddress *addrP) APPLE_KEXT_OVERRIDE;
	virtual IOReturn setHardwareAddress(const void *addr, UInt32 addrBytes) APPLE_KEXT_OVERRIDE;
	virtual IOReturn getPacketFilters(const OSSymbol *group, UInt32 *filters) const APPLE_KEXT_OVERRIDE;
	virtual IOReturn enablePacketFilter(const OSSymbol *group, UInt32 aFilter,
	                                     UInt32 enabledFilters, IOOptionBits options = 0) APPLE_KEXT_OVERRIDE;
	virtual IOReturn disablePacketFilter(const OSSymbol *group, UInt32 aFilter,
	                                      UInt32 enabledFilters, IOOptionBits options = 0) APPLE_KEXT_OVERRIDE;
	virtual IOReturn enable(IONetworkInterface *interface) APPLE_KEXT_OVERRIDE;
	virtual IOReturn disable(IONetworkInterface *interface) APPLE_KEXT_OVERRIDE;
	virtual UInt32   outputPacket(mbuf_t m, void *param) APPLE_KEXT_OVERRIDE;

	// Called by whatever eventually drives the bulk IN endpoint with
	// received frame data (not wired to anything yet - see header).
	void inputPacketAvailable(void *frameData, uint32_t frameLen);
};

#endif /* _SMSC9514_ETHERNET_H */
