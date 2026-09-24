/*
 * Minimal concrete IOCPU for BCM2837's single effectively-running core
 * (secondary cores are parked below IOKit - see project memory). This
 * closes a real gap found investigating why BCM2837SDHCI never sees a
 * real ARM64 IRQ exception reach BCM2837LocalIntc::handleInterrupt(),
 * despite the hardware genuinely raising the interrupt (confirmed via
 * direct INT_STATUS register readback):
 *
 * osfmk/arm64/sleh.c's sleh_irq() (the actual ARM64 IRQ exception
 * handler XNU's vector table jumps to) unconditionally calls
 * `cdp->interrupt_handler(cdp->interrupt_target, cdp->interrupt_refCon,
 * cdp->interrupt_nub, cdp->interrupt_source)` - a per-cpu_data_t function
 * pointer that starts out unset (BootCpuData is a zero-initialized
 * static). The only place in this xnu-7195 tree that ever calls
 * ml_install_interrupt_handler() (osfmk/arm64/machine_routines.c) to
 * populate it is IOCPUInterruptController::enableCPUInterrupt()
 * (iokit/Kernel/IOCPU.cpp) - real, unmodified Apple code, but only
 * reachable if a concrete IOCPU subclass exists, matches a "cpuN"
 * device-tree node, and calls it from initCPU(). This project never had
 * one, so no real ARM64 IRQ has ever reached XNU's own dispatch code in
 * this project's history - BCM2837SDHCI's interrupt-polling workaround
 * was masking a much lower-level gap than originally suspected.
 *
 * IOCPUInterruptController itself is a single, undifferentiated "this
 * CPU's incoming IRQ line fired, go figure out what" dispatcher - real
 * boards' L1/root interrupt controller driver (BCM2837LocalIntc here)
 * is the thing that registers against it (source 0, the only source we
 * create - one core) and re-dispatches into the normal per-DT-node
 * IOInterruptController cascade this project already has working
 * (BCM2837LocalIntc -> BCM2837InterruptController -> BCM2837SDHCI). See
 * the registerInterrupt() call added to BCM2837LocalIntc::start().
 *
 * IOCPUInterruptController::enableCPUInterrupt() also calls
 * IOService::cpusRunning() once all CPUs (just one, here) are up - the
 * real mechanism behind a fix already applied as a stand-in directly in
 * IOStartIOKit.cpp::StartIOKitMatching() (see that file's own comment)
 * for an unrelated bug (unbounded _IOConfigThread creation). That
 * stand-in is left in place as a safety net - cpusRunning() is
 * idempotent - since ordering between "cpu0" and other nodes' matching
 * isn't strictly guaranteed to run this (correct, real) path first.
 */

#ifndef _BCM2837_CPU_H
#define _BCM2837_CPU_H

#include <IOKit/IOCPU.h>

class BCM2837CPU : public IOCPU
{
	OSDeclareDefaultStructors(BCM2837CPU);

private:
	// Cached by getCPUName() - see that method for why (backlog item 17:
	// was allocating a fresh retained OSSymbol on every call and leaking
	// it).
	const OSSymbol *fCPUName;

public:
	virtual IOService       *probe(IOService *provider, SInt32 *score) APPLE_KEXT_OVERRIDE;
	virtual bool            start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void            initCPU(bool boot) APPLE_KEXT_OVERRIDE;
	virtual void            quiesceCPU(void) APPLE_KEXT_OVERRIDE;
	virtual kern_return_t   startCPU(vm_offset_t start_paddr, vm_offset_t arg_paddr) APPLE_KEXT_OVERRIDE;
	virtual void            haltCPU(void) APPLE_KEXT_OVERRIDE;
	virtual void            signalCPU(IOCPU *target) APPLE_KEXT_OVERRIDE;
	virtual const OSSymbol *getCPUName(void) APPLE_KEXT_OVERRIDE;
	virtual void            free(void) APPLE_KEXT_OVERRIDE;
};

#endif /* _BCM2837_CPU_H */
