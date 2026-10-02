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
 * framing. As of 2026-08-25 the RX side is fully wired: fRxTimer polls
 * SMSCUSBTransport::bulkIn() on the same IOWorkLoop as fLinkTimer (see
 * pollReceive()) and hands unpacked frames to inputPacketAvailable() -
 * it is not interrupt-driven because nothing in BCM2837DWC2 is (see that
 * driver's "KNOWN LIMITATIONS": one host channel, one transfer at a time,
 * no async completion callbacks - a second thread polling bulkIn()
 * independently of outputPacket()/updateLinkStatus() would race the same
 * shared channel registers and DMA buffer they use, so this has to be a
 * timer on that one shared, gated workloop, not a separate thread). Link
 * state (updateLinkStatus()) and BSD/ifconfig registration
 * (BCM2837DWC2.cpp's IONetworkStack wiring) are both real as of
 * 2026-08-22 but unverified end-to-end on real hardware (QEMU can't
 * exercise this path at all, see above) - the RX wiring above is equally
 * unverified on real hardware for the same reason. It did already surface
 * one real bug on first real-hardware exercise (2026-08-25): the RX poll
 * path's `memcpy()` into/out of BCM2837DWC2's DMA buffer paniced with an
 * unaligned kernel data abort because that buffer was mapped as real ARM64
 * Device memory (`kIOMapInhibitCache`), which forbids unaligned multi-byte
 * access outright - fixed in BCM2837DWC2::initDMABuffer() by switching to
 * `kIOMapWriteCombineCache` (real Normal Non-Cacheable memory, which this
 * project's SCTLR_EL1 config permits unaligned access on). See
 * docs/historic/backlog.md's P0 item 10 for the full writeup - still not
 * re-verified on real hardware since this fix, only QEMU-regression-tested
 * (QEMU cannot reach this code path at all).
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

	// RX poll timer - see the header comment above on why this is a timer
	// on the shared workloop rather than a dedicated thread. fRxBuffer is
	// reused across polls (allocated in start(), freed in stop()/free())
	// rather than stack-allocated per poll to keep pollReceive()'s stack
	// footprint small.
	IOTimerEventSource  *fRxTimer;
	uint8_t             *fRxBuffer;
	// Consecutive immediate (0ms) re-arms so far in the current burst-drain
	// run - see rxTimerFired()'s own comment and kRxBurstCap below on why
	// this is bounded rather than open-ended.
	uint32_t             fRxBurstCount;
	enum {
		// One bulk-IN transfer can pack several frames (each padded to a
		// 4-byte boundary, see pollReceive()); sized comfortably past one
		// max-size 802.3 frame (1518 bytes) plus its RX_STS word so a
		// single poll can usually drain more than one queued frame.
		kRxBufferSize = 4096,
	};
	// Re-poll interval for pollReceive() while the interface is enabled.
	// SMSC9514USBTransport::bulkIn() itself bounds each individual poll's
	// NAK-retry loop short (kRxPollTimeoutMS there, not transfer()'s 1000ms
	// default) so an idle poll can't hold up TX/link-status polling on the
	// shared workloop - this is how often a new poll gets kicked off.
	//
	// DAR-344: this re-arm is scheduled kRxPollIntervalMS from when the
	// PREVIOUS poll returned, not from when it started, so a poll that
	// found nothing (itself up to kRxPollTimeoutMS of NAK-retrying) stacks
	// with this interval - worst-case gap between NAK-checks was
	// kRxPollTimeoutMS + kRxPollIntervalMS (20+20=40ms), giving a real,
	// measured ~13-21ms average RTT on a same-switch LAN ping that should
	// be sub-millisecond. Lowered from 20 to 4 (with kRxPollTimeoutMS
	// matching in SMSC9514USBTransport.cpp) to cut worst-case latency ~5x.
	//
	// DAR-351: DWC2 channel-completion waits are now real interrupt-driven
	// (DAR-136, BCM2837DWC2.h's fInterruptLock comment) - that is no longer
	// what bounds this gap. rxTimerFired() below now re-arms at 0ms instead
	// of kRxPollIntervalMS immediately after a poll that actually received
	// a frame, on the theory that a live link just proved itself worth
	// draining fast; an idle poll still backs off to kRxPollIntervalMS, so
	// a single request/reply exchange from a cold idle link (ping's own
	// pattern) is not helped by this - only a channel dedicated to RX
	// (see BCM2837DWC2.h's updated KNOWN LIMITATIONS) removes that half of
	// the gap, and that is real, scoped, un-started follow-up work, not
	// done here.
	enum {
		kRxPollIntervalMS = 4,
	};
	// DAR-351: ceiling on consecutive immediate re-arms in rxTimerFired()'s
	// burst-drain path, so a sustained-traffic RX burst cannot hold the
	// shared single DWC2 channel (see BCM2837DWC2.h's fTransferLock/KNOWN
	// LIMITATIONS comments) away from TX/link-status polling indefinitely -
	// the same starvation risk kRxPollTimeoutMS already bounds per-poll,
	// applied across polls. 64 back-to-back full-size frames is already a
	// generous burst (real LAN9514 traffic this project has ever driven is
	// nowhere near sustained enough to hit this in practice); the interval
	// falls back to kRxPollIntervalMS for one tick once hit, which is enough
	// to let a pending TX or link-status poll through before draining
	// resumes.
	enum {
		kRxBurstCap = 64,
	};

	// SCSRs (System Control and Status Registers), byte offsets.
	enum {
		kRegIdRev   = 0x00,
		kRegIntSts  = 0x08,
		kRegRxCfg   = 0x0C,
		kRegTxCfg   = 0x10,
		kRegHwCfg   = 0x14,
		kRegPmCtl   = 0x20,
		// LED_GPIO_CFG - the LAN9514's GPIO0/1/2 pin-function register.
		// Real offset from third_party/u-boot/drivers/usb/eth/smsc95xx.c:22
		// (Linux's drivers/net/usb/smsc95xx.h agrees). On a Pi 3 these three
		// pins are the only thing driving the RJ45 jack's LEDs - see
		// resetChip()'s write of it.
		kRegLedGpioCfg = 0x24,
		kRegAfcCfg  = 0x2C,
		kRegE2pCmd  = 0x30,
		kRegE2pData = 0x34,
		// BURST_CAP/BULK_IN_DLY, real offsets from
		// third_party/u-boot/drivers/usb/eth/smsc95xx.c:72 and :77. Both
		// are written by smsc95xx_init_common() BEFORE it enables
		// HW_CFG_BCE_ - see resetChip() for why that order is load-bearing.
		kRegBurstCap  = 0x38,
		kRegBulkInDly = 0x6C,
	};

	// MAC-level CSRs, same flat register space at higher offsets.
	enum {
		kRegMacCr   = 0x100,
		kRegAddrH   = 0x104,
		kRegAddrL   = 0x108,
		kRegMiiAddr = 0x114,
		kRegMiiData = 0x118,
		kRegFlow    = 0x11C, // smsc95xx.h:253 FLOW
	};

	enum {
		kHwCfgLRst = 1U << 3, // Soft Lite Reset
		kHwCfgSRst = 1U << 0, // Soft Reset

		// Real values from third_party/u-boot/drivers/usb/eth/smsc95xx.c's
		// HW_CFG_MEF_/HW_CFG_BCE_/HW_CFG_RXDOFF_. That file's TURBO_MODE is
		// unconditionally #define'd (line 141), so
		// smsc95xx_init_common()'s `read_buf |= (HW_CFG_MEF_ | HW_CFG_BCE_)`
		// is live code there, not a dead #ifdef branch - see resetChip()'s
		// post-lite-reset HW_CFG read-modify-write.
		kHwCfgMef    = 0x00000020, // Multiple Ethernet Frames
		kHwCfgBce    = 0x00000002, // Burst Cap Enable
		kHwCfgRxDoff = 0x00000600, // RX Data Offset

		// Real BURST_CAP/BULK_IN_DLY values smsc95xx_init_common() writes
		// on a high-speed link (smsc95xx.c:535-536 and :125):
		// burst_cap = DEFAULT_HS_BURST_CAP_SIZE / HS_USB_PKT_SIZE
		//           = (5 * 512) / 512 = 5, and DEFAULT_BULK_IN_DELAY =
		// 0x2000. The LAN9514 on a Pi 3 is always behind the SoC's own
		// high-speed hub, never full-speed, so the reference driver's
		// FS branch has no case here.
		kBurstCapHs  = 5,
		kBulkInDelay = 0x00002000,

		kPmCtlPhyRst = 1U << 4,

		// LED_GPIO_CFG pin-function selects, real values from
		// third_party/u-boot/drivers/usb/eth/smsc95xx.c:23-25
		// (LED_GPIO_CFG_SPD_LED/_LNK_LED/_FDX_LED). Each nibble-aligned
		// field picks what GPIOn does; these three values select the
		// nSPD_LED/nLNK_LED/nFDX_LED hardware LED outputs rather than the
		// plain-GPIO default the chip powers up (and lite-resets) into.
		kLedGpioCfgSpdLed = 0x01000000, // GPIO2 -> speed (10/100) LED
		kLedGpioCfgLnkLed = 0x00100000, // GPIO1 -> link/activity LED
		kLedGpioCfgFdxLed = 0x00010000, // GPIO0 -> full-duplex/collision LED

		kMacCrRcvOwn = 1U << 23, // Disable Receive Own (smsc95xx.h:213, MAC_CR_RCVOWN_)
		kMacCrFdpx  = 1U << 20, // Full Duplex Mode (smsc95xx.h:215, MAC_CR_FDPX_)
		kMacCrMcPas = 1U << 19, // Pass All Multicast
		kMacCrPrmS  = 1U << 18, // Promiscuous Mode
		kMacCrTxEn  = 1U << 3,  // MAC Transmitter Enable
		kMacCrRxEn  = 1U << 2,  // MAC Receiver Enable

		kTxCfgOn = 1U << 2,
		// DAR-465, from smsc95xx.h (Linux v6.6) TX_CFG_STOP_/TX_CFG_FIFO_FLUSH_,
		// INT_STS_TX_STOP_/TXE_/TDFU_/TDFO_, HW_CFG_SBP_.
		kTxCfgStop      = 1U << 1,
		kTxCfgFifoFlush = 1U << 0,
		kIntStsTxStop   = 0x00020000,
		kIntStsTxe      = 0x00004000,
		kIntStsTdfu     = 0x00002000,
		kIntStsTdfo     = 0x00001000,
		kIntStsRxdf     = 0x00000800, // RX Dropped Frame (smsc95xx.h:59)
		kHwCfgSbp       = 0x00000100,
		kAfcCfgDefault  = 0x00F830A1, // smsc95xx.h:137 AFC_CFG_DEFAULT

		kMiiAddrBusy = 1U << 0,
		kMiiAddrWrite = 1U << 1,
	};

	// EEPROM access via E2P_CMD/E2P_DATA (kRegE2pCmd/kRegE2pData above),
	// from smsc95xx.h's E2P_CMD_* bits - same vendor-request register
	// space as everything else, no separate transport method needed.
	enum {
		kE2pCmdBusy     = 1U << 31,
		kE2pCmdRead     = 0,        // E2P_CMD_READ_ - value, not a bit
		kE2pCmdTimeout  = 1U << 10, // set instead of clearing Busy when no EEPROM is present
		kE2pCmdAddrMask = 0x1FF,
	};

	// smsc95xx.c's EEPROM_MAC_OFFSET - the 6 bytes at this offset are the
	// factory-programmed MAC address, when an EEPROM is populated at all.
	enum {
		kEepromMacOffset = 0x01,
	};

	// RX status word prefixing each frame in a bulk-IN transfer, from
	// smsc95xx.h's RX_STS_* bits - see pollReceive().
	enum {
		kRxStsFrameLenMask  = 0x3FFF0000,
		kRxStsFrameLenShift = 16,
		kRxStsErrorSummary  = 0x00008000,
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
		// Pause advertisement, same bit in ANAR and ANLPAR
		// (include/uapi/linux/mii.h:83-84, :106-107 ADVERTISE_/LPA_PAUSE_*).
		kAnarPause     = 1U << 10,
		kAnarPauseAsym = 1U << 11,
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
	bool resetChip(bool resetPhy = true);
	// DAR-465: TX recovery after a send failed even past the transport's
	// own clear-halt-and-retry. See outputPacket().
	void logTxState(void);
	bool recoverChip(bool full);
	// DAR-465: MAC_CR full/half duplex and flow control from the negotiated
	// link, as Linux smsc95xx_mac_update_fullduplex() does.
	void applyDuplex(bool fullDuplex, uint16_t anar, uint16_t anlpar);
	int                  fAppliedDuplex;  // -1 not applied since link-up/reset, 0 half, 1 full
	uint32_t             fTxFailStreak;   // consecutive failed sends
	uint32_t             fTxRecoveries;   // logTxState() calls, for sampled logging
	uint32_t             fRecoveriesInStreak; // recoverChip() calls in the current failure streak
	uint64_t             fLastChipRecovery; // absolute time of the last recoverChip()
	uint32_t             fRecoveryBackoffS; // current recoverChip() spacing: 0, 1, 2 .. 60 s
	bool                 fPromiscOn;      // last setPromiscuousMode(), re-applied after recoverChip()
	bool                 fMulticastOn;    // last setMulticastMode(), likewise
	bool programMACAddress(const IOEthernetAddress *addr);
	bool eepromWait(void);
	bool readEEPROM(uint8_t offset, uint8_t *data, uint32_t length);
	bool createMediumTables(void);
	void updateLinkStatus(void);
	static void linkTimerFired(OSObject *owner, IOTimerEventSource *sender);
	// Returns true if this poll actually received at least one valid frame
	// (see DAR-351's comment on fRxTimer above) - rxTimerFired() uses this
	// to decide whether to re-arm immediately or back off to the normal
	// interval.
	bool pollReceive(void);
	static void rxTimerFired(OSObject *owner, IOTimerEventSource *sender);

public:
	virtual bool init(OSDictionary *properties, SMSCUSBTransport *transport);
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free(void) APPLE_KEXT_OVERRIDE;

	// IONetworkController / IOEthernetController required overrides.
	//
	// enablePacketFilter()/disablePacketFilter() are deliberately NOT
	// overridden here (an earlier version of this file did, as no-op
	// stubs) - IOEthernetController's own real implementation
	// (IOEthernetController.cpp) already dispatches kIOPacketFilterMulticast/
	// kIOPacketFilterPromiscuous to setMulticastMode()/setPromiscuousMode()
	// correctly; overriding it here was shadowing that real logic with a
	// no-op instead of filling in the two virtual hooks it actually wants.
	virtual IOReturn getHardwareAddress(IOEthernetAddress *addrP) APPLE_KEXT_OVERRIDE;
	virtual IOReturn setHardwareAddress(const void *addr, UInt32 addrBytes) APPLE_KEXT_OVERRIDE;
	virtual IOReturn getPacketFilters(const OSSymbol *group, UInt32 *filters) const APPLE_KEXT_OVERRIDE;
	virtual IOReturn setMulticastMode(bool active) APPLE_KEXT_OVERRIDE;
	virtual IOReturn setPromiscuousMode(bool active) APPLE_KEXT_OVERRIDE;
	virtual IOReturn setMulticastList(IOEthernetAddress *addrs, UInt32 count) APPLE_KEXT_OVERRIDE;
	virtual IOReturn enable(IONetworkInterface *interface) APPLE_KEXT_OVERRIDE;
	virtual IOReturn disable(IONetworkInterface *interface) APPLE_KEXT_OVERRIDE;
	virtual UInt32   outputPacket(mbuf_t m, void *param) APPLE_KEXT_OVERRIDE;

	// Called by pollReceive() with each unpacked frame's data (see header
	// and fRxTimer).
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
