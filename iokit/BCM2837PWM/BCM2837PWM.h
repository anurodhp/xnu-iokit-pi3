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
 *   - PWM clock: the VideoCore firmware owns the clock manager on this
 *     board, so the rate is requested through the mailbox ("set clock
 *     rate", clock id 10, BCM2837Mailbox::kClockIdPWM) rather than by
 *     writing CM_PWMCTL/CM_PWMDIV behind its back.
 *
 * Not done here: the audio engine (DARW-315), DMA wiring (uses BCM2837DMA).
 */
#ifndef BCM2837PWM_H
#define BCM2837PWM_H

#include <IOKit/IOService.h>

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
	// programs both channels (FIFO + DMA mode, output idling at mid-scale,
	// not yet enabled). *rngOut is the range (one frame = rng clocks) and
	// *actualRateOut the resulting frame rate.
	IOReturn configure(uint32_t rateHz, uint32_t *rngOut, uint32_t *actualRateOut);
	// Enable / disable both channels and the DMA request.
	IOReturn startOutput();
	void     stopOutput();
	bool     isConfigured() const { return fRng != 0; }
	uint32_t getRange() const { return fRng; }

	uint32_t readReg(uint32_t offset);

private:
	void     writeReg(uint32_t offset, uint32_t value);
	bool     muxPins();
	void     dumpRegs(const char *tag);
	void     selfTest();

	IOMemoryMap       *fRegMap;
	volatile uint32_t *fRegs;
	uint32_t           fClockHz;
	uint32_t           fRng;
	bool               fRunning;
};

#endif
