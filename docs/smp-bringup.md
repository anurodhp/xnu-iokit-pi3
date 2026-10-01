# SMP bring-up (DAR-455)

Status: **Phase 1 (scoping) done 2026-10-01.** Nothing here is built or
verified yet; every claim below cites the file it rests on. Paths are
relative to the repo root, `third_party/` paths are the pinned forks.
`armstub8.S` is the Raspberry Pi firmware's built-in 64-bit stub,
`raspberrypi/tools` master, `armstubs/armstub8.S` (not vendored here; the
firmware on the Pi's card may embed an older version).

## (a) How the Pi 3 starts cores 1-3

- `sdcard/config.txt` has `arm_64bit=1`, `kernel=u-boot.bin` and **no
  `armstub=` / `kernel_old=` line**, so the firmware runs its built-in
  armstub8 on all four cores.
- armstub8, every core: EL3 setup, then `SCR_EL3`, `ACTLR_EL3 = 0x73`
  (bits 0,1,4,5,6: lower-EL access to CPUACTLR/CPUECTLR/L2CTLR/L2ECTLR/
  L2ACTLR), **`CPUECTLR_EL1.SMPEN = 1`**, `CNTFRQ_EL0 = 19.2 MHz`,
  `CNTVOFF_EL2 = 0`, `SCTLR_EL2 = 0x30c50830` (MMU, I and D cache off),
  then `eret` to **EL2** (armstub8.S:98-148).
- Core 0 jumps to the kernel (u-boot) at EL2. Cores 1-3 spin at EL2:
  `wfe; ldr x4, [0xd8 + 8*core]; cbz x4, spin; mov x0,#0; br x4`
  (armstub8.S:149-171). Release addresses: **core1 0xe0, core2 0xe8,
  core3 0xf0** (armstub8.S:175-189; same table in u-boot's
  `arch/arm/dts/bcm2837.dtsi:41-71`, `cpu-release-addr`). Not the QA7
  mailbox-3 method: that (0x4000008c + 0x10*core) is the 32-bit stub's,
  see QEMU's `write_smpboot()` `hw/arm/raspi.c:40-65`.
- u-boot does not touch them: `configs/rpi3_xnu_defconfig` sets neither
  `ARMV8_MULTIENTRY` nor `ARMV8_SPIN_TABLE`
  (`arch/arm/cpu/armv8/Kconfig:35-52`). `bootxnu` enters XNU on core 0 via
  `armv8_switch_to_el1()` with the D-cache off (`cmd/bootxnu.c:193-198`);
  that macro sets CNTHCTL_EL2 EL1PCEN/EL1PCTEN, CNTVOFF_EL2=0, VPIDR/VMPIDR,
  CPTR_EL2, HSTR_EL2, SCTLR_EL1, HCR_EL2.RW, SPSR_EL2=EL1h
  (`arch/arm/include/asm/macro.h:241-300`).
- XNU's RAM starts at `phys_base = CONFIG_SYS_LOAD_ADDR + 0x4000 =
  0x1004000` (`cmd/bootxnu.c:14-15,146`, `include/configs/rpi.h:100`), so
  page 0 (the stub and spin table) is outside what XNU manages.
- **So a woken secondary arrives at EL2, MMU/caches off, SMPEN already
  set, x0=0.** XNU needs an EL2->EL1 trampoline for secondaries (core 0
  gets that from u-boot). ACTLR_EL2 is not needed: XNU never touches
  CPUECTLR (SMPEN is the firmware's job, already done).

## (b) What XNU's arm64 secondary start needs

- **Wake path (Apple's, used unchanged):** `processor_start()`
  (`osfmk/kern/processor.c:721-850`) -> `cpu_start()` sets
  `cpu_reset_handler = start_cpu_paddr`, cleans `CpuDataEntries[cpu]` and
  the cpu_data to PoC, calls `PE_cpu_start(cpu_id, NULL, NULL)`
  (`osfmk/arm64/cpu.c:815-880`) -> in this build (`!USE_APPLEARMSMP`)
  `IOCPU::startCPU()` (`iokit/Kernel/IOCPU.cpp:90-99`). Apple passes no
  start address: the core must begin at the **physical address of
  `LowResetVectorBase`** (`osfmk/arm64/start.s:102-112`).
- `reset_vector` (start.s:126-291), MMU off, EL1: sets VBAR_EL1, looks up
  its cpu_data by `MPIDR_EL1 & 0xff == cpu_data->cpu_phys_id` through
  `ResetHandlerData.cpu_data_entries` (start.s:184-205; filled at
  cpu.c:1099-1100), checks the handler is `start_cpu`/`resume_idle_cpu`
  and `blr`s it (start.s:264-291). `start_cpu` reads boot args, stacks
  from cpu_data, `common_start` loads TCR from `sysreg_restore`, TTBR1 from
  `cpu_ttep`, enables the MMU, trampolines to `arm_init_cpu`
  (start.s:387-430, 726-990). All of those reads happen **with caches off**
  -> the data must be clean to PoC (QEMU does not model caches; only the
  Pi can catch a miss here).
- start.s:786-797 already has a `#if defined(BCM2837)` block: every core
  routes its own generic-timer IRQs to FIQ (`0x40000040 + 4*core = 0xF0`)
  before the MMU goes on. The scheduler tick is CNTV
  (`osfmk/arm64/machine_routines.c:1805,2052-2056`), per core.
- `arm_init_cpu()` -> `slave_main()` (`osfmk/arm/arm_init.c:623-737`) ->
  `cpu_machine_init()` -> `PE_cpu_machine_init(cpu_id, TRUE)` **on the new
  core** (`osfmk/arm/cpu_common.c:650-666`) -> `IOCPU::initCPU(true)`.
  `ml_install_interrupt_handler()` writes `getCpuDatap()`
  (`osfmk/arm64/machine_routines.c:683-703`), so `enableCPUInterrupt()`
  must run on the target core, i.e. from initCPU.
- **Topology:** `ml_parse_cpu_topology()` walks `/cpus`, honours
  `cpus=N` (`machine_routines.c:849-1009`, boot-arg at :859), phys_id =
  DT `reg`, boot CPU = `state = "running"`. `ml_processor_register()`
  refuses more registrations than `num_cpus` (:1178-1180), allocates a
  cpu_data for non-boot cores, sets `cpu_phys_id` (:1218).
  `IODTPlatformExpert::createNubs()` gives each `device_type = "cpu"` nub a
  `logical-cpu-id` only if topology kept it (`iokit/Kernel/
  IOPlatformExpert.cpp:1594-1632`). `machine_conf()` sets
  `max_cpus = max_cpu_id+1` (`osfmk/arm/machine_routines_common.c:
  1062-1076`). `MAX_CPUS = 4` for BCM2837
  (`pexpert/pexpert/arm64/board_config.h:228-236`).
- **Today's DT** has one node, `cpu0 { reg = <0xf00>; cpu-id = <0>;
  state = "running"; }`, no `device_type` (`devicetrees/rpi3.dts:66-75`);
  `BCM2837CPU::probe()` matches only `cpu0`
  (`iokit/BCM2837CPU/BCM2837CPU.cpp:48`), hard-codes `phys_id = 0`
  (:130), builds a 1-source/1-CPU `IOCPUInterruptController` (:117) and
  `startCPU()` returns KERN_FAILURE (:299-309).
- **IOCPUInterruptController:** `registerInterrupt()` blocks until
  `enabledCPUs == numCPUs` (`IOCPU.cpp:690-700`); `handleInterrupt()`
  dispatches `vectors[source]` with source = the CPU number
  (:744-760, :640). So with N CPUs: N sources, BCM2837LocalIntc registers
  every source, and an unregistered source on core N just returns
  kIOReturnInvalid.
- **IPIs (DAR-375):** `BCM2837CPU::signalCPU()` only signals itself
  (:416) via core 0 mailbox 0; `BCM2837LocalIntc::handleInterrupt()` reads
  only core 0's pending register (`iokit/BCM2837LocalIntc/
  BCM2837LocalIntc.cpp:273`). For SMP: core N mailbox 0 = write-set
  `0x40000080 + 0x10*N`, clear `0x400000c0 + 0x10*N`, enable
  `0x40000050 + 4*N`, pending `0x40000060 + 4*N` (QA7; QEMU
  `hw/intc/bcm2836_control.c:22-26,93-97`).
- **Reference implementation:** Apple's own `AppleARMSMP.cpp`
  `cpu_boot_thread()` (`iokit/Kernel/arm/AppleARMSMP.cpp:137-196`):
  `ml_processor_register()` per topology CPU, then `processor_start()`;
  `PE_cpu_machine_init()` self-IPIs to clear SIGPdisabled (:229-247).
  `ml_cpu_begin/end_state_transition()` are no-ops without
  USE_APPLEARMSMP (`osfmk/arm/cpu_common.c:866-876`).
- No uniprocessor build shortcut: the config has no `__SMP__`/one-CPU
  option (`config/MASTER.arm64.bcm2837`); spinlocks, broadcast TLBI
  (`osfmk/arm64/tlb.h:69,121`) and `ic ialluis`
  (`osfmk/arm64/caches_asm.s:47`) are already SMP. Kernel mappings are
  outer-shareable write-back (`osfmk/arm64/proc_reg.h:1243-1249`).

## (c) QEMU (qemu-zhuowei 2.12.91)

- raspi3 always has 4 CPUs (`min_cpus = max_cpus = default = 4`,
  `hw/arm/raspi.c` raspi3_machine_init), so `-smp 4` is already implied;
  `-smp 1` is rejected. MPIDR aff0 = core (`hw/arm/bcm2836.c:125`,
  clusterid 0 for bcm2837 :41). Per-core timers are wired to the local
  controller (:151-161) and per-core mailboxes are modelled.
- The parking patch is fork commit `a12a08f`: in `do_cpu_reset()`'s XNU
  branch every core but the first gets `cs->halted = 1` and nothing else
  (`hw/arm/boot.c:663-697`). The Linux path instead writes
  `write_smpboot64()` (the armstub8 spin loop at 0x300 + a zeroed spin
  table at 0xd8, `hw/arm/raspi.c:67-97`) and resets secondaries into it
  (:106-110), but only `if (is_linux)` (`hw/arm/boot.c:1410-1481`).
- Plan: a raspi3 machine property (off by default) that, for an XNU
  kernel, writes the same smpboot64 blob and puts cores 1-3 into it at
  **EL2** (as the Pi firmware does) instead of halting them. The QEMU
  A53 implements EL2/EL3 (`target/arm/cpu64.c:116-117`); CPUECTLR is a
  read-as-zero const (`target/arm/cpu64.c:79-81`).
- MTTCG: guest MO 0 vs aarch64 host -> enabled by default on this arm64
  host (`cpus.c:185-196`). QEMU WFE is a yield, so released-but-unused
  cores spin: only turn the QEMU option on together with the SMP kernel.

## (d) Single-core assumptions / risks

- `BCM2837CPU` (above), `BCM2837LocalIntc` core-0-only pending/mailbox.
- Peripheral IRQs stay routed to core 0 (`GPU routing = 0`), but driver
  workloops/threads will now run on cores 1-3 concurrently with core 0's
  interrupt filters: DWC2, SDHCI, IOBootFramebuffer, SMSC9514 have never
  run with real concurrency. Locks exist but were never tested under
  parallelism.
- Userland: libdispatch width follows `hw.activecpu`; objc/GNUstep
  locking; DAR-409 worker pool.
- Pi only: cache maintenance for data read MMU-off by a secondary,
  armstub version on the card, whether page 0 is intact at the time XNU
  releases the cores.

## Plan (phase 2)

1. **xnu fork, branch `dar-455-smp`:** default `cpus` to 1 on BCM2837
   unless the boot-arg is given (so a 4-CPU DT is harmless by default;
   `cpus=4` turns SMP on - Apple's own boot-arg); add a BCM2837 secondary
   entry in start.s: EL2 -> EL1 (as u-boot's macro does) then
   `b LowResetVectorBase`.
2. **iokit:** DT `cpu1..3` with `device_type = "cpu"`, `reg = <N>`,
   `state = "waiting"` (cpu0 `reg = <0>`); `BCM2837CPU` matches cpuN only
   if the nub has `logical-cpu-id`; IC sized to `num_cpus`; cpuN
   `start()`: `ml_processor_register()` + `processor_start()`;
   `startCPU()`: clean the MMU-off data to PoC, write the trampoline's PA
   to `0xd8 + 8*N` through an uncached mapping, `dsb; sev`; `initCPU()` on
   the core: enable its interrupt, self-IPI; `signalCPU()` to any core via
   its mailbox 0; LocalIntc: per-core pending/clear/enable, all sources.
3. **QEMU fork, branch `dar-455-smp`**, separate worktree + build so the
   shared binary is untouched; runvm.sh gets `QEMU_SMP=1` (adds the
   machine property and `cpus=4`).
4. Separate output: own xnu worktree and own build/out, never the shared
   `build/xnu-latest-build/out/` kernel.

Unknowns: whether caches-off reads need more than the targeted cleans
(Pi only), whether the card's armstub matches master (log page 0 before
release), whether any driver breaks under real concurrency.
