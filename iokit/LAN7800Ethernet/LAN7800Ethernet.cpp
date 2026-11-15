/*
 * LAN7800Ethernet - see LAN7800Ethernet.h. References: FreeBSD
 * sys/dev/usb/net/if_muge.c (release/14.1.0) and Linux
 * drivers/net/usb/lan78xx.c (v6.6), cited by function name.
 */
#include "LAN7800Ethernet.h"
#include "BCM2837Mailbox.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/network/IONetworkMedium.h>
#include <pexpert/pexpert.h>

#define super IOEthernetController

OSDefineMetaClassAndStructors(LAN7800Ethernet, IOEthernetController);

static inline uint32_t
le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool
validMAC(const uint8_t *a)
{
	// ETHER_IS_VALID: not multicast, not all zero.
	return !(a[0] & 1) && (a[0] | a[1] | a[2] | a[3] | a[4] | a[5]) != 0;
}

bool
LAN7800Ethernet::init(OSDictionary *properties, SMSCUSBTransport *transport)
{
	if (!super::init(properties)) {
		return false;
	}
	fTransport = transport;
	fInterface = NULL;
	fNetif = NULL;
	bzero(fMedium, sizeof(fMedium));
	fLinkTimer = NULL;
	fRxTimer = NULL;
	fRxBuffer = NULL;
	fRxBurstCount = 0;
	fChipId = fChipRev = 0;
	fRfeCtl = 0;
	fLinkMedium = -1;
	fTxFailures = 0;
	fRxErrors = 0;
	fMulticastOn = false;
	fPromiscOn = false;
	bzero(&fMAC, sizeof(fMAC));
	return true;
}

// ------------------------------------------------------------- registers

bool
LAN7800Ethernet::waitClear(uint32_t reg, uint32_t bits)
{
	// lan78xx_wait_for_bits(): poll every 10 ms for up to 1 s.
	for (int i = 0; i < 100; i++) {
		uint32_t v;
		if (!rd(reg, &v)) {
			return false;
		}
		if (!(v & bits)) {
			return true;
		}
		IOSleep(10);
	}
	return false;
}

bool
LAN7800Ethernet::mdioRead(uint8_t reg, uint16_t *v)
{
	// lan78xx_miibus_readreg()
	if (!waitClear(kRegMiiAcc, kMiiAccBusy)) {
		return false;
	}
	uint32_t acc = ((uint32_t)kPhyAddr << 11) | ((uint32_t)reg << 6) | kMiiAccBusy;
	uint32_t data;
	if (!wr(kRegMiiAcc, acc) || !waitClear(kRegMiiAcc, kMiiAccBusy) || !rd(kRegMiiData, &data)) {
		return false;
	}
	*v = (uint16_t)data;
	return true;
}

bool
LAN7800Ethernet::mdioWrite(uint8_t reg, uint16_t v)
{
	// lan78xx_miibus_writereg()
	if (!waitClear(kRegMiiAcc, kMiiAccBusy)) {
		return false;
	}
	uint32_t acc = ((uint32_t)kPhyAddr << 11) | ((uint32_t)reg << 6) | kMiiAccWrite | kMiiAccBusy;
	return wr(kRegMiiData, v) && wr(kRegMiiAcc, acc) && waitClear(kRegMiiAcc, kMiiAccBusy);
}

bool
LAN7800Ethernet::eepromPresent(void)
{
	// lan78xx_eeprom_present() / lan78xx_eeprom_read_raw(): one byte at
	// offset 0 must read ETH_E2P_INDICATOR. On the LAN7800 the EEPROM pins
	// are shared with LED0/LED1, which are switched off for the read and
	// restored after.
	uint32_t saved = 0, v;
	bool led = (fChipId == kChipId7800);
	if (led) {
		if (!rd(kRegHwCfg, &saved) || !wr(kRegHwCfg, saved & ~(kHwCfgLed0En | kHwCfgLed1En))) {
			return false;
		}
	}
	bool ok = waitClear(kRegE2pCmd, kE2pCmdBusy) &&
	    wr(kRegE2pCmd, kE2pCmdBusy | kE2pCmdRead | (0 & kE2pCmdAddrMask));
	if (ok) {
		ok = false;
		for (int i = 0; i < 100; i++) {
			if (!rd(kRegE2pCmd, &v)) {
				break;
			}
			if (!(v & kE2pCmdBusy) || (v & kE2pCmdTimeout)) {
				ok = !(v & (kE2pCmdBusy | kE2pCmdTimeout));
				break;
			}
			IOSleep(10);
		}
	}
	uint32_t data = 0;
	ok = ok && rd(kRegE2pData, &data);
	if (led) {
		wr(kRegHwCfg, saved);
	}
	return ok && (data & 0xFF) == kE2pIndicator;
}

bool
LAN7800Ethernet::programMAC(const IOEthernetAddress *a)
{
	// lan78xx_setmacaddress() writes RX_ADDRL/H; Linux
	// lan78xx_init_mac_address() also fills perfect-filter slot 0 and marks
	// it valid, which RFE_CTL's DA_PERFECT matches unicast against.
	const uint8_t *m = a->bytes;
	uint32_t lo = (uint32_t)m[3] << 24 | (uint32_t)m[2] << 16 | (uint32_t)m[1] << 8 | m[0];
	uint32_t hi = (uint32_t)m[5] << 8 | m[4];
	return wr(kRegRxAddrL, lo) && wr(kRegRxAddrH, hi) &&
	    wr(kRegMafLo0, lo) && wr(kRegMafHi0, hi | kMafHiValid);
}

bool
LAN7800Ethernet::phyInit(void)
{
	// lan78xx_phy_init() (if_muge.c).
	if (!mdioWrite(kMiiBmcr, kBmcrReset)) {
		return false;
	}
	uint16_t bmcr = kBmcrReset;
	for (int i = 0; i < 100 && (bmcr & kBmcrReset); i++) {
		IOSleep(10);
		if (!mdioRead(kMiiBmcr, &bmcr)) {
			return false;
		}
	}
	if (bmcr & kBmcrReset) {
		IOLog("LAN7800Ethernet: PHY reset timed out\n");
		return false;
	}
	uint16_t v;
	mdioRead(kPhyIntStat, &v);
	mdioWrite(kPhyIntMask, kPhyIntAnegComp | kPhyIntLinkChange);

	// lan78xx_set_mdix_auto(): extended page 1, MODE_CTRL MDIX = auto.
	if (mdioWrite(kPhyExtPage, 1) && mdioRead(kPhyExtModeCtrl, &v)) {
		mdioWrite(kPhyExtModeCtrl, (uint16_t)((v & ~kExtModeMdixMask) | kExtModeAutoMdix));
	}
	mdioWrite(kPhyExtPage, 0);

	// All 10/100 modes with symmetric and asymmetric pause, as if_muge.c.
	// Gigabit is advertised in the 1000BASE-T control register (IEEE 802.3
	// clause 40.5, register 9), which FreeBSD's miibus and Linux's
	// genphy_config_advert() set for a gigabit-capable PHY; this driver has
	// no PHY layer, so it is written here.
	mdioWrite(kMiiAnar, kAnar10Half | kAnar10Full | kAnar100Half | kAnar100Full |
	    kAnarCsma | kAnarPause | kAnarPauseAsym);
	uint16_t gtcr = 0;
	if (mdioRead(kMiiGtcr, &gtcr)) {
		mdioWrite(kMiiGtcr, gtcr | kGtcr1000Full | kGtcr1000Half);
	}

	// LEDs as the Pi 3B+ wires them: bcm283x-rpi-lan7515.dtsi
	// microchip,led-modes = <LAN78XX_LINK_1000_ACTIVITY (1)
	// LAN78XX_LINK_10_100_ACTIVITY (6)>. Linux microchip.c lan88xx_probe()
	// puts mode i in nibble i of LED_MODE_SELECT and forces the rest off
	// (LAN78XX_FORCE_LED_OFF, 14); lan78xx_phy_init() then enables LED0 and
	// LED1 in HW_CFG.
	mdioWrite(kPhyLedMode, (uint16_t)(1 | 6 << 4 | 14 << 8 | 14 << 12));
	uint32_t hw;
	if (rd(kRegHwCfg, &hw)) {
		wr(kRegHwCfg, (hw & ~(kHwCfgLed0En | kHwCfgLed1En | (1U << 22) | (1U << 23))) |
		    kHwCfgLed0En | kHwCfgLed1En);
	}

	if (!mdioRead(kMiiBmcr, &bmcr)) {
		return false;
	}
	return mdioWrite(kMiiBmcr, bmcr | kBmcrAnEnable | kBmcrAnRestart);
}

bool
LAN7800Ethernet::chipInit(void)
{
	// lan78xx_chip_init() (if_muge.c), in its order.
	uint32_t v;
	if (!wr(kRegHwCfg, kHwCfgLrst) || !waitClear(kRegHwCfg, kHwCfgLrst)) {
		IOLog("LAN7800Ethernet: lite reset did not complete\n");
		return false;
	}
	if (!programMAC(&fMAC)) {
		return false;
	}
	if (!rd(kRegIdRev, &v)) {
		return false;
	}
	fChipId = v >> 16;
	fChipRev = v & 0xFFFF;
	if (fChipId != kChipId7800 && fChipId != kChipId7850) {
		IOLog("LAN7800Ethernet: chip id %#x not supported\n", fChipId);
		return false;
	}
	// NAK bulk-in while the RX FIFO is empty.
	if (!rd(kRegUsbCfg0, &v) || !wr(kRegUsbCfg0, v | kUsbCfgBir)) {
		return false;
	}
	// Burst cap in high-speed packets. The LAN7515 is a USB 2.0 device
	// behind the Pi's high-speed hub, so the HS case of if_muge.c's switch.
	// It bounds one bulk-IN transfer, which must fit the transport's
	// kRxBufferSize read.
	bool ok = wr(kRegBurstCap, kBurstCapBytes / kHsUsbPktSize) &&
	    wr(kRegBulkInDly, kBulkInDelay) &&
	    rd(kRegHwCfg, &v) && wr(kRegHwCfg, v | kHwCfgMef) &&      // several frames per transfer
	    rd(kRegUsbCfg0, &v) && wr(kRegUsbCfg0, v | kUsbCfgBce) &&
	    wr(kRegFctRxFifoEnd, (kFifoSize - 512) / 512) &&
	    wr(kRegFctTxFifoEnd, (kFifoSize - 512) / 512) &&
	    wr(kRegIntSts, 0xFFFFFFFF) &&
	    wr(kRegFlow, 0) && wr(kRegFctFlow, 0) &&
	    rd(kRegRfeCtl, &v);
	if (!ok) {
		return false;
	}
	fRfeCtl = (v & ~(kRfeMcast | kRfeUcast)) | kRfeBcast | kRfeDaPerfect;
	if (fMulticastOn) fRfeCtl |= kRfeMcast;
	if (fPromiscOn) fRfeCtl |= kRfeMcast | kRfeUcast;
	if (!wr(kRegRfeCtl, fRfeCtl)) {
		return false;
	}
	if (!wr(kRegPmtCtl, kPmtCtlPhyRst) || !waitClear(kRegPmtCtl, kPmtCtlPhyRst)) {
		IOLog("LAN7800Ethernet: PHY reset did not complete\n");
		return false;
	}
	// A LAN7800 without an EEPROM takes speed and duplex from the PHY.
	bool eeprom = eepromPresent();
	if (!rd(kRegMacCr, &v)) {
		return false;
	}
	if (fChipId == kChipId7800 && !eeprom) {
		v |= kMacCrAutoDuplex | kMacCrAutoSpeed;
	}
	// lan78xx_set_rx_max_frame_length(ETHER_MAX_LEN 1518): the field holds
	// size + 4, written with the receiver off (it is off here).
	ok = wr(kRegMacCr, v) &&
	    rd(kRegIntEpCtl, &v) && wr(kRegIntEpCtl, v | kIntEpPhy) &&
	    rd(kRegMacTx, &v) && wr(kRegMacTx, v | kMacTxEn) &&
	    rd(kRegFctTxCtl, &v) && wr(kRegFctTxCtl, v | kFctCtlEn) &&
	    rd(kRegMacRx, &v) &&
	    wr(kRegMacRx, (v & ~(kMacRxMaxSizeMask | kMacRxEn)) |
	        (((1518 + 4) << kMacRxMaxSizeShift) & kMacRxMaxSizeMask));
	if (!ok || !phyInit()) {
		return false;
	}
	ok = rd(kRegMacRx, &v) && wr(kRegMacRx, v | kMacRxEn) &&
	    rd(kRegFctRxCtl, &v) && wr(kRegFctRxCtl, v | kFctCtlEn);
	if (ok) {
		IOLog("LAN7800Ethernet: chip %#x rev %#x, %s EEPROM, burst cap %u bytes\n",
		    fChipId, fChipRev, eeprom ? "with" : "no", (unsigned)kBurstCapBytes);
	}
	return ok;
}

// ------------------------------------------------------------ lifecycle

bool
LAN7800Ethernet::createMedia(void)
{
	static const struct { UInt32 index; IOMediumType type; UInt64 speed; } kMedia[kMediumCount] = {
		{ kMediumAuto,     kIOMediumEthernetAuto, 0 },
		{ kMedium10Half,   kIOMediumEthernet10BaseT, 10000000ULL },
		{ kMedium10Full,   kIOMediumEthernet10BaseT | kIOMediumOptionFullDuplex, 10000000ULL },
		{ kMedium100Half,  kIOMediumEthernet100BaseTX, 100000000ULL },
		{ kMedium100Full,  kIOMediumEthernet100BaseTX | kIOMediumOptionFullDuplex, 100000000ULL },
		{ kMedium1000Half, kIOMediumEthernet1000BaseT, 1000000000ULL },
		{ kMedium1000Full, kIOMediumEthernet1000BaseT | kIOMediumOptionFullDuplex, 1000000000ULL },
	};
	OSDictionary *dict = OSDictionary::withCapacity(kMediumCount);
	if (dict == NULL) {
		return false;
	}
	bool ok = true;
	for (UInt32 i = 0; i < kMediumCount && ok; i++) {
		IONetworkMedium *m = IONetworkMedium::medium(kMedia[i].type, kMedia[i].speed, 0, kMedia[i].index);
		if (m == NULL || !IONetworkMedium::addMedium(dict, m)) {
			if (m) m->release();
			ok = false;
			break;
		}
		fMedium[kMedia[i].index] = m;   // released in free()
	}
	ok = ok && publishMediumDictionary(dict);
	dict->release();
	return ok;
}

bool
LAN7800Ethernet::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}
	if (fTransport == NULL) {
		return false;
	}

	// MAC address. FreeBSD muge_set_mac_addr() tries the RX_ADDR registers
	// (loaded from an EEPROM/OTP), then lets the board's FDT override them;
	// on a Pi the board's address comes from the VideoCore firmware (what
	// it hands Linux as local-mac-address), so that goes first, as in
	// SMSC9514Ethernet. Read the registers before the lite reset.
	uint32_t lo = 0, hi = 0;
	uint8_t regMAC[6] = {0};
	if (rd(kRegRxAddrL, &lo) && rd(kRegRxAddrH, &hi)) {
		regMAC[0] = lo; regMAC[1] = lo >> 8; regMAC[2] = lo >> 16; regMAC[3] = lo >> 24;
		regMAC[4] = hi; regMAC[5] = hi >> 8;
	}
	const char *src;
	if (BCM2837Mailbox::getBoardMACAddress(fMAC.bytes) && validMAC(fMAC.bytes)) {
		src = "VideoCore mailbox";
	} else if (validMAC(regMAC)) {
		memcpy(fMAC.bytes, regMAC, 6);
		src = "chip registers (EEPROM/OTP)";
	} else {
		static const uint8_t ph[6] = { 0x02, 0x00, 0x00, 0x00, 0x78, 0x00 };
		memcpy(fMAC.bytes, ph, 6);
		src = "placeholder (no mailbox answer, no EEPROM/OTP)";
	}
	IOLog("LAN7800Ethernet: MAC %02x:%02x:%02x:%02x:%02x:%02x from %s\n",
	    fMAC.bytes[0], fMAC.bytes[1], fMAC.bytes[2], fMAC.bytes[3], fMAC.bytes[4], fMAC.bytes[5], src);

	if (!chipInit()) {
		IOLog("LAN7800Ethernet: chip init failed\n");
		return false;
	}
	if (!createMedia()) {
		return false;
	}
	IOWorkLoop *wl = getWorkLoop();
	if (wl == NULL) {
		return false;
	}
	fLinkTimer = IOTimerEventSource::timerEventSource(this, &LAN7800Ethernet::linkTimerFired);
	if (fLinkTimer == NULL || wl->addEventSource(fLinkTimer) != kIOReturnSuccess) {
		return false;
	}
	fRxBuffer = (uint8_t *)IOMalloc(kRxBufferSize);
	if (fRxBuffer == NULL) {
		return false;
	}
	fRxTimer = IOTimerEventSource::timerEventSource(this, &LAN7800Ethernet::rxTimerFired);
	if (fRxTimer == NULL || wl->addEventSource(fRxTimer) != kIOReturnSuccess) {
		return false;
	}
	if (!attachInterface((IONetworkInterface **)&fNetif, true)) {
		IOLog("LAN7800Ethernet: attachInterface failed\n");
		return false;
	}
	registerService();
	return true;
}

void
LAN7800Ethernet::stop(IOService *provider)
{
	IOWorkLoop *wl = getWorkLoop();
	IOTimerEventSource **timers[2] = { &fLinkTimer, &fRxTimer };
	for (int i = 0; i < 2; i++) {
		if (*timers[i] != NULL) {
			(*timers[i])->cancelTimeout();
			if (wl) wl->removeEventSource(*timers[i]);
			(*timers[i])->release();
			*timers[i] = NULL;
		}
	}
	if (fNetif != NULL) {
		detachInterface(fNetif);
		fNetif = NULL;
	}
	super::stop(provider);
}

void
LAN7800Ethernet::free(void)
{
	if (fLinkTimer) { fLinkTimer->cancelTimeout(); fLinkTimer->release(); fLinkTimer = NULL; }
	if (fRxTimer) { fRxTimer->cancelTimeout(); fRxTimer->release(); fRxTimer = NULL; }
	if (fRxBuffer) { IOFree(fRxBuffer, kRxBufferSize); fRxBuffer = NULL; }
	for (UInt32 i = 0; i < kMediumCount; i++) {
		OSSafeReleaseNULL(fMedium[i]);
	}
	super::free();
}

// --------------------------------------------------------------- link

void
LAN7800Ethernet::updateLink(void)
{
	// BMSR's link bit latches low: the second read is the current state.
	uint16_t bmsr;
	if (!mdioRead(kMiiBmsr, &bmsr) || !mdioRead(kMiiBmsr, &bmsr)) {
		return;
	}
	if (!(bmsr & kBmsrLink)) {
		if (fLinkMedium >= 0) {
			fLinkMedium = -1;
			setLinkStatus(kIONetworkLinkValid, NULL);
			IOLog("LAN7800Ethernet: link down\n");
		}
		return;
	}
	// Resolve what was negotiated: 1000BASE-T from register 9 (ours) and
	// 10 (the partner's), else the highest common 10/100 ability.
	uint16_t gtcr = 0, gtsr = 0, anar = 0, anlpar = 0;
	int medium = kMediumAuto;
	UInt64 speed = 0;
	mdioRead(kMiiAnar, &anar);
	mdioRead(kMiiAnlpar, &anlpar);
	if (mdioRead(kMiiGtcr, &gtcr) && mdioRead(kMiiGtsr, &gtsr)) {
		if ((gtcr & kGtcr1000Full) && (gtsr & kGtsr1000Full)) {
			medium = kMedium1000Full;
		} else if ((gtcr & kGtcr1000Half) && (gtsr & kGtsr1000Half)) {
			medium = kMedium1000Half;
		}
	}
	if (medium == kMediumAuto) {
		uint16_t c = anar & anlpar;
		medium = (c & kAnar100Full) ? kMedium100Full : (c & kAnar100Half) ? kMedium100Half :
		    (c & kAnar10Full) ? kMedium10Full : (c & kAnar10Half) ? kMedium10Half : kMediumAuto;
	}
	switch (medium) {
	case kMedium1000Full: case kMedium1000Half: speed = 1000000000ULL; break;
	case kMedium100Full: case kMedium100Half: speed = 100000000ULL; break;
	case kMedium10Full: case kMedium10Half: speed = 10000000ULL; break;
	}
	if (medium == fLinkMedium) {
		return;
	}
	// MAC_CR's auto speed/duplex follows the PHY. Flow control is the
	// driver's (lan78xx_update_flowcontrol(), the muge statchg): pause
	// resolved from both advertisements on a full-duplex link
	// (mii_resolve_flowctrl_fdx), FCT_FLOW's high-speed thresholds written
	// before FLOW enables it.
	bool full = (medium == kMedium10Full || medium == kMedium100Full || medium == kMedium1000Full);
	bool txPause = false, rxPause = false;
	if (full) {
		if (anar & anlpar & kAnarPause) {
			txPause = rxPause = true;
		} else if (anar & anlpar & kAnarPauseAsym) {
			txPause = (anlpar & kAnarPause) != 0;
			rxPause = (anar & kAnarPause) != 0;
		}
	}
	uint32_t flow = (txPause ? (kFlowTxFcen | 0xFFFF) : 0) | (rxPause ? kFlowRxFcen : 0);
	wr(kRegFctFlow, kFctFlowHs);
	wr(kRegFlow, flow);

	fLinkMedium = medium;
	setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive, fMedium[medium], speed);
	IOLog("LAN7800Ethernet: link up, %llu Mb/s %s duplex, pause tx %u rx %u\n",
	    speed / 1000000, full ? "full" : "half", txPause, rxPause);
}

void
LAN7800Ethernet::linkTimerFired(OSObject *owner, IOTimerEventSource *sender)
{
	LAN7800Ethernet *self = OSDynamicCast(LAN7800Ethernet, owner);
	if (self == NULL) {
		return;
	}
	self->updateLink();
	sender->setTimeoutMS(kLinkPollIntervalMS);
}

// ------------------------------------------------------------- filters

IOReturn
LAN7800Ethernet::getHardwareAddress(IOEthernetAddress *addrP)
{
	*addrP = fMAC;
	return kIOReturnSuccess;
}

IOReturn
LAN7800Ethernet::setHardwareAddress(const void *addr, UInt32 addrBytes)
{
	if (addrBytes != sizeof(IOEthernetAddress)) {
		return kIOReturnBadArgument;
	}
	const IOEthernetAddress *a = (const IOEthernetAddress *)addr;
	if (!programMAC(a)) {
		return kIOReturnIOError;
	}
	fMAC = *a;
	return kIOReturnSuccess;
}

IOReturn
LAN7800Ethernet::getPacketFilters(const OSSymbol *group, UInt32 *filters) const
{
	if (group != gIONetworkFilterGroup) {
		*filters = 0;
		return kIOReturnSuccess;
	}
	*filters = kIOPacketFilterUnicast | kIOPacketFilterBroadcast |
	    kIOPacketFilterMulticast | kIOPacketFilterPromiscuous;
	return kIOReturnSuccess;
}

IOReturn
LAN7800Ethernet::setMulticastMode(bool active)
{
	// RFE_CTL MCAST_EN passes all multicast (muge_setmulti()'s IFF_ALLMULTI
	// case) - a superset of the per-address hash filter, which is not
	// programmed, as SMSC9514Ethernet does with MAC_CR MCPAS.
	fMulticastOn = active;
	uint32_t v = fRfeCtl & ~(kRfeMcast | kRfeUcast);
	if (fMulticastOn) v |= kRfeMcast;
	if (fPromiscOn) v |= kRfeMcast | kRfeUcast;
	if (!wr(kRegRfeCtl, v)) {
		return kIOReturnIOError;
	}
	fRfeCtl = v;
	return kIOReturnSuccess;
}

IOReturn
LAN7800Ethernet::setPromiscuousMode(bool active)
{
	// muge_setpromisc(): MCAST_EN | UCAST_EN.
	fPromiscOn = active;
	return setMulticastMode(fMulticastOn);
}

IOReturn
LAN7800Ethernet::setMulticastList(IOEthernetAddress *, UInt32)
{
	return kIOReturnSuccess;   // all multicast passes while the filter is on
}

IOReturn
LAN7800Ethernet::enable(IONetworkInterface *interface)
{
	fInterface = interface;
	fLinkMedium = -1;
	updateLink();
	if (fLinkTimer) fLinkTimer->setTimeoutMS(kLinkPollIntervalMS);
	fRxBurstCount = 0;
	if (fRxTimer) fRxTimer->setTimeoutMS(kRxPollIntervalMS * (UInt32)debug_cpu_performance_degradation_factor);
	return kIOReturnSuccess;
}

IOReturn
LAN7800Ethernet::disable(IONetworkInterface *)
{
	if (fLinkTimer) fLinkTimer->cancelTimeout();
	if (fRxTimer) fRxTimer->cancelTimeout();
	fLinkMedium = -1;
	fInterface = NULL;
	return kIOReturnSuccess;
}

// ------------------------------------------------------------ TX / RX

UInt32
LAN7800Ethernet::outputPacket(mbuf_t m, void *)
{
	// muge_bulk_write_callback(): TX_CMD_A = length | FCS (the chip
	// appends the CRC), TX_CMD_B = 0 (no LSO/VLAN), then the frame.
	size_t len = 0;
	for (mbuf_t n = m; n != NULL; n = mbuf_next(n)) {
		len += mbuf_len(n);
	}
	if (len == 0 || len + 8 > kTxBufferSize || len > kTxCmdALenMask) {
		freePacket(m);
		return kIOReturnOutputDropped;
	}
	// Per frame, as SMSC9514Ethernet: outputPacket() can run on more than
	// one thread (no output queue), and the transport copies the frame
	// into its own DMA buffer under its lock.
	uint8_t *buf = (uint8_t *)IOMalloc(kTxBufferSize);
	if (buf == NULL) {
		freePacket(m);
		return kIOReturnOutputDropped;
	}
	uint32_t a = (uint32_t)len | kTxCmdAFcs, b = 0;
	for (int i = 0; i < 4; i++) {
		buf[i] = (uint8_t)(a >> (8 * i));
		buf[4 + i] = (uint8_t)(b >> (8 * i));
	}
	size_t off = 8;
	for (mbuf_t n = m; n != NULL; n = mbuf_next(n)) {
		bcopy(mbuf_data(n), buf + off, mbuf_len(n));
		off += mbuf_len(n);
	}
	freePacket(m);
	// The transport ends a transfer that fills whole max-size packets with
	// a zero-length packet, as lan78xx_tx_bh() asks for URB_ZERO_PACKET
	// when tx_buf->len % maxpacket == 0 (lan78xx.c:3921-3923).
	uint32_t sent = fTransport->bulkOut(buf, (uint32_t)off);
	IOFree(buf, kTxBufferSize);
	if (sent != off) {
		if (fTxFailures++ < 8 || (fTxFailures & (fTxFailures - 1)) == 0) {
			IOLog("LAN7800Ethernet: bulk OUT of %zu bytes failed (failure #%u)\n", off, fTxFailures);
		}
		return kIOReturnOutputDropped;
	}
	return kIOReturnOutputSuccess;
}

void
LAN7800Ethernet::inputFrame(const uint8_t *data, uint32_t len)
{
	if (fInterface == NULL) {
		return;
	}
	mbuf_t m = allocatePacket(len);
	if (m == NULL) {
		return;
	}
	mbuf_copyback(m, 0, len, data, MBUF_DONTWAIT);
	fInterface->inputPacket(m, len);
}

bool
LAN7800Ethernet::pollReceive(void)
{
	if (fRxBuffer == NULL || fInterface == NULL) {
		return false;
	}
	uint32_t actual = fTransport->bulkIn(fRxBuffer, kRxBufferSize);
	// "There is always a zero length frame after bringing the interface
	// up" - anything shorter than one header and an FCS carries nothing.
	if (actual < 4 + 4) {
		return false;
	}
	// muge_bulk_read_callback(): frames packed back to back, each header
	// 4-byte aligned from the start of the transfer: RX_CMD_A (4),
	// RX_CMD_B (4), RX_CMD_C (2), then RX_CMD_A.len bytes of frame
	// including its 4-byte FCS. Every step bounds-checked against `actual`.
	uint32_t off = 0;
	bool got = false;
	while (off < actual) {
		off = (off + 3) & ~3U;
		if (off + 10 > actual) {
			break;
		}
		uint32_t a = le32(fRxBuffer + off);
		off += 10;
		uint32_t len = a & kRxCmdALenMask;
		if (len > actual - off) {
			break;
		}
		if (a & kRxCmdARed) {
			if (fRxErrors++ < 8) {
				IOLog("LAN7800Ethernet: RX error, RX_CMD_A %#x\n", a);
			}
		} else if (len >= 14 + 4) {      // ETHER_HDR_LEN + FCS
			inputFrame(fRxBuffer + off, len - 4);
			got = true;
		}
		off += len;
	}
	return got;
}

void
LAN7800Ethernet::rxTimerFired(OSObject *owner, IOTimerEventSource *sender)
{
	// As SMSC9514Ethernet::rxTimerFired(): re-arm at once after a transfer
	// that carried frames, bounded by kRxBurstCap, else back off.
	LAN7800Ethernet *self = OSDynamicCast(LAN7800Ethernet, owner);
	if (self == NULL) {
		return;
	}
	bool got = self->pollReceive();
	if (got && self->fRxBurstCount < kRxBurstCap) {
		self->fRxBurstCount++;
		sender->setTimeoutMS(0);
	} else {
		self->fRxBurstCount = 0;
		sender->setTimeoutMS(kRxPollIntervalMS * (UInt32)debug_cpu_performance_degradation_factor);
	}
}
