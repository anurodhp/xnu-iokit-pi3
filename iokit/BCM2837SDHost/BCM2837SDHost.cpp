#include "BCM2837SDHost.h"
#include "BCM2837Mailbox.h"
#include "BCM2837GPIO.h"
#include "BCM2837SDCardOwner.h"

#include <IOKit/IOLib.h>
#include <IOKit/storage/IOMedia.h>
#include <libkern/c++/OSString.h>
#include <pexpert/pexpert.h>
#include <string.h>

#define super IOBlockStorageDevice

OSDefineMetaClassAndStructors(BCM2837SDHost, IOBlockStorageDevice);

// Mailbox clock id of the VPU core clock, sdhost's input clock (Linux
// bcm2835.c takes it from the DT `clocks` of the sdhost node, which on the
// Pi is BCM2835_CLOCK_VPU, i.e. the core clock; RPI_FIRMWARE_CORE_CLK_ID = 4
// in include/soc/bcm2835/raspberrypi-firmware.h). config.txt pins it at
// 250 MHz (core_freq=250) for the mini UART console.
static const uint32_t kClockIdCore = 4;
static const uint32_t kFallbackCoreHz = 250000000;

static bool
wifiModeEnabled(void)
{
	uint32_t wifi = 0;
	return PE_parse_boot_argn("wifi", &wifi, sizeof(wifi)) && wifi != 0;
}

IOService *
BCM2837SDHost::probe(IOService *provider, SInt32 *score)
{
	IOService *result = super::probe(provider, score);
	if (result == NULL) {
		return NULL;
	}
	// Same defensive name check as BCM2837SDHCI::probe() (IONameMatch has
	// been seen matching sibling nodes under arm-io).
	const char *name = provider->getName();
	if (name == NULL || strcmp(name, "sdhost") != 0) {
		return NULL;
	}
	if (!wifiModeEnabled()) {
		return NULL;
	}
	return result;
}

bool
BCM2837SDHost::init(OSDictionary *properties)
{
	if (!super::init(properties)) {
		return false;
	}
	fRegMap = NULL;
	fRegBase = NULL;
	fCoreClockHz = 0;
	fHcfg = 0;
	fCdiv = 0;
	fClockHz = 0;
	fCardPresent = false;
	fCardIsSDHC = false;
	fBus4Bit = false;
	fRCA = 0;
	fBlockCount = 0;
	fBlockSize = 512;
	fTransferLock = IOLockAlloc();
	return fTransferLock != NULL;
}

void
BCM2837SDHost::free(void)
{
	if (fTransferLock != NULL) {
		IOLockFree(fTransferLock);
		fTransferLock = NULL;
	}
	super::free();
}

void
BCM2837SDHost::pinmux(bool toSDHost)
{
	// GPIO48-53: CLK, CMD, DAT0-3. ALT0 routes them to sdhost, ALT3 to the
	// Arasan controller (Linux bcm283x.dtsi: sdhost_gpio48 = ALT0,
	// emmc_gpio48 = ALT3). QEMU's GPIO model only moves the emulated card
	// on a transition to "all six ALT0" (sdhost) or "all six input"
	// (sdhci), hw/gpio/bcm2835_gpio.c gpfsel_set(); it ignores ALT3. So the
	// hand-back goes through input first, which is harmless on hardware
	// (the pins float for a few microseconds with nothing driving the bus).
	if (toSDHost) {
		BCM2837GPIO::setFunction(48, 6, BCM2837GPIO::kFSelAlt0);
		// Pulls as Linux's sdhost_gpio48 group leaves them on the Pi 3 DT:
		// the firmware default (CLK no pull, CMD/DAT pull-up) - set
		// explicitly so the state does not depend on what ran before us.
		BCM2837GPIO::setPull(48, 1, BCM2837GPIO::kPullOff);
		BCM2837GPIO::setPull(49, 5, BCM2837GPIO::kPullUp);
	} else {
		BCM2837GPIO::setFunction(48, 6, BCM2837GPIO::kFSelInput);
		BCM2837GPIO::setFunction(48, 6, BCM2837GPIO::kFSelAlt3);
	}
}

void
BCM2837SDHost::resetHost(void)
{
	// bcm2835_reset_internal(), step for step.
	writeReg(kRegVdd, 0);
	writeReg(kRegCmd, 0);
	writeReg(kRegArg, 0);
	writeReg(kRegTout, 0xf00000);
	writeReg(kRegCdiv, 0);
	writeReg(kRegHsts, 0x7f8); // write 1s to clear
	writeReg(kRegHcfg, 0);
	writeReg(kRegHbct, 0);
	writeReg(kRegHblc, 0);

	// "Limit fifo usage due to silicon bug"
	uint32_t edm = readReg(kRegEdm);
	edm &= ~((kEdmThresholdMask << kEdmReadThresholdShift) |
	    (kEdmThresholdMask << kEdmWriteThresholdShift));
	edm |= (kFifoReadThreshold << kEdmReadThresholdShift) |
	    (kFifoWriteThreshold << kEdmWriteThresholdShift);
	writeReg(kRegEdm, edm);
	IOSleep(20);
	writeReg(kRegVdd, 1);
	IOSleep(20);

	// bcm2835_set_ios(): internal bus always wide, and SLOW_CARD so the
	// ident-mode divisor is used in data mode too ("the 3-bit clock divisor
	// in data mode is too small if the core clock is higher than 250MHz").
	// BUSY_IRPT_EN is what makes SDHSTS_BUSY_IRPT latch at the end of an R1b
	// busy period (Linux sets it permanently in host->hcfg at probe); no
	// interrupt handler is registered, the bit is polled.
	fHcfg = kHcfgBusyIrptEn | kHcfgWideIntBus | kHcfgSlowCard;
	writeReg(kRegHcfg, fHcfg);
	writeReg(kRegCdiv, fCdiv);
}

void
BCM2837SDHost::setClock(uint32_t hz)
{
	// bcm2835_set_clock() without the firmware path: SDCDIV holds div - 2,
	// rounded so the result never exceeds the request.
	uint32_t div = fCoreClockHz / hz;
	if (div < 2) {
		div = 2;
	}
	if ((fCoreClockHz / div) > hz) {
		div++;
	}
	div -= 2;
	if (div > 0x7ff) {
		div = 0x7ff;
	}
	fCdiv = div;
	fClockHz = fCoreClockHz / (div + 2);
	writeReg(kRegCdiv, fCdiv);
	// "Set the timeout to 500ms"
	writeReg(kRegTout, fClockHz / 2);
	IOLog("BCM2837SDHost: SD clock %u Hz (core %u Hz, SDCDIV %u)\n", fClockHz, fCoreClockHz, fCdiv);
}

void
BCM2837SDHost::dumpRegs(const char *why)
{
	IOLog("BCM2837SDHost: %s: CMD=%#x ARG=%#x HSTS=%#x EDM=%#x HCFG=%#x HBCT=%#x HBLC=%#x CDIV=%#x\n",
	    why, readReg(kRegCmd), readReg(kRegArg), readReg(kRegHsts), readReg(kRegEdm),
	    readReg(kRegHcfg), readReg(kRegHbct), readReg(kRegHblc), readReg(kRegCdiv));
}

bool
BCM2837SDHost::waitCmdIdle(uint32_t timeoutUs, uint32_t *outCmd)
{
	// bcm2835_read_wait_sdcmd(): 1 us polls for the first 10 us, then 10 us.
	uint32_t waited = 0;
	uint32_t cmd = readReg(kRegCmd);
	while (cmd & kCmdNew) {
		if (waited >= timeoutUs) {
			break;
		}
		uint32_t step = (waited < 10) ? 1 : 10;
		IODelay(step);
		waited += step;
		cmd = readReg(kRegCmd);
	}
	if (outCmd != NULL) {
		*outCmd = cmd;
	}
	return (cmd & kCmdNew) == 0;
}

bool
BCM2837SDHost::waitFsmIdle(bool isRead)
{
	// bcm2835_wait_transfer_complete(): the data state machine must be back
	// in IDENTMODE/DATAMODE; READWAIT (read) or WRITESTART1 (write) are the
	// "finished but parked" states it is kicked out of with FORCE_DATA_MODE.
	uint32_t alternateIdle = isRead ? kFsmReadWait : kFsmWriteStart1;
	for (uint32_t i = 0; i < 100000; i++) {
		uint32_t edm = readReg(kRegEdm);
		uint32_t fsm = edm & kEdmFsmMask;
		if (fsm == kFsmIdentMode || fsm == kFsmDataMode) {
			return true;
		}
		if (fsm == alternateIdle) {
			writeReg(kRegEdm, edm | kEdmForceDataMode);
			return true;
		}
		IODelay(1);
	}
	dumpRegs("data FSM never returned to idle");
	return false;
}

bool
BCM2837SDHost::sendCommand(uint8_t idx, uint32_t arg, int respType, uint32_t resp[4],
    uint32_t dataFlags, uint32_t blockSize, uint32_t blockCount)
{
	uint32_t cmd;
	if (!waitCmdIdle(100000, &cmd)) {
		IOLog("BCM2837SDHost: previous command never completed (before CMD%u)\n", idx);
		dumpRegs("stuck NEW_FLAG");
		return false;
	}

	// bcm2835_request(): a new request must not start while the data FSM
	// is mid-transfer. CMD12 is exempt: Linux sends the stop command from
	// bcm2835_transfer_complete() straight after the data, with the FSM
	// still parked in READWAIT/WRITESTART1, and only checks the FSM at the
	// start of the next request.
	uint32_t fsm = readReg(kRegEdm) & kEdmFsmMask;
	if (idx != 12 && fsm != kFsmIdentMode && fsm != kFsmDataMode) {
		IOLog("BCM2837SDHost: CMD%u: data FSM busy (%#x)\n", idx, fsm);
		if (!waitFsmIdle((dataFlags & kCmdRead) != 0)) {
			return false;
		}
	}

	uint32_t hsts = readReg(kRegHsts);
	if (hsts & kHstsErrorMask) {
		writeReg(kRegHsts, hsts);
	}

	if (dataFlags != 0) {
		// SDHBCT before SDHBLC: the block count write is what arms the
		// transfer (QEMU: datacnt = hblc * hbct on the SDHBLC write).
		writeReg(kRegHbct, blockSize);
		writeReg(kRegHblc, blockCount);
	}

	writeReg(kRegArg, arg);

	uint32_t sdcmd = idx & kCmdMask;
	switch (respType) {
	case kRespNone:
		sdcmd |= kCmdNoResponse;
		break;
	case kRespLong:
		sdcmd |= kCmdLongResp;
		break;
	case kRespShortBusy:
		sdcmd |= kCmdBusyWait;
		break;
	default:
		break;
	}
	sdcmd |= dataFlags;
	writeReg(kRegCmd, sdcmd | kCmdNew);

	// bcm2835_finish_command().
	if (!waitCmdIdle(1000000, &cmd)) {
		IOLog("BCM2837SDHost: CMD%u never completed\n", idx);
		dumpRegs("command timeout");
		return false;
	}
	if (cmd & kCmdFail) {
		hsts = readReg(kRegHsts);
		writeReg(kRegHsts, kHstsErrorMask);
		bool crcOnly = (hsts & kHstsErrorMask) == kHstsCrc7;
		if (!(crcOnly && respType == kRespShortNoCrc)) {
			// CMD8 on a v1 card and CMD55/ACMD41 polling can time out
			// legitimately; log once and let the caller decide.
			IOLog("BCM2837SDHost: CMD%u failed, HSTS=%#x\n", idx, hsts);
			uint32_t edm = readReg(kRegEdm);
			uint32_t f = edm & kEdmFsmMask;
			if (f == kFsmReadWait || f == kFsmWriteStart1) {
				writeReg(kRegEdm, edm | kEdmForceDataMode);
			}
			return false;
		}
	}

	if (respType == kRespShortBusy) {
		// Busy completion latches SDHSTS_BUSY_IRPT (bcm2835_busy_irq()).
		uint32_t waitedMs = 0;
		for (;;) {
			hsts = readReg(kRegHsts);
			if (hsts & (kHstsBusyIrpt | kHstsErrorMask)) {
				break;
			}
			if (waitedMs >= 1000) {
				IOLog("BCM2837SDHost: CMD%u busy never ended\n", idx);
				dumpRegs("busy timeout");
				return false;
			}
			IOSleep(1);
			waitedMs++;
		}
		writeReg(kRegHsts, kHstsBusyIrpt);
		if (hsts & kHstsErrorMask) {
			writeReg(kRegHsts, kHstsErrorMask);
			IOLog("BCM2837SDHost: CMD%u error during busy, HSTS=%#x\n", idx, hsts);
			return false;
		}
	}

	if (resp != NULL) {
		if (respType == kRespLong) {
			// cmd->resp[3 - i] = readl(SDRSP0 + i * 4): resp[0] is bits
			// 127:96 of the CID/CSD, the layout BCM2837SDHCI's
			// unstuffBits() expects (it produces the same after undoing
			// the Arasan's CRC strip).
			for (int i = 0; i < 4; i++) {
				resp[3 - i] = readReg(kRegRsp0 + i * 4);
			}
		} else if (respType != kRespNone) {
			resp[0] = readReg(kRegRsp0);
		}
	}
	return true;
}

bool
BCM2837SDHost::pioTransfer(bool isRead, uint32_t *buf, uint32_t words)
{
	// bcm2835_transfer_block_pio(): the FIFO fill level is SDEDM bits 8:4.
	uint64_t deadline;
	clock_interval_to_deadline(500, kMillisecondScale, &deadline);
	while (words > 0) {
		uint32_t edm = readReg(kRegEdm);
		uint32_t level = (edm >> 4) & 0x1f;
		uint32_t avail = isRead ? level : (kFifoWords - level);
		if (avail == 0) {
			uint32_t fsm = edm & kEdmFsmMask;
			bool inData = isRead ?
			    (fsm == kFsmReadData || fsm == kFsmReadWait || fsm == kFsmReadCrc) :
			    (fsm == kFsmWriteData || fsm == kFsmWriteStart1 || fsm == kFsmWriteStart2);
			if (!inData) {
				uint32_t hsts = readReg(kRegHsts);
				if (hsts & kHstsErrorMask) {
					IOLog("BCM2837SDHost: PIO %s error, fsm %#x HSTS %#x, %u words left\n",
					    isRead ? "read" : "write", fsm, hsts, words);
					return false;
				}
			}
			if (mach_absolute_time() > deadline) {
				IOLog("BCM2837SDHost: PIO %s timeout, EDM %#x, %u words left\n",
				    isRead ? "read" : "write", edm, words);
				return false;
			}
			IODelay(1);
			continue;
		}
		if (avail > words) {
			avail = words;
		}
		words -= avail;
		while (avail--) {
			if (isRead) {
				*buf++ = readReg(kRegData);
			} else {
				writeReg(kRegData, *buf++);
			}
		}
	}

	uint32_t hsts = readReg(kRegHsts);
	if (hsts & (kHstsCrc16 | kHstsCrc7 | kHstsFifoError | kHstsCmdTimeout | kHstsRewTimeout)) {
		IOLog("BCM2837SDHost: %s transfer error, HSTS %#x\n", isRead ? "read" : "write", hsts);
		writeReg(kRegHsts, kHstsErrorMask);
		return false;
	}
	return true;
}

bool
BCM2837SDHost::waitCardReady(uint32_t timeoutMs)
{
	// After a write the card programs flash while holding DAT0 low. The
	// mmc core handles that on hosts without busy detection by polling
	// CMD13 until READY_FOR_DATA (bit 8) and CURRENT_STATE == tran (4)
	// (drivers/mmc/core/block.c card_busy_detect()). Same here.
	for (uint32_t ms = 0; ms < timeoutMs; ms++) {
		uint32_t r[4] = {0};
		if (sendCommand(13, fRCA << 16, kRespShort, r)) {
			uint32_t state = (r[0] >> 9) & 0xf;
			if ((r[0] & (1U << 8)) && state == 4) {
				return true;
			}
		}
		IOSleep(1);
	}
	IOLog("BCM2837SDHost: card not ready after %u ms\n", timeoutMs);
	return false;
}

bool
BCM2837SDHost::widenBusTo4Bit(void)
{
	// Same negotiation as BCM2837SDHCI::widenBusTo4Bit() (DAR-218): read
	// the SCR (ACMD51, 8 bytes), check SD_BUS_WIDTHS bit 2, ACMD6(2), and
	// only then flip the host to 4-bit.
	uint32_t r[4];
	if (!sendCommand(55, fRCA << 16, kRespShort, r)) {
		return false;
	}
	if (!sendCommand(51, 0, kRespShort, r, kCmdRead, 8, 1)) {
		return false;
	}
	uint32_t scr[2] = {0, 0};
	if (!pioTransfer(true, scr, 2)) {
		return false;
	}
	waitFsmIdle(true);
	// FIFO packs bytes little-endian in arrival order, so scr[1] (the
	// second byte on the wire, SD_BUS_WIDTHS in its low nibble) is bits
	// 15:8 of the first word - same as on the Arasan FIFO (QEMU's
	// bcm2835_sdhost_fifo_run(): value |= byte << (n * 8)).
	uint32_t busWidths = (scr[0] >> 8) & 0xf;
	if (!(busWidths & 0x4)) {
		IOLog("BCM2837SDHost: card does not support 4-bit (SD_BUS_WIDTHS=%#x)\n", busWidths);
		return false;
	}
	if (!sendCommand(55, fRCA << 16, kRespShort, r) ||
	    !sendCommand(6, 0x2, kRespShort, r)) {
		IOLog("BCM2837SDHost: ACMD6 failed\n");
		return false;
	}
	fHcfg |= kHcfgWideExtBus;
	writeReg(kRegHcfg, fHcfg);
	fBus4Bit = true;
	return true;
}

static uint32_t
unstuffBits(const uint32_t resp[4], int start, int size)
{
	// Linux drivers/mmc/core/mmc_ops.h unstuff_bits(), as in BCM2837SDHCI.
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
BCM2837SDHost::cardIdentify(void)
{
	uint32_t resp[4];

	if (!sendCommand(0, 0, kRespNone, NULL)) {
		IOLog("BCM2837SDHost: CMD0 failed\n");
		return false;
	}
	if (!sendCommand(8, 0x1AA, kRespShort, resp) || (resp[0] & 0xFF) != 0xAA) {
		IOLog("BCM2837SDHost: CMD8 failed or wrong echo - pre-v2.00 cards unsupported\n");
		return false;
	}

	uint32_t ocr = 0;
	bool ready = false;
	for (int i = 0; i < 100; i++) {
		uint32_t r55[4], r41[4];
		if (!sendCommand(55, 0, kRespShort, r55)) {
			break;
		}
		if (!sendCommand(41, 0x40FF8000, kRespShortNoCrc, r41)) {
			break;
		}
		ocr = r41[0];
		if (ocr & 0x80000000) {
			ready = true;
			break;
		}
		IOSleep(10);
	}
	if (!ready) {
		IOLog("BCM2837SDHost: ACMD41 never went ready (OCR %#x)\n", ocr);
		return false;
	}
	fCardIsSDHC = (ocr & 0x40000000) != 0;

	if (!sendCommand(2, 0, kRespLong, resp)) {
		IOLog("BCM2837SDHost: CMD2 failed\n");
		return false;
	}
	if (!sendCommand(3, 0, kRespShort, resp)) {
		IOLog("BCM2837SDHost: CMD3 failed\n");
		return false;
	}
	fRCA = resp[0] >> 16;
	if (!sendCommand(9, fRCA << 16, kRespLong, resp)) {
		IOLog("BCM2837SDHost: CMD9 failed\n");
		return false;
	}
	uint32_t csdStruct = unstuffBits(resp, 126, 2);
	if (csdStruct != 1) {
		IOLog("BCM2837SDHost: CSD structure %u unsupported (SDHC/SDXC only)\n", csdStruct);
		return false;
	}
	fBlockCount = ((uint64_t)unstuffBits(resp, 48, 22) + 1) * 1024;

	if (!sendCommand(7, fRCA << 16, kRespShortBusy, NULL)) {
		IOLog("BCM2837SDHost: CMD7 failed\n");
		return false;
	}

	if (!widenBusTo4Bit()) {
		IOLog("BCM2837SDHost: staying at 1-bit bus\n");
	}
	if (!sendCommand(16, 512, kRespShort, NULL)) {
		IOLog("BCM2837SDHost: CMD16 failed\n");
		return false;
	}

	setClock(kDataClockHz);

	// Prove the data path at the new clock before IOKit sees the disk,
	// like BCM2837SDHCI::tryRaiseDataPhaseClock(): block 0 must read, and
	// carry an MBR signature (the card always has one here, the image is
	// built by tools/make_sd_image.sh / make_realhw_sd_image.sh).
	uint8_t *mbr = (uint8_t *)IOMalloc(512);
	if (mbr == NULL) {
		return false;
	}
	bool ok = readBlocks(0, 1, mbr);
	if (ok && (mbr[510] != 0x55 || mbr[511] != 0xAA)) {
		IOLog("BCM2837SDHost: block 0 has no MBR signature (%02x %02x)\n", mbr[510], mbr[511]);
		ok = false;
	}
	IOFree(mbr, 512);
	if (!ok) {
		IOLog("BCM2837SDHost: block 0 read check failed at %u Hz\n", fClockHz);
		return false;
	}

	fCardPresent = true;
	IOLog("BCM2837SDHost: card ready, RCA=%#x SDHC=%d %s-bus blocks=%llu (%llu MB)\n",
	    fRCA, fCardIsSDHC, fBus4Bit ? "4" : "1", fBlockCount, (fBlockCount * 512) / (1024 * 1024));
	return true;
}

bool
BCM2837SDHost::readBlocks(uint64_t start, uint32_t n, void *buf)
{
	uint32_t arg = fCardIsSDHC ? (uint32_t)start : (uint32_t)(start * fBlockSize);
	uint8_t idx = (n > 1) ? 18 : 17;
	if (!sendCommand(idx, arg, kRespShort, NULL, kCmdRead, fBlockSize, n)) {
		return false;
	}
	bool ok = pioTransfer(true, (uint32_t *)buf, n * (fBlockSize / 4));
	if (n > 1) {
		// Open-ended multi-block read: Linux sends the stop command
		// whenever there is no CMD23 (bcm2835_transfer_complete()).
		uint32_t r[4];
		if (!sendCommand(12, 0, kRespShortBusy, r)) {
			ok = false;
		}
	}
	if (!waitFsmIdle(true)) {
		ok = false;
	}
	return ok;
}

bool
BCM2837SDHost::writeBlocks(uint64_t start, uint32_t n, const void *buf)
{
	uint32_t arg = fCardIsSDHC ? (uint32_t)start : (uint32_t)(start * fBlockSize);
	uint8_t idx = (n > 1) ? 25 : 24;
	if (!sendCommand(idx, arg, kRespShort, NULL, kCmdWrite, fBlockSize, n)) {
		return false;
	}
	bool ok = pioTransfer(false, (uint32_t *)(uintptr_t)buf, n * (fBlockSize / 4));
	// bcm2835_transfer_complete(): an open-ended multi-block write is ended
	// with CMD12 (R1b); a single block waits for the FSM to come back to
	// idle (WRITESTART1 = finished and parked).
	if (n > 1) {
		uint32_t r[4];
		if (!sendCommand(12, 0, kRespShortBusy, r)) {
			ok = false;
		}
	}
	if (!waitFsmIdle(false)) {
		ok = false;
	}
	if (!waitCardReady(1000)) {
		ok = false;
	}
	return ok;
}

void
BCM2837SDHost::publishVerdict(const char *host)
{
	IOLog("BCM2837SDHost: SD card host is \"%s\"\n", host);
	OSString *val = OSString::withCString(host);
	if (val != NULL) {
		IOService::publishResource(kCardHostResource, val);
		val->release();
	}
}

bool
BCM2837SDHost::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}

	fRegMap = provider->mapDeviceMemoryWithIndex(0);
	if (fRegMap == NULL) {
		IOLog("BCM2837SDHost: failed to map \"sdhost\" registers\n");
		publishVerdict("sdhci");
		return false;
	}
	fRegBase = (volatile void *)fRegMap->getVirtualAddress();

	if (!BCM2837Mailbox::getClockRateHz(kClockIdCore, &fCoreClockHz) ||
	    fCoreClockHz < 50000000 || fCoreClockHz > 1000000000) {
		IOLog("BCM2837SDHost: core clock not reported (got %u) - assuming %u Hz\n",
		    fCoreClockHz, kFallbackCoreHz);
		fCoreClockHz = kFallbackCoreHz;
	}

	// Claim the card before touching GPIO48-53 (BCM2837SDCardOwner.h): if
	// BCM2837SDHCI already timed out waiting for us and took it, stay off
	// the pins entirely.
	if (!BCM2837SDCardOwner::claim(BCM2837SDCardOwner::kOwnerSDHost)) {
		IOLog("BCM2837SDHost: the Arasan controller already owns the SD card - not starting\n");
		fRegMap->release();
		fRegMap = NULL;
		fRegBase = NULL;
		return false;
	}

	pinmux(true);
	resetHost();
	setClock(kIdentClockHz);
	IODelay(1000); // >= 74 clocks at 400 kHz before CMD0

	uint32_t failTest = 0;
	PE_parse_boot_argn("sdhost-fail-test", &failTest, sizeof(failTest));

	bool ok = (failTest == 0) && cardIdentify();
	if (!ok) {
		if (failTest != 0) {
			IOLog("BCM2837SDHost: sdhost-fail-test=1 - pretending card init failed\n");
		}
		IOLog("BCM2837SDHost: card init failed - handing GPIO48-53 back to the Arasan controller\n");
		writeReg(kRegVdd, 0);
		pinmux(false);
		BCM2837SDCardOwner::handBack();
		publishVerdict("sdhci");
		fRegMap->release();
		fRegMap = NULL;
		fRegBase = NULL;
		return false;
	}

	publishVerdict("sdhost");
	registerService();
	return true;
}

// --- IOBlockStorageDevice ---

IOReturn
BCM2837SDHost::doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks,
    IOStorageAttributes * /*attributes*/, IOStorageCompletion *completion)
{
	if (!fCardPresent) {
		IOStorage::complete(completion, kIOReturnNoMedia, 0);
		return kIOReturnSuccess;
	}
	// SDHBLC is 9 bits (Linux: "Host block count (SDIO/SDHC) - 9 R/W");
	// mmc->max_blk_count = 65535 there only because the core splits
	// requests for it. Split here into <= 256-block commands.
	if (nblks == 0 || block > fBlockCount || (fBlockCount - block) < nblks) {
		IOLog("BCM2837SDHost: bad request block=%llu nblks=%llu\n",
		    (unsigned long long)block, (unsigned long long)nblks);
		IOStorage::complete(completion, kIOReturnBadArgument, 0);
		return kIOReturnSuccess;
	}

	bool isWrite = (buffer->getDirection() == kIODirectionOut);
	const UInt64 kChunk = 256;
	size_t bounceSize = (size_t)(((nblks < kChunk) ? nblks : kChunk) * fBlockSize);
	void *bounce = IOMalloc(bounceSize);
	if (bounce == NULL) {
		IOStorage::complete(completion, kIOReturnNoMemory, 0);
		return kIOReturnSuccess;
	}

	IOLockLock(fTransferLock);
	bool ok = true;
	UInt64 done = 0;
	while (ok && done < nblks) {
		UInt64 n = nblks - done;
		if (n > kChunk) {
			n = kChunk;
		}
		IOByteCount off = done * fBlockSize, len = n * fBlockSize;
		if (isWrite) {
			buffer->readBytes(off, bounce, len);
			ok = writeBlocks(block + done, (uint32_t)n, bounce);
		} else {
			ok = readBlocks(block + done, (uint32_t)n, bounce);
			if (ok) {
				buffer->writeBytes(off, bounce, len);
			}
		}
		if (ok) {
			done += n;
		}
	}
	IOLockUnlock(fTransferLock);
	IOFree(bounce, bounceSize);

	if (!ok) {
		IOLog("BCM2837SDHost: %s of %llu blocks at %llu failed after %llu\n",
		    isWrite ? "write" : "read", (unsigned long long)nblks,
		    (unsigned long long)block, (unsigned long long)done);
	}
	IOStorage::complete(completion, ok ? kIOReturnSuccess : kIOReturnIOError,
	    ok ? nblks * fBlockSize : 0);
	return kIOReturnSuccess;
}

IOReturn
BCM2837SDHost::doSynchronize(UInt64, UInt64, IOStorageSynchronizeOptions)
{
	// writeBlocks() returns only after CMD13 reports the card back in tran
	// state with READY_FOR_DATA, i.e. programming finished; there is no
	// cache to flush (same reasoning as BCM2837SDHCI::doSynchronize()).
	return kIOReturnSuccess;
}

IOReturn BCM2837SDHost::doEjectMedia(void) { return kIOReturnUnsupported; }
IOReturn BCM2837SDHost::doFormatMedia(UInt64) { return kIOReturnUnsupported; }

UInt32
BCM2837SDHost::doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const
{
	if (capacities != NULL && capacitiesMaxCount > 0) {
		capacities[0] = fBlockCount * fBlockSize;
	}
	return 1;
}

char *BCM2837SDHost::getVendorString(void) { return (char *)"Broadcom"; }
char *BCM2837SDHost::getProductString(void) { return (char *)"BCM2835 SDHOST"; }
char *BCM2837SDHost::getRevisionString(void) { return (char *)"1.0"; }
char *BCM2837SDHost::getAdditionalDeviceInfoString(void) { return (char *)""; }

IOReturn BCM2837SDHost::reportBlockSize(UInt64 *blockSize) { *blockSize = fBlockSize; return kIOReturnSuccess; }
IOReturn BCM2837SDHost::reportEjectability(bool *isEjectable) { *isEjectable = false; return kIOReturnSuccess; }

IOReturn
BCM2837SDHost::reportMaxValidBlock(UInt64 *maxBlock)
{
	*maxBlock = (fBlockCount > 0) ? (fBlockCount - 1) : 0;
	return kIOReturnSuccess;
}

IOReturn
BCM2837SDHost::reportMediaState(bool *mediaPresent, bool *changedState)
{
	*mediaPresent = fCardPresent;
	if (changedState != NULL) {
		*changedState = false;
	}
	return kIOReturnSuccess;
}

IOReturn BCM2837SDHost::reportRemovability(bool *isRemovable) { *isRemovable = true; return kIOReturnSuccess; }
IOReturn BCM2837SDHost::reportWriteProtection(bool *isWriteProtected) { *isWriteProtected = false; return kIOReturnSuccess; }
