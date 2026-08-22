/*
 * Driver for the SMSC LAN9514 USB-to-Ethernet+hub chip on the RPi3's
 * onboard USB/network combo (Linux: drivers/net/usb/smsc95xx.c).
 *
 * ================================ SCOPE ================================
 * This chip is a USB device, not a memory-mapped one: every register
 * access and every packet TX/RX goes over USB control/bulk transfers to
 * the DWC2 host controller. That host controller driver
 * (iokit/BCM2837DWC2/) and a concrete SMSCUSBTransport
 * (SMSC9514USBTransport, iokit/BCM2837DWC2/) now exist and have both
 * been probe()-verified against real LAN9514 silicon (2026-08-21/22) -
 * this driver is genuinely reachable on real hardware, not stubbed. It
 * still cannot be exercised in QEMU: QEMU's usb-net device is always
 * generic CDC/RNDIS (0525:a4a1/a4a2), never a LAN9514 - see
 * BCM2837DWC2.cpp's findEthernetFunction() call site.
 *
 * Given the USB indirection, this driver is deliberately split in two:
 *   - SMSC9514Ethernet (this class): all of the chip-specific protocol
 *     logic (reset sequence, MAC/PHY setup, RX/TX packet framing) ported
 *     from smsc95xx.c, talking only to the abstract SMSCUSBTransport
 *     interface below - zero DWC2/USB-host-specific code.
 *   - SMSCUSBTransport: a pure-virtual interface capturing exactly the
 *     two USB operations smsc95xx.c actually needs (vendor-specific
 *     register read/write control transfers, and bulk IN/OUT for packet
 *     data). SMSC9514USBTransport is the one concrete implementation,
 *     constructed and handed to SMSC9514Ethernet::start() by
 *     BCM2837DWC2::start() on real hardware only.
 *
 * Known gaps: outputPacket()/inputPacketAvailable() implement real TX/RX
 * framing but the RX side (inputPacketAvailable()) is not called from
 * anywhere yet - no code drives the bulk IN endpoint to feed it. Link
 * state (updateLinkStatus()) and BSD/ifconfig registration
 * (BCM2837DWC2.cpp's IONetworkStack wiring) are both real as of
 * 2026-08-22 but unverified end-to-end on real hardware (QEMU can't
 * exercise this path at all, see above).
 *
 * Register offsets/bits below are from Linux's drivers/net/usb/smsc95xx.h
 * (SCSRs at 0x00-0x3F, MAC-level CSRs at 0x100+, both accessed through the
 * same generic USB_VENDOR_REQUEST_READ/WRITE_REGISTER control transfer -
 * there is no separate indirect-addressing scheme, unlike some other MAC
 * register sets), plus the standard IEEE 802.3 clause-22 MII registers
 * (BMCR/BMSR/ANAR/ANLPAR) for PHY link-state/auto-negotiation.
 */

#ifndef _SMSC9514_ETHERNET_H
#define _SMSC9514_ETHERNET_H

#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/network/IOGatedOutputQueue.h>

class IONetworkMedium;
class IOTimerEventSource;

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
	IONetworkInterface  *fNetif; // owned; the interface attachInterface() created in start()
	bool                 fLinkUp;

	// Real PHY link-state tracking (see updateLinkStatus()). No PHY
	// interrupt is wired up (LAN9514's PHY IRQ line, if even routed to
	// this board, would need real hardware interrupt plumbing through
	// BCM2837DWC2 that doesn't exist), so this is polled instead - the
	// same fallback this project uses elsewhere when a real IRQ path
	// isn't available yet.
	enum {
		kMediumIndexAuto = 0,
		kMediumIndex10HalfDuplex,
		kMediumIndex10FullDuplex,
		kMediumIndex100HalfDuplex,
		kMediumIndex100FullDuplex,
		kMediumCount
	};
	IONetworkMedium     *fMediumTable[kMediumCount];
	IOTimerEventSource  *fLinkTimer;

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

	// LAN9514's internal PHY, at the fixed MDIO address smsc95xx.h calls
	// SMSC95XX_INTERNAL_PHY_ID.
	enum {
		kMiiPhyAddr = 1,
	};

	// Standard IEEE 802.3 clause-22 MII management registers - not
	// SMSC-specific, unlike the SCSR/MAC-CSR enums above, so any
	// standard-compliant PHY would use the same numbers/bits.
	enum {
		kMiiBmcr   = 0x00, // Basic Mode Control Register
		kMiiBmsr   = 0x01, // Basic Mode Status Register
		kMiiAnar   = 0x04, // Auto-Negotiation Advertisement Register
		kMiiAnlpar = 0x05, // Auto-Negotiation Link Partner Ability Register
	};

	enum {
		kBmcrAnEnable  = 1U << 12,
		kBmcrAnRestart = 1U << 9,

		// Link Status latches low on a down transition - a stale read can
		// show a link that dropped and came back as still up. Callers
		// must read kMiiBmsr twice, discarding the first value, to get
		// the true current state (standard MII quirk, not chip-specific).
		kBmsrLinkStatus = 1U << 2,

		// Advertisement/link-partner-ability bits shared between kMiiAnar
		// and kMiiAnlpar - intersecting the two and picking the highest
		// priority set bit (100Full > 100Half > 10Full > 10Half) is the
		// standard IEEE 802.3 auto-negotiation resolution algorithm.
		kAnar100Full = 1U << 8,
		kAnar100Half = 1U << 7,
		kAnar10Full  = 1U << 6,
		kAnar10Half  = 1U << 5,
	};

	// Re-poll interval for updateLinkStatus() while the interface is
	// enabled - see fLinkTimer's header comment for why this is polled
	// rather than interrupt-driven.
	enum {
		kLinkPollIntervalMS = 1000,
	};

	bool mdioWait(void);
	bool mdioRead(uint8_t phyAddr, uint8_t regAddr, uint16_t *value);
	bool mdioWrite(uint8_t phyAddr, uint8_t regAddr, uint16_t value);
	bool resetChip(void);
	bool programMACAddress(const IOEthernetAddress *addr);
	bool createMediumTables(void);
	void updateLinkStatus(void);
	static void linkTimerFired(OSObject *owner, IOTimerEventSource *sender);

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

	// The IONetworkInterface attachInterface() created in start() (fNetif
	// is private). BCM2837DWC2::start() needs the *interface*, not this
	// controller, to hand to IOKitBSDInit.cpp's IORegisterNetworkInterface()
	// - that call resolves its argument's IORegistry path back with
	// OSDynamicCast(IONetworkInterface, ...), which fails silently on a
	// controller object. NULL before start() succeeds / after stop().
	IONetworkInterface *getEthernetInterface(void) const { return fNetif; }
};

#endif /* _SMSC9514_ETHERNET_H */
