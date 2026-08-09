/*
 * Driver for the BCM2835-style "armctrl-ic" global peripheral interrupt
 * controller (Linux: brcm,bcm2836-armctrl-ic; DT node "intc" in
 * devicetrees/rpi3.dts). Multiplexes IRQs from all VC-bus peripherals
 * (gpio, aux, sdhci, ...) onto a single line that feeds into the
 * BCM2836/7 L1 controller ("local-intc") on IRQ 8.
 *
 * Register layout (BCM2835 ARM Peripherals datasheet, ch.7), offsets from
 * the mapped base (0x7e00b200 VC / 0x3f00b200 phys, matches the "intc"
 * node's reg property):
 *   0x00 IRQ_basic_pending   (RO)
 *   0x04 IRQ_pending_1       (RO)   bank 1, GPU IRQs 0-31
 *   0x08 IRQ_pending_2       (RO)   bank 2, GPU IRQs 32-63
 *   0x0C FIQ_control
 *   0x10 Enable_IRQs_1       (W1S)  bank 1 enable
 *   0x14 Enable_IRQs_2       (W1S)  bank 2 enable
 *   0x18 Enable_Basic_IRQs   (W1S)
 *   0x1C Disable_IRQs_1      (W1C)  bank 1 disable
 *   0x20 Disable_IRQs_2      (W1C)  bank 2 disable
 *   0x24 Disable_Basic_IRQs  (W1C)
 *
 * Vector numbering used by this driver: 0-31 = bank 1, 32-63 = bank 2,
 * 64-71 = basic. This is a software convention on our side - it only
 * matches reality if IODeviceTreeSupport's ADT interrupt-cell parsing
 * hands registerInterrupt() the same (bank*32 + bit) integer we derived
 * by hand for the "sdhci" node's `interrupts = <2 30>` (vector 62).
 * UNVERIFIED - confirm against actual IODeviceTreeSupport.cpp behavior
 * on a compiled ADT before trusting this.
 *
 * Also unverified/unimplemented: how a raw ARM64 IRQ exception actually
 * reaches this class's handleInterrupt(). That's pexpert-level wiring
 * (the primary interrupt handler installed for the exception vector),
 * not something IOKit-level code here can set up on its own - it has to
 * be confirmed against this fork's pexpert/arm64 sources.
 */

#ifndef _BCM2837_INTERRUPT_CONTROLLER_H
#define _BCM2837_INTERRUPT_CONTROLLER_H

#include <IOKit/IOInterruptController.h>
#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>


class BCM2837InterruptController : public IOInterruptController
{
	OSDeclareDefaultStructors(BCM2837InterruptController);

private:
	IOMemoryMap *fRegMap;
	volatile uint32_t *fRegBase;

	enum {
		kNumVectors = 72,

		kRegIRQBasicPending  = 0x00 / sizeof(uint32_t),
		kRegIRQPending1      = 0x04 / sizeof(uint32_t),
		kRegIRQPending2      = 0x08 / sizeof(uint32_t),
		kRegFIQControl       = 0x0C / sizeof(uint32_t),
		kRegEnableIRQs1      = 0x10 / sizeof(uint32_t),
		kRegEnableIRQs2      = 0x14 / sizeof(uint32_t),
		kRegEnableBasicIRQs  = 0x18 / sizeof(uint32_t),
		kRegDisableIRQs1     = 0x1C / sizeof(uint32_t),
		kRegDisableIRQs2     = 0x20 / sizeof(uint32_t),
		kRegDisableBasicIRQs = 0x24 / sizeof(uint32_t),

		kVectorBank1Base = 0,
		kVectorBank2Base = 32,
		kVectorBasicBase = 64,
	};

	void dispatchBank(uint32_t pending, IOInterruptVectorNumber base);

public:
	virtual bool init(OSDictionary *propTable) APPLE_KEXT_OVERRIDE;

	// Defensive re-check that provider is actually named "intc" - see
	// iokit/Drivers/BCM2837/BCM2837ArmIO.h (xnu-7195 tree) for why this is
	// needed (IOService::matchInternal()'s generic IONameMatch handling
	// was empirically observed matching this personality against sibling
	// nodes too, not just "intc").
	virtual IOService *probe(IOService *provider, SInt32 *score) APPLE_KEXT_OVERRIDE;

	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free(void) APPLE_KEXT_OVERRIDE;

	// Cascade entry point: called (indirectly, via getInterruptHandlerAddress())
	// by our provider (local-intc) when its "GPU IRQ" line (IRQ 8) fires.
	virtual IOInterruptAction getInterruptHandlerAddress(void) APPLE_KEXT_OVERRIDE;
	virtual IOReturn handleInterrupt(void *refCon, IOService *nub, int source) APPLE_KEXT_OVERRIDE;

	// Simplified per-vector hooks (see IOInterruptController.h) - these are
	// the only overrides the base class actually requires for a normal
	// hardware controller.
	virtual bool vectorCanBeShared(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	virtual void initVector(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	virtual int  getVectorType(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	virtual void disableVectorHard(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
	virtual void enableVector(IOInterruptVectorNumber vectorNumber, IOInterruptVector *vector) APPLE_KEXT_OVERRIDE;
};

#endif /* _BCM2837_INTERRUPT_CONTROLLER_H */
