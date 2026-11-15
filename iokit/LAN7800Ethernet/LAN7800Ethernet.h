/*
 * LAN7800Ethernet - the Raspberry Pi 3 Model B+'s wired Ethernet: the
 * Microchip LAN7515's gigabit Ethernet function, which "probes and
 * functions as LAN7800" (FreeBSD if_muge.c header), USB 0424:7800 behind
 * the LAN7515's two USB2514 hubs (Linux bcm283x-rpi-lan7515.dtsi).
 *
 * Ported from FreeBSD sys/dev/usb/net/if_muge.c / if_mugereg.h
 * (release/14.1.0, BSD-2-Clause), with Linux drivers/net/usb/lan78xx.c
 * (v6.6) where FreeBSD is silent or leaves it to its miibus layer (the
 * MAC perfect-filter slot 0, flow control, LED modes, the gigabit
 * advertisement). Function names in comments refer to those files.
 *
 * Same split as SMSC9514Ethernet: all chip logic here, talking only to the
 * abstract SMSCUSBTransport (vendor register read/write, bulk IN/OUT) - the
 * LAN78xx uses the same vendor requests, 0xA0 write / 0xA1 read
 * (if_mugereg.h UVR_WRITE_REG/UVR_READ_REG). LAN7800USBDriver supplies an
 * SMSC9514FamilyTransport over the IOUSBFamily pipes and the workloop.
 *
 * As in SMSC9514Ethernet, link state and RX are polled from timers on the
 * workloop (the chip's interrupt endpoint is not used, as FreeBSD does not
 * use it either: "they aren't needed as we poll on the MII status").
 */
#ifndef _LAN7800_ETHERNET_H
#define _LAN7800_ETHERNET_H

#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>
#include "SMSC9514Ethernet.h"   // SMSCUSBTransport

class IONetworkMedium;
class IOTimerEventSource;

class LAN7800Ethernet : public IOEthernetController
{
	OSDeclareDefaultStructors(LAN7800Ethernet);

public:
	// RX buffer the transport must be given: holds one bulk-IN transfer,
	// which BURST_CAP (below) keeps at or under kBurstCapBytes. FreeBSD
	// reads 20480 (muge_config MUGE_BULK_DT_RD); one 16 KB transfer is
	// enough for the 12 KB burst plus slack.
	enum {
		kRxBufferSize  = 16384,
		kBurstCapBytes = 12 * 1024,   // MUGE_DEFAULT_BURST_CAP_SIZE (= MAX_TX_FIFO_SIZE)
		kTxBufferSize  = 2048,        // one frame + TX_CMD_A/B
	};

	virtual bool init(OSDictionary *properties, SMSCUSBTransport *transport);
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free(void) APPLE_KEXT_OVERRIDE;

	virtual IOReturn getHardwareAddress(IOEthernetAddress *addrP) APPLE_KEXT_OVERRIDE;
	virtual IOReturn setHardwareAddress(const void *addr, UInt32 addrBytes) APPLE_KEXT_OVERRIDE;
	virtual IOReturn getPacketFilters(const OSSymbol *group, UInt32 *filters) const APPLE_KEXT_OVERRIDE;
	virtual IOReturn setMulticastMode(bool active) APPLE_KEXT_OVERRIDE;
	virtual IOReturn setPromiscuousMode(bool active) APPLE_KEXT_OVERRIDE;
	virtual IOReturn setMulticastList(IOEthernetAddress *addrs, UInt32 count) APPLE_KEXT_OVERRIDE;
	virtual IOReturn enable(IONetworkInterface *interface) APPLE_KEXT_OVERRIDE;
	virtual IOReturn disable(IONetworkInterface *interface) APPLE_KEXT_OVERRIDE;
	virtual UInt32   outputPacket(mbuf_t m, void *param) APPLE_KEXT_OVERRIDE;

private:
	// Registers (if_mugereg.h ETH_*; lan78xx.h agrees).
	enum {
		kRegIdRev      = 0x000,
		kRegIntSts     = 0x00C,
		kRegHwCfg      = 0x010,
		kRegPmtCtl     = 0x014,
		kRegE2pCmd     = 0x040,
		kRegE2pData    = 0x044,
		kRegUsbCfg0    = 0x080,
		kRegBurstCap   = 0x090,
		kRegBulkInDly  = 0x094,
		kRegIntEpCtl   = 0x098,
		kRegRfeCtl     = 0x0B0,
		kRegFctRxCtl   = 0x0C0,
		kRegFctTxCtl   = 0x0C4,
		kRegFctRxFifoEnd = 0x0C8,
		kRegFctTxFifoEnd = 0x0CC,
		kRegFctFlow    = 0x0D0,
		kRegMacCr      = 0x100,
		kRegMacRx      = 0x104,
		kRegMacTx      = 0x108,
		kRegFlow       = 0x10C,
		kRegRxAddrH    = 0x118,
		kRegRxAddrL    = 0x11C,
		kRegMiiAcc     = 0x120,
		kRegMiiData    = 0x124,
		kRegMafHi0     = 0x400,   // PFILTER_HI(0) = ETH_MAF_BASE + 8*0 + 0
		kRegMafLo0     = 0x404,   // PFILTER_LO(0)
	};
	enum : uint32_t {
		kChipId7800 = 0x7800, kChipId7801 = 0x7801, kChipId7850 = 0x7850,

		kHwCfgLed1En = 1U << 21,
		kHwCfgLed0En = 1U << 20,
		kHwCfgMef    = 1U << 4,
		kHwCfgLrst   = 1U << 1,

		kPmtCtlPhyRst = 1U << 4,

		kE2pCmdBusy    = 1U << 31,
		kE2pCmdRead    = 0,
		kE2pCmdTimeout = 1U << 10,
		kE2pCmdAddrMask = 0x1FF,
		kE2pIndicator  = 0xA5,     // ETH_E2P_INDICATOR at offset 0: EEPROM present

		kUsbCfgBir = 1U << 6,      // NAK bulk-in when the RX FIFO is empty
		kUsbCfgBce = 1U << 5,      // burst cap enable

		kBulkInDelay = 0x0800,     // MUGE_DEFAULT_BULK_IN_DELAY
		kHsUsbPktSize = 512,       // MUGE_HS_USB_PKT_SIZE
		kFifoSize = 12 * 1024,     // MUGE_MAX_RX/TX_FIFO_SIZE

		kIntEpPhy = 1U << 17,

		kRfeBcast    = 1U << 10,
		kRfeMcast    = 1U << 9,
		kRfeUcast    = 1U << 8,
		kRfeDaPerfect = 1U << 1,

		kFctCtlEn = 1U << 31,      // FCT_TX_CTL_EN_ / FCT_RX_CTL_EN_

		kMacCrAutoDuplex = 1U << 12,
		kMacCrAutoSpeed  = 1U << 11,
		kMacRxEn = 1U << 0,
		kMacRxMaxSizeShift = 16,
		kMacRxMaxSizeMask  = 0x3FFF0000,
		kMacTxEn = 1U << 0,

		kFlowTxFcen = 1U << 30,
		kFlowRxFcen = 1U << 29,
		// lan78xx.c FLOW_CTRL_THRESHOLD(FLOW_ON_HS 8704, FLOW_OFF_HS 1024),
		// = if_muge.c's high-speed fct_flow 0x211.
		kFctFlowHs = 0x211,

		kMafHiValid = 1U << 31,

		kMiiAccBusy  = 1U << 0,
		kMiiAccWrite = 1U << 1,

		kRxCmdARed     = 1U << 22,     // receive error
		kRxCmdALenMask = 0x3FFF,
		kTxCmdAFcs     = 1U << 22,     // append FCS
		kTxCmdALenMask = 0xFFFFF,
	};

	// The LAN7800's internal PHY (if_muge.c muge_attach_post: sc_phyno = 1;
	// lan7515.dtsi ethernet-phy@1). Clause-22 registers plus the LAN88xx
	// vendor ones (if_mugereg.h MUGE_*; include/linux/microchipphy.h).
	enum {
		kPhyAddr = 1,
		kMiiBmcr = 0, kMiiBmsr = 1, kMiiAnar = 4, kMiiAnlpar = 5,
		kMiiGtcr = 9, kMiiGtsr = 10,
		kPhyIntStat = 25, kPhyIntMask = 26,
		kPhyLedMode = 29,           // LAN78XX_PHY_LED_MODE_SELECT
		kPhyExtPage = 31,           // LAN88XX_EXT_PAGE_ACCESS
		kPhyExtModeCtrl = 0x13,     // MUGE_EXT_MODE_CTRL (page 1)
	};
	enum : uint16_t {
		kBmcrReset = 1U << 15, kBmcrAnEnable = 1U << 12, kBmcrAnRestart = 1U << 9,
		kBmsrLink = 1U << 2,
		kAnarPauseAsym = 1U << 11, kAnarPause = 1U << 10,
		kAnar100Full = 1U << 8, kAnar100Half = 1U << 7,
		kAnar10Full = 1U << 6, kAnar10Half = 1U << 5, kAnarCsma = 0x0001,
		kGtcr1000Full = 1U << 9, kGtcr1000Half = 1U << 8,    // 1000BASE-T control
		kGtsr1000Full = 1U << 11, kGtsr1000Half = 1U << 10,  // link partner's
		kPhyIntLinkChange = 1U << 13, kPhyIntAnegComp = 1U << 10,
		kExtModeMdixMask = 0x000C, kExtModeAutoMdix = 0x0000,
	};

	enum {
		kMediumAuto = 0,
		kMedium10Half, kMedium10Full, kMedium100Half, kMedium100Full,
		kMedium1000Half, kMedium1000Full,
		kMediumCount
	};
	enum { kLinkPollIntervalMS = 1000, kRxPollIntervalMS = 4, kRxBurstCap = 64 };

	SMSCUSBTransport    *fTransport;   // not owned
	IOEthernetAddress    fMAC;
	IONetworkInterface  *fInterface;   // set by enable()
	IONetworkInterface  *fNetif;       // attachInterface()'s
	IONetworkMedium     *fMedium[kMediumCount];
	IOTimerEventSource  *fLinkTimer;
	IOTimerEventSource  *fRxTimer;
	uint8_t             *fRxBuffer;
	uint32_t             fRxBurstCount;
	uint32_t             fChipId, fChipRev;
	uint32_t             fRfeCtl;      // RFE_CTL as last written (filters)
	bool                 fMulticastOn; // setMulticastMode(), kept across chipInit()
	bool                 fPromiscOn;   // setPromiscuousMode(), likewise
	int                  fLinkMedium;  // -1 down
	uint32_t             fTxFailures;
	uint32_t             fRxErrors;

	bool rd(uint32_t reg, uint32_t *v) { return fTransport->readRegister(reg, v); }
	bool wr(uint32_t reg, uint32_t v) { return fTransport->writeRegister(reg, v); }
	bool waitClear(uint32_t reg, uint32_t bits);
	bool mdioRead(uint8_t reg, uint16_t *v);
	bool mdioWrite(uint8_t reg, uint16_t v);
	bool eepromPresent(void);
	bool programMAC(const IOEthernetAddress *a);
	bool chipInit(void);
	bool phyInit(void);
	bool createMedia(void);
	void updateLink(void);
	bool pollReceive(void);
	void inputFrame(const uint8_t *data, uint32_t len);
	static void linkTimerFired(OSObject *owner, IOTimerEventSource *sender);
	static void rxTimerFired(OSObject *owner, IOTimerEventSource *sender);
};

#endif /* _LAN7800_ETHERNET_H */
