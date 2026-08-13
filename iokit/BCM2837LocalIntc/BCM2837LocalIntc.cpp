#include "BCM2837LocalIntc.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOCPU.h>
#include <string.h>

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
	// the highest offset this driver touches (kRegIRQPending0 = 0x60)
	// before trusting it - "local-intc"'s own reg property declares 0x100
	// bytes, so this should always pass; catches a misconfigured DT rather
	// than a runtime condition.
	if (fRegMap->getLength() < (kRegIRQPending0 * sizeof(uint32_t)) + sizeof(uint32_t)) {
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
	fRegBase[kRegTimerControl0] = 0xF0;

	const OSSymbol *controllerName = IODTInterruptControllerName(provider);
	if (controllerName == NULL) {
		IOLog("BCM2837LocalIntc: IODTInterruptControllerName failed (no AAPL,phandle on \"local-intc\"?)\n");
		return false;
	}
	getPlatform()->registerInterruptController((OSSymbol *)controllerName, this);

	IOLog("BCM2837LocalIntc: started, base=%p, registered as \"%s\"\n",
	    (void *)fRegBase, controllerName->getCStringNoCopy());
	controllerName->release();

	// No registerInterrupt() against our own provider via the normal
	// DT-interrupt-parent cascade mechanism - local-intc is the top of
	// this board's interrupt tree (see header), not a cascade consumer
	// of anything else. But it IS the thing that should be told about a
	// real ARM64 IRQ trap reaching this CPU at all: register directly
	// against the platform's IOCPUInterruptController (source 0, our
	// only CPU) so IOCPUInterruptController::handleInterrupt() (called
	// from sleh_irq() via ml_install_interrupt_handler(), wired up by
	// BCM2837CPU::initCPU()) actually calls back into our own
	// handleInterrupt() - see BCM2837CPU.h for the full chain this
	// closes.
	IOInterruptController *cpuIC =
	    getPlatform()->lookUpInterruptController((OSSymbol *)gPlatformInterruptControllerName);
	if (cpuIC == NULL) {
		IOLog("BCM2837LocalIntc: no IOPlatformInterruptController yet - real IRQ delivery will not work\n");
		return true;
	}
	IOInterruptHandler cpuICHandler = OSMemberFunctionCast(
		IOInterruptHandler, this, &BCM2837LocalIntc::handleInterrupt);
	IOReturn err = cpuIC->registerInterrupt(provider, 0, this, cpuICHandler, NULL);
	if (err != kIOReturnSuccess) {
		IOLog("BCM2837LocalIntc: registerInterrupt against IOPlatformInterruptController failed: %#x\n", err);
		return true;
	}
	cpuIC->enableInterrupt(provider, 0);
	IOLog("BCM2837LocalIntc: registered against IOPlatformInterruptController\n");

	return true;
}

void
BCM2837LocalIntc::stop(IOService *provider)
{
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
	uint32_t pending0 = fRegBase[kRegIRQPending0];

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
	// than kVectorGPUIRQ. Structurally impossible to hit in QEMU
	// (bcm2836_control_reset() zeroes the mailbox/local-timer control
	// registers that would need to be live for one of these to assert),
	// but real firmware may leave one enabled. A bit here that this
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
	uint32_t unexpected = pending0 & ~(1U << kVectorGPUIRQ);
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
