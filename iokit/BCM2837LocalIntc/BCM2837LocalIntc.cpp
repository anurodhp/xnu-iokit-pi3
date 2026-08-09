#include "BCM2837LocalIntc.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceTreeSupport.h>
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
	fRegBase = (volatile uint32_t *)fRegMap->getVirtualAddress();

	// Force GPU IRQ routing to core 0 (bits[2:0]=0), matching Linux's own
	// irq-bcm2836.c init - real hardware/QEMU default this way already,
	// but don't rely on firmware/QEMU state being correct.
	fRegBase[kRegGPURouting] = 0;

	const OSSymbol *controllerName = IODTInterruptControllerName(provider);
	if (controllerName == NULL) {
		IOLog("BCM2837LocalIntc: IODTInterruptControllerName failed (no AAPL,phandle on \"local-intc\"?)\n");
		return false;
	}
	getPlatform()->registerInterruptController((OSSymbol *)controllerName, this);

	IOLog("BCM2837LocalIntc: started, base=%p, registered as \"%s\"\n",
	    (void *)fRegBase, controllerName->getCStringNoCopy());
	controllerName->release();

	// No registerInterrupt() against our own provider - local-intc is the
	// top of this board's interrupt tree (see header), not a cascade
	// consumer of anything else.

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
		if (!vector->interruptDisabledSoft && vector->interruptRegistered) {
			vector->handler(vector->target, vector->refCon, vector->nub, vector->source);
		}
		vector->interruptActive = 0;
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
