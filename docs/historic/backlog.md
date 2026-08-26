# Backlog — historic (resolved items)

This is a verbatim archive of items from `docs/backlog.md` that were
explicitly marked fixed/done/resolved. See `docs/backlog.md` for the
current open punch list.

The block below is the original document's front-matter/status-update
narrative (predates the P0/P1/P2/P3 item split), preserved verbatim since
it documents real fix history that later items reference. It was removed
from the open file because it discusses only now-resolved items.

---

Open items found but not yet fixed, mostly surfaced by two adversarial
code reviews on 2026-08-13 (one on `BCM2837SDHCI`/the boot chain, one on
the rest of the custom IOKit kexts) plus a few longer-standing known gaps.
Organized by how much it matters, not by discovery order. File:line
references are as of commit `86b1653`; re-check before trusting them if
this list is stale.

Items already fixed *before* this backlog was written (real IRQ delivery
to `BCM2837SDHCI` via the DT interrupt-cell bug, the broken hit-counter,
ACMD41 CRC/index flags, GPIO pinmux WiFi collision, the `pmap.c`
real-hardware panic regression, missing lazy hard-disable in both
interrupt controllers, `SMSC9514Ethernet`'s missing `IONetworkingFamily`
clone, `BCM2837InterruptController` mirror drift) are **not** repeated
here — see `docs/roadmap.md`'s Phase 3 section and
`iokit/BCM2837SDHCI/BCM2837SDHCI.h`'s header comment for that history.

**Status update 2026-08-13 (second pass)**: a dedicated pass through this
list fixed items 1, 2, 3, 8, 9, 12, 13, 14, 16, 17, 26, 27 — each rebuilt
+ boot-tested individually via `_pty_test.py`, no regressions, real
interrupt delivery reconfirmed after every change. Each is marked
`[FIXED 2026-08-13]` in place below with a one-line pointer to what
changed; the original problem description is left intact underneath for
context/history, not deleted. Commits: `third_party/xnu-7195`
`228f4d203`..`8eddfacc2` (pushed to `xnu-pi3-macos11`), main repo
`f2ae010`..`5d2b46f` (not pushed). Items 4, 5, 6, 7, 10, 11 (all
`BCM2837SDHCI` real-hardware-timing/robustness items) were deliberately
**not** attempted this pass — see the note after item 11 for why.

**Status update 2026-08-13 (third pass, final integration testing)**: after
the second pass above plus Phase 3.5a (real `init.c` syscalls) landed
independently, a combined rebuild + repeated boot-test (kernel with all
P0/P1 fixes + shell with real `ls`/`cd`/`pwd`/`cat`/`mkdir`) surfaced two
new, real findings neither individual change's own testing had hit:

- **[MITIGATED, see item 0 below]** A `~40%`-reproducible panic
  (`INTERRUPT_MASKED_DEBUG`'s "Processing of an interrupt ... took N ns"
  watchdog, always flagging `_rtclock_intr`) turned out to be QEMU/TCG
  wall-clock timing jitter, not a guest bug - confirmed via `cpu-factor=10`
  (a real Apple pexpert boot-arg) sharply reducing but not fully
  eliminating the failure rate. Added to `runvm.sh`/`_pty_test.py` (QEMU
  boot paths only, not real hardware).
- **[NEW P0 ITEM 0 - NOT FIXED, root cause identified]** A separate, more
  serious panic — real kernel instruction fetch abort at `pc = 0x0`, `lr`
  inside `sleh_irq` — see item 0 below, added at the top since it's the
  most significant open finding in the whole backlog as of this pass.

New commits this pass: main repo only (`runvm.sh`, `_pty_test.py`
`cpu-factor=10`), not yet pushed pending the adversarial review the user
asked for once this pass was done.

**Status update 2026-08-13 (fourth pass — item 0 FIXED)**: the adversarial
review requested above found the *exact* mechanism behind item 0 via
disassembly (not just theory) and a specific minimal fix, which has now
been applied and verified: see item 0's own entry below, updated in
place. Also corrected two misleading comments the same review caught
(item 3's timer-register write, `IOMediaBSDClient`'s CAS-singleton "leak"
claim) and refreshed `sdcard/`'s kernel copy, which item 1 had left stale
even though it was marked fixed. Re-verified via 6 consecutive full boot
tests: 0 panics (down from 3 crashes across the prior 10 runs), real
interrupt-driven I/O confirmed intact. Commits: `third_party/xnu-7195`
`cb5a5baf7` (pushed), main repo (this commit).

The review also found the "hard kernel wall" conclusion in
`project_xnu_rpi3_userspace_libc_dyld.md` about pthread TSD is **wrong** —
see that memory file's own correction, added the same pass. Not yet acted
on (out of scope for this pass, which was driver/kernel-focused) but
important for whoever picks up Phase 4 `Libc` work next.

---

## P0

## P0, item 0 — [FIXED 2026-08-13, same day] real interrupt handler could be NULL when a genuine hardware IRQ arrived during early boot

**FIXED**: an adversarial review disassembled the actual panic (confirming
`lr` lands exactly at `sleh_irq`'s indirect-call return address) and found
the real arming mechanism the original investigation below hadn't:
`BCM2837LocalIntc::start()` and `BCM2837InterruptController::start()` both
published themselves as usable interrupt controllers
(`getPlatform()->registerInterruptController()`) *before* completing
their own upstream registration - breaking what would otherwise have been
a natural blocking barrier via `IOPlatformExpert::lookUpInterruptController()`
(confirmed unconditionally blocking, never returns NULL). A downstream
client like `BCM2837SDHCI` could find and start arming real hardware
interrupts through a controller that hadn't itself finished becoming
safe. Fixed by reordering both `start()` functions to publish only
*after* their own upstream registration succeeds, and making
`BCM2837CPU::initCPU()`'s three failure paths `panic()` instead of
silently returning (with the reorder in place, a failure there would
otherwise hang the whole interrupt chain forever with no diagnostic).
Re-verified via 6 consecutive full boot tests: 0 panics (down from 3
crashes in the prior 10 runs), real interrupt-driven I/O confirmed intact
throughout. The review also found real-hardware risk is likely *higher*
than the ~1-in-10 QEMU rate suggests: the BCM2835 `Enable_IRQs`
latches are not cleared by a warm reset (only a power cycle), so on real
hardware rebooting out of a previous OS/firmware, arbitrary sources may
already be live the instant this project's own masking runs - a
follow-on hardening (masking both controllers pre-`spllo()`, next to the
existing pre-MMU BCM2837 block in `osfmk/arm64/start.s`) is noted as
still-open, lower-priority hardening, not blocking. Original investigation
kept below for history.

---

**ORIGINAL (superseded by the fix above, kept for history)**

**Found 2026-08-13, during final integration testing** of the combined
P0/P1 fixes + Phase 3.5a real shell syscalls (neither individually
surfaced this — it needed real, sustained interrupt-driven I/O load from
several real filesystem operations in a row, which no single-item test
happened to exercise this heavily).

**Symptom**: `panic(cpu 0 caller ...): Kernel instruction fetch abort at
pc 0x0000000000000000, lr 0xfffffff0073242c8`. `lr` resolves (via `nm` on
the built kernel) to `_sleh_irq + 0x38`. Reading `sleh_irq()`
(`osfmk/arm64/sleh.c`): its non-`USE_APPLEARMSMP` body is
`cdp->interrupt_handler(cdp->interrupt_target, cdp->interrupt_refCon,
cdp->interrupt_nub, cdp->interrupt_source);` — a call through a per-CPU
function pointer. A jump to `pc=0` at exactly that call site means
`cdp->interrupt_handler` was still `NULL` when this ARM64 IRQ exception
fired.

**Root cause (established, not yet fixed)**: `cdp->interrupt_handler` is
only populated by `ml_install_interrupt_handler()`, called from
`IOCPUInterruptController::enableCPUInterrupt()`
(`third_party/xnu-7195/iokit/Kernel/IOCPU.cpp`), which
`BCM2837CPU::initCPU()` calls near the *end* of its setup sequence (after
`ml_processor_register()`). If a genuine hardware IRQ can be signaled to
this core before that point in boot — plausible, since nothing in this
driver stack currently guarantees CPU-level interrupts stay masked
(`DAIF`/`PSTATE.I`) until `enableCPUInterrupt()` has run — `sleh_irq()`
unconditionally calls through the still-NULL pointer. **This exact race
was structurally impossible to hit before today**: prior to the DT
interrupt-cell fix (this same day, see `docs/roadmap.md` Phase 3), no real
hardware interrupt had ever successfully reached this dispatch chain at
all, so the window was never live. Fixing the DT bug made real interrupts
work — and, as a direct consequence, exposed this previously-dormant
early-boot race for the first time.

**Reproduction rate**: hit once in ~10 combined-integration boot-test runs
this pass (separate from, and less frequent than, the timing-watchdog
issue above) — infrequent enough to need real investigation (not just a
retry) but frequent enough to be a real, live bug, not a one-off fluke;
unlike the timing-watchdog panic, this one is a hard crash with a
consistent, specific signature (same faulting PC/LR pattern), not a
symptom that varies run to run.

**Not fixed this pass** — needs real investigation into (a) exactly when
CPU-level interrupt masking is first lifted relative to
`enableCPUInterrupt()` running (grep `DAIFClr`/equivalent in
`osfmk/arm64/start.s` and early boot asm — not yet done), and (b) the
right fix: either delay unmasking until the handler is installed, install
a safe default/no-op handler earlier as a guard, or serialize
`BCM2837SDHCI`/other early-matching drivers' `registerInterrupt()` calls
to strictly happen after `enableCPUInterrupt()` (may already be true in
practice — the actual gap is CPU-level unmasking, not driver
registration order, which is a different, already-partially-addressed
class of race, see items 13/14 below).

---

1. **[FIXED 2026-08-13]** `devicetrees/rpi3-realhw.dts`/`.dtb` regenerated
   from `rpi3.dts` (confirmed the only real difference is `dram-size`),
   recompiled with the correct ADT `dtc`; `sdcard/`'s local (gitignored)
   copy refreshed too. Still genuinely unverified on real hardware (no
   serial cable) - this just means a real attempt is no longer silently
   doomed by a stale image. Original problem description below, kept for
   context.

   **`sdcard/`'s real-hardware image is stale and would boot none of this
   session's fixes.** `sdcard/rpi3.dtb` is a copy of `devicetrees/rpi3-realhw.dtb`,
   which is missing the `arm-io` `ranges` fix, `local-intc`'s
   `AAPL,phandle`/`interrupt-controller` marker, and `intc`'s
   `interrupt-parent` wiring that `devicetrees/rpi3.dts` (the QEMU-verified
   tree) already has — each independently documented in `rpi3.dts` as
   confirmed-fatal without it. `sdcard/kernel.development.bcm2837.unstripped`
   predates the driver work entirely. Regenerate `rpi3-realhw.dts` from
   `rpi3.dts` (the two should only differ in `dram-size`), recompile with
   `third_party/dtc-AppleDeviceTree/dtc` (**not** stock `dtc`), and copy the
   current kernel build into `sdcard/` before ever attempting a real boot.

2. **[FIXED 2026-08-13]** Latches (not `IOLog` - real interrupt context)
   the first occurrence of an unexpected pending bit into
   `fLastUnexpectedPending`, inspectable via debugger/coredump. Does NOT
   implement actual mailbox/local-timer source handling or masking (no
   safe non-interrupt-context hook exists in this class to log from more
   visibly) - converts total silence into *something* discoverable, not a
   full fix. Original problem description below, kept for context.

   **`BCM2837LocalIntc::handleInterrupt()` silently drops every pending bit
   except bit 8 (GPU IRQ).** `iokit/BCM2837LocalIntc/BCM2837LocalIntc.cpp`
   (`handleInterrupt`) reads `LOCAL_IRQ_PENDING0` once and only services the
   GPU passthrough bit. If firmware ever leaves a mailbox or local-timer
   source routed to this core (plausible on real hardware, structurally
   impossible in QEMU since `bcm2836_control_reset()` zeroes those there),
   the pending bit is never cleared, the CPU IRQ line stays asserted, and
   `sleh_irq()` re-enters forever — a silent hang with no panic.

3. **[FIXED 2026-08-13]** Investigated first, per this item's own framing:
   read the register live via the QEMU monitor post-boot, found it
   already `0xF0` (correctly FIQ-enabled) despite zero writers anywhere
   in this repo *or* in `u-boot`'s `bootxnu.c` (real hardware's actual
   boot chain) - QEMU's loader was silently providing this, real firmware
   won't. `BCM2837LocalIntc::start()` now writes it explicitly. Original
   problem description below, kept for context.

   **Nothing in this repo programs QA7's `LOCAL_TIMER_INT_CONTROL0`
   (`0x40000040`).** Repo-wide grep for that address / `timercontrol`
   finds zero writers — not the kexts, not `pexpert/`, not `osfmk/arm64/`.
   XNU arm64 takes the scheduler tick via **FIQ** (`sleh_fiq()`), not IRQ,
   and the generic timer's FIQ output is gated by this exact register in
   both QEMU and real hardware. Worth a runtime check (read back
   `0x40000040`, or confirm via QEMU monitor that `sleh_fiq` is ever
   entered) before assuming the "steady-state idle" boot claims generalize
   to real hardware — if this needs a write and doesn't have one, a real
   Pi would likely hang shortly after console init with no diagnostic.

8. **[FIXED 2026-08-13]** `resetHost()` now writes `0x0E` (the field's
   maximum, ~1s at typical base clocks). Original problem description
   below, kept for context.

   **`kRegTimeoutControl` (`0x2E`) is declared and never written.** The
   data-timeout counter is left at its reset (shortest) value. QEMU
   doesn't implement `DATA_TIMEOUT_ERR` at all, so a real card that's
   merely slow could spuriously fail where QEMU never would.

9. **[FIXED 2026-08-13]** `BCM2837InterruptController::start()` now writes
   `FIQ_CONTROL = 0` explicitly. Original problem description below, kept
   for context.

   **`kRegFIQControl` (intc `+0x0C`) is declared, never written.**
   `BCM2837InterruptController::start()` masks all IRQs but leaves
   `FIQ_CONTROL` at whatever firmware left it. QEMU resets it to 0; real
   firmware may not. Any source left FIQ-routed arrives at `sleh_fiq()`
   and gets misclassified as a timer tick.

10. **[FIXED 2026-08-25]** `BCM2837DWC2::initDMABuffer()`'s
    `IOBufferMemoryDescriptor` now uses `kIOMapWriteCombineCache`. Original
    problem description below, kept for context.

    **Real Pi 3 boot panic, first hit running `ifconfig en0 inet ...
    netmask ...` against the real LAN9514/SMSC9514 chip**: `Unaligned
    kernel data abort` (ESR `0x96000021`, DFSC = alignment fault) inside
    `_memmove`, called from `BCM2837DWC2::doTransfer()`'s
    `memcpy((uint8_t *)buffer + done, fDMAVirt, actual)` on
    `SMSC9514Ethernet`'s RX poll path (`rxTimerFired` ->
    `pollReceive` -> `SMSC9514USBTransport::bulkIn` -> `transfer` ->
    `doTransfer`). Root cause, confirmed against this project's own
    `third_party/xnu-7195/osfmk/arm/pmap.c` (`wimg_to_pte()`) and
    `osfmk/arm64/proc_reg.h`, not guessed: the DMA staging buffer was
    allocated with `kIOMapInhibitCache`, which maps `VM_WIMG_IO` ->
    `MAIR_DISABLE` (`0x00`, real Device-nGnRnE memory) — and Device memory
    unconditionally faults on any unaligned multi-byte access, regardless
    of `SCTLR.A` (confirmed off in this project's `SCTLR_EL1_DEFAULT`).
    `doTransfer()`'s `memcpy()` in/out of this buffer runs at whatever
    length a real Ethernet frame happens to be, essentially never aligned.
    The buffer's own header comment already claimed it was "uncached", but
    `kIOMapInhibitCache` is not the ARM64 option that actually means
    that — `kIOMapWriteCombineCache` (`VM_WIMG_WCOMB` -> `MAIR_WRITECOMB`,
    `0x44`, real Normal Non-Cacheable memory) is, and Normal memory
    permits unaligned access under this project's SCTLR config, matching
    the standard idiom real Linux/U-Boot DMA-coherent buffers use for
    exactly this reason. **Not yet re-verified on real hardware** —
    `third_party/qemu-zhuowei` never emulates the real LAN9514 (see
    project memory "QEMU never emulates LAN9514"; QEMU's `usb-net` stands
    in as generic CDC/RNDIS, and `SMSC9514Ethernet` correctly never
    matches it, so `en0` doesn't exist under QEMU at all) — only rebuilt,
    QEMU-boot-regression-tested (no panic, login/shell/other `ifconfig`
    paths unaffected), and confirmed via direct architecture-manual +
    in-tree pmap source reading, not by reproducing the exact crash.
    Also fixed in passing: `tools/boot_test.py`'s `PROMPT` constant was
    stale (`"% "`, the old custom init-shell's prompt, silently never
    matching real bash's `"-bash-3.2# "` and making every `--cmd` a
    no-op) and it had no login-prompt handling at all, so it could never
    actually drive a command on current images — added `--login`/`--user`/
    `--password` (defaults `root`/`darwin`, matching
    `inject_into_sd_image.sh`'s provisioned account) and fixed `PROMPT`.

11. **[FIXED 2026-08-25, same day as item 10]** Added `fTransferLock`
    (`IOLock`) to `BCM2837DWC2`, held around the whole body of
    `transfer()` and `controlTransfer()`. Original problem description
    below, kept for context.

    **`BCM2837DWC2`'s single shared USB host channel + DMA staging buffer
    had zero serialization between callers**, despite this file's own
    header comment already flagging it ("Revisit when Ethernet RX wants
    to overlap with TX") from before RX existed. Once item 10 above made
    `SMSC9514Ethernet`'s RX poll timer real, that revisit became live:
    `fRxTimer` calls `transfer()` from `SMSC9514Ethernet`'s `IOWorkLoop`
    timer callout while `outputPacket()` (TX) calls it synchronously from
    whatever arbitrary thread `ifnet_output()` runs on — confirmed by
    reading `IONetworkInterface::if_output()`
    (`third_party/IONetworkingFamily/IONetworkInterface.cpp`), which
    calls the controller's output action directly with no queue at all
    when `createOutputQueue()` returns `NULL` (the `IONetworkController`
    default, never overridden here — and this project's vendored
    `IONetworkingFamily` has no `IOBasicOutputQueue.cpp`/
    `IOGatedOutputQueue.cpp` to override it *with*, only the headers).
    Real-hardware symptom this caused: `ping` reporting
    `sendto: Operation not permitted` on roughly every other echo request
    while every ICMP sequence number still got a real reply with no
    gaps — i.e. the transfer really reached the wire, but
    `doTransfer()`'s own HCINT-based completion readback got corrupted by
    the concurrently-running RX poll reprogramming the same channel
    mid-poll. Diagnosed from source (no lock existed, confirmed via grep;
    both call paths confirmed via direct reads; the real xnu-7195 network
    stack has no synchronous per-packet `EPERM` path that fits an
    always-recovers, every-other-packet pattern) and QEMU-boot-regression-
    tested (`BCM2837USBEnum`'s boot-time enumeration issues many
    sequential `controlTransfer()` calls through the new lock with no
    hang/deadlock) — **not yet real-hardware-confirmed** the actual `ping`
    symptom is gone, since QEMU cannot exercise `SMSC9514Ethernet` at all
    (see item 10).

---

## P1

12. **[FIXED 2026-08-13]** `IOMediaBSDClientGlobals_getInstance()` now uses
    a lock-free CAS singleton (`OSCompareAndSwapPtr`) instead of a bare
    check-then-assign. Verified: all 3 `IOMediaBSDClient` instances
    (disk0, disk0s1, disk0s2) still match correctly, root device still
    resolves the same as before. Original problem description below, kept
    for context.

    **`IOMediaBSDClient`'s lazy-singleton globals have an unguarded
    first-use race.** `third_party/xnu-7195/iokit/Storage/IOMediaBSDClient.cpp`
    (`IOMediaBSDClientGlobals_getInstance()`) does a bare
    `if (ptr == NULL) ptr = new ...` with no lock. The code comment already
    concedes the gap but rationalizes it as "expected to happen serially" —
    no longer true since `IOFDiskPartitionScheme` now produces 3 near-
    simultaneous `IOMedia` nubs (whole disk + 2 partitions), each spawning
    an `IOMediaBSDClient::init()` on a potentially different config thread.
    Two racing constructions means two major numbers / split minor tables —
    nodes from the losing instance are silently orphaned. Intermittent,
    would look exactly like "root device sometimes not found." Fix: make
    the allocation happen from a known-serial point, or guard with a lock.

13. **[FIXED 2026-08-13]** Added `probe()` re-checking `provider->getName()
    == "cpu0"`, matching every other DT-matched driver. Original problem
    description below, kept for context.

    **`BCM2837CPU` has no defensive `probe()` name re-check.** Every other
    DT-matched driver in this tree (`BCM2837ArmIO`, `BCM2837InterruptController`,
    `BCM2837LocalIntc`, `BCM2837SDHCI`) re-verifies `provider->getName()`
    in `probe()` as a defensive guard; `BCM2837CPU` doesn't. If it were
    ever to over-match (see item 20 for why the originally-cited reason for
    these guards was probably a misdiagnosis, but the guards themselves are
    still good practice), duplicate instantiation would drive
    `enabledCPUs` past `numCPUs`, permanently wedging
    `IOCPUInterruptController::registerInterrupt()`'s `thread_block()` wait
    for every future caller. Cheap to add for consistency alone.

14. **[FIXED 2026-08-13]** Reordered `BCM2837CPU::initCPU()` so
    `registerCPUInterruptController()` runs after `enableCPUInterrupt()`.
    Investigated while fixing: `IOPlatformExpert::lookUpInterruptController()`
    is unconditionally *blocking* (loops on `IOLockSleep()`, never returns
    NULL) - this item's original framing (a lookup finding NULL) doesn't
    match the real API contract, so the "race" mostly self-healed via
    `IOCPUInterruptController::registerInterrupt()`'s own blocking wait
    even before this fix; still a real hardening (removes an unnecessary
    block-then-wake cycle). Verified via 3 boot-test runs: 2/3 clean with
    real interrupt delivery confirmed, 1/3 stalled matching a pre-existing
    documented flake pattern at the same point under the *original* code -
    not attributed to this change (see the commit for the structural
    reasoning), but noted in case it recurs. Original problem description
    below, kept for context.

    **Ordering race: `BCM2837LocalIntc::start()` can block indefinitely** if
    it runs after `BCM2837CPU::initCPU()`'s `registerCPUInterruptController()`
    call but before its later `enableCPUInterrupt()` call.
    `lookUpInterruptController()` succeeds in that window while
    `enabledCPUs == 0`, and `IOCPUInterruptController::registerInterrupt()`
    (`third_party/xnu-7195/iokit/Kernel/IOCPU.cpp`) blocks on
    `thread_block()` until the later wakeup. `BCM2837CPU.h` acknowledges
    matching-order isn't strictly guaranteed but only reasons about
    `cpusRunning()`, not this specific window. Fix: reorder `initCPU()` so
    `registerCPUInterruptController()` happens after `enableCPUInterrupt()`.

16. **[FIXED 2026-08-13]** `doAsyncReadWrite()` now rejects `nblks==0`,
    `nblks>=65536`, and `block+nblks > fBlockCount` up front, before the
    bounce-buffer allocation. Original problem description below, kept
    for context.

    **`BCM2837SDHCI::doAsyncReadWrite()`/PIO paths truncate `nblks` with no
    bounds check.** `writeReg16(kRegBlockCount, (uint16_t)nblks)` silently
    truncates requests ≥65536 blocks (32MB); nothing validates
    `block + nblks <= fBlockCount`, and no `kIOMaximumBlockCountReadKey` is
    advertised. Unlikely to be hit today, silent corruption if it is.

17. **[FIXED 2026-08-13]** Cached in a new `fCPUName` member, released in
    a new `free()` override. Original problem description below, kept for
    context.

    **`BCM2837CPU::getCPUName()` leaks an `OSSymbol` on every call** —
    `OSSymbol::withCString("cpu0")` returns a retained ref the caller
    doesn't release. Called rarely; cache it in a member instead.

---

## P2

30. **[DONE 2026-08-16]** Real, unmodified Apple TZif2 zoneinfo files
    (`UTC`/`GMT`, copied from this host Mac's own `/usr/share/zoneinfo/`)
    now provisioned onto the HFS+ root at `/var/db/timezone/zoneinfo/`
    by `inject_into_sd_image.sh`; `touch` now links the real
    `Libc/stdtime/FreeBSD/localtime.c` (`simple_cmds_mktime_compat.c`
    deleted). QEMU-verified: `touch -t` exits 0 through the real
    `tzload()` code path (confirmed real SD block I/O reading the
    provisioned file). Full writeup: `docs/roadmap.md`'s Phase 4.6. Real
    `TZ` variable support still needs item 31 below (`getenv()` stub).

31. **[FIXED 2026-08-16]** Real, non-empty `envp` now built by `init.c`'s
    `cmd_exec()` and correctly read via a real `getenv()`
    (`simple_cmds_getenv_compat.c`). Found a real, previously-latent
    third bug along the way: `third_party/Csu/start.s`'s arm64 entry
    point calls `main()` directly, bypassing `crt.c`'s own `_start()`
    (the function that sets `environ`) entirely -- that function is
    real, documented dead code without dyld (which this project doesn't
    have). Fixed with one real store instruction in `start.s`. `which`/
    `env` both built and QEMU-verified as the capstone. Full writeup:
    `docs/roadmap.md`'s Phase 4.6. Original problem description below,
    kept for context.

    **No real environment-variable plumbing.** `getenv()` always returns
    `NULL`; `init.c`'s `cmd_exec()` always execs children with an empty
    `envp`. Blocks `which`/`whereis` (both exit immediately on
    `getenv("PATH") == NULL`, confirmed real/honest behavior, not a
    crash), `env`(1), and `TZ`. Two real halves needed (PID-1-side
    variable table + `envp` construction, then `getenv()`/`environ`
    read-side) — see `docs/roadmap.md`'s Phase 4.6 for the full split.

---

## P3

26. **[FIXED 2026-08-13]** Added `vectorInRange()` bounds check to
    `BCM2837InterruptController::enableVector()`/`disableVectorHard()`,
    and `fRegMap->getLength()` validation to both controllers' `start()`.
    Original problem description below, kept for context.

    **Missing bounds/length validation in both interrupt controllers**:
    `registerInterrupt()`/`enableVector()`/`disableVectorHard()` never
    range-check `vectorNumber` against `kNumVectors` (a malformed DT gives
    an OOB `&vectors[n]` write), and neither controller validates
    `fRegMap->getLength()` before indexing straight to a fixed offset.

27. **[FIXED 2026-08-13]** Both controllers' `free()` now releases
    `fRegMap` if still set (covers the start()-fails-after-mapping path
    `stop()` never runs for). Original problem description below, kept
    for context.

    **Resource leak on `start()` failure path** in both interrupt
    controllers: if `start()` fails after `fRegMap` is set, `stop()` is
    never called (IOKit convention) and `free()` doesn't release `fRegMap`
    either. Move the release into `free()`.
