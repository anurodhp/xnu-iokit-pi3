#include "SMSC9514Ethernet.h"
#include "BCM2837Mailbox.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/network/IONetworkMedium.h>

#define super IOEthernetController

OSDefineMetaClassAndStructors(SMSC9514Ethernet, IOEthernetController);

bool
SMSC9514Ethernet::init(OSDictionary *properties, SMSCUSBTransport *transport)
{
	if (!super::init(properties)) {
		return false;
	}
	fTransport = transport;
	fOutputQueue = NULL;
	fInterface = NULL;
	fNetif = NULL;
	fLinkUp = false;
	fLinkTimer = NULL;
	fRxTimer = NULL;
	fRxBuffer = NULL;
	bzero(fMediumTable, sizeof(fMediumTable));
	bzero(&fMACAddress, sizeof(fMACAddress));
	return true;
}

bool
SMSC9514Ethernet::mdioWait(void)
{
	// UNVERIFIED timeout: smsc95xx.c polls with usleep/retry rather than a
	// fixed spin count; this driver can't run yet regardless (no
	// transport), so exact timing tuning is deferred until it can be
	// tested against real (or emulated) hardware.
	for (int i = 0; i < 1000; i++) {
		uint32_t v;
		if (!fTransport->readRegister(kRegMiiAddr, &v)) {
			return false;
		}
		if (!(v & kMiiAddrBusy)) {
			return true;
		}
		IODelay(10);
	}
	return false;
}

bool
SMSC9514Ethernet::mdioRead(uint8_t phyAddr, uint8_t regAddr, uint16_t *value)
{
	if (!mdioWait()) {
		return false;
	}
	uint32_t addr = ((uint32_t)phyAddr << 11) | ((uint32_t)regAddr << 6) | kMiiAddrBusy;
	if (!fTransport->writeRegister(kRegMiiAddr, addr)) {
		return false;
	}
	if (!mdioWait()) {
		return false;
	}
	uint32_t data;
	if (!fTransport->readRegister(kRegMiiData, &data)) {
		return false;
	}
	*value = (uint16_t)data;
	return true;
}

bool
SMSC9514Ethernet::mdioWrite(uint8_t phyAddr, uint8_t regAddr, uint16_t value)
{
	if (!mdioWait()) {
		return false;
	}
	if (!fTransport->writeRegister(kRegMiiData, value)) {
		return false;
	}
	uint32_t addr = ((uint32_t)phyAddr << 11) | ((uint32_t)regAddr << 6) | kMiiAddrBusy | kMiiAddrWrite;
	if (!fTransport->writeRegister(kRegMiiAddr, addr)) {
		return false;
	}
	return mdioWait();
}

bool
SMSC9514Ethernet::resetChip(void)
{
	if (!fTransport->writeRegister(kRegHwCfg, kHwCfgLRst)) {
		return false;
	}
	bool cleared = false;
	for (int i = 0; i < 1000; i++) {
		uint32_t v;
		if (!fTransport->readRegister(kRegHwCfg, &v)) {
			return false;
		}
		if (!(v & kHwCfgLRst)) {
			cleared = true;
			break;
		}
		IODelay(10);
	}
	if (!cleared) {
		IOLog("SMSC9514Ethernet: lite reset never cleared\n");
		return false;
	}

	if (!fTransport->writeRegister(kRegPmCtl, kPmCtlPhyRst)) {
		return false;
	}
	bool phyCleared = false;
	for (int i = 0; i < 1000; i++) {
		uint32_t v;
		if (!fTransport->readRegister(kRegPmCtl, &v)) {
			return false;
		}
		if (!(v & kPmCtlPhyRst)) {
			phyCleared = true;
			break;
		}
		IODelay(10);
	}
	if (!phyCleared) {
		// Real bug fixed here (docs/backlog.md item 2): this loop used to
		// fall through and continue as if the reset had cleared, same
		// mistake the lite-reset loop above it doesn't make. Matters more
		// now than when first flagged - the MDIO reads/writes right below
		// this (auto-negotiation restart) talk to the PHY this reset is
		// supposed to have brought out of reset; proceeding on a timeout
		// would make their failures much more confusing to diagnose.
		IOLog("SMSC9514Ethernet: PHY reset never cleared\n");
		return false;
	}

	// Enable MAC receiver/transmitter and the corresponding FIFO paths.
	// Promiscuous/multicast filtering correctly starts at defaults (off)
	// here - setMulticastMode()/setPromiscuousMode() are real read-modify-
	// write over whatever resetChip() leaves MAC_CR at, and the network
	// stack (IOEthernetInterface) re-applies whichever filters it needs
	// once the interface actually comes up, so there is nothing for reset
	// to pre-apply.
	uint32_t macCr = kMacCrTxEn | kMacCrRxEn;
	if (!fTransport->writeRegister(kRegMacCr, macCr)) {
		return false;
	}
	if (!fTransport->writeRegister(kRegTxCfg, kTxCfgOn)) {
		return false;
	}

	/*
	 * Explicitly (re)start auto-negotiation rather than relying on
	 * whatever the PHY defaulted to out of the PM_CTL PHY_RST pulse
	 * above. The PHY_RST alone is very likely enough on its own (most
	 * PHYs including this one default ANENABLE=1 out of reset), but
	 * smsc95xx.c's own smsc95xx_reset() does this explicitly too
	 * (mii_nway_restart()) instead of assuming reset defaults, and
	 * that's the only real reference this driver has for LAN9514 PHY
	 * bring-up - mirror it rather than hope.
	 */
	uint16_t bmcr;
	if (!mdioRead(kMiiPhyAddr, kMiiBmcr, &bmcr)) {
		IOLog("SMSC9514Ethernet: PHY BMCR read failed\n");
		return false;
	}
	if (!mdioWrite(kMiiPhyAddr, kMiiBmcr, bmcr | kBmcrAnEnable | kBmcrAnRestart)) {
		IOLog("SMSC9514Ethernet: PHY auto-negotiation restart failed\n");
		return false;
	}

	return true;
}

bool
SMSC9514Ethernet::programMACAddress(const IOEthernetAddress *addr)
{
	uint32_t addrL = (uint32_t)addr->bytes[0] | ((uint32_t)addr->bytes[1] << 8) |
	    ((uint32_t)addr->bytes[2] << 16) | ((uint32_t)addr->bytes[3] << 24);
	uint32_t addrH = (uint32_t)addr->bytes[4] | ((uint32_t)addr->bytes[5] << 8);
	return fTransport->writeRegister(kRegAddrL, addrL) &&
	    fTransport->writeRegister(kRegAddrH, addrH);
}

bool
SMSC9514Ethernet::eepromWait(void)
{
	for (int i = 0; i < 1000; i++) {
		uint32_t v;
		if (!fTransport->readRegister(kRegE2pCmd, &v)) {
			return false;
		}
		if (v & kE2pCmdTimeout) {
			// The chip itself reports "no EEPROM" this way, not via a
			// transport-level failure - a genuinely absent EEPROM is the
			// common case on this board (see readEEPROM()'s caller).
			return false;
		}
		if (!(v & kE2pCmdBusy)) {
			return true;
		}
		IODelay(10);
	}
	return false;
}

bool
SMSC9514Ethernet::readEEPROM(uint8_t offset, uint8_t *data, uint32_t length)
{
	if (!eepromWait()) {
		return false;
	}
	for (uint32_t i = 0; i < length; i++) {
		uint32_t cmd = kE2pCmdBusy | kE2pCmdRead | (((uint32_t)offset + i) & kE2pCmdAddrMask);
		if (!fTransport->writeRegister(kRegE2pCmd, cmd)) {
			return false;
		}
		if (!eepromWait()) {
			return false;
		}
		uint32_t val;
		if (!fTransport->readRegister(kRegE2pData, &val)) {
			return false;
		}
		data[i] = (uint8_t)(val & 0xFF);
	}
	return true;
}

bool
SMSC9514Ethernet::createMediumTables(void)
{
	static const struct {
		UInt32       index;
		IOMediumType type;
		UInt64       speed;
	} kMedia[kMediumCount] = {
		{ kMediumIndexAuto,          kIOMediumEthernetAuto,                                    0 },
		{ kMediumIndex10HalfDuplex,  kIOMediumEthernet10BaseT,                                 10 * 1000000 },
		{ kMediumIndex10FullDuplex,  kIOMediumEthernet10BaseT   | kIOMediumOptionFullDuplex,    10 * 1000000 },
		{ kMediumIndex100HalfDuplex, kIOMediumEthernet100BaseTX,                                100 * 1000000 },
		{ kMediumIndex100FullDuplex, kIOMediumEthernet100BaseTX | kIOMediumOptionFullDuplex,    100 * 1000000 },
	};

	OSDictionary *dict = OSDictionary::withCapacity(kMediumCount);
	if (dict == NULL) {
		return false;
	}

	bool ok = true;
	for (UInt32 i = 0; i < kMediumCount && ok; i++) {
		IONetworkMedium *medium = IONetworkMedium::medium(
		    kMedia[i].type, kMedia[i].speed, 0, kMedia[i].index);
		if (medium == NULL || !IONetworkMedium::addMedium(dict, medium)) {
			ok = false;
			if (medium != NULL) {
				medium->release();
			}
			break;
		}
		// addMedium() above retained its own reference for the
		// dictionary; this one is kept for updateLinkStatus() to hand
		// straight to setLinkStatus() without a dictionary lookup each
		// time. Released in stop().
		fMediumTable[kMedia[i].index] = medium;
	}

	if (ok) {
		ok = publishMediumDictionary(dict);
	}
	dict->release(); // publishMediumDictionary() retains what it needs

	return ok;
}

void
SMSC9514Ethernet::updateLinkStatus(void)
{
	if (fTransport == NULL) {
		return;
	}

	// BMSR's Link Status bit latches low - discard this first read (it
	// may reflect a transient drop already cleared) and trust the second.
	uint16_t bmsr;
	if (!mdioRead(kMiiPhyAddr, kMiiBmsr, &bmsr) ||
	    !mdioRead(kMiiPhyAddr, kMiiBmsr, &bmsr)) {
		return;
	}

	if (!(bmsr & kBmsrLinkStatus)) {
		if (fLinkUp) {
			fLinkUp = false;
			setLinkStatus(kIONetworkLinkValid, NULL);
		}
		return;
	}

	// Link is up. Resolve the negotiated speed/duplex the standard IEEE
	// 802.3 way: intersect what we advertised (ANAR) with what the link
	// partner advertised (ANLPAR), then pick the highest-priority bit set
	// in the intersection (100Full > 100Half > 10Full > 10Half). Falls
	// back to Auto/unknown-duplex if for any reason the intersection is
	// empty (e.g. a partner that doesn't auto-negotiate at all).
	uint16_t anar, anlpar;
	UInt32   mediumIndex = kMediumIndexAuto;
	UInt64   speed = 0;
	if (mdioRead(kMiiPhyAddr, kMiiAnar, &anar) &&
	    mdioRead(kMiiPhyAddr, kMiiAnlpar, &anlpar)) {
		uint16_t common = anar & anlpar;
		if (common & kAnar100Full) {
			mediumIndex = kMediumIndex100FullDuplex;
			speed = 100 * 1000000;
		} else if (common & kAnar100Half) {
			mediumIndex = kMediumIndex100HalfDuplex;
			speed = 100 * 1000000;
		} else if (common & kAnar10Full) {
			mediumIndex = kMediumIndex10FullDuplex;
			speed = 10 * 1000000;
		} else if (common & kAnar10Half) {
			mediumIndex = kMediumIndex10HalfDuplex;
			speed = 10 * 1000000;
		}
	}

	fLinkUp = true;
	setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive,
	    fMediumTable[mediumIndex], speed);
}

void
SMSC9514Ethernet::linkTimerFired(OSObject *owner, IOTimerEventSource *sender)
{
	SMSC9514Ethernet *self = OSDynamicCast(SMSC9514Ethernet, owner);
	if (self == NULL) {
		return;
	}
	self->updateLinkStatus();
	sender->setTimeoutMS(kLinkPollIntervalMS);
}

bool
SMSC9514Ethernet::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}

	if (fTransport == NULL) {
		IOLog("SMSC9514Ethernet: no USB transport - see header, DWC2 host controller support doesn't exist yet\n");
		return false;
	}

	if (!resetChip()) {
		IOLog("SMSC9514Ethernet: chip reset failed\n");
		return false;
	}

	/*
	 * Try the chip's EEPROM for a real, board-programmed MAC address
	 * first (smsc95xx.c's smsc95xx_read_eeprom(), EEPROM_MAC_OFFSET, 6
	 * bytes). Most Raspberry Pi boards ship this LAN9514 with no EEPROM
	 * populated at all - the vendor assigns the MAC a different way -
	 * so eepromWait() returning false via E2P_CMD_TIMEOUT_ is a real,
	 * expected board configuration, not a bug. Also reject a read that
	 * "succeeds" but comes back looking uninitialized: all-zero, all-FF,
	 * or with the multicast bit set are not values any EEPROM would
	 * legitimately contain for a station address.
	 *
	 * When the EEPROM genuinely isn't usable - i.e. on essentially every
	 * real Pi - the next source is the VideoCore firmware over the
	 * mailbox property channel (BCM2837Mailbox), which is where the
	 * Foundation's own boot chain gets this board's b8:27:eb:xx:xx:xx
	 * address from. The locally-administered placeholder is now only the
	 * third and last resort, reached when neither works.
	 */
	uint8_t eeprom[6];
	bool haveEepromMac = readEEPROM(kEepromMacOffset, eeprom, sizeof(eeprom));
	if (haveEepromMac) {
		bool allZero = true, allFF = true;
		for (int i = 0; i < 6; i++) {
			if (eeprom[i] != 0x00) {
				allZero = false;
			}
			if (eeprom[i] != 0xFF) {
				allFF = false;
			}
		}
		if (allZero || allFF || (eeprom[0] & 0x01)) {
			haveEepromMac = false;
		}
	}

	if (haveEepromMac) {
		for (int i = 0; i < 6; i++) {
			fMACAddress.bytes[i] = eeprom[i];
		}
		IOLog("SMSC9514Ethernet: MAC address from EEPROM: "
		      "%02x:%02x:%02x:%02x:%02x:%02x\n",
		    fMACAddress.bytes[0], fMACAddress.bytes[1], fMACAddress.bytes[2],
		    fMACAddress.bytes[3], fMACAddress.bytes[4], fMACAddress.bytes[5]);
	} else if (BCM2837Mailbox::getBoardMACAddress(fMACAddress.bytes)) {
		/*
		 * No EEPROM, which is the normal Raspberry Pi configuration -
		 * so ask the VideoCore firmware the way the Foundation's own
		 * firmware/u-boot/Linux stack does (property tag 0x00010003,
		 * "get board MAC address"; BCM2837Mailbox falls back to
		 * deriving b8:27:eb + the low three bytes of the board serial
		 * if the firmware only answers tag 0x00010004). This is a real
		 * per-board address, not a constant - see DAR-144 for why the
		 * old unconditional placeholder was a real bug and not just
		 * cosmetic.
		 */
		IOLog("SMSC9514Ethernet: MAC address from VideoCore mailbox: "
		      "%02x:%02x:%02x:%02x:%02x:%02x\n",
		    fMACAddress.bytes[0], fMACAddress.bytes[1], fMACAddress.bytes[2],
		    fMACAddress.bytes[3], fMACAddress.bytes[4], fMACAddress.bytes[5]);
	} else {
		/*
		 * Last resort only: no EEPROM AND no usable answer from the
		 * VideoCore (mailbox timeout, or firmware that implements
		 * neither tag). Still better than refusing to start - an
		 * interface with a colliding address is more useful than no
		 * interface - but it is a real, logged degradation, not a
		 * normal outcome.
		 */
		IOLog("SMSC9514Ethernet: no usable EEPROM MAC and no VideoCore "
		      "mailbox answer - falling back to a locally-administered "
		      "placeholder, every board will present the same address\n");
		fMACAddress.bytes[0] = 0x02;
		fMACAddress.bytes[1] = 0x00;
		fMACAddress.bytes[2] = 0x00;
		fMACAddress.bytes[3] = 0x00;
		fMACAddress.bytes[4] = 0x00;
		fMACAddress.bytes[5] = 0x01;
	}
	if (!programMACAddress(&fMACAddress)) {
		IOLog("SMSC9514Ethernet: failed to program MAC address\n");
		return false;
	}

	if (!createMediumTables()) {
		IOLog("SMSC9514Ethernet: createMediumTables failed\n");
		return false;
	}

	/*
	 * Real link-state monitoring (see updateLinkStatus()'s header
	 * comment on why this is polled, not interrupt-driven). The timer is
	 * only created/attached here; enable()/disable() start and stop it,
	 * matching those methods' role as "the interface is/isn't in active
	 * use right now" rather than start()/stop()'s "the driver object
	 * exists at all".
	 */
	IOWorkLoop *workLoop = getWorkLoop();
	if (workLoop == NULL) {
		IOLog("SMSC9514Ethernet: no workloop\n");
		return false;
	}
	fLinkTimer = IOTimerEventSource::timerEventSource(this, &SMSC9514Ethernet::linkTimerFired);
	if (fLinkTimer == NULL || workLoop->addEventSource(fLinkTimer) != kIOReturnSuccess) {
		IOLog("SMSC9514Ethernet: link timer setup failed\n");
		return false;
	}

	// RX poll timer - see the header comment on why this has to be a
	// timer on this same gated workloop rather than a dedicated thread.
	fRxBuffer = (uint8_t *)IOMalloc(kRxBufferSize);
	if (fRxBuffer == NULL) {
		IOLog("SMSC9514Ethernet: RX buffer allocation failed\n");
		return false;
	}
	fRxTimer = IOTimerEventSource::timerEventSource(this, &SMSC9514Ethernet::rxTimerFired);
	if (fRxTimer == NULL || workLoop->addEventSource(fRxTimer) != kIOReturnSuccess) {
		IOLog("SMSC9514Ethernet: RX timer setup failed\n");
		return false;
	}

	/*
	 * Real Apple convention (IONetworkController.cpp's own comment on
	 * attachInterface(): "Drivers will usually call this method from their
	 * start() implementation, after they are ready to process client
	 * requests"). This is the step that was missing before: without it,
	 * the chip-level logic above is real and now genuinely reachable over
	 * USB, but nothing ever creates the IOEthernetInterface the BSD
	 * network stack (INET, phase-6b-inet) attaches to - the driver would
	 * publish an IOService and nothing more, invisible to ifconfig no
	 * matter how correct the register protocol underneath it is.
	 * createInterface() is not overridden here because
	 * IOEthernetController::createInterface() already does exactly what
	 * this chip needs - allocate a plain IOEthernetInterface and init() it
	 * against `this` - so attachInterface() is left to call it via its own
	 * default path rather than duplicating that.
	 */
	if (!attachInterface((IONetworkInterface **)&fNetif, true)) {
		IOLog("SMSC9514Ethernet: attachInterface failed\n");
		return false;
	}

	registerService();
	return true;
}

void
SMSC9514Ethernet::stop(IOService *provider)
{
	// Undoes the workloop attachment start() made, same idea as
	// detachInterface() below undoing attachInterface() - stop() reverses
	// everything start() did that's visible outside this object, before
	// free() does the final (and here, purely defensive/idempotent)
	// pointer cleanup.
	if (fLinkTimer != NULL) {
		fLinkTimer->cancelTimeout();
		IOWorkLoop *wl = getWorkLoop();
		if (wl != NULL) {
			wl->removeEventSource(fLinkTimer);
		}
		fLinkTimer->release();
		fLinkTimer = NULL;
	}

	if (fRxTimer != NULL) {
		fRxTimer->cancelTimeout();
		IOWorkLoop *wl = getWorkLoop();
		if (wl != NULL) {
			wl->removeEventSource(fRxTimer);
		}
		fRxTimer->release();
		fRxTimer = NULL;
	}
	if (fRxBuffer != NULL) {
		IOFree(fRxBuffer, kRxBufferSize);
		fRxBuffer = NULL;
	}

	if (fNetif != NULL) {
		detachInterface(fNetif);
		fNetif = NULL;
	}
	super::stop(provider);
}

void
SMSC9514Ethernet::free(void)
{
	// Defensive, not the primary teardown path: stop() above already
	// clears fLinkTimer on any boot where start() succeeded. This only
	// matters if start() failed partway through (e.g. createMediumTables()
	// or the timer setup itself failing) - BCM2837DWC2.cpp's cleanup for
	// that case calls release() directly, never stop(), the same way it
	// already does for SMSC9514Ethernet itself.
	if (fLinkTimer != NULL) {
		fLinkTimer->cancelTimeout();
		fLinkTimer->release();
		fLinkTimer = NULL;
	}
	if (fRxTimer != NULL) {
		fRxTimer->cancelTimeout();
		fRxTimer->release();
		fRxTimer = NULL;
	}
	if (fRxBuffer != NULL) {
		IOFree(fRxBuffer, kRxBufferSize);
		fRxBuffer = NULL;
	}
	for (UInt32 i = 0; i < kMediumCount; i++) {
		if (fMediumTable[i] != NULL) {
			fMediumTable[i]->release();
			fMediumTable[i] = NULL;
		}
	}
	super::free();
}

IOReturn
SMSC9514Ethernet::getHardwareAddress(IOEthernetAddress *addrP)
{
	*addrP = fMACAddress;
	return kIOReturnSuccess;
}

IOReturn
SMSC9514Ethernet::setHardwareAddress(const void *addr, UInt32 addrBytes)
{
	if (addrBytes != sizeof(IOEthernetAddress)) {
		return kIOReturnBadArgument;
	}
	const IOEthernetAddress *ea = (const IOEthernetAddress *)addr;
	if (fTransport != NULL && !programMACAddress(ea)) {
		return kIOReturnIOError;
	}
	fMACAddress = *ea;
	return kIOReturnSuccess;
}

IOReturn
SMSC9514Ethernet::getPacketFilters(const OSSymbol *group, UInt32 *filters) const
{
	// Real support is unicast/broadcast/multicast/promiscuous (see
	// setMulticastMode()/setPromiscuousMode() below), and only for the
	// standard RX filter group - this chip has no wake-on-LAN support at
	// all, so any other group (e.g. gIOEthernetWakeOnLANFilterGroup, which
	// publishProperties() also queries this through) must report 0,
	// matching IOEthernetController::getPacketFilters()'s own real
	// default rather than misreporting WOL support this chip doesn't have.
	if (group != gIONetworkFilterGroup) {
		*filters = 0;
		return kIOReturnSuccess;
	}
	*filters = kIOPacketFilterUnicast | kIOPacketFilterBroadcast |
	    kIOPacketFilterMulticast | kIOPacketFilterPromiscuous;
	return kIOReturnSuccess;
}

IOReturn
SMSC9514Ethernet::setMulticastMode(bool active)
{
	if (fTransport == NULL) {
		return kIOReturnNotReady;
	}
	uint32_t macCr;
	if (!fTransport->readRegister(kRegMacCr, &macCr)) {
		return kIOReturnIOError;
	}
	// MAC_CR_MCPAS_ ("pass all multicast") rather than the hash-table
	// filter (MAC_CR_HFILT_/HASHH/HASHL, unimplemented) - coarser than a
	// real per-address filter, but a real, correct superset: anything a
	// hash filter would have accepted, this accepts too, which is exactly
	// what IPv6 ND/mDNS need to keep working.
	macCr = active ? (macCr | kMacCrMcPas) : (macCr & ~(uint32_t)kMacCrMcPas);
	if (!fTransport->writeRegister(kRegMacCr, macCr)) {
		return kIOReturnIOError;
	}
	return kIOReturnSuccess;
}

IOReturn
SMSC9514Ethernet::setPromiscuousMode(bool active)
{
	if (fTransport == NULL) {
		return kIOReturnNotReady;
	}
	uint32_t macCr;
	if (!fTransport->readRegister(kRegMacCr, &macCr)) {
		return kIOReturnIOError;
	}
	macCr = active ? (macCr | kMacCrPrmS) : (macCr & ~(uint32_t)kMacCrPrmS);
	if (!fTransport->writeRegister(kRegMacCr, macCr)) {
		return kIOReturnIOError;
	}
	return kIOReturnSuccess;
}

IOReturn
SMSC9514Ethernet::setMulticastList(IOEthernetAddress * /*addrs*/, UInt32 /*count*/)
{
	// setMulticastMode(true) above already puts the chip in "pass all
	// multicast" mode whenever the multicast filter is active at all, so
	// there is no narrower per-address list to program - this chip has no
	// hash-table filtering implemented. Real success, not a stub: every
	// address in the list the caller would have programmed is already
	// guaranteed to be received.
	return kIOReturnSuccess;
}

IOReturn
SMSC9514Ethernet::enable(IONetworkInterface *interface)
{
	fInterface = interface;
	updateLinkStatus(); // real initial state, not a hardcoded guess
	if (fLinkTimer != NULL) {
		fLinkTimer->setTimeoutMS(kLinkPollIntervalMS);
	}
	if (fRxTimer != NULL) {
		fRxTimer->setTimeoutMS(kRxPollIntervalMS);
	}
	return kIOReturnSuccess;
}

IOReturn
SMSC9514Ethernet::disable(IONetworkInterface * /*interface*/)
{
	if (fLinkTimer != NULL) {
		fLinkTimer->cancelTimeout();
	}
	if (fRxTimer != NULL) {
		fRxTimer->cancelTimeout();
	}
	fLinkUp = false;
	fInterface = NULL;
	return kIOReturnSuccess;
}

UInt32
SMSC9514Ethernet::outputPacket(mbuf_t m, void * /*param*/)
{
	if (fTransport == NULL) {
		freePacket(m);
		return kIOReturnOutputDropped;
	}

	// TX framing per smsc95xx.c: each USB bulk-out transfer is prefixed
	// with two 32-bit command words (TX_CMD_A, TX_CMD_B) describing the
	// frame, matching TX_CMD_A_FIRST_SEG_/LAST_SEG_/BUF_SIZE_ and
	// TX_CMD_B_FRAME_LENGTH_ from smsc95xx.h.
	size_t frameLen = 0;
	for (mbuf_t n = m; n != NULL; n = mbuf_next(n)) {
		frameLen += mbuf_len(n);
	}

	size_t bufSize = frameLen + 8;
	uint8_t *buf = (uint8_t *)IOMalloc(bufSize);
	if (buf == NULL) {
		freePacket(m);
		return kIOReturnOutputDropped;
	}

	uint32_t cmdA = 0x2000 /* FIRST_SEG */ | 0x1000 /* LAST_SEG */ | (uint32_t)(frameLen & 0x7FF);
	uint32_t cmdB = (uint32_t)(frameLen & 0x7FF);
	*(uint32_t *)(buf + 0) = cmdA;
	*(uint32_t *)(buf + 4) = cmdB;

	size_t off = 8;
	for (mbuf_t n = m; n != NULL; n = mbuf_next(n)) {
		size_t len = mbuf_len(n);
		bcopy(mbuf_data(n), buf + off, len);
		off += len;
	}

	uint32_t sent = fTransport->bulkOut(buf, (uint32_t)bufSize);
	IOFree(buf, bufSize);
	freePacket(m);

	return (sent == bufSize) ? (UInt32)kIOReturnOutputSuccess : (UInt32)kIOReturnOutputDropped;
}

void
SMSC9514Ethernet::inputPacketAvailable(void *frameData, uint32_t frameLen)
{
	// RX framing per smsc95xx.c: each bulk-in transfer is prefixed with a
	// 32-bit RX_STS word (error/length flags) before the raw frame bytes -
	// see RX_STS_* in smsc95xx.h. Caller (the not-yet-existing USB
	// transport driver) is expected to have already stripped/validated
	// that word; this just wraps the frame bytes and hands them to the
	// network stack.
	if (fInterface == NULL) {
		return;
	}
	mbuf_t m = allocatePacket(frameLen);
	if (m == NULL) {
		return;
	}
	mbuf_copyback(m, 0, frameLen, frameData, MBUF_DONTWAIT);
	fInterface->inputPacket(m, frameLen);
}

void
SMSC9514Ethernet::pollReceive(void)
{
	if (fTransport == NULL || fRxBuffer == NULL || fInterface == NULL) {
		return;
	}

	// bulkIn() is a short-timeout poll (see SMSC9514USBTransport::bulkIn())
	// - 0 back just means nothing was queued this tick, the normal case
	// for an idle link, not a failure.
	uint32_t actual = fTransport->bulkIn(fRxBuffer, kRxBufferSize);
	if (actual < 4) {
		return;
	}

	/*
	 * One bulk-IN transfer can pack several frames, each preceded by a
	 * 32-bit RX_STS word and followed by its FCS, padded so the next
	 * status word starts 4-byte aligned (smsc95xx.c's rx_fixup loop).
	 * Every step is bounds-checked against `actual`: a corrupt length
	 * field in a status word would otherwise walk straight off the end
	 * of fRxBuffer.
	 */
	uint32_t offset = 0;
	while (offset + 4 <= actual) {
		uint32_t sts = (uint32_t)fRxBuffer[offset] | ((uint32_t)fRxBuffer[offset + 1] << 8) |
		    ((uint32_t)fRxBuffer[offset + 2] << 16) | ((uint32_t)fRxBuffer[offset + 3] << 24);
		offset += 4;

		uint32_t frameLen = (sts & kRxStsFrameLenMask) >> kRxStsFrameLenShift;
		if (frameLen == 0 || offset + frameLen > actual) {
			// Either the end of the packed frames or a malformed
			// length. Either way there is nothing more to safely read.
			break;
		}

		if (!(sts & kRxStsErrorSummary)) {
			// frameLen includes the 4-byte FCS, which the network stack
			// does not want. Guard the subtraction: a runt claiming
			// fewer than 4 bytes would underflow into a huge length.
			if (frameLen > 4) {
				inputPacketAvailable(fRxBuffer + offset, frameLen - 4);
			}
		}

		// Next status word is 4-byte aligned.
		offset += (frameLen + 3) & ~3U;
	}
}

void
SMSC9514Ethernet::rxTimerFired(OSObject *owner, IOTimerEventSource *sender)
{
	SMSC9514Ethernet *self = OSDynamicCast(SMSC9514Ethernet, owner);
	if (self == NULL) {
		return;
	}
	self->pollReceive();
	sender->setTimeoutMS(kRxPollIntervalMS);
}
