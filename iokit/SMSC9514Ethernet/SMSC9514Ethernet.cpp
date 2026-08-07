#include "SMSC9514Ethernet.h"

#include <IOKit/IOLib.h>

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
	fLinkUp = false;
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
	for (int i = 0; i < 1000; i++) {
		uint32_t v;
		if (!fTransport->readRegister(kRegPmCtl, &v)) {
			return false;
		}
		if (!(v & kPmCtlPhyRst)) {
			break;
		}
		IODelay(10);
	}

	// Enable MAC receiver/transmitter and the corresponding FIFO paths.
	// Promiscuous/multicast filtering left at defaults (off) - packet
	// filter methods below are real but this reset sequence doesn't yet
	// apply whatever the current filter state should be at bring-up.
	uint32_t macCr = kMacCrTxEn | kMacCrRxEn;
	if (!fTransport->writeRegister(kRegMacCr, macCr)) {
		return false;
	}
	if (!fTransport->writeRegister(kRegTxCfg, kTxCfgOn)) {
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

	// No EEPROM-backed MAC address read implemented (E2P_CMD/E2P_DATA,
	// smsc95xx.c's smsc95xx_read_eeprom) - locally-administered address
	// used as a placeholder until that's added.
	fMACAddress.bytes[0] = 0x02;
	fMACAddress.bytes[1] = 0x00;
	fMACAddress.bytes[2] = 0x00;
	fMACAddress.bytes[3] = 0x00;
	fMACAddress.bytes[4] = 0x00;
	fMACAddress.bytes[5] = 0x01;
	if (!programMACAddress(&fMACAddress)) {
		IOLog("SMSC9514Ethernet: failed to program MAC address\n");
		return false;
	}

	registerService();
	return true;
}

void
SMSC9514Ethernet::stop(IOService *provider)
{
	super::stop(provider);
}

void
SMSC9514Ethernet::free(void)
{
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
SMSC9514Ethernet::getPacketFilters(const OSSymbol * /*group*/, UInt32 *filters) const
{
	*filters = kIOPacketFilterUnicast | kIOPacketFilterBroadcast | kIOPacketFilterMulticast;
	return kIOReturnSuccess;
}

IOReturn
SMSC9514Ethernet::enablePacketFilter(const OSSymbol * /*group*/, UInt32 /*aFilter*/,
    UInt32 /*enabledFilters*/, IOOptionBits /*options*/)
{
	// TODO: translate into MAC_CR PRMS/MCPAS bits via fTransport. Not
	// wired up yet - see resetChip()'s comment on filter defaults.
	return kIOReturnSuccess;
}

IOReturn
SMSC9514Ethernet::disablePacketFilter(const OSSymbol * /*group*/, UInt32 /*aFilter*/,
    UInt32 /*enabledFilters*/, IOOptionBits /*options*/)
{
	return kIOReturnSuccess;
}

IOReturn
SMSC9514Ethernet::enable(IONetworkInterface *interface)
{
	fInterface = interface;
	fLinkUp = true; // no real link-state/PHY-status polling wired up yet
	return kIOReturnSuccess;
}

IOReturn
SMSC9514Ethernet::disable(IONetworkInterface * /*interface*/)
{
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
