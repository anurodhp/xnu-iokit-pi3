#include "BCM2837PWM.h"
#include "BCM2837PWMMath.h"

#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>

#define super IOService
OSDefineMetaClassAndStructors(BCM2837PWM, IOService)

enum {
	kRegCtl  = 0x00 / 4,
	kRegSta  = 0x04 / 4,
	kRegDmac = 0x08 / 4,
	kRegRng1 = 0x10 / 4,
	kRegDat1 = 0x14 / 4,
	kRegFif1 = 0x18 / 4,
	kRegRng2 = 0x20 / 4,
	kRegDat2 = 0x24 / 4,

	// CTL (datasheet ch. 9, see the header's caveat)
	kCtlPwen1 = 1U << 0,
	kCtlMode1 = 1U << 1,    // 0 = PWM, 1 = serialiser
	kCtlRptl1 = 1U << 2,    // repeat the last word when the FIFO runs dry
	kCtlRptl2 = 1U << 10,
	kCtlUsef1 = 1U << 5,    // take data from the FIFO
	kCtlClrf1 = 1U << 6,    // clear the FIFO
	kCtlMsen1 = 1U << 7,    // mark-space instead of the balanced algorithm
	kCtlPwen2 = 1U << 8,
	kCtlUsef2 = 1U << 13,
	kCtlMsen2 = 1U << 15,

	// DMAC
	kDmacEnab  = 1U << 31,
	kDmacPanic = 7U << 8,
	kDmacDreq  = 7U,

	// STA error flags, write 1 to clear: WERR1 RERR1 GAPO1-4 BERR
	kStaErrors = 0x1fc,
	kRegLen    = 0x28,
};

bool
BCM2837PWM::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}
	// Audio is on by default; boot-arg audio=0 turns it off (DARW-320). A node
	// marked "sim-only" (devicetrees/rpi3-pwmtest.dts, for QEMU, which maps no
	// PWM block) is only used with pwm-sim=1, since touching the registers
	// there takes an external abort.
	uint32_t audioArg = 1, simArg = 0, testArg = 0;
	PE_parse_boot_argn("audio", &audioArg, sizeof(audioArg));
	bool wantSim = PE_parse_boot_argn("pwm-sim", &simArg, sizeof(simArg)) && simArg;
	bool wantTest = PE_parse_boot_argn("pwm-test", &testArg, sizeof(testArg)) && testArg;
	if ((audioArg == 0 && !wantTest && !wantSim) || (provider->getProperty("sim-only") && !wantSim)) {
		return false;
	}
	fRegMap = provider->mapDeviceMemoryWithIndex(0);
	if (fRegMap == NULL || fRegMap->getLength() < kRegLen) {
		IOLog("BCM2837PWM: cannot map the register block\n");
		OSSafeReleaseNULL(fRegMap);
		return false;
	}
	fRegs = (volatile uint32_t *)fRegMap->getVirtualAddress();
	uint32_t sim = 0;
	fSim = PE_parse_boot_argn("pwm-sim", &sim, sizeof(sim)) && sim;
	if (fSim) {
		bzero(fSimRegs, sizeof(fSimRegs));
		fRegs = fSimRegs;       // never touch the (unmapped on QEMU) hardware
		IOLog("BCM2837PWM: simulated (pwm-sim=1), registers in RAM\n");
	}
	fLock = IOLockAlloc();
	if (fLock == NULL) {
		OSSafeReleaseNULL(fRegMap);
		return false;
	}
	fClockHz = 0;
	fRng = 0;
	fRunning = false;

	// Report what the firmware left behind; reads only.
	dumpRegs("at start");
	registerService();

	uint32_t test = 0;
	if (PE_parse_boot_argn("pwm-test", &test, sizeof(test)) && test) {
		selfTest();
	}
	return true;
}

void
BCM2837PWM::stop(IOService *provider)
{
	if (fRegs && fRunning) {
		stopOutput();
	}
	OSSafeReleaseNULL(fRegMap);
	fRegs = NULL;
	if (fLock) {
		IOLockFree(fLock);
		fLock = NULL;
	}
	super::stop(provider);
}

uint32_t
BCM2837PWM::readReg(uint32_t offset)
{
	return (fRegs && offset < kRegLen) ? fRegs[offset / 4] : 0;
}

void
BCM2837PWM::writeReg(uint32_t offset, uint32_t value)
{
	if (fRegs && offset < kRegLen) {
		fRegs[offset / 4] = value;
	}
}

void
BCM2837PWM::dumpRegs(const char *tag)
{
	IOLog("BCM2837PWM: %s: CTL=%#x STA=%#x DMAC=%#x RNG1=%#x DAT1=%#x RNG2=%#x DAT2=%#x\n", tag,
	    readReg(0x00), readReg(0x04), readReg(0x08), readReg(0x10), readReg(0x14),
	    readReg(0x20), readReg(0x24));
}

// CM_PWMCTL / CM_PWMDIV in the clock manager (cprman, bus 0x7e101000, u-boot
// bcm283x.dtsi:138-141). From Linux raspberrypi/linux rpi-6.6.y
// drivers/clk/bcm/clk-bcm2835.c: password 0x5a000000 (:41), CM_PWMCTL 0xa0 /
// CM_PWMDIV 0xa4 (:86-87), ENAB bit 4, KILL 5, GATE 6, BUSY 7 (:124-130), SRC
// [3:0] with 6 = plld_per (:144, parent list :1623-1632), 12.12 divider (:2206-
// 2214) so DIVI is [23:12]. The disable step is Linux's bcm2835_clock_off()
// (:1089-1101); the start sequence is Circle's CGPIOClock::Start() (lib/
// gpioclock.cpp:66-85) with GATE set alongside ENAB as Linux's
// bcm2835_clock_on() does (:1104-1115). plld_per is set by the firmware, nominally
// 500 MHz (Circle machineinfo.cpp), measured at 5/8 of that on a real Pi 3.
bool
BCM2837PWM::programClock()
{
	const IOPhysicalAddress kCmPwm = 0x3f101000 + 0xa0;
	const uint32_t kPw = 0x5a000000, kEnab = 1U << 4, kKill = 1U << 5, kGate = 1U << 6, kBusy = 1U << 7, kSrcPllD = 6;
	IODeviceMemory *mem = IODeviceMemory::withRange(kCmPwm, 2 * sizeof(uint32_t));
	if (mem == NULL) {
		return false;
	}
	IOMemoryMap *map = mem->map(kIOMapInhibitCache);
	mem->release();
	if (map == NULL) {
		return false;
	}
	volatile uint32_t *ctl = (volatile uint32_t *)map->getVirtualAddress();
	volatile uint32_t *div = ctl + 1;

	// Stop the clock and wait for it to idle (KILL if it will not).
	*ctl = kPw | (*ctl & ~kEnab & 0xffffff);
	for (int i = 0; i < 1000 && (*ctl & kBusy); i++) {
		IODelay(1);
	}
	if (*ctl & kBusy) {
		*ctl = kPw | kKill;
		for (int i = 0; i < 1000 && (*ctl & kBusy); i++) {
			IODelay(1);
		}
	}
	bool ok = !(*ctl & kBusy);
	if (ok) {
		*div = kPw | (kPWMClockDivisor << 12);          // integer divide, DIVF = 0
		*ctl = kPw | kSrcPllD;                          // source PLLD, MASH off
		IODelay(10);
		*ctl = kPw | kSrcPllD | kEnab | kGate;
		for (int i = 0; i < 1000 && !(*ctl & kBusy); i++) {
			IODelay(1);
		}
		ok = (*ctl & kBusy) != 0;
	}
	if (!ok) {
		IOLog("BCM2837PWM: PWM clock did not start (CM_PWMCTL=%#x CM_PWMDIV=%#x)\n", *ctl, *div);
	}
	map->release();
	return ok;
}

// GPIO40/41 -> ALT0 (GPFSEL4 bits 2:0 and 5:3, function 0b100), the same
// phys-mapped access BCM2837SDHCI::pinmuxSDSlot() uses for the GPIO block.
bool
BCM2837PWM::muxPins()
{
	const IOPhysicalAddress kGPFSEL4 = 0x3f200010;
	IODeviceMemory *mem = IODeviceMemory::withRange(kGPFSEL4, sizeof(uint32_t));
	if (mem == NULL) {
		return false;
	}
	IOMemoryMap *map = mem->map(kIOMapInhibitCache);
	mem->release();
	if (map == NULL) {
		return false;
	}
	volatile uint32_t *fsel = (volatile uint32_t *)map->getVirtualAddress();
	uint32_t v = *fsel;
	v &= ~((7U << 0) | (7U << 3));      // pins 40, 41
	v |= (4U << 0) | (4U << 3);         // ALT0 = 0b100
	*fsel = v;
	map->release();
	return true;
}

IOReturn
BCM2837PWM::configure(uint32_t rateHz, uint32_t *rngOut, uint32_t *actualRateOut)
{
	if (fRegs == NULL || rateHz < 8000 || rateHz > 192000) {
		return kIOReturnBadArgument;
	}
	IOLockLock(fLock);
	IOReturn r = configureLocked(rateHz, rngOut, actualRateOut);
	IOLockUnlock(fLock);
	return r;
}

IOReturn
BCM2837PWM::configureLocked(uint32_t rateHz, uint32_t *rngOut, uint32_t *actualRateOut)
{
	if (fRunning) {
		stopOutputLocked();
	}
	// Forget the old configuration first: a failure below must not leave
	// startOutput() able to run an old range against a changed clock.
	fRng = 0;
	fClockHz = 0;

	if (fSim) {
		uint32_t simClock = pwmTargetClockHz(rateHz);
		uint32_t simRng = pwmRangeFor(simClock, rateHz);
		writeReg(0x10, simRng);
		writeReg(0x20, simRng);
		fClockHz = simClock;
		fRng = simRng;
		if (rngOut) {
			*rngOut = simRng;
		}
		if (actualRateOut) {
			*actualRateOut = pwmActualRateHz(simClock, simRng);
		}
		return kIOReturnSuccess;
	}

	// Clock: program the clock manager directly (programClock()). The mailbox
	// "set clock rate" for clock id 10 was tried first and has no effect on
	// the PWM clock here (acknowledged, clock unchanged; Minoca's UEFI PWM
	// driver notes the same).
	uint32_t want = pwmTargetClockHz(rateHz), got = 0;
	if (!programClock()) {
		return kIOReturnIOError;
	}
	got = want;
	uint32_t rng = pwmRangeFor(got, rateHz);
	if (rng == 0) {
		IOLog("BCM2837PWM: clock %u Hz (asked %u) gives no usable range for %u Hz\n", got, want, rateHz);
		return kIOReturnUnsupported;
	}
	if (!muxPins()) {
		return kIOReturnNoResources;
	}

	// Both channels off, FIFO cleared, errors cleared, then ranges.
	writeReg(0x00, 0);
	writeReg(0x04, kStaErrors);
	writeReg(0x00, kCtlClrf1);
	writeReg(0x10, rng);
	writeReg(0x20, rng);
	writeReg(0x08, 0);              // DMA request off until startOutput()

	fClockHz = got;
	fRng = rng;
	if (rngOut) {
		*rngOut = rng;
	}
	if (actualRateOut) {
		*actualRateOut = pwmActualRateHz(got, rng);
	}
	IOLog("BCM2837PWM: configured: clock asked %u got %u Hz, range %u, frame rate %u Hz\n",
	    want, got, rng, pwmActualRateHz(got, rng));
	return kIOReturnSuccess;
}

IOReturn
BCM2837PWM::setRange(uint32_t rng)
{
	if (rng < kPWMMinRange || rng > kPWMMaxRange) {
		return kIOReturnBadArgument;
	}
	IOLockLock(fLock);
	IOReturn r = kIOReturnNotReady;
	if (fRng != 0) {
		writeReg(0x10, rng);
		writeReg(0x20, rng);
		fRng = rng;
		r = kIOReturnSuccess;
	}
	IOLockUnlock(fLock);
	return r;
}

IOReturn
BCM2837PWM::startOutput()
{
	IOLockLock(fLock);
	IOReturn r = startOutputLocked();
	IOLockUnlock(fLock);
	return r;
}

// Order contract: the caller primes the DMA ring (so the first words are
// ready the moment DREQ asserts) before this; this clears stale errors and
// the FIFO, then enables DMAC, then the channels. RPTL repeats the last word
// on an underrun instead of dropping to the idle (low) level.
IOReturn
BCM2837PWM::startOutputLocked()
{
	if (fRng == 0) {
		return kIOReturnNotReady;
	}
	writeReg(0x04, kStaErrors);
	writeReg(0x00, kCtlClrf1);
	uint32_t ctl = kCtlPwen1 | kCtlUsef1 | kCtlRptl1 | kCtlPwen2 | kCtlUsef2 | kCtlRptl2;
	uint32_t ms = 0;
	if (PE_parse_boot_argn("pwm-ms", &ms, sizeof(ms)) && ms) {
		ctl |= kCtlMsen1 | kCtlMsen2;       // mark-space, to compare by ear
	}
	writeReg(0x08, kDmacEnab | kDmacPanic | kDmacDreq);
	writeReg(0x00, ctl);
	fRunning = true;
	return kIOReturnSuccess;
}

void
BCM2837PWM::stopOutput()
{
	IOLockLock(fLock);
	stopOutputLocked();
	IOLockUnlock(fLock);
}

// The caller stops its DMA channel first (a channel still running would stall
// mid-block on the cleared request, and could refill the FIFO after CLRF).
// In FIFO mode DAT is unused and the idle level with the channels off is low,
// so stopping and starting step the line between low and the signal: the
// audio engine ramps the first/last words to hide that (DARW-315).
void
BCM2837PWM::stopOutputLocked()
{
	writeReg(0x08, 0);
	writeReg(0x00, kCtlClrf1);              // channels off, FIFO flushed
	fRunning = false;
}

// Boot-arg pwm-test=1 (real hardware): program the block for 48 kHz, log the
// registers before and after, then leave it idle. Proves the clock request,
// pin mux and register programming without any DMA or audio.
void
BCM2837PWM::selfTest()
{
	uint32_t rng = 0, rate = 0;
	IOReturn r = configure(48000, &rng, &rate);
	IOLog("BCM2837PWM: self-test configure: %#x range %u rate %u\n", r, rng, rate);
	dumpRegs("after configure");
	if (r == kIOReturnSuccess) {
		bool ok = readReg(0x10) == rng && readReg(0x20) == rng;
		IOLog("BCM2837PWM: self-test readback: %s\n", ok ? "PASS" : "FAIL");
	}
}
