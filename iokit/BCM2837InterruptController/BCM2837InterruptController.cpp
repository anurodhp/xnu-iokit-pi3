#include "BCM2837InterruptController.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <string.h>

#define super IOInterruptController

OSDefineMetaClassAndStructors(BCM2837InterruptController, IOInterruptController);

IOService *
BCM2837InterruptController::probe(IOService *provider, SInt32 *score)
{
	IOService *result = super::probe(provider, score);
	if (result == NULL) {
		return NULL;
	}
	const char *name = provider->getName();
	if (name == NULL || strcmp(name, "intc") != 0) {
		return NULL;
	}
	return result;
}

bool
BCM2837InterruptController::init(OSDictionary *propTable)
{
	if (!super::init(propTable)) {
		return false;
	}

	fRegMap = NULL;
	fRegBase = NULL;

	// Allocate the vector table + per-vector locks up front, same pattern
	// IOSharedInterruptController::initInterruptController uses - the base
	// class declares `vectors` (IOInterruptController.h) but does not
	// allocate it for us.
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
BCM2837InterruptController::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}

	fRegMap = provider->mapDeviceMemoryWithIndex(0);
	if (fRegMap == NULL) {
		IOLog("BCM2837InterruptController: failed to map \"intc\" registers\n");
		return false;
	}
	// Backlog item 26: confirm the mapping actually covers the highest
	// offset this driver touches (kRegDisableBasicIRQs = 0x24) before
	// trusting it - "intc"'s own reg property declares 0x200 bytes, so
	// this should always pass; catches a misconfigured DT rather than a
	// runtime condition.
	if (fRegMap->getLength() < (kRegDisableBasicIRQs * sizeof(uint32_t)) + sizeof(uint32_t)) {
		IOLog("BCM2837InterruptController: \"intc\" register mapping too small (%llu bytes)\n",
		    (unsigned long long)fRegMap->getLength());
		fRegMap->release();
		fRegMap = NULL;
		return false;
	}
	fRegBase = (volatile uint32_t *)fRegMap->getVirtualAddress();

	// Mask everything at the hardware before we start trusting the
	// enable/disable vector hooks to be the only thing touching these
	// registers.
	fRegBase[kRegDisableIRQs1] = 0xFFFFFFFF;
	fRegBase[kRegDisableIRQs2] = 0xFFFFFFFF;
	fRegBase[kRegDisableBasicIRQs] = 0xFFFFFFFF;

	// Backlog item 9: explicitly clear FIQ_CONTROL (bit 7 = FIQ enable,
	// bits 6:0 = routed source) rather than leaving it at whatever
	// firmware left it. This driver only ever handles sources via the
	// normal IRQ path (handleInterrupt() below) - if firmware left some
	// source FIQ-routed, it would arrive at sleh_fiq() instead and get
	// misclassified as a timer tick (sleh_fiq() has no other case for an
	// unrecognized FIQ source). QEMU already resets this to 0, so this is
	// a no-op there; real firmware behavior is unverified, hence writing
	// it explicitly rather than assuming.
	fRegBase[kRegFIQControl] = 0;

	// Publish ourselves under the exact symbol IODTMapOneInterrupt() will
	// look us up by: IODTInterruptControllerName() (IODeviceTreeSupport.cpp)
	// derives "IOInterruptController<AAPL,phandle in hex>" from the "intc"
	// node's own phandle - it is NOT a fixed/chosen name, confirmed by
	// reading that function directly. A hardcoded string here (what this
	// used to do) would silently make every registerInterrupt() call from
	// a device whose interrupt-parent points at us fail to find this
	// controller at all.
	const OSSymbol *controllerName = IODTInterruptControllerName(provider);
	if (controllerName == NULL) {
		IOLog("BCM2837InterruptController: IODTInterruptControllerName failed (no AAPL,phandle on \"intc\"?)\n");
		return false;
	}
	getPlatform()->registerInterruptController((OSSymbol *)controllerName, this);

	// Cascade: we are ourselves a consumer of one interrupt on our
	// provider (local-intc), source 0 (its "interrupts = <8 ...>" is
	// wired up by the DT parse). When it fires, it calls the handler we
	// register here, which is us.
	IOReturn err = provider->registerInterrupt(0, this,
	    OSMemberFunctionCast(IOInterruptAction, this,
	    &BCM2837InterruptController::handleInterrupt), NULL);
	if (err != kIOReturnSuccess) {
		IOLog("BCM2837InterruptController: registerInterrupt on provider failed: 0x%x\n", err);
		controllerName->release();
		return false;
	}
	provider->enableInterrupt(0);

	IOLog("BCM2837InterruptController: started, base=%p, registered as \"%s\"\n",
	    (void *)fRegBase, controllerName->getCStringNoCopy());
	controllerName->release();

	return true;
}

void
BCM2837InterruptController::stop(IOService *provider)
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
BCM2837InterruptController::free(void)
{
	// Backlog item 27: if start() fails after fRegMap is already set (e.g.
	// a later registerInterrupt()/registerInterruptController() step
	// fails), IOKit calls free() to tear down the never-started instance,
	// not stop() - stop()'s own fRegMap release (further down this file)
	// never runs in that path. Safe to also release here regardless: on
	// the normal stop()-then-free() path, stop() already NULLed fRegMap,
	// so this is a no-op there.
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
BCM2837InterruptController::getInterruptHandlerAddress(void)
{
	return OSMemberFunctionCast(IOInterruptAction, this,
	           &BCM2837InterruptController::handleInterrupt);
}

void
BCM2837InterruptController::dispatchBank(uint32_t pending, IOInterruptVectorNumber base)
{
	while (pending != 0) {
		int bit = __builtin_ctz(pending);
		pending &= ~(1U << bit);

		IOInterruptVectorNumber vectorNumber = base + bit;
		IOInterruptVector *vector = &vectors[vectorNumber];

		vector->interruptActive = 1;
		OSMemoryBarrier();

		if (!vector->interruptDisabledSoft) {
			if (vector->interruptRegistered) {
				vector->handler(vector->target, vector->refCon, vector->nub, vector->source);
			}
		} else {
			// Lazy hard-disable, matching Apple's own reference pattern
			// (iokit/Examples/drvGenericInterruptController/
			// GenericInterruptController.cpp's handleInterrupt()) - found
			// missing via adversarial review 2026-08-13. Without this, a
			// client that soft-disables a still-asserted level source (see
			// getVectorType() - these are all level-triggered) never gets
			// its handler called again to clear the condition at the
			// device, the pending bit never actually clears in hardware,
			// and the CPU IRQ line stays asserted - sleh_irq() would
			// re-enter immediately on return, a real livelock.
			vector->interruptDisabledHard = 1;
			disableVectorHard(vectorNumber, vector);
		}

		vector->interruptActive = 0;
	}
}

IOReturn
BCM2837InterruptController::handleInterrupt(void * /*refCon*/, IOService * /*nub*/, int /*source*/)
{
	// Basic-bank bits 8-9 mirror "GPU IRQ pending in bank 1/2" (i.e. bit 8
	// tells you pending_1 is non-zero, bit 9 that pending_2 is non-zero);
	// simplest correct approach is to just always check all three banks.
	uint32_t pending1 = fRegBase[kRegIRQPending1];
	uint32_t pending2 = fRegBase[kRegIRQPending2];
	uint32_t pendingBasic = fRegBase[kRegIRQBasicPending] & 0xFF; // low 8 bits = the 8 ARM-specific basic sources

	if (pending1 != 0) {
		dispatchBank(pending1, kVectorBank1Base);
	}
	if (pending2 != 0) {
		dispatchBank(pending2, kVectorBank2Base);
	}
	if (pendingBasic != 0) {
		dispatchBank(pendingBasic, kVectorBasicBase);
	}

	return kIOReturnSuccess;
}

bool
BCM2837InterruptController::vectorCanBeShared(IOInterruptVectorNumber /*vectorNumber*/, IOInterruptVector * /*vector*/)
{
	// Keep it simple for now: one device per line. Revisit if two DT nodes
	// ever need to share a bit (shouldn't happen for sdhci/gpio/aux).
	return false;
}

void
BCM2837InterruptController::initVector(IOInterruptVectorNumber /*vectorNumber*/, IOInterruptVector * /*vector*/)
{
	// Nothing hardware-specific needed at first-use time; disableVectorHard
	// already masked everything in start().
}

int
BCM2837InterruptController::getVectorType(IOInterruptVectorNumber /*vectorNumber*/, IOInterruptVector * /*vector*/)
{
	// BCM2835-style peripheral IRQs are level-triggered: the pending bit
	// stays set until the device itself deasserts, it doesn't self-clear.
	return kIOInterruptTypeLevel;
}

void
BCM2837InterruptController::disableVectorHard(IOInterruptVectorNumber vectorNumber, IOInterruptVector * /*vector*/)
{
	if (!vectorInRange(vectorNumber)) {
		IOLog("BCM2837InterruptController: disableVectorHard: vectorNumber %d out of range\n", (int)vectorNumber);
		return;
	}
	if (vectorNumber < kVectorBank2Base) {
		fRegBase[kRegDisableIRQs1] = (1U << (vectorNumber - kVectorBank1Base));
	} else if (vectorNumber < kVectorBasicBase) {
		fRegBase[kRegDisableIRQs2] = (1U << (vectorNumber - kVectorBank2Base));
	} else {
		fRegBase[kRegDisableBasicIRQs] = (1U << (vectorNumber - kVectorBasicBase));
	}
}

void
BCM2837InterruptController::enableVector(IOInterruptVectorNumber vectorNumber, IOInterruptVector * /*vector*/)
{
	if (!vectorInRange(vectorNumber)) {
		IOLog("BCM2837InterruptController: enableVector: vectorNumber %d out of range\n", (int)vectorNumber);
		return;
	}
	if (vectorNumber < kVectorBank2Base) {
		fRegBase[kRegEnableIRQs1] = (1U << (vectorNumber - kVectorBank1Base));
	} else if (vectorNumber < kVectorBasicBase) {
		fRegBase[kRegEnableIRQs2] = (1U << (vectorNumber - kVectorBank2Base));
	} else {
		fRegBase[kRegEnableBasicIRQs] = (1U << (vectorNumber - kVectorBasicBase));
	}
}
