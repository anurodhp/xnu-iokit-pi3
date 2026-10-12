/*
 * SDIO host mode of BCM2837SDHCI and its BCM2837SDIODevice nub
 * (docs/wifi-plan.md, W2). See BCM2837SDIODevice.h.
 *
 * Everything register-level reuses BCM2837SDHCI's own, already proven on
 * this silicon: resetHost(), readClockConfig()/setClock(), sendCommand()
 * with the DAR-316 combined BLOCK_SIZE/COUNT and TRANSFER_MODE/COMMAND
 * writes, waitForIntStatus32(), the write spacing. What is new is only the
 * CMD53 data phase with SDIO block sizes, the card interrupt, and the
 * GPIO34-39 routing.
 */
#include "BCM2837SDIODevice.h"
#include "BCM2837SDHCI.h"
#include "BCM2837GPIO.h"

#include <IOKit/IOLib.h>
#include <string.h>

OSDefineMetaClassAndStructors(BCM2837SDIODevice, IOService);

// --- BCM2837SDHCI, SDIO side ---

void
BCM2837SDHCI::setIntEnable(uint32_t bits, bool on)
{
	// INT_ENABLE/SIGNAL_ENABLE are also written by interruptOccurred() (to
	// mask CARD_INT), so the shadow and both writes happen under fStatusLock
	// with interrupts off.
	IOInterruptState st = IOSimpleLockLockDisableInterrupt(fStatusLock);
	if (on) {
		fIntEnableShadow |= bits;
	} else {
		fIntEnableShadow &= ~bits;
	}
	writeReg32(kRegIntEnable, fIntEnableShadow);
	writeReg32(kRegSignalEnable, fIntEnableShadow);
	IOSimpleLockUnlockEnableInterrupt(fStatusLock, st);
}

bool
BCM2837SDHCI::sdioData(bool write, uint32_t arg, uint32_t blockSize, uint32_t blocks, void *buf)
{
	// CMD53 with a data phase: the same shape as readBlocksPIO()/
	// writeBlocksPIO() (BUFFER_READ/WRITE_READY per block, then
	// TRANSFER_COMPLETE), with an SDIO block size and no CMD12. Byte mode is
	// one "block" of blockSize = byte count (SDHCI spec 1.13: the host
	// transfers BLOCK_SIZE bytes per block; for a single block either mode
	// looks identical on the bus).
	uint16_t mode = kCmdTransBlkCntEn | (write ? 0 : kCmdTransRead) |
	    (blocks > 1 ? kCmdTransMulti : 0);
	fPendingBlkSizeCount = (blocks << 16) | blockSize;

	uint32_t resp[4] = {0};
	if (!sendCommand(53, arg, kCmdRespShort, true, resp, mode)) {
		return false;
	}
	if (resp[0] & 0xCB00) {
		IOLog("BCM2837SDHCI: CMD53 arg %#x R5 flags %#x\n", arg, (resp[0] >> 8) & 0xff);
		resetDatLine();
		return false;
	}

	uint8_t *p = (uint8_t *)buf;
	uint32_t words = blockSize / 4;
	for (uint32_t b = 0; b < blocks; b++) {
		uint32_t status;
		uint32_t want = write ? kIntBufWriteRdy : kIntBufReadRdy;
		if (!waitForIntStatus32(want | kIntErrorMask, 1000000, &status) ||
		    (status & kIntErrorMask)) {
			IOLog("BCM2837SDHCI: CMD53 %s block %u/%u failed, status %#x PRESENT_STATE %#x\n",
			    write ? "write" : "read", b, blocks, status, readReg32(kRegPresentState));
			resetDatLine();
			return false;
		}
		for (uint32_t w = 0; w < words; w++) {
			uint32_t v;
			if (write) {
				memcpy(&v, p, 4);
				writeReg32(kRegBuffer, v);
			} else {
				v = readReg32(kRegBuffer);
				memcpy(p, &v, 4);
			}
			p += 4;
		}
	}

	uint32_t status;
	if (!waitForIntStatus32(kIntTransferDone | kIntErrorMask, 1000000, &status) ||
	    (status & kIntErrorMask)) {
		IOLog("BCM2837SDHCI: CMD53 did not complete, status %#x\n", status);
		resetDatLine();
		return false;
	}
	return true;
}

bool
BCM2837SDHCI::startSDIOMode(IOService *provider)
{
	fSDIOMode = true;

	// bcm283x.dtsi emmc_gpio34: GPIO34-39 ALT3, CLK no pull, CMD/DAT0-3
	// pull-up. GPIO48-53 are sdhost's and are not touched.
	BCM2837GPIO::setFunction(34, 6, BCM2837GPIO::kFSelAlt3);
	BCM2837GPIO::setPull(34, 1, BCM2837GPIO::kPullOff);
	BCM2837GPIO::setPull(35, 5, BCM2837GPIO::kPullUp);
	unsigned gpclk = 0;
	BCM2837GPIO::getFunction(43, &gpclk);
	// GPIO43 ALT0 = GPCLK2, the 43438's 32 kHz LPO in Linux's DT
	// (bcm2710-rpi-3-b.dts bt_pins). The firmware is expected to have set it.
	IOLog("BCM2837SDHCI: SDIO pins routed (GPIO34-39 ALT3); GPIO43 function %u%s\n",
	    gpclk, gpclk == BCM2837GPIO::kFSelAlt0 ? " (GPCLK2)" : " (NOT GPCLK2)");

	IOReturn r = provider->registerInterrupt(0, this, &BCM2837SDHCI::interruptOccurred);
	if (r == kIOReturnSuccess) {
		r = provider->enableInterrupt(0);
	}
	if (r != kIOReturnSuccess) {
		IOLog("BCM2837SDHCI: SDIO: interrupt setup failed %#x\n", r);
		return false;
	}

	if (!resetHost()) {
		return false;
	}
	writeReg8(kRegPowerControl, 0x0F);
	IODelay(1000);
	readClockConfig();
	if (!setClock(kIdentificationClockHz)) {
		return false;
	}
	IODelay(1000);

	fSDIONub = BCM2837SDIODevice::withHost(this);
	if (fSDIONub == NULL || !fSDIONub->attach(this)) {
		IOLog("BCM2837SDHCI: SDIO: could not publish the nub\n");
		OSSafeReleaseNULL(fSDIONub);
		return false;
	}
	fSDIONub->registerService();
	IOLog("BCM2837SDHCI: SDIO host ready, nub published\n");
	// Deliberately no registerService() on this object: it is an
	// IOBlockStorageDevice, and IOBlockStorageDriver would attach to it.
	return true;
}

// --- BCM2837SDIODevice ---

BCM2837SDIODevice *
BCM2837SDIODevice::withHost(BCM2837SDHCI *host)
{
	BCM2837SDIODevice *d = new BCM2837SDIODevice;
	if (d != NULL && !d->init()) {
		d->release();
		return NULL;
	}
	d->fHost = host;
	for (int i = 0; i < 8; i++) {
		d->fBlockSize[i] = 64;
	}
	d->setName("sdio");
	return d;
}

IOReturn
BCM2837SDIODevice::command(uint8_t index, uint32_t arg, int respType, uint32_t *resp)
{
	uint32_t hostType;
	switch (respType) {
	case kRespR1:  hostType = BCM2837SDHCI::kCmdRespShort; break;
	case kRespR1b: hostType = BCM2837SDHCI::kCmdRespShortBusy; break;
	case kRespR4:  hostType = BCM2837SDHCI::kCmdRespShortNoChecks; break;
	default:       hostType = BCM2837SDHCI::kCmdRespNone; break;
	}
	uint32_t r[4] = {0};
	IOLockLock(fHost->fTransferLock);
	bool ok = fHost->sendCommand(index, arg, hostType, false, r);
	IOLockUnlock(fHost->fTransferLock);
	if (resp != NULL) {
		*resp = r[0];
	}
	return ok ? kIOReturnSuccess : kIOReturnIOError;
}

IOReturn
BCM2837SDIODevice::readDirect(unsigned fn, uint32_t addr, uint8_t *out)
{
	uint32_t r = 0;
	IOReturn ret = command(52, ((fn & 7) << 28) | ((addr & 0x1FFFF) << 9), kRespR1, &r);
	if (ret != kIOReturnSuccess) {
		return ret;
	}
	if (r & 0xCB00) {
		return kIOReturnIOError;
	}
	*out = (uint8_t)r;
	return kIOReturnSuccess;
}

IOReturn
BCM2837SDIODevice::writeDirect(unsigned fn, uint32_t addr, uint8_t value)
{
	uint32_t r = 0;
	IOReturn ret = command(52, (1U << 31) | ((fn & 7) << 28) | ((addr & 0x1FFFF) << 9) | value,
	    kRespR1, &r);
	if (ret != kIOReturnSuccess) {
		return ret;
	}
	return (r & 0xCB00) ? kIOReturnIOError : kIOReturnSuccess;
}

IOReturn
BCM2837SDIODevice::extended(bool write, unsigned fn, uint32_t addr, bool incr, uint8_t *buf, uint32_t len)
{
	if (len == 0 || (len & 3) != 0) {
		return kIOReturnBadArgument;
	}
	uint32_t bsize = fBlockSize[fn & 7];
	while (len > 0) {
		uint32_t blk, count, m;
		if (len >= 511 * bsize) {
			blk = 1; count = 511; m = count * bsize;
		} else if (len > bsize) {
			blk = 1; count = len / bsize; m = count * bsize;
		} else {
			blk = 0; count = len; m = len;
		}
		// Byte mode count 512 is encoded as 0 (SDIO 5.3).
		uint32_t arg = (write ? (1U << 31) : 0) | ((fn & 7) << 28) | (blk << 27) |
		    ((incr ? 1U : 0) << 26) | ((addr & 0x1FFFF) << 9) | (count & 0x1FF);
		IOLockLock(fHost->fTransferLock);
		bool ok = blk ? fHost->sdioData(write, arg, bsize, count, buf)
		              : fHost->sdioData(write, arg, m, 1, buf);
		IOLockUnlock(fHost->fTransferLock);
		if (!ok) {
			return kIOReturnIOError;
		}
		len -= m;
		buf += m;
		if (incr) {
			addr += m;
		}
	}
	return kIOReturnSuccess;
}

IOReturn
BCM2837SDIODevice::readExtended(unsigned fn, uint32_t addr, bool incr, void *buf, uint32_t len)
{
	return extended(false, fn, addr, incr, (uint8_t *)buf, len);
}

IOReturn
BCM2837SDIODevice::writeExtended(unsigned fn, uint32_t addr, bool incr, const void *buf, uint32_t len)
{
	return extended(true, fn, addr, incr, (uint8_t *)(uintptr_t)buf, len);
}

void
BCM2837SDIODevice::setFunctionBlockSize(unsigned fn, uint32_t size)
{
	if (size >= 4 && size <= 2048) {
		fBlockSize[fn & 7] = size;
	}
}

IOReturn
BCM2837SDIODevice::setBusWidth4(bool wide)
{
	IOLockLock(fHost->fTransferLock);
	uint8_t hc = fHost->readReg8(BCM2837SDHCI::kRegHostControl);
	hc = wide ? (hc | BCM2837SDHCI::kHostControl4BitBus) : (hc & ~BCM2837SDHCI::kHostControl4BitBus);
	fHost->writeReg8(BCM2837SDHCI::kRegHostControl, hc);
	IOLockUnlock(fHost->fTransferLock);
	return kIOReturnSuccess;
}

IOReturn
BCM2837SDIODevice::setClock(uint32_t hz, uint32_t *actualHz)
{
	IOLockLock(fHost->fTransferLock);
	bool ok = fHost->setClock(hz);
	if (actualHz != NULL) {
		*actualHz = fHost->fCurrentClockHz;
	}
	IOLockUnlock(fHost->fTransferLock);
	return ok ? kIOReturnSuccess : kIOReturnIOError;
}

void
BCM2837SDIODevice::setCardInterruptHandler(CardInterruptHandler handler, OSObject *target)
{
	fHost->setIntEnable(BCM2837SDHCI::kIntCardInt, false);
	fHost->fCardIntTarget = target;
	fHost->fCardIntHandler = handler;
}

void
BCM2837SDIODevice::enableCardInterrupt(bool on)
{
	fHost->setIntEnable(BCM2837SDHCI::kIntCardInt, on);
}
