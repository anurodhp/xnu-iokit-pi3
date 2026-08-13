#include "BCM2837CPU.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <string.h>

#define super IOCPU

OSDefineMetaClassAndStructors(BCM2837CPU, IOCPU);

// The shared "IOPlatformInterruptController" singleton - real Apple
// convention (see IOCPU.cpp) is one of these per platform, shared by every
// IOCPU. We only ever have one CPU/one source, but keep this a real
// (if trivially small) IOCPUInterruptController rather than special-casing
// away the mechanism sleh_irq() actually depends on.
static IOCPUInterruptController *sBCM2837CPUIC = NULL;

// Defensive name re-check (backlog item 13) - every other DT-matched
// driver in this tree (BCM2837ArmIO, BCM2837InterruptController,
// BCM2837LocalIntc, BCM2837SDHCI) already re-verifies provider->getName()
// in probe() rather than trusting IONameMatch alone; this class was the
// one exception. Cheap insurance: if this were ever to over-match against
// a sibling DT node, initCPU() would call enableCPUInterrupt() an extra
// time, driving enabledCPUs past numCPUs and permanently wedging
// IOCPUInterruptController::registerInterrupt()'s thread_block() wait for
// every future caller (see IOCPU.cpp).
IOService *
BCM2837CPU::probe(IOService *provider, SInt32 *score)
{
	IOService *result = super::probe(provider, score);
	if (result == NULL) {
		return NULL;
	}
	const char *name = provider->getName();
	if (name == NULL || strcmp(name, "cpu0") != 0) {
		return NULL;
	}
	return result;
}

bool
BCM2837CPU::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}

	initCPU(true);

	setCPUState(kIOCPUStateRunning);

	IOLog("BCM2837CPU: started \"%s\"\n", provider->getName() ? provider->getName() : "?");

	registerService();

	return true;
}

void
BCM2837CPU::initCPU(bool boot)
{
	if (!boot) {
		return;
	}

	// Backlog item 14: registerCPUInterruptController() used to run in
	// this block, immediately after allocation - that call is what makes
	// getPlatform()->lookUpInterruptController() find this controller
	// from ANY config thread, including BCM2837LocalIntc's, which may be
	// matching concurrently on a different config thread. If it ran
	// before enableCPUInterrupt() (further down, after
	// ml_processor_register()), a lookup landing in that window would
	// find a real controller and call registerInterrupt() against it,
	// which correctly self-heals by blocking in
	// IOCPUInterruptController::registerInterrupt()'s thread_block() -
	// see that function - until enabledCPUs catches up. Real per
	// IOCPU.cpp: neither registerCPUInterruptController() nor
	// enableCPUInterrupt() depends on the other having already run - so
	// eliminate the window entirely rather than relying on the self-heal:
	// only publish the controller after this CPU is actually enabled.
	bool justCreatedIC = false;
	if (sBCM2837CPUIC == NULL) {
		IOCPUInterruptController *ic = OSTypeAlloc(IOCPUInterruptController);
		if (ic == NULL) {
			// P0 backlog item 0 companion fix (2026-08-13, adversarial
			// review): panic instead of silently returning. With
			// BCM2837LocalIntc/BCM2837InterruptController now correctly
			// blocking on IOPlatformInterruptController before publishing
			// themselves (see their own start()'s matching comment), a
			// failure here no longer just "degrades to no real IRQ
			// delivery" - it hangs the whole board's interrupt controller
			// chain forever, silently, on a config thread with no other
			// diagnostic. Loud and immediate is strictly better than a
			// silent permanent hang.
			panic("BCM2837CPU: failed to allocate IOCPUInterruptController");
		}
		// One source (this CPU's own incoming IRQ line), one CPU - see
		// BCM2837CPU.h for why this needs to exist at all.
		if (ic->initCPUInterruptController(1, 1) != kIOReturnSuccess) {
			panic("BCM2837CPU: initCPUInterruptController failed");
		}
		ic->attach(this);
		sBCM2837CPUIC = ic;
		justCreatedIC = true;
	}

	ml_processor_info_t procInfo;
	memset(&procInfo, 0, sizeof(procInfo));
	procInfo.cpu_id = (cpu_id_t)this;
	procInfo.supports_nap = FALSE;
	procInfo.log_id = 0;
	procInfo.phys_id = 0;
	procInfo.cluster_id = 0;
	procInfo.cluster_type = CLUSTER_TYPE_SMP;

	processor_t theProcessor = NULL;
	ipi_handler_t ipiHandler = NULL;
	perfmon_interrupt_handler_func pmiHandler = NULL;

	kern_return_t kr = ml_processor_register(&procInfo, &theProcessor, &ipiHandler, &pmiHandler);
	if (kr != KERN_SUCCESS) {
		// P0 backlog item 0 companion fix - see the matching comment above,
		// same reasoning: this CPU never becomes enabled, so the whole
		// interrupt controller chain hangs forever downstream instead of
		// diagnosably failing here.
		panic("BCM2837CPU: ml_processor_register failed: %d", kr);
	}

	machProcessor = theProcessor;
	ipi_handler = ipiHandler;

	sBCM2837CPUIC->enableCPUInterrupt(this);

	// Publish the controller only now that it's actually enabled - see
	// the comment above at its allocation for why this moved here.
	if (justCreatedIC) {
		sBCM2837CPUIC->registerCPUInterruptController();
	}

	IOLog("BCM2837CPU: initCPU done, machProcessor=%p\n", (void *)machProcessor);
}

void
BCM2837CPU::quiesceCPU(void)
{
	// Single effectively-running core, never sleeps in this project's
	// testing so far - nothing to do.
}

kern_return_t
BCM2837CPU::startCPU(vm_offset_t /*start_paddr*/, vm_offset_t /*arg_paddr*/)
{
	// Secondary cores are parked below IOKit entirely (see project
	// memory: QEMU-side SMP-parking fix) - this project has no real
	// mechanism to bring one up from here, and nothing should be asking
	// to (we only ever register one IOCPU). Fail loudly rather than
	// silently pretending to succeed.
	IOLog("BCM2837CPU: startCPU called but not supported (single-core boot)\n");
	return KERN_FAILURE;
}

void
BCM2837CPU::haltCPU(void)
{
	IOLog("BCM2837CPU: haltCPU called (unsupported, ignoring)\n");
}

const OSSymbol *
BCM2837CPU::getCPUName(void)
{
	// Cached rather than allocating a fresh retained OSSymbol on every
	// call (backlog item 17) - OSSymbol::withCString() returns a ref the
	// caller doesn't release, and getCPUName() has no documented "caller
	// owns this" contract that would make repeated allocation correct.
	if (fCPUName == NULL) {
		fCPUName = OSSymbol::withCString("cpu0");
	}
	return fCPUName;
}

void
BCM2837CPU::free(void)
{
	if (fCPUName != NULL) {
		fCPUName->release();
		fCPUName = NULL;
	}
	super::free();
}
