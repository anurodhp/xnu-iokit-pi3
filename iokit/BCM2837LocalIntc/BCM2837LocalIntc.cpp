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
