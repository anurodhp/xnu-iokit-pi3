/*
 * Driver for the BCM2835/2837 PWM block, used as a two-channel audio sink
 * (the Pi 3's 3.5mm jack is PWM0/PWM1 on GPIO40/41). DARW-314.
 *
 * Facts and where they come from:
 *   - Block at bus 0x7e20c000, 0x28 bytes, one controller with two
 *     channels: third_party/u-boot/arch/arm/dts/bcm283x.dtsi:534
 *     (pwm@7e20c000, reg <0x7e20c000 0x28>).
 *   - Pi 3 wiring: GPIO40 = PWM0_OUT and GPIO41 = PWM1_OUT, ALT0
 *     (bcm283x.dtsi:297,309 pwm0_gpio40/pwm1_gpio41 = BCM2835_FSEL_ALT0;
 *     bcm2837-rpi-3-b.dts:103-104 pin names, :120-123 the pwm node using
 *     them).
 *   - Register map and bit positions (CTL 0x00, STA 0x04, DMAC 0x08, RNG1
 *     0x10, DAT1 0x14, FIF1 0x18, RNG2 0x20, DAT2 0x24; CTL PWENx/MODEx/RPTLx/
 *     SBITx/POLAx/USEFx/MSENx, CLRF1; DMAC ENAB/PANIC/DREQ; DMA peripheral
 *     map 5): the BCM2835 ARM Peripherals datasheet, chapter 9 and the DMA
 *     chapter's DREQ table. That document is NOT vendored in this tree and
 *     QEMU has no PWM model yet (DARW-318), so these are from the datasheet
 *     as remembered; the first run on a Pi (pwm-test=1 logs the registers)
 *     and the QEMU model must cross-check them.
 *   - PWM clock: programmed directly in the clock manager (CM_PWMCTL/
 *     CM_PWMDIV, plld_per / 2; programClock(), citations there). The
 *     VideoCore mailbox "set clock rate" for clock id 10 (u-boot mach/mbox.h:
 *     228-238) is acknowledged but has no effect on this clock. plld_per is
 *     nominally 500 MHz but measured at 5/8 of that on a real Pi 3, so the
 *     audio engine trims the range at start (DARW-317).
 *   - GPIO40/41 are muxed through BCM2837GPIO, whose lock also covers
 *     sdhost's GPIO48/49 in the same GPFSEL4 word. BCM2837SDHCI::
 *     pinmuxSDSlot() still writes GPFSEL4 directly; it runs at boot, the PWM
 *     mux only when audio is configured.
 *
 * Not done here: the audio engine (DARW-315), DMA wiring (uses BCM2837DMA).
 */
#ifndef BCM2837PWM_H
#define BCM2837PWM_H

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>

#define kBCM2837PWMBusBase  0x7e20c000ULL
#define kBCM2837PWMFifoBus  (kBCM2837PWMBusBase + 0x18)
#define kBCM2837PWMDmaPerMap 5      // DREQ line number for the DMA TI PERMAP field

class BCM2837PWM : public IOService
{
	OSDeclareDefaultStructors(BCM2837PWM)

public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;

	// Sets the PWM clock for `rateHz` stereo frames/s, muxes GPIO40/41, and
	// programs both channels (FIFO + DMA mode, not yet enabled). Output idles
	// low while disabled (DAT is unused in FIFO mode), so enabling/disabling
	// steps the line; the engine ramps its first/last samples. *rngOut is the range (one frame = rng clocks) and
	// *actualRateOut the resulting frame rate.
	IOReturn configure(uint32_t rateHz, uint32_t *rngOut, uint32_t *actualRateOut);
	// Enable / disable both channels and the DMA request. Order contract: prime
	// the DMA ring first, then startOutput(); stop the DMA channel first, then
	// stopOutput(). All public entry points are serialised by one lock.
	IOReturn startOutput();
	void     stopOutput();
	bool     isConfigured() const { return fRng != 0; }
	// Changes the range (frame period) of a configured block, even while running
	// (the new value applies from the next period). Used by the engine to trim the
	// frame rate after measuring it.
	IOReturn setRange(uint32_t rng);
	// pwm-sim=1: registers live in RAM and no clock, pin or hardware is touched
	// (QEMU has no PWM block). For exercising the audio path off-hardware.
	bool     isSimulated() const { return fSim; }
	uint32_t getRange() const { return fRng; }

	uint32_t readReg(uint32_t offset);

private:
	void     writeReg(uint32_t offset, uint32_t value);
	IOReturn configureLocked(uint32_t rateHz, uint32_t *rngOut, uint32_t *actualRateOut);
	IOReturn startOutputLocked();
	void     stopOutputLocked();
	bool     muxPins();
	bool     programClock();
	void     dumpRegs(const char *tag);
	void     selfTest();

	IOLock            *fLock;
	IOMemoryMap       *fRegMap;
	volatile uint32_t *fRegs;
	uint32_t           fClockHz;
	uint32_t           fRng;
	bool               fRunning;
	bool               fSim;
	uint32_t           fSimRegs[0x28 / 4];
};

#endif
