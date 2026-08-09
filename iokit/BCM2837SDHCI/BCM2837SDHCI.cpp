#include "BCM2837SDHCI.h"

#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/storage/IOMedia.h>
#include <libkern/OSByteOrder.h>
#include <string.h>

#define super IOBlockStorageDevice

OSDefineMetaClassAndStructors(BCM2837SDHCI, IOBlockStorageDevice);

IOService *
BCM2837SDHCI::probe(IOService *provider, SInt32 *score)
{
	IOService *result = super::probe(provider, score);
	if (result == NULL) {
		return NULL;
	}
	const char *name = provider->getName();
	if (name == NULL || strcmp(name, "sdhci") != 0) {
		return NULL;
	}
	return result;
}

bool
BCM2837SDHCI::init(OSDictionary *properties)
{
	if (!super::init(properties)) {
		return false;
	}
	fRegMap = NULL;
	fRegBase = NULL;
	fCardPresent = false;
	fCardIsSDHC = false;
	fRCA = 0;
	fBlockCount = 0;
	fBlockSize = 512;

	fPendingIntStatus = 0;
	fInterruptLock = IOLockAlloc();
	if (fInterruptLock == NULL) {
		return false;
	}
	fStatusLock = IOSimpleLockAlloc();
	if (fStatusLock == NULL) {
		IOLockFree(fInterruptLock);
		fInterruptLock = NULL;
		return false;
	}
	return true;
}

uint8_t  BCM2837SDHCI::readReg8(uint32_t off)  { return *((volatile uint8_t  *)((uintptr_t)fRegBase + off)); }
uint16_t BCM2837SDHCI::readReg16(uint32_t off) { return *((volatile uint16_t *)((uintptr_t)fRegBase + off)); }
uint32_t BCM2837SDHCI::readReg32(uint32_t off) { return *((volatile uint32_t *)((uintptr_t)fRegBase + off)); }
void BCM2837SDHCI::writeReg8(uint32_t off, uint8_t v)   { *((volatile uint8_t  *)((uintptr_t)fRegBase + off)) = v; }
void BCM2837SDHCI::writeReg16(uint32_t off, uint16_t v) { *((volatile uint16_t *)((uintptr_t)fRegBase + off)) = v; }
void BCM2837SDHCI::writeReg32(uint32_t off, uint32_t v) { *((volatile uint32_t *)((uintptr_t)fRegBase + off)) = v; }

bool
BCM2837SDHCI::waitClear32(uint32_t offset, uint32_t mask, uint32_t timeoutUs)
{
	uint32_t waited = 0;
	while ((readReg32(offset) & mask) != 0) {
		if (waited >= timeoutUs) {
			return false;
		}
		IODelay(50);
		waited += 50;
	}
	return true;
}

bool
BCM2837SDHCI::pollUntil(bool (^predicate)(void), uint32_t timeoutUs)
{
	uint32_t waited = 0;
	while (!predicate()) {
		if (waited >= timeoutUs) {
			return false;
		}
		IODelay(50);
		waited += 50;
	}
	return true;
}

void
BCM2837SDHCI::interruptOccurred(OSObject *target, void * /*refCon*/, IOService * /*nub*/, int /*source*/)
{
	// Primary interrupt handler - runs at hardware interrupt context, same
	// role as Linux's sdhci_irq() top half (drivers/mmc/host/sdhci.c).
	// Deliberately minimal: no IOLog, no IOLockLock/IOLockSleep (both are
	// documented in IOKit/IOLocks.h as "may block and so should not be
	// called from interrupt level") - only IOSimpleLock (a real spinlock)
	// to touch fPendingIntStatus, and IOLockWakeup, which is safe here
	// because it's implemented as a bare thread_wakeup_prim() that never
	// touches the IOLock passed to it (confirmed in xnu's IOLocks.cpp).
	BCM2837SDHCI *self = OSDynamicCast(BCM2837SDHCI, target);
	if (self == NULL) {
		return;
	}

	uint32_t intmask = self->readReg32(kRegIntStatus);
	if (intmask == 0) {
		return;
	}

	// Ack exactly the bits we understand, mirroring sdhci_irq()'s
	// `mask = intmask & (SDHCI_INT_CMD_MASK|SDHCI_INT_DATA_MASK|...);
	// sdhci_writel(host, mask, SDHCI_INT_STATUS);` - not a blind
	// write-back of the whole register.
	uint32_t known = intmask & (kIntCmdComplete | kIntTransferDone |
	    kIntBufWriteRdy | kIntBufReadRdy | kIntErrorMask);
	if (known != 0) {
		self->writeReg32(kRegIntStatus, known);
	}

	// Any remaining bit is one this driver doesn't act on (e.g.
	// CARD_INSERT/REMOVE). Still ack it - left set with its SIGNAL_ENABLE
	// bit on, it would just keep re-asserting the interrupt line forever,
	// the same IRQ-storm reasoning sdhci_irq() calls out for its own
	// trailing "unexpected" cleanup.
	uint32_t unexpected = intmask & ~known;
	if (unexpected != 0) {
		self->writeReg32(kRegIntStatus, unexpected);
	}

	if (known != 0) {
		IOInterruptState istate = IOSimpleLockLockDisableInterrupt(self->fStatusLock);
		self->fPendingIntStatus |= known;
		IOSimpleLockUnlockEnableInterrupt(self->fStatusLock, istate);

		IOLockWakeup(self->fInterruptLock, (void *)&self->fPendingIntStatus, false);
	}
}

bool
BCM2837SDHCI::waitForIntStatus32(uint32_t mask, uint32_t timeoutUs, uint32_t *outStatus)
{
	uint64_t deadline;
	clock_interval_to_deadline(timeoutUs, kMicrosecondScale, &deadline);

	// Two different lock types are load-bearing here, not interchangeable:
	// fStatusLock (IOSimpleLock) is the only thing interruptOccurred() may
	// touch, since it runs at interrupt level; fInterruptLock (IOLock) is
	// a real mutex only ever used from this thread-context side, needed
	// because IOLockSleepDeadline() requires a lockable IOLock to
	// atomically unlock-and-sleep on. IOLockWakeup() bridges the two - it
	// doesn't actually take fInterruptLock (see interruptOccurred()), it
	// just targets the same wait event address.
	IOLockLock(fInterruptLock);
	uint32_t matched = 0;
	for (;;) {
		IOInterruptState istate = IOSimpleLockLockDisableInterrupt(fStatusLock);
		matched = fPendingIntStatus & mask;
		if (matched != 0) {
			fPendingIntStatus &= ~matched;
		}
		IOSimpleLockUnlockEnableInterrupt(fStatusLock, istate);

		if (matched != 0) {
			break;
		}

		int wr = IOLockSleepDeadline(fInterruptLock, (void *)&fPendingIntStatus, deadline, THREAD_UNINT);
		if (wr == THREAD_TIMED_OUT) {
			// One last check: the interrupt could have landed and called
			// IOLockWakeup() in the narrow window between our check above
			// and entering IOLockSleepDeadline(), in which case the bits
			// are sitting in fPendingIntStatus even though we timed out
			// waiting to be woken for them.
			istate = IOSimpleLockLockDisableInterrupt(fStatusLock);
			matched = fPendingIntStatus & mask;
			if (matched != 0) {
				fPendingIntStatus &= ~matched;
			}
			IOSimpleLockUnlockEnableInterrupt(fStatusLock, istate);
			break;
		}
	}
	IOLockUnlock(fInterruptLock);

	if (outStatus != NULL) {
		*outStatus = matched;
	}
	return matched != 0;
}

bool
BCM2837SDHCI::resetHost(void)
{
	// SDHCI_SOFTWARE_RESET: bit0 = reset all (CMD+DAT+misc).
	writeReg8(kRegSoftwareReset, 0x01);
	if (!pollUntil(^bool(void) { return (readReg8(kRegSoftwareReset) & 0x01) == 0; }, 100000)) {
		IOLog("BCM2837SDHCI: software reset did not clear\n");
		return false;
	}

	// INT_ENABLE arms status-bit reporting (readable via kRegIntStatus
	// regardless of whether an actual IRQ line fires); SIGNAL_ENABLE is the
	// subset that's also allowed to assert the real interrupt line to the
	// interrupt controller. Both are set to exactly the bits interruptOccurred()
	// understands - matches Linux's sdhci_irq() pattern of writing the same
	// `host->ier` value to both SDHCI_INT_ENABLE and SDHCI_SIGNAL_ENABLE.
	uint32_t enabledInts = kIntCmdComplete | kIntTransferDone |
	    kIntBufWriteRdy | kIntBufReadRdy | kIntErrorMask;
	writeReg32(kRegIntEnable, enabledInts);
	writeReg32(kRegSignalEnable, enabledInts);

	return true;
}

bool
BCM2837SDHCI::setClock(uint32_t targetHz)
{
	// missing_caps=true on this SoC (see header) - CAPABILITIES can't be
	// trusted for the base clock frequency. Rather than guess a specific
	// base clock value, use the maximum 8-bit divisor (SDCLK_FREQ_SEL,
	// bits [15:8]) for the ~400kHz identification-phase clock, which is
	// safe/conservative regardless of the true base clock on any plausible
	// BCM2837 VPU clock rate. For "data phase" targetHz, this is a
	// TODO - currently just reuses the same conservative divisor, which
	// means transfers run slower than necessary but should stay correct.
	writeReg16(kRegClockControl, 0x0000); // disable SD clock while reprogramming
	(void)targetHz; // see comment above - not yet used to pick a tighter divisor

	uint16_t clk = (0xFFU << 8) | 0x01 /* internal clock enable */;
	writeReg16(kRegClockControl, clk);

	if (!pollUntil(^bool(void) { return (readReg16(kRegClockControl) & 0x02) != 0; }, 100000)) {
		IOLog("BCM2837SDHCI: internal clock never stabilized\n");
		return false;
	}

	writeReg16(kRegClockControl, clk | 0x04 /* SD clock enable */);
	return true;
}

bool
BCM2837SDHCI::sendCommand(uint8_t cmdIndex, uint32_t arg, uint32_t respType, bool hasData, uint32_t resp[4])
{
	if (!waitClear32(kRegPresentState, kPresentCmdInhibit, 1000000)) {
		IOLog("BCM2837SDHCI: CMD_INHIBIT stuck before CMD%u\n", cmdIndex);
		return false;
	}
	if (hasData && !waitClear32(kRegPresentState, kPresentDatInhibit, 1000000)) {
		IOLog("BCM2837SDHCI: DAT_INHIBIT stuck before CMD%u\n", cmdIndex);
		return false;
	}

	writeReg32(kRegArgument, arg);

	uint16_t flags = respType & kCmdRespShortBusy; // low 2 bits
	if (respType == kCmdRespShort || respType == kCmdRespShortBusy) {
		flags |= kCmdCRCCheckEn | kCmdIndexCheckEn;
	} else if (respType == kCmdRespLong) {
		flags |= kCmdCRCCheckEn;
	}
	if (hasData) {
		flags |= kCmdDataPresent;
	}

	writeReg16(kRegCommand, (uint16_t)((cmdIndex << 8) | flags));

	uint32_t status;
	if (!waitForIntStatus32(kIntCmdComplete | kIntErrorMask, 1000000, &status)) {
		IOLog("BCM2837SDHCI: CMD%u timed out (no CMD_COMPLETE/error)\n", cmdIndex);
		return false;
	}
	if (status & kIntErrorMask) {
		IOLog("BCM2837SDHCI: CMD%u error, INT_STATUS=%#x\n", cmdIndex, status);
		return false;
	}

	if (resp != NULL) {
		if (respType == kCmdRespLong) {
			// Raw hardware order: RESPONSE+0x0..0xC = hw[0..3].
			uint32_t hw[4];
			hw[0] = readReg32(kRegResponse0 + 0x0);
			hw[1] = readReg32(kRegResponse0 + 0x4);
			hw[2] = readReg32(kRegResponse0 + 0x8);
			hw[3] = readReg32(kRegResponse0 + 0xC);
			// Linux's sdhci_read_rsp_136: resp[i] = readl(RESPONSE + (3-i)*4),
			// i.e. resp[0]=hw[3], resp[1]=hw[2], resp[2]=hw[1], resp[3]=hw[0].
			resp[0] = hw[3];
			resp[1] = hw[2];
			resp[2] = hw[1];
			resp[3] = hw[0];
			// Hardware strips the CRC7+stop bit (bottom 8 bits of the real
			// 128-bit CSD/CID), so shift left 8 and pull in the top 8 bits
			// of the next word, matching sdhci.c exactly (this SoC has no
			// SDHCI_QUIRK2_RSP_136_HAS_CRC).
			for (int i = 0; i < 4; i++) {
				resp[i] <<= 8;
				if (i != 3) {
					resp[i] |= resp[i + 1] >> 24;
				}
			}
		} else {
			resp[0] = readReg32(kRegResponse0);
		}
	}

	return true;
}

// Mirrors Linux's unstuff_bits() (drivers/mmc/core/mmc_ops.h) exactly:
// `start`/`size` are bit positions within the reconstructed 128-bit
// CSD/CID as numbered by the SD spec (bit 0 = LSB), and resp[] uses the
// same resp[0]=high 32 bits .. resp[3]=low 32 bits convention produced by
// sendCommand()'s R2 handling above.
static uint32_t
unstuffBits(const uint32_t resp[4], int start, int size)
{
	uint32_t mask = (size < 32) ? ((1U << size) - 1) : 0xFFFFFFFFU;
	int off = 3 - (start / 32);
	int shift = start & 31;
	uint32_t res = resp[off] >> shift;
	if (size + shift > 32 && off > 0) {
		res |= resp[off - 1] << ((32 - shift) % 32);
	}
	return res & mask;
}

bool
BCM2837SDHCI::cardIdentify(void)
{
	uint32_t resp[4];

	// CMD0: GO_IDLE_STATE.
	if (!sendCommand(0, 0, kCmdRespNone, false, NULL)) {
		IOLog("BCM2837SDHCI: CMD0 failed\n");
		return false;
	}

	// CMD8: SEND_IF_COND - voltage 2.7-3.6V (0x100) + check pattern 0xAA.
	// Failure here would normally mean "very old card" (pre-v2.00); this
	// driver doesn't implement that fallback path - see doGetFormatCapacities.
	bool isV2 = sendCommand(8, 0x1AA, kCmdRespShort, false, resp) && ((resp[0] & 0xFF) == 0xAA);
	if (!isV2) {
		IOLog("BCM2837SDHCI: CMD8 failed or echoed wrong pattern - pre-v2.00 cards unsupported\n");
		return false;
	}

	// ACMD41 (via CMD55 app-cmd prefix): SD_SEND_OP_COND, HCS bit (0x40000000)
	// to advertise host support for SDHC/SDXC, OCR voltage window 0x00FF8000
	// (2.7-3.6V). Poll until busy bit (bit31) is set.
	uint32_t acmd41Resp = 0;
	bool ready = false;
	for (uint32_t waited = 0; waited < 1000000 /* 1s, generous - real cards settle in ms */; waited += 10000) {
		uint32_t r55[4], r41[4];
		if (!sendCommand(55, 0, kCmdRespShort, false, r55)) {
			break;
		}
		if (!sendCommand(41, 0x40FF8000, kCmdRespShort, false, r41)) {
			break;
		}
		acmd41Resp = r41[0];
		if (acmd41Resp & 0x80000000) {
			ready = true;
			break;
		}
		IODelay(10000);
	}

	if (!ready) {
		IOLog("BCM2837SDHCI: ACMD41 never went ready (no card, or unsupported voltage)\n");
		return false;
	}
	fCardIsSDHC = (acmd41Resp & 0x40000000) != 0; // CCS bit

	// CMD2: ALL_SEND_CID.
	if (!sendCommand(2, 0, kCmdRespLong, false, resp)) {
		IOLog("BCM2837SDHCI: CMD2 failed\n");
		return false;
	}

	// CMD3: SEND_RELATIVE_ADDR - card returns its chosen RCA in resp[0] bits [31:16].
	if (!sendCommand(3, 0, kCmdRespShort, false, resp)) {
		IOLog("BCM2837SDHCI: CMD3 failed\n");
		return false;
	}
	fRCA = resp[0] >> 16;

	// CMD9: SEND_CSD.
	if (!sendCommand(9, fRCA << 16, kCmdRespLong, false, resp)) {
		IOLog("BCM2837SDHCI: CMD9 failed\n");
		return false;
	}

	uint32_t csdStruct = unstuffBits(resp, 126, 2);
	if (csdStruct == 1) {
		// CSD version 2.0 (SDHC/SDXC): C_SIZE is bits[69:48], 22 bits;
		// capacity in 512-byte sectors = (C_SIZE+1)*1024. Verified against
		// Linux's drivers/mmc/core/sd.c mmc_decode_csd(), not derived from
		// memory of the spec.
		uint32_t cSize = unstuffBits(resp, 48, 22);
		fBlockCount = ((uint64_t)cSize + 1) * 1024;
	} else {
		// CSD version 1.0 (SDSC, pre-2GB cards): C_SIZE/C_SIZE_MULT/
		// READ_BL_LEN based capacity formula is NOT implemented - this
		// driver only supports SDHC/SDXC cards. Flagged rather than
		// silently mis-reporting capacity.
		IOLog("BCM2837SDHCI: CSD structure version %u (SDSC) unsupported, only SDHC/SDXC (v2.0 CSD)\n", csdStruct);
		return false;
	}

	// CMD7: SELECT_CARD - move card from stand-by to transfer state.
	if (!sendCommand(7, fRCA << 16, kCmdRespShortBusy, false, NULL)) {
		IOLog("BCM2837SDHCI: CMD7 failed\n");
		return false;
	}

	// CMD16: SET_BLOCKLEN - force 512-byte blocks. On SDHC/SDXC cards this
	// is actually a no-op per spec (always 512), but harmless/cheap to send.
	if (!sendCommand(16, 512, kCmdRespShort, false, NULL)) {
		IOLog("BCM2837SDHCI: CMD16 failed\n");
		return false;
	}
	writeReg16(kRegBlockSize, 512);

	fCardPresent = true;
	IOLog("BCM2837SDHCI: card ready, RCA=%#x SDHC=%d blocks=%llu (%llu MB)\n",
	    fRCA, fCardIsSDHC, fBlockCount, (fBlockCount * 512) / (1024 * 1024));
	return true;
}

bool
BCM2837SDHCI::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}

	fRegMap = provider->mapDeviceMemoryWithIndex(0);
	if (fRegMap == NULL) {
		IOLog("BCM2837SDHCI: failed to map \"sdhci\" registers\n");
		return false;
	}
	fRegBase = (volatile void *)fRegMap->getVirtualAddress();

	// UNVERIFIED / not yet done: nothing here pin-muxes GPIO48-53 to ALT3.
	// Without that, every command below will simply time out (no card
	// visible) rather than fail cleanly - see header. Left as a TODO
	// rather than guessed at, since the actual GPIO controller mapping
	// isn't wired into this driver at all yet.

	// Install the primary interrupt handler and unmask it at the
	// controller (BCM2837InterruptController, via the "sdhci" DT node's
	// interrupt-parent/interrupts properties) before resetHost() arms
	// SIGNAL_ENABLE - otherwise a real interrupt could fire with nothing
	// registered to receive it.
	IOReturn intResult = provider->registerInterrupt(0, this, &BCM2837SDHCI::interruptOccurred);
	if (intResult != kIOReturnSuccess) {
		IOLog("BCM2837SDHCI: registerInterrupt failed, %#x\n", intResult);
		return false;
	}
	intResult = provider->enableInterrupt(0);
	if (intResult != kIOReturnSuccess) {
		IOLog("BCM2837SDHCI: enableInterrupt failed, %#x\n", intResult);
		return false;
	}

	if (!resetHost()) {
		return false;
	}
	if (!setClock(400000)) {
		return false;
	}
	// SDHCI_POWER_CONTROL: 3.3V (0x0E) | SD_BUS_POWER (0x01).
	writeReg8(kRegPowerControl, 0x0F);

	if (!cardIdentify()) {
		IOLog("BCM2837SDHCI: no card identified at start() - device will report no media\n");
		// Not fatal: publish the nub anyway so it's visible in the
		// registry / a card can be identified later. reportMediaState
		// reflects fCardPresent honestly either way.
	}

	registerService();
	return true;
}

void
BCM2837SDHCI::stop(IOService *provider)
{
	provider->disableInterrupt(0);
	provider->unregisterInterrupt(0);

	if (fRegMap != NULL) {
		fRegMap->release();
		fRegMap = NULL;
	}
	super::stop(provider);
}

void
BCM2837SDHCI::free(void)
{
	if (fStatusLock != NULL) {
		IOSimpleLockFree(fStatusLock);
		fStatusLock = NULL;
	}
	if (fInterruptLock != NULL) {
		IOLockFree(fInterruptLock);
		fInterruptLock = NULL;
	}
	super::free();
}

// --- IOBlockStorageDevice pure-virtual methods ---

IOReturn
BCM2837SDHCI::doEjectMedia(void)
{
	return kIOReturnUnsupported; // non-removable-in-practice for this bring-up driver
}

IOReturn
BCM2837SDHCI::doFormatMedia(UInt64 /*byteCapacity*/)
{
	return kIOReturnUnsupported;
}

UInt32
BCM2837SDHCI::doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const
{
	if (capacities != NULL && capacitiesMaxCount > 0) {
		capacities[0] = fBlockCount * fBlockSize;
	}
	return 1;
}

char *
BCM2837SDHCI::getVendorString(void)
{
	return (char *)"Broadcom";
}

char *
BCM2837SDHCI::getProductString(void)
{
	return (char *)"BCM2837 Arasan SDHCI";
}

char *
BCM2837SDHCI::getRevisionString(void)
{
	return (char *)"1.0";
}

char *
BCM2837SDHCI::getAdditionalDeviceInfoString(void)
{
	return (char *)"";
}

IOReturn
BCM2837SDHCI::reportBlockSize(UInt64 *blockSize)
{
	*blockSize = fBlockSize;
	return kIOReturnSuccess;
}

IOReturn
BCM2837SDHCI::reportEjectability(bool *isEjectable)
{
	*isEjectable = false;
	return kIOReturnSuccess;
}

IOReturn
BCM2837SDHCI::reportMaxValidBlock(UInt64 *maxBlock)
{
	*maxBlock = (fBlockCount > 0) ? (fBlockCount - 1) : 0;
	return kIOReturnSuccess;
}

IOReturn
BCM2837SDHCI::reportMediaState(bool *mediaPresent, bool *changedState)
{
	*mediaPresent = fCardPresent;
	if (changedState != NULL) {
		*changedState = false; // no card-detect interrupt wired up (see header)
	}
	return kIOReturnSuccess;
}

IOReturn
BCM2837SDHCI::reportRemovability(bool *isRemovable)
{
	*isRemovable = true;
	return kIOReturnSuccess;
}

IOReturn
BCM2837SDHCI::reportWriteProtection(bool *isWriteProtected)
{
	*isWriteProtected = false; // SDHCI_PRESENT_STATE WRITE_PROTECT bit unread - see header (no card-detect either)
	return kIOReturnSuccess;
}

bool
BCM2837SDHCI::readBlocksPIO(uint64_t startBlock, uint32_t nblks, void *buf)
{
	uint32_t *dst = (uint32_t *)buf;
	uint32_t arg = fCardIsSDHC ? (uint32_t)startBlock : (uint32_t)(startBlock * fBlockSize);
	uint32_t transferMode = kCmdTransBlkCntEn | kCmdTransRead | (nblks > 1 ? kCmdTransMulti : 0);
	writeReg16(kRegTransferMode, transferMode);
	writeReg16(kRegBlockCount, (uint16_t)nblks);

	uint8_t cmdIndex = (nblks > 1) ? 18 : 17; // READ_MULTIPLE_BLOCK / READ_SINGLE_BLOCK
	if (!sendCommand(cmdIndex, arg, kCmdRespShort, true, NULL)) {
		return false;
	}

	for (uint32_t b = 0; b < nblks; b++) {
		uint32_t status;
		if (!waitForIntStatus32(kIntBufReadRdy | kIntErrorMask, 1000000, &status)) {
			IOLog("BCM2837SDHCI: read block %u/%u timed out waiting BUFFER_READ_READY\n", b, nblks);
			return false;
		}
		if (status & kIntErrorMask) {
			IOLog("BCM2837SDHCI: read error, INT_STATUS=%#x\n", status);
			return false;
		}

		for (uint32_t w = 0; w < fBlockSize / 4; w++) {
			*dst++ = readReg32(kRegBuffer);
		}
	}

	uint32_t xferStatus;
	if (!waitForIntStatus32(kIntTransferDone | kIntErrorMask, 1000000, &xferStatus)) {
		IOLog("BCM2837SDHCI: read did not reach TRANSFER_COMPLETE\n");
		return false;
	}
	if (xferStatus & kIntErrorMask) {
		IOLog("BCM2837SDHCI: read error at TRANSFER_COMPLETE, INT_STATUS=%#x\n", xferStatus);
		return false;
	}

	if (nblks > 1) {
		uint32_t r[4];
		sendCommand(12, 0, kCmdRespShortBusy, false, r); // STOP_TRANSMISSION
	}
	return true;
}

bool
BCM2837SDHCI::writeBlocksPIO(uint64_t startBlock, uint32_t nblks, const void *buf)
{
	const uint32_t *src = (const uint32_t *)buf;
	uint32_t arg = fCardIsSDHC ? (uint32_t)startBlock : (uint32_t)(startBlock * fBlockSize);
	uint32_t transferMode = kCmdTransBlkCntEn | (nblks > 1 ? kCmdTransMulti : 0); // no READ bit = write
	writeReg16(kRegTransferMode, transferMode);
	writeReg16(kRegBlockCount, (uint16_t)nblks);

	uint8_t cmdIndex = (nblks > 1) ? 25 : 24; // WRITE_MULTIPLE_BLOCK / WRITE_BLOCK
	if (!sendCommand(cmdIndex, arg, kCmdRespShort, true, NULL)) {
		return false;
	}

	for (uint32_t b = 0; b < nblks; b++) {
		uint32_t status;
		if (!waitForIntStatus32(kIntBufWriteRdy | kIntErrorMask, 1000000, &status)) {
			IOLog("BCM2837SDHCI: write block %u/%u timed out waiting BUFFER_WRITE_READY\n", b, nblks);
			return false;
		}
		if (status & kIntErrorMask) {
			IOLog("BCM2837SDHCI: write error, INT_STATUS=%#x\n", status);
			return false;
		}

		for (uint32_t w = 0; w < fBlockSize / 4; w++) {
			writeReg32(kRegBuffer, *src++);
		}
	}

	uint32_t xferStatus;
	if (!waitForIntStatus32(kIntTransferDone | kIntErrorMask, 1000000, &xferStatus)) {
		IOLog("BCM2837SDHCI: write did not reach TRANSFER_COMPLETE\n");
		return false;
	}
	if (xferStatus & kIntErrorMask) {
		IOLog("BCM2837SDHCI: write error at TRANSFER_COMPLETE, INT_STATUS=%#x\n", xferStatus);
		return false;
	}

	if (nblks > 1) {
		uint32_t r[4];
		sendCommand(12, 0, kCmdRespShortBusy, false, r); // STOP_TRANSMISSION
	}
	return true;
}

IOReturn
BCM2837SDHCI::doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks,
    IOStorageAttributes * /*attributes*/, IOStorageCompletion *completion)
{
	if (!fCardPresent) {
		IOStorage::complete(completion, kIOReturnNoMedia, 0);
		return kIOReturnSuccess;
	}

	bool isWrite = (buffer->getDirection() == kIODirectionOut);
	IOByteCount transferSize = nblks * fBlockSize;

	// PIO only (see header) - stage through a bounce buffer rather than
	// assuming `buffer` is a single contiguous, CPU-addressable range.
	void *bounce = IOMalloc((size_t)transferSize);
	if (bounce == NULL) {
		IOStorage::complete(completion, kIOReturnNoMemory, 0);
		return kIOReturnSuccess;
	}

	bool ok;
	if (isWrite) {
		buffer->readBytes(0, bounce, transferSize);
		ok = writeBlocksPIO(block, (uint32_t)nblks, bounce);
	} else {
		ok = readBlocksPIO(block, (uint32_t)nblks, bounce);
		if (ok) {
			buffer->writeBytes(0, bounce, transferSize);
		}
	}

	IOFree(bounce, (size_t)transferSize);

	// Genuinely synchronous under the hood - readBlocksPIO/writeBlocksPIO
	// block this calling thread on waitForIntStatus32() rather than
	// returning immediately and completing from interruptOccurred() itself
	// (no DMA yet either) - but IOBlockStorageDevice's contract is
	// asynchronous: call the completion routine before returning rather
	// than returning a result directly, same as a real async driver's
	// caller expects.
	IOStorage::complete(completion, ok ? kIOReturnSuccess : kIOReturnIOError,
	    ok ? transferSize : 0);
	return kIOReturnSuccess;
}
