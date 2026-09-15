#include "BCM2837SDHCI.h"
#include "BCM2837Mailbox.h"

#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODeviceMemory.h>
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
	fBaseClockHz = 0;
	fSpecVersion = 0;
	fCurrentClockHz = 0;

	fPendingIntStatus = 0;
	fRealInterruptHits = 0;
	fPollFallbackHits = 0;
	fLoggedRealInterrupt = false;
	fLoggedPollFallback = false;
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
	fTransferLock = IOLockAlloc();
	if (fTransferLock == NULL) {
		IOSimpleLockFree(fStatusLock);
		fStatusLock = NULL;
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
		// Credited here, unconditionally, regardless of what any particular
		// waitForIntStatus32() call's mask cares about right now - this is
		// the one and only place that can honestly claim "a real ARM64 IRQ
		// reached this driver". (Counting this at the *consuming* side in
		// waitForIntStatus32() instead was tried first and was wrong: bits
		// in `known` outside a given call's mask are left set in
		// fPendingIntStatus for a *later* call to find already-pending on
		// its very first loop iteration, with no new interrupt or poll
		// having actually happened - that later call has no way to tell
		// "leftover from this function's own earlier poll" apart from "a
		// genuine interrupt fired just now". See fPollFallbackHits' own
		// comment for the symmetric fix on the poll side.)
		self->fRealInterruptHits++;
		IOSimpleLockUnlockEnableInterrupt(self->fStatusLock, istate);

		IOLockWakeup(self->fInterruptLock, (void *)&self->fPendingIntStatus, false);
	}
}

bool
BCM2837SDHCI::waitForIntStatus32(uint32_t mask, uint32_t timeoutUs, uint32_t *outStatus)
{
	// Two different lock types are load-bearing here, not interchangeable:
	// fStatusLock (IOSimpleLock) is the only thing interruptOccurred() may
	// touch, since it runs at interrupt level; fInterruptLock (IOLock) is
	// a real mutex only ever used from this thread-context side, needed
	// because IOLockSleepDeadline() requires a lockable IOLock to
	// atomically unlock-and-sleep on. IOLockWakeup() bridges the two - it
	// doesn't actually take fInterruptLock (see interruptOccurred()), it
	// just targets the same wait event address.
	//
	// KEPT, but now genuinely a safety net rather than the only working
	// path (2026-08-13): this sleeps in short (1ms) increments and
	// directly polls+acks the raw INT_STATUS register on every wakeup.
	// Real ARM64 interrupt delivery to this driver is now CONFIRMED
	// working (see the class header comment for the full corrected story
	// - it took two wrong turns first: a buggy hit-counter producing a
	// false "confirmed" claim, then a correctly-measured "genuinely 0
	// hits" result, before the real root cause - a devicetrees/rpi3.dts
	// interrupt-cell encoding bug, see that file's header comment - was
	// found and fixed). A real QEMU boot with the DT fix shows
	// fRealInterruptHits firing and fPollFallbackHits NOT appearing at all
	// - genuinely interrupt-driven now. Keeping the hybrid anyway rather
	// than reverting to a pure blocking IOLockSleepDeadline(): it's a
	// strict superset (a real interrupt still wins immediately via
	// IOLockWakeup() below), costs nothing, and is one less thing to
	// re-verify if this ever regresses. Verified end-to-end against a real
	// (emulated) SD card: full init sequence + a real PIO block read
	// returning correct data, genuinely interrupt-driven.
	IOLockLock(fInterruptLock);
	uint32_t matched = 0;
	uint32_t waitedUs = 0;
	const uint32_t kPollIntervalUs = 1000;
	for (;;) {
		IOInterruptState istate = IOSimpleLockLockDisableInterrupt(fStatusLock);
		matched = fPendingIntStatus & mask;
		if (matched != 0) {
			fPendingIntStatus &= ~matched;
		}
		IOSimpleLockUnlockEnableInterrupt(fStatusLock, istate);

		if (matched != 0) {
			// NOTE: do NOT attribute a hit-counter bump here. fPendingIntStatus
			// can be set either by interruptOccurred() OR by this same
			// function's own poll block below (in a *previous* call, for
			// mask bits that call didn't care about) - by the time we're
			// here we cannot tell which. Both real sources credit their own
			// counters directly at the point they actually observe the
			// register (see interruptOccurred() and the poll block below).
			break;
		}

		// Poll fallback: read+ack the raw register ourselves, same bit
		// handling as interruptOccurred() (see that function's own
		// comments for why exactly these bits and why ack unconditionally).
		uint32_t intmask = readReg32(kRegIntStatus);
		if (intmask != 0) {
			uint32_t known = intmask & (kIntCmdComplete | kIntTransferDone |
			    kIntBufWriteRdy | kIntBufReadRdy | kIntErrorMask);
			if (known != 0) {
				writeReg32(kRegIntStatus, known);
			}
			uint32_t unexpected = intmask & ~known;
			if (unexpected != 0) {
				writeReg32(kRegIntStatus, unexpected);
			}
			if (known != 0) {
				istate = IOSimpleLockLockDisableInterrupt(fStatusLock);
				fPendingIntStatus |= known;
				// Credited here, unconditionally on `known != 0`, not on
				// `matched != 0` below - this call's own direct register
				// read genuinely found real bits regardless of whether they
				// happen to be ones *this* call's mask wants (see
				// interruptOccurred()'s matching comment for why
				// mask-gating the counter was the original bug).
				fPollFallbackHits++;
				matched = fPendingIntStatus & mask;
				if (matched != 0) {
					fPendingIntStatus &= ~matched;
				}
				IOSimpleLockUnlockEnableInterrupt(fStatusLock, istate);
				if (matched != 0) {
					break;
				}
			}
		}

		if (waitedUs >= timeoutUs) {
			break;
		}

		uint64_t pollDeadline;
		clock_interval_to_deadline(kPollIntervalUs, kMicrosecondScale, &pollDeadline);
		IOLockSleepDeadline(fInterruptLock, (void *)&fPendingIntStatus, pollDeadline, THREAD_UNINT);
		waitedUs += kPollIntervalUs;
	}
	IOLockUnlock(fInterruptLock);

	// One-shot logging, done here rather than at either counter's
	// increment site: interruptOccurred() runs at interrupt level and may
	// not IOLog (see its own comment); this function's poll block *could*
	// safely log inline, but doing both counters' logging in the one place
	// that's always safe keeps the two paths symmetric. Benign race on the
	// counter reads below (no lock) - this is a one-time diagnostic message,
	// not something correctness depends on.
	if (!fLoggedRealInterrupt && fRealInterruptHits > 0) {
		fLoggedRealInterrupt = true;
		IOLog("BCM2837SDHCI: real hardware interrupt observed at least once "
		    "(interruptOccurred() -> waitForIntStatus32())\n");
	}
	if (!fLoggedPollFallback && fPollFallbackHits > 0) {
		fLoggedPollFallback = true;
		IOLog("BCM2837SDHCI: poll fallback resolved at least one wait "
		    "(no interrupt arrived within one %u us tick)\n", kPollIntervalUs);
	}

	if (outStatus != NULL) {
		*outStatus = matched;
	}
	return matched != 0;
}

bool
BCM2837SDHCI::pinmuxSDSlot(void)
{
	// GPIO base phys = 0x3f200000: arm-io's "ranges" (devicetrees/rpi3.dts,
	// child-base=0 / parent-base=0x3f000000) applied to "gpio"'s own
	// reg=0x200000 - the identical translation already verified correct
	// for this driver's own "sdhci" registers (0x3f00b200 for "intc").
	// Mapped directly by physical address rather than through the "gpio"
	// DT node/IOKit driver (GPIO isn't this driver's provider, and no
	// IOKit driver for "gpio" exists in this project yet regardless).
	const IOPhysicalAddress kGPIOBase = 0x3f200000;
	const IOByteCount kGPFSEL3Offset = 0x0C; // pins 30-39
	const IOByteCount kGPFSEL4Offset = 0x10; // pins 40-49
	const IOByteCount kGPFSEL5Offset = 0x14; // pins 50-59
	const IOByteCount kMapLen = (kGPFSEL5Offset - kGPFSEL3Offset) + sizeof(uint32_t);

	IODeviceMemory *devMem = IODeviceMemory::withRange(kGPIOBase + kGPFSEL3Offset, kMapLen);
	if (devMem == NULL) {
		IOLog("BCM2837SDHCI: pinmuxSDSlot: IODeviceMemory::withRange failed\n");
		return false;
	}
	IOMemoryMap *map = devMem->map();
	devMem->release();
	if (map == NULL) {
		IOLog("BCM2837SDHCI: pinmuxSDSlot: map() failed\n");
		return false;
	}

	volatile uint32_t *fsel = (volatile uint32_t *)map->getVirtualAddress();

	// REAL-HARDWARE BUG FOUND (2026-08-13 adversarial review) AND FIXED:
	// this function used to only touch GPFSEL4/5 (GPIO40-59), routing
	// GPIO48-53 to ALT3 for the physical SD slot - but never touched
	// GPFSEL3, so GPIO34-39 were left at the firmware's power-on default
	// of ALT3 too, which on a real Pi 3 Model B routes the on-board
	// BCM43438 WiFi/BT chip's SDIO lines to the SAME Arasan/EMMC
	// controller this driver drives. With both left muxed on, CMD0/CMD8/
	// ACMD41 broadcast to both devices and CMD2/CMD3 responses collide on
	// the shared open-drain CMD line - real silicon would see spurious
	// CRC errors or end up talking to the WiFi chip's RCA instead of the
	// SD card's. Invisible in QEMU: this driver's header already notes
	// QEMU parents the emulated SD card onto the bus at reset regardless
	// of GPFSEL, so there was never a collision to see in emulation.
	// Fixed by also setting GPIO34-39 to INPUT (0b000, the real
	// "unclaimed" state - not ALT3), so the on-board WiFi/BT chip's SDIO
	// bus is disconnected before GPIO48-53 claims the same controller for
	// the physical slot.
	const uint32_t kFSelInput = 0x0;
	const uint32_t kFSelAlt3 = 0x7;

	// GPFSEL3 (fsel[0]): GPIO34 at bits[14:12] .. GPIO39 at bits[29:27].
	uint32_t fsel3 = fsel[0];
	fsel3 = (fsel3 & ~(0x7U << 12)) | (kFSelInput << 12); // GPIO34 = input (was WiFi/BT ALT3)
	fsel3 = (fsel3 & ~(0x7U << 15)) | (kFSelInput << 15); // GPIO35
	fsel3 = (fsel3 & ~(0x7U << 18)) | (kFSelInput << 18); // GPIO36
	fsel3 = (fsel3 & ~(0x7U << 21)) | (kFSelInput << 21); // GPIO37
	fsel3 = (fsel3 & ~(0x7U << 24)) | (kFSelInput << 24); // GPIO38
	fsel3 = (fsel3 & ~(0x7U << 27)) | (kFSelInput << 27); // GPIO39
	fsel[0] = fsel3;

	// GPFSEL4 (fsel[1]): GPIO48 at bits[26:24], GPIO49 at bits[29:27].
	uint32_t fsel4 = fsel[1];
	fsel4 = (fsel4 & ~(0x7U << 24)) | (kFSelAlt3 << 24); // GPIO48 = ALT3
	fsel4 = (fsel4 & ~(0x7U << 27)) | (kFSelAlt3 << 27); // GPIO49 = ALT3
	fsel[1] = fsel4;

	// GPFSEL5 (fsel[2]): GPIO50/51/52/53 at bits[2:0]/[5:3]/[8:6]/[11:9].
	uint32_t fsel5 = fsel[2];
	fsel5 = (fsel5 & ~(0x7U << 0)) | (kFSelAlt3 << 0);   // GPIO50 = ALT3
	fsel5 = (fsel5 & ~(0x7U << 3)) | (kFSelAlt3 << 3);   // GPIO51 = ALT3
	fsel5 = (fsel5 & ~(0x7U << 6)) | (kFSelAlt3 << 6);   // GPIO52 = ALT3
	fsel5 = (fsel5 & ~(0x7U << 9)) | (kFSelAlt3 << 9);   // GPIO53 = ALT3
	fsel[2] = fsel5;

	IOLog("BCM2837SDHCI: pinmuxSDSlot: GPIO34-39 -> input, GPIO48-53 -> ALT3 "
	    "(GPFSEL3=%#x GPFSEL4=%#x GPFSEL5=%#x)\n", fsel3, fsel4, fsel5);

	// Deliberately not releasing `map`: confirmed via a live crash
	// (backtrace through IOMemoryMap::free() ->
	// IOGeneralMemoryDescriptor::memoryReferenceFree() -> a Mach IPC
	// send) that tearing down this particular kind of map - a fresh
	// IODeviceMemory::withRange()+map() with no IOService provider behind
	// it, unlike this driver's own fRegMap (provider->
	// mapDeviceMemoryWithIndex()) - isn't safe this early in boot in this
	// kernel-only environment; the machinery it exercises expects more
	// task/IPC infrastructure than exists yet. This runs exactly once at
	// start(), the mapping is 8 bytes, and never being freed for the
	// process's lifetime is the same tradeoff bring-up/firmware code
	// makes routinely for small, permanent control-register mappings -
	// leaking it here is the pragmatic choice, not an oversight.
	return true;
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

	// Backlog item 8: TIMEOUT_CONTROL was declared and never written,
	// leaving the data-timeout counter at its reset value - the
	// SHORTEST interval the field supports, not the longest. QEMU
	// doesn't implement DATA_TIMEOUT_ERR at all so this was invisible in
	// emulation; a real card that's merely slow (not actually failed)
	// could spuriously fail where QEMU never would. 0xE is the field's
	// maximum (TMCLK * 2^27, ~1s at typical base clocks) - the same
	// conservative "use the longest timeout rather than compute an exact
	// one" default Linux's sdhci.c falls back to when it isn't computing
	// a value from the live clock rate, appropriate here since this
	// driver's own clock rate is itself fixed/conservative (see
	// setClock()).
	writeReg8(kRegTimeoutControl, 0x0E);

	return true;
}

void
BCM2837SDHCI::resetCmdLine(void)
{
	// SDHCI_SOFTWARE_RESET bit1 = "reset cmd" (SOFTWARE_RESET_CMD): resets
	// only the command-inhibit logic/state machine, not the data path or
	// clock config - the correct, narrower recovery after a command
	// timeout/CRC/index error (SDHCI spec 3.7.1), matching Linux's
	// sdhci_reset(host, SDHCI_RESET_CMD).
	writeReg8(kRegSoftwareReset, 0x02);
	if (!pollUntil(^bool(void) { return (readReg8(kRegSoftwareReset) & 0x02) == 0; }, 100000)) {
		IOLog("BCM2837SDHCI: resetCmdLine: SOFTWARE_RESET_CMD did not clear\n");
	}
}

void
BCM2837SDHCI::resetDatLine(void)
{
	// CMD12 (STOP_TRANSMISSION) first, matching Linux's
	// sdhci_error_recovery()/mmc_send_stop() ordering - the card may still
	// be mid-transfer and the spec expects an explicit abort before the
	// host resets its own DAT logic out from under it. Best-effort only:
	// if CMD12 itself fails (e.g. the card is gone), still fall through to
	// SOFTWARE_RESET_DAT unconditionally rather than leaving DAT_INHIBIT
	// stuck - that stuck-forever state is exactly the bug this item exists
	// to fix.
	uint32_t r[4];
	sendCommand(12, 0, kCmdRespShortBusy, false, r);

	// SDHCI_SOFTWARE_RESET bit2 = "reset dat" (SOFTWARE_RESET_DAT): resets
	// the data path (buffer pointers, DAT_INHIBIT, block/transfer-mode
	// registers) without touching the command logic or clock config -
	// SDHCI spec 3.7.1, matching Linux's sdhci_reset(host, SDHCI_RESET_DATA).
	writeReg8(kRegSoftwareReset, 0x04);
	if (!pollUntil(^bool(void) { return (readReg8(kRegSoftwareReset) & 0x04) == 0; }, 100000)) {
		IOLog("BCM2837SDHCI: resetDatLine: SOFTWARE_RESET_DAT did not clear\n");
	}
}

/*
 * The divider math itself lives in BCM2837SDHCIDivider.h - see that file
 * for the two encodings, the safety argument, and why it is a standalone
 * pure function (tools/test_sdhci_divider.sh compiles the very same
 * source into a host-side exhaustive test). This thin forwarder exists
 * only so the rest of the driver can keep calling it as a class method.
 */
bool
BCM2837SDHCI::calcDivider(uint32_t baseHz, uint32_t targetHz, uint16_t specVersion,
    uint16_t *outClkField, uint32_t *outActualHz)
{
	return bcm2837SDHCICalcDivider(baseHz, targetHz, specVersion,
	           outClkField, outActualHz);
}

void
BCM2837SDHCI::readClockConfig(void)
{
	// HOST_VERSION's spec field is what decides the divider encoding
	// calcDivider() must use. QEMU's model reports 0x2402 here
	// (hw/sd/sdhci.c: version = (SDHC_HCVER_VENDOR << 8) | (spec - 1),
	// and hw/arm/bcm2835_peripherals.c sets sd-spec-version=3), i.e.
	// spec field 2 == SDHCI 3.00.
	//
	// 0xFE is the last 16-bit register in the mapped window - the "sdhci"
	// DT node's reg length is 0x100 (devicetrees/rpi3.dts), so this reads
	// the final two bytes of the mapping and nothing past it.
	uint16_t hostVersion = readReg16(kRegHostVersion);
	uint16_t specField = hostVersion & 0xFF;

	// A register block that reads back all-ones (unmapped/powered-off) or
	// reports a spec version that does not exist yields a spec field this
	// driver cannot interpret. Since "unknown" would otherwise fall
	// through to the >= 3.00 branch - the more permissive encoding, whose
	// bit layout on an actually-2.00 controller is undefined - refuse the
	// whole computed-divisor path in that case rather than pick an
	// encoding by default. SDHCI 4.x reports 3 here and keeps the 3.00
	// divider layout, so it is accepted.
	if (specField > 3) {
		IOLog("BCM2837SDHCI: HOST_VERSION=%#x has an uninterpretable spec "
		      "field %u - falling back to the conservative fixed divisor\n",
		    hostVersion, specField);
		return;
	}
	fSpecVersion = specField;

	// missing_caps=true on this SoC (Linux sdhci-iproc's bcm2835_data,
	// see class header): CAPABILITIES' base-clock field is explicitly
	// untrustworthy here, so the real rate comes from the only place that
	// actually knows it - the VideoCore firmware that programmed it. This
	// is the same source Linux uses on this SoC, via the DT clock
	// provider in drivers/clk/bcm/clk-raspberrypi.c.
	uint32_t hz = 0;
	if (!BCM2837Mailbox::getClockRateHz(BCM2837Mailbox::kClockIdEMMC, &hz)) {
		IOLog("BCM2837SDHCI: VideoCore mailbox did not report an EMMC base "
		      "clock - falling back to the conservative fixed divisor "
		      "(HOST_VERSION=%#x)\n", hostVersion);
		return;
	}

	// Sanity range, not a default: this only ever *rejects* a value, it
	// never invents one. Real SDHCI base clocks live in the low tens of
	// MHz to a couple hundred MHz (the Pi's EMMC clock is commonly
	// ~41-250MHz depending on core_freq; QEMU reports exactly 50MHz).
	// Anything outside 1MHz..1GHz means the response was not a clock rate
	// at all (a protocol/framing error, a firmware that answered a
	// different tag), and dividing by it could produce an arbitrarily
	// wrong - possibly far too fast - SD clock.
	const uint32_t kMinPlausibleBaseHz = 1000000;
	const uint32_t kMaxPlausibleBaseHz = 1000000000;
	if (hz < kMinPlausibleBaseHz || hz > kMaxPlausibleBaseHz) {
		IOLog("BCM2837SDHCI: firmware reported implausible EMMC base clock "
		      "%u Hz - ignoring it and falling back to the conservative "
		      "fixed divisor\n", hz);
		return;
	}

	fBaseClockHz = hz;
	IOLog("BCM2837SDHCI: HOST_VERSION=%#x (SDHCI spec field %u), EMMC base "
	      "clock %u Hz from VideoCore firmware\n",
	    hostVersion, fSpecVersion, fBaseClockHz);
}

bool
BCM2837SDHCI::setClock(uint32_t targetHz)
{
	// Initialised to the conservative fallback divisor, not to 0: 0 is the
	// divider field's "base clock, no division at all" encoding, i.e. the
	// fastest rate the controller can produce. If some future edit ever
	// leaves this unset on a path that reaches the register write, it
	// should produce a too-slow clock, not a too-fast one.
	uint16_t clkField = (uint16_t)(0xFFU << 8);
	uint32_t actualHz = 0;
	bool haveDivider = (fBaseClockHz != 0) &&
	    calcDivider(fBaseClockHz, targetHz, fSpecVersion, &clkField, &actualHz);

	if (!haveDivider) {
		// Pre-DAR-103 fallback, kept byte-for-byte: with no trustworthy
		// base clock there is nothing to compute a divisor *from*, so use
		// the largest value the 8-bit field can hold (SDCLK_FREQ_SEL,
		// bits [15:8]). It is not a legal 8-bit-mode encoding (that mode
		// wants exactly one bit set) and the resulting rate is genuinely
		// unknown, but it is the slowest thing this driver has ever
		// programmed and it is what every real boot to date has run on -
		// so it stays the fallback rather than something new and
		// untested. Deliberately NOT replaced by a guessed base clock:
		// guessing high enough to be "fast" is how you program a
		// data-phase clock the card cannot keep up with, which corrupts
		// data instead of merely being slow.
		clkField = (uint16_t)(0xFFU << 8);
		actualHz = 0;
	}

	writeReg16(kRegClockControl, 0x0000); // disable SD clock while reprogramming

	uint16_t clk = clkField | 0x01 /* internal clock enable */;
	writeReg16(kRegClockControl, clk);

	if (!pollUntil(^bool(void) { return (readReg16(kRegClockControl) & 0x02) != 0; }, 100000)) {
		IOLog("BCM2837SDHCI: internal clock never stabilized\n");
		return false;
	}

	writeReg16(kRegClockControl, clk | 0x04 /* SD clock enable */);
	fCurrentClockHz = actualHz;

	// The SD clock was gated off across the reprogramming above. The SD
	// Physical Layer spec requires at least 8 clock cycles of running
	// clock, bus idle, before the host may issue a command after a clock
	// stop/restart. This never mattered while setClock() was called
	// exactly once at start() (start()'s own 74-clock power-up delay
	// covered it by a wide margin), but tryRaiseDataPhaseClock() now calls
	// it a second time with a real command following immediately. 200us is
	// >= 8 clocks at any rate this driver can end up on, including the
	// unknown-but-slow fallback divisor, and is paid at most twice per
	// boot.
	IODelay(200);

	if (actualHz != 0) {
		IOLog("BCM2837SDHCI: SD clock %u Hz (target %u Hz, base %u Hz, "
		      "CLOCK_CONTROL divider field %#x)\n",
		    actualHz, targetHz, fBaseClockHz, clkField);
	} else {
		IOLog("BCM2837SDHCI: SD clock left on the conservative fixed "
		      "divisor (no trustworthy base clock; target was %u Hz)\n",
		    targetHz);
	}
	return true;
}

bool
BCM2837SDHCI::tryRaiseDataPhaseClock(void)
{
	// SD default speed: 0-25MHz, 3.3V, no CMD6 function switch and no bus
	// widening involved. Every SD card is required to support it once
	// identification has completed, so this needs no negotiation with the
	// card - unlike high speed (>25MHz, CMD6) or 4-bit bus width (ACMD6),
	// neither of which this driver does. Staying at default speed is also
	// consistent with the SDHCI_QUIRK_NO_HISPD_BIT / preset-broken quirks
	// this SoC carries (see class header).
	const uint32_t kDefaultSpeedHz = kDefaultSpeedClockHz;

	if (fBaseClockHz == 0) {
		return false; // nothing to compute from - stay exactly where we were
	}
	if (!fCardPresent) {
		return false;
	}

	uint16_t clkField = 0; // out param only; setClock() recomputes it
	uint32_t candidateHz = 0;
	if (!calcDivider(fBaseClockHz, kDefaultSpeedHz, fSpecVersion,
	    &clkField, &candidateHz)) {
		IOLog("BCM2837SDHCI: no divider reaches <=%u Hz from a %u Hz base - "
		      "staying at the identification clock\n",
		    kDefaultSpeedHz, fBaseClockHz);
		return false;
	}
	if (candidateHz <= fCurrentClockHz) {
		return false; // already there (or the base clock is just slow)
	}

	// Real end-to-end check rather than trusting the arithmetic alone: read
	// a reference block at the known-good identification clock, raise the
	// clock, then read the same block again and compare. On a board where
	// the higher rate does not actually work (marginal signal integrity,
	// a base clock the firmware reported but did not really program, a
	// divider encoding this controller implements differently than its
	// HOST_VERSION claims), this catches it at boot, on the real hardware,
	// and backs out - instead of silently handing corrupt blocks to HFS.
	// It cannot prove the absence of *intermittent* corruption; it does
	// prove the data path works at all at the new rate.
	//
	// Block 0 is used because it always exists on a mounted card and is
	// only ever read here, never written.
	const uint32_t kRefBlock = 0;
	uint8_t *before = (uint8_t *)IOMalloc(fBlockSize);
	uint8_t *after = (uint8_t *)IOMalloc(fBlockSize);
	if (before == NULL || after == NULL) {
		if (before != NULL) {
			IOFree(before, fBlockSize);
		}
		if (after != NULL) {
			IOFree(after, fBlockSize);
		}
		return false;
	}

	bool raised = false;
	bool touchedClock = false;
	uint32_t slowHz = fCurrentClockHz;
	bool refOK = readBlocksPIO(kRefBlock, 1, before);
	bool clockOK = false;
	if (refOK) {
		// setClock() can fail *after* having already written
		// CLOCK_CONTROL (it fails at the internal-clock-stable poll, with
		// SD_CLOCK_EN not yet re-asserted), so once this call has been
		// made at all the clock registers are dirty regardless of its
		// result and must be reprogrammed rather than assumed untouched.
		// Hence touchedClock being set before the call, not after it.
		touchedClock = true;
		clockOK = setClock(kDefaultSpeedHz);
	}

	if (!refOK) {
		IOLog("BCM2837SDHCI: reference read at the identification clock "
		      "failed - not raising the SD clock\n");
	} else if (!clockOK) {
		IOLog("BCM2837SDHCI: setClock(%u) failed - reverting to the "
		      "identification clock\n", kDefaultSpeedHz);
	} else if (!readBlocksPIO(kRefBlock, 1, after)) {
		IOLog("BCM2837SDHCI: read-back at %u Hz FAILED - reverting to the "
		      "identification clock\n", candidateHz);
	} else if (memcmp(before, after, fBlockSize) != 0) {
		IOLog("BCM2837SDHCI: read-back at %u Hz returned DIFFERENT data than "
		      "at %u Hz - reverting to the identification clock\n",
		    candidateHz, slowHz);
	} else {
		// DAR-218: bus width is negotiated once in cardIdentify(), before
		// this clock-raise ever runs - report what's actually live in
		// kRegHostControl rather than a hardcoded "1-bit", which would
		// now be wrong on any card widenBusTo4Bit() succeeded on.
		bool is4Bit = (readReg8(kRegHostControl) & kHostControl4BitBus) != 0;
		IOLog("BCM2837SDHCI: SD clock raised to %u Hz (default speed, %s "
		      "bus); block %u verified identical before and after\n",
		    candidateHz, is4Bit ? "4-bit" : "1-bit", kRefBlock);
		raised = true;
	}

	if (!raised && touchedClock) {
		// Put the clock back exactly where cardIdentify() ran - including
		// after a failed setClock(), which may have left CLOCK_CONTROL
		// half-programmed with the SD clock gated off. If even this fails
		// there is nothing further this driver can do about it, but the
		// log above already says what happened.
		if (!setClock(kIdentificationClockHz)) {
			IOLog("BCM2837SDHCI: reverting to the identification clock also "
			      "failed - the SD bus is in an unknown state\n");
		}
		// A failed read leaves the data path needing the SDHCI-spec
		// recovery the read paths already perform; the clock change on top
		// of it does not, but re-issuing CMD13 is a cheap way to confirm
		// the card is still responsive at the restored clock and to leave
		// a diagnosable log line if it is not.
		uint32_t st[4];
		if (fRCA != 0 && !sendCommand(13, fRCA << 16, kCmdRespShort, false, st)) {
			IOLog("BCM2837SDHCI: card did not answer CMD13 after reverting "
			      "the clock\n");
		}
	}

	IOFree(before, fBlockSize);
	IOFree(after, fBlockSize);
	return raised;
}

bool
BCM2837SDHCI::sendCommand(uint8_t cmdIndex, uint32_t arg, uint32_t respType, bool hasData, uint32_t resp[4])
{
	if (!waitClear32(kRegPresentState, kPresentCmdInhibit, 1000000)) {
		IOLog("BCM2837SDHCI: CMD_INHIBIT stuck before CMD%u - resetting cmd line\n", cmdIndex);
		// Backlog P0#2: this used to just log and fail, leaving the stuck
		// inhibit bit for every subsequent command to die on too. A stuck
		// CMD_INHIBIT this early (before we've even issued anything) means
		// a prior transaction's cleanup never finished - the SDHCI-spec
		// recovery for that is a command-line reset, not a wait-forever.
		resetCmdLine();
		return false;
	}
	if (hasData && !waitClear32(kRegPresentState, kPresentDatInhibit, 1000000)) {
		IOLog("BCM2837SDHCI: DAT_INHIBIT stuck before CMD%u - resetting dat line\n", cmdIndex);
		writeReg8(kRegSoftwareReset, 0x04); // SOFTWARE_RESET_DAT only - no card to send CMD12 to yet
		pollUntil(^bool(void) { return (readReg8(kRegSoftwareReset) & 0x04) == 0; }, 100000);
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
		IOLog("BCM2837SDHCI: CMD%u timed out (no CMD_COMPLETE/error) raw INT_STATUS=%#x PRESENT_STATE=%#x\n",
		    cmdIndex, readReg32(kRegIntStatus), readReg32(kRegPresentState));
		// Backlog P0#2: a real command timeout leaves CMD_INHIBIT asserted
		// with nothing to ever clear it again (the completion that would
		// normally do so never arrived) - every subsequent command would
		// die on the very first waitClear32() above. Reset the command
		// line so the controller comes back to a clean, usable state.
		resetCmdLine();
		return false;
	}
	if (status & kIntErrorMask) {
		IOLog("BCM2837SDHCI: CMD%u error, INT_STATUS=%#x\n", cmdIndex, status);
		// Same recovery as the timeout case above - a real CRC/index/end-bit
		// error (SDHCI spec 3.7.1) requires SOFTWARE_RESET_CMD before the
		// controller can reliably accept another command.
		resetCmdLine();
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

/*
 * Real ACMD51 (SEND_SCR) + ACMD6 (SET_BUS_WIDTH) negotiation (DAR-218).
 * See this file's header comment for why setting kRegHostControl's 4-bit
 * bit without this sequence would be data corruption, not a clean
 * failure: the card and controller must agree on bus width.
 *
 * ACMD51 is a real 8-byte data-phase read, done here as its own tiny PIO
 * transfer rather than reusing readBlocksPIO() (which is sized around
 * fBlockSize/512-byte block-storage reads) - kRegBlockSize/BlockCount are
 * left at 8/1 after this call, but that's harmless: cardIdentify() always
 * calls CMD16 (SET_BLOCKLEN) right after this, which resets
 * kRegBlockSize back to 512 unconditionally.
 */
bool
BCM2837SDHCI::widenBusTo4Bit(void)
{
	uint32_t r55[4];
	if (!sendCommand(55, fRCA << 16, kCmdRespShort, false, r55)) {
		IOLog("BCM2837SDHCI: CMD55 (before ACMD51) failed\n");
		return false;
	}

	writeReg16(kRegBlockSize, 8);
	writeReg16(kRegBlockCount, 1);
	writeReg16(kRegTransferMode, kCmdTransBlkCntEn | kCmdTransRead);

	uint32_t r51[4];
	if (!sendCommand(51, 0, kCmdRespShort, true, r51)) {
		IOLog("BCM2837SDHCI: ACMD51 (SEND_SCR) command failed\n");
		return false;
	}

	uint32_t status;
	if (!waitForIntStatus32(kIntBufReadRdy | kIntErrorMask, 1000000, &status)) {
		IOLog("BCM2837SDHCI: ACMD51 data phase timed out - resetting dat line\n");
		resetDatLine();
		return false;
	}
	if (status & kIntErrorMask) {
		IOLog("BCM2837SDHCI: ACMD51 data phase error, INT_STATUS=%#x - resetting dat line\n", status);
		resetDatLine();
		return false;
	}

	// Unlike the CMD-line response registers (a defined hardware layout
	// that sendCommand()'s R2 handling already gets as resp[0]=high 32
	// bits), kRegBuffer is a raw DAT-line FIFO: each 32-bit read packs
	// whatever bytes are next in arrival order into the word
	// little-endian - the FIRST byte off the wire (SCR's MSB, scr[0])
	// lands in the LOW byte of the word, not the high one. Confirmed
	// against QEMU's own sdhci_read_dataport() (hw/sd/sdhci.c):
	// `value |= fifo_buffer[data_count] << i*8` with i starting at 0 for
	// the first byte consumed.
	uint32_t scrWord0 = readReg32(kRegBuffer); // scr[0..3], scr[0] in bits[7:0]
	(void)readReg32(kRegBuffer); // scr[4..7] - unused, nothing this driver needs lives there

	if (!waitForIntStatus32(kIntTransferDone | kIntErrorMask, 1000000, &status)) {
		IOLog("BCM2837SDHCI: ACMD51 did not reach TRANSFER_COMPLETE - resetting dat line\n");
		resetDatLine();
		return false;
	}
	if (status & kIntErrorMask) {
		IOLog("BCM2837SDHCI: ACMD51 error at TRANSFER_COMPLETE, INT_STATUS=%#x - resetting dat line\n", status);
		resetDatLine();
		return false;
	}

	// SD_BUS_WIDTHS is the low nibble of scr[1] (SCR bits [51:48]). scr[1]
	// is the second byte consumed, so per the little-endian FIFO packing
	// above it lands in scrWord0 bits [15:8]. Bit 2 of that nibble (0x4)
	// is "card supports 4-bit DAT[3:0]"; bit 0 (0x1, DAT0-only) is always
	// set and not itself useful here. Verified against QEMU's own
	// sd_set_scr() (hw/sd/sd.c: scr[1] = (2<<4)|0b0101) and the SD
	// Physical Layer spec's SCR field table, not derived from memory of
	// the spec alone.
	uint32_t sdBusWidths = (scrWord0 >> 8) & 0xF;
	if (!(sdBusWidths & 0x4)) {
		IOLog("BCM2837SDHCI: card SCR reports no 4-bit support (SD_BUS_WIDTHS=%#x) - staying at 1-bit\n",
		    sdBusWidths);
		return false;
	}

	uint32_t r55b[4], r6[4];
	if (!sendCommand(55, fRCA << 16, kCmdRespShort, false, r55b)) {
		IOLog("BCM2837SDHCI: CMD55 (before ACMD6) failed\n");
		return false;
	}
	// ACMD6 arg bits[1:0]: 00 = 1-bit (default), 10 = 4-bit. This is what
	// actually switches the CARD side; only once it succeeds is it safe
	// to flip the host's own bit below.
	if (!sendCommand(6, 0x2, kCmdRespShort, false, r6)) {
		IOLog("BCM2837SDHCI: ACMD6 (SET_BUS_WIDTH) failed\n");
		return false;
	}

	writeReg8(kRegHostControl, readReg8(kRegHostControl) | kHostControl4BitBus);
	IOLog("BCM2837SDHCI: bus widened to 4-bit (card SCR confirmed support, ACMD6 negotiated)\n");
	return true;
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
		if (!sendCommand(41, 0x40FF8000, kCmdRespShortNoChecks, false, r41)) {
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

	// DAR-218: negotiate 4-bit bus width now that the card is selected
	// (ACMD-prefixed commands and data transfers both require transfer
	// state). Not fatal if this returns false - that just means "stayed
	// at 1-bit", either because the card doesn't support it or the
	// negotiation itself failed; either way CMD16 right below restores
	// kRegBlockSize regardless of what widenBusTo4Bit() left it at.
	widenBusTo4Bit();

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

	// Real-hardware correctness only - confirmed NOT required for QEMU
	// (see pinmuxSDSlot()'s own comment) - failure here isn't fatal to
	// starting since QEMU testing doesn't need it, but log it either way.
	if (!pinmuxSDSlot()) {
		IOLog("BCM2837SDHCI: pinmuxSDSlot failed - continuing anyway (harmless on QEMU, real hardware needs this)\n");
	}

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

	// Backlog P0#4: this used to enable the clock (setClock() below)
	// *before* turning on bus power at all, and sent CMD0 with no
	// settling delay after either - the reverse of the real SD Physical
	// Layer spec's power-up sequencing (6.4.1/"Power Up" figure): Vdd
	// ramp first, wait for it to stabilize, only then start the clock,
	// and hold the clock running for >=74 clocks with the bus idle before
	// the first command. Real cards can misbehave (wrong power-on state
	// latched, CMD0 arriving before the card's own internal reset
	// completes) if commands start before this settles - QEMU's SD model
	// has no such power/clock-dependent reset behavior to catch the old
	// ordering being wrong, which is exactly why this went unnoticed.
	//
	// SDHCI_POWER_CONTROL: 3.3V (0x0E) | SD_BUS_POWER (0x01) - now first.
	writeReg8(kRegPowerControl, 0x0F);
	// >=1ms after Vdd is asserted before the clock may start (spec
	// 6.4.1). IODelay is a busy-wait; this path runs once at start(),
	// long before interrupts/scheduling pressure matter here.
	IODelay(1000);

	// DAR-103: establish the controller's real base clock and divider
	// encoding before the first setClock(), so that call can compute a
	// real divisor instead of falling back to the fixed conservative one.
	// Must come after resetHost() (which is a full SOFTWARE_RESET_ALL) so
	// nothing this reads or programs is about to be reset out from under
	// it, and before setClock() for the obvious reason.
	readClockConfig();

	if (!setClock(kIdentificationClockHz)) {
		return false;
	}
	// >=74 SD clocks with CMD held high (the hardware idles CMD high
	// automatically once the clock is running and nothing has been
	// issued yet) before the first command. This used to be a flat 250us,
	// derived from "74 clocks at 400kHz is ~185us, round up". That
	// derivation only holds if the clock really is ~400kHz; setClock()
	// guarantees <= its target, not == it, so a slower programmed rate
	// makes 74 clocks take *longer* and could outrun a fixed 250us.
	// Compute it from the rate actually programmed when that is known,
	// doubled for margin, and never shorter than the old 250us. Bounded
	// so an unexpectedly slow clock can't turn this into a long busy-wait.
	uint32_t settleUs = 250;
	if (fCurrentClockHz != 0) {
		uint64_t us = (74ULL * 1000000ULL + fCurrentClockHz - 1) / fCurrentClockHz;
		us *= 2;
		if (us > 10000) {
			us = 10000;
		}
		if (us > settleUs) {
			settleUs = (uint32_t)us;
		}
	}
	IODelay(settleUs);

	if (!cardIdentify()) {
		IOLog("BCM2837SDHCI: no card identified at start() - device will report no media\n");
		// Not fatal: publish the nub anyway so it's visible in the
		// registry / a card can be identified later. reportMediaState
		// reflects fCardPresent honestly either way.
	} else {
		// DAR-103: identification is done, so the bus may leave the
		// 400kHz identification clock for default speed. Self-verifying
		// and self-reverting (see tryRaiseDataPhaseClock()); a false
		// return just means the bus stays at the identification clock,
		// which is exactly where every boot before this change ran.
		tryRaiseDataPhaseClock();
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
	if (fTransferLock != NULL) {
		IOLockFree(fTransferLock);
		fTransferLock = NULL;
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
			IOLog("BCM2837SDHCI: read block %u/%u timed out waiting BUFFER_READ_READY - resetting dat line\n", b, nblks);
			// Backlog P0#2: a stuck DAT_INHIBIT here previously meant every
			// later command touching the data path would die too.
			resetDatLine();
			return false;
		}
		if (status & kIntErrorMask) {
			IOLog("BCM2837SDHCI: read error, INT_STATUS=%#x - resetting dat line\n", status);
			resetDatLine();
			return false;
		}

		for (uint32_t w = 0; w < fBlockSize / 4; w++) {
			*dst++ = readReg32(kRegBuffer);
		}
	}

	uint32_t xferStatus;
	if (!waitForIntStatus32(kIntTransferDone | kIntErrorMask, 1000000, &xferStatus)) {
		IOLog("BCM2837SDHCI: read did not reach TRANSFER_COMPLETE - resetting dat line\n");
		resetDatLine();
		return false;
	}
	if (xferStatus & kIntErrorMask) {
		IOLog("BCM2837SDHCI: read error at TRANSFER_COMPLETE, INT_STATUS=%#x - resetting dat line\n", xferStatus);
		resetDatLine();
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
			IOLog("BCM2837SDHCI: write block %u/%u timed out waiting BUFFER_WRITE_READY - resetting dat line\n", b, nblks);
			resetDatLine();
			return false;
		}
		if (status & kIntErrorMask) {
			IOLog("BCM2837SDHCI: write error, INT_STATUS=%#x - resetting dat line\n", status);
			resetDatLine();
			return false;
		}

		for (uint32_t w = 0; w < fBlockSize / 4; w++) {
			writeReg32(kRegBuffer, *src++);
		}
	}

	uint32_t xferStatus;
	if (!waitForIntStatus32(kIntTransferDone | kIntErrorMask, 1000000, &xferStatus)) {
		IOLog("BCM2837SDHCI: write did not reach TRANSFER_COMPLETE - resetting dat line\n");
		resetDatLine();
		return false;
	}
	if (xferStatus & kIntErrorMask) {
		IOLog("BCM2837SDHCI: write error at TRANSFER_COMPLETE, INT_STATUS=%#x - resetting dat line\n", xferStatus);
		resetDatLine();
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

	// Backlog item 16: neither this function nor readBlocksPIO()/
	// writeBlocksPIO() validated nblks before this fix.
	// writeReg16(kRegBlockCount, (uint16_t)nblks) truncates any request
	// >=65536 blocks (32MB) silently, and nothing checked
	// block+nblks <= fBlockCount either - a request past the end of the
	// card would have been issued to hardware as-is. Reject both here,
	// once, before doing anything else (including the IOMalloc bounce
	// allocation below, which a huge bogus nblks would otherwise attempt
	// at full size for no reason).
	if (nblks == 0 || nblks >= 65536 || block > fBlockCount || (fBlockCount - block) < nblks) {
		IOLog("BCM2837SDHCI: doAsyncReadWrite: bad request block=%llu nblks=%llu fBlockCount=%llu\n",
		    (unsigned long long)block, (unsigned long long)nblks, (unsigned long long)fBlockCount);
		IOStorage::complete(completion, kIOReturnBadArgument, 0);
		return kIOReturnSuccess;
	}

	bool isWrite = (buffer->getDirection() == kIODirectionOut);
	IOByteCount transferSize = nblks * fBlockSize;

	// PIO only (see header) - stage through a bounce buffer rather than
	// assuming `buffer` is a single contiguous, CPU-addressable range.
	// Allocated before taking fTransferLock: IOMalloc can block/sleep and
	// there's no need to hold the hardware lock across it.
	void *bounce = IOMalloc((size_t)transferSize);
	if (bounce == NULL) {
		IOStorage::complete(completion, kIOReturnNoMemory, 0);
		return kIOReturnSuccess;
	}

	// Backlog P0#1: serialize the whole command-issue-through-completion
	// span against any other concurrent doAsyncReadWrite() call (see
	// fTransferLock's own header comment for why a plain mutex is a
	// sufficient critical section for this specific, already-synchronous
	// driver). Without this, two overlapping requests (now plausible with
	// two mounted partitions) could interleave register writes and both
	// consume from the same fPendingIntStatus, corrupting or hanging one
	// of them.
	IOLockLock(fTransferLock);

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

	IOLockUnlock(fTransferLock);

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

IOReturn
BCM2837SDHCI::doSynchronize(UInt64 /*block*/, UInt64 /*nblks*/,
    IOStorageSynchronizeOptions /*options*/)
{
	// Real root cause of the "jnl: b(14, 2): flushing fs disk buffer
	// returned 0x2d" console spam seen on every HFS journal transaction
	// commit: IOBlockStorageDevice::doSynchronize()'s real Apple default
	// body (iokit/Storage/IOBlockStorageDevice.cpp) falls back to the
	// deprecated doSynchronizeCache(), whose own default returns
	// kIOReturnUnsupported (-> ENOTSUP -> 0x2d) when a subclass doesn't
	// override either one - which this driver never did, since neither is
	// one of IOBlockStorageDevice's 13 *pure*-virtual methods this class
	// otherwise implements in full.
	//
	// The correct fix is to report success here, not to silence
	// hfs_journal.c's print - by the time DKIOCSYNCHRONIZE reaches this
	// driver there is genuinely nothing left to flush:
	//   - writeBlocksPIO() (above) blocks this calling thread on
	//     waitForIntStatus32(kIntTransferDone, ...) - the SDHCI
	//     controller's TRANSFER_COMPLETE status, which per the SD
	//     Physical Layer spec is only asserted once the card has driven
	//     DAT0 busy through to completion of its internal program
	//     operation. doAsyncReadWrite() doesn't return (and thus this
	//     driver doesn't consider a write done) until that has already
	//     happened - i.e. writes are already durable on the card before
	//     the journal's next DKIOCSYNCHRONIZE could even be issued.
	//   - There is no software write-back cache anywhere in this driver's
	//     PIO path (no bounce-then-defer, no queueing) for a barrier/flush
	//     to drain.
	//   - Standard SD (unlike eMMC, which defines CMD6 CACHE_CTRL plus
	//     CMD44/45 FLUSH_CACHE for cards that opt into an internal write
	//     cache) has no generalized cache-flush command in the base
	//     Physical Layer spec at all, so there's no real hardware command
	//     this driver could issue even if it wanted to go further.
	// Acknowledging the barrier as satisfied is therefore the real,
	// correct answer for this device, not a lie dressed up as one.
	return kIOReturnSuccess;
}
