#include "BCM2837LocalIntc.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOCPU.h>
#include <string.h>

extern "C" {
#include <machine/machine_routines.h>   // ml_get_cpu_count()
}

#define super IOInterruptController

OSDefineMetaClassAndStructors(BCM2837LocalIntc, IOInterruptController);

IOService *
BCM2837LocalIntc::probe(IOService *provider, SInt32 *score)
{
	IOService *result = super::probe(provider, score);
	if (result == NULL) {
		return NULL;
	}
	const char *name = provider->getName();
	if (name == NULL || strcmp(name, "local-intc") != 0) {
		return NULL;
	}
	return result;
}

bool
BCM2837LocalIntc::init(OSDictionary *propTable)
{
	if (!super::init(propTable)) {
		return false;
	}

	fRegMap = NULL;
	fRegBase = NULL;
	fLoggedUnexpectedPending = false;
	fLastUnexpectedPending = 0;
	fNumCores = 1;

	vectors = (IOInterruptVector *)IOMalloc(kNumVectors * sizeof(IOInterruptVector));
	if (vectors == NULL) {
		return false;
	}
	bzero(vectors, kNumVectors * sizeof(IOInterruptVector));

	for (int i = 0; i < kNumVectors; i++) {
		vectors[i].interruptLock = IOLockAlloc();
		if (vectors[i].interruptLock == NULL) {
			for (int j = 0; j < i; j++) {
				IOLockFree(vectors[j].interruptLock);
			}
			IOFree(vectors, kNumVectors * sizeof(IOInterruptVector));
			vectors = NULL;
			return false;
		}
	}

	controllerLock = IOSimpleLockAlloc();
	if (controllerLock == NULL) {
		for (int i = 0; i < kNumVectors; i++) {
			IOLockFree(vectors[i].interruptLock);
		}
		IOFree(vectors, kNumVectors * sizeof(IOInterruptVector));
		vectors = NULL;
		return false;
	}

	return true;
}

BCM2837LocalIntc *BCM2837LocalIntc::sInstance = NULL;
void (*BCM2837LocalIntc::sIPIHandler)(void) = NULL;

void
BCM2837LocalIntc::setIPIHandler(void (*handler)(void))
{
	sIPIHandler = handler;
	OSMemoryBarrier();
}

/*
 * Raise core 0's mailbox 0. The IRQ it produces is taken like any other,
 * when the CPU next has IRQs unmasked. That is what makes it an IPI rather
 * than a function call. On arm64 the self-signals that matter are xcalls
 * (cpu_xcall()/cpu_immediate_xcall() to the current CPU) and
 * SIGPnop/SIGPast, and cpu_signal_handler() processes them from interrupt
 * context, never inside the code that posted them.
 * cpu_signal_internal() has already issued DSB ISH before calling
 * PE_cpu_signal() (osfmk/arm/cpu_common.c:477), so no barrier is needed here.
 */
bool
BCM2837LocalIntc::sendIPI(UInt32 core)
{
	BCM2837LocalIntc *me = sInstance;
	if (me == NULL || core >= me->fNumCores) {
		return false;
	}
	// DAR-455: the target core's mailbox 0 (core 0 only, before).
	me->fRegBase[kRegMailbox0Set0 + core * kCoreStrideMbox] = 1;
	return true;
}

bool
BCM2837LocalIntc::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}

	fRegMap = provider->mapDeviceMemoryWithIndex(0);
	if (fRegMap == NULL) {
		IOLog("BCM2837LocalIntc: failed to map \"local-intc\" registers\n");
		return false;
	}
	// Backlog item 26: confirm the mapping is actually big enough to cover
	// the highest offset this driver touches (kRegMailbox0Clr0 = 0xC0 since
	// DAR-375)
	// before trusting it - "local-intc"'s own reg property declares 0x100
	// bytes, so this should always pass; catches a misconfigured DT rather
	// than a runtime condition.
	if (fRegMap->getLength() < ((kRegMailbox0Clr0 + (kMaxCores - 1) * kCoreStrideMbox) *
	    sizeof(uint32_t)) + sizeof(uint32_t)) {
		IOLog("BCM2837LocalIntc: \"local-intc\" register mapping too small (%llu bytes)\n",
		    (unsigned long long)fRegMap->getLength());
		fRegMap->release();
		fRegMap = NULL;
		return false;
	}
	fRegBase = (volatile uint32_t *)fRegMap->getVirtualAddress();

	// Force GPU IRQ routing to core 0 (bits[2:0]=0), matching Linux's own
	// irq-bcm2836.c init - real hardware/QEMU default this way already,
	// but don't rely on firmware/QEMU state being correct.
	fRegBase[kRegGPURouting] = 0;

	// Backlog item 3, investigated (not just assumed) 2026-08-13: XNU's
	// arm64 scheduler tick is taken via FIQ (osfmk/arm64/sleh.c's
	// sleh_fiq(), dispatching on ml_get_timer_pending() - never through
	// this driver's own IRQ path at all), which requires this exact
	// register - real QA7 hardware resets REG_TIMERCONTROL to 0 (masked),
	// same as third_party/qemu-zhuowei's own bcm2836_control_reset().
	// A live QEMU boot read this register back as already 0xF0 (FIQ
	// enabled for all 4 generic timer comparators) by the time a shell
	// was reached, WITHOUT any write from this repo's own source -
	// confirmed by grepping the built kernel tree for the register/offset
	// and finding nothing, and by checking FlorentRevest/u-boot's
	// cmd/bootxnu.c (real hardware's actual boot chain) for the same,
	// also finding nothing. That means QEMU's own loader is silently
	// providing this as a courtesy real firmware has no reason to
	// replicate - the exact "don't rely on firmware/QEMU state being
	// correct" gap the GPU-routing write above already guards against,
	// just not yet applied here. Write it explicitly: bits 4-7 = FIQ
	// enable for CNTPSIRQ/CNTPNSIRQ/CNTHPIRQ/CNTVIRQ (bits 0-3, IRQ
	// enable, deliberately left clear - this driver's own IRQ dispatch
	// path has no handler for any of these four sources, only the GPU
	// passthrough bit, so IRQ-routing a timer comparator here would hit
	// the exact "unhandled pending bit, no way to clear it" spin
	// backlog item 2 just added a diagnostic latch for).
	// DAR-455: every core this controller serves. osfmk/arm64/start.s's
	// BCM2837 block already writes the same value for each core as it
	// starts; this is the same explicit, don't-trust-firmware write.
	fNumCores = ml_get_cpu_count();
	if (fNumCores < 1 || fNumCores > kMaxCores) {
		IOLog("BCM2837LocalIntc: topology reports %u CPUs\n", (unsigned)fNumCores);
		return false;
	}
	for (UInt32 core = 0; core < fNumCores; core++) {
		fRegBase[kRegTimerControl0 + core * kCoreStrideCtl] = 0xF0;
	}

	// DAR-375: each core's mailbox 0 is its IPI. Clear anything stale now;
	// the IRQ enable (bit 0; FIQ bits 4-7 left clear) is set below, once a
	// handler is registered for every core's IRQ line. Covered by the
	// mapping-size check above.
	for (UInt32 core = 0; core < fNumCores; core++) {
		fRegBase[kRegMailbox0Clr0 + core * kCoreStrideMbox] = 0xFFFFFFFF;
	}

	const OSSymbol *controllerName = IODTInterruptControllerName(provider);
	if (controllerName == NULL) {
		IOLog("BCM2837LocalIntc: IODTInterruptControllerName failed (no AAPL,phandle on \"local-intc\"?)\n");
		return false;
	}

	// P0 backlog item 0 fix (2026-08-13, adversarial review): register
	// against IOPlatformInterruptController and confirm real ARM64 IRQ
	// delivery is actually wired up BEFORE publishing ourselves as a
	// usable interrupt controller below - not after, as this used to do.
	// Publishing first let a downstream client (e.g. BCM2837SDHCI, via the
	// normal blocking lookUpInterruptController()/registerInterrupt()
	// chain) start arming real hardware interrupts before
	// BCM2837CPU::initCPU() had installed the real handler via
	// ml_install_interrupt_handler() - if a genuine ARM64 IRQ arrived in
	// that window, sleh_irq() called through a still-NULL
	// cdp->interrupt_handler and crashed (confirmed via disassembly: the
	// panic's lr lands exactly at sleh_irq's own indirect-call return
	// address). IOPlatformExpert::lookUpInterruptController() is
	// unconditionally blocking - it sleeps until the name is registered,
	// never returns NULL - so this reordering turns that existing lookup
	// chain into a real barrier instead of a race: no new lock, no new
	// deadlock (BCM2837CPU depends on nothing in this chain).
	IOInterruptController *cpuIC =
	    getPlatform()->lookUpInterruptController((OSSymbol *)gPlatformInterruptControllerName);
	if (cpuIC == NULL) {
		// A real failure now, not "keep going anyway": without this,
		// nothing may safely publish us as a usable interrupt controller -
		// see the reordering comment above.
		IOLog("BCM2837LocalIntc: no IOPlatformInterruptController - real IRQ delivery cannot be made safe\n");
		controllerName->release();
		return false;
	}
	IOInterruptHandler cpuICHandler = OSMemberFunctionCast(
		IOInterruptHandler, this, &BCM2837LocalIntc::handleInterrupt);
	// DAR-455: one source per CPU (the source number is the CPU number,
	// iokit/Kernel/IOCPU.cpp:640, :744-760). The first call blocks until
	// every CPU has enabled itself (IOCPU.cpp:690-700).
	for (UInt32 cpu = 0; cpu < fNumCores; cpu++) {
		IOReturn err = cpuIC->registerInterrupt(provider, (int)cpu, this, cpuICHandler, NULL);
		if (err != kIOReturnSuccess) {
			IOLog("BCM2837LocalIntc: registerInterrupt(cpu%u) against IOPlatformInterruptController failed: %#x\n",
			    (unsigned)cpu, err);
			controllerName->release();
			return false;
		}
		cpuIC->enableInterrupt(provider, (int)cpu);
	}

	// Every core's IRQ now has a handler: enable each core's mailbox 0.
	for (UInt32 core = 0; core < fNumCores; core++) {
		fRegBase[kRegMailboxControl0 + core * kCoreStrideCtl] = 0x1;
	}

	getPlatform()->registerInterruptController((OSSymbol *)controllerName, this);

	IOLog("BCM2837LocalIntc: started, base=%p, registered as \"%s\", %u core(s)\n",
	    (void *)fRegBase, controllerName->getCStringNoCopy(), (unsigned)fNumCores);
	controllerName->release();

	// Live now: take IPIs, and deliver any signal posted before we were -
	// including each CPU's own SIGPdisabled-clearing self-IPI
	// (BCM2837CPU::initCPU()), which was a no-op until now.
	OSMemoryBarrier();
	sInstance = this;
	for (UInt32 core = 0; core < fNumCores; core++) {
		sendIPI(core);
	}

	return true;
}

void
BCM2837LocalIntc::stop(IOService *provider)
{
	// No more IPIs through a mapping that is about to go away.
	if (sInstance == this) {
		for (UInt32 core = 0; core < fNumCores; core++) {
			fRegBase[kRegMailboxControl0 + core * kCoreStrideCtl] = 0;
		}
		sInstance = NULL;
		OSMemoryBarrier();
	}
	if (fRegMap != NULL) {
		fRegMap->release();
		fRegMap = NULL;
	}
	super::stop(provider);
}

void
BCM2837LocalIntc::free(void)
{
	// Backlog item 27 - see BCM2837InterruptController::free()'s matching
	// comment for the full reasoning (same start()-failure-path leak).
	if (fRegMap != NULL) {
		fRegMap->release();
		fRegMap = NULL;
	}

	if (vectors != NULL) {
		for (int i = 0; i < kNumVectors; i++) {
			if (vectors[i].interruptLock != NULL) {
				IOLockFree(vectors[i].interruptLock);
			}
		}
		IOFree(vectors, kNumVectors * sizeof(IOInterruptVector));
		vectors = NULL;
	}
	if (controllerLock != NULL) {
		IOSimpleLockFree(controllerLock);
		controllerLock = NULL;
	}
	super::free();
}

IOInterruptAction
BCM2837LocalIntc::getInterruptHandlerAddress(void)
{
	return OSMemberFunctionCast(IOInterruptAction, this,
	           &BCM2837LocalIntc::handleInterrupt);
}

IOReturn
BCM2837LocalIntc::handleInterrupt(void * /*refCon*/, IOService * /*nub*/, int /*source*/)
{
	// DAR-455: this core's own pending register and mailbox. IRQs are
	// per core (QA7 core n IRQ source at 0x60 + 4n), and this handler runs
	// on whichever core took the IRQ. MPIDR_EL1 Aff0 is the core.
	UInt32 core = (UInt32)(__builtin_arm_rsr64("MPIDR_EL1") & 0x3);
	uint32_t pending0 = fRegBase[kRegIRQPending0 + core * kCoreStrideCtl];

	if (pending0 & (1U << kVectorMailbox0)) {
		// DAR-375 IPI. Acknowledge before running the handler, so a signal
		// posted while it runs raises a fresh one rather than being lost
		// (the same ordering sleh.c keeps for Apple's fast IPIs,
		// osfmk/arm64/sleh.c:1833-1844).
		// Write-1-to-clear: one bit is all this mailbox is ever used for.
		fRegBase[kRegMailbox0Clr0 + core * kCoreStrideMbox] = 0xFFFFFFFF;
		__asm__ volatile ("dsb sy" ::: "memory");
		void (*handler)(void) = sIPIHandler;
		if (handler != NULL) {
			handler();
		}
	}

	if (pending0 & (1U << kVectorGPUIRQ)) {
		IOInterruptVector *vector = &vectors[kVectorGPUIRQ];
		vector->interruptActive = 1;
		OSMemoryBarrier();
		if (!vector->interruptDisabledSoft) {
			if (vector->interruptRegistered) {
				vector->handler(vector->target, vector->refCon, vector->nub, vector->source);
			}
		} else {
			// Same lazy hard-disable pattern as
			// BCM2837InterruptController::dispatchBank() (Apple's own
			// GenericInterruptController reference), found missing via
			// adversarial review 2026-08-13. disableVectorHard() is a
			// documented no-op at this L1 level (see that function) -
			// this call is still correct to make (matches the interface
			// contract, harmless), but real recovery from a stuck GPU IRQ
			// line still requires masking at the "intc" level below us,
			// not here - not fixed further, just no longer silently
			// missing the attempt.
			vector->interruptDisabledHard = 1;
			disableVectorHard(kVectorGPUIRQ, vector);
		}
		vector->interruptActive = 0;
	}

	// Backlog item 2: this used to silently ignore any pending0 bit other
	// than kVectorGPUIRQ. Mailbox 0 is handled above since DAR-375; the
	// other mailboxes and the local-timer IRQ sources are never enabled by
	// this driver (bcm2836_control_reset() zeroes their control registers
	// in QEMU), but real firmware may leave one enabled. A bit here that this
	// driver doesn't service never gets cleared, so the CPU IRQ line
	// stays asserted and sleh_irq() re-enters immediately - a silent
	// spin with zero diagnostic trace, previously. Latch (not IOLog -
	// this runs at real interrupt context, same reasoning as
	// BCM2837SDHCI::interruptOccurred() avoiding IOLog there) the first
	// occurrence so it's at least visible via a debugger/coredump instead
	// of leaving no trace at all - implementing the actual mailbox/local-
	// timer source handling is out of scope here (see docs/backlog.md
	// item 3 for the related, separate LOCAL_TIMER_INT_CONTROL0
	// question).
	uint32_t unexpected = pending0 & ~((1U << kVectorGPUIRQ) | (1U << kVectorMailbox0));
	if (unexpected != 0 && !fLoggedUnexpectedPending) {
		fLoggedUnexpectedPending = true;
		fLastUnexpectedPending = unexpected;
	}

	return kIOReturnSuccess;
}

bool
BCM2837LocalIntc::vectorCanBeShared(IOInterruptVectorNumber /*vectorNumber*/, IOInterruptVector * /*vector*/)
{
	return false;
}

void
BCM2837LocalIntc::initVector(IOInterruptVectorNumber /*vectorNumber*/, IOInterruptVector * /*vector*/)
{
}

int
BCM2837LocalIntc::getVectorType(IOInterruptVectorNumber /*vectorNumber*/, IOInterruptVector * /*vector*/)
{
	return kIOInterruptTypeLevel;
}

void
BCM2837LocalIntc::disableVectorHard(IOInterruptVectorNumber /*vectorNumber*/, IOInterruptVector * /*vector*/)
{
	// The GPU IRQ passthrough bit (vector 8, the only vector this driver
	// actually wires up - see header) has no independent per-source mask
	// at the L1 level; it's live whenever LOCAL_GPU_ROUTING points at
	// this core. Nothing to do here.
}

void
BCM2837LocalIntc::enableVector(IOInterruptVectorNumber /*vectorNumber*/, IOInterruptVector * /*vector*/)
{
	// See disableVectorHard - no per-source enable at this level either.
}
