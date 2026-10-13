/*
 * BCM2837GPIO - pin function select and pull-up/down for the BCM2835-family
 * GPIO block (ARM physical 0x3f200000, the "gpio" node under "arm-io" in
 * devicetrees/rpi3.dts). Header-only: there is no IOKit driver for "gpio";
 * the few drivers that route pins (SD card, SDIO WiFi) map the block
 * themselves, as BCM2837SDHCI::pinmuxSDSlot() always did.
 *
 * Register layout and the pull sequence are from the BCM2835 ARM
 * Peripherals manual, section 6.1 (GPFSELn at 0x00-0x14, 3 bits per pin;
 * GPPUD at 0x94; GPPUDCLK0/1 at 0x98/0x9C), the same sequence Linux's
 * pinctrl-bcm2835.c bcm2835_pull_config_set() uses on BCM2835/6/7:
 * write GPPUD, wait 150 cycles, clock the pins in GPPUDCLKn, wait 150
 * cycles, clear GPPUD and GPPUDCLKn. 150 cycles of the 250 MHz VPU clock
 * is 0.6 us; Linux waits 1 us (udelay(1)), as does this.
 *
 * One spinlock covers every GPFSEL read-modify-write and the GPPUD
 * sequence: pins in the same GPFSEL word (sdhost's GPIO48/49 and PWM's
 * GPIO40/41 share GPFSEL4) are otherwise a lost update between two
 * drivers starting on different CPUs. BCM2837SDHCI::pinmuxSDSlot() still
 * writes GPFSEL3-5 directly, without it; it is the default-boot path and
 * runs before anything else here touches those words.
 */
#ifndef _BCM2837GPIO_H
#define _BCM2837GPIO_H

#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLocks.h>
#include <libkern/OSAtomic.h>

class BCM2837GPIO {
public:
	enum {
		kFSelInput  = 0,
		kFSelOutput = 1,
		kFSelAlt0   = 4,
		kFSelAlt1   = 5,
		kFSelAlt2   = 6,
		kFSelAlt3   = 7,
		kFSelAlt4   = 3,
		kFSelAlt5   = 2,
	};
	enum {
		kPullOff  = 0,
		kPullDown = 1,
		kPullUp   = 2,
	};

	// One-pin helpers. `count` consecutive pins starting at `first` get the
	// same setting. Return false only if the register block can't be mapped.
	static bool setFunction(unsigned first, unsigned count, unsigned fsel)
	{
		volatile uint32_t *regs = mapRegs();
		if (regs == NULL) {
			return false;
		}
		IOSimpleLock *lock = regLock();
		if (lock == NULL) {
			return false;
		}
		IOInterruptState st = IOSimpleLockLockDisableInterrupt(lock);
		for (unsigned pin = first; pin < first + count; pin++) {
			unsigned word = pin / 10, shift = (pin % 10) * 3;
			uint32_t v = regs[word];
			v = (v & ~(0x7U << shift)) | ((fsel & 0x7U) << shift);
			regs[word] = v;
		}
		IOSimpleLockUnlockEnableInterrupt(lock, st);
		return true;
	}

	static bool setPull(unsigned first, unsigned count, unsigned pull)
	{
		volatile uint32_t *regs = mapRegs();
		if (regs == NULL) {
			return false;
		}
		uint32_t clk0 = 0, clk1 = 0;
		for (unsigned pin = first; pin < first + count; pin++) {
			if (pin < 32) {
				clk0 |= 1U << pin;
			} else {
				clk1 |= 1U << (pin - 32);
			}
		}
		IOSimpleLock *lock = regLock();
		if (lock == NULL) {
			return false;
		}
		IOInterruptState st = IOSimpleLockLockDisableInterrupt(lock);
		regs[kGPPUD / 4] = pull & 0x3U;
		IODelay(1);
		regs[kGPPUDCLK0 / 4] = clk0;
		regs[kGPPUDCLK1 / 4] = clk1;
		IODelay(1);
		regs[kGPPUD / 4] = 0;
		regs[kGPPUDCLK0 / 4] = 0;
		regs[kGPPUDCLK1 / 4] = 0;
		IOSimpleLockUnlockEnableInterrupt(lock, st);
		return true;
	}

	static bool getFunction(unsigned pin, unsigned *outFsel)
	{
		volatile uint32_t *regs = mapRegs();
		if (regs == NULL) {
			return false;
		}
		*outFsel = (regs[pin / 10] >> ((pin % 10) * 3)) & 0x7U;
		return true;
	}

private:
	enum {
		kGPPUD     = 0x94,
		kGPPUDCLK0 = 0x98,
		kGPPUDCLK1 = 0x9C,
		kMapLen    = 0xA0,
	};

	// Created on first use; the loser of a creation race frees its copy.
	static IOSimpleLock *regLock(void)
	{
		static IOSimpleLock *volatile sLock = NULL;
		if (sLock != NULL) {
			return sLock;
		}
		IOSimpleLock *l = IOSimpleLockAlloc();
		if (l == NULL) {
			return NULL;
		}
		if (!OSCompareAndSwapPtr(NULL, l, (void *volatile *)&sLock)) {
			IOSimpleLockFree(l);
		}
		return sLock;
	}

	// arm-io's "ranges" maps child 0 to parent 0x3f000000, and "gpio" is at
	// child 0x200000 (devicetrees/rpi3.dts).
	//
	// Mapped once and never released. Releasing a provider-less
	// IODeviceMemory::withRange()+map() this early in boot crashed (backtrace
	// through IOMemoryMap::free(), see BCM2837SDHCI::pinmuxSDSlot()'s comment),
	// and that function leaks its own mapping for the same reason. The race on
	// first use is benign: two callers would each create a mapping and one
	// leaks; both point at the same registers.
	static volatile uint32_t *mapRegs(void)
	{
		static volatile uint32_t *sRegs = NULL;
		if (sRegs != NULL) {
			return sRegs;
		}
		const IOPhysicalAddress kGPIOBase = 0x3f200000;
		IODeviceMemory *mem = IODeviceMemory::withRange(kGPIOBase, kMapLen);
		if (mem == NULL) {
			return NULL;
		}
		IOMemoryMap *map = mem->map();
		mem->release();
		if (map == NULL) {
			return NULL;
		}
		sRegs = (volatile uint32_t *)map->getVirtualAddress();
		return sRegs;
	}
};

#endif /* _BCM2837GPIO_H */
