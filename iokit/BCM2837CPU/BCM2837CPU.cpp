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

	if (sBCM2837CPUIC == NULL) {
		IOCPUInterruptController *ic = OSTypeAlloc(IOCPUInterruptController);
		if (ic == NULL) {
			IOLog("BCM2837CPU: failed to allocate IOCPUInterruptController\n");
			return;
		}
		// One source (this CPU's own incoming IRQ line), one CPU - see
		// BCM2837CPU.h for why this needs to exist at all.
		if (ic->initCPUInterruptController(1, 1) != kIOReturnSuccess) {
			IOLog("BCM2837CPU: initCPUInterruptController failed\n");
			ic->release();
			return;
		}
		ic->attach(this);
		ic->registerCPUInterruptController();
		sBCM2837CPUIC = ic;
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
		IOLog("BCM2837CPU: ml_processor_register failed: %d\n", kr);
		return;
	}

	machProcessor = theProcessor;
	ipi_handler = ipiHandler;

	sBCM2837CPUIC->enableCPUInterrupt(this);

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
	return OSSymbol::withCString("cpu0");
}
