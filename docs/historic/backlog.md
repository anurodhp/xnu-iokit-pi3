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

## P0, items 1/2/4/5 — [FIXED 2026-08-27, QEMU-verified, commit `d472f37`] full fix writeup archived

All four rebuilt together (`tools/build_kexts.sh` clean compile, then a
full `tools/build_xnu_7195.sh` kernel rebuild) and verified with one real
QEMU boot (`tools/boot_test.py --login --cmd 'ls /' --cmd 'cat /etc/hosts'
--cmd 'ls /System'`, `--expect-fail 'panic\('`) — real boot, real login,
real reads against both mounted partitions (`disk0s1` root / `disk0s2`
boot), zero panics, `BCM2837SDHCI` log confirms a real hardware interrupt
still observed and the card still identifies correctly (RCA=0x4567, SDHC,
1200MB). This confirms no regression from any of the four changes
together; it does not and cannot exercise the specific fault conditions
items 1/2/5 exist to guard against (concurrent I/O, a real card/bus
error, or a real MMIO-ordering violation), since QEMU reproduces none of
those.

**Item 1** — `BCM2837SDHCI::doAsyncReadWrite()` had no serialization at
all — no `IOCommandGate`, no workloop, no mutex. Two concurrent requests
(now plausible: the SD image has two mounted partitions, `disk0s1` +
`disk0s2`) could interleave register writes and both consume from the
same `fPendingIntStatus`, causing one to eat the other's completion —
silent data corruption or a hang, not a clean error. Re-confirmed still
present exactly as described before fixing.

Fix: added `fTransferLock` (a plain `IOLock`), held for the whole
command-issue-through-completion span of `doAsyncReadWrite()`. Not the
full `IOCommandGate`/workloop integration this item originally asked for
(that remains the architecturally "fuller" answer) — but this driver's
`doAsyncReadWrite()` is already synchronous under the hood (it blocks the
calling thread on `waitForIntStatus32()` rather than completing from
interrupt context), so a plain mutex around that already-synchronous span
is a real, sufficient critical section: it guarantees only one thread
ever touches the command/transfer registers or `fPendingIntStatus`'s
producer side at a time, which is exactly the corruption this item
described. See `fTransferLock`'s own header comment
(`iokit/BCM2837SDHCI/BCM2837SDHCI.h`) for the full reasoning.
Verification caveat: nothing in this project currently issues genuinely
concurrent I/O (same limitation the original filing noted), so the QEMU
boot test confirms no regression — real boot, real login, real reads
against both partitions — but does not and cannot exercise the actual
interleaving race the fix closes, since no current workload drives
concurrent requests to begin with.

**Item 2** — no error recovery anywhere in `BCM2837SDHCI` — every error
path was `IOLog(); return false;`. Re-confirmed still true before fixing:
no `SOFTWARE_RESET_CMD`/`SOFTWARE_RESET_DAT`/`CMD12` anywhere in the
file.

Fix: added `resetCmdLine()` (SDHCI spec 3.7.1 `SOFTWARE_RESET_CMD`,
`kRegSoftwareReset` bit1) and `resetDatLine()` (`CMD12`
STOP_TRANSMISSION best-effort, then `SOFTWARE_RESET_DAT` bit2), wired
into every real error/timeout path: `sendCommand()`'s CMD_INHIBIT/
DAT_INHIBIT-stuck checks, its CMD_COMPLETE timeout and error-status
paths, and `readBlocksPIO()`/`writeBlocksPIO()`'s buffer-ready-timeout,
error-status, and TRANSFER_COMPLETE-timeout/error paths — matching
Linux's `sdhci_reset(host, SDHCI_RESET_CMD/_DATA)` split. QEMU's SD model
still doesn't inject real CRC/timeout/card-removal errors (per this
item's own original note), so the QEMU boot test confirms the reset
paths compile and don't regress a clean boot, not that they trigger
correctly under a real error (no current QEMU-reachable workload can
force one).

**Item 4** — no power-on ramp / 74-clock delay before `CMD0`, and power
was enabled *after* the clock in `start()` rather than before.
Re-confirmed still true before fixing (power write was after
`setClock(400000)`).

Fix: reordered `start()` to assert `kRegPowerControl` (3.3V +
`SD_BUS_POWER`) *before* `setClock()`, added an `IODelay(1000)` (≥1ms,
spec 6.4.1) between power-on and clock-start, and an `IODelay(250)`
(≥74 clocks at the 400kHz identification clock, ~185us, rounded up)
between clock-start and the first command (`cardIdentify()`). This only
changes *ordering and idle delays*, not any actual signal rate/timing —
the same class of change item 3 declined to make — so unlike item 3
there's no way for a wrong guess here to make things worse on real
hardware (a busy-wait can only be too short or comfortably long, never
"wrong speed"). QEMU has no power/clock-dependent reset behavior to
catch the old ordering being wrong (which is exactly why it went
unnoticed originally), so this is verified only as "boots clean, no
regression."

**Item 5** — code correct but genuinely unverifiable from QEMU, exactly
as this item originally predicted. Re-confirmed the core claim (no
barrier at the specific location this item calls out) still held before
fixing. One correction found while re-checking it: the broader claim
that `grep -rn "dsb\|dmb\|isb\|OSSynchronizeIO"` returns nothing
project-wide except `OSMemoryBarrier()` was stale — `iokit/BCM2837DWC2/
BCM2837DWC2.cpp` (added 2026-08-21, after this item was first filed)
already has two real `dsb sy` barriers around its DMA buffer/HCDMA
programming. The specific gap this item flagged — no barrier in
`BCM2837InterruptController::enableVector()`/`disableVectorHard()`
before the next `IRQ_PENDING` read — was still real and unfixed.

Fix: added `__asm__ volatile ("dsb sy" ::: "memory")` after each of
`enableVector()`'s and `disableVectorHard()`'s register writes, matching
the barrier style already used in `BCM2837DWC2.cpp`. As predicted, this
is genuinely unverifiable from QEMU/TCG (all MMIO is strongly ordered
there regardless of the barrier's presence) — the QEMU boot test only
confirms the added instruction doesn't itself break anything, not that
it fixes a real ordering bug on silicon.

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

## P1, SMSC9514Ethernet — resolved sub-items archived 2026-08-27

These were inline strikethrough bullets under `docs/backlog.md`'s P1 item
2 (`SMSC9514Ethernet has several real logic bugs`). The parent item
stays open there (blind register-write RMW gap, `frameLen` truncation,
and the 2026-08-25 real-hardware RX-dead report are still live) —
archived here just to keep it from re-growing to its full multi-session
history inline every time a new sub-item resolves.

- **FIXED 2026-08-22**: the PHY-reset loop didn't check its own timeout
  flag before continuing as if it succeeded. `resetChip()` now has a
  `phyCleared` flag matching the lite-reset loop's own pattern,
  logs+fails instead of continuing on timeout.
- **FIXED 2026-08-22**: `mdioRead`/`mdioWrite` were never called from
  anywhere — the PHY was never initialized/auto-negotiated, yet
  `enable()` unconditionally set `fLinkUp = true`. `resetChip()` now
  does a real BMCR auto-negotiation restart, and `updateLinkStatus()`
  (polled via a real `IOTimerEventSource`, started/stopped in
  `enable()`/`disable()`) reads BMSR/ANAR/ANLPAR and reports real link
  state + negotiated speed/duplex through `setLinkStatus()`.
- **FIXED 2026-08-21 + 2026-08-22**: `attachInterface()` was never
  called from `start()`, so no `IONetworkInterface` could ever be
  created. Fixed by calling it in `start()`, then by having
  `BCM2837DWC2::start()` manually construct/attach/start the class on
  real hardware and wire its `IONetworkInterface` to BSD/`ifconfig` via
  a hand-instantiated `IONetworkStack` (`iokit/BCM2837DWC2/
  BCM2837DWC2.cpp`).
- **RESOLVED 2026-08-25**: `getPacketFilters()` advertised multicast
  filtering while `enablePacketFilter()` was a no-op — would've broken
  IPv6 ND. Root cause was one layer up: `IOEthernetController`'s own
  base-class `enablePacketFilter()`/`disablePacketFilter()` already
  correctly dispatch `kIOPacketFilterMulticast`/`kIOPacketFilterPromiscuous`
  to the virtual hooks `setMulticastMode()`/`setPromiscuousMode()` —
  this driver's own no-op override was *shadowing* that real logic
  entirely, not failing to extend it. Fix removed the shadowing
  overrides and implemented `setMulticastMode()`/`setPromiscuousMode()`
  (real MAC_CR MCPAS/PRMS read-modify-write) and `setMulticastList()`
  (real success — MCPAS already passes all multicast whenever the
  filter is active). Also fixed `getPacketFilters()` ignoring `group`,
  which falsely advertised WOL filter support. Build-verified; unverified
  on real hardware at the time.
- **RESOLVED 2026-08-25**: `fInterface` was read without a retain/lock
  from a USB-completion context that could race `disable()` setting it
  to NULL — a UAF shape, though moot in practice since nothing drove
  the bulk IN endpoint yet. Once RX was wired (`fRxTimer`/
  `pollReceive()`), turned out not to be a real race: RX runs from an
  `IOTimerEventSource` on the *same gated `IOWorkLoop`* as
  `outputPacket()`'s output queue and `disable()`'s framework calls —
  there is no independent USB-completion thread anywhere in
  `BCM2837DWC2` — so all of these are already serialized against each
  other.
- **RESOLVED 2026-08-22**: real hardware confirmed `en0` attaches, shows
  link up in `ifconfig`, and responds to ICMP echo (ping) over a real
  Ethernet link. BSD/`ifconfig` registration and link-state/auto-
  negotiation logic verified end-to-end on silicon, not just build+QEMU.
  No DHCP/address assignment yet — that was Phase 6c.
- **2026-08-25, real-hardware report**: user reported real Pi 3
  networking not working — Ethernet activity LEDs never lit, MAC
  address looked like a placeholder, inbound traffic never worked at
  all (self-ping only, despite outbound frames visibly reaching the
  wire). Root-caused: `inputPacketAvailable()` was real but **nothing
  ever called it** — no code drove the bulk IN endpoint, so TX worked
  and RX was completely dead. Fixed same day: RX poll timer wired up
  (`fRxTimer`/`pollReceive()`) and MAC address now attempted via a real
  EEPROM read before falling back to the locally-administered
  placeholder. Board LEDs explicitly out of scope per user. Full kernel
  build verified; not yet booted on real hardware at the time.

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

## P2, item d6 (launchd) — full session narrative archived 2026-08-27

`docs/backlog.md`'s current P2 item d6 condenses this down to current
state only (109 undefined symbols, categorized). This is the full,
verbatim blow-by-blow of how it got there, preserved for the real
detective work it documents.

d6. **CLOSED 2026-08-26 — every real file in `launchd/` now compiles
    clean.** `tools/userland_staging/build_launchd_recon.sh` (real
    `-fsyntax-only` reconnaissance harness, not yet a real build script
    that produces `.o`/link output) is the reproducer — see its own
    header comment for the complete real-fix writeup. All 8 real daemon
    source files (`pd_launchd_boot.c`, `kill2.c`, `pd_sandbox_check.c`,
    `pd_console_login.c`, `pd_launchd_plist.c`, `log.c`, `ipc.c`,
    `launchd.c`) plus `core.c` (12,162 real lines, launchd's actual
    job-management engine) compile with zero errors. Real fixes:
    `spawn_private.h`/`libinfo.h` include-path gaps; the `union
    sockaddr_in_4_6`/`struct sockaddr_dl` incomplete-type errors (same
    "SDK's own same-named older header wins" bug class as
    `mach/mach_vm.h`/`mach/mach_time.h`); `third_party/Libinfo`'s real
    `lookup.subproj/aliasdb.h` (a separately-pinned repo, reused for a
    header PureDarwin's own bundled libinfo/ is missing); a real,
    embedded-only `LAUNCH_JOBKEY_DEFAULTS` macro missing from the
    vendored `launch.h`; a real `sandbox_spawnattrs` compat extension
    matching this project's own already-established "always allow, no
    Sandbox.kext" precedent; and — the interesting one — a real,
    from-scratch `ld2xpc()`/`xpc2ld()` (`tools/userland_staging/
    ld2xpc_compat.c`), CONFIRMED via a fresh clone of the real,
    historically-open-sourced `apple-oss-distributions/launchd` tag
    `launchd-842.92.1` (the last launchd Apple ever published source
    for) — not just this project's own PureDarwin checkout — that these
    two `launch_data_t`<->`xpc_object_t` converters are called but never
    defined in EITHER real repository; they must come from a private,
    closed-source part of Apple's real libxpc neither tree ships. Real,
    from-scratch recursive implementation (last resort, only after
    confirming no real Apple source exists anywhere), mapping between
    the two real, already-working type systems.

    **UPDATE, same day**: `build_launchd_core.sh` (new) promoted the
    recon harness into a real build script — compiles all 9 real daemon
    files (`runtime.c`, 1512 lines, was mistakenly omitted from the
    first pass — real fix needed too: this project's pinned SDK's own
    `<bsm/libbsm.h>` marks `audit_token_to_au32()`
    `API_UNAVAILABLE(ios,...)` at the resolved-symbol level, a real hard
    error neither `__asm` nor C-identifier renaming can bypass —
    resolved via a redirect header, `launchd_compat_headers/bsm/
    libbsm.h`, pointing at the real, unrestricted upstream BSM repo's
    own copy instead) plus all 7 real SERVER-side MIG files
    (`jobServer.c` etc, `job_forwardServer.c` needing one extra
    `-include core.h`) into `liblaunchd_core.a` (20 objects).
    `build_launchd_syscalls.sh` (new) then closed a large real BSD-
    syscall/libc gap (`chown`/`fork`/`posix_spawn`/`wait4`/`uuid_clear`/
    `getopt`/etc, ~35 real functions) into `liblaunchd_syscalls.a` (46
    objects). A real, honest trial link (`-Wl,-force_load` — a plain
    link against `launchd.c`'s own real `main()` was found to not
    actually pull in `core.c`/`ipc.c` at all, since `main()` only
    reaches them indirectly through dispatch/XPC handlers, the same
    "empty main proves nothing" lesson recurring in a subtler form) is
    down to **~110 genuinely-undefined-anywhere symbols, from 266** at
    the start of this pass. Remaining, categorized (none fixed yet):
    - **CoreFoundation** (~14 symbols: `CFArrayGetCount`,
      `CFPropertyListCreateFromXMLData`, `CFDictionaryApplyFunction`,
      etc) — the same real structural wall already documented for
      IOKitUser (`project_xnu_rpi3_iokituser_cf_wall` in memory): no CF
      port exists on this target. `pd_launchd_plist.c` itself already
      avoids CF (compiles clean), but `core.c`'s own real plist-parsing
      path uses it directly.
    - **pthread QoS/attr API** (~20 symbols) — already tracked as this
      section's own item **d3**, unchanged by this pass.
    - **Mach voucher / host-special-port routines** (~10 symbols:
      `host_reboot`, `host_set_special_port`, `mach_voucher_attr_command`,
      etc) — already tracked as item **d5**.
    - **BSM audit-record construction** (`au_open`/`au_write`/`au_to_*`,
      ~10 symbols) — real source exists (`third_party/BSM/bsm/lib/
      bsm_audit.c`/`bsm_token.c`) but needs a real private header
      (`struct au_token`/`au_record_t`/`AU_ARG64_TOKEN`/`ADD_U_CHAR`
      etc) this project's pinned BSM repo doesn't ship — only `xnu-7195`'s
      kernel-side `bsd/bsm/audit_internal.h` came close, and pulling
      that in surfaced further real gaps (`HEADER_SIZE`/`TRAILER_SIZE`/
      `be32enc`/`be64enc`). Deprioritized as a peripheral, non-critical
      logging feature, not core job-management.
    - **`os_log`/`__mach_errors`** — already-documented, genuinely
      unbuildable gaps (Phase 6.8's own libdispatch/libxpc sections).
    - **A long tail of smaller items**: `job_mig_look_up2_forward`/
      `job_mig_parent_forward` (real functions, not yet located —
      possibly a genuinely-unimplemented job-forwarding feature in this
      exact PureDarwin branch, same class of gap as `ld2xpc`/`xpc2ld`,
      not yet checked against the real `launchd-842.92.1` clone the way
      those were); `runtime_ktrace`/`runtime_ktrace0` (called by
      `runtime.c` itself but still undefined — likely gated behind a
      macro this project doesn't define, not yet root-caused); `exec.c`/
      `signal.c` (need a real internal "private name" alias convention,
      e.g. `_execve` distinct from public `execve`, not yet ported);
      `waitpid.c` (a real K&R-style declaration trips this project's
      clang flags); `atexit.c`/`glob.c` (each need one more real private
      header not yet cross-referenced); `initgroups` (`NO_SYSCALL_STUB`,
      no real dedicated wrapper source located yet).

    **Concrete next steps, in priority order**: (1) ~~decide whether to
    invest in a real CF port now~~ — DONE, see item **d7**; (2) close the
    pthread QoS/attr gap (item d3) since it's shared with the still-open
    `libdispatch.dylib` gap too, so fixing it once helps both; (3) the
    smaller one-off items above are all real, bounded, and individually
    quick once picked up.

    **UPDATE 2026-08-26: CoreFoundation wired in for real, verified via
    the actual launchd trial link (not a synthetic CF-only probe).**
    First checked the real question this raised: does launchd's actual
    CF usage need ICU? Checked `pd_launchd_plist.c` (the only real
    launchd file that touches CF) line by line — its full CF surface is
    `CFGetTypeID`/`CFStringGetTypeID`/`CFStringGetCString`/
    `CFDictionaryGetTypeID`/`ApplyFunction`/`CFArrayGetTypeID`/
    `GetCount`/`GetValueAtIndex`/`CFBooleanGetTypeID`/`GetValue`/
    `CFNumberGetTypeID`/`GetValue`/`CFPropertyListCreateFromXMLData`/
    `CFRelease`/`kCFAllocatorNull`. **None of it touches CFLocale,
    CFCharacterSet, collation, or CFNumberFormatter** — the only real
    ICU-dependent parts of this CF port (item d7). Also corrected a
    stale assumption in `build_corefoundation.sh`'s own scope comment:
    `CFPropertyListCreateFromXMLData` was documented as "explicitly not
    included," but it already works — its real XML parser
    (`_CFPropertyListCreateFromUTF8Data`) is self-contained inside
    `CFPropertyList.c` itself, no separate `CFXMLTree`/libxml2
    dependency needed. **So: no, ICU is not on launchd's critical path.**
    One real, non-ICU caveat: `CFStringGetCString`'s fast path (a plain
    `memmove`) only covers strings already stored 8-bit/ASCII-compatible
    internally; non-ASCII plist string values would hit the still-
    missing `__CFStringEncodeByteStream` slow path. Real launchd plist
    content (labels, paths, program arguments) is essentially always
    ASCII in practice, so this doesn't block the realistic case, but
    it's a real, tracked gap (see item d7), not eliminated.

    Re-ran `build_launchd_core.sh`'s own real trial link (now updated to
    a real DYNAMIC link — dropped `-static`, dropped `libdispatch_real.a`
    for `libdispatch.dylib`, same reason as item d7's own harness fix —
    and wired in `libCoreFoundation.a` + `cf_libc_extras.a`). Also added
    a new, permanent `launchd_libc_extras.a` selective-extraction step
    (same pattern as d7's `cf_libc_extras.a`) closing 15 more real,
    already-built-elsewhere BSD libc symbols: `chdir`/`access`/`strstr`/
    `sleep`/`sigprocmask`/`sigaction`/`getifaddrs`/`freeifaddrs`/
    `arc4random_uniform`/`bind`/`getentropy`/`uuid_copy`/`uuid_is_null`/
    `fegetenv`/`fesetenv`/`fstatat`. (`ctime_r`/`localtime_r` via
    `libopenpam.a`'s `localtime.o` and `setlogmask` via its
    `oldsyslog.o` were tried and reverted the same way item d7 reverted
    `localtime.o`/`libproc.o`/`strtoul.o` — real, deep, not-locally-
    satisfied dependency chains for one function each, a net loss.)

    **Real, current count: 167 undefined symbols** (down from the
    pre-CF ~110 count's own honest baseline, but that number wasn't
    apples-to-apples — CF's ~14 direct symbols were simply unresolved
    before, now they're real and working, but exposed ~150 of CF's *own*
    internal gaps as a side effect of `CFRuntime.c`'s monolithic
    class-registration init path, see item d7). Categorized, **critical
    path vs backlog per explicit user instruction (2026-08-26: "continue
    on the critical path to get launchd done, everything else goes to
    backlog")**:
    - **BACKLOG (CF/ICU scope, item d7's own territory, not launchd-
      specific)**: all remaining `CF*`/`kCF*`/`u_*`/`__CF*` symbols —
      CFCharacterSet/CFUniChar/CFLocale/CFNumberFormatter (ICU),
      CFReadStream/CFWriteStream (needs kqueue/FD), CFBundle (tried,
      reverted — 30-for-3 bad trade), CFBurstTrie, CFURLCreateDataAnd
      PropertiesFromResource, CFTimeZoneGetSecondsFromGMT,
      CFCopySearchPathForDirectoriesInDomains, the StringEncodings-
      internal family (`CFStringEncodeByteStream`/`DecodeByteStream3`/
      `EncodingByteLengthForCharacters`/`GetConverter`/
      `ConvertIANACharSetNameToEncoding`/`UnicodeToBytes`,
      `CFStringGetFileSystemRepresentation`/`GetMaximumSizeOf...`,
      `CFStringScanDouble`/`Integer`, `CFSetCharToUniCharFunc`,
      `CFStrConvertBytesToUnicode`, `CFCharToUniCharTable`,
      `CFCreateOldStylePropertyListOrStringsFile`, `__CFGetSlash`) — not
      needed by launchd's real plist-parsing CF usage (verified above).
    - **BACKLOG (already-tracked)**: pthread QoS/attr (item d3);
      Mach voucher/host-special-port routines (item d5) — this pass
      found more of the same real category (`host_reboot`/
      `host_set_exception_ports`/`_special_port`/`_UNDServer`,
      `mach_port_get_attributes`/`get_set_status`/`move_member`/
      `request_notification`/`set_attributes`/`set_mscount`,
      `mig_allocate`, `pid_for_task`) — extend d5's scope to cover
      these too, same real MIG-client-routine-regeneration fix; BSM
      audit-record construction (`au_*`, `audit_session_*`,
      `getaudit_addr`/`setaudit_addr`) — already deprioritized as
      peripheral logging; `job_mig_look_up2_forward`/`_parent_forward`,
      `runtime_ktrace`/`runtime_ktrace0` — already-tracked long tail.
    - **BACKLOG (`libSystem.B.dylib` export-parity gap, item d7's own
      already-documented architecture debt)**: `__dispatch_queue_attr_
      concurrent`, `__os_alloc_once*`, `__os_log_*`, `__platform_bzero`,
      `__pthread_bsdthread_init`/`_main_thread_postfork_init`/
      `_clear_qos_tsd`, `malloc_zone_*`, `malloc_default_zone`/
      `_good_size`, `mach_vm_region`, `__NSGetArgc`, `fmod`/`modf`/`pow`,
      `uuid_generate_random`/`_time`, `pthread_getugid_np`/
      `_is_threaded_np`/`_threadid_np`, `strtoul_l`, `getegid`.
    - **BACKLOG (never open-sourced, item d7's own finding)**: ASL
      (`asl_open`/`_close`/`_free`/`_new`/`_send`/`_set`).
    - **CLOSED 2026-08-26, same day, second real pass on the "new, not
      yet triaged" batch above — 11 more symbols, 167 → 156**: real,
      individually-verified fixes for every one of these turned out to
      be small and mechanical once actually attempted, not deep rabbit
      holes:
      - `mkdirat`/`getsockopt`: plain, non-`NO_SYSCALL_STUB`
        `create-syscalls.pl` raw-trap entries this project just hadn't
        listed yet — one-line addition each.
      - `getpeereid`/`getlogin`: both real `NO_SYSCALL_STUB` entries
        Apple implements as hand-written wrappers, not raw traps
        (`getpeereid` via `getsockopt(LOCAL_PEERCRED)`,
        `getlogin` via a raw `__getlogin` trap this project's own
        `create-syscalls.pl` step CAN still generate despite
        `NO_SYSCALL_STUB` — it just skips the convenience-wrapper name,
        not the trap itself). The old `_getpeereid` entry in the
        `create-syscalls.pl` loop was actually silently failing the
        whole time (swallowed by the loop's own `2>/dev/null || true`)
        since `getpeereid` was never a raw trap in the first place — a
        real, pre-existing bug in this script, now fixed with the real
        `third_party/Libc/gen/FreeBSD/getpeereid.c`/`getlogin.c`.
      - `execl`/`execv`, `signal`, `waitpid`: real, unmodified
        `third_party/Libc/gen/FreeBSD/{exec,signal,waitpid}.c`. Each
        needed one real, small, previously-undiagnosed fix:
        `exec.c`/`signal.c` call internal "private name" aliases
        (`_execve`/`_write`/`_sigaction`) that real Apple's own build
        generates via a linker alias/export-map step this project
        doesn't have — closed via a small, real, thin-forwarding compat
        file (`launchd_syscall_compat_headers/private_names_compat.c` +
        `private_names_decls.h`), not a from-scratch implementation.
        `waitpid.c`'s real issue was never "K&R syntax" as first
        suspected — it was `__weak_reference(sym,alias)` (meant to
        alias real `__waitpid` to public `waitpid`) being a genuine
        no-op in this project's `fbsdcompat/sys/cdefs.h`, AND not
        defined by the real SDK's own `sys/cdefs.h` at all, so clang
        parsed the bare macro-call lines as invalid K&R declarations.
        Fixed via `-D'__weak_reference(sym,alias)='` (makes the real
        no-op explicit) + a curated public `waitpid()` wrapper (same
        forwarding-only pattern as this script's existing
        `wait4_compat.c`).
      - `atexit`: real `stdlib/FreeBSD/atexit.c`. Needed a real,
        genuinely-undefined-anywhere `__isthreaded` global (Apple's
        skip-locking-when-single-threaded optimization flag) — closed
        via `isthreaded_compat.c`, hardcoded to 1/always-threaded,
        always correct since this project's pthread support is real,
        just not maximally optimized. Also needed a new tracked
        `os/alloc_once_private.h` (same class of per-consumer-tracked-
        copy as `mach_vm.h`'s own precedent — the untracked, gitignored
        `libc_build/compat_include/` already has one for a different
        key this consumer doesn't use).
      - `glob`, `sys_siglist`, `qsort`: real `gen/FreeBSD/glob.c` needed
        a chain of real headers (`sys/syslimits.h` for
        `COLL_WEIGHTS_MAX`, `setlocale.h`, `malloc_private.h`, a new
        tracked `System/machine/cpu_capabilities.h` forward) plus
        `-D__LIBC__=1` (a real Apple-internal build macro gating
        `collate.h`'s own declarations — without it,
        `__collate_range_cmp` has no visible prototype) plus its own
        real dependency `locale/FreeBSD/collcmp.c`. `sys_siglist`
        (`gen/FreeBSD/siglist.c`) and `qsort` (selectively extracted
        from `libremovefile.a`, same pattern as `cf_libc_extras.a`)
        needed no fixes at all, just wiring in.
      - **Not fixed, real gap confirmed, left for a future pass**:
        `initgroups` — genuinely `NO_SYSCALL_STUB` with no dedicated
        wrapper source anywhere in this project's pinned repos (checked
        again this pass, same conclusion as before).
      All real fixes are now permanent steps in `build_launchd_syscalls.sh`
      (61 objects, up from 46) and `build_launchd_core.sh`'s
      `launchd_libc_extras.a` (14 objects, up from 5) — see either
      script's own header comment for the full per-symbol writeup.
    - **BACKLOG (new this pass, real but not yet triaged as critical-
      path-or-not)**: `mktime` (real source exists but only inside
      `stdtime/FreeBSD/localtime.c`, the same expensive file already
      reverted twice for `localtime_r`/`ctime_r` — tried again implicitly
      via this pass's other fixes, still not worth its own real,
      deep, not-locally-satisfied dependency chain just for one
      function); `memorystatus_control`/`setiopolicy_np` (real
      macOS-specific resource-management syscalls, need real raw-trap
      generation, not yet attempted); `xpc_copy` (a REAL gap, not just
      unbuilt — confirmed via source inspection that `libxpc/xpc_misc.c`
      has `xpc_copy_description`/`_description_level`/
      `_entitlement_for_token` but no plain `xpc_copy(xpc_object_t)`
      deep-copy function at all; needs the same "confirm via historical
      `launchd-842.92.1` clone" treatment `ld2xpc`/`xpc2ld` got before
      concluding it needs a from-scratch implementation);
      `__logname`/`_mutex` (getlogin/logname caching — `getlogin()`
      itself is now real, this is a separate caching layer some code
      calls directly); `__sprintf_chk`; `__udivti3` (a compiler-rt
      128-bit-division builtin — checked `libclang_rt.ios.a`, doesn't
      provide it for this target, real gap, small and unlikely to
      matter much); `__mach_errors` (a data table backing
      `mach_error_string`, which already works — this looks like a
      separate, real, currently-unused symbol some code references
      directly); `_localtime`/`_ctime_r`/`_localtime_r` (all live in the
      same reverted `localtime.c`, same as `mktime` above).
    Every one of these was spot-checked against the current curated
    stack (`nm -g` across all `.a` files) before being called a real
    gap, not assumed.

    **UPDATE, same day, real 5th fix on `mktime`/`localtime`/
    `localtime_r`/`ctime_r`**: unblocked for real by looking at how
    PureDarwin's own CMake build gets past this exact wall — it depends
    on a real `libc_features` step that runs `third_party/Libc`'s own
    real `xcodescripts/generate_features.pl` against a real per-
    platform `Makefile.inc`. Both pieces were already pinned in this
    project's own Libc checkout, just never run. Real, self-contained
    regen via a new tracked `libc_features_platform/Platforms/iphoneos/
    Makefile.inc` (one documented change from the real pinned copy:
    `FEATURE_TIMEZONE_CHANGE_NOTIFICATION` disabled, since enabling it
    needs the real `notify(3)` client API this project has only built
    the MIG server side of) + `-fno-stack-check` (avoids needing a
    from-scratch, safety-critical `__chkstk_darwin` ABI routine with no
    real source anywhere in this project) + `libsystem_pthread.dylib`
    linked directly (localtime.c calls the internal
    `_pthread_rwlock_*` variants, outside `libSystem.B.dylib`'s own
    limited re-export list — the same already-documented export-parity
    gap, not a new one). `asctime`/`asctime_r` confirmed to hit the
    identical deep `xlocale.c` chain already reverted for `strtoul_l` —
    left deferred, not re-chased. **Real gap count now 109.**

    **FINAL STATUS, end of the 2026-08-26 critical-path pass: 183 → 109
    undefined symbols** (a 40% reduction, every single fix real and
    individually trial-link-verified — no fabricated stubs beyond the
    honestly-scoped compat forwards documented throughout this section
    and item d7). Real, closed this pass: CF wired in for real (item
    d7); 15+11+6 real syscall/libc gaps via selective `ar x` extraction
    and real curated wrappers (`execl`/`execv`/`signal`/`waitpid`/
    `atexit`/`glob`/`getpeereid`/`getlogin`/`mkdirat`/`getsockopt`/
    `sys_siglist`/`qsort`/`chdir`/`access`/`strstr`/`sleep`/
    `sigprocmask`/`sigaction`/`getifaddrs`/`freeifaddrs`/
    `arc4random_uniform`/`bind`/`getentropy`/`uuid_copy`/
    `uuid_is_null`/`fegetenv`/`fesetenv`/`fstatat`/`getaudit_addr`/
    `setaudit_addr`/`memorystatus_control`); real Mach host/port admin
    routines (`host_reboot`/`host_set_*`, `mach_port_set_mscount`/
    `get_set_status`/`move_member`/`request_notification`/
    `get_attributes`/`set_attributes`, `pid_for_task`, `mig_allocate`);
    pthread thread-ops (`pthread_detach`/`kill`/`setschedparam`/
    `threadid_np`/`is_threaded_np`/`getugid_np`), honestly scoped to
    what's reachable given this project's own pre-existing
    pthread_create() gap (no real per-thread targetable handle); real
    malloc zone API (`malloc_zone_*`, CFAllocator's real default-
    allocator implementation); real math (`fmod`/`modf`/`pow`, no Libm
    anywhere in this project); `mach_vm_region`, `uuid_generate_random`/
    `_time`, `_NSGetArgc`, `getegid`; real `ktrace.c` wiring
    (`runtime_ktrace`/`0`, root-caused as "never compiled" not "gated
    behind a macro"); a real, from-scratch `xpc_copy()` (confirmed
    genuine gap, same class as `ld2xpc`/`xpc2ld`); real `__sprintf_chk`/
    `__vsprintf_chk`/`__mach_errors`/`__udivti3`. Two real, pre-existing
    bugs found and fixed as a side effect: `getpeereid`'s
    `create-syscalls.pl` entry was silently failing forever (swallowed
    by the loop's own `2>/dev/null || true`); a real "SDK's own older
    header wins" conflict for `mach_traps.h`/`libkern/OSReturn.h`/
    `IOKit/IOReturn.h` (same bug class as `mach_vm.h`/`spawn.h` before
    them).

---

## P2, item d7 (CoreFoundation/ICU/libxml2 port) — full session narrative archived 2026-08-27

`docs/backlog.md`'s current P2 item d7 condenses this down to current
state only (42 undefined-symbol groups, categorized, plus the 4 real
compile-time exclusions). This is the full, verbatim blow-by-blow of how
it got there — the ICU two-stage cross-build story, the CFBundle
30-for-3 trade that got reverted then un-reverted once the dependency
turned out already-buildable, the CFError/CFDate/CFURL "just try
compiling it" sweep, every individual syscall wrapper closed along the
way, and the real `-reexport_library` fix that closed 27 gaps at once —
preserved for the real detective work it documents.

d7. **Real CoreFoundation port started 2026-08-26 — real minimal subset
    compiles + archives clean; real trial link has ~150 honestly-
    categorized remaining gaps.** `tools/userland_staging/
    build_corefoundation.sh` (new) — see its own HONEST SCOPE STATEMENT
    for the full writeup. Real, unmodified source from
    `third_party/swift-corelibs-foundation` (Apple's own, actively-
    maintained CF fork; pinned in `setup_third_party.sh` at
    `swift-5.4-DEVELOPMENT-SNAPSHOT-2021-02-12-a`), NOT
    `apple-oss-distributions/CF` — that repo's last real tag
    (`CF-855.17`) is dated 2014-08-15 and CF isn't part of the real
    `macos-112` distribution-macOS manifest at all (both confirmed via
    real clones). This resolves the "try Apple's real source first"
    question for CF specifically: it works, so the GNUstep fallback the
    user authorized as a contingency was never needed.

    17 real files compile clean (Base/Collections/NumberDate/String/
    Parsing subprojs — Array/Dictionary/Data/BasicHash/Set/Bag/
    BinaryHeap/BitVector/Storage/Tree/Number/Boolean/String/
    PropertyList/BinaryPList/Base/Runtime/Utilities/UUID/Platform/
    KnownLocations/SortFunctions), archived into `libCoreFoundation.a`.
    Real fixes along the way: a genuine upstream `bool`/`Boolean`
    prototype-conflict bug in `CFPriv.h` (patched via
    `third_party-patches/swift-corelibs-foundation.patch`, applied by
    `setup_third_party.sh` — same real-patch convention as `bash.patch`
    etc); a genuine upstream `TARGET_OS_OSX`/`TARGET_OS_MAC` guard
    mismatch around `__CFTSDInitialize` (iOS-target-only bug, forward-
    declared via a compat header rather than patched); `DEPLOYMENT_RUNTIME_C=1`
    + `INCLUDE_OBJC=1` (matches `CoreFoundation/build.py`'s own real,
    non-Swift-runtime build path — the CMake default assumes a Swift
    runtime this project doesn't have); the pinned iPhoneOS SDK's real
    `<mach/mach_vm.h>` being `#error unsupported` (same gap
    `build_libmalloc.sh` already found — reused + extended its compat
    header); `OS_LOG_SUBSYSTEM_RUNTIME_ISSUES` missing from this SDK's
    older `<os/log.h>`.

    **The bigger structural finding**: `CFRuntime.c` has one monolithic
    static class-registration table referencing every real CF type by
    extern symbol, so even a "minimal subset" needs *something* to
    satisfy symbols for types it doesn't build. Real, safe (never
    dereferenced unless that specific type is instantiated, which
    nothing in this project's call graph does) zero-initialized
    `CFRuntimeClass` stub descriptors for ~26 unbuilt types (RunLoop/
    Stream/Socket/MachPort/URL family/Locale/Calendar/TimeZone/Date
    family/Formatters/CharacterSet/AttributedString/Bundle/PlugIn/
    Preferences/Error) plus a handful of related init-hook/toll-free-
    bridging no-ops now live in `cf_compat_headers/cf_class_stubs.c`
    (fully documented per-symbol in that file). One of these
    (`_CFCompareStringsWithLocale`) got a real ordinal-UTF16-compare
    fallback instead of a stub, since it's real, reachable logic, not
    dead code.

    **Real trial link, ~150 symbols remaining, categorized** (none
    fixed yet — this is the honest state after the above):
    - **Real ICU dependency** (`_u_getIntPropertyValue`/
      `_u_hasBinaryProperty` + the entire `CFUniChar*`/
      `CFCharacterSet*` family + `CFStringUtilities.c`'s own
      `_CFCompareStringsWithLocale` needing `<unicode/ucol.h>`) — this
      project has no ICU port; out of scope until/unless one exists.
      Real impact: CFString's non-ASCII case-mapping/decomposition/
      collation and CFCharacterSet are unavailable, but basic
      CFStringGetCString/CFStringCreateWithCString-class ASCII usage is
      unaffected.
    - **Real, unported CF subsystems** referenced by API (not just the
      class table already stubbed above): `CFLocaleCopyCurrent`/
      `GetIdentifier`/`GetValue`/`GetTypeID` (real ICU dependency,
      confirmed via trial compile: `CFLocale.c` needs `<unicode/uloc.h>`,
      same wall as `CFStringUtilities.c`), `CFNumberFormatter*` (8
      functions, real ICU dependency too), `CFRead`/`CFWriteStream*` (6
      functions, needs real kqueue/FD integration), `CFBundleCreate`/
      `CopyLocalizedString`/`GetBundleWithIdentifier` — **tried and
      reverted 2026-08-26**: real `PlugIn.subproj/CFBundle.c` (1738
      lines) also compiled clean with zero errors, but a real trial link
      showed it drags in ~30 new symbols (real `dlfcn`/DYLD-introspection
      bundle-loading machinery — `_CFBundleDlfcnLoadBundle`/
      `_LoadFramework`/`_CheckLoaded`/`_Preflight`/`_Unload`,
      `_CFBundleDYLDCopyLoadedImagePaths*`, `_CFBundleGrokBinaryType`,
      real path-manipulation helpers from a different, not-yet-built
      file — `__CFGetFileProperties`/`_GetPathProperties`/
      `_CreateContentsOfDirectory`/`_LengthAfterDeletingLastPathComponent`
      etc) to close just 3 real symbols — a **30-for-3 trade**, much
      worse than `CFURL.c`'s own 6-for-3 trade, and reverted for the
      same reason `localtime.o`/`libproc.o`/`strtoul.o` were: real
      bundle/plugin discovery is genuinely out of proportion to
      launchd/IOKitUser's actual need (reading one plist's already-
      known-path contents, not real bundle resource lookup). Left
      stubbed;
      `CFCopySearchPathForDirectoriesInDomains`, `CFBurstTrie*` (4
      functions — a real CFPropertyList/CFBinaryPList performance
      helper, not yet located which subproj it lives in),
      `CFURLCreateDataAndPropertiesFromResource` (real old-style
      resource-fork API — CFURL's own basic path-creation functions are
      closed, see below), `CFTimeZoneGetSecondsFromGMT` (CFURL.c's own
      real dependency on the still-unported CFTimeZone).
      **CLOSED 2026-08-26: `CFError*`** — real `Error.subproj/CFError.c`
      (645 lines) compiled clean with zero extra work needed, added to
      `build_corefoundation.sh`'s `FILES` array, its class-table entry
      in `cf_class_stubs.c` removed in favor of the real one. Closed 6
      symbols (`CFErrorCreate`/`GetCode`/`GetDomain`/`CopyUserInfo`,
      `kCFErrorDomainCocoa`/`OSStatus`/`POSIX`,
      `kCFErrorDebugDescriptionKey`/`UnderlyingErrorKey`).
      **CLOSED 2026-08-26: `CFDate*`** — real
      `NumberDate.subproj/CFDate.c` (593 lines) also compiled clean with
      zero extra work, same treatment (class-table stub removed, the
      real file's own `__CFDateInitialize()` replaces the compat no-op
      that used to stand in for it). Closed 4 symbols (`CFDateCreate`/
      `GetAbsoluteTime`, `CFAbsoluteTimeGetCurrent`/`GetGregorianDate`,
      `CFGregorianDateGetAbsoluteTime`, `kCFAbsoluteTimeIntervalSince1970`).
      **CLOSED 2026-08-26: `CFURL*`'s core path-creation API** — real
      `URL.subproj/CFURL.c` (5413 lines, the biggest file added so far)
      also compiled clean with zero extra work, `__CFURLClass` stub
      removed (`__CFURLComponentsClass`, a genuinely separate real file
      not yet built, keeps its stub). Closed 3 real symbols
      (`CFURLCreateFromFileSystemRepresentation`/
      `CreateWithFileSystemPath`, `CFURLCopyFileSystemPath`) but honestly
      surfaced 6 new ones as real, previously-invisible transitive
      dependencies of CFURL's fuller feature set (`__CFGetSlash`,
      `__os_log_debug_impl`/`_default` [same os_log-export-gap category
      as `__os_log_error_impl`], `_CFStringEncodingUnicodeToBytes`,
      `_CFStringGetMaximumSizeOfFileSystemRepresentation`,
      `_CFTimeZoneGetSecondsFromGMT`) — a net **worse** raw symbol count
      (+3) for **more** real capability (the actually-useful path-
      creation functions now work for real; the newly-exposed gaps are
      in more peripheral CFURL functionality). Kept rather than reverted
      — the raw count is a progress signal, not the goal.
      Real gap count now **108** (dynamic-link count including the
      curated `cf_libc_extras.a`), starting from 153. Now 25 real/compat
      files in `build_corefoundation.sh`'s `FILES` array (24 real Apple
      source + 1 compat stub file).
    - **CONFIRMED out of scope 2026-08-26 (was "not yet checked")**:
      ASL legacy logging (`asl_open`/`_close`/`_free`/`_new`/`_send`/
      `_set`) — CFUtilities.c's own legacy console-logging fallback. The
      real full ASL client library is a separate apple-oss-distributions
      repo Apple never open-sourced as far as this project's
      `docs/apple-oss-catalog.md` inventory goes, and isn't pinned in
      `setup_third_party.sh`; `third_party/libplatform/src/simple/asl.c`
      only has the much smaller `_simple_asl_*` kernel/early-boot subset,
      a different real API, not a drop-in. Same "out of scope, no source
      to port" treatment as ICU, for a genuinely different reason (never
      open-sourced vs. real, tractable, just-not-attempted).
    - **CLOSED, verified 2026-08-26**: `dispatch_apply`/`_queue_create`/
      `_release`/`_once`, `os_log_create`/`_type_enabled`/
      `__os_log_error_impl`, `__os_alloc_once*`, `__chkstk_darwin`,
      `__stack_chk_fail`/`_guard`, `__strlcpy_chk`/`__vsnprintf_chk`,
      `__platform_bzero`, `__pthread_bsdthread_init`/
      `_main_thread_postfork_init`, `_NSGetExecutablePath`, `_dlopen`/
      `_dlsym`, `_kill`/`_mmap`/`_mprotect`/`_read`/`_write`/`_kdebug_trace`,
      `_flsl`, `_pthread_rwlock_rdlock`/`_unlock`, `_mach_error_string`,
      `_CFURLCreateWithFileSystemPathRelativeToBase` (30 symbols) were a
      **link-mode mismatch in the test harness itself**, not real gaps —
      confirmed by actually doing the real dynamic trial link this item
      called for: dropped `-static` (kept `-nostdlib`), linked
      `libCoreFoundation.a` against `libdispatch.dylib` +
      `libdyld.dylib` + `libSystem.B.dylib` directly (same real recipe
      `build_dyld_test_exec.sh` already established: plain dylib links
      on the command line, `-Wl,-fixup_chains`, no `-static`) — every
      one of these 30 resolved for real. Real gap count is **138, not
      153**. This also surfaced a second, smaller, already-documented
      finding: 15 *new* symbols (`fprintf`/`printf`/`getcwd`/`close`/
      `isdigit`/`isspace`/`strrchr`/`strnlen`/`memchr`/`asprintf`/
      `pthread_atfork`/`issetugid`/`fprintf_l`/`snprintf_l`/
      `strncasecmp_l`) appeared that weren't missing in the static link
      — `libSystem.B.dylib` doesn't yet export full parity with the
      curated static libc archives it was flattened from. This is
      CLAUDE.md's own already-documented "Static-vs-dylib architecture
      debt", not a new CF-specific problem — no action needed here
      beyond noting CF's own real future dynamic link (once wired into
      launchd/IOKitUser) will hit it too.
    - **Real `libSystem.B.dylib` export-parity gaps** (the *other* half
      of the already-documented architecture debt found while verifying
      the dynamic-link-mode item above): `close`/`getcwd`/`isdigit`/
      `isspace`/`strrchr`/`strnlen`/`memchr`/`asprintf`/`fprintf`/
      `_fprintf_l`/`printf`/`snprintf_l`/`strncasecmp_l`/`pthread_atfork`/
      `issetugid`/`__memmove_chk` are all real, ordinary libc functions
      this project's own static `libc_gen.a`/`libc_string.a`/
      `libc_stdio.a`/`libc_printf.a` already provide — they only show up
      as gaps in a *dynamic* link because `libSystem.B.dylib` (the
      flattened static-stack-as-one-dylib, per CLAUDE.md's own
      documented debt note) doesn't yet export them. Not a CF-specific
      problem and not fixed here; real fix is closing that dylib's own
      export-parity gap, tracked wherever that dylib's own build script
      lives (not yet cross-referenced from here).
    - **CLOSED 2026-08-26 (6 symbols)**: `getpwnam`/`getpwuid`/`getuid`/
      `geteuid`/`gethostname`/`writev`. Real, selective `ar x` extraction
      (same pattern `build_libdispatch_dylib.sh` already established) of
      5 small `.o` members — `copyfile_syscalls.o` (from `libcopyfile.a`,
      has both `getpwnam`+`getpwuid`), `getuid.o` (`libcopyfile.a`),
      `geteuid.o` (`libremovefile.a`), `gethostname.o` (`libresolv.a`),
      `date_compat.o` (`libopenpam.a`, has `writev`) — into a new
      `cf_libc_extras.a`, now a permanent `build_corefoundation.sh` step.
      Verified via real trial, not assumed: `localtime.o` (real home of
      `localtime_r`), `libproc.o` (`proc_pidinfo`), and `strtoul.o`
      (`strtoul_l`) were tried the same way and reverted — each pulls in
      its own deep, not-locally-satisfied dependency chain (pthread
      once/get|setspecific/rwlock_wrlock + open/close/read/fstat/access
      for `localtime.o`; `__proc_info`/`__thread_selfid`/
      `__process_policy` for `libproc.o`; locale globals for
      `strtoul.o`) that a real trial link confirmed is a **net loss** —
      more new gaps introduced than symbols closed. Left as real,
      documented remaining gaps (`localtime_r`, `proc_pidinfo`,
      `strtoul_l` below) rather than chasing diminishing returns. Also
      added the same curated `libc_string.a`/`libc_gen.a`/`libc_stdio.a`/
      `libc_printf.a`/`libc_locale_for_stdio.a` archives the static trial
      link already used to the dynamic one (they'd only ever been linked
      into the static trial link before — real, zero-pollution, already-
      proven-complete for these, and closed a further batch:
      `isdigit`/`isspace`/`strrchr`/`strnlen`/`memchr`/`asprintf`/
      `fprintf`/`_fprintf_l`/`printf`/`snprintf_l`/`strncasecmp_l`/
      `close`/`getcwd`/`issetugid`/`__memmove_chk`, 15 more symbols).
    - **Real, still-genuinely-missing-everywhere libc/Mach gaps**
      (present in neither the curated static archives now linked into
      this trial link nor `libSystem.B.dylib`, and not worth chasing
      further per the reverted attempts above): `getegid`/`localtime_r`/
      `proc_pidinfo`/`strtoul_l`/`pthread_getugid_np`/
      `pthread_is_threaded_np`/`pthread_threadid_np`/`mach_vm_region`
      (declared in `cf_compat_headers/mach/mach_vm.h`, but the real
      definition in `libmalloc.a`'s `mach_vm.c` needs checking — likely
      just needs exporting)/`malloc_zone_*` (6 functions)/
      `malloc_default_zone`/`malloc_good_size`/`uuid_generate_random`/
      `_time`/`_NSGetArgc`/`fmod`/`modf`/`pow`/
      `__dispatch_queue_attr_concurrent`/`__os_log_error_impl`/
      `pthread_atfork`. Not yet spot-checked against the full curated
      stack one more time — worth a quick pass before assuming any of
      these need new work.

    **Verified, not just assumed**: tried a real, narrow trial link of a
    tiny probe program calling only IOKitUser's specific documented
    need (`CFArrayCreate`/`GetCount`/`GetTypeID`/`GetValueAtIndex`,
    `CFBooleanGetTypeID`/`GetValue`, `CFDataCreateWithBytesNoCopy`,
    `CFDictionaryCreate`/`ApplyFunction`/`GetTypeID`, `CFGetTypeID`,
    `CFNumberGetTypeID`/`GetValue`, `CFRelease`) — both with and without
    `-force_load` — expecting it to link far cleaner than the full
    `core.c`/launchd-scale trial link above, since none of those calls
    touch CFString/CFPropertyList. It didn't: the undefined-symbol list
    came back nearly identical (~146 vs ~150 symbols). Root cause:
    `CFRuntime.c`'s own `__CFInitialize()` — unconditionally reached by
    any real `CFRuntimeCreateInstance()` call, i.e. any real CF object
    creation at all, `CFArrayCreate` included — itself calls into
    `_CFStringGetUserDefaultEncoding` (stubbed, fine) but real archive-
    member pull-in also drags in enough of `CFUtilities.c`/
    `CFPlatform.c`'s own real logging/ASL/time fallbacks to reproduce
    almost the whole gap list. **Correction to this project's own
    working assumption**: there is no "cheap partial CF" shortcut here —
    closing the real gap list above (or at least its non-ICU, non-
    dynamic-link-mode portion) is a prerequisite for ANY real CF usage
    in this project, not just CFString-heavy usage.

    **Concrete next steps, in priority order**: (1) ~~resolve the
    static-vs-dynamic trial-link harness gap~~ — DONE 2026-08-26; (2)
    ~~real `CFError.c` port~~ — DONE same day; (3) ~~selective `ar x`
    extraction of already-built-elsewhere libc symbols~~ — DONE same
    day (`cf_libc_extras.a`, 6 symbols, plus 15 more from linking the
    already-existing curated `libc_string.a`/etc into the dynamic trial
    link too). **Real gap count is now 111**, down from the original
    153. Remaining: (4) decide whether `CFPropertyListCreateFromXMLData`
    (needed by `launchd/core.c`, explicitly out of scope for this pass —
    only binary-plist parsing was included) is worth a follow-up pass,
    since XML plists are real launchd/LaunchDaemons input format on real
    Darwin; (5) the ICU wall (CFUniChar/CFCharacterSet/
    CFStringUtilities collation, CFLocale, CFNumberFormatter) is the one
    item on this list that's a genuine scope decision, not just
    unstarted work — real ICU is a large, separate port this project has
    never attempted; (6) `libSystem.B.dylib`'s own export-parity gap vs
    the curated static libc stack (found 2026-08-26, see above) — not
    CF-specific, but CF's real future dynamic link will hit it, so it's
    worth closing before CF actually gets wired into launchd/IOKitUser;
    (7) the remaining real, still-missing-everywhere libc/Mach symbols
    (`getegid`, `mach_vm_region`, `malloc_zone_*`, `uuid_generate_*`,
    etc, see above) — smaller and more bounded than the CF subsystem/ICU
    work, worth a quick pass; (8) once the gap list is as small as it's
    going to get without ICU/further subsystem ports, wire the working
    subset into IOKitUser (closing that project's own long-documented
    "IOKitUser needs CoreFoundation" wall) — the actual original reason
    this port started.

    **Real ICU port started 2026-08-26** (the wall named in item (5)
    above is no longer a scope decision — begun for real). First,
    corrected a stale assumption: `docs/apple-oss-catalog.md` had listed
    `ICU` under "not currently needed," but Apple did open-source it —
    confirmed via `git ls-remote apple-oss-distributions/ICU` and the
    `macos-112` manifest (`git ls-tree macos-112 -- ICU` == commit
    `f7b86563706f76d5267452815c6b6121cf4698b7` == tag `ICU-66109`), same
    method every other pin in this project uses. Pinned in
    `setup_third_party.sh`, cloned to `third_party/ICU` (325MB checkout:
    `icuSources/` is stock upstream ICU4C with its own real autotools
    build, plus `cldrFiles/` locale data and Apple's `ICU.xcodeproj` for
    their internal B&I pipeline, unused here).

    ICU4C's real cross-compile story (confirmed via `configure.ac`) is a
    two-stage `--with-cross-build=<dir>` mechanism: build ICU natively
    for the host first (to get working `genrb`/`gencnv`/`gennorm2`/etc.
    tools plus generated locale/data tables, since generating those
    requires actually running ICU code), then cross-configure the real
    target build pointing at that host build root. Categorically
    different from this project's usual from-scratch clang-per-file-loop
    template (`build_libc_regex.sh`) — reinventing autotools by hand for
    a project this size isn't worth it. `tools/userland_staging/
    build_icu_host.sh` is stage 1 (native host build only, never linked
    into anything that ships) — **DONE 2026-08-26**: real ICU 66.1 host
    build succeeded, producing `libicuuc`/`libicui18n`/`libicudata`/
    `libicuio` dylibs, all host tools, and a real packaged CLDR data
    library (`icudt66l`), with `config/icucross.mk` present and ready to
    be the `--with-cross-build` target for stage 2 (not yet written: the
    real arm64-apple-ios14.4 cross build using this project's usual
    Xcode-12 iPhoneOS SDK / `-ffreestanding` recipe for `CC`/`CXX`).

    Two real gaps found and fixed getting stage 1 green (both documented
    in that script's own header comment, and the first is also filed as
    `third_party-patches/ICU.patch`, applied via `setup_third_party.sh`
    like this project's other patches):
    - `common/Makefile.in` never listed `aaplbfct.cpp`
      (`AppleLanguageBreakFactory`) even though `localsvc.cpp`
      unconditionally references it and Apple's own `ICU.xcodeproj`
      builds both — a real upstream (Apple) build-system oversight, not
      a this-project issue. Plain `configure && make` hits an
      undefined-symbol link error without it.
    - Apple's fork flips ICU's normal upstream default of
      `U_SHOW_CPLUSPLUS_API` from 1 to 0 (deliberate: client code linking
      the shipped OS dylib must not depend on ICU's non-ABI-stable C++
      API) but ICU's own internal `.cpp` files are real C++-API consumers
      and need it back on to build the library itself at all — confirmed
      via `ICU.xcodeproj/project.pbxproj` that Apple's real internal
      build passes `U_SHOW_CPLUSPLUS_API=1`. Passed through `CPPFLAGS`,
      matching Apple's own recipe (not a source patch).
    - Also disabled `extra`/`tests`/`samples` in this host build
      (`--disable-extras --disable-tests --disable-samples`): the only
      real goal here is the `--with-cross-build` reference root, and
      `extra/uconv`'s sample CLI tool has its own real, separate
      `UChar`/`char16_t` pointer-type mismatch against any current host
      clang (an ICU-66-era-vs-modern-libc++ incompatibility, unrelated to
      this project) that isn't worth fixing for dead-to-this-project
      sample code.

    **Stage 2 (real arm64-apple-ios14.4 target cross build) DONE
    2026-08-26** — `tools/userland_staging/build_icu_target.sh`: real
    `libicuuc`/`libicui18n`/`libicuio`/`libicudata` dylibs, confirmed
    genuine arm64 Mach-O (`file`/`otool -l`), correctly linking
    `/usr/lib/libSystem.B.dylib` + `/usr/lib/libc++.1.dylib` (this
    project's own dylibs resolve those install names at actual link
    time, same as every other real per-library dylib here).
    `libicudata.66.1.dylib` is a real 29MB packaged CLDR/Unicode data
    library — built by having the HOST's own real `genrb`/`pkgdata`
    (stage 1's output, run natively on the build machine, per ICU4C's
    own `--with-cross-build` design) generate+package data for the
    target arch, never executing target-arch code during the build.
    Confirmed the two exact symbols item (5) above named as ICU's real
    wall are now real exports: `_u_hasBinaryProperty`/
    `_u_getIntPropertyValue` in `libicuuc`, `_ucol_open` in `libicui18n`.

    Three more real gaps found and fixed getting stage 2 green (all
    documented in `build_icu_target.sh`'s own header comment; none
    needed a source patch):
    - This project's usual `-ffreestanding` flag (every other
      `build_*.sh` here uses it) breaks ICU's `./configure` outright:
      per the C++ standard, `-ffreestanding` makes `main()` an ordinary,
      mangled function (`__Z4mainv`) instead of the special unmangled
      `_main` a linked executable's entry stub expects, so every one of
      configure's own full-link compiler-sanity checks (AC_PROG_CXX, the
      namespace-support probe, etc) fails with "_main ... referenced".
      Confirmed with a standalone repro isolating the flag. Fix: drop
      `-ffreestanding` (confirmed `-fno-builtin`/`-fno-stack-protector`
      alone don't cause this), since samples/tests/extras — the only
      real ICU components that ever link a full executable — are
      already disabled.
    - `common/aaplbfct.cpp` unconditionally `#include`s
      `<NSSystemDirectories.h>` (missing entirely from the iPhoneOS
      SDK), but its only real use is already dead code on this target
      (wrapped in the file's own `#if !TARGET_OS_EMBEDDED` / `#if 0`,
      and `TARGET_OS_EMBEDDED=1` confirmed for arm64-apple-ios14.4) —
      just the `#include` itself was left unguarded. Fixed by moving it
      inside the same `#if !TARGET_OS_EMBEDDED` guard its usage already
      has (second commit in `third_party-patches/ICU.patch`).
    - `--disable-tools` (needed to avoid `tools/pkgdata.cpp`'s real
      `system()` call, which the iPhoneOS SDK marks `__IOS_PROHIBITED` —
      a hard unavailable-on-iOS error, not a warning, for a build-time
      tool binary that would never run on this project's actual boot
      target) turned out to also disable the real `data` subdir
      entirely: `Makefile.in` ties `DATASUBDIR` to the same `TOOLS`
      autoconf conditional, even though `data/Makefile.in`'s own
      packaging rule already resolves through `--with-cross-build`'s
      `TOOLBINDIR` to the *host's* tools regardless of whether local
      target-arch tools get built. A real ICU build-system gap specific
      to combining `--disable-tools` with `--with-cross-build` (Apple
      never needed to hit this, since they don't build pkgdata for an
      iOS device target in their public source drops) — fixed by
      uncommenting `DATASUBDIR = data` in the *generated* top-level
      `Makefile` after configure (scoped to this build tree, not filed
      as a general `ICU.patch` change, since it's specific to that flag
      combination rather than a general upstream inconsistency).

    **CF wiring DONE 2026-08-27** — `build_corefoundation.sh` now links
    the real ICU dylibs and builds 21 more real, previously-stubbed CF
    source files: `CFUniChar.c`/`CFCharacterSet.c` (+ their real embedded
    Unicode data, `CFCharacterSetData.S`/`CFUniCharPropertyDatabase.S`
    wrapping `.incbin`-included `CharacterSets/CFCharacterSetBitmaps
    .bitmap`/`CFUniCharPropertyDatabase.data` — real Apple data already in
    this checkout, NOT ICU-generated), `CFStringUtilities.c` (real ICU
    collation) + `CFStringEncodings.c`, `CFLocale.c`/`CFLocaleIdentifier.c`
    /`CFLocaleKeys.c`/`CFNumberFormatter.c`, `CFCalendar.c`/
    `CFCalendar_Enumerate.c` + `CFTimeZone.c` (pulled in because
    `CFLocale.c` unconditionally calls `_CFCalendarCreateCoWWithIdentifier`
    and dereferences the result — a real crash risk if stubbed to NULL,
    unlike this file's other safe-because-unreachable stubs — and
    `CFCalendar.c` in turn unconditionally calls
    `CFTimeZoneCopyDefault`/`CFTimeZoneGetSecondsFromGMT`), plus
    `CFBigNumber.c`/`CFUnicodeDecomposition.c`/`CFStringScanner.c`/
    `CFBurstTrie.c`/`CFStringEncodingConverter.c`/`CFBuiltinConverters.c`/
    `CFStringEncodingDatabase.c`/`CFICUConverters.c`/`CFFileUtilities.c`/
    `CFOldStylePList.c` — each pulled in only after a real trial compile
    confirmed it, matching the same "just try it, it's often free"
    pattern CFError.c/CFDate.c/CFURL.c already established. All 21 files
    compiled clean or near-clean (`CFCalendar.c`'s only diagnostics are
    harmless `-Wswitch` "not all enum cases handled" warnings). Removed
    the now-superseded `cf_class_stubs.c` entries these real files
    replace: 5 `CF_STUB_CLASS` descriptors (CharacterSet/Locale/
    NumberFormatter/Calendar/TimeZone), `__CFCharacterSetInitialize`,
    `__kCFLocaleCollatorID`, `__CFLocaleGetNullLocale`/
    `__CFLocaleSetNullLocale`, `_CFStringGetUserDefaultEncoding`, and
    `_CFCompareStringsWithLocale` (the exact stub docs/backlog.md's own
    d7 entry above already named as the one needing real ICU).
    Real gap count for this specific trial link is now **83 symbol
    groups** (down from ~150 when ICU was entirely stubbed out), and
    every one left falls into an already-categorized bucket: the
    genuinely-out-of-scope subsystems this project has already decided
    not to port (`CFBundle*`/`CFPreferences*`/`CFDateFormatter*`/
    `CFDateComponents*`/`CFDateInterval*`/`CFReadStream*`/
    `CFWriteStream*`), confirmed-never-open-sourced ASL, the
    already-documented `libSystem.B.dylib` export-parity gap (`_abs`/
    `_bsearch`/`_qsort`/`_strstr`/`_scalbn`/`_llabs`/`_readlink`/
    `_setenv`/`_time`/`_getpwuid_r`/`_mkdir`/`_pwrite`/`_atol`/
    `_getsectbynamefromheader_64`/`_pthread_atfork`), the already-
    documented "still genuinely missing everywhere" list (`_getegid`/
    `_proc_pidinfo`/`_pthread_getugid_np`/`_pthread_is_threaded_np`/
    `_pthread_threadid_np`/`_uuid_generate_random`/`_uuid_generate_time`),
    a handful of non-ICU "platform" (built-in-table, not ICU-backed)
    string-encoding-converter symbols and one lone `_CFUniCharPrecomposeCharacter`
    not yet traced to their real source file, and a real but
    **newly-surfaced, not-yet-root-caused** `os_log`/`dispatch` gap
    (`__os_log_debug_impl`/`__os_log_default`/`__os_log_error_impl`/
    `_os_log_create`/`_os_log_type_enabled`/
    `__dispatch_queue_attr_concurrent`/`__mh_dylib_header`) — these were
    confirmed genuinely absent from both `libdispatch.dylib` and
    `libSystem.B.dylib` via `nm` (not the static-link-mode artifact the
    similarly-named symbols in this item's earlier "CLOSED 2026-08-26"
    entry turned out to be), so CFLocale/CFCalendar's new code paths
    exercise more of the real os_log API surface than earlier CF files
    did. Worth a dedicated follow-up pass, not chased further this
    session.

    Also found+fixed along the way: this session's own ICU stage 2
    `uameasureformat.o` fix (see above) had gone stale — patched
    `i18n/Makefile.in` mid-session but never re-ran
    `build_icu_target.sh` afterward, so the on-disk dylib didn't actually
    have the symbol until a real rebuild (third commit now in
    `third_party-patches/ICU.patch`, verified via a fresh-clone `git am`
    same as the other two). Real lesson: after editing anything under
    `third_party/ICU`, always rebuild both ICU stages before trusting
    the dylibs on disk are current — same class of staleness this port's
    "GNU Make self-regeneration" gotcha already documented, just at the
    level of remembering to invoke the build at all.

    **Full-sweep pass DONE 2026-08-27** — with ICU-dependent Locale/
    Calendar/CharacterSet in place, did a real "just try compiling it"
    pass (the same method that already proved CFError/CFDate/CFURL/etc
    free) across literally every remaining `.c` file in the
    swift-corelibs-foundation checkout, rather than trusting the
    original scope note's assumption that RunLoop/Stream/Socket/
    MachPort/PlugIn structurally needed new kqueue+Mach port
    infrastructure. That assumption was never actually verified and
    turned out to be wrong: **RunLoop.subproj (CFRunLoop.c/CFMachPort.c/
    CFMachPort_Lifetime.c/CFMessagePort.c/CFSocket.c), Stream.subproj
    (CFStream.c/CFSocketStream.c/CFConcreteStreams.c), and the entire
    PlugIn.subproj bundle family (CFBundle.c + all 10 of its
    CFBundle_*.c helper files + CFPlugIn.c) all compile clean** — this
    project's existing real syscall layer was already sufficient for
    the *compile* step (whether the real runtime *semantics* work is a
    separate, unverified question — this pass only proves compilation
    and linking, not correctness). Also newly added: the DateFormatter
    family (`CFDateFormatter.c`/`CFDateComponents.c`/`CFDateInterval.c`/
    `CFDateIntervalFormatter.c`, excluded from the original scope note
    specifically because Locale/Calendar didn't exist yet — they do
    now), `CFURLComponents.c`/`CFURLComponents_URIParser.c`/
    `CFURLAccess.c`, `CFAttributedString.c`, `CFRegularExpression.c`,
    `CFRunArray.c`, `CFStringTransform.c`, `CFPlatformConverters.c`,
    `CFUnicodePrecomposition.c`, `CFSystemDirectories.c`, and
    `CFApplicationPreferences.c`. **37 more real files, bringing the
    total to 94** (up from 57), every one compiling with zero errors
    except two needing a one-line real fix each (both compat-header
    additions, no source patches): `CFBundle_Binary.c`'s real dlfcn
    introspection call `dyld_image_path_containing_address` (added to
    the existing `cf_compat_headers/mach-o/dyld_priv.h` stub), and
    `CFSocket.c`'s `#include <libc.h>` (a real macOS-only umbrella
    header the iPhoneOS SDK never shipped — new
    `cf_compat_headers/libc_umbrella/libc.h` re-including just the
    specific standard headers it needs).

    Removed all now-superseded `cf_class_stubs.c` `CF_STUB_CLASS`
    entries except one — `__CFPreferencesDomainClass` is the only class
    stub left, since `CFPreferences.c` itself has a real, genuine,
    multi-part compile failure (see below) unlike everything else on
    the original stub list.

    **Five real, verified (not assumed) exclusions remain at this
    point** (see below — 2026-08-27's libxml2 pass closed one of them,
    `CFXMLInterface.c`, down to four), each a confirmed compile
    failure, not a scope guess:
    - `CFUserNotification.c` (AppServices.subproj): real GUI
      alert-dialog API, `kCFUserNotificationIconURLKey`/`SoundURLKey`/
      `LocalizationURLKey` are hard `unavailable: not available on iOS`
      SDK errors. No GUI on this project's boot target regardless.
    - `CFPreferences.c` / `CFXMLPreferencesDomain.c`
      (Preferences.subproj): real, multi-part gap — `gethostuuid`
      marked unavailable on iOS behind a `#if TARGET_OS_MAC` guard that
      doesn't actually exclude iOS (`TARGET_OS_MAC` means "any Apple
      OS," not "specifically macOS" — a real bug in this exact
      checkout, or at least a guard that doesn't do what it looks like
      it does), plus a missing CFUUID-family declaration and an
      undeclared `prefDir`; `CFXMLPreferencesDomain.c` separately hits
      "Dont know how to define sleep for this platform" in its own
      `#if` chain. Real preference-file storage
      (`CFPreferencesCopyAppValue` et al) stays unimplemented as a
      result — `CFApplicationPreferences.c` (built, clean) can't close
      that gap alone since it calls into `CFPreferences.c`'s own real
      functions.
    - `CFURLSessionInterface.c` (URL.subproj): real libcurl dependency
      (`curl/curl.h` not found). No `CFRuntimeClass` and nothing else
      in the checkout references its symbols (unlike
      `CFUserNotification.c` above, which needed a real stub) — a real
      libcurl port doesn't just close a compile gap, it's a
      **prerequisite for any real `NSURLSession`-style networking at
      all**, tracked as real future work below, not stubbed, since
      there's genuinely nothing to stub.
    - `CFWindowsUtilities.c` (Base.subproj): Windows-only, structurally
      inapplicable.

    **libcurl investigated 2026-08-27, real future-work item filed, not
    pursued this pass**: Apple did open-source it
    (`apple-oss-distributions/curl`, `macos-112` pins `curl-121.40.2`,
    confirmed via `git ls-remote`/manifest same as every other pin).
    Real, hard wall found before any build attempt: Apple's own real
    iOS/macOS build (`README.APPLE`) configures with `--with-darwinssl`
    (Secure Transport) for TLS — closed-source, this project has no
    equivalent, and per the standing crypto-scope decision (P2 item 0a)
    real crypto work here goes through corecrypto/AES, not
    OpenSSL/LibreSSL/etc, so swapping in a different TLS backend isn't
    in scope either. `--without-ssl` would let curl *compile* (real,
    standard, supported configure option), but real HTTP networking at
    runtime needs `socket`/`connect`/`bind`/`getaddrinfo`/etc — the
    exact same real BSD-socket-syscall gap already documented above for
    `CFSocket.c`/libxml2's own disabled `nanohttp.c`, which this project
    has no wrapper for anywhere yet. Net assessment: building curl now
    would only close one more *compile-time* file (mirroring
    libxml2/`CFXMLInterface.c`'s own "just needs the library to exist"
    situation), not enable any real functioning network client — a
    real, substantial, separate lift (implement real socket syscalls,
    then decide on a real TLS backend) for a much smaller immediate
    payoff than libxml2 had. **Real prerequisite order for whoever picks
    this up**: (1) real BSD socket syscall wrappers in userland libc
    (needed by `CFSocket.c`/`CFRunLoop.c` too, so likely worth doing
    once for both consumers, not curl-specific), (2) a real TLS backend
    decision (LibreSSL/BoringSSL/mbedTLS are real, portable,
    non-corecrypto options if a from-scratch TLS stack is ever wanted;
    `--without-ssl` HTTP-only curl is a smaller intermediate step that
    needs only (1)), (3) then a real `build_libcurl.sh` following
    `build_libxml2.sh`'s own template (check for a pregenerated
    `config.h` first, same as both ICU and libxml2 had).

    **Real gap count for this trial link is now 101 symbol groups**
    (up from 83, not down) — an honest tradeoff, not a regression: the
    newly-included RunLoop/Socket/MachPort/Bundle subsystems pull in
    their own real, previously-invisible transitive dependencies this
    project's userland stack doesn't expose yet anywhere (confirmed via
    `nm` against every existing archive, not assumed) — real Mach port
    syscalls (`mach_port_allocate`/`_construct`/`_destruct`/
    `_get_attributes`/`_insert_member`/`_extract_member`/`_type`), real
    BSD socket syscalls (`socket`/`bind`/`connect`/`listen`/`accept`/
    `recv`/`recvfrom`/`sendto`/`getsockname`/`getpeername`/
    `getsockopt`/`setsockopt`/`socketpair`), real Mach timer syscalls
    (`mk_timer_create`/`_arm`/`_cancel`/`_destroy`, CFRunLoop's timer
    sources), real `pthread_attr_*` thread-creation-attribute calls,
    `NXFindBestFatArch`/`NXGetLocalArchInfo` (CFBundle's fat-binary
    architecture detection), and `sysdir_start_search_path_enumeration`/
    `_get_next_search_path_enumeration` (CFBundle's real search-path
    API). None of these exist in any archive this project has already
    built (confirmed via `nm`, not assumed) — real follow-up work, not
    a quick link-order fix, and not chased further this session. Plus
    the previously-documented, still-not-root-caused os_log/dispatch
    gap and the already-categorized libSystem.B.dylib export-parity/
    still-missing-everywhere buckets.

    **libxml2 DONE 2026-08-27, closing `CFXMLInterface.c`** (one of the
    five exclusions above — down to four now). Investigated whether
    libxml2/curl were portable enough to close the last two real
    external-dependency exclusions; libxml2 turned out to be a real,
    easy win, curl a much bigger, murkier one (Apple's real iOS build
    uses closed-source Secure Transport for TLS, which this project
    doesn't have and isn't adding per the standing corecrypto/AES-only
    crypto scope decision — `--without-ssl` would compile but real
    networking needs socket syscalls this project doesn't have either,
    so curl wasn't pursued). Pinned `libxml2-34.9` (matches both the
    `macos-112` manifest and, confirmed via `LIBXML_DOTTED_VERSION`, the
    Xcode 12 iPhoneOS SDK's own bundled headers exactly — real upstream
    libxml2 2.9.4 either way). Unlike ICU, no need to run `configure` at
    all: Apple's checkout ships a real pregenerated `Pregenerated
    Files/include/config.h` (a real `configure` run's output, checked in
    for their own Xcode-based build).

    The earlier "conflicting types" compile failure blocking
    `CFXMLInterface.c` was never a real libxml2 dependency at all — a
    real, tiny, unrelated bug in this exact swift-corelibs-foundation
    checkout (`CFXMLInterface.h` declares 3 functions returning
    `Boolean`, `CFXMLInterface.c` defines them returning `bool`), fixed
    via a second commit in the existing `swift-corelibs-foundation
    .patch` (same category as that patch's first, already-existing
    fix). `tools/userland_staging/build_libxml2.sh` builds real libxml2
    as a per-library dylib (45 real source files, all compiling clean
    with Apple's real pregenerated headers) linked into
    `build_corefoundation.sh`'s dynamic trial link — introduced **zero
    new gaps** there, confirmed via a real trial link before and after.

    Real gaps found and fixed getting the dylib itself to link clean
    (all in `build_libxml2.sh`'s own header comment): disabled
    `LIBXML_HTTP_ENABLED`/`FTP_ENABLED` (real network transport code,
    `nanohttp.c`/`nanoftp.c` — needs `socket`/`connect`/`bind`/
    `getaddrinfo`/etc, the same real gap already documented for
    `CFSocket.c` above) and `LIBXML_ZLIB_ENABLED`/`HAVE_ZLIB_H` (real
    `gzopen`/`gzread`/etc compressed-I/O — this project's vendored
    zlib has no `gzFile` family) by editing the *copied* pregenerated
    headers, not Apple's checked-in files (same pattern
    `build_icu_target.sh`'s own `DATASUBDIR` fix already used); two
    more real `<mach-o/dyld_priv.h>` functions needed adding to the
    existing compat stub (`dyld_get_program_sdk_version` +
    `DYLD_MACOSX_VERSION_10_9`, a real, historically-stable constant
    confirmed against an older real `apple-oss-distributions/dyld` tag
    since this project's own pinned dyld-940 already replaced it);
    `Libc/secure/{memmove,strncat,strncpy,stpncpy}_chk.c` (already-
    established pattern, reused not reinvented); real `labs`/`rand_r`
    (trivial, zero-risk, hand-implemented) and a real from-scratch
    `log10()` (this project has never had any Libm — see
    `cf_compat_headers/libm_compat.c`'s own header for the same `fmod`/
    `pow` gap, reused unmodified here since libxml2's own real call
    sites match those functions' existing documented scope) using a
    real IEEE-754 exponent extraction + atanh-series mantissa
    approximation, verified against real libm to ~1e-10 accuracy with a
    standalone host-side test before being wired into the real build —
    catching and fixing one real, self-inflicted bug along the way
    (brace-initializing a union initializes its first member, not
    whichever member matches the initializer's type; the fix assigns
    `.bits` explicitly instead). `_time`/`_sched_yield` (real syscalls
    with no wrapper anywhere in this project yet) were deliberately
    deferred via the established per-symbol `-Wl,-U,<name>` mechanism
    rather than implemented, matching this port's overall "defer real
    syscall work, don't fake it" boundary. Also caught and fixed a real
    build-script bug of its own: the first dylib link attempt produced
    a "clean" but nearly-empty dylib (~33 symbols, none of libxml2's own
    API) because `-force_load` was missing — without it, ld64's normal
    archive semantics only pull `.a` members needed to satisfy an
    already-outstanding reference, and nothing else in that link
    actually called any real libxml2 function yet. Confirmed fixed via
    `nm -g` showing real exports (`xmlParseFile`, `xmlNewDoc`,
    `xmlDocGetRootElement`, etc, ~2000 total) after adding it.

    **Real syscall work started 2026-08-27 — 27 of the 100 remaining
    gap-groups closed** (100→73), all via real hand-written raw
    syscall/Mach-trap wrappers or verbatim-extracted real Apple
    functions, same established per-consumer-wrapper-file convention
    this project already uses (`ifconfig_extra_syscalls.c`/
    `libresolv_socket_syscalls.c`):
    - **BSD socket family (12 symbols) DONE**:
      `cf_socket_syscalls.c` — `socket`/`bind`/`listen`/`accept`/
      `connect`/`getsockname`/`getsockopt`/`setsockopt`/`socketpair`/
      `recvfrom`/`sendto`/`recv`, real syscall numbers cross-checked
      against `syscalls.master` (using the `_nocancel` trap numbers
      directly for `accept`/`connect`/`recvfrom`/`sendto`, matching
      real Apple's own `libsyscall/wrappers/legacy/*.c`, which do the
      same — this project has no real pthread cancellation to make the
      cancelable variant meaningfully different anyway).
    - **`mach_port_*` family (7 symbols) DONE**: `cf_mach_port_syscalls
      .c` — `mach_port_allocate`/`construct`/`destruct`/
      `extract_member`/`get_attributes`/`insert_member`/`type`,
      extracted verbatim from `libsyscall/mach/mach_port.c` (each
      citing its real origin line) rather than compiling that whole
      file — its other functions (`move_member`/`insert_right`/`guard`/
      `unguard`/`request_notification`/`mach_voucher_extract_attr_recipe`)
      pull in a genuinely different, unneeded MIG subsystem
      (`mach_voucher.defs`). Each function tries a real fast-path Mach
      trap first (`mach_port_traps.s`, 7 real trap numbers cross-checked
      against `syscall_sw.h`, confirmed backed by a real kernel
      implementation in `osfmk/ipc/mach_kernelrpc.c`) and only falls
      back to full MIG RPC on `MACH_SEND_INVALID_DEST` — since the real
      trap always succeeds on this project's real from-source kernel,
      that fallback is dead code in practice, so it's an honest,
      documented stub (`cf_mach_port_mig_fallback_stub.c`, returns
      `KERN_NOT_SUPPORTED`) rather than the real MIG marshaling, which
      would need this project's still-unbuilt real `mach_msg()` (item 7
      below — a real, substantial, already-tracked separate task, not
      something to pull in as a side effect of `mach_port_allocate`).
      Real fix once `mach_msg()` lands: replace the stub with the real
      `mig`-generated `mach_portUser.c` (already proven to compile
      clean against this project's headers during this investigation —
      same real recipe as `build_corefoundation.sh`'s own
      `mach_vm_region` precedent).
    - **`mk_timer_*` family (4 symbols) DONE**: `mk_timer_traps.s` —
      plain Mach traps, no MIG fallback at all (confirmed absent from
      real Apple source), real trap numbers `-91`/`-92`/`-93`/`-94`.
    - **`pthread_attr_*` (4 of 6 symbols) DONE**:
      `cf_pthread_attr_syscalls.c` — `pthread_attr_init`/`destroy`/
      `setdetachstate`/`setscope`, extracted verbatim from
      `libpthread/src/pthread.c` (pure struct-field manipulation, zero
      syscalls, no real dependency on that file's own excluded
      `pthread_create`-stack-allocation header conflict). Needed the
      real internal `pthread_attr_t` struct (named fields like `.sig`/
      `.detached`) from `libpthread/src/internal.h`, not the public
      SDK's opaque one — confirmed the hard way (first attempt using
      plain `<pthread.h>` failed with "no member named 'sig'"), not
      assumed. `pthread_attr_set_qos_class_np`/`qos_class_main` remain
      real gaps: both live in `libpthread/src/qos.c`, already
      documented by `build_libpthread.sh` as unreachable ("needs real
      `bsdthread_register`") — a real, already-diagnosed kernel-
      registration wall, not chased further here.

    **`_proc_pidinfo`/`_pthread_setname_np`/`NXFindBestFatArch`/
    `NXGetLocalArchInfo` CLOSED 2026-08-27** (real twofer + real easy
    win, 4 symbol groups, 73 -> 70 net — see below for the 1 new gap
    this surfaced): `_proc_pidinfo`/`_pthread_setname_np` both needed
    one real `__proc_info` raw syscall wrapper, generated fresh via
    `create-syscalls.pl` against xnu-7195's real `syscalls.master` (same
    recipe `build_libxpc_syscalls.sh` already used for this exact
    symbol) — `cf_libproc_syscalls.c` (real `proc_pidinfo`, extracted
    verbatim from `libsyscall/wrappers/libproc/libproc.c:100-110`) and
    `cf_pthread_setname_syscalls.c` (real `pthread_setname_np`,
    extracted verbatim from `libpthread/src/pthread.c:1130-1153`, same
    "whole file has an unrelated real header conflict, extract just this
    function" pattern as `cf_pthread_attr_syscalls.c`). The latter also
    needed real `_platform_bzero`/`_platform_strlcpy`
    (`third_party/libplatform/src/string/generic/{bzero,strlcpy}.c`),
    adapted rather than compiled as-is: each real file also defines its
    own generic `_platform_memset`/`_platform_memset_pattern4` under the
    same `#if !_PLATFORM_OPTIMIZED_*` this project's arm64 target takes,
    which duplicate-symbol-conflicts with `libmalloc.a`'s already-built
    copies — confirmed via a real trial compile, not assumed. `sys/
    proc_info.h`/`sys/event.h` are deliberately NOT included by either
    new file: pulling them drags in `struct kevent_qos_s`
    (`PRIVATE`-gated, stripped from this project's exported userspace
    headers) for proc_info flavors this extraction doesn't use — real
    Apple's own `pthread.c` takes the same raw-literal-call-number
    shortcut for this exact call, confirmed by reading it, not guessed.
    `NXFindBestFatArch`/`NXGetLocalArchInfo` (CFBundle's fat-binary
    detection) needed a new pin, real `cctools`, NOT part of the
    `macos-112` manifest at all (`cctools` ships with Xcode/the Command
    Line Tools, not the OS distribution — confirmed via a real
    `git show macos-112:.gitmodules`) — pinned instead to
    `cctools-973.0.1`, matched against this project's own pinned Xcode
    12's real `otool --version` output ("Apple Inc. version
    cctools-973.0.1"), not guessed by date proximity. Only
    `libmacho/arch.c` needed, real and unmodified, confirmed by a real
    trial compile to need nothing this project doesn't already have
    (`host_info`/`mach_host_self` from `libmach_ipc.a`,
    `mach_port_deallocate`/`mach_task_self_` from `libmalloc.a`) — zero
    new gaps from this one. See `setup_third_party.sh`'s own header
    comment and `build_corefoundation.sh`'s own per-file comments for
    the full detail.

    **`libSystem.B.dylib` real `-reexport_library` fix DONE 2026-08-27
    — 70 -> 42, the single biggest gap-closing change of this whole
    item d7 effort.** The `__pthread_ptr_munge_token` gap the twofer
    above surfaced turned out to be a symptom of something much bigger:
    `build_libsystem.sh`'s final `libSystem.B.dylib` link passed every
    real per-library sub-dylib (`libsystem_kernel`/`_platform`/`_blocks`/
    `_malloc`/`_pthread`/`_c`.dylib, `libc++.dylib`) as a plain linker
    input, never `-Wl,-reexport_library` — confirmed via a real `nm -gU`
    that `libSystem.B.dylib` exported only its own ~19 locally-compiled
    symbols and re-exported nothing from any sub-dylib at all (not even
    `_strlen`; CF's own dynamic trial link only ever resolved libc
    symbols because it separately links the curated static archives
    directly, alongside — not through — `libSystem.B.dylib`). Real
    macOS's own `libSystem.dylib` is itself exactly this kind of thin
    `LC_REEXPORT_DYLIB` umbrella (this project's own header comment on
    `build_libsystem.sh` already said so, just hadn't been carried
    through to the actual link command after the static-to-dylib
    migration split sub-libraries out). Fix: swap the 7 plain positional
    dylib args for `-Wl,-reexport_library,<path>` each. Verified for
    real, not assumed: a real ld64-609 smoke test first
    (`-reexport_library` correctly makes an umbrella's linker-level
    symbol resolution succeed for a symbol only defined in the
    re-exported sub-dylib, confirmed via a real failing-in-a-different-
    way link, not a guess about semantics), then the actual rebuild —
    `nm -u` on the new `libSystem.B.dylib` is BYTE-IDENTICAL to the old
    one (zero regressions, confirmed via a real diff), `otool -l` shows
    all 7 real `LC_REEXPORT_DYLIB` commands, and CF's own real dynamic
    trial link dropped from 70 to 43 in one rebuild — closing
    `_pthread_atfork`/`_pthread_getugid_np`/`_pthread_is_threaded_np`/
    `_pthread_threadid_np`/`_pthread_mutexattr_destroy`/
    `_pthread_attr_set_qos_class_np`/`_qos_class_main` (the whole
    pthread export-parity bucket at once) plus `_abs`/`_atol`/`_bsearch`/
    `_llabs`/`_mkdir`/`_pwrite`/`_qsort`/`_readlink`/`_setenv`/
    `_setrlimit`/`_strstr`/`_getegid`/`_getrlimit`/`_getpeername`/
    `_uuid_generate_random`/`_uuid_generate_time`/`___vsprintf_chk`/
    `___darwin_check_fd_set_overflow`/`_select$DARWIN_EXTSN`/`_time` — 27
    symbol groups in one real structural fix, confirming the "probably
    closes several export-parity entries at once" prediction. The one
    straggler, `__pthread_ptr_munge_token` itself, needed a second,
    separate real fix: a real trial (adding a genuine cross-object
    reference from `pthread_tsd.o`, then a real `-exported_symbols_list`
    wildcard override) both failed to change ld64's resolution of that
    *specific* common/tentative data symbol to LOCAL inside
    `libsystem_pthread.dylib`'s own link (confirmed via `nm`, still `s`
    not `S` either way) — a real, narrower ld64 default-visibility
    behavior for common symbols resolved entirely within one link unit,
    not an export-list problem. Real Apple never needed this symbol
    exported either (private `pthread.c` implementation detail, never a
    cross-image dependency on real macOS); this project's own new
    `pthread_setname_np` extraction was the first real consumer needing
    it externally. Fix: dropped the `_pthread_validate_signature(self)`
    corruption-check call from `cf_pthread_setname_syscalls.c` (not part
    of the actual rename logic) — same real "no corruption path exists
    on this project's simplified threading model, the defensive check
    that assumes one is safely skippable" pattern already established by
    `pthread_thread_ops_compat.c`. 43 -> 42.

    **`sysdir_start_search_path_enumeration`/
    `sysdir_get_next_search_path_enumeration` CONFIRMED never
    open-sourced 2026-08-27** (not merely "not yet investigated"): real
    `apple-oss-distributions/Libc`'s FULL git history (every tag through
    `Libc-1592.100.35`, not just the pinned `Libc-1439.40.11`) has no
    `gen/sysdir.c` and never has; no standalone `sysdir`/`libsysdir` repo
    exists in the org either (checked via `git ls-remote`); a real GitHub
    code search across the whole `apple-oss-distributions` org for
    `sysdir_start_search_path_enumeration` (via `gh api search/code`,
    not guessed) turns up only real CALLERS (`system_cmds`, `configd`) —
    zero definitions anywhere. Same category as `_asl_*` below: real,
    confirmed, out of scope, not a source-location problem to keep
    chasing.

---

## P2, item 0b (crypto: real SHA-256) — full writeup archived 2026-08-27

`docs/backlog.md`'s current P2 item 0b condenses this down to a short
DONE pointer. This is the full investigation, preserved for the real
detective work it documents (including a corrected finding partway
through about which mechanism actually needed fixing).

0b. **[DONE 2026-08-27] `ccsha256_di` wired to REAL corecrypto instead of
    the from-scratch `corecrypto_sha2.c`.** Found 2026-08-26, acted on
    2026-08-27. Turned out `register_crypto_functions()`/`g_crypto_funcs`
    was never the right mechanism for this one (see below) — the real
    fix was smaller: `SHA256_Init`/`_Update`/`_Final`
    (`libkern/crypto/corecrypto_sha2.c`) now call real
    `ccdigest_init`/`_update`/`_final` + `ccsha256_di()` directly, the
    same real corecrypto entry points `osfmk/prng/prng_random.c`'s
    `bootseed_init()` already used successfully. SHA-384/512 deliberately
    left on the from-scratch path (see below, unchanged). QEMU-verified:
    real login, real shell, real `/bin/ls`/`md5` exec (each real Mach-O
    exec runs `cs_hash_sha256` for code-signing), no panic — commit
    `ceb6d419c` in the `xnu-7195` checkout.

    **Correction to the original plan below**: `SHA256_Init` et al were
    never `g_crypto_funcs`-backed in the current tree (only an *earlier*,
    already-superseded revision of this file was) — `cs_hash_sha256`,
    `kern_exec.c`'s cdhash hashing, and `chunklist.c` all call
    `SHA256_Init`/`_Update`/`_Final` directly with zero table indirection.
    So `register_crypto_functions()` wiring, as originally scoped just
    below, would not actually have touched any of those real call sites —
    filed as a real, corrected finding, not a rewrite of the history below
    (kept for the real detective work it documents).

    The SHA256_Init row above says "from-scratch SHA-2, bypasses the table"
    on the assumption (from an earlier pass) that corecrypto's digest-core
    bodies were never open-sourced — that check only looked in
    `EXTERNAL_HEADERS/corecrypto/` (headers only, genuinely empty of
    bodies). Real, unmodified, APSL-licensed corecrypto digest-core source
    is actually sitting one directory over at
    `third_party/xnu-7195/osfmk/corecrypto/`: `ccdigest_init.c`,
    `ccdigest_update.c`, `ccdigest_final_64be.c`, `ccsha256_di.c`,
    `ccsha256_initial_state.c`, `ccsha256_K.c`, `ccsha256_ltc_compress.c`,
    `ccsha256_ltc_di.c` (plus `cchmac*.c`). These are marked `standard` in
    `osfmk/conf/files` (lines 338-354) — **already compiled into this
    exact kernel binary** — and `osfmk/prng/prng_random.c:128` already
    calls the real `ccdigest_init()` from this same file set for the
    kernel's own PRNG. Only a `register_crypto_functions()` boot-time call
    filling in `ccsha256_di` from this real source was ever missing, not
    the whole SHA-256 implementation.

    **Why this matters**: `cs_hash_sha256` (`bsd/kern/ubc_subr.c:185-191`,
    code-signing hash dispatch), cdhash/boot-session-UUID hashing
    (`bsd/kern/kern_exec.c:5607-5612`, runs on every real exec), and
    software-update chunk integrity verification (`bsd/kern/
    chunklist.c:249-257`) all currently run on the hand-rolled
    `corecrypto_sha2.c` instead of Apple's real, audited implementation
    that is already linked into this kernel and one `register_crypto_functions()`
    call away from being live.

    **Real scope for whoever picks this up**: wire `register_crypto_functions()`
    to populate `g_crypto_funcs->ccsha256_di` with the real `ccsha256_di()`
    from `osfmk/corecrypto/ccsha256_di.c`, rebuild, and boot-verify
    (code-signing/exec paths are hit on every boot, so a real QEMU boot
    test is a real, cheap verification here — this is a kernel change on a
    security-relevant path, run the project's adversarial-review-before-
    commit convention on it). SHA-384/SHA-512 real corecrypto bodies do
    **not** exist in this project's primary `xnu-7195` checkout (only in
    the separate, not-currently-built `third_party/xnu-latest`) — leave
    those on the from-scratch path for now, this item is SHA-256 only.
    MD5/SHA-1 real corecrypto bodies are confirmed absent from both
    checkouts (headers only, no bodies) — `corecrypto_md5.c`'s from-scratch
    implementation is a genuine last resort, not part of this item.

---

## P2 — misc resolved items archived 2026-08-27

Four items from `docs/backlog.md`'s P2 numbered list (regex, setattrlist,
SD image size default, less/more) that were fully or mostly resolved.
`docs/backlog.md` keeps short pointers for the two with a small real
remainder (grep/sed-family commands not yet built on top of the regex
engine; `touch -h` not yet revisited); the SD image size fix and the
less/more verification pass had nothing left, so they're archived here
in full.

**[RESOLVED 2026-08-20]** ~~No real POSIX regex (`regcomp`/`regexec`/
`regfree`) built~~ — real, unmodified Apple TRE engine
(`third_party/Libc/regex/TRE/lib/*.c` + `FreeBSD/regerror.c`) now
builds clean into `libc_build/libc_regex.a` via the new, permanent
`tools/userland_staging/build_libc_regex.sh`, and is linked into
`bash.macho` (`build_bash.sh`), replacing `bash_syscalls.c`'s old
`REG_BADPAT`/`REG_NOMATCH` stand-ins. QEMU-verified: real bash
`[[ str =~ pattern ]]` performs genuine matches (both positive and
negative cases confirmed correct). `rm -i`'s `rpmatch()` was
deliberately left on its existing narrow C-locale-only replacement
(`simple_cmds_rpmatch_compat.c`) — that file's own comment already
establishes it's exactly equivalent real behavior in this project's
always-C locale, not a degraded approximation, so there was nothing
to gain by rewiring it.

**[RESOLVED 2026-08-20]** ~~`setattrlist(2)` never wired to
userland~~ — real raw syscall wired in `tools/userland_staging/
cp_mv_syscalls.c` (Block 1 of the 2026-08-20 command-sweep session),
used for real by `cp -p`'s `lchmod`/`lchflags` path and `chmod`'s ACL
machinery, QEMU-verified.

**[FIXED 2026-08-21]** `tools/make_sd_image.sh`'s `SIZE_MB` default
is now `1200` (was `500`, silently below the 1024 MiB SDHC/SDSC
threshold — see below for the real mechanism). Also added a real
fail-fast check: the script now exits with a clear error if invoked
with `SIZE_MB` at or below 1024, instead of silently building an
image `BCM2837SDHCI` refuses much later at card-init time. 1200 (not
the originally-considered 4096) deliberately keeps a wide safety
margin above 1024 while minimizing attach/partition/detach time on
every test iteration -- picked once the exact QEMU-side mechanism
was confirmed (see below), not guessed.

Original finding, kept for context: found while QEMU-testing the
`ls`/`find`/`date`/`gzip` session: `qemu/hfs_root_test.img`
regenerated at the buggy 500 MB default puts QEMU's emulated SD card
into SDSC (CSD v1) mode -- confirmed exactly why by reading
`third_party/qemu-zhuowei/hw/sd/sd.c`: `if (sd->size > 1 * GiB)` is
literally what selects SDHC vs SDSC in the emulated card, based on
the backing image file's own size, not any property of a real
card. `BCM2837SDHCI` (`iokit/BCM2837SDHCI/BCM2837SDHCI.cpp`, ~line
590-605) explicitly refuses SDSC cards outright (never implemented
that CSD version's older capacity formula) -- exercised live as
`no card identified at start()`, boot hangs forever at "Still
waiting for root device". A real, reproducible, silent-failure-mode
bug (produced a normal-looking image that only failed much later, at
root-mount time) -- the new fail-fast check closes that class of
mistake going forward, not just this one instance of it.

**[RESOLVED 2026-08-21, one real scope limit remains]** ~~`less`/
`more` interactive behavior only partially verified~~ — a follow-up
one-off timing-based pty test (RETURN/space/`q` sent on a fixed
schedule, not depending on the shared harness's prompt-string
matching, since `less`'s own real `(press RETURN)` prompt isn't in
its `PROMPT_SUFFIXES` list) confirmed: RETURN correctly advances
past the hardcopy-mode prompt and prints the file header, and `q`
correctly exits back to the shell with a real exit code 0. `less`/
`more`'s launch → degraded-mode-detection → quit lifecycle is real
and QEMU-verified end to end. Remaining real, honest scope limit
(not a bug): this project has no real termcap/terminfo database, so
`less` always runs in real hardcopy mode on this target — see
`docs/backlog.md`'s own item 16 for the live remainder of this note.

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
