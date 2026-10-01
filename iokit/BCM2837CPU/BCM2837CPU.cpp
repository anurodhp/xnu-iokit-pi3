#include "BCM2837CPU.h"
#include "BCM2837LocalIntc.h"

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
#include <kern/clock.h>

// DAR-455. processor_start() is defined at osfmk/kern/processor.c:721 and
// declared for kernel callers only by the MIG-generated processor headers
// (osfmk/mach/processor.defs), which IOKit does not include;
// iokit/Kernel/IOCPU.cpp:309 calls it the same way. The secondary entry is
// BCM2837-only code in osfmk/arm64/start.s.
kern_return_t processor_start(processor_t processor);
extern char bcm2837_secondary_entry[];
// Defined at osfmk/arm64/machine_routines.c:1354; its prototype in
// osfmk/arm/machine_routines.h:639-643 is visible only to
// PEXPERT_KERNEL_PRIVATE/MACH_KERNEL_PRIVATE code, not IOKit.
vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
}

#define super IOCPU

OSDefineMetaClassAndStructors(BCM2837CPU, IOCPU);

// The shared "IOPlatformInterruptController" singleton - real Apple
// convention (see IOCPU.cpp) is one of these per platform, shared by every
// IOCPU. We only ever have one CPU/one source, but keep this a real
// (if trivially small) IOCPUInterruptController rather than special-casing
// away the mechanism sleh_irq() actually depends on.
static IOCPUInterruptController *sBCM2837CPUIC = NULL;

/*
 * DAR-455: number of CPUs the kernel's topology kept, i.e. the cpus=N
 * boot-arg capped at the DT's cpu nodes (osfmk/arm64/machine_routines.c
 * ml_parse_cpu_topology(); on BCM2837 the default is 1, see that file).
 * Read once by the boot CPU before it sizes the interrupt controller.
 */
static UInt32 sNumCPUs = 1;

/*
 * DAR-455: the Pi 3 firmware's spin table. The built-in armstub8 parks
 * cores 1-3 at EL2 in `wfe; ldr x4, [0xd8 + 8*core]; cbz x4, spin; br x4`
 * (raspberrypi/tools armstubs/armstub8.S:149-161, table at :175-189);
 * u-boot's own bcm2837.dtsi lists the same release addresses
 * (third_party/u-boot/arch/arm/dts/bcm2837.dtsi:41-71). QEMU's raspi3
 * models the same stub (hw/arm/raspi.c write_smpboot64()). Page 0 lies
 * below XNU's phys_base (0x1004000 on the Pi, cmd/bootxnu.c:14-15,146),
 * so XNU never hands it out as RAM; it is mapped here as device memory, so
 * the write reaches DRAM for a core that reads it with caches off.
 */
enum {
	kSpinTablePhys  = 0xd8,
	kSpinTableSlots = 4,
};
static volatile uint64_t *sSpinTable = NULL;

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
	if (name == NULL || strncmp(name, "cpu", 3) != 0 ||
	    name[3] < '0' || name[3] > '3' || name[4] != '\0') {
		return NULL;
	}
	// DAR-455: a secondary only if the kernel's topology kept it.
	// IODTPlatformExpert::createNubs() sets "logical-cpu-id" on exactly
	// those nubs (iokit/Kernel/IOPlatformExpert.cpp:1612-1632), so with
	// the default cpus=1 cpu1-3 never match.
	if (name[3] != '0' && provider->getProperty("logical-cpu-id") == NULL) {
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

	// DAR-455. Logical number from the nub (createNubs()), physical core
	// from the DT "reg" (the core's MPIDR Aff0, devicetrees/rpi3.dts).
	fLogicalID = 0;
	OSNumber *logical = OSDynamicCast(OSNumber, provider->getProperty("logical-cpu-id"));
	if (logical != NULL) {
		fLogicalID = logical->unsigned32BitValue();
	}
	fPhysID = fLogicalID;
	OSData *reg = OSDynamicCast(OSData, provider->getProperty("reg"));
	if (reg != NULL && reg->getLength() >= sizeof(UInt32)) {
		fPhysID = *(const UInt32 *)reg->getBytesNoCopy();
	}
	setCPUNumber(fLogicalID);

	if (fLogicalID != (UInt32)ml_get_boot_cpu_number()) {
		return startSecondary();
	}

	sNumCPUs = ml_get_cpu_count();
	if (sNumCPUs < 1 || sNumCPUs > kSpinTableSlots) {
		panic("BCM2837CPU: topology reports %u CPUs", (unsigned)sNumCPUs);
	}

	initCPU(true);

	setCPUState(kIOCPUStateRunning);

	IOLog("BCM2837CPU: started \"%s\" (boot CPU, %u CPU(s) in topology)\n",
	    provider->getName() ? provider->getName() : "?", (unsigned)sNumCPUs);

	registerService();

	return true;
}

/*
 * DAR-455. ml_processor_register() for this CPU. For the boot CPU this runs
 * from initCPU() (on the boot CPU, as before). For a secondary it runs from
 * startSecondary() on the boot CPU, before the core exists, which is what
 * Apple's own CPU bring-up does: register every topology CPU, then
 * processor_start() it (iokit/Kernel/arm/AppleARMSMP.cpp:163-195).
 */
void
BCM2837CPU::registerWithMach(void)
{
	ml_processor_info_t procInfo;
	memset(&procInfo, 0, sizeof(procInfo));
	procInfo.cpu_id = (cpu_id_t)this;
	procInfo.supports_nap = FALSE;
	procInfo.log_id = fLogicalID;
	// cpu_data->cpu_phys_id: what the reset vector compares MPIDR_EL1 &
	// 0xff against to find a waking core's cpu_data
	// (osfmk/arm64/start.s:184-205, machine_routines.c:1218).
	procInfo.phys_id = fPhysID;
	procInfo.cluster_id = 0;
	procInfo.cluster_type = CLUSTER_TYPE_SMP;

	processor_t theProcessor = NULL;
	ipi_handler_t ipiHandler = NULL;
	perfmon_interrupt_handler_func pmiHandler = NULL;

	kern_return_t kr = ml_processor_register(&procInfo, &theProcessor, &ipiHandler, &pmiHandler);
	if (kr != KERN_SUCCESS) {
		// P0 backlog item 0 companion fix: this CPU never becomes enabled,
		// so the whole interrupt controller chain would hang forever
		// downstream instead of diagnosably failing here.
		panic("BCM2837CPU: ml_processor_register(cpu%u) failed: %d", (unsigned)fLogicalID, kr);
	}

	machProcessor = theProcessor;
	ipi_handler = ipiHandler;
}

/*
 * DAR-455. Bring up a secondary core. Runs on the boot CPU, on this nub's
 * matching thread.
 */
bool
BCM2837CPU::startSecondary(void)
{
	// The boot CPU's interrupt controller must exist before this core can
	// enable itself in initCPU(). The boot CPU publishes it only after it
	// is itself enabled (initCPU(true) below), and this lookup blocks
	// until then (IOPlatformExpert::lookUpInterruptController() sleeps
	// until the name is registered).
	IOInterruptController *ic =
	    getPlatform()->lookUpInterruptController((OSSymbol *)gPlatformInterruptControllerName);
	if (ic == NULL || ic != sBCM2837CPUIC) {
		IOLog("BCM2837CPU: cpu%u: no BCM2837 CPU interrupt controller, not starting\n",
		    (unsigned)fLogicalID);
		return false;
	}

	registerWithMach();
	setCPUState(kIOCPUStateStopped);
	registerService();

	// processor_start() -> cpu_start() (sets cpu_reset_handler =
	// start_cpu, cleans the cpu_data to PoC) -> PE_cpu_start() ->
	// startCPU() below (osfmk/kern/processor.c:721-850,
	// osfmk/arm64/cpu.c:815-880, iokit/Kernel/IOCPU.cpp:90-99).
	kern_return_t kr = processor_start(machProcessor);
	if (kr != KERN_SUCCESS) {
		IOLog("BCM2837CPU: cpu%u: processor_start failed: %d\n", (unsigned)fLogicalID, kr);
		return false;
	}

	// Diagnostic only: the core sets kIOCPUStateRunning in initCPU(), on
	// itself. Nothing waits for it otherwise (Apple's PE_cpu_start() does
	// not either), but a core that never arrives would otherwise be
	// silent until IOCPUInterruptController::registerInterrupt() blocks
	// forever waiting for it.
	uint64_t deadline;
	clock_interval_to_deadline(2, kSecondScale, &deadline);
	while (getCPUState() != kIOCPUStateRunning && mach_absolute_time() < deadline) {
		IOSleep(10);
	}
	if (getCPUState() == kIOCPUStateRunning) {
		IOLog("BCM2837CPU: cpu%u (core %u) is running\n", (unsigned)fLogicalID, (unsigned)fPhysID);
	} else {
		IOLog("BCM2837CPU: cpu%u (core %u) did NOT come up within 2 s\n",
		    (unsigned)fLogicalID, (unsigned)fPhysID);
	}
	return true;
}

void
BCM2837CPU::initCPU(bool boot)
{
	if (!boot) {
		// Resume (PE_cpu_machine_init(target, FALSE)). ml_arm_sleep() set
		// SIGPdisabled again (osfmk/arm64/machine_routines.c:592); clear it
		// the way Apple does on every init - see the DAR-375 comment below.
		// Latent here: this port never sleeps (quiesceCPU()).
		signalCPU(this);
		return;
	}

	// DAR-455: on a secondary core this runs on that core, from
	// PE_cpu_machine_init() via cpu_machine_init() at splsched
	// (osfmk/arm/cpu_common.c:650-666, osfmk/kern/startup.c:893-899). The
	// boot CPU already created the controller and registered this CPU
	// with Mach (startSecondary()); what must happen here, on the core
	// itself, is enableCPUInterrupt(): it installs the IRQ handler through
	// ml_install_interrupt_handler(), which writes getCpuDatap(), i.e. the
	// calling core's cpu_data (osfmk/arm64/machine_routines.c:683-703,
	// iokit/Kernel/IOCPU.cpp:640). No IOLog here: interrupts are off.
	if (fLogicalID != (UInt32)ml_get_boot_cpu_number()) {
		sBCM2837CPUIC->enableCPUInterrupt(this);
		if (ipi_handler != NULL) {
			BCM2837LocalIntc::setIPIHandler(ipi_handler);
			// Self-IPI to clear SIGPdisabled, as on the boot CPU below. A
			// no-op until BCM2837LocalIntc has started; its start() then
			// signals every CPU once the mailboxes are live.
			signalCPU(this);
		}
		setCPUState(kIOCPUStateRunning);
		kprintf("BCM2837CPU: cpu%u (core %u) initCPU on core %llu\n",
		    (unsigned)fLogicalID, (unsigned)fPhysID,
		    (unsigned long long)(__builtin_arm_rsr64("MPIDR_EL1") & 0xff));
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
		// One source per CPU (each CPU's own incoming IRQ line; the source
		// number is the CPU number, IOCPU.cpp:640/744-760), one entry per
		// CPU the topology kept (DAR-455; 1 unless booted with cpus=N).
		// registerInterrupt() then blocks until every one of them is
		// enabled (IOCPU.cpp:690-700). See BCM2837CPU.h for why this needs
		// to exist at all.
		if (ic->initCPUInterruptController((int)sNumCPUs, (int)sNumCPUs) != kIOReturnSuccess) {
			panic("BCM2837CPU: initCPUInterruptController failed");
		}
		ic->attach(this);
		sBCM2837CPUIC = ic;
		justCreatedIC = true;
	}

	registerWithMach();

	sBCM2837CPUIC->enableCPUInterrupt(this);

	/*
	 * DAR-375: open this CPU for signals, or it never idles.
	 *
	 * Every CPU starts with cpu_signal = SIGPdisabled
	 * (osfmk/arm64/cpu.c:738). The only thing that clears it is
	 * cpu_signal_handler() -> cpu_signal_handler_internal(FALSE)
	 * (osfmk/arm/cpu_common.c:541-562), and that is the IPI handler
	 * ml_processor_register() just handed back
	 * (osfmk/arm64/machine_routines.c:1255). On a real multi-core Apple SoC
	 * the other cores' IPIs call it soon enough. Here cpu1-3 are parked
	 * and this class sends no IPIs, so it never ran: cpu0 kept
	 * SIGPdisabled for its whole uptime. cpu_idle() treats that as "idle
	 * disallowed" and returns before WFI (osfmk/arm64/cpu.c:409-414), so
	 * processor_idle() spun, ~22,000 loops/s at an idle shell. That was
	 * QEMU's ~100% host CPU, and on a real Pi a core that never idles.
	 * Confirmed from guest memory: idle_enable == 1,
	 * cpu_data[0].cpu_signal == 0x80000000.
	 *
	 * Apple's boot-CPU path clears it by sending the CPU an IPI to itself
	 * on every init, boot and resume alike: PE_cpu_machine_init(), "Send
	 * myself an IPI to clear SIGPdisabled" (iokit/Kernel/arm/AppleARMSMP.cpp
	 * :242-247). Do the same, through core 0's mailbox 0 (signalCPU()).
	 * Apple then waits for ml_cpu_signal_is_enabled(). This cannot wait:
	 * the mailbox is BCM2837LocalIntc's, and that driver only starts after
	 * this CPU's interrupt controller is published below. So the IPI raised
	 * here is a no-op at boot, and BCM2837LocalIntc::start() raises the one
	 * that clears the flag, once the mailbox IRQ is live. Until then the
	 * flag stays set, so a self-xcall keeps failing with KERN_FAILURE as it
	 * always has, instead of succeeding with nothing to deliver it.
	 *
	 * Once the flag is clear, a self-xcall (SIGPxcall/SIGPxcallImm, the one
	 * self-signal that checks it, cpu_common.c:429-435) succeeds and is
	 * delivered by that same mailbox IPI.
	 */
	if (ipi_handler != NULL) {
		BCM2837LocalIntc::setIPIHandler(ipi_handler);
		signalCPU(this);
	}

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
	// this port registers one IOCPU unless booted with cpus=N (DAR-455),
	// never takes a processor offline, and never sleeps the kernel (PE_halt_restart is still
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
	// machine layer, that owes that call. On a default (cpus=1) boot
	// only cpu0 exists, so in practice this branch is always taken.
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
	/*
	 * DAR-455. Release this core from the firmware's spin table. Reached
	 * from processor_start() -> cpu_start() -> PE_cpu_start() on the boot
	 * CPU, after cpu_start() has pointed this core's cpu_reset_handler at
	 * start_cpu and cleaned its cpu_data to PoC (osfmk/arm64/cpu.c
	 * :815-880). Like Apple's PE_cpu_start() (AppleARMSMP.cpp:207-217) it
	 * ignores the start/arg addresses (cpu_start() passes NULL): the core
	 * enters at bcm2837_secondary_entry (osfmk/arm64/start.s), which drops
	 * EL2 -> EL1 if needed and continues into LowResetVectorBase, the
	 * same reset path an Apple core takes.
	 */
	if (fLogicalID == (UInt32)ml_get_boot_cpu_number()) {
		return KERN_SUCCESS;    // cpu_start() never calls this for itself
	}
	if (fPhysID == 0 || fPhysID >= kSpinTableSlots) {
		IOLog("BCM2837CPU: startCPU: core %u has no spin-table slot\n", (unsigned)fPhysID);
		return KERN_FAILURE;
	}

	if (sSpinTable == NULL) {
		// Page 0 holds the stub and the table; map it once, as device
		// memory, so neither our write nor our reads go through a cache.
		// Not IODeviceMemory: IOKit asserts a mapping's first physical
		// address is non-zero (iokit/Kernel/IOMemoryDescriptor.cpp:5013,
		// a panic on the first QEMU try). ml_io_map() maps it VM_WIMG_IO
		// (device memory) through pmap_map(), which takes page 0 like any
		// other (osfmk/arm64/machine_routines.c:1354-1359,
		// osfmk/arm/pmap.c:4100-4126). Kept for the life of the system.
		vm_offset_t va = ml_io_map(0, PAGE_SIZE);
		if (va == 0) {
			IOLog("BCM2837CPU: startCPU: cannot map the spin-table page\n");
			return KERN_FAILURE;
		}
		sSpinTable = (volatile uint64_t *)(va + kSpinTablePhys);
		// Diagnostic for the real Pi: the first words of the firmware stub
		// (armstub8.S:98 onwards) and the table as we found it.
		volatile uint32_t *stub = (volatile uint32_t *)va;
		IOLog("BCM2837CPU: spin table page: stub %08x %08x %08x %08x, "
		    "slots %llx %llx %llx %llx\n",
		    stub[0], stub[1], stub[2], stub[3],
		    (unsigned long long)sSpinTable[0], (unsigned long long)sSpinTable[1],
		    (unsigned long long)sSpinTable[2], (unsigned long long)sSpinTable[3]);
	}

	if (sSpinTable[fPhysID] != 0) {
		// The stub would already have taken a non-zero value, so the core
		// is not sitting in the spin loop we know how to release.
		IOLog("BCM2837CPU: startCPU: core %u spin slot already %llx, not releasing\n",
		    (unsigned)fPhysID, (unsigned long long)sSpinTable[fPhysID]);
		return KERN_FAILURE;
	}

	uint64_t entry = (uint64_t)ml_static_vtop((vm_offset_t)bcm2837_secondary_entry);
	IOLog("BCM2837CPU: releasing core %u at PA 0x%llx\n", (unsigned)fPhysID,
	    (unsigned long long)entry);
	sSpinTable[fPhysID] = entry;
	// Store complete before the event that wakes the core's WFE.
	__asm__ volatile ("dsb sy" ::: "memory");
	__asm__ volatile ("sev");
	return KERN_SUCCESS;
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
	//    if that CPU is still kIOCPUStateRunning. Unreachable here:
	//    nothing in this port ever sleeps the kernel. With cpus=N
	//    (DAR-455) secondaries exist, and this would still only log and
	//    return - the "Spurious wakeup" panic is the honest outcome for
	//    an S2R this board cannot do.
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
		// DAR-455: "cpuN", the DT node this instance matched.
		char name[8];
		snprintf(name, sizeof(name), "cpu%u", (unsigned)fLogicalID);
		fCPUName = OSSymbol::withCString(name);
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

/*
 * DAR-375. IOCPU::signalCPU() is a no-op (iokit/Kernel/IOCPU.cpp:460-463); a
 * real Apple CPU class sends an IPI. cpu_signal_internal() has posted the
 * signal bits and xcall arguments and then called PE_cpu_signal() -> here
 * (osfmk/arm/cpu_common.c:429-498). Raise a real IPI through the target
 * core's mailbox 0 (BCM2837LocalIntc::sendIPI()); on a default boot
 * (cpus=1) that is always core 0 signalling itself, as before DAR-455. It is taken as an IRQ once this
 * CPU unmasks IRQs, and BCM2837LocalIntc runs the IPI handler from
 * interrupt context, as a real IPI would.
 *
 * Not called inline. An earlier version of this fix ran the handler right
 * here, inside the sender. That runs a pended decrementer or an xcall in
 * the middle of whatever posted it, which XNU never expects. While that
 * version was in, one boot in about six panicked "RootDomain: not inside PM
 * gate" on the PM workloop, a panic not seen in ~30 boots before it.
 * Consistent with, though not proven to be, that inline delivery.
 */
void
BCM2837CPU::signalCPU(IOCPU *target)
{
	// DAR-455: any CPU, through the target core's own mailbox 0.
	BCM2837CPU *cpu = OSDynamicCast(BCM2837CPU, target);
	if (cpu == NULL) {
		return;
	}
	BCM2837LocalIntc::sendIPI(cpu->fPhysID);
}
