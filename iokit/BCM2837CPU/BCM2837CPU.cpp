#include "BCM2837CPU.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <string.h>

// Same two headers, same reason, as iokit/Kernel/IOCPU.cpp:31-35 pulls
// them in: ml_set_interrupts_enabled() (osfmk/arm/machine_routines.h:70)
// and master_cpu (osfmk/kern/cpu_number.h:62, the value IOCPU.cpp itself
// compares getCPUNumber() against - e.g. IOCPU.cpp:158, :259).
// IOCPURunPlatformQuiesceActions() and kprintf() need no include of
// their own here: IOKit/IOCPU.h already pulls in
// <IOKit/IOPlatformActions.h> and <pexpert/pexpert.h> respectively.
extern "C" {
#include <machine/machine_routines.h>
#include <kern/cpu_number.h>
}

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
	// DAR-157. This function MUST NOT RETURN, and that is a contract of
	// its callers rather than a style choice. Nothing in this tree ever
	// defines USE_APPLEARMSMP, so the half of IOCPU.cpp that compiles
	// here is the `#if !USE_APPLEARMSMP` one (iokit/Kernel/IOCPU.cpp:67)
	// - the same reason a concrete IOCPU subclass has to exist at all
	// (see BCM2837CPU.h) - and the only path in is:
	//
	//   processor_offline_intstack()      osfmk/kern/machine.c:441-456
	//     -> cpu_sleep()                  osfmk/arm64/cpu.c:306
	//       -> PE_cpu_machine_quiesce()   osfmk/arm64/cpu.c:337
	//         -> IOCPU::quiesceCPU()      iokit/Kernel/IOCPU.cpp:162
	//
	// cpu_sleep()'s call is immediately followed by `/*NOTREACHED*/`
	// (osfmk/arm64/cpu.c:339), and its own caller has
	// panic("zombie processor") on the very next line
	// (osfmk/kern/machine.c:455). The empty body this replaces would
	// have fallen straight into that panic.
	//
	// UNEXERCISED on this platform, and a QEMU boot cannot change that:
	// this port registers exactly one IOCPU, never takes a processor
	// offline, and never sleeps the kernel (PE_halt_restart is still
	// NULL here - iokit/Kernel/IOPlatformExpert.cpp:334). A boot test
	// proves only that this still builds and still boots, never that the
	// park below is correct.

	// Interrupts off first. We are about to stop servicing them forever,
	// and IOCPURunPlatformQuiesceActions() asserts
	// preemption_enabled() == false (iokit/Kernel/IOPlatformActions.cpp:134),
	// which osfmk/kern/sched_prim.c:5978-5981 defines as
	// `get_preemption_level() == 0 && ml_get_interrupts_enabled()` - so
	// masking here satisfies that assert on any caller. The real caller
	// already runs with interrupts disabled
	// (osfmk/kern/machine.c:376's assert(ml_get_interrupts_enabled() ==
	// FALSE)); this is belt-and-braces, not a fix for it.
	(void) ml_set_interrupts_enabled(FALSE);

	// Boot CPU runs the platform quiesce actions. That split is Apple's
	// own: PE_cpu_machine_quiesce() in
	// iokit/Kernel/arm/AppleARMSMP.cpp:292-300 calls
	// IOCPURunPlatformQuiesceActions() for the boot CPU and hands every
	// other core to the SoC's power manager (gPMGR->disableCPUCore()),
	// which BCM2837 has no equivalent of. osfmk/i386/cpu.c:95 says the
	// same thing about the IOCPU-driver path this build actually uses:
	// "This calls IOCPURunPlatformQuiesceActions when sleeping the boot
	// cpu" - i.e. on this path it is the IOCPU subclass, not the
	// machine layer, that owes that call. probe() only ever matches
	// "cpu0", so in practice this branch is always taken.
	if (getCPUNumber() == (UInt32)master_cpu) {
		IOCPURunPlatformQuiesceActions();
	}

	// kprintf(), not IOLog(): on the sleep path IOCPUSleepKernel() has
	// already called console_suspend() (iokit/Kernel/IOCPU.cpp:272)
	// before any CPU reaches here, and we are running with interrupts
	// masked on a core that is going away.
	kprintf("BCM2837CPU: quiesceCPU: parking cpu%u in WFI (no SoC sleep on BCM2837)\n",
	    (unsigned int)getCPUNumber());

	// The park itself. cpu_sleep() already did the cache maintenance
	// this needs - CleanPoC_Dcache(), osfmk/arm64/cpu.c:322 - before
	// calling us.
	//
	// This is a literal transliteration of the *generic* (i.e.
	// !APPLE_ARM64_ARCH_FAMILY) arm64_prepare_for_sleep(),
	// osfmk/arm64/machine_routines_asm.s:985-995, which is exactly
	// `Lwfi_inst: dsb sy ; isb sy ; wfi ; b Lwfi_inst`. That generic
	// variant is the one this board builds: nothing but
	// pexpert/pexpert/arm64/apple_arm64_common.h:46 ever defines
	// APPLE_ARM64_ARCH_FAMILY, and pexpert/pexpert/arm64/BCM2837.h - the
	// board config this target uses
	// (pexpert/pexpert/arm64/board_config.h:228-236) - does not include
	// it. On a SoC with no power controller that loop *is* the park
	// state; there is nothing further to gate off. The `b Lwfi_inst`
	// back-edge is not optional either: WFI is architecturally allowed
	// to complete on a wake-up event even with interrupts masked, so a
	// single WFI would fall through.
	//
	// Deliberately NOT ml_arm_sleep() (what AppleARMSMP.cpp:303 calls),
	// even though on this board it would end in this same instruction
	// sequence. Its boot-CPU half (osfmk/arm64/cpu.c:927-1008) exists to
	// set up a *resume*: wake_abstime (cpu.c:953) and
	// ml_set_reset_time(UINT64_MAX) deliberately stop mach_absolute_time()
	// so cpu_timebase_init() can stitch the timebase back together when
	// the core comes back in through cpu_data_ptr->cpu_reset_handler
	// (see the comment at osfmk/arm64/cpu.c:898-908), plus a
	// cpu_sleep_token other cores poll. Nothing in this port can bring a
	// core back that way, so that bookkeeping would only leave the
	// timebase state describing a wake that never happens.
	for (;;) {
		__asm__ volatile ("dsb sy" ::: "memory");
		__asm__ volatile ("isb sy" ::: "memory");
		__asm__ volatile ("wfi");
	}
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
	// DAR-157. haltCPU() is NOT the shutdown/reboot hook, despite the
	// name - that is PE_halt_restart (iokit/IOKit/IOPlatformExpert.h:88,
	// still NULL in this port: iokit/Kernel/IOPlatformExpert.cpp:334),
	// and nothing routes through here to reach it. In this whole tree
	// haltCPU() has exactly three call sites, all in
	// iokit/Kernel/IOCPU.cpp: PE_cpu_halt() (:106, which has no caller
	// anywhere in xnu-7195), and IOCPUSleepKernel()'s two - :265 for
	// each non-boot CPU and :282 for the boot CPU. So its only real
	// meaning here is "enter S2R", and its contract differs by CPU:
	//
	//  - Non-boot CPU (IOCPU.cpp:265): must actually go down; when the
	//    system wakes, IOCPU.cpp:305 panics "Spurious wakeup of cpu %u"
	//    if that CPU is still kIOCPUStateRunning. Unreachable here -
	//    this port registers exactly one IOCPU (probe() matches only
	//    "cpu0"), and startCPU() below refuses to bring a secondary up.
	//    Apple's equivalent for that case is processor_exit() (see
	//    PE_cpu_halt(), iokit/Kernel/arm/AppleARMSMP.cpp:251-256) -
	//    defined at osfmk/kern/processor.c:853 but declared only by the
	//    MIG-generated processor_server.h (osfmk/mach/processor.defs:98),
	//    not by osfmk/kern/processor.h, which declares just
	//    processor_exit_from_user() (:440). Writing that branch here
	//    would be unverifiable dead code reaching around a header.
	//
	//  - Boot CPU (IOCPU.cpp:282): "The system sleeps here"
	//    (IOCPU.cpp:277-280) and this call is expected to RETURN, on
	//    wake, into IOCPUSleepKernel()'s resume path (:283 onwards).
	//
	// BCM2837 has no suspend-to-RAM: no power controller, no wake
	// source, and nothing in this port programs a warm-boot resume
	// vector (cpu_data_ptr->cpu_reset_handler / the S2R token machinery
	// ml_arm_sleep() expects, osfmk/arm64/cpu.c:927-1008). So the honest
	// answer to "sleep the boot CPU" is to decline it and return, which
	// IOCPUSleepKernel() then handles as an immediate wake: it restarts
	// the PM watchdog, resumes the console and runs the post-resume
	// platform actions (IOCPU.cpp:283-297) with the machine still
	// running. The alternative - parking in WFI the way quiesceCPU()
	// does - would be a permanent hang needing a physical power cycle,
	// with no wake source that could ever end it. A WFI park is the
	// right answer only where returning is forbidden, which is
	// quiesceCPU(), not here.
	//
	// Deliberately changes no machine state (no interrupt masking, no
	// setCPUState()): we are returning, so the CPU must be left exactly
	// as it was found.
	//
	// UNEXERCISED: nothing in this project ever sleeps the kernel, so no
	// boot test can validate this beyond "still builds, still boots".
	//
	// kprintf() rather than IOLog() because IOCPUSleepKernel() calls
	// console_suspend() (IOCPU.cpp:272) before this point on the boot
	// CPU, so IOLog output could be swallowed exactly when it matters.
	kprintf("BCM2837CPU: haltCPU(cpu%u): sleep unsupported on BCM2837 "
	    "(no S2R/resume path) - declining, system stays running\n",
	    (unsigned int)getCPUNumber());
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
