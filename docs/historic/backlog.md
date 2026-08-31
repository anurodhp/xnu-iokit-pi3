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


### Condensed 2026-08-27 summaries of items 1/2/4/5 (archived verbatim 2026-08-30)

These are the shorter, in-place summaries `docs/backlog.md` carried for
items 1/2/4/5 between the 2026-08-27 archive pass and the 2026-08-30 pass
that collapsed them to one-line pointers. Kept verbatim so nothing from
that intermediate wording is lost; the fuller writeup is above.

1. **[FIXED 2026-08-27, QEMU-verified]** `BCM2837SDHCI::doAsyncReadWrite()`
   had no serialization at all — no `IOCommandGate`, no workloop, no
   mutex. Two concurrent requests (plausible now that the SD image has
   two mounted partitions) could interleave register writes and corrupt
   `fPendingIntStatus`. Fixed with `fTransferLock` (a plain `IOLock`) held
   for the whole command-issue-through-completion span — sufficient
   because `doAsyncReadWrite()` is already synchronous under the hood
   (blocks on `waitForIntStatus32()` rather than completing from
   interrupt context). **Verification caveat**: nothing in this project
   currently issues genuinely concurrent I/O, so the QEMU boot test
   confirms no regression, not that the actual interleaving race is
   exercised. Full writeup (exact reasoning, `fTransferLock` placement)
   archived to `docs/historic/backlog.md`.

2. **[FIXED 2026-08-27, QEMU-verified]** No error recovery anywhere in
   `BCM2837SDHCI` — every error path was `IOLog(); return false;`. Fixed
   with `resetCmdLine()`/`resetDatLine()` (SDHCI spec 3.7.1
   `SOFTWARE_RESET_CMD`/`_DATA`, matching Linux's `sdhci_reset()` split),
   wired into every real error/timeout path in `sendCommand()`/
   `readBlocksPIO()`/`writeBlocksPIO()`. **Verification caveat**: QEMU's
   SD model still doesn't inject real CRC/timeout/card-removal errors, so
   the boot test confirms the reset paths compile and don't regress a
   clean boot, not that they trigger correctly under a real error. Full
   writeup archived to `docs/historic/backlog.md`.

4. **[FIXED 2026-08-27, QEMU-verified]** No power-on ramp / 74-clock delay
   before `CMD0`, and power was enabled *after* the clock in `start()`
   rather than before. Fixed by reordering `start()` to assert
   `kRegPowerControl` before `setClock()`, plus real `IODelay()`s for the
   ≥1ms power-on ramp (spec 6.4.1) and ≥74-clock identification delay.
   Only changes ordering/idle delays, not signal rate/timing, so unlike
   item 3 there's no way a wrong guess here makes things worse on real
   hardware. QEMU has no power/clock-dependent reset behavior to catch
   the old ordering being wrong, so this is verified only as "boots
   clean, no regression." Full writeup archived to
   `docs/historic/backlog.md`.

5. **[FIXED 2026-08-27, code correct but genuinely unverifiable from
   QEMU — exactly as this item originally predicted.]** No memory barrier
   in `BCM2837InterruptController::enableVector()`/`disableVectorHard()`
   before the next `IRQ_PENDING` read. Fixed with
   `__asm__ volatile ("dsb sy" ::: "memory")` after each register write,
   matching the barrier style `BCM2837DWC2.cpp` already uses. Genuinely
   unverifiable from QEMU/TCG (all MMIO is strongly ordered there
   regardless of the barrier's presence) — the boot test only confirms
   the added instruction doesn't itself break anything. Full writeup
   (including a correction to this item's own original "no barriers
   anywhere" claim) archived to `docs/historic/backlog.md`.

**On items 1, 2, 4, 5 above**: all four were rebuilt together
(`tools/build_kexts.sh` clean compile, then a full `tools/
build_xnu_7195.sh` kernel rebuild) and verified with one real QEMU boot
(`tools/boot_test.py --login --cmd 'ls /' --cmd 'cat /etc/hosts' --cmd
'ls /System'`, `--expect-fail 'panic\('`) — real boot, real login, real
reads against both mounted partitions (`disk0s1` root / `disk0s2` boot),
zero panics, `BCM2837SDHCI` log confirms a real hardware interrupt still
observed and the card still identifies correctly (RCA=0x4567, SDHC,
1200MB). This confirms no regression from any of the four changes
together; it does not and cannot exercise the specific fault conditions
items 1/2/5 exist to guard against (concurrent I/O, a real card/bus
error, or a real MMIO-ordering violation), since QEMU reproduces none of
those - noted individually above rather than glossed over.

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



---

# Third archive pass, 2026-08-30

Verbatim archive of items from `docs/backlog.md` that were marked
DONE/RESOLVED/CLOSED and verified, moved here on 2026-08-30 once that file
had grown back to ~2670 lines. Nothing below is edited, summarized, or
reworded — only relocated. Each archived item still has a one-line status
pointer under its original ID in `docs/backlog.md`, so every `item dN` /
`item 0a` reference from source comments and other docs still resolves
there first.

Archived here: **d7** (CoreFoundation + ICU + libxml2, current-state
detail), **d8** (static-archive-vs-dylib debt, both RESOLVED passes),
**d9** (`build_simple_cmds.sh` static→dynamic conversion), **0a** (real
AES), **0b** (real SHA-256/384/512/MD5), and **d11**'s session-2-through-5
investigation trail (all of it superseded by
`docs/launchd-bug6-postmortem.md` / `docs/launchd-pid1-postmortem.md` —
its "second thread"/`TPIDRRO_EL0`/`SP_EL0` framings are known wrong;
read those postmortems first). The same pass also collapsed P0 items
1/2/4/5 to short pointers in `docs/backlog.md`; their intermediate
2026-08-27 summary wording is archived verbatim in the "P0, items 1/2/4/5"
section above, under its own "Condensed 2026-08-27 summaries" subheading.

Items still open (P0#3, P0#6, P0#7, all of P1, d1-d5, d10, d12-d18, the
`### d10`/`### d11` sections, and P3) were left in place in
`docs/backlog.md`.

---

## P2, item d7 — [DONE 2026-08-26/27] current-state detail archived

d7. **CoreFoundation + real ICU + real libxml2 ports DONE 2026-08-26/27
    — CF dynamic trial link down to 42 undefined-symbol groups, from a
    starting count of 153.** Real, unmodified Apple source throughout:
    `third_party/swift-corelibs-foundation` (Apple's own actively-
    maintained CF fork — NOT the 2014-dated `apple-oss-distributions/CF`,
    which isn't in the `macos-112` manifest at all), real ICU 66.1
    (`third_party/ICU`, real two-stage host+arm64-apple-ios14.4 cross
    build), real libxml2-34.9. 94 real CF source files build clean into
    `libCoreFoundation.a` (`build_corefoundation.sh`) — String/
    Collections/NumberDate/Parsing/RunLoop/Stream/PlugIn(Bundle)/
    DateFormatter/URL/AttributedString/RegularExpression, plus
    ICU-backed Locale/Calendar/CharacterSet. `libSystem.B.dylib`'s
    missing `-Wl,-reexport_library` (it was plain-linking its 7
    sub-dylibs, exporting almost nothing through the umbrella — real
    macOS's own `libSystem.dylib` is exactly this kind of thin reexport
    umbrella) turned out to be the single biggest gap-closing fix in
    this whole effort once found, closing 27 gaps in one rebuild. Full
    session-by-session build/port narrative (the ICU two-stage
    cross-build story, the CFBundle 30-for-3 trade that got reverted
    then un-reverted once the dependency turned out already-buildable,
    the CFError/CFDate/CFURL "just try compiling it" sweep, every
    individual syscall wrapper closed along the way, etc) archived
    verbatim to `docs/historic/backlog.md` — this entry only tracks
    current state.

    **Real, confirmed compile-time exclusions (4, not build gaps — each
    individually investigated and confirmed structural)**:
    - `CFUserNotification.c`: real GUI alert-dialog API, hard
      `unavailable: not available on iOS` SDK errors. No GUI on this
      target regardless.
    - `CFPreferences.c`/`CFXMLPreferencesDomain.c`: **CLOSED 2026-08-28,
      stale below** — both now compile and link for real (three real
      `TARGET_OS_OSX`/`TARGET_OS_MAC` guard mismatches, not the vaguer
      "gethostuuid unavailable" framing this entry used to have; full
      writeup in `docs/daemon-roadmap.md`'s "Milestone 2 CLOSED,
      2026-08-28" section). Whether `CFPreferencesCopyAppValue` et al are
      *behaviorally* correct against a real flat preferences file on disk
      is a separate, not-yet-QEMU-verified question this pass didn't
      address — but the exclusion itself (this bullet's original claim)
      no longer holds.
    - `CFURLSessionInterface.c`: real libcurl dependency. Apple did
      open-source curl (`curl-121.40.2`, pinned per the manifest), but
      its real build needs closed-source Secure Transport for TLS (out
      of scope per the standing corecrypto/AES-only crypto decision,
      item 0a) — `--without-ssl` would compile, but real HTTP
      networking needs BSD socket syscalls this project's userland
      doesn't wrap yet either. **Real prerequisite order for whoever
      picks this up**: (1) BSD socket syscall wrappers in userland libc
      (shared need with `CFSocket.c`), (2) a real TLS backend decision,
      (3) a `build_libcurl.sh` off `build_libxml2.sh`'s own template.
    - `CFWindowsUtilities.c`: Windows-only, structurally inapplicable.

    **Remaining real gaps in the dynamic trial link, 42 groups** (the
    `-reexport_library` fix closed the entire previous 27-symbol
    export-parity bucket — what's left is qualitatively different,
    genuine "not built anywhere yet" or "never open-sourced" gaps):
    - `_scalbn`, `_getpwuid_r`, `_getsectbynamefromheader_64`,
      `_getsegbyname`: real functions, not yet confirmed built anywhere
      in this project's curated stack — not yet investigated.
    - `___chk_assert_no_overlap`/`___chk_fail_overflow`/`___chk_overlap`:
      real `_FORTIFY_SOURCE`/`_chk`-family hardening routines, no shim
      source located yet (same gap `build_libxpc_syscalls.sh` already
      flagged).
    - `__dispatch_queue_attr_concurrent`/`__mh_dylib_header`/
      `__os_log_debug_impl`/`__os_log_default`/`__os_log_error_impl`/
      `_os_log_create`/`_os_log_type_enabled`: a real, not-yet-root-
      caused os_log/dispatch gap — confirmed genuinely absent from both
      `libdispatch.dylib` and `libSystem.B.dylib` via `nm`.
    - `_asl_*` (6 symbols): **CORRECTED 2026-08-27** — not actually
      out of scope, see the parallel correction under item d6 above.
      The real ASL client library is bundled in
      `apple-oss-distributions/syslog`'s `libsystem_asl.tproj/src/`
      (pinned as `syslog-385`, see `docs/daemon-roadmap.md`'s Phase 6.7
      syslogd section) — genuine, just not yet wired into CF's own
      trial link.
    - `___CFPreferencesAllocator`/`__CFPreferencesDomain*`/
      `_CFPreferencesCopyValue`/`_kCFPreferences*`/`_kCFLocale*`/
      `_kCFDateFormatter*`/`_kCFGregorianCalendar`: real
      `CFPreferences.c`-family gaps, tracked by the exclusion above.
    - `sysdir_start_search_path_enumeration`/
      `sysdir_get_next_search_path_enumeration`: confirmed never
      open-sourced (checked real `Libc`'s full git history plus a
      GitHub code search across the whole `apple-oss-distributions`
      org — zero definitions anywhere, only real callers).

---

## P2, item d8 — [RESOLVED 2026-08-28 / 2026-08-29] full writeup archived

d8. **Every curated support library built since the real per-library-dylib
    convention was announced (`CLAUDE.md`'s "New project convention going
    forward: prefer real, per-library `.dylib`s over new static
    archives", Phase 6.5) has still shipped as a plain `.a` static
    archive, not a `.dylib` — a real, growing gap between stated
    convention and actual practice, not yet reconciled.** Confirmed via
    `ls tools/userland_staging/build_*.sh` + `file libc_build/*.a`:
    `build_libm.sh` (`libm.a`), `build_libnotify_client.sh`
    (`libnotify_client.a`), `build_libresolv.sh` (`libresolv.a`),
    `build_openpam.sh` (`libopenpam.a`), and `build_bsm.sh` (`libbsm.a`,
    2026-08-27/28) were all written and built *after* the convention
    landed, and all five chose `.a` anyway — each one's own header
    comment documents the same real reasoning (a curated support library
    linked into this project's still-dominant Csu-static `-nostdlib
    -static` consumers has no real dyld-based consumer to justify the
    extra dylib-linking complexity yet, unlike `libdyld.dylib`/
    `libsystem_*.dylib`, which exist specifically because real dyld
    structurally requires them). `libbsm.a` is the sharpest case: real
    Apple's own `bsm/lib/Makefile` *already* targets a dylib
    (`libbsm.dylib`, `-dynamiclib -install_name /usr/lib/libbsm.dylib`)
    — this project deliberately chose `.a` anyway, for consistency with
    the other four, not because the real upstream build system does.

    Not a correctness bug (every one of these `.a`s builds clean, links
    clean, and passes its own real smoketest) — filed because the
    convention exists precisely to stop this exact pattern from
    compounding (see `docs/daemon-roadmap.md`'s "Static-vs-dylib
    architecture debt" section for the same concern at the
    `libSystem.B.dylib`-flattening scale), and five real, independent
    instances of "wrote a new `.a` anyway" since the convention was
    announced is a real signal the convention isn't being followed by
    default, not just an occasional deliberate exception. Real follow-up,
    not attempted here: either (a) revisit each of these five as a real
    `.dylib` once/if a real dyld-loaded consumer actually needs one
    (lowest-risk — matches `build_libdyld.sh`'s own "link against real
    sibling dylibs, not their static ancestors" pattern, see item d1
    above for why that matters for correctness, not just style), or (b)
    make an explicit, documented call that `.a` is in fact still correct
    for small curated-support libraries with no standalone runtime role,
    and narrow the convention's own wording in `CLAUDE.md` to say so —
    but don't leave the stated convention and the actual practice
    silently diverging the way they are today.

    **RESOLVED 2026-08-28 — explicit user directive: option (a), no
    narrowing of the convention.** Real dyld-loaded consumers now exist
    for real (item d9 below: all 76 `build_simple_cmds.sh` commands,
    plus this project's real per-library `libsystem_*.dylib`/
    `libdispatch.dylib`/`libc++.dylib`/`libunwind.dylib`/`libxml2.dylib`
    conversions, plus this same day's launchd trial link now linking
    real dylibs throughout) — the "no real dyld-loaded consumer to
    justify it yet" reasoning every `.a`-choosing script above cited is
    no longer true project-wide, even for scripts that individually
    predate a consumer of their own. Standing rule going forward, not
    just for the five libraries above: **do not add a new static archive
    for anything that is a real, standalone, nameable library** (as
    opposed to a per-consumer curated-extraction bundle like
    `launchd_libc_extras.a`/`cf_libc_extras.a`/`syslogd_libc_extras.a` —
    those stay `.a` by design, they're not independent libraries) —
    build a real `.dylib` instead, even if today's only consumer is
    still static (mirrors `tools/init_binary/build.py`'s own real,
    narrow exception: PID-1 and `pwtool` stay fully static because they
    run before/without dyld by design — everything else doesn't get that
    exemption just because it hasn't been asked yet).

    **Full current static-archive inventory** (`ls libc_build/*.a` +
    `ls libc_build/system/*.dylib`, re-derived 2026-08-28, not copied
    from the five-library list above — that list is now a subset of
    this one):
    - **Already real dylibs, `.a` retired/superseded** (confirmed by
      reading each dylib's own build script, not guessed): libc_gen/
      libc_string/libc_stdio/libc_printf/libc_locale_for_stdio/
      libc_regex → bundled into `libsystem_c.dylib`; libcxx/libcxxabi/
      libc_locale (selectively) → `libc++.dylib`; libmalloc →
      `libsystem_malloc.dylib`; libplatform → `libsystem_platform.dylib`;
      libpthread → `libsystem_pthread.dylib`; libunwind →
      `libunwind.dylib`; libxml2 → `libxml2.dylib`. `libclosure.a` is a
      **partial** case: `libsystem_blocks.dylib` already builds the real
      Blocks runtime from source independently (confirmed via
      `build_libsystem_blocks.sh`), but `libclosure.a` itself is still a
      live, real dependency of `build_launchd_core.sh`'s own static-ish
      trial-link line — retire the `.a` only once that trial link (or
      its eventual real `build_launchd.sh`) links `libsystem_blocks.dylib`
      directly instead.
    - **Real, standalone libraries still static, genuine conversion
      candidates** (each has a real Apple `.dylib` counterpart on actual
      macOS, confirmed for the starred ones by reading that library's own
      real upstream `Makefile`/build config, not assumed for the rest):
      `libCoreFoundation.a`* (Phase 9, by far the largest and riskiest —
      real load-time global state/constructors, scope its own session,
      see `docs/daemon-roadmap.md`'s "Static-vs-dylib architecture debt"),
      `libbsm.a`* (real `bsm/lib/Makefile` already targets
      `libbsm.dylib`, see this item's own note above), `libresolv.a`,
      `libopenpam.a` (real PAM modules are themselves per-module
      dylib-shaped on macOS, not just the base library), `libnotify.a`/
      `libnotify_client.a`, `libncurses.a`, `libiconv.a`,
      `libcopyfile.a`, `libremovefile.a`, `libm.a`, `libxpc.a`/
      `libxpc_syscalls.a`/`libxpc_syscalls2.a` (real `libxpc.dylib`),
      `libiokituser.a` (real `IOKit.framework`, not a plain dylib, but
      the same "should be independently loadable" concern applies).
    - **Not real conversion candidates, stay `.a` on purpose** (this
      project's own internal glue, not independent Apple libraries):
      `cf_libc_extras.a`, `launchd_libc_extras.a`, `syslogd_libc_extras.a`,
      `libcxx_locale_for_stdio_scratch.a` (per-consumer curated-extraction
      bundles); `liblaunchd_core.a`/`liblaunchd_mig.a`/
      `liblaunchd_syscalls.a`/`libsyslogd.a` (per-daemon object archives
      feeding a real executable link, not a runtime-loadable library);
      `libmach_ipc.a` (this project's own MIG-regen compat layer, no real
      Apple equivalent to match).
    - **Possibly dead, not yet confirmed**: `libdispatch_min.a`/
      `libdispatch_real.a` — likely superseded now that
      `libdispatch.dylib` is real (Phase 6.6); check for remaining real
      consumers before deleting, don't assume. **Still open as of
      2026-08-29** — not investigated this pass either, carried forward.

    **RESOLVED 2026-08-29 — all 11 "genuine conversion candidate"
    archives above converted to real dylibs, and every real consumer
    relinked (not just left as unused parallel output).** User paused
    the launchd SIGSEGV investigation (bug #6, session 2) specifically
    to run this pass. `libCoreFoundation.a` deliberately still deferred
    (explicit user decision, own future session — unchanged from this
    item's own original reasoning). `libiokituser.a` converted to
    `libiokituser.dylib` (a plain dylib, not a `.framework` bundle — this
    project has no bundle/Info.plist packaging pipeline for kexts either,
    same established precedent).
    - Real per-library dylibs: `libresolv.dylib`, `libsystem_notify.dylib`
      (replaces libnotify.a + libnotify_client.a — real Apple ships these
      as one product), `libbsm.dylib`, `libncurses.dylib`,
      `libcopyfile.dylib`, `libremovefile.dylib`, `libiconv.dylib`,
      `libsystem_m.dylib` (real Apple name for what was libm.a),
      `libopenpam.dylib` (base library only — real per-module PAM dylibs
      need `dlopen`, which this project doesn't have; same documented gap
      class as OpenPAM's own module loader elsewhere in this project, not
      attempted), `libxpc.dylib` (replaces libxpc.a + libxpc_syscalls.a +
      libxpc_syscalls2.a as one real dylib), `libiokituser.dylib`.
    - Consumers relinked away from the static archives: `build_launchd.sh`
      (libbsm/libxpc — the launchd SIGSEGV diagnostics from the paused
      session were left completely untouched/uncommitted throughout),
      `build_syslogd.sh`, `build_launchctl.sh`, `build_simple_cmds.sh`
      (all 76 commands, re-verified 76/0 no regression), `build_corefoundation.sh`
      (non-CF library refs only — CF itself stays static per the scope
      decision above). `build_dyld_link.sh` deliberately left alone — a
      real, legitimate exception (dyld itself can't depend on the real
      dylibs it's the thing that loads), same class as `tools/init_binary`/
      `pwtool`.
    - Real duplicate-global-state bugs closed at the architectural root
      (same bug class as `mach_task_self_`/`g_zone`, not hypothetical):
      `build_libdyld.sh`/`build_libsystem.sh` now `-Wl,-reexport_library`
      (not statically bundle) `libresolv.dylib`/`libcopyfile.dylib`/
      `libremovefile.dylib`, closing three independently-uninitialized
      private copies of `_res` down to one shared copy.
    - Two new real gaps found+closed the same class of investigation this
      whole migration already established: `mach_msg()`/`mach_msg_trap()`
      now live for real in `libsystem_kernel.dylib` (previously bundled
      separately into launchd.macho's static portion AND
      libdispatch.dylib), and a real `libsystem_pthread_dylib_bootstrap_compat.c`
      constructor closes the same "nothing calls
      `pthread_main_thread_bootstrap()` for a real dyld-linked consumer"
      gap at the `libsystem_pthread.dylib` level that
      `libsystem_dyld_initializer_compat.c` already closed once for
      `libSystem.B.dylib`.
    - **Real, deliberately deferred residual, not silently dropped**:
      `build_libsystem_c.sh` still does narrow, curated `ar x` object
      extraction (not full-archive bundling) from `libm.a`/
      `libremovefile.a`/`libopenpam.a` for a handful of specific symbols
      ICU's own dylibs expect from "libSystem" (`acos`/`asin`/`sqrt`/
      `fmod`/`qsort`/`heapsort`/`time`/`gettimeofday`/etc). Lower risk
      than the `_res`-class bug above (mostly stateless math/qsort/time
      functions, not mutable process-global state) — and genuinely
      structurally different: `libsystem_m.dylib`/`libremovefile.dylib`/
      `libopenpam.dylib` all link AGAINST `libsystem_c.dylib`, so
      `libsystem_c.dylib` linking them back would be a real circular
      build-order dependency, not just a style choice. Real follow-up,
      not attempted here: reexport `libsystem_m.dylib`/`libopenpam.dylib`
      through `libSystem.B.dylib` (matching the umbrella-reexport pattern
      that already made ICU's `libresolv`/`libcopyfile`/`libremovefile`
      needs resolve without embedding), then drop the curated extraction
      entirely.

---

## P2, item d9 — [RESOLVED 2026-08-28] full writeup archived

d9. **`build_simple_cmds.sh` converted from fully-static to real dynamic
    linking (2026-08-28) — RESOLVED: 76 of 76 commands now build, the
    ld64-609 crash below fixed for real.** See this item's own trailing
    "RESOLVED 2026-08-28" paragraph for the fix; the rest of this item
    (kept verbatim) is the real bisection trail that root-caused it.
    This is the first real
    dyld-loaded consumer d8 above was waiting for, and confirms d8's own
    premise: converting a real static consumer to real dynamic linking
    against `libSystem.B.dylib`/`libdyld.dylib` eliminates the entire
    `-Wl,-alias` workaround class this file used to need (the ~60-entry
    `LINK_ALIASES`/`LINK_ALIASES_TAR` saga, and the real ld64-609
    "Assertion failed: (target != NULL)" crash in `generateLinkEditInfo`
    that saga existed to work around) — `libSystem.B.dylib` already
    bakes the same real UNIX2003/INODE64/DARWIN_EXTSN aliases into its
    own build (`build_libsystem.sh`'s own tail), so a real dynamic
    consumer needs zero alias flags of its own for anything whose real
    base is locally resolvable at THAT dylib's own link time.

    Real, load-bearing finding from this conversion, not previously
    documented anywhere: **`-Wl,-alias` inside a dylib only lands in
    that dylib's real `LC_DYLD_EXPORTS_TRIE`** (confirmed via
    `xcrun dyld_info -exports`, **not** `nm` — `nm`'s plain symbol-table
    listing shows an alias-created indirect symbol regardless of whether
    it actually made the trie, which is actively misleading) **when the
    alias's BASE symbol is genuinely local to that same link.** Adding
    an alias for e.g. `_fputs$UNIX2003`/`_strerror$UNIX2003` to
    `libSystem.B.dylib`'s own build — where those bases are only
    reachable via `-reexport_library`, not local — silently produces an
    alias no real consumer can ever resolve (a real trial link fails
    with "symbol not found" despite `nm -gU` claiming the alias exists).
    The fix used throughout this conversion: add each alias at the
    specific sub-dylib (`build_libsystem_c.sh`, `build_libsystem.sh`,
    etc) where its base is genuinely local, never at the umbrella
    `libSystem.B.dylib` level for a reexported base. A real runtime
    regression from exactly this class of bug was caught live in QEMU
    (2026-08-28): `cat.macho` linked clean but crashed at real dyld load
    time (`dyld[3]: Symbol not found: _fdopen$UNIX2003`) because the
    private test SD image's `/usr/lib/system/libsystem_c.dylib` was
    stale — `tools/init_binary/inject_into_sd_image.sh` must be re-run
    after every `build_libsystem_c.sh`/`build_libsystem.sh` rebuild, not
    just after `build_simple_cmds.sh`, or booted binaries will resolve
    dylib imports against old exports. A second, related finding: **any
    sub-dylib whose real, non-aliased symbols a consumer's own compiled
    code calls directly must also be linked directly on that consumer's
    own command line** (`libsystem_pthread.dylib` for
    `pthread_main_thread_bootstrap()`, `libsystem_c.dylib` for the bulk
    of libc, `libsystem_malloc.dylib` for the private
    `_pthread_setspecific`/`_pthread_getspecific`/`_pthread_once`
    variants real libmalloc's own TSD-cache path needs) — ld64 does not
    resolve a multi-level reexport chain through `libSystem.B.dylib`
    alone for this toolchain, regardless of whether the symbol is
    alias-derived or a real, directly-defined export.

    **The 32 blocked commands** (`cp`, `mv`, `ls`, `grep`, `tar`, `diff`,
    `gzip`, `less`, `find`, `date`, `du`, `stat`, `chmod`, `chown`,
    `sed`, `xargs`, `nice`, `printf`, `echo`, `wc`, `cut`, `vis`, `rs`,
    `mknod`, `expand`, `unexpand`, `fold`, `hexdump`, `jot`, `nl`,
    `tail`, `touch`) all need real locale support this project's dropped
    `libc_locale_for_stdio.a` used to provide — at minimum
    `__locale_key`/`__global_locale` (real `ctype.h` xlocale macros;
    `libc_locale_globals_compat.c` already provides a narrow real
    reproduction of these two globals, same file used by
    `build_libdispatch_dylib.sh`), and for about 20 of them, deeper real
    `LC_TIME`/`LC_COLLATE` support (`ls`'s date formatting; `grep`/
    `find`/`diff`'s `[a-z]`-range `fnmatch()` matching). The real
    blocker: `libc_locale_globals_compat.c`'s `__global_locale` needs
    `&_DefaultRuneXLocale` — real `third_party/Libc/locale/FreeBSD/
    table.c`'s big built-in "C"-locale character-classification table —
    and linking that table into ANY of these 32 commands' already-
    substantial per-command object set reproduces a real, narrow
    ld64-609 internal crash: `Assertion failed: (_mode ==
    modeFinalAddress), function finalAddress` (a different assertion
    from the `generateLinkEditInfo` one d8/the alias saga hit — this one
    fires deep in `OutputFile::applyFixUps`, under `-Wl,-fixup_chains`
    specifically). Extensively, but so far unsuccessfully, bisected
    (2026-08-28) — real approaches tried, in order, each with a real,
    specific reason it didn't work:
    - Removing the one confirmed real cross-object static relocation
      (`__global_locale`'s static initializer took `&_DefaultRuneXLocale`
      directly; moved to a runtime constructor assignment instead) — no
      change, still crashes.
    - Source-level merging of `table.c`+`none.c`+`ldpart.c`+`collate.c`+
      `collcmp.c`+`findfp.c`+`getcwd.c` into one translation unit via
      `#include` (fewer separate link-time objects) — crashes MORE, not
      less (even locale-light commands started crashing once every
      command shared the same larger bundle).
    - Hand-copying the same real function bodies directly into `table.c`
      instead of `#include`-merging (avoids per-file `__FBSDID`/
      `sccsid`/license-comment overhead) — no reliable improvement over
      `#include`-merging at the same content size.
    - A real `ld -r` partial-link merge of the separately-compiled
      objects into one — still crashes, ruling out "too many separate
      *objects*" as the mechanism (it's total complexity/fixup count
      regardless of object boundaries).
    - Swapping `none.o` for an unrelated third object (`findfp.o`) at
      the same `{libc_locale_globals_compat, table}` pair — still
      crashes, ruling out anything `none.c`-specific.
    - Dropping the by-far-largest file (`collate.c`, 1046 real lines) in
      favor of just `collcmp.c`'s own tiny real `__collate_range_cmp()`
      (which only needs real `wcscoll_l()`, already a direct
      `libsystem_c.dylib` export, confirmed via `nm -gU`) — reduced but
      did not eliminate the crash.
    - **Results were not even consistently reproducible between a clean
      rebuild and an incremental one with identical source** — the same
      `{libc_locale_globals_compat, table}` pair that built 0/32 clean
      in one run reproduced 12/32 crashes in a supposedly-identical
      clean rebuild immediately after. This is the real reason bisection
      was abandoned rather than continued: a threshold that doesn't
      reproduce deterministically can't be reliably bisected against.

    Current state: `third_party/Libc/locale/FreeBSD/table.c` is back to
    fully pristine (`git checkout --`, no patches survive) and
    `build_simple_cmds.sh`'s `locale_extra_objs` is deliberately left
    empty — the 32 commands above simply don't build in this pass, a
    real, visible failure (`build_simple_cmds: 44 built, 32 failed`,
    stable across repeated clean rebuilds), not a silently-degraded one.
    Real follow-up, not attempted here: either find a real ld64-609
    workaround for this specific fixup-chains threshold (possibly
    version-specific to this project's pinned Xcode 12 toolchain — worth
    checking whether a newer `ld64`/`ld-prime` bundled with a later
    Xcode still on this machine, if any, reproduces it at all before
    assuming it's permanent), or restructure `_DefaultRuneXLocale`
    itself (e.g. lazily-populated instead of one giant static
    initializer) to reduce its own real fixup/atom footprint below
    whatever this toolchain's real threshold is.

    **RESOLVED 2026-08-28, same day, following the exact "worth
    checking" follow-up above.** Real, isolated repro (minimal
    `table.o`+`none.o`+`libc_locale_globals_compat.o`+trivial `main`,
    same `-Wl,-fixup_chains`, same dylib set) reproduced the identical
    `Assertion failed: (_mode == modeFinalAddress)` under Xcode 12's own
    `ld64-609`, then the *same* link command with `clang`'s `-B` flag
    pointed at the daily-driver Xcode's own much newer `ld` (this
    machine: Xcode 26.6, `ld` 1267) succeeded — confirmed not just
    "succeeds" but real load-command parity via `otool -l` against an
    already-QEMU-verified dynamic executable (`true.macho`): identical
    `LC_LOAD_DYLINKER`/`LC_MAIN`/`LC_DYLD_CHAINED_FIXUPS` shape. This is
    a real, narrow ld64-609 bug in the daily-driver Xcode's own
    predecessor, not anything about this project's object code —
    confirmed by two back-to-back clean rebuilds (`rm -rf
    libc_build/simple_obj`) both landing exactly `76 built, 0 failed`,
    where the crash used to reproduce non-deterministically per the
    bisection notes above.

    Fix, in `build_simple_cmds.sh`: `NEWLD_BINDIR` (`xcode-select -p`'s
    `Toolchains/XcodeDefault.xctoolchain/usr/bin`, fail-fast if missing)
    passed via `-B"$NEWLD_BINDIR"` on the final per-command link only —
    still compiling with Xcode 12's own `clang`/SDK throughout (the
    SDK/header-vintage invariant this project's other "must be Xcode
    12" rules are actually about is untouched; only ld64's own internal
    bug is being routed around). `table.c`/`none.c` joined
    `COMMON_EXTRA` (always linked, same "harmless if unreferenced"
    precedent as `err.c`/`getopt.c`), which resolved the crash and the
    original `___global_locale`/`___locale_key`/`__none_*` gaps
    together (44 → 55 built). Closing the remaining 21 needed three
    more real ports, each a genuine new finding, not just re-adding
    files the bisection above already tried:
    - `ldpart.c`/`getcwd.c`/`findfp.c`: compiled clean with zero
      changes, resolving `__ldpart_free_extra`/`__part_load_locale`
      (real `LC_TIME` via `__time_load_locale`, needed by every command
      already linking `strftime.c`/`timelocal.c`) and `___sfprelease`
      (`findfp.c`, needed by `freopen$UNIX2003`) — 55 → 71 built.
    - `collate.c` (real `LC_COLLATE`/`fnmatch()` range-matching support
      for `grep`/`find`/`diff`/`du`) has a genuine, narrow compile bug
      *unrelated to ld64-609*: its own header
      (`locale/FreeBSD/collate.h`) guards
      `__collate_mbstowcs`/`__collate_wcsdup`'s prototypes behind
      `#ifdef __LIBC__` (real Apple's own umbrella Libc build always
      defines this; no other file this project compiles ever needed
      to) — without it, collate.c's own internal forward-reference to
      `__collate_mbstowcs()` gets an implicit-int declaration that then
      conflicts with its real `wchar_t*`-returning definition later in
      the same file. Fixed with a `collate.c`-only `-D__LIBC__`
      override in the `COMMON_EXTRA` compile loop (scoped, not global —
      untested whether `__LIBC__` changes anything else this project
      compiles). `collcmp.c` (the actual home of `__collate_range_cmp`,
      confirmed via `grep`, not assumed to live in `collate.c`) added
      alongside it — 71 → 75 built.
    - `cat` **regressed** during this pass (real, caught immediately by
      the full-suite rerun, not silently missed): `cat_socket_stub.o`'s
      link-alias case aliased `_send`/`_writev` to base symbols
      `cat_socket_stub.c` never actually defined (only
      `socket()`/`connect()`/`shutdown()` — real `cat.c` never calls
      `send()`/`writev()`). Xcode 12's `ld64-609` silently tolerated
      aliasing an undefined base; the daily-driver Xcode's newer `ld`
      hard-fails on it (`symbol(s) not found ... in
      command-line-aliases-file`) — a real, confirmed behavioral
      divergence between the two linkers' `-alias` handling, not a
      regression from anything else in this pass. Fixed by dropping the
      two dead aliases from `cat_socket_stub.o`'s case entirely.
    - `date` was the last failure, and a *different* real gap: its own
      `date_compat.c` already ships honest `ENOSYS` `send()`/`writev()`
      stubs for `oldsyslog.c`'s `vsyslog()` path (predates this pass —
      real `netsettime()`/`pututxline()` network-daemon gaps this same
      file's header already documents), but no `case` ever added the
      matching `-Wl,-alias` flags for `date` specifically (only
      `cat_socket_stub.o`'s case did, and cat doesn't need them). Fixed
      by adding a `date`-only alias case — no new stub code needed, the
      base symbols already existed.

    **Second real regression found by the real QEMU boot-test that
    followed (2026-08-28, not caught by any of the above — `otool -l`/
    `gap_audit.sh` both passed clean on `ed`/`diff`/`less` first):**
    `diff /tmp/a /tmp/b` (the first of the 76 to actually launch and
    exercise this path — `diff` was one of the newly-unblocked 32, `ed`/
    `less` were already in the original 44) crashed immediately at
    process launch, before printing anything: `dyld[8]: Symbol not
    found: _pipe / Referenced from: /bin/diff / Expected in:
    /usr/lib/libSystem.B.dylib`. Root cause, confirmed via `nm -m` on
    all four real `pipe(2)` consumers (`ed`, `diff`, `less`, `tar`):
    real `ed_popen_system_compat.c` (`ed`/`less`) and real
    `filter_fork_posix.c`/GNU `diff.c` both call plain `pipe(fd)`,
    which needs a real `_pipe` symbol, but `ed_pipe_trap.o` (Apple's own
    real `libsyscall/custom/__pipe.s`, which all four already link)
    only exports the raw trap under `___pipe` (three underscores) —
    real Apple Libc needs a thin public-name forwarder in between,
    which `tar_pipe_compat.c` already provided (written earlier, for a
    *different* reason — see that file's own header) but only tar ever
    linked it. `ed`/`less` used to get this bridged for free via the
    old static-linking era's project-wide `-alias ___pipe _pipe`
    directive, which d9's own dynamic-link conversion earlier this same
    day removed project-wide (the whole obsoleted `LINK_ALIASES` stack)
    without anyone re-providing the bridge for these two specific
    commands — a real, dormant regression from *earlier in this same
    d9 conversion*, invisible until something actually launched under
    real chained-fixups dyld (which binds every referenced symbol
    eagerly at process start, unlike the old lazy-bind model — a call
    site being unreached at runtime doesn't save you). `diff` becoming
    linkable was what finally exercised it, but `ed`/`less` had already
    been silently broken the same way since d9's conversion, just never
    actually launched afterward to prove it (link-time `otool -l` /
    `gap_audit.sh` both pass regardless, since a `(from libSystem)`
    "safe two-level-namespace binding" classification only checks that
    *some* dylib in the process could plausibly satisfy the symbol at
    load, not that it actually does). Fixed by linking
    `tar_pipe_compat.c` into `ed`/`diff`/`less` too (that file's own
    header updated to reflect it's no longer tar-specific despite the
    name) rather than reintroducing the removed alias mechanism. Real
    QEMU re-verification after the fix: `ls`, `grep`, `date`, `diff`
    (real `1c1`/`<`/`>` output), `find`, `stat`, `fold` all confirmed
    working with no panic/assertion/dyld-symbol-not-found anywhere in
    the transcript; `ed`'s own file-write in that same session failed
    with an unrelated "No such file or directory" that traces to the
    test harness's own quoting through the pty, not a `pipe()`/dyld
    issue (ed's core buffer/file-write path doesn't call `pipe()` at
    all — only its `!`-shell-escape and `r !cmd`/`w !cmd` forms do),
    not chased further here.

    Real, still-open, deliberately out of scope for this pass: none
    known — all 76 commands build, and the two real regressions the
    QEMU pass surfaced (cat's dead aliases, ed/diff/less's missing
    `_pipe` bridge) are both fixed and re-verified. `docs/roadmap.md`'s
    own Phase 4/4.6 command tiers should be checked for whether any of
    them still list `date`/`ls`/etc as behaviorally-limited for a
    *different* reason (e.g. `date`'s real clock-setting path is still
    stubbed, see `date_compat.c`'s own header) — this item is scoped to
    "does it link and boot," not full feature parity. `ed`'s own
    file-write behavior under a real interactive session (as opposed to
    this pass's scripted/piped test) has not been separately verified.

---

## P2, item 0a — [DONE 2026-08-28] real AES core, full writeup archived

0a. **[DONE 2026-08-28] Real AES core wired: FreeBSD rijndael, not
    PureDarwin.** Filed 2026-08-21 as a hard prerequisite for Phase 7b
    (`sshd`); closed by vendoring `sys/crypto/rijndael/` (public-domain,
    Rijmen/Bosselaers/Barreto — the AES designers themselves, cleaner
    than PureDarwin's mixed-license option scoped 2026-08-27 below and
    never acted on) into `libkern/crypto/freebsd/rijndael-alg-fst.c`,
    plus a new corecrypto-shaped adapter
    (`libkern/crypto/corecrypto_aes_register.c`) implementing real
    `ccmode_ecb`/`ccmode_cbc`/`ccmode_ctr` instances against it and
    calling `register_crypto_functions()` with only the AES fields
    filled (ecb/cbc/ctr encrypt+decrypt), everything else left NULL per
    the incremental-fill plan below.

    `g_crypto_funcs` (`libkern/crypto/register_crypto.c`) is a table of
    function pointers real macOS's closed-source corecrypto kext fills in
    via `register_crypto_functions()`; this kernel loads no kexts, so
    nothing had ever called it before this. Bitten five separate times
    now, each rediscovered the hard way from a fault address — the fifth
    (`cc_rand_generate`) found and fixed *by this same AES work*, not a
    surprise later:

    | Caller | Offset | Field | Resolution |
    |---|---|---|---|
    | `SHA256_Init` via `devfs_mount` | `0x30` | `ccsha256_di` | real corecrypto, item 0b |
    | MD5 | `0x20` | `ccmd5_di` | now real FreeBSD `md5c.c`, bypasses the table (see below) |
    | `read_random` | n/a | `cckprng` | falls back to `read_erandom()` |
    | `tcp_init`→`tcp_tfo_init` | `0x78` | `ccaes_cbc_encrypt` | **now real** — TFO auto-enables |
    | `cc_rand_generate` (`corecrypto_rand.c`) | n/a | `ccrng_fn` | **found by this work**, see below |

    **Two real bugs found and fixed while wiring this up, neither
    guessed — both from real QEMU panics:**
    1. `register_crypto_functions()` writes `g_crypto_funcs`, a
       `SECURITY_READ_ONLY_LATE` global — that section becomes
       hardware-write-protected at `machine_lockdown()`
       (`osfmk/kern/startup.c`). First attempt called the registration
       function from `bsd_init()` (which runs well after lockdown) and
       produced a real "Unexpected fault in kernel static region"
       (ESR=0x9600004f, a write-permission fault) at boot. Fixed by
       moving the call into `kernel_bootstrap_thread()`
       (`osfmk/kern/startup.c`), immediately before `machine_lockdown()`
       — the same place a real corecrypto kext's own registration would
       need to land relative to lockdown.
    2. Once AES was actually live, a *second*, unrelated real panic
       appeared inside `necp_init()` — a NULL-function-pointer jump
       (`Kernel instruction fetch abort at pc 0x0`). Root cause:
       `corecrypto_rand.c`'s `cc_rand_generate()` guards on
       `if (g_crypto_funcs)` only, then unconditionally calls
       `g_crypto_funcs->ccrng_fn(...)` — a field this table intentionally
       leaves NULL (no RNG registered here; `read_random()` has its own
       separate `read_erandom()` fallback). That guard was written
       assuming corecrypto's real all-or-nothing kext fill and had never
       been exercised, because `g_crypto_funcs` had literally never been
       non-NULL before in this project's history. Fixed with a
       field-specific null check
       (`g_crypto_funcs && g_crypto_funcs->ccrng_fn`), matching the
       pattern `corecrypto_aes.c` already uses. **Root-caused via a real
       A/B test**, not guessed: the exact same kernel with only the
       registration call disabled booted cleanly past this point, proving
       the crash was caused by AES becoming active, not a pre-existing
       gap.
    3. **Known, deliberately NOT fixed — now filed as its own bug, P1
       item 3**: six other `g_crypto_funcs->` call sites
       (`corecrypto_des.c`, `corecrypto_rsa.c`, `corecrypto_aesxts.c`,
       `corecrypto_chacha20poly1305.c`, `bsd/nfs/gss/ccrypto.c`,
       `bsd/netinet/flow_divert.c`) have **no null check at all**, not
       even the bare `if (g_crypto_funcs)` that `corecrypto_rand.c` had.
       Not reached by anything exercised up through a full boot to login
       (QEMU-verified), so not made worse by this change — but a live
       NULL-function-pointer call waiting for whoever next touches DES/
       RSA/AES-XTS/ChaCha20-Poly1305/NFS-GSS/flow-divert. Full writeup:
       P1 item 3 above.

    Context sizing: `libkern/libkern/crypto/aes.h`'s
    `AES_CBC_CTX_MAX_SIZE` (a hardcoded ~280-byte budget baked into the
    `aes_encrypt_ctx`/`aes_decrypt_ctx` typedefs used throughout the
    kernel) comfortably fits the new adapter's per-direction context
    (an `int` plus a 4×(Nr+1)-word key schedule, ≤244 bytes for AES-256)
    — confirmed for real by the AES-256 case in the boot-time self-test
    below, not just computed on paper.

    **FIPS-197/SP 800-38A self-test at boot, not just KAT-in-a-vacuum**:
    `xnu_rpi3_register_freebsd_aes()` runs AES-128/192/256 ECB
    (FIPS-197 Appendix C.1–C.3) and AES-128 CBC/CTR (SP 800-38A Appendix
    F.2.1/F.5.1) known-answer vectors through the real
    `ccmode_ecb`/`ccmode_cbc`/`ccmode_ctr` interfaces — not the raw
    cipher core directly — so a bug in the adapter glue fails loudly at
    boot (`panic()`) exactly like a bug in the vendored core would,
    before `register_crypto_functions()` is ever called.

    ARMv8 Crypto Extensions (AESE/AESMC) on BCM2837's Cortex-A53:
    believed absent, still not verified — irrelevant now since the
    software core is real and working, not a blocker.

    **PureDarwin/corecrypto (scoped 2026-08-27) was NOT used in the end.**
    That investigation found real, verified-matching `struct
    crypto_functions` ABI precedent but a mixed-license AES core
    (`LICENSE.md` says Apache-2.0/mbedTLS, `README.md` says Botan +
    `rafirafi/pdcrypto`, the code's own header credits `tiny-AES128-C` —
    three different stated lineages) and AES-128-only coverage (no
    CTR/AES-256). FreeBSD's `sys/crypto/rijndael/` turned out to need no
    interface borrowing at all — it's just a block cipher core
    (`rijndaelKeySetupEnc/Dec` + `rijndaelEncrypt/Decrypt`), and this
    project's own `struct ccmode_ecb/cbc/ctr` adapter (project-specific
    glue either way, per the original scoping above) covers ECB/CBC/CTR/
    AES-128/192/256 cleanly under one public-domain license. Vendoring
    details, pin, and per-file license notes: `setup_third_party.sh`'s
    `freebsd-src` block.

---

## P2, item 0b — [DONE 2026-08-27/28] real SHA-256/384/512/MD5, full writeup archived

0b. **[DONE 2026-08-27] `ccsha256_di` wired to real corecrypto, replacing
    the from-scratch `corecrypto_sha2.c` for code-signing/exec hashing.**
    `SHA256_Init`/`_Update`/`_Final` (`libkern/crypto/corecrypto_sha2.c`)
    now call real `ccdigest_init`/`_update`/`_final` + `ccsha256_di()`
    directly (the real digest-core source was already sitting unused at
    `third_party/xnu-7195/osfmk/corecrypto/` and already compiled into
    the kernel binary for the PRNG's own use — only the SHA-256 call
    sites hadn't been pointed at it). QEMU-verified: real login, real
    shell, real `/bin/ls`/`md5` exec, no panic — commit `ceb6d419c` in
    the `xnu-7195` checkout. Full investigation (including a corrected
    finding that `register_crypto_functions()`/`g_crypto_funcs` was
    never actually the right mechanism for this call site) archived to
    `docs/historic/backlog.md`.

    **[DONE 2026-08-28] SHA-384/512 and MD5 also off the from-scratch
    path now**, alongside item 0a's AES work — no real corecrypto body
    exists for either in this checkout (confirmed absent, only headers),
    so per this project's Apple→FreeBSD→custom source priority: SHA-384/
    512's compress core (the real K[80] table and round function, not
    just constants copied by hand) is now FreeBSD's real 2-clause-BSD
    `sys/crypto/sha2/sha512c.c` (vendored to
    `libkern/crypto/freebsd/sha512_transform.c`, upstream's ARM64 ifunc
    hardware-accel path dropped — dead code on Cortex-A53 either way);
    `corecrypto_sha2.c` keeps its own buffering/padding wrapper since
    that has to match this project's `SHA512_CTX`/`SHA384_CTX` layout,
    not FreeBSD's differently-shaped one. MD5
    (`libkern/crypto/corecrypto_md5.c`, deleted) is now FreeBSD's real
    `sys/kern/md5c.c` (RSA Data Security's own 1991-2 reference
    implementation) at `libkern/crypto/freebsd/md5c.c`, K&R-style
    function definitions modernized to ANSI prototypes (mechanical,
    same bodies) — this project's own `MD5_CTX` layout
    (`state[4]`/`count[2]`/`buffer[64]`) is byte-identical to FreeBSD's,
    no adapter needed. Both QEMU-verified as part of the same boot that
    verified item 0a's AES work.

---

## P2, item d11 — sessions 2-5 investigation trail archived (framings now known wrong)

This is the "Historical context below is retained for the record" block
from item d11, verbatim. **Its "second thread"/`TPIDRRO_EL0`/`SP_EL0`
framings are all now known wrong** — the real root cause was
`_FORTIFY_SOURCE` self-recursion in `third_party/Libc/secure/*_chk.c`, and
the follow-on silent `exit(1)` was two more duplicate-static-copy bugs.
See `docs/launchd-bug6-postmortem.md` and
`docs/launchd-pid1-postmortem.md` before reading any of this as current.

    Historical context below is retained for the record; note its
    "second thread"/`TPIDRRO_EL0`/`SP_EL0` framings are all now known
    wrong. Full context, diagnostic infrastructure,
    and the TPIDRRO_EL0 cross-image-confusion hypothesis:
    project memory `project_xnu_rpi3_launchd_pid1_2026-08-28_session2.md`
    and `project_xnu_rpi3_static_to_dylib_sweep_2026-08-29.md`. Short
    version for anyone without memory access: after item d10's real
    dyld-gap-closing pass, `launchd.macho` boots all the way through real
    dyld initializers, `main()`, and `launchd_runtime_init()` — reaching
    exactly the same failure as before: the `kqueue_demand_loop` second
    thread starts, calls `select()`, `select()` returns, then crashes
    with a real SIGSEGV (`namespace 2 subcode 0xb`) inside
    `mig_get_reply_port` shortly after
    (`qemu/launchd_boot_test48.log`).

    One real fix already applied and tested, confirmed **not** to
    resolve this on its own: `third_party/PureDarwin/src/Libraries/XPC/
    launchd/launchd.c`'s `main()` now calls
    `pthread_main_thread_bootstrap()` explicitly before
    `launchd_runtime_init()` (session 2's own resume-hypothesis) —
    confirmed via `nm launchd.macho` this was genuinely the *first* call
    against launchd's own local `_main_thread`/`__pthread_head` (not a
    double-bootstrap; the existing `libsystem_dyld_initializer_compat.c`
    constructor only ever touched `libsystem_pthread.dylib`'s separate
    copy). Real, correct, keeps launchd's local pthread state
    consistent — but the SIGSEGV in the second thread persists
    regardless, so it's a real, separate fix, not the root cause. **This
    change is UNCOMMITTED** (submodule `third_party/PureDarwin`),
    consistent with how the rest of this investigation's state has been
    left throughout (the `iokit_diag8`/`9`/`12`/`13`/`14` instrumentation
    across `tools/userland_staging/launchd_mach_port_compat.c`,
    `mach_msg_real.c`, `libsystem_c_gap_compat.c`,
    `libsystem_dyld_initializer_compat.c`, plus
    `third_party/xnu-7195`/`third_party/llvm-project` diagnostic edits) —
    not yet proven correct/complete, don't commit as a real fix until
    bug #6 itself is resolved.

    **Real next step**: the crash site is identical to before
    (`mig_get_reply_port` in the second thread, right after `select()`
    returns) — add a diagnostic print inside `mig_get_reply_port` itself
    (or wherever it dereferences whatever crashes) to see the actual
    faulting address/register state, same method that closed every other
    bug in this whole investigation. Don't re-derive the TPIDRRO_EL0
    hypothesis from scratch — it's already written up in the session 2
    memory file, just not yet confirmed or ruled out.

    **UPDATE 2026-08-29, same-day resume: TWO more real duplicate-copy
    bugs found+fixed (same bug class as d10), crash site re-isolated with
    much higher fidelity, TPIDRRO_EL0-cross-image hypothesis effectively
    RULED OUT, new leading hypothesis is kernel-side.**

    Added finer-grained diagnostics inside `mig_get_reply_port()` (raw
    TPIDRRO_EL0-derived TSD base + return value at each step) to
    directly test the TPIDRRO_EL0 hypothesis — and discovered the new
    prints never fired at all. Root cause: a **third and fourth real
    instance** of item d10's exact duplicate-copy bug class, both newly
    found this session, neither previously known:
    - `launchd.macho` itself statically linked its own private copy of
      `mig_reply_port.o` (bundled inside `libmalloc.a`, only
      `mach_init.o` had ever been stripped from that local copy, not
      `mig_reply_port.o`) — confirmed via `nm launchd.macho` showing
      `T _mig_get_reply_port` as a LOCAL strong symbol, not `U`. Since
      ld64 always prefers an executable's own local strong definition
      over any linked dylib's export of the same name, EVERY call to
      `mig_get_reply_port`/`mig_dealloc_reply_port`/`mig_put_reply_port`
      from anywhere in launchd's statically-linked code silently
      resolved to this stale local copy, not `libsystem_kernel.dylib`'s
      real one — so the new diagnostics, added only to
      `libsystem_kernel.dylib`'s copy, never ran. Fixed in
      `build_launchd.sh` the same way `mach_init.o` was already stripped
      once before: `ar d "$OUT/libmalloc_no_mach_init.a" mig_reply_port.o`
      (verified via `nm` first that every symbol it defines is already a
      real export of `libsystem_kernel.dylib`, same precedent as the
      `mach_init.o` fix).
    - `libdispatch.dylib` (via `build_libdispatch_dylib.sh`) bundles the
      exact same `libmalloc_no_mach_init.a` local copy with the exact
      same gap — confirmed via `nm -gU libdispatch.dylib` showing a
      real, STRONG `T _mig_get_reply_port` (not weak, not `U`). Because
      `libdispatch.dylib` appears before `libSystem.B.dylib` on
      `build_launchd.sh`'s own dylib link line, two-level-namespace
      binding picked libdispatch's copy once launchd's own local copy
      (above) was removed — so fixing launchd's own copy alone wasn't
      enough; had to fix both. Same fix applied
      (`ar d ... mig_reply_port.o` on that script's own
      `libmalloc_no_mach_init.a` copy). **Real follow-on consequence,
      also fixed**: removing these 3 exported symbols from
      `libdispatch.dylib` broke `libxpc.dylib` and `libc++.dylib`, which
      had each baked a two-level-namespace bind record at their OWN
      earlier build time expecting `_mig_dealloc_reply_port` etc "in
      /usr/lib/system/libdispatch.dylib" — dyld enforces the specific
      recorded provider at load time (real, observed failure: `pid 1
      exited -- exit reason ... Symbol not found: _mig_dealloc_reply_port
      Referenced from: /usr/lib/system/libxpc.dylib Expected in:
      /usr/lib/system/libdispatch.dylib`). Both rebuilt
      (`build_libxpc_dylib.sh`, `build_libcxx_dylib.sh`) to re-resolve
      against `libsystem_kernel.dylib` instead — same "must rebuild any
      dylib with a baked two-level-namespace expectation, not just the
      dylib whose exports changed" gotcha item d4/session 2 already
      documented for `libSystem.B.dylib`'s own reexports, now confirmed
      to apply to ordinary cross-dylib binds too, not just
      `-reexport_library`.
    - **Unrelated real regression found+fixed along the way**:
      `build_libdispatch_dylib.sh` could no longer build at all —
      `ar: atoi.o: not found in archive`. Root cause: the 2026-08-29
      static-to-dylib cleanup pass (item d8) dropped `atoi.o` from
      `syslogd_libc_extras.a` (openpam/copyfile/etc became real dylibs,
      so `build_syslogd.sh`'s own extraction list no longer produces
      that member), but `build_libdispatch_dylib.sh` still tried to
      `ar x` it from there. Fixed by compiling
      `third_party/Libc/stdlib/FreeBSD/atoi.c` fresh, right where it's
      needed, using the same CFLAGS already established for that
      script's neighboring `libc_locale_globals_compat.c` compile,
      instead of chasing a member that no longer exists anywhere.

    **With all of the above fixed and real diagnostics finally executing
    the intended code**, the crash site is NOT `mig_get_reply_port`
    after all — `mig_get_reply_port()` now genuinely succeeds every
    time, on both threads, confirmed via the new per-call hex dumps
    (`qemu/launchd_boot_test53.log`). The TPIDRRO_EL0-cross-image-
    confusion hypothesis from session 2 is therefore effectively RULED
    OUT for this specific symptom (the second thread's own TSD/TPIDRRO_EL0
    is demonstrably valid enough to make a real MIG reply-port
    allocation succeed) — though the broader hypothesis (some OTHER
    TSD-backed state being cross-image-confused) isn't fully closed,
    just no longer the leading explanation.

    Added a second, matching per-thread-tagged/atomic diagnostic dump
    inside `mach_msg()` itself (`mach_msg_real.c`, replacing the old
    multi-syscall-per-line `iokit_diag12` prints, which were visibly
    interleaving/corrupting each other across the two real concurrent
    threads) — prints a thread tag (masked TPIDRRO_EL0) plus the real
    msg header (`msgh_bits`/`msgh_size`/`msgh_remote_port`/
    `msgh_local_port`/`msgh_id`) and `rcv_name` immediately before/after
    the raw trap. Result (`qemu/launchd_boot_test53.log` lines 851-853):
    the crash is a real SIGSEGV **inside/immediately after the raw
    `MACH_MSG_TRAP` syscall itself**, on the `kqueue_demand_loop` thread
    (confirmed by its own distinct thread tag, `0x100ad40e0`, differing
    from the main thread's `0x100a52470` earlier in the same boot) —
    "about to call" prints, "returned" never does. The message being
    sent is a completely legitimate, well-formed real Mach message, NOT
    corrupted/garbage as first suspected from the large `msgh_id`
    (0x21728 = 137256): cross-checked against
    `third_party/PureDarwin/src/Libraries/XPC/launchd/internal.defs`'s
    own `subsystem internal 137000;` declaration — 137000 decimal ==
    0x21728 hex EXACTLY, confirming this is `handle_kqueue`'s real,
    correct, compile-time-fixed MIG message ID (the very first routine
    in that subsystem, so its ID == the subsystem base). `remote=0x603`/
    `local=0x903`/`rcv_name=0x903` are real, live, valid port names in
    the process's one shared task IPC space (0x903 was the reply port
    `mig_get_reply_port()` had JUST successfully allocated on this same
    thread moments earlier; 0x603 is `launchd_internal_port`, a real
    receive right the main thread set up earlier in the very same boot).
    So: real message, real ports, real MIG id — ruled out as a message-
    header-corruption bug. The exact same kind of call (a MIG-client
    send+receive round trip via `mach_msg()`) succeeded cleanly on the
    MAIN thread earlier in the identical boot
    (`qemu/launchd_boot_test53.log` lines 816-819, id=0xc8c for
    `mach_port_move_member`) — so whatever's wrong is specific to this
    being the SECOND, `bsdthread_create()`-spawned thread making the
    call, not the message content or the reply-port machinery.

    **Real next step, not yet attempted**: this now points at the
    KERNEL side, not userland — specifically whatever
    `bsd_pthread_workq.c`'s `real_bsdthread_create()` (verbatim port of
    `kern_support.c`'s real `_bsdthread_create`, added 2026-08-25, see
    that function's own header) sets up (or doesn't) for a new thread's
    Mach-IPC-relevant kernel state that real `thread_create()` plus
    `convert_thread_to_port()`/`ipc_port_copyout_send()` may not fully
    cover on this simplified path, vs. whatever the kernel-created
    original/main thread has that a `bsdthread_create()`-spawned one
    doesn't. Mach ports themselves are task-scoped, not thread-scoped
    (rules out "the port itself is only valid for the main thread"), so
    the likely shape of the bug is something the kernel's real
    `mach_msg_trap`/`ipc_kmsg_*` path reads or writes per-CALLING-THREAD
    (not per-port) that's only correctly initialized for a thread that
    went through the FULL real thread-creation path. Needs kernel-side
    instrumentation (e.g. a diagnostic print at kernel entry to
    `mach_msg_overwrite_trap`/wherever it dereferences per-thread state)
    to find the exact faulting access — not yet attempted this session;
    a genuinely different, kernel-side investigation from everything
    above, which was all userland/build-system work.

    All new diagnostics from this update are still live, not reverted
    (same standing convention as every other `iokit_diag*` in this
    investigation) — see `mig_reply_port.c`'s `iokit_diag13_hex`/
    `iokit_diag13_tsd_base` and `mach_msg_real.c`'s `iokit_diag12_dump`.

    **UPDATE 2026-08-29, later same day: crash bisected all the way into
    the kernel's own context-switch machinery — a genuinely different,
    much deeper class of investigation than everything above (which was
    100% userland/build-system). Four real kernel rebuild-and-boot
    cycles this pass, each narrowing the fault site further.**

    Real prerequisite fix, needed before ANY kernel-side kprintf showed
    up at all: `kprintf()` on this arm64 port is gated off by default
    for a DEVELOPMENT kernel build
    (`pexpert/arm/pe_kprintf.c`'s `disable_kprintf_output`, static-init
    `TRUE`, only flipped `FALSE` if boot-arg `-enable_kprintf_spam`
    parses — real, unmodified Apple gating, not a project bug). The
    project's own earlier `TEMP DIAGNOSTIC: timer_queue_expire` kprintfs
    (`osfmk/kern/timer_call.c`, backlog P0#7) only ever appeared during
    the KERNEL-only pre-userland boot phase in every log checked — never
    once during/after `load_init_program`, which is consistent with this
    gating rather than contradicting it (not yet understood exactly why
    those specific early ones got through un-gated; not investigated
    further since it wasn't blocking). Fixed for real kernel-side
    debugging going forward: added `-enable_kprintf_spam` to
    `runvm.sh`'s own `-append` boot-args string. This is a real,
    permanent, low-risk addition (worth keeping even after bug #6
    closes, not a bug-#6-specific temporary hack) — it only affects
    whether `kprintf()` calls anywhere in the kernel are visible on the
    serial console at all; it changes zero runtime behavior otherwise.

    Added real, unmodified-Apple-source-adjacent `kprintf()` diagnostics
    (all still live, `IOKITDIAG15`/`16`/`17` tags, each printing
    `current_thread()` as a per-thread tag matching the userland
    `iokit_diag*` convention) at every real sub-step of the kernel-side
    call chain a blocking `mach_msg()` receive goes through:
    `osfmk/ipc/mach_msg.c`'s `mach_msg_overwrite_trap()` (SEND phase:
    `ipc_kmsg_get`/`ipc_kmsg_copyin`/`ipc_kmsg_send`; RCV phase:
    `ipc_mqueue_copyin`/`ipc_mqueue_receive`/`mach_msg_receive_results`)
    and `mach_msg_receive_continue()`; `osfmk/ipc/ipc_mqueue.c`'s
    `ipc_mqueue_receive_continue()` and, most granularly,
    `ipc_mqueue_receive_on_thread()`'s real turnstile/waitq setup
    (`turnstile_prepare`/`ipc_port_recv_update_inheritor`/
    `waitq_assert_wait64_locked`) and `ipc_mqueue_receive()`'s own
    `thread_block(ipc_mqueue_receive_continue)` call site.

    **Real, evidenced findings, in bisection order:**
    1. The main thread's own `mach_msg()` call (the earlier
       `mach_port_move_member` one) never blocks at all -- its reply is
       already ready synchronously (`THREAD_NOT_WAITING`), so it never
       exercises any of the blocking/continuation machinery below. Only
       the second (`kqueue_demand_loop`) thread's `handle_kqueue()` call
       does, since its reply comes from launchd's own userspace dispatch,
       not an immediate kernel reply.
    2. **Confirmed real, run-to-run NONDETERMINISM with the exact same
       kernel binary** (`qemu/launchd_boot_test57.log` vs `test58.log`,
       back-to-back boot_test runs, zero source changes between them):
       one run's second thread never even reaches
       `mach_msg_overwrite_trap` entry before the panic; the very next
       run reaches all the way to "about to call `ipc_mqueue_receive`"
       and beyond. This is a real, load-bearing finding on its own -- the
       exact crash SITE observed in any single boot is not fully
       deterministic, consistent with a genuine race (not a single fixed
       faulting instruction), and explains why earlier passes'
       "isolated" crash sites (`mig_get_reply_port`, then raw
       `MACH_MSG_TRAP`) kept moving as more diagnostics were added --
       each new diagnostic's own console-write latency measurably
       perturbs the race's timing, not just adding visibility.
    3. **With the most granular instrumentation (`qemu/
       launchd_boot_test59.log`), the fault is bisected to inside
       `thread_block(ipc_mqueue_receive_continue)` itself** -- every
       real setup step immediately before it succeeds cleanly and is
       fully confirmed via real, non-garbage values: `imq_is_turnstile_proxy=1`
       (the reply port IS a real turnstile proxy, as expected for a MIG
       reply port), `turnstile_prepare()` returns a real, valid
       `rcv_turnstile` pointer, `ipc_port_recv_update_inheritor()`
       completes, `waitq_assert_wait64_locked()` returns `THREAD_WAITING`
       (wresult=-1) as expected, `self`/`current_thread()` agree and are
       non-garbage, `ith_continuation` is a real, valid, non-null kernel
       code pointer. The very next kprintf (`IOKITDIAG17: ... about to
       call thread_block`) is the LAST diagnostic output in the entire
       boot log before the panic -- zero occurrences of `IOKITDIAG16`
       (placed at the top of both `ipc_mqueue_receive_continue()` and
       `mach_msg_receive_continue()`, i.e. the real wakeup/continuation
       resume path) ever fire.
    4. **Real complication, not yet resolved**: `bsd/kern/kern_exit.c`'s
       `launchd_crashed_panic()` (the real, unmodified-Apple source that
       prints the "pid 1 exited -- exit reason ..." line and then
       panics) only ever fires from a genuine process exit/signal
       delivery path -- meaning "namespace 2 subcode 0xb" really is a
       true `OS_REASON_SIGNAL`/SIGSEGV(11) delivered to a thread running
       in EL0 (user mode), not a raw EL1 kernel-mode data abort (which
       would panic directly, with a full kernel register dump, never
       reaching this signal-shaped path at all). This is in real tension
       with finding 3 above: if the fault is genuinely EL0, it should
       occur only AFTER `thread_syscall_return()` hands control back to
       user mode -- which happens only via `ith_continuation`'s call
       chain (`ipc_mqueue_receive_continue` → `mach_msg_receive_continue`
       → `thread_syscall_return`), all of which are still real kernel
       (EL1) C code that the still-live `IOKITDIAG16` prints should have
       caught before any EL0 transition could occur, and never did.
       Two live, unresolved possibilities, neither confirmed: (a) the
       fault is right at/inside the actual `eret`-style EL1→EL0 return
       transition itself (past the last point pure C instrumentation can
       safely observe without risking further perturbing the race), or
       (b) — given finding 2's confirmed nondeterminism — the thread that
       actually faults at the moment of the panic in a given boot isn't
       always the second thread at all; it could be the MAIN thread
       (racing concurrently, e.g. while first entering its own MIG
       server dispatch loop to receive+reply to `handle_kqueue`'s
       request, a code path that has NOT yet been instrumented or
       confirmed to even execute in any log gathered so far -- no log
       shows a second `mach_msg_overwrite_trap` call tagged with the
       main thread's own `current_thread()` pointer after its first,
       successful one).

    **Real next step, not yet attempted**: this is now genuinely
    arm64 exception/context-switch-level work, a different tier from
    everything in this investigation so far (C-level kprintf bisection
    is reaching its practical limit -- the next boundary, the real
    `eret`/exception-return assembly and `machine_switch_context()`,
    needs either raw register-state capture directly from the fault
    handler (find wherever this simplified kernel's EL0 data-abort path
    converts a fault into `OS_REASON_SIGNAL` and log the real
    `ESR_EL1`/`FAR_EL1`/`ELR_EL1` there, before it gets abstracted away
    into a generic "signal 11" description) or a lower-level, lock-free
    print primitive safe to call from inside the actual context-switch
    path itself (kprintf's own lock may not be safe to take from
    `thread_invoke`/`machine_switch_context` -- not yet verified either
    way). Confirming/ruling out possibility (b) above (instrumenting the
    main thread's own MIG server dispatch loop, not yet touched at all)
    is a real, comparatively cheaper next step to try FIRST, before
    committing to assembly-level fault-handler instrumentation.

    Repo state: real, permanent fixes from this update (`runvm.sh`'s
    `-enable_kprintf_spam` addition) are committable now; the
    `IOKITDIAG15`/`16`/`17` kprintf additions in
    `third_party/xnu-7195/osfmk/ipc/mach_msg.c` and `ipc_mqueue.c` are
    live TEMP DIAGNOSTIC, same standing convention as everything else in
    this investigation.

    **UPDATE 2026-08-29, later still: possibility (b) tested and
    RULED OUT — cheap (launchd.macho-only, no kernel rebuild) real
    PureDarwin-side instrumentation, two more real findings.**

    Traced the real dispatch chain for who's supposed to service
    `launchd_internal_port`/`ipc_port_set`: `launchd.c`'s `main()` calls
    `launchd_runtime()` → `launchd_runtime2()` (`runtime.c`), a real
    `for (;;)` loop calling `xpc_pipe_try_receive(ipc_port_set, ...,
    launchd_mig_demux, ...)` — this is the main thread's own receive
    loop, and it's real, unmodified-shape Apple/PureDarwin source, not
    fabricated. Added `iokit_diag8()` prints at its entry and around the
    `xpc_pipe_try_receive` call (real, existing helper already used
    throughout this file, no new infrastructure needed).

    **Finding 1**: across every log gathered with this instrumentation
    (`qemu/launchd_boot_test60.log`), the main thread's own
    `launchd_runtime2` entry print NEVER fires before the panic — the
    main thread is still doing earlier sequential `main()` setup
    (`pid1_magic_init`, then a `pthread_create()` for a previously-
    unnoticed THIRD real thread, `update_thread` -- see finding 2 --
    then `pd_launchd_boot()`/`pd_launchd_load_daemons_dir()`) and hasn't
    reached its receive loop at all by the time the second thread dies.
    **This rules out possibility (b) as originally framed** (main thread
    racing inside its OWN MIG dispatch/receive) — it hasn't started that
    loop yet.

    **Finding 2, a real, previously-missed detail**: `launchd.c`'s
    `main()` spawns a THIRD real thread via the same `pthread_create()`
    mechanism as the second (`kqueue_demand_loop`) — `update_thread`
    (`pthread_create(&t, NULL, update_thread, NULL)`, right after the
    "`*** launchd[1] has started up ***`" log line, i.e. very close in
    wall-clock time to where the second thread's blocking receive
    happens). Reopened possibility (b) in a broader form: not "main
    thread racing in its dispatch loop" but "main thread racing while
    CREATING a third thread, concurrently with the second thread's
    context-switch/block — genuine SMP concurrency in real kernel data
    structures (`threads` global list, run queues, turnstile subsystem)
    this project's port may not have been exercised against before,
    since nothing prior to this investigation ever had 2+ real threads
    actually contending for kernel-side thread-management state at the
    same wall-clock moment on this 4-core target." Added
    `iokit_diag()` prints immediately around the `update_thread`
    `pthread_create()`/`pthread_detach()` calls to test this directly.

    **Result (`qemu/launchd_boot_test61.log`): also ruled out.** The
    "about to call pthread_create for update_thread" print never fires
    either — the main thread doesn't even reach LINE 316 (barely 30
    lines of straight-line code, mostly `if` checks and log calls, past
    the "started up" print) before the second thread's crash happens.
    Combined with finding 1, this is strong evidence the crash's timing
    is dictated entirely by the second thread's own blocking-receive
    transition, not by anything concurrent the main thread is doing —
    the main thread simply never gets far enough, regardless of how
    little or much code sits between the last print it does reach and
    whatever comes next, across every one of this session's tests.
    **Both forms of possibility (b) are now ruled out with real evidence
    (not just re-run and hope) — the fault is not a race with the main
    thread doing anything in particular.**

    This pushes essentially all remaining weight onto possibility (a)
    from the prior update: the fault is at or inside the actual
    `eret`-style EL1→EL0 return transition for THIS
    `bsdthread_create()`-spawned thread's very first continuation-based
    block, past what plain C-level `kprintf` bisection can safely reach
    without either (i) raw `ESR_EL1`/`FAR_EL1`/`ELR_EL1` capture directly
    in whatever this simplified kernel's fault handler does to convert
    an EL0 data abort into `OS_REASON_SIGNAL`, before that detail is
    lost, or (ii) a lock-free print primitive proven safe to call from
    inside `thread_invoke`/`machine_switch_context` itself. Neither
    attempted yet — genuinely different, higher-risk tier of kernel work
    (exception-vector/context-switch assembly) than anything in this
    investigation so far, flagged here for an explicit decision before
    continuing rather than attempted unprompted.

    Repo state: `launchd.c`/`runtime.c` (submodule `third_party/
    PureDarwin`) diagnostic additions from this update are real,
    live TEMP DIAGNOSTIC, not yet committed as of this writing (see
    project memory for exact commit status at any given resume point).

    **UPDATE 2026-08-29, final update this session: raw fault-register
    capture at the real EL0 abort handler — ROOT CAUSE FOUND (not yet
    fixed). The second thread's block was a red herring the whole time;
    the actual crash is the MAIN thread, and the evidence points at a
    real kernel context-switch bug (thread-stack cross-contamination),
    not a race, not a threading-setup gap.**

    Added a real diagnostic directly in `osfmk/arm64/sleh.c`'s
    `handle_user_abort()` — the real, unmodified-Apple EL0 data/
    instruction-abort handler, right before `exception_triage()` (the
    call that turns an unresolved fault into the real Mach exception ->
    `OS_REASON_SIGNAL` -> the exact "pid 1 exited... signal 11" path
    confirmed real back in the prior update). Prints `current_thread()`,
    the real fault PC/SP/FP/LR (`get_saved_state_pc/sp/fp/lr()`), `esr`,
    `fault_addr` (FAR), and the `vm_fault()` result code — the actual
    raw register state at the moment of the real fault, one tier below
    anything C-level bisection could see before.

    **Result (`qemu/launchd_boot_test62.log`/`test63.log`): decisive.**
    `esr=0x92000047` decodes to `ESR_EC_DABORT_EL0` (confirmed real EL0
    data abort, matching the signal-shaped exit reason) with DFSC =
    "Translation fault, level 3" (the page is genuinely never mapped,
    not a permission fault) and the WnR bit set (a real WRITE, not a
    read). `pc` resolves (via `nm -n` against `launchd.macho`, cross-
    referencing dyld's own real init-order base-address prints in the
    same boot log to compute the real ASLR slide) to exactly
    `___vsprintf_chk+0xc` — i.e. real, very-early function-prologue
    stack-frame setup, not application logic.

    **The critical cross-reference**: `current_thread()` at the fault
    (`test63.log` line 901) is `0xfffffff2122f3540` — the EXACT SAME
    thread pointer as the MAIN thread's own earlier, successful
    `mach_port_move_member` call (line 848), NOT the second
    (`kqueue_demand_loop`) thread's pointer (`0xfffffff2122f1aa0`,
    confirmed still separately visible completing its own
    `about to call thread_block` sequence normally in the very same
    log). **The main thread is the one that actually crashes** — every
    previous session-4 finding about the second thread's blocking
    receive completing cleanly through real turnstile/waitq setup
    remains true and real, it was just never the direct cause; it was
    coincidentally the last diagnostic output before a SEPARATE,
    main-thread-only fault, which is exactly why the "main-thread races
    ruled out" update earlier this same session drew the wrong
    conclusion (it only checked reachability of print points AFTER the
    "started up" `launchd_syslog()` call, never diagnostics INSIDE that
    call itself, where the real fault turned out to live).

    **Why this points at a real kernel bug, not application logic**: the
    faulting `fp`/`fault_addr` (`0x16ebf4000`/`0x16ebf3ff8` in
    `test63.log`) sit **~18MB below `USRSTACK64`**
    (`bsd/arm/vmparam.h`'s real `0x16FE00000`) — physically impossible
    for the main thread's own process stack to reach under this
    embedded-config target's real `MAXSSIZ` (1MB, same file). `fp` is
    also exactly page-aligned (both 4K and 16K), matching the shape of
    `pthread_create()`'s own `stack_top` computation
    (`tools/userland_staging/pthread_main_thread_bootstrap.c`:
    `stack_base + PTHREAD_CREATE_STACK_SIZE(256KB)`, 16-byte-aligned) —
    i.e. the address pattern of the SECOND thread's own dedicated
    mmap'd stack, not anything belonging to the main thread. Added one
    more (cheap, userland-only) diagnostic to
    `pthread_create()`/`iokit_diag14_hex` in
    `pthread_main_thread_bootstrap.c` + `launchd_mach_port_compat.c` to
    print the real `stack_base`/`stack_top` for direct same-boot
    comparison against `handle_user_abort`'s `fp`/`fault_addr` and
    confirm the exact-match hypothesis definitively — written but NOT
    YET boot-tested as of this writing (session ended here on an
    unrelated tooling interruption, not a technical blocker; the test
    itself is cheap, launchd.macho-only, no kernel rebuild needed).

    **Working theory, not yet proven**: when `thread_block()` switches
    the CPU away from the second thread (which legitimately, correctly
    blocks) to the main thread (the next runnable thread, since this
    kernel is confirmed single-core — only `cpu0` ever starts, per this
    boot log's own `"BCM2837CPU: started \"cpu0\""` line, ruling out a
    genuine SMP/multi-core cache-visibility race as a competing
    explanation), the kernel's arm64 context-switch/register-save-
    restore path does not correctly keep the two threads' saved user-
    mode `SP_EL0` values separate — the main thread appears to resume
    executing with (or has its own saved state corrupted by) the second
    thread's `SP_EL0`, pointing into the second thread's own private
    256KB stack instead of the main thread's real one. The very next
    real stack-frame-setup instruction on the main thread's resumed
    execution (`___vsprintf_chk`'s prologue) then either legitimately
    stack-overflows past the second thread's small 256KB region
    (if it actually IS writing into that region but past its bounds) or
    hits a boundary specific to how that region was mapped — either way,
    a real, previously-undiscovered bug in this project's `thread_invoke`/
    context-switch machinery (`osfmk/arm64/machine_routines_asm.s` or
    wherever `machine_switch_context`/`Thread_continue`-equivalent
    saves/restores `SP_EL0` per-thread), not anything in
    launchd/PureDarwin application code and not the second thread's own
    setup (already independently confirmed correct through exhaustive
    C-level bisection earlier this session).

    **Real next step**: (1) boot-test the already-written
    stack_base/stack_top vs fp/fault_addr comparison (cheap, no kernel
    rebuild) to convert "working theory" into "confirmed" -- this is
    the very next action, blocked only by an unrelated tooling
    interruption this session, not by any remaining technical unknown;
    (2) if confirmed, find and fix the real `SP_EL0` save/restore bug in
    the arm64 context-switch path -- likely either `thread_invoke()`'s
    own machine-state handoff (`osfmk/kern/sched_prim.c`) or the
    architecture-specific `machine_switch_context`/PCB save-restore
    (`osfmk/arm64/machine_routines_asm.s`,
    `osfmk/arm64/status.c`/`cpu_data.h`'s `cpu_active_thread`
    bookkeeping) -- not yet located precisely, real fix work, not just
    more diagnosis, once step (1) confirms the theory.

    **UPDATE 2026-08-29, session 5: step (1) done, theory REFINED not
    confirmed as originally stated -- one real userland bug found+fixed
    along the way (kept), but bug #6 itself is still open.**

    Ran the already-written `stack_base`/`stack_top` vs `fp`/`fault_addr`
    comparison. It never printed at all (`IOKITDIAG19` absent from
    `launchd_boot_test64.log`/`65.log`) -- root-caused to a *second*,
    independent real bug: `libpthread.a` (a plain static archive, linked
    *before* `system/libsystem_pthread.dylib` in `build_launchd.sh`) was
    shadowing the dylib's real, shared `pthread_create` -- ld64 pulled
    `pthread_main_thread_bootstrap.o` out of the archive to satisfy
    `core.o`'s undefined `_pthread_create` reference before the dylib was
    ever considered, baking a second, independently-`__bsdthread_register`'d
    copy of the whole pthread bootstrap path into `launchd.macho`
    (confirmed via `nm`: both `launchd.macho` and the dylib defined a
    strong `T _pthread_create`). Exact same bug class as this same
    script's own prior `mig_reply_port.o`/`mach_msg_real.o` fixes -- see
    memory `feedback_suspect_duplicate_static_copy_early` for the
    standing rule this earned. **Fixed** (iokit main repo commit
    `a07bc1c`): narrowed a launchd-only copy of `libpthread.a`
    (`ld -r -unexported_symbol ... -keep_private_externs`, demoting the
    duplicated functions to private-extern while preserving the shared
    DATA symbols `pthread_tsd.o`/`pthread_clear_qos_tsd_compat.o` still
    need) so `pthread_create` now genuinely resolves to the dylib's one
    real copy at runtime (verified: the launchd-only diagnostic that can
    only fire from the old local copy no longer does).

    **This did NOT fix bug #6** -- `launchd_boot_test66.log` shows the
    identical crash (same thread pointer, same fault pc/fp/fault_addr
    pattern) even with only one canonical `pthread_create`/
    `__bsdthread_register` call site in the whole process. This falsifies
    the specific "two racing `__bsdthread_register` calls" mechanism, but
    the underlying observation (main thread's `SP_EL0` ending up pointing
    at what looks exactly like the second thread's own private stack) is
    unchanged and still real.

    Went one level deeper at the kernel C level (xnu-7195 submodule
    commits `6e4983dec`, `6bad23b75`, both real diagnostic-only changes,
    both boot-tested): added `thread->machine.contextData` (the real,
    per-thread `zalloc(user_ss_zone)`-allocated PCB, `osfmk/arm64/pcb.c`)
    and `thread->kernel_stack` (the real, per-thread
    `kernel_memory_allocate`-backed kernel stack with guard pages,
    `osfmk/kern/stack.c`) to both `IOKITDIAG17` (second thread, right
    before its own `thread_block()`) and `IOKITDIAG18` (main thread, at
    the fault), for a direct same-boot comparison. **Both ruled out**:
    `contextData` differs (`0xfffffff214184470` vs `0xfffffff214184b10`,
    `test67.log`) and `kernel_stack` differs (`0xfffffff851c20000` vs
    `0xfffffff851c30000`, 64KB apart, `test68.log`) -- the two threads'
    per-thread state is genuinely, separately allocated, not aliased.
    Also traced (read-only) `save_general_registers`/
    `load_general_registers` (`osfmk/arm64/cswitch.s`) and confirmed they
    correctly include `x21` (the exception-return machine-context
    pointer) in the real AAPCS-64 x19-x28 callee-saved set, and
    `arm64_thread_exception_return` (`osfmk/arm64/locore.s`) explicitly
    recomputes `x21 = current_thread()->machine.contextData` fresh on
    every continuation-resume, not a stale carried-over value. Every
    generic mechanism traced so far looks like real, correct,
    unmodified-looking Apple code -- no bug found in it yet.

    **Real, still-open next step**: the corruption is proven to be a
    *value* written into the main thread's own, correctly-separate PCB's
    `SS64_SP` field -- not a pointer-aliasing bug of any kind checked so
    far.

    **UPDATE 2026-08-29, session 5 continued: the `Switch_context`
    bracketing attempt itself caused a real, SEPARATE, 100%-reproducible
    crash -- reverted, not a lead on bug #6 itself.** Added
    `kprintf()`-based before/after prints of `thread->machine.upcb`'s
    `SS64_SP` around the real `Switch_context()` call in
    `machine_switch_context()` (`osfmk/arm64/pcb.c` -- confirmed via
    `osfmk/conf/files.arm64` this is the real, compiled-for-this-target
    file; `osfmk/arm/pcb.c`'s near-identical twin is dead armv7-only
    code here), gated to `task != kernel_task` on both sides to avoid
    firing during early boot. Caught by direct user question ("is one
    specific print line crashing launchd -- does removing it fix it?"):
    `launchd_boot_test70.log`/`71.log` showed a NEW crash, byte-identical
    modulo ASLR across two separate boots (i.e. newly, fully
    deterministic), in `load_init_program_at_path` -- PID 1's own very
    first thread, resumed with `resumed_sp=0x0` (real, but premature --
    before `exec`'s own machinery finishes setting the real initial user
    SP) -- immediately after the added `IOKITDIAG21` print. Reverted the
    print (xnu-7195 submodule commit `0483290d5`); `launchd_boot_test72.log`
    confirms both (a) the new crash is gone and (b) bug #6 itself
    reproduces again, unchanged (`panic(` match, same fault pc/fp/
    fault_addr pattern, `contextData`/`kernel_stack` still separately
    allocated) -- **confirming bug #6 is real and independent of any of
    this session's instrumentation**, not an artifact, since it
    predates all of it (session 2/3/4) and reproduces with or without
    the reverted print.

    **Lesson for next attempt**: `kprintf()` itself is evidently not
    safe to call from inside `machine_switch_context()` this early in a
    brand-new thread's life, independent of whether the specific data
    being read is valid -- a `task != kernel_task` guard is not
    sufficient by itself; also needs to exclude a thread's own first-
    ever resume (e.g. check the thread has a real, non-zero
    `SS64_SP` -- or better, avoid instrumenting inside
    `machine_switch_context()`/`Switch_context()` directly at all and
    find another way to compare a thread's own saved SP immediately
    before it yields vs. immediately after it resumes, e.g. from
    `ipc_mqueue.c`'s own already-safe call sites bracketing specific,
    already-identified thread activity rather than every switch
    process-wide.

# Fourth archive pass -- 2026-08-30 (static-linking cleanup, Phase 1d/2a)

Archived from `docs/backlog.md` item d1 (resolved 2026-08-30, see the
condensed pointer left in place there and
`docs/static-linking-cleanup-plan.md`'s 2026-08-30 session section for
the fix trail):

d1 (original text). **`libdispatch.dylib` duplicates process-global state
    with its own sibling dylibs — real heap-corruption-class risk, same
    shape as the bug `build_libsystem_malloc.sh` already fixed once.**
    `tools/userland_staging/build_libdispatch_dylib.sh` builds a real
    `libdispatch.dylib` (`-nostdlib`, MH_DYLIB confirmed via `otool`) but
    statically bundles its CURRENT dependency stack
    (`libmach_ipc.a`/`libclosure.a`/`libmalloc.a`/`libplatform.a`/
    `libpthread.a`/`libc_gen.a`/`libc_string.a`/`libc_stdio.a`) rather
    than linking against this project's REAL sibling dylibs
    (`libsystem_kernel.dylib`/`libsystem_platform.dylib`/
    `libsystem_malloc.dylib`/`libsystem_pthread.dylib`/
    `libsystem_c.dylib`, all already built). That means any real process
    loading both `libdispatch.dylib` and one of those sibling dylibs
    gets two independent copies of `mach_task_self_`, `g_zone` (the live
    heap zone — see `build_libsystem_malloc.sh`'s own header comment for
    why that specific duplicate is real heap-corruption territory, not
    just "wrong value"), `errno`, etc. Real fix: relink against the
    sibling `.dylib`s (`-l`-style, not static bundling) instead of their
    static ancestors.

---

# Fifth archive pass -- 2026-08-31 (full migration to Linear)

Every item in `docs/backlog.md` as it stood at this point (P0/P1/P2/P3
numbered items, all `d`-prefixed userland-library-debt items, and both
`### d10`/`### d11` locale/launchd sections) was migrated one-by-one into
Linear issues (Darwin project, team DAR) as real, individually-detailed
tracked work items -- not summarized or lumped. Every currently-open item
got a `bug` label, a native Linear priority (P0->Urgent, P1->High,
P2->Medium, P3->Low, matching this doc's own tiering exactly), and at
least one system-area label (`kernel`, `storage`, `usb`, `network`,
`crypto`, `launchd`, `dyld`, `pid1-init`, `static-linking`, `daemon`,
`userland-libc`, `userland-commands`, `iokit`, `auth`, or
`duplicate-symbols` as appropriate). Items already marked
FIXED/RESOLVED/DONE in the text below were NOT re-filed (they're already
closed, real work, just kept here for history). `docs/backlog.md` itself
was replaced with a short pointer to Linear as of this pass -- this
section is the full verbatim snapshot of its final content, unedited.

Full verbatim content of `docs/backlog.md` at the time of this migration
follows:

```
Resolved items moved to `docs/historic/backlog.md` — see there for fix history
(most recently the third archive pass, 2026-08-30: P0 items 1/2/4/5, and
items d7/d8/d9/0a/0b plus item d11's session-2-through-5 investigation trail).

# Backlog

Open items found but not yet fixed, mostly surfaced by two adversarial
code reviews on 2026-08-13 (one on `BCM2837SDHCI`/the boot chain, one on
the rest of the custom IOKit kexts) plus a few longer-standing known gaps.
Organized by how much it matters, not by discovery order. File:line
references are as of commit `d472f37`; re-check before trusting them if
this list is stale (this pointer itself is only updated when someone
actually re-verifies line numbers, not on every commit — treat it as "not
older than" rather than "current as of").

---

## P0 — real-hardware risk (would plausibly hang, storm, or panic a real Pi)

1. **[FIXED 2026-08-27, QEMU-verified]** `BCM2837SDHCI::doAsyncReadWrite()`
   had no serialization at all (no `IOCommandGate`, no workloop, no mutex);
   fixed with `fTransferLock`, a plain `IOLock` held for the whole
   command-issue-through-completion span. **Verification caveat**: nothing
   in this project issues genuinely concurrent I/O, so the QEMU boot test
   confirms no regression, not that the interleaving race is exercised.
   Full writeup: `docs/historic/backlog.md`'s "P0, items 1/2/4/5" section.

2. **[FIXED 2026-08-27, QEMU-verified]** No error recovery anywhere in
   `BCM2837SDHCI` — every error path was `IOLog(); return false;`. Fixed
   with `resetCmdLine()`/`resetDatLine()` (SDHCI spec 3.7.1, matching
   Linux's `sdhci_reset()` split), wired into every real error/timeout
   path. **Verification caveat**: QEMU's SD model injects no real CRC/
   timeout/card-removal errors, so this is verified only as "the reset
   paths compile and don't regress a clean boot". Full writeup:
   `docs/historic/backlog.md`'s "P0, items 1/2/4/5" section.

3. **NOT FIXED — deliberately, after real investigation, not just
   deferred.** `BCM2837SDHCI`'s SD clock still never leaves ~490 kHz /
   1-bit; `setClock()` still always writes divisor `0xFF` (re-confirmed
   unchanged), and `kRegHostControl` is still never written. Investigated
   whether a real, low-risk fix was possible this pass (per this backlog
   entry's own instructions: write one if "the right fix is clear and
   low-risk", defer with a status note otherwise) and concluded it is
   **not** clear/low-risk here, so no code was changed: computing a
   correct data-phase divisor requires knowing the controller's real base
   clock frequency, and this SoC is `missing_caps=true` (the
   `CAPABILITIES` register's base-clock field is explicitly untrustworthy
   per the Linux `sdhci-iproc` quirk this driver already documents) — the
   real base clock is set by VideoCore firmware at boot and can vary by
   board/firmware config (commonly cited real-hardware values range
   ~41-50 MHz depending on `core_freq`), and this project has no
   VC-mailbox driver reachable from this kext to query it for real (see
   `tools/sdhci_baremetal/README.md`'s Finding, and backlog item 8's
   identical gap for `SMSC9514Ethernet`'s MAC address). Hardcoding a
   guessed base clock and computing a divisor from it risks programming a
   data-phase clock *faster* than the card/bus can actually handle on
   some real boards — active data corruption, strictly worse than today's
   always-conservative divisor `0xFF`, and exactly the kind of fabricated,
   unverifiable-from-QEMU guess this project's conventions exist to
   avoid. Same reasoning blocks widening the bus via `kRegHostControl`
   without also negotiating `ACMD6` with the card first (untested
   protocol addition, same class of real-hardware-only risk). Real fix
   needs a real VC-mailbox "get clock rate" query (or a bootloader-stashed
   DT `clock-frequency` property) wired to this driver first — not
   attempted, scoped here for whoever picks up the mailbox work.

4. **[FIXED 2026-08-27, QEMU-verified]** No power-on ramp / 74-clock delay
   before `CMD0`, and power was enabled *after* the clock in `start()`
   rather than before. Fixed by reordering `start()` and adding real
   `IODelay()`s. Only changes ordering/idle delays, not signal rate/timing,
   so unlike item 3 there's no way a wrong guess here makes real hardware
   worse. Full writeup: `docs/historic/backlog.md`'s "P0, items 1/2/4/5"
   section.

5. **[FIXED 2026-08-27, code correct but genuinely unverifiable from
   QEMU — exactly as this item originally predicted.]** No memory barrier
   in `BCM2837InterruptController::enableVector()`/`disableVectorHard()`
   before the next `IRQ_PENDING` read; fixed with
   `__asm__ volatile ("dsb sy" ::: "memory")` after each register write.
   All MMIO is strongly ordered under QEMU/TCG regardless of the barrier.
   Full writeup (including a correction to this item's own original "no
   barriers anywhere" claim): `docs/historic/backlog.md`'s "P0, items
   1/2/4/5" section.

6. **Re-verified, not re-tested (inherently real-hardware-only).** GPIO
    pinmux for the SD slot is still only ever exercised in QEMU, where
    it remains a documented no-op (`tools/sdhci_baremetal/README.md`'s
    own from-scratch bare-metal harness independently confirms this
    QEMU model parents the card onto `sdhci` by default regardless of
    GPFSEL). Re-read `pinmuxSDSlot()` (`iokit/BCM2837SDHCI/BCM2837SDHCI.cpp`)
    end to end: the GPIO34-39-to-input fix for the WiFi-collision bug is
    still in place, unchanged, and the reasoning still holds (GPFSEL3
    bits for GPIO34-39 set to input `0b000` before GPFSEL4/5 claim
    GPIO48-53 as ALT3). Still genuinely untested on real silicon — no
    QEMU boot can close this, confirmed again this pass rather than
    assumed.

**On items 1, 2, 4, 5 above**: all four were rebuilt together and verified
with one real QEMU boot — real login, real reads against both mounted
partitions (`disk0s1` root / `disk0s2` boot), zero panics, `BCM2837SDHCI`
log confirming a real hardware interrupt and correct card identification.
That confirms no regression from the four changes together; it does not
and cannot exercise the specific fault conditions items 1/2/5 exist to
guard against, since QEMU reproduces none of them. Full text archived to
`docs/historic/backlog.md`'s "P0, items 1/2/4/5" section.

7. **Real, reproducible kernel PANIC (not a hang) from sustained fast
    console output — found 2026-08-16 while QEMU-verifying the real
    `yes` command (Phase 4 step 3, `docs/roadmap.md`).** `yes.macho`
    (real, unmodified Apple `shell_cmds/yes/yes.c` — a tight `puts("y")`
    loop, nothing unusual) reliably panics the kernel within seconds of
    starting: `"Processing of an interrupt (type = 2, handler address =
    0xfffffff00732d170, vector = 0) took 397932291 nanoseconds (timeout =
    354986666 ns)"` — the real `INTERRUPT_MASKED_DEBUG` watchdog (the
    same mechanism `cpu-factor=10` already tunes for ordinary QEMU/TCG
    jitter, per `CLAUDE.md`'s "Boot / test commands" section) firing for
    real, not a timing false-positive: ~400ms with interrupts
    provably masked is a genuine, large stall, not jitter. A single
    ordinary userspace process writing to its own stdout as fast as
    possible should never be able to mask interrupts for hundreds of
    milliseconds, on any real system — and the handler address resolves
    (via `nm` against this session's kernel build, not guessed) to
    `_rtclock_intr+0x0` exactly, i.e. the real-time-clock/timer
    interrupt's own handler, not the console/UART path as first
    suspected. The panic message's own semantics ("processing of an
    interrupt ... took N ns") means `rtclock_intr()` itself, once
    entered, took ~400ms to return — real per-tick scheduler/timer
    bookkeeping inside that handler is what's slow under this specific
    load, not something masking interrupts before it ran. Leading
    hypothesis, not confirmed: `yes`'s extremely tight `write()` loop
    (one syscall per line, no batching, real `puts()`+`fflush` per
    iteration) drives an unusually high trap/context-switch rate, and
    something per-tick inside `rtclock_intr`'s real scheduling
    accounting scales with that rate in a way this project has never
    exercised before (no prior Phase 4 command loops anywhere near this
    fast). Not root-caused or fixed this pass — flagging as P0 because
    it's a real userspace-triggerable kernel panic, not merely a
    slow/ugly path, and because real hardware's timer tick rate is not
    obviously immune to the same mechanism (unlike most of this
    section's QEMU-vs-real-hardware caveats, which lean on QEMU-specific
    timing slop).
    `yes.macho` still ships (the bug is not in `yes` itself, which is
    real, correct, unmodified source) but should not be treated as
    "verified safe to run" the way this project's other Phase 4 step 3
    commands are — anyone hitting this again should start from the exact
    panic string above and `nm`/`atos` the handler address against the
    current kernel build rather than re-diagnosing from scratch.

    **UPDATE 2026-08-27: does NOT currently reproduce, after real,
    sustained re-verification — not closing this out, but downgrading
    the "reliably panics within seconds" claim, which is now stale.**
    Per this backlog's own "verify claims against source, not by date/
    name proximity" rule, re-ran the exact real repro (`yes.macho`, real
    unmodified source, at the console) against the CURRENT kernel build
    rather than assuming the 2026-08-16 finding still holds — it does
    not, at least not under this same QEMU config:
    - First re-derived the current handler address the same honest way
      this item asks for: `nm build/xnu-latest-build/out/
      kernel.development.bcm2837-7195.unstripped | grep _rtclock_intr`
      → `0xfffffff007335230` (shifted from the original
      `0x...732d170`, exactly as expected — confirms the address in
      this item's own text was already stale before this pass, matching
      the note right above this update).
    - `yes | head -c 2000000 > /dev/null` (real pipe, output never
      touches the console) ran clean, no panic — first real signal that
      console I/O specifically, not just high syscall/trap rate alone,
      is implicated (matches the original finding's own handler-address
      evidence pointing at `rtclock_intr`, but narrows what actually
      drives it).
    - `yes` run bare at the real console (exact original repro shape)
      for **100 real seconds**: no panic, `[boot_test] PASS`.
    - Added a real, cheap TEMP DIAGNOSTIC to `third_party/xnu-7195/
      osfmk/kern/timer_call.c`'s `timer_queue_expire_with_options()`
      (kept in place per this project's existing convention of leaving
      diagnostic instrumentation rather than reverting it — see this
      file's own P3#3 entry) to test the leading hypothesis directly:
      an unbounded/growing number of `timer_call` entries piling up on
      the per-CPU rtclock queue (each real, legitimate, but summing to
      hundreds of ms if there are enough of them) rather than genuinely
      unbounded per-entry cost — logs `queue->count` and the loop's real
      iteration count whenever either exceeds 32 in one call. Rebuilt
      the kernel with this in place and re-ran `yes` bare at the console
      for **210 real seconds** (25.5MB of real console output, `[boot_test]
      PASS`) — the diagnostic **never fired once**: the per-CPU timer
      queue never exceeded 32 pending entries at any point in 3.5
      minutes of the exact workload that used to panic within seconds.
      `BCM2837SDHCI` still shows a real hardware interrupt and a correctly
      identified card partway through this same boot, confirming items
      1/2/4/5's fixes above are intact in this build and this is a real,
      unmodified boot, not a degraded one.
    - **Working hypothesis for why, not fully confirmed**: this
      kernel has changed substantially since 2026-08-16 — most notably
      `xnu-7195` commit `d7c6934d8` ("Switch root from mockfs to real
      msdosfs, **fix console I/O plumbing for pid 1**"), which landed
      afterward for unrelated Phase 5 reasons but, per its own commit
      message, touched exactly the console I/O path this item's own
      panic evidence pointed at (`rtclock_intr` handler address, high
      syscall rate from console writes) before ever pointing at a
      confirmed root cause inside `rtclock_intr`/`timer_intr` itself.
      Also relevant: the original finding predates Phase 6b (INET
      enabled 2026-08-21) entirely, so today's per-CPU timer queue
      actually carries *more* background load (network timers) than it
      did during the original repro, yet still never panics — evidence
      against "more timer load" being the driver, consistent with the
      bug instead being specific to whatever the console I/O plumbing
      fix changed.
    - **Not claiming root-caused or intentionally fixed** — no code
      change was made to actually address this item (the TEMP
      DIAGNOSTIC is observation-only), and it's possible a different
      workload, a longer run, or real hardware's own timing could still
      trigger it. Downgrading from "reliably panics within seconds" to
      "not reproducible in 210 real QEMU seconds against the current
      build, despite a real, honest attempt" — a meaningfully different,
      weaker claim than this item's original text, which is why this
      update doesn't just delete the original finding. Whoever revisits
      this should re-run the exact repro above first before assuming
      either "still broken" or "fixed" — both the original finding and
      this update were real, evidenced observations at their respective
      times.

---

## P1 — real correctness bugs (not hardware-specific, will eventually bite)

1. **`tools/init_binary/init.c` (PID 1) has a few real rough edges**,
    lowest priority since it's bootstrap scaffolding Phase 4 will replace:
    - `read_line()`: a `sys_read` error/EOF makes `shell_main()`'s
      `if (n < 0) continue;` an unthrottled tight loop re-printing the
      prompt at 100% CPU on any console where input isn't polled (masked
      today by `serial=3` always being passed).
    - `init_main()`'s `sys_wait4()` return value is discarded, and there's
      no respawn-rate throttle — a child that dies instantly (e.g. `ECHILD`)
      would fork-loop unthrottled. Classic `init` rate-limits this.
    - The raw-syscall inline-asm wrappers (`syscall3`/`syscall4`/`sys_fork`/
      `sys_wait4`) don't mark the `is_error`/status output operand with an
      early-clobber (`&`) constraint. Currently safe (the compiler happens
      to consume inputs before the clobbered write), but fragile across a
      compiler upgrade — add `&` to be robust rather than lucky.
    - The boot banner still says `"booted via mockfs"` — stale since root
      moved to real HFS+ (`d7c6934d8`). Cosmetic, but it's the first thing
      a human sees.

2. **`SMSC9514Ethernet` has two real, confirmed-still-open logic bugs**
    (six other sub-items from this entry's original 2026-08-22 filing
    have since resolved one at a time — PHY-reset timeout handling,
    MDIO/auto-negotiation wiring, `attachInterface()` never called,
    packet-filter shadowing, a suspected `fInterface` UAF that turned
    out not to be a real race, real-hardware link-up/ping verification,
    and the 2026-08-25 RX-completely-dead report — full history archived
    to `docs/historic/backlog.md`):
    - `smsc95xx_reset()`-equivalent code does blind register writes to
      `HW_CFG`/`PM_CTRL`/`MAC_CR` where Linux does read-modify-write,
      clobbering unrelated bits (`BIR`/`RXDOFF`/`MEF`, full-duplex/
      auto-speed defaults). Confirmed still true 2026-08-27 (`resetChip()`
      still writes `kMacCrTxEn | kMacCrRxEn` directly, no RMW).
    - `frameLen & 0x7FF` truncates silently with no length guard.

3. **Six `g_crypto_funcs->` call sites have zero null-checking at all**
   (filed 2026-08-28, found while closing `docs/backlog.md` item 0a's AES
   gate): `libkern/crypto/corecrypto_des.c`, `corecrypto_rsa.c`,
   `corecrypto_aesxts.c`, `corecrypto_chacha20poly1305.c`,
   `bsd/nfs/gss/ccrypto.c`, and `bsd/netinet/flow_divert.c` all
   dereference `g_crypto_funcs->somefield` directly — not even a bare
   `if (g_crypto_funcs)` guard, let alone a check of the specific field
   — because they were written assuming real macOS's corecrypto kext
   always fills every field atomically. Now that `g_crypto_funcs` is a
   real, non-NULL, *partially*-filled table (AES ecb/cbc/ctr only, see
   item 0a), every one of these is a live NULL-pointer function call the
   instant anything reaches DES, RSA, AES-XTS, ChaCha20-Poly1305, NFS
   GSS-API crypto, or flow-divert's HMAC-SHA1 token signing — the exact
   same crash class (`Kernel instruction fetch abort at pc 0x0`) already
   hit once and fixed in `corecrypto_rand.c`'s `cc_rand_generate()`
   during this same work (that one *did* have a bare
   `if (g_crypto_funcs)` guard, just not a field-specific one — these
   six don't even have that much).

   Not currently reached by anything exercised through a full QEMU boot
   to login (verified), so this is latent, not currently crashing — but
   it is a real, load-bearing landmine for whoever next touches DES/RSA/
   AES-XTS/ChaCha20-Poly1305/NFS-GSS/flow-divert. Fix is mechanical
   either way: add a field-specific null check (`corecrypto_aes.c`'s
   `aes_encrypt_key()` is the reference pattern) before wiring up real
   functionality that would exercise the call site for the first time,
   the same guard `corecrypto_rand.c` needed once its own gap got found.

---

## P2 — known gaps / incomplete work (not bugs, just unfinished)

### Userland library debt (filed 2026-08-26, from the real libdispatch port + dylib conversion)

d1. **RESOLVED 2026-08-29/30 (static-linking cleanup, Phases 1a–1d,
    QEMU-verified).** `libdispatch.dylib` no longer statically bundles
    its dependency stack — it links the real sibling dylibs directly.
    `mach_task_self_`/`g_zone`/`errno` each now have exactly one real
    owner project-wide (errno's own fix, moving it fully into
    `libsystem_kernel.dylib`, landed 2026-08-30 and was verified via a
    full real QEMU boot — see `docs/static-linking-cleanup-plan.md`'s
    2026-08-30 session section for the full trail, including one real
    regression that boot test caught and fixed). Full detail on the
    original bug archived in `docs/historic/backlog.md`.

d2. **Real duplicate-symbol conflicts between archives built at
    different points in this project's history — surfaced while linking
    `libdispatch.dylib`, not yet de-duplicated for good.** Three
    confirmed, real cases (not fabricated exclusions — verified via `nm`
    before excluding each one):
    - `_pthread_key_init_np`: real in both `libpthread.a` (canonical)
      and `libc_locale_for_stdio.a`'s `pthread_tsd_shim.o` (an older
      compat copy from before `libpthread.a` had its own).
    - `_voucher_mach_msg_set` + `_mach_port_extract_right`/
      `_insert_right`/`_unguard`: real in both `libmach_ipc.a`'s own
      `mach_ipc_dead_stubs.o` (now fully redundant — all 4 of its
      symbols are superseded elsewhere, confirmed via `nm`) and the real
      voucher.c/MIG-generated bodies that superseded it.
    - `_vm_allocate`/`_deallocate`/`_map`/`_protect`/`_remap`/
      `_purgable_control`/`_read`, `_mach_port_mod_refs`/`_deallocate`:
      real in both `libmalloc.a`'s own `mach_vm.o`/
      `libmalloc_dispatch.o` and `dyld_core.a`'s `vm_mapUser.o`/
      `mach_portUser.o` (built for the dyld port, regenerated
      independently).
    `build_libdispatch_dylib.sh` works around all three via `ar x`
    exclusion or by simply not linking the conflicting object (see that
    script's own comments for exactly which).

    **Partially resolved 2026-08-30**: `_pthread_key_init_np`'s
    `libc_locale_for_stdio.a` copy (`pthread_tsd_shim.o`) is still real
    and still linked into `libsystem_malloc.dylib` (malloc sits *below*
    `libsystem_pthread.dylib` in the real dependency order and cannot
    link the real dylib back for this), but its 5 public POSIX names are
    now narrowed to private-extern via `ld -r -keep_private_externs
    -unexported_symbol`, so it can no longer shadow the real
    `libsystem_pthread.dylib` copy for any other image — see
    `docs/static-linking-cleanup-plan.md`'s 2026-08-30 session section.
    The `mach_portUser.o`/`vm_mapUser.o` MIG-regen duplication (second
    bullet) is also resolved the same session: `libsystem_kernel.dylib`
    now runs its own real `mig` regen of `mach_port.defs` and owns the
    whole `__kernelrpc_mach_port_*` set outright, and both `libxpc.dylib`
    and `libiokituser.dylib` dropped their private copies. Still open:
    picking one canonical provider for the `_vm_allocate` family
    specifically (`libmalloc.a` vs `dyld_core.a`'s `vm_mapUser.o`).

d3. **Standard POSIX/QoS pthread API entirely missing from this
    project's curated `libpthread.a`/`libsystem_pthread.dylib` — a real
    completeness gap, confirmed via `nm -g`, not assumed.** Surfaced by
    the real libdispatch port's own trial link
    (`tools/userland_staging/build_libdispatch_real.sh`'s own HONEST
    SCOPE STATEMENT has the full, current list). Two groups:
    - Standard, simple POSIX API with no real implementation anywhere
      in this project: `pthread_attr_init`/`_destroy`/`_setdetachstate`/
      `_setstacksize`/`_setschedparam`/`_setschedpolicy`/
      `_getschedparam`/`_getschedpolicy`/`_get_qos_class_np`/
      `_set_qos_class_np`, `pthread_exit`, `pthread_sigmask`,
      `pthread_setschedparam`, `pthread_get_stackaddr_np`,
      `qos_class_main`, `pthread_qos_max_parallelism`,
      `pthread_set_qos_class_self_np`.
    - Real QoS-override/workqueue-internals API, genuinely more
      involved (interacts with the kernel workqueue subsystem this
      project's `DISPATCH_USE_INTERNAL_WORKQUEUE=1` choice was
      specifically meant to avoid needing):
      `_pthread_qos_override_start_direct`/`_end_direct`,
      `_pthread_workqueue_override_start_direct`/`_reset`/
      `_start_direct_check_owner`, `_pthread_workloop_create`/
      `_destroy`, `_pthread_set_properties_self`,
      `_pthread_qos_class_encode`.
    Real Apple source for both groups should be in
    `third_party/libpthread` already (pinned repo) — this is a "port the
    missing files" task, not a "write from scratch" one; not yet
    attempted.

d4. **A handful of raw syscall traps this project hasn't generated
    stubs for yet, needed by the real libdispatch port.** `mmap`/
    `munmap` (raw trap objects exist -- `pthread___mmap.o`/
    `pthread___munmap.o` -- but only export the triple-underscore raw
    trap name, not the convenience wrapper libdispatch actually calls),
    `kevent_qos`, `clock_gettime_nsec_np`, `kpersona_get`,
    `thread_destruct_special_reply_port`. Small, mechanical, same
    create-syscalls.pl-based pattern already used throughout `tools/
    userland_staging/build_libxpc_syscalls*.sh`.

d5. **Real Mach voucher MIG routines not yet regenerated.**
    `mach_voucher_attr_command`/`_deallocate`/`_debug_info`/
    `_extract_attr_recipe`, `host_create_mach_voucher`,
    `host_get_special_port` -- real client routines from
    `mach_voucher.defs`/`host_priv.defs` (osfmk), same real mig-regen
    pattern already used for `job.defs`/`mach_port.defs`/
    `protocol.defs` elsewhere in this project; not yet attempted.

d6. **CLOSED 2026-08-26 (critical path) — launchd's full daemon +
    server-MIG source tree compiles and trial-links down to 109
    undefined symbols, from 266 at the start of this pass (a 59%
    reduction).** All 9 real daemon source files (`core.c` — 12,162
    lines, launchd's actual job-management engine — plus
    `pd_launchd_boot.c`/`kill2.c`/`pd_sandbox_check.c`/
    `pd_console_login.c`/`pd_launchd_plist.c`/`log.c`/`ipc.c`/
    `launchd.c`/`runtime.c`) and all 7 real SERVER-side MIG files
    compile clean and trial-link via `build_launchd_core.sh`/
    `build_launchd_syscalls.sh`/`launchd_libc_extras.a`. Real
    CoreFoundation is wired in (`libCoreFoundation.a`, see item d7) —
    confirmed launchd's actual CF usage (just `pd_launchd_plist.c`,
    plist parsing) doesn't need ICU. The full session-by-session fix
    history — 32+ syscall/libc gaps closed via selective `ar x`
    extraction and curated wrappers, real Mach host/port admin
    routines, `mktime`/`localtime`/`ctime_r` unblocked via a real
    `generate_features.pl` regen, a real from-scratch `ld2xpc()`/
    `xpc2ld()` confirmed genuinely absent from both the real
    `launchd-842.92.1` and PureDarwin trees, etc — is archived verbatim
    to `docs/historic/backlog.md`; this entry only tracks what's left.

    **What's left (109 symbols), categorized, every item individually
    investigated this pass — no unexplored territory**:
    - **Genuine scope walls** (separate, large undertakings, not bugs):
      real ICU (~50 symbols: CFCharacterSet/CFUniChar/CFLocale/
      CFNumberFormatter/StringEncodings-internal — since ported for CF
      generally, see item d7, but not yet re-verified against launchd's
      own trial link specifically); `asl_*` (6 symbols) — **CORRECTED
      2026-08-27, was wrong**: previously filed as "confirmed never
      open-sourced by Apple." That was true only in the narrow sense
      that no standalone `ASL` repo exists — the real ASL client library
      (`asl_open`/`asl_log`/`syslog()`, 14,555 real lines, Apple-
      licensed) is bundled inside `apple-oss-distributions/syslog`'s
      `libsystem_asl.tproj/src/` and is real and pinnable (pinned
      2026-08-27 as `syslog-385` for the `syslogd` port, see
      `docs/daemon-roadmap.md`'s Phase 6.7 syslogd section) — not yet
      wired into this project's CF/launchd trial links, but no longer a
      genuine scope wall, just unattempted.
    - **Deprioritized, real, peripheral**: BSM audit-record
      construction (`au_*`/`audit_session_*`, ~10 symbols) — needs a
      private header this project's pinned BSM repo doesn't ship.
    - **`libSystem.B.dylib` export-parity gap** (not launchd-specific,
      tracked under item d7) — mostly closed project-wide by the real
      `-reexport_library` fix (see d7); not yet re-verified against
      launchd's own trial link.
    - **Real, genuinely unimplemented feature in this exact PureDarwin
      branch** (not a build gap): `job_mig_look_up2_forward`/
      `job_mig_parent_forward` — real multi-instance-launchd job
      forwarding this project has no concept of (root-bootstrap only);
      needs the right MIG reply-client function identified first.
    - **Small, real, individually-scoped stragglers**: `asctime`/
      `asctime_r`/`strtoul_l` remain deferred (need the same deep
      `xlocale.c` locale-loading chain, tried+reverted twice);
      `xpc_copy` confirmed a real gap (no plain deep-copy function
      anywhere in either real repo, same class as `ld2xpc`);
      `initgroups`/`setiopolicy_np` confirmed to have no dedicated
      wrapper source anywhere in this project's pinned repos.

    **Bottom line**: launchd is not a zero-gap link (needs the real ICU
    port above, a separate, large undertaking — ASL is real and pinned
    now, just not yet wired in) — but the critical-path work is
    exhausted for this pass.

d7. **CoreFoundation + real ICU + real libxml2 ports DONE 2026-08-26/27
    — CF dynamic trial link down to 42 undefined-symbol groups, from a
    starting count of 153.** Real, unmodified Apple source throughout
    (`third_party/swift-corelibs-foundation` — Apple's own actively-
    maintained CF fork, NOT the 2014-dated `apple-oss-distributions/CF`;
    real ICU 66.1 via a real two-stage host+arm64 cross build; real
    libxml2-34.9). 94 real CF source files build clean into
    `libCoreFoundation.a`. The single biggest gap-closing fix of the whole
    effort was `libSystem.B.dylib`'s missing `-Wl,-reexport_library` (27
    groups in one rebuild). Full port/build narrative archived to
    `docs/historic/backlog.md` — the session-by-session story in its
    2026-08-27 "item d7" section, the current-state detail in its "Third
    archive pass, 2026-08-30" section.

    **Real, confirmed compile-time exclusions (4, structural, not build
    gaps)**: `CFUserNotification.c` (real GUI alert API, no GUI on this
    target); `CFPreferences.c`/`CFXMLPreferencesDomain.c` (**CLOSED
    2026-08-28** — both now compile and link for real, three
    `TARGET_OS_OSX`/`TARGET_OS_MAC` guard mismatches, see
    `docs/daemon-roadmap.md`'s "Milestone 2 CLOSED, 2026-08-28"; whether
    `CFPreferencesCopyAppValue` et al are *behaviorally* correct against a
    real on-disk preferences file is separately unverified);
    `CFURLSessionInterface.c` (real libcurl → closed-source Secure
    Transport for TLS, plus BSD socket wrappers this userland lacks — real
    prerequisite order: socket syscall wrappers, then a TLS backend
    decision, then a `build_libcurl.sh` off `build_libxml2.sh`'s template);
    `CFWindowsUtilities.c` (Windows-only).

    **Remaining real gaps in the dynamic trial link, 42 groups — still
    open** (full per-symbol list archived): `_scalbn`/`_getpwuid_r`/
    `_getsectbynamefromheader_64`/`_getsegbyname` (real functions, not
    confirmed built anywhere in this project's stack, not investigated);
    the `___chk_*` `_FORTIFY_SOURCE` hardening family (no shim source
    located); an os_log/dispatch bucket (`__os_log_*`/`_os_log_create`/
    `_os_log_type_enabled`/`__dispatch_queue_attr_concurrent`/
    `__mh_dylib_header`, confirmed absent from both `libdispatch.dylib`
    and `libSystem.B.dylib`); `_asl_*` (6 symbols — real and pinned as
    `syslog-385`, just not yet wired into CF's own trial link); the
    `CFPreferences`-family symbols tracked by the exclusion above; and
    `sysdir_*` (confirmed never open-sourced by Apple anywhere — full
    `Libc` git history plus an org-wide GitHub code search).

d8. **Static-archive-vs-dylib convention debt — RESOLVED 2026-08-28
    (explicit user directive: option (a), convention NOT narrowed) and
    2026-08-29 (all 11 "genuine conversion candidate" archives converted
    to real dylibs, every real consumer relinked, not just left as unused
    parallel output).** Filed 2026-08-26 after five libraries written
    *after* the per-library-dylib convention landed all shipped as `.a`
    anyway. Real per-library dylibs now: `libresolv`, `libsystem_notify`
    (replaces libnotify.a + libnotify_client.a), `libbsm`, `libncurses`,
    `libcopyfile`, `libremovefile`, `libiconv`, `libsystem_m`,
    `libopenpam` (base library only), `libxpc`, `libiokituser`.

    **Standing rule this produced, still in force**: do not add a new
    static archive for anything that is a real, standalone, nameable
    library — build a real `.dylib` even if today's only consumer is
    static. Per-consumer curated-extraction bundles (`cf_libc_extras.a`,
    `launchd_libc_extras.a`, `syslogd_libc_extras.a`), per-daemon object
    archives, and `libmach_ipc.a` stay `.a` by design; `tools/init_binary`/
    `pwtool` (run before/without dyld) and `build_dyld_link.sh` (dyld
    can't depend on the dylibs it loads) are the real exceptions.

    Full inventory, the real duplicate-`_res` bug the conversion closed at
    its architectural root, and the two new gaps it surfaced (`mach_msg()`
    now living for real in `libsystem_kernel.dylib`; a real
    `libsystem_pthread_dylib_bootstrap_compat.c` constructor) are archived
    verbatim to `docs/historic/backlog.md`'s "Third archive pass,
    2026-08-30".

    **Still open, carried forward into item d12** — do not re-derive here:
    `libCoreFoundation.a` → `.dylib` (deliberately deferred, own future
    session — real load-time global state/constructors);
    `build_libsystem_c.sh`'s narrow curated `ar x` extraction from
    `libm.a`/`libremovefile.a`/`libopenpam.a` for ICU's sake (a real
    circular-build-order constraint, not just style — real follow-up is to
    reexport `libsystem_m.dylib`/`libopenpam.dylib` through
    `libSystem.B.dylib`, then drop the extraction);
    `libdispatch_min.a`/`libdispatch_real.a` likely dead since
    `libdispatch.dylib` became real, never actually checked for remaining
    consumers.

d9. **`build_simple_cmds.sh` converted from fully-static to real dynamic
    linking — RESOLVED 2026-08-28: 76 of 76 commands build, and the
    ld64-609 crash that blocked 32 of them is fixed for real.** This is
    the first real dyld-loaded consumer item d8 was waiting for, and
    confirms d8's own premise: a real dynamic consumer needs zero
    `-Wl,-alias` flags of its own (the ~60-entry `LINK_ALIASES` saga is
    gone), because `libSystem.B.dylib` already bakes the same real
    UNIX2003/INODE64/DARWIN_EXTSN aliases into its own build. Root cause
    of the 32 failures was a real, narrow `ld64-609` bug (`Assertion
    failed: (_mode == modeFinalAddress)` under `-Wl,-fixup_chains`), not
    this project's object code — fixed by passing `-B"$NEWLD_BINDIR"` (the
    daily-driver Xcode's much newer `ld`) on the final per-command link
    only, still compiling with Xcode 12's own `clang`/SDK throughout.

    **Two load-bearing findings from this item are standing rules, kept
    here rather than archived:**
    - **`-Wl,-alias` inside a dylib only lands in that dylib's real
      `LC_DYLD_EXPORTS_TRIE` when the alias's BASE symbol is genuinely
      local to that same link** — confirm with `xcrun dyld_info -exports`,
      **not** `nm` (nm lists the alias regardless, which is actively
      misleading). Add each alias at the specific sub-dylib where its base
      is local, never at the umbrella `libSystem.B.dylib` level for a
      reexported base.
    - **`tools/init_binary/inject_into_sd_image.sh` must be re-run after
      every `build_libsystem_c.sh`/`build_libsystem.sh` rebuild**, not
      just after `build_simple_cmds.sh`, or booted binaries resolve dylib
      imports against stale on-image exports (a real QEMU crash,
      `Symbol not found: _fdopen$UNIX2003`, was caught exactly this way).
      Related: any sub-dylib whose real, non-aliased symbols a consumer's
      own compiled code calls directly must ALSO be linked directly on
      that consumer's own command line — ld64 does not resolve a
      multi-level reexport chain through `libSystem.B.dylib` alone for
      this toolchain.

    Full bisection trail, the four real ports that closed the remaining 21
    commands (`table.c`/`none.c`; `ldpart.c`/`getcwd.c`/`findfp.c`;
    `collate.c`'s real `__LIBC__` compile bug plus `collcmp.c`; `date`'s
    own alias case), and both real regressions the follow-up QEMU boot
    surfaced (`cat`'s dead aliases over a linker behavior divergence;
    `ed`/`diff`/`less`'s missing `_pipe` bridge — a dormant regression
    from earlier in this same conversion, invisible until something
    actually launched under real eager chained-fixups binding, fixed via
    `tar_pipe_compat.c`) are archived verbatim to
    `docs/historic/backlog.md`'s "Third archive pass, 2026-08-30".

    **Still unverified**: `ed`'s own file-write behavior under a real
    interactive session (as opposed to this pass's scripted/piped test).
    This item is scoped to "does it link and boot," not full feature
    parity — e.g. `date`'s real clock-setting path is still stubbed (item
    13 below).

0a. **[DONE 2026-08-28] Real AES core wired: FreeBSD rijndael, not
    PureDarwin.** Filed 2026-08-21 as a hard prerequisite for Phase 7b
    (`sshd`); closed by vendoring public-domain `sys/crypto/rijndael/`
    (Rijmen/Bosselaers/Barreto — the AES designers themselves) into
    `libkern/crypto/freebsd/rijndael-alg-fst.c`, plus a corecrypto-shaped
    adapter (`libkern/crypto/corecrypto_aes_register.c`) implementing real
    `ccmode_ecb`/`ccmode_cbc`/`ccmode_ctr` against it and calling
    `register_crypto_functions()` with only the AES fields filled.
    FIPS-197/SP 800-38A known-answer vectors run at boot *through the real
    ccmode interfaces* (not the raw cipher core) and `panic()` on failure.
    `tcp_tfo_init`'s TCP Fast Open path now works for real instead of
    degrading. PureDarwin/corecrypto (scoped 2026-08-27) was investigated
    and deliberately NOT used — mixed-license AES core, AES-128-only, no
    CTR.

    Two real bugs found and fixed while wiring this up, both from real
    QEMU panics, neither guessed: the registration call had to move into
    `kernel_bootstrap_thread()` immediately before `machine_lockdown()`
    (writing `g_crypto_funcs`, a `SECURITY_READ_ONLY_LATE` global, from
    `bsd_init()` faults for real); and `corecrypto_rand.c`'s
    `cc_rand_generate()` needed a field-specific null check, since it
    guarded only on `if (g_crypto_funcs)` and then unconditionally called
    a deliberately-NULL `ccrng_fn` — root-caused via a real A/B test.
    Full writeup (the five-times-bitten `g_crypto_funcs` caller table,
    context sizing against `AES_CBC_CTX_MAX_SIZE`, the PureDarwin
    license/coverage analysis) archived to `docs/historic/backlog.md`'s
    "Third archive pass, 2026-08-30".

    **Still open, filed separately as P1 item 3 above**: six other
    `g_crypto_funcs->` call sites have no null check at all.

0b. **[DONE 2026-08-27] `ccsha256_di` wired to real corecrypto**,
    replacing the from-scratch `corecrypto_sha2.c` for code-signing/exec
    hashing — the real digest core was already sitting unused at
    `third_party/xnu-7195/osfmk/corecrypto/` and already compiled into the
    kernel for the PRNG's own use; only the SHA-256 call sites hadn't been
    pointed at it. QEMU-verified (`xnu-7195` commit `ceb6d419c`).
    **[DONE 2026-08-28]** SHA-384/512 and MD5 are off the from-scratch
    path too: no real corecrypto body exists for either in this checkout
    (confirmed absent, only headers), so per the Apple→FreeBSD→custom
    source priority both now use FreeBSD's real source (`sha512c.c`'s
    compress core behind this project's own buffering/padding wrapper;
    `md5c.c` wholesale, byte-identical `MD5_CTX` layout). Both
    QEMU-verified in the same boot that verified item 0a's AES work. Full
    writeups archived to `docs/historic/backlog.md` (its 2026-08-27 "item
    0b" section and the "Third archive pass, 2026-08-30" section).

0. **DWC2 transfer completion is polled, not interrupt-driven** (added
    2026-08-21 with `iokit/BCM2837DWC2/`). `docs/historic/usb-support-plan.md` task 2
    called for mirroring `BCM2837SDHCI`'s interrupt-driven design; this
    first cut leaves global interrupts masked (`GAHBCFG.GLBLINTRMSK` clear)
    and polls `HCINT.CHHLTD`, exactly as its u-boot reference does. That was
    a deliberate sequencing call — doing first-ever register bring-up and
    interrupt plumbing in one change means debugging two unproven things at
    once, and `BCM2837SDHCI`'s interrupt work needed three passes and a
    device-tree root cause before a real interrupt was ever observed. The
    conversion is contained: `waitForBits()`/`waitForChannelHalted()` are the
    only places that spin, and `BCM2837SDHCI`'s hybrid
    `IOSimpleLock` + `IOLockSleepDeadline` pattern (real interrupt wins
    immediately, poll tick as a safety net that also proves which path
    fired) is the model to copy. Worth doing before any sustained Ethernet
    RX load, since polling a bulk-IN endpoint burns a core.

1. **Phase 3.5a still not done**: `tools/init_binary/init.c`'s `ls`/`cat`/
    `cd`/`pwd`/`mkdir` operate on a hardcoded in-memory fake tree, not real
    `open`/`read`/`getdirentries` syscalls against the actually-mounted
    root. Small, self-contained, no dependency on Phase 4.

2. **Phase 4's `Libc` build is unstarted** (`Csu`/crt0 and real syscall
    stubs are verified working — see `tools/userland_staging/` — but `Libc`
    itself, the actual C library, is not). Confirmed to be an Xcode-project
    build (`Libc.xcodeproj`, no plain Makefile) against a broken local
    `xcodebuild`; the real path is compiling a curated `.c` subset directly
    with `clang`, starting with string/mem functions (no syscall/TSD
    dependency) before `stdio`/`malloc`/locale. Expect the same
    header-skew-per-subsystem fight `IOStorageFamily`/`hfs` needed.

3. **`build_kexts.sh` only standalone-compiles some driver sources**
    (PARTIALLY ADDRESSED 2026-08-21: it now compiles every `.cpp` in each
    listed directory rather than only `<dir>/<dir>.cpp`, and `BCM2837DWC2`
    was added, so the four USB sources are covered. The `KEXTS` array is
    still a hand-maintained list, so the gap below stands for the rest.)
    Originally: only 3 of 8 driver sources
    (`BCM2837InterruptController`, `BCM2837SDHCI`, `SMSC9514Ethernet`) —
    `BCM2837CPU`, `BCM2837LocalIntc`, `BCM2837ArmIO`, `BCM2837Personalities`,
    `BCM2837PlatformExpert` are never syntax-checked outside a full kernel
    build, though all five are listed in `third_party/xnu-7195/iokit/conf/files`.
    Cheap to extend the `KEXTS` array (note `BCM2837Personalities.cpp`
    actually lives in `iokit/BCM2837ArmIO/`, not a same-named directory).

4. **`ls /` crash mentioned in old project memory needs re-verification.**
    An earlier session flagged `ls /` as crashing (real SIGSEGV-looking
    panic) after printing one entry. Recent boots in this session ran `ls`
    without incident, but that was likely still against the fake
    in-memory tree (item 1 above) rather than a real directory read — status
    unclear, re-check once 3.5a lands real syscalls.

5. **No `grep`/`sed`/`sort`/`nl`/`xargs`/`ed`-style command has been built
    yet.** Not blocked on a missing subsystem — the real POSIX regex engine
    (`libc_build/libc_regex.a`, Apple's own TRE port, `[RESOLVED
    2026-08-20]`, full writeup archived to `docs/historic/backlog.md`) has
    been linkable since 2026-08-20 and is already proven in real use
    (`bash`'s `[[ str =~ pattern ]]`, QEMU-verified). Each of these is now
    a normal new-command port against a real, linkable engine.

6. **`touch -h` (utimes-on-symlinks) still hits an honest `ENOSYS` stub**
    despite the real syscall it needs (`setattrlist(2)`, `[RESOLVED
    2026-08-20]`, full writeup archived to `docs/historic/backlog.md`)
    being wired and QEMU-verified since 2026-08-20 for `cp -p`/`chmod`'s
    ACL machinery. Small, low-priority follow-up — hasn't been revisited.

7. **No real Mach IPC (`mach_msg()` trap + MIG RPC) in userland**, real
    open-source kernel-side implementation already present in this
    project's `third_party/xnu-7195` (`osfmk/kern/ipc_*`/`clock.c`) but
    never exercised from a userland binary. Blocks real `ps` CPU%/RSS/VSZ
    columns (`ps_task_info_stub.c`) and real blocking sleep anywhere
    (`simple_cmds_nanosleep_compat.c` currently spin-waits, burning a
    full core instead of blocking — used by `tail -f`'s poll interval).
    NOT closed-source like Phase 7.5's pthread/corecrypto gaps — "not
    yet built," not "cannot be built." See `docs/roadmap.md`'s Phase 4.6
    for a smaller-scoped first real fix (hand-written trap wrappers for
    just `host_get_clock_service`/`clock_get_time`/`semaphore_*`, without
    needing full MIG-stub generation).

8. **`SMSC9514Ethernet`'s MAC address is a locally-administered
    placeholder (`02:00:00:00:00:01`) on any board where the LAN9514 has
    no EEPROM populated** — filed 2026-08-25 alongside that day's RX-poll
    fix. `start()` now genuinely tries a real EEPROM read first
    (`readEEPROM()`/`eepromWait()`, `E2P_CMD`/`E2P_DATA`) and only falls
    back to the placeholder if that read times out or looks
    uninitialized, but most Raspberry Pi boards ship this chip with no
    EEPROM at all, so the placeholder is likely still what real hardware
    reports — every such board presents the identical address, which
    will misbehave the moment two of them are on the same network
    segment (DHCP/ARP collisions). The Raspberry Pi Foundation's own
    convention doesn't use chip EEPROM either: it derives a real
    per-board MAC from the VideoCore's unique board serial (mailbox
    property tag "get board serial", low 3 bytes combined with the
    Foundation's OUI, e.g. `b8:27:eb:xx:xx:xx`) at firmware/u-boot time.
    This kernel has no VC-mailbox driver yet reachable from
    `SMSC9514Ethernet::start()` — would need to either read a value the
    bootloader already stashed somewhere (devicetree property?) or add a
    real mailbox "get board serial" call. Not started; scoped here for a
    follow-up session rather than attempted as part of the RX-poll fix.

9. **`hexdump` prints only the final byte-count address line, never the
    actual hex+ASCII data rows**, against a real non-empty file, for every
    format flag tried (plain/`-c`/`-C`) -- found and narrowed (not fully
    root-caused) during the 2026-08-20 command-sweep session's Block 3.
    Specific to `hexdump`'s own `freopen(argv, "r", stdin)` +
    `getc()`/`ungetc()` read idiom, not a general `freopen` bug (Block 2's
    `fold`/`expand`/`unexpand`, linking the identical real `freopen.c`,
    correctly read real file-argument content the same session). Most
    likely a latent bug in this project's ported `getc()`/`ungetc()`
    interaction with a freshly-`freopen`'d stream. Ships anyway (real,
    unmodified source, links, runs, doesn't crash) -- same tolerance as
    P0 item 7's `yes` kernel-panic bug. See `docs/historic/roadmap.md`'s
    "Twenty-fifth pass" Block 3 entry for the full evidence trail.

10. **`pathchk ""` (empty pathname) exits 0 instead of POSIX's specified
    "shall report an error"** -- real, unmodified upstream FreeBSD source,
    found during the same session's Block 3. Low practical impact, not
    investigated further.

11. **`rs -T` (transpose) dropped the first row of a small piped 2x3
    matrix in one observed test** (`printf '1 2 3\n4 5 6\n' | rs -T`
    printed only `4\n5\n6\n`) -- found during the same session's Block 2.
    Not confirmed as a port bug vs. a genuine regression: real upstream
    `rs`'s array-dimension auto-guessing heuristics, active whenever
    neither `-g` nor `-c` is given, are documented as sensitive to
    small/irregular input shapes. Not investigated further given `rs`'s
    low practical value.

12. **`ls -e`'s real ACL display (`acl_get_link_np()`) is an honest
    always-NULL stub** (`tools/userland_staging/ls_acl_compat.c`), not a
    real implementation -- found/scoped during the 2026-08-20 Tier-A
    command sweep (`ls`/`find`/`date`/`gzip`/`compress`). Real Apple
    `acl_get_link_np()` (`Libc/posix1e/acl_file.c`) goes through a real
    `filesec_t` abstraction (`Libc/gen/filesec.c`, implementation
    vendored) whose *declaring header* (`<sys/filesec.h>`) Apple removed
    from the public SDK entirely -- confirmed absent from both this
    project's vendored Libc checkout and Xcode 12's iPhoneOS/MacOSX SDKs
    directly (grepped both real `sys/acl.h` headers: zero `filesec_t`/
    `FILESEC_*` mentions in either), not merely unfound. The real kernel
    side is NOT missing -- `stat64_extended`/`lstat64_extended`/
    `fstat64_extended` (bsd/kern/syscalls.master #341-343, real,
    `NO_SYSCALL_STUB`, real `vn_stat(..., &fsec, ...)` kernel
    implementation already in `bsd/vfs/vfs_syscalls.c`) are proven live
    and callable via this same tier's raw-syscall pattern (see
    `cp_mv_syscalls.c`'s `lchown`/`sigaction`/`setattrlist` precedent) --
    only the userland-side struct layout is the gap. `find -acl`'s real
    `__APPLE__` path also depends on this same stub (same honest
    always-"no ACL" answer). Real follow-up: reconstruct or re-source a
    real `filesec_t` layout (possibly from an older/different Apple SDK
    vintage, or by reverse-engineering `Libc/gen/filesec.c`'s own real
    property-ID usage) and wire the three real syscalls above through
    it, then re-point `acl_get_link_np()`/`acl_get_file()` at the real
    path. Low urgency -- this target's HFS+/msdosfs mounts have no
    kernel-side ACL *storage* wired either, so a real userland
    implementation would correctly report "no ACL" for every file today
    regardless.

13. **`date`'s network time-sync (`netsettime()`, `-n`-less clock-set)
    and login-record write (`pututxline()`, also clock-set-only) are
    honest always-fail stubs** (`tools/userland_staging/date_compat.c`),
    found/scoped during the same 2026-08-20 Tier-A sweep. Neither
    affects `date`'s ordinary display/format path (QEMU-verified
    working) at all. `netsettime()`'s real implementation
    (`shell_cmds/date/netdate.c`) needs a real resolver+socket stack
    (Phase 6, unstarted) for the legacy `timed` UDP protocol, dead on
    every real modern system regardless. `pututxline()`'s real
    implementation (`Libc/gen/utmpx-darwin.c`, 1193 lines) needs a real
    Mach-RPC-to-ASL/utmpx-daemon path this project has no launchd/
    bootstrap-service registry to back (see CLAUDE.md's "no launchd"
    line) -- a whole additional unbuilt subsystem, not a narrow gap.
    `settimeofday(2)` itself IS real (wired as a raw `NO_SYSCALL_STUB`
    syscall in the same file), so `-d`/`-t` and an actual privileged
    clock-set still work for real, just without Apple's own wrapper's
    cosmetic `notify(3)`/ASL side effects. Not planned to be revisited
    before Phase 6/Phase 5.5's `syslogd` land for real.

14. **`tools/init_binary/inject_into_sd_image.sh`'s `for cmd in ...` list
    needs each new command name appended by hand** (documented in this
    repo's own working conventions but easy to forget) -- caught during
    this session's `find`/`date`/`gzip`/`compress` QEMU verification when
    each was initially silently absent from the injected image despite
    building cleanly (no error, the command just never got copied to
    `/bin`, "command not found" was the only symptom). Not a design
    flaw worth fixing structurally this pass (the list is deliberately
    explicit, matching the project's "no fabricated automation" bias),
    just a real, repeated friction point worth flagging for whoever adds
    the next command: verify the injected image actually contains the
    new binary (e.g. `strings`-check the mount, or just `ls /bin` at a
    real login prompt) before trusting a QEMU test's "command not found"
    absence to mean anything about the command itself.

15. **This project's kext personalities never wire up
    `kIOFDiskPartitionSchemeContentTable`** (`third_party/IOStorageFamily/
    IOFDiskPartitionScheme.cpp`'s `instantiateMediaObject()` looks this
    dictionary up to translate a raw MBR partition-type byte, e.g. real
    `0xAF`, into a content-hint string like `"Apple_HFS"` -- with no
    table wired, every partition's `Content` property is just the raw
    `"0xNN"` hex string instead). Only actually matters when XNU boots
    with no `rd=` boot-arg (real hardware's default, confirmed via a
    real device boot 2026-08-21 -- `iokit/bsddev/IOKitBSDInit.cpp` falls
    through to generic `IOProviderClass=IOMedia`/`Content=Apple_HFS`
    root matching in that case, which can never succeed without this
    table); every QEMU boot always passes `rd=disk0s2` (`runvm.sh`) and
    never hits this path, which is why it went unnoticed all session.
    Worked around for now by baking `setenv bootargs rd=disk0s2` into
    real hardware's `u-boot` autoboot (`third_party/u-boot`'s
    `configs/rpi3_xnu_defconfig`, see `sdcard/README.md`) rather than
    fixing the underlying table -- the real, more general fix (wiring an
    actual content-hint table into this project's hand-built
    `BCM2837Personalities.cpp`, matching how a real Apple platform
    expert would populate it) is still open for whoever wants boot to
    work without an explicit `rd=` in the future.

16. **`less`/`more`: no real termcap/terminfo database exists on this
    target**, so `less` always runs in real hardcopy mode, never true
    cursor-addressed full-screen paging — a real, honest scope limit, not
    a bug. The launch → degraded-mode-detection → quit lifecycle itself
    is real and QEMU-verified end to end (`[RESOLVED 2026-08-21]`, full
    verification writeup archived to `docs/historic/backlog.md`), and a
    genuine page-by-page scroll-through of multi-screen content was not
    separately exercised (only single RETURN + quit). Porting a real
    terminal-capability database is a distinct, larger, not-yet-scoped
    follow-up if full-screen paging is ever needed.

17. **Once `syslogd`/ASL land, switch real callers off the stderr-only
    `syslog(3)` fallback onto the real thing — but only via the legacy
    Unix-socket path first, not `libsystem_asl`'s real `asl_open()`
    client.** Filed 2026-08-27 alongside the `syslog-385` pin
    (`docs/daemon-roadmap.md`'s Phase 6.7) so this sequencing isn't lost:
    `tools/userland_staging/ifconfig_syslog_compat.c`'s `syslog()`/
    `openlog()`/`closelog()` stand-ins (also used by `cron`, see
    `build_cron.sh`'s own header comment) are correct and should stay
    exactly as-is until a real legacy-only `syslogd` (`bsd_in.c`-based,
    Unix-domain-socket `/var/run/syslog`, no Mach dependency) is actually
    running. Do **not** wire callers onto `libsystem_asl`'s real
    `asl_open()`/`syslog()` (`third_party/syslog/libsystem_asl.tproj/`)
    until the bootstrap-namespace registrar shim scoped for `notifyd`
    also exists — that client unconditionally calls
    `bootstrap_look_up2(bootstrap_port, ASL_SERVICE_NAME, ...)`
    (`asl.c:271`, confirmed no non-Mach fallback path exists anywhere in
    the client library), so wiring it in early would produce code that
    compiles clean but silently fails to log anything, exactly the kind
    of regression this project's own conventions exist to avoid.

    **UPDATE 2026-08-27, same day**: `syslogd.macho` itself now builds
    and links completely clean (`tools/userland_staging/build_syslogd.sh`,
    real dynamic Mach-O) and PID-1 can fork+`execve()` it for real — but
    it is **not yet auto-spawned at boot**. The originally-found blocker
    (dyld crashing on `__dispatch_wait_for_enqueuer`, a real symbol
    `libdispatch.dylib` itself referenced but never exported) is fixed —
    `shims/yield.c` was compiled-but-never-included, a genuine missing-
    file bug, not an architecture gap; two more of the exact same class
    (`os/semaphore.c`, a narrow reproduction of `workgroup.c`'s TSD
    cleanup) were found and fixed the same way via iterative real QEMU
    boot-testing.

    **UPDATE 2026-08-27, later same day**: the pthread QoS-class/attr
    blocker (`__pthread_qos_class_encode` and ~30 real siblings) is now
    substantially closed too — `qos.c` turned out to compile clean
    against this project's real toolchain (the "unreachable per the
    kernel constraint" note in `build_libpthread.sh` predates Phase
    7.5's real kernel thread-creation port and is now stale for the pure
    encode/decode pieces); new, isolated `build_libpthread_qos.sh` +
    `libpthread_qos_compat.c`/`libpthread_attr_compat.c` close real
    `pthread_attr_*`, `pthread_sigmask`, `pthread_exit`,
    `pthread_workloop_create`/`destroy`, `clock_gettime_nsec_np`, and the
    `mach_voucher`/`host_priv` MIG-regenerated routines this needed along
    the way. Kept deliberately separate from the master `libpthread.a` to
    avoid regression risk to bash/ncurses/getty/syslogd. `pthread_get_
    stackaddr_np`/`pthread_setschedparam` stay honestly deferred (would
    need this project's from-scratch thread-bootstrap to match `pthread.c`'s
    real thread-list ABI, not yet verified safe to copy). Full writeup:
    `docs/daemon-roadmap.md`'s Phase 6.7 section.

    **UPDATE 2026-08-27, later still**: Step 2 (the sibling-dylib relink
    from this doc's own userland-library-debt section, `d1`) was applied
    to `libdispatch.dylib` specifically, ahead of the general cleanup:
    it now links for real against this project's own already-built
    `libsystem_c.dylib`/`libsystem_malloc.dylib`/`libsystem_pthread.dylib`
    (real `LC_LOAD_DYLIB` entries, confirmed via `otool -L`), closing 20
    of the ~54 remaining undefined symbols that a real `nm -gU` sweep
    found were already real, defined exports there. Two more closed by
    narrow, non-import fixes the same day: `gettimeofday` (real raw
    syscall #116 body) and `geteuid` (real single-object extraction from
    `syslogd_libc_extras.a`).

    **UPDATE 2026-08-27, later same day: all `libdispatch.dylib` symbol
    gaps closed — `syslogd.macho` reaches real execution.** Two more
    passes closed the remaining 32-symbol gap in full:
    - `3fab93c` closed 20 of the 32: real `kevent_qos`, `kpersona_get`,
      `pread`/`pwrite`, `sigsuspend`, `sleep`, `semaphore_timedwait`,
      `malloc_engaged_nano`/`memory_event_handler`,
      `mach_continuous_time`/`mach_get_times`,
      `mach_make_memory_entry_64`, all 7 `mach_port_*` traps/MIG-fallback
      wrappers, and `thread_destruct_special_reply_port`. Found and fixed
      a real bug along the way: `mach_ipc_dead_stubs.o`'s narrowly-scoped
      `KERN_FAILURE` `mach_port_insert_right`/`extract_right`/`unguard`
      stand-ins (meant only for one unreachable reply-validation path)
      would otherwise have been the only definition satisfying
      `syslogd`'s real dispatch/eventlink calls to those same names —
      replaced with real trap-first/MIG-fallback bodies. Added
      `tools/userland_staging/gap_audit.sh` (triages a built dylib/macho's
      undefined-symbol table against already-built archives and real
      Apple source without a QEMU boot per symbol; now a standing
      pre-boot step per `CLAUDE.md`).
    - `7629e79` closed the remaining 8: the `mach_eventlink_*` family (real
      `libsyscall/mach/mach_eventlink.c` BSD-syscall bodies for
      `signal`/`wait_until`/`signal_wait_until`, plus a fresh `mig` regen
      against `osfmk/mach/mach_eventlink.defs` for
      `create`/`destroy`/`associate`/`disassociate`) and
      `pthread_get_stackaddr_np` (a narrow extraction of `pthread.c`'s own
      function, skipping the internal cross-thread liveness check since
      child threads aren't inserted into `__pthread_head` on this
      project's from-scratch thread bootstrap — safe here specifically
      because `pthread_s` structs are never freed/reused).

    Verified via real QEMU boot: `syslogd.macho` now passes dyld's eager
    symbol binding completely (zero "symbol not found in flat namespace"
    aborts) and reaches real execution — where it then hit a **real
    runtime SIGSEGV (11)**, a different, later-stage failure than the old
    load-time SIGABRT (6).

    **UPDATE 2026-08-27, later still: that SIGSEGV fixed, `syslogd.macho`
    now runs to completion (`e819550`).** Two independent real bugs, both
    root-caused via the real crash (the first time anything in this
    project called plain `snprintf()`/`vsnprintf()` at runtime,
    `syslogd`'s `write_boot_log()`) and bisected with isolated static
    smoketests:
    1. This SDK defaults `_FORTIFY_SOURCE` to 2 for this iOS target.
       `third_party/Libc/secure/vsnprintf_chk.c`'s own internal
       `vsnprintf(...)` call was getting macro-rewritten back into
       `__vsnprintf_chk(...)` — infinite self-recursion, stack-guard-page
       SIGSEGV. Fixed with `-D_FORTIFY_SOURCE=0` on the two build scripts
       that compile `Libc/secure/*.c`
       (`build_libxpc_syscalls2.sh`/`build_libdispatch_dylib.sh`).
    2. Real `vsnprintf()` calls `__current_locale()`, which does a raw,
       unchecked TSD-array lookup using `__locale_key` — left at its
       static `-1` sentinel because nothing in this project ever called
       the real `__xlocale_init()` real Apple calls from
       `_libc_initializer()` (never ported here). Fixed with a real patch
       (`third_party-patches/Libc.patch`) adding a same-translation-unit
       constructor to `xlocale.c` itself, so every build script pulling in
       that object file links it for free;
       `libc_locale_globals_compat.c` (this project's own narrow private
       copy of the same two globals, used by `libdispatch.dylib`'s
       separate static link) got the identical fix.

    Verified end-to-end: `syslogd.macho` no longer crashes at all — it
    runs its full real startup (dispatch queue creation, boot-log writing
    with real `snprintf`, notify registration) and exits cleanly with the
    expected, already-documented "no real launchd" limitation (its
    `asl_open()`/`notify_register_*` calls need `bootstrap_look_up2()`
    against a real bootstrap namespace registrar this project doesn't
    have yet, see the notifyd scoping note above), not a memory-safety
    bug.

    **What's still actually open, confirmed by re-reading
    `tools/init_binary/init.c` directly rather than trusting the above as
    "done"**: `spawn_syslogd()` is defined (`init.c:1665`) but its call
    site is still commented out — grepping the file for
    `spawn_syslogd()` finds exactly one hit, inside that disabling
    comment block, no active call anywhere in `init_main()`'s loop.
    `syslogd` is **not** auto-spawned at boot despite every gap above
    being closed; nothing currently prevents re-enabling it (the
    documented "no real launchd" exit is a clean, non-fatal outcome, not
    a crash), it just hasn't been done yet. Note also: `init.c`'s own
    disabling comment block still enumerates the old "32 remaining
    symbols" list from before `3fab93c`/`7629e79` — that comment is now
    stale on gap count (not rewritten as part of this docs pass, since
    editing `init.c` itself is out of scope here); whoever re-enables the
    call site should refresh that comment too. Once re-enabled and QEMU-
    boot-confirmed, this item (switching real callers onto the legacy
    Unix-socket path) becomes actionable.

d10. **Real, project-wide finding, 2026-08-29 — `-Wl,-U` does not safely
    defer an unreached symbol under this project's `-Wl,-fixup_chains`
    (chained fixups) convention, and this has NOT been re-verified for
    every consumer yet.** Found resuming launchd's own real QEMU boot
    after item d8's static-to-dylib pass: with chained fixups, real dyld
    resolves every bound `__got` entry at real image-load time, before
    any code runs — not lazily on first call. A flat-namespace `-Wl,-U`
    symbol that's genuinely unresolvable ANYWHERE in the loaded process
    aborts the whole process load (`symbol not found in flat namespace
    '_X'`, exit reason namespace 6 subcode 0x9) the instant that dylib is
    loaded, regardless of whether the referencing code path ever
    executes. This never mattered for this project's static
    (`-nostdlib -static`, no dyld) consumers, and never mattered for a
    dylib nobody had actually booted into a real process yet — **launchd
    is the first real, fully-dynamic process complex enough to load
    `libresolv`/`libxpc`/`libbsm`/`libsystem_notify`/`libdispatch` all at
    once and actually hit this.** `gap_audit.sh`'s static analysis does
    **not** catch this class of bug (it only checks whether a symbol is
    defined *somewhere* in the process's linked images, not whether real
    chained-fixups binding order would actually succeed) — only a real
    boot finds it, one `symbol not found` at a time.

    **~20 real symbols across `libxpc`/`libresolv`/`libbsm`/
    `libsystem_notify` were found+closed this way for launchd
    specifically** (real syscalls where a wrapper was genuinely missing,
    real honest "service not available" stand-ins where a whole
    subsystem doesn't exist here — mDNSResponder, protocol/service
    databases; real sibling-dylib links where a symbol already existed
    elsewhere) — see `tools/userland_staging/libxpc_csops_syscall.c`,
    `mach_error_string_minimal_compat.c`, `libresolv_dnssd_compat.c`,
    `libresolv_dns_internal_compat.c`, `libresolv_more_socket_syscalls.c`,
    `libresolv_netdb_compat.c`, `libsystem_notify_misc_compat.c`, and
    each one's own build script (`build_libxpc_dylib.sh`,
    `build_libresolv_dylib.sh`, `build_bsm_dylib.sh`,
    `build_libsystem_notify.sh`) for the full real, individually-cited
    trail — not re-summarized here.

    **What's NOT yet done, real remaining work, not to be lost**: those
    fixes closed exactly the symbols launchd's own particular dylib load
    set needed. A real re-derivation (`xcrun dyld_info -fixups
    <dylib>.dylib | grep '<flat-namespace>'`) on 2026-08-29 shows these
    dylibs still carry real, unverified flat-namespace deferrals that
    **only currently "work" because launchd.macho happens to provide the
    matching symbol locally** (via its own static portion or another
    linked dylib) — a **different** real consumer of the same dylib
    (`syslogd.macho`, `launchctl.macho`, `build_corefoundation.sh`'s
    trial link, any of `build_simple_cmds.sh`'s 76 commands) has **not**
    been re-verified and may hit the identical class of boot failure the
    moment it's actually run, not just linked:
    - `libresolv.dylib`: `___darwin_check_fd_set_overflow` (real,
      genuinely safe — see its own citation, real Apple header already
      marks it `weak_import`), `_kevent`/`_kqueue`/`_pipe` (real, resolve
      via `libxpc.dylib`/`libdispatch.dylib` being loaded alongside —
      true for any consumer that also loads one of those two, not
      guaranteed for one that doesn't).
    - `libbsm.dylib`: `_getegid`/`_getgid`/`_getgrgid`/`_getpwuid`/
      `_syslog` — all resolve today only via launchd's own local BSM/
      passwd-compat wrappers (`liblaunchd_core.a`).
    - `libsystem_notify.dylib`: `__xpc_bool_true`/`_bootstrap_look_up2`/
      `_xpc_dictionary_create`/`_xpc_dictionary_set_string`/
      `_xpc_dictionary_set_uint64`/`_xpc_release`/`_pipe` — all resolve
      today only via `libxpc.dylib` being loaded in the same process.
    - `libdispatch.dylib`: the full 11-symbol `_kernelrpc_mach_port_*`
      family, `__mach_errors`, `_nanosleep`, `_thread_get_special_reply_port`
      — all resolve today only via launchd's own local
      `liblaunchd_core.a`/`mach_port_gap_closures2.o` (a narrower, only-
      3-of-11-symbol local copy that happens to be enough because
      `libxpc_syscalls2.a`'s `mach_portUser.o` is *also* force-loaded
      into `liblaunchd_core.a` for other reasons, covering the rest).
      `build_libdispatch_dylib.sh` itself already documents this exact
      reliance in its own `UNDEF_SYMS_ELSEWHERE_IN_PROCESS` comment
      (written 2026-08-27, before this was ever actually exercised by a
      real launchd boot) — confirmed still accurate, not yet closed at
      the architectural root the way `libxpc`/`libresolv`/`libbsm`/
      `libsystem_notify` were.
    - `libncurses.dylib`: `_clock_get_time`/`_host_get_clock_service`/
      `_mach_host_self`/`_semaphore_create` — not yet investigated
      against any real consumer at all.
    - `libopenpam.dylib`: `___ldpart_free_extra`/`___part_load_locale`/
      `_alarm`/`_connect$UNIX2003`/`_getgroups`/`_initgroups`/
      `_send$UNIX2003`/`_setegid`/`_seteuid`/`_sigaction`/
      `_writev$UNIX2003` — not yet investigated against any real
      consumer at all.

    **Real next step for whoever picks this up**: before trusting
    `syslogd.macho`/`launchctl.macho`/any `build_simple_cmds.sh` command
    that loads one of the dylibs above, run a real QEMU boot/exec of it
    specifically (not just a clean link) — the exact same
    `tools/boot_test.py --expect-fail 'panic\('` + read-the-panic-string
    method that closed all ~20 launchd-specific gaps above. Don't assume
    a clean `ld` link or a passing `gap_audit.sh` means it will actually
    run.

d11. **RESOLVED 2026-08-29 (session 5, main-repo commit `76ff8cd`;
    diagnostics removed in `a1678ab`). Full write-up:
    `docs/launchd-bug6-postmortem.md`.**
    **Root cause: `_FORTIFY_SOURCE` self-recursion in
    `third_party/Libc/secure/*_chk.c`, a USERLAND BUILD-FLAG bug — not a
    kernel context-switch bug, not QEMU.** Apple builds that whole
    directory with fortify OFF because each `__X_chk()` falls through to
    plain `X()` on the "unbounded size" path, and `<stdio.h>`/
    `<string.h>`'s fortify inlines rewrite that call back into
    `__builtin___X_chk()` — itself. Unconditionally: the rewritten call
    passes `__darwin_obsz(dest)` == `(size_t)-1` for any pointer whose
    size isn't statically known (always true of a `_chk` function's own
    parameter), and "unbounded" is exactly the branch that calls the
    plain function. `___vsprintf_chk` was emitting
    `mov x2, #-0x1; bl ___vsprintf_chk`, blowing launchd's main-thread
    stack into its guard page on the first real `launchd_syslog()` call.
    Fix: `-D_FORTIFY_SOURCE=0` on every `Libc/secure/*_chk.c` compile in
    `build_launchd_core.sh`/`build_corefoundation.sh`/`build_libxml2.sh`/
    `build_libmalloc.sh`/`build_dyld_core.sh`, matching what
    `build_libdispatch_dylib.sh`/`build_libxpc_syscalls2.sh` already did
    (their `CFLAGS_SECURE` arrays) — which is precisely why *their* `_chk`
    functions were fine and these five scripts' were not. Swept every
    built artifact for the same class: `___memmove_chk` (launchd.macho +
    libxml2.dylib) and `___stpncpy_chk` (libxml2.dylib) were identical
    latent landmines; post-fix sweep finds zero self-recursing `_chk`
    functions anywhere.

    **Two decisive clues were sitting in already-captured logs, needing
    no new boots — worth internalizing for next time:**
    (1) `handle_user_abort`'s `codes[0]` was `0x2` =
    `KERN_PROTECTION_FAILURE`, i.e. `vm_fault()` FOUND a VM entry and
    refused the write => **guard page => stack exhaustion**. It was
    read as "page never mapped" (that would be `0x1`,
    `KERN_INVALID_ADDRESS`), and the whole kernel-side `SP_EL0` theory
    was built on that misreading. (2) `lr - pc` was exactly `0x50` in
    all five captured boots across five different kernel builds — the
    return address sits inside the same function faulting in its own
    prologue, the signature of direct self-recursion. **Decode fault
    result codes exactly, and check `lr` vs `pc` for self-recursion,
    before theorising about the scheduler.**

    Verified statically (`llvm-objdump`: `___vsprintf_chk` now calls
    `_vsprintf`/`_vsnprintf`) and by real boot
    (`qemu/launchd_boot_test74.log`): the `IOKITDIAG18` fault is gone (0
    occurrences; present in every prior boot), launchd mounts devfs,
    finishes its mutex work, and successfully spawns its **third** thread
    (task reports 3 threads, was 2 — the `update_thread` `pthread_create`
    that used to trigger the crash).

    **Follow-on #1 — the silent `exit(1)` — ALSO RESOLVED 2026-08-30**
    (commits `b01de02`, `ebfd181`, PureDarwin `2f0ef34`). The `pid 1
    exited ... (signal 0, exit 1)` with zero console output was TWO more
    duplicate-static-copy bugs (the 4th and 5th of that class): a TSD
    base clobber in `pthread_tsd_shim.c` and two private
    `__pthread_ptr_munge_token`s signing one shared `_main_thread`
    (launchd statically linked a second full copy of libpthread next to
    the real `libsystem_pthread.dylib`). Full write-up:
    **`docs/launchd-pid1-postmortem.md`**, which also covers the
    2026-08-30 `fork()`/atfork work below. The kernel-side
    `IOKITDIAG15`/`17`/`18` instrumentation referenced by the older text
    here has since been stripped (`a1678ab`, xnu `54de1e85c`); the
    `TEMP DIAGNOSTIC: devfs_mount ...` prints still in
    `bsd/miscfs/devfs/devfs_vfsops.c` on `bcm2837-bringup` are a
    separate leftover, tracked as its own P3 item below.

    **Follow-on #2 — forked children — PARTIALLY CLOSED 2026-08-30.**
    launchd as PID 1 now reaches `launchd_runtime()` and really forks
    daemon children. Those children used to die with a real
    `EXC_CORPSE_NOTIFY` (`qemu/launchd_boot_test95.log`, pids 3 and 4)
    because this port's `fork()` did almost none of what real
    `libSystem_atfork_child()` does. `fork()` now implements the real
    `libSystem_atfork_prepare/parent/child` triple (see
    `tools/userland_staging/build_launchd_syscalls.sh`'s `fork_compat.c`
    and `docs/launchd-pid1-postmortem.md`); the corpses are gone
    (`qemu/launchd_boot_test109_clean.log`) and children now get through
    `_mach_fork_child` → `_pthread_atfork_child` → `_malloc_fork_child`
    → `dispatch_atfork_child`. They still do not reach `exec`. The
    remaining known walls are filed individually as **d13** (dyld atfork),
    **d14** (`_libc_initializer` tier) and **d15** (residual atfork
    handlers) below — do not treat this item as the tracker for them.

    Historical context (sessions 2–5) archived, not deleted: the full
    investigation trail — the TPIDRRO_EL0 cross-image-confusion
    hypothesis, the third and fourth duplicate-static-copy bugs found in
    `mig_reply_port.o` and a duplicate `pthread_create`, the kernel-side
    `IOKITDIAG15`/`16`/`17`/`18` kprintf bisection down into
    `thread_block(ipc_mqueue_receive_continue)`, the raw
    `handle_user_abort()` fault-register capture that finally pointed at
    the MAIN thread, the `SP_EL0` cross-contamination theory, and the
    `Switch_context` bracketing attempt that caused its own separate crash
    and had to be reverted — is archived verbatim to
    `docs/historic/backlog.md`'s "Third archive pass, 2026-08-30".
    **Note its "second thread"/`TPIDRRO_EL0`/`SP_EL0` framings are all now
    known wrong**; read `docs/launchd-bug6-postmortem.md` and
    `docs/launchd-pid1-postmortem.md` first, plus project memory
    `project_xnu_rpi3_launchd_pid1_2026-08-28_session2.md` /
    `project_xnu_rpi3_static_to_dylib_sweep_2026-08-29.md`.

    Two lessons from that trail are standing rules and are kept here:
    decode fault result codes exactly and check `lr` vs `pc` for
    self-recursion *before* theorising about the scheduler; and
    `kprintf()` is not safe to call from inside `machine_switch_context()`
    on a brand-new thread's first-ever resume (a `task != kernel_task`
    guard is not sufficient).

d12. **Missing/incomplete library work, explicitly tracked so it isn't
    lost (cross-referenced from item d8, not duplicated in full here)**:
    - `libCoreFoundation.a` → `.dylib`: deliberately deferred, own future
      session (real load-time global state/constructors, largest/
      riskiest remaining conversion) — see item d8's own "RESOLVED
      2026-08-29" note.
    - Real per-module PAM `.dylib`s: out of scope, this project has no
      `dlopen` — `libopenpam.dylib` (base library only) is as far as
      this converts; documented, not attempted further.
    - `build_libsystem_c.sh`'s own narrow curated `ar x` extraction from
      `libm.a`/`libremovefile.a`/`libopenpam.a` (acos/asin/sqrt/fmod/
      qsort/heapsort/time/gettimeofday/etc, needed by ICU): real,
      deliberately deferred residual — lower risk than a full-archive
      duplicate-global-state bug (mostly stateless math/qsort/time
      functions), but still real static-linking debt. Real follow-up
      path already scoped in item d8's own note: reexport
      `libsystem_m.dylib`/`libopenpam.dylib` through `libSystem.B.dylib`
      (matching the pattern that already closed the equivalent
      `libresolv`/`libcopyfile`/`libremovefile` need for ICU), then drop
      the curated extraction entirely.
    - `libdispatch_min.a`/`libdispatch_real.a`: still not investigated —
      likely dead now that `libdispatch.dylib` is real (Phase 6.6), but
      nobody has actually checked for remaining real consumers before
      proposing deletion. Still open.
    - `tools/userland_staging/build_libpthread.sh`'s own
      `pthread_smoketest.macho` link is currently broken (real,
      pre-existing, found 2026-08-29 while rebuilding this library for
      unrelated reasons — not caused by that session's own changes):
      fails with undefined `_kdebug_trace`/`_kill`/`_kill$UNIX2003`/
      `_mprotect$UNIX2003`/`_sleep$UNIX2003`/`_write$UNIX2003` (real
      `-Wl,-alias`/raw-syscall gaps in that one smoketest's own link
      line, same class of gap as this file's other items, just not yet
      triaged). The real `libpthread.a` archive itself still builds and
      is correct; only its own standalone smoketest executable fails to
      link. Not investigated further this pass.

d13. **Real dyld atfork hooks exist and are built, but hang every forked
    child — currently NOT called (filed 2026-08-30).** Real Apple
    `libSystem_atfork_child()` calls `_dyld_fork_child()` (and
    `_dyld_atfork_prepare()`/`_dyld_atfork_parent()` on the other two
    sides). All three are real, present in `third_party/dyld`, and really
    exported by this project's `libdyld.dylib` (`nm -gU`). This project's
    `fork()` (`tools/userland_staging/build_launchd_syscalls.sh`'s
    `fork_compat.c`) deliberately does **not** call them, because doing so
    is a measured regression: with them in, every forked launchd child
    reached `_dyld_fork_child()` and hung there forever — no crash, no
    corpse, just a stuck process (`qemu/launchd_boot_test107_atexit_fix.log`:
    the console markers for `_mach_fork_child`, `_pthread_atfork_child`
    and `_malloc_fork_child` all printed for all three children, the
    marker immediately after `_dyld_fork_child()` never did). With them
    removed the same children get all the way through
    `dispatch_atfork_child()` (`qemu/launchd_boot_test108_nodyld.log`).

    **Not diagnosed — this is the lead, not the answer.** Do not write it
    up as root-caused until it is actually confirmed:
    `RuntimeState::takeLockBeforeFork()`
    (`third_party/dyld/dyld/DyldRuntimeState.cpp`) takes `loadersLock`,
    `notifiersLock` and `tlvInfosLock` unconditionally, while the matching
    `resetLockInForkChild()` only resets them when
    `libSystemHelpers->version() >= 2`; and the reset itself calls
    libplatform's `os_unfair_recursive_lock_unlock_forked_child()`
    (`third_party/libplatform/src/os/lock.c`), which by design
    `__LIBPLATFORM_CLIENT_CRASH__`es with "Lock was not held" when the
    lock does not look held. Either half of that mismatch strands a
    forked child. **First thing to check is this port's real
    `LibSystemHelpers::version()`** (`third_party/dyld/libdyld/
    LibSystemHelpers.cpp`), then whether `gDyld.apis` and `gProcessInfo`
    are what dyld expects in a forked child here. Note `build_dyld_core.sh`
    builds with `-DBUILDING_DYLD=1` (so those bodies are live) while
    `build_libdyld.sh` uses `-DBUILDING_DYLD=0` (glue only).

    Leaving them out is not a regression against the pre-2026-08-30 state:
    this port's `fork()` never called any dyld atfork hook at all before.

d14. **`Libc/sys/_libc_init.c` — real Apple's `_libc_initializer()` — is
    never built, and nothing calls the eight real initializers it runs
    (filed 2026-08-30, one of them narrowly worked around).** Real Apple
    runs, once per image, at libSystem init time:
    `_program_vars_init`, `_libc_fork_init`, `__confstr_init`,
    `__atexit_init`, `_init_clock_port`, `__chk_init`, `__xlocale_init`,
    `__guard_setup`, `_subsystem_init`. This project has never built that
    file — several of the supporting Libc sources it needs
    (`gen/nanosleep.c` for `_init_clock_port`, `stdlib/FreeBSD/getenv.c`'s
    lock half, `_subsystem_init`'s `apple[]` plumbing) are not in this
    port's libc tier at all — so every one of those steps silently does
    not happen.

    **This is not theoretical; it already produced one real, totally
    silent boot hang.** `atexit()` hung forever in every binary linking
    `liblaunchd_syscalls.a`'s `atexit.o`. Real `atexit_register()`
    (`Libc/stdlib/FreeBSD/atexit.c`) opens with `struct atexit *p =
    __atexit; assert(p);`, and `__atexit` is a file-static that only
    `__atexit_init()` ever sets — from `_libc_initializer()` and nowhere
    else. So `__atexit` stayed `NULL`, the assert fired, and on this port
    `assert()`'s `abort()` is `kill(getpid(), SIGABRT)` with no real
    signal delivery yet, so instead of dying the process spins forever —
    silently, with launchd's `stderr` on `/dev/null`. It went unnoticed
    for months only because launchd's one boot-path `atexit()` call site
    (`ipc_server_init()`'s `atexit(ipc_clean_up)`) was unreachable: that
    function bailed out several statements earlier at
    `mkdir(LAUNCHD_SOCK_PREFIX)` returning `EROFS` while the root
    filesystem was still read-only. Fixing that ordering made the call
    reachable and turned the boot into a silent hang.

    **What was done**: `__atexit_init()` alone is now called from a real
    image constructor in `tools/userland_staging/
    launchd_syscall_compat_headers/isthreaded_compat.c` (that file,
    because `__atexit_init` is `__private_extern__` so only a same-image
    object can reach it, and because `__isthreaded` is referenced by
    `atexit.c` itself so the linker pulls that object out of the archive
    exactly when `atexit.o` is pulled and never otherwise). It is
    idempotent — `__atexit_init()` is one `os_alloc_once()` call.
    Verified by real boot (`qemu/launchd_boot_test107_atexit_fix.log`,
    `..._109_clean.log`).

    **What is still open**: the other seven initializers, for every image
    in this project, not just launchd. `third_party-patches/Libc.patch`
    already carries a second one-off of the same shape — a ctor calling
    `__xlocale_init()` for `__locale_key` — which is the same bug class
    solved the same ad-hoc way (see the `### d11` locale section below).
    Two one-offs is the point at which this should stop being handled
    per-symbol: either build `_libc_init.c` for real (closing
    `_init_clock_port`/`__environ_lock_fork_child`/`_subsystem_init`'s own
    gaps on the way, which also unblocks `_libc_fork_child()` in d15) or
    write one deliberate, documented per-image init constructor that runs
    exactly the steps this port really has. **Whoever touches this should
    also sweep for other Libc globals with the same "only
    `_libc_initializer()` ever sets it" shape** — there is no reason to
    believe `__atexit` and `__locale_key` are the only two.

d15. **Real atfork handlers still missing from this port's
    `libSystem_atfork_child()` (filed 2026-08-30).** `fork()` now runs the
    real Libsystem-1292.50.1 sequence, but four real handlers in it are
    genuinely absent here and two more are present-but-unreachable. Full
    per-symbol writeup lives in `fork_compat.c`'s own header comment in
    `build_launchd_syscalls.sh`; summarized so it is findable:
    - `_libc_fork_child` — the source exists
      (`third_party/Libc/sys/_libc_fork_child.c`) but two of its three
      callees do not: `_init_clock_port` (`Libc/gen/nanosleep.c`) and
      `__environ_lock_fork_child` (`Libc/stdlib/FreeBSD/getenv.c`) are
      absent from the built `libsystem_c.dylib` (`_arc4_fork_child` is
      there, as a non-exported local). Blocked on the same missing libc
      tier as **d14**, and must be compiled *into* `libsystem_c.dylib` to
      see its own private callees.
    - `xpc_atfork_prepare`/`_parent`/`_child` — PureDarwin's libxpc
      reimplementation has no fork handlers at all (zero hits for
      `xpc_atfork` anywhere in `third_party/PureDarwin`). A real gap in
      that source, not an omission here. Relevant: `bootstrap_init()`,
      which is exactly where the forked children used to die, is libxpc
      code.
    - `_notify_fork_child` — real and built
      (`libsystem_notify.dylib` exports it), but `launchd.macho` does not
      link `libsystem_notify.dylib` and `libSystem.B.dylib` does not
      reexport it, so calling it means a real link-line change to every
      consumer of `liblaunchd_syscalls.a`. Wire it up when something in
      launchd actually uses libnotify across a fork.
    - `cc_atfork_*` (corecrypto), `_asl_fork_child`,
      `_libcoreservices_fork_child`, `_libtrace_fork_child`,
      `_libSC_info_fork_*` — those subsystems do not exist in this port at
      all.

    **Concrete lead for the current child wall.** With dyld's trio omitted
    (**d13**), forked children get through `dispatch_atfork_child()` and
    then stop at the last step, `_pthread_atfork_child_handlers()`
    (`qemu/launchd_boot_test108_nodyld.log`). That step is not a no-op
    here: `nm -m launchd.macho` shows real `___cf_atfork_prepare` and
    `___cf_atfork_child` in the image, i.e. **CoreFoundation really does
    register `pthread_atfork()` handlers in this process**, and
    `_pthread_atfork_child_handlers()` is what invokes them in the child.
    So the next thing to instrument is CF's own atfork child handler, not
    libpthread's iteration loop. Note also that `pthread_atfork()` handler
    *registration* is what `globals->pthread_atfork_lock` protects, and
    `_pthread_atfork_prepare_handlers()`/`_pthread_atfork_parent_handlers()`
    take/release it around the fork — verify that pairing holds in this
    port before assuming the handler body itself is at fault.

d16. **`_malloc_fork_child()` only reinitializes launchd's own private
    heap, because `launchd.macho` has two heaps (filed 2026-08-30).**
    `nm -m launchd.macho` shows `_malloc`, `_free`, `_calloc`, `_g_zone`
    and `_create_scalable_zone` all defined **locally** in the executable,
    while it also loads `libSystem.B.dylib` (which reexports
    `libsystem_malloc.dylib` with its own separate `g_zone`). So
    `libdispatch`/CF/libc++ allocate from one zone and launchd's own code
    from another, and the new `_malloc_fork_child()` call in `fork()`
    binds to launchd's local copy and reinitializes only that zone. This
    is exactly the already-documented Phase 2b condition in
    `docs/static-linking-cleanup-plan.md` ("`launchd.macho` is the
    highest-risk one: it defines `_malloc`, `_free`, ... locally despite
    loading `libSystem.B.dylib`") — closing Phase 2b fixes this hook for
    free. Deliberately no `dlsym(RTLD_NEXT)` workaround was added to paper
    over it.

d17. **`tools/userland_staging/build_launchctl.sh` does not link — real,
    pre-existing, found 2026-08-30.** Confirmed pre-existing by rebuilding
    it with every one of that session's changes stashed: same failure.
    The `launchctl.macho` that had been sitting in `libc_build/` was a
    stale artifact from before `libCoreFoundation.a` grew the call sites
    that break the link, i.e. it was **not reproducible from source** —
    a direct violation of this repo's "every built artifact needs a
    reproducing script" rule, and the reason the breakage went unnoticed.
    (The script `rm -f`s its output before linking, so the stale copy is
    gone now.)
    - **Fixed**: undefined `___chkstk_darwin` (referenced by CF's
      `CFAttributedString.o`/`CFBinaryPList.o`). `libsystem_c.dylib` must
      be listed *explicitly* on the link line, not left to
      `libSystem.B.dylib`'s `LC_REEXPORT_DYLIB` of it — ld64 cannot follow
      a reexport by install-name when `/usr/lib/system/libsystem_c.dylib`
      does not exist in the on-disk SDK/sysroot, which it does not here.
      `build_launchd.sh` already documents this at length above its own
      link line; `build_launchctl.sh` had simply never had the same fix.
    - **Still open**: undefined `_lchflags` (`launchctl.c`'s
      `empty_dir()`) and `_sysctlnametomib`
      (`limitloadtohardware_iterator()`). Real Apple source for both is
      already pinned — `third_party/Libc/emulated/lchflags.c` and
      `third_party/Libc/gen/FreeBSD/sysctlnametomib.c` — but each pulls
      its own further real dependencies (`lchflags` needs `chflags` +
      `setattrlist`; `sysctlnametomib` needs the raw `__sysctl` trap,
      which this stack does not currently generate — only the public
      `sysctl` exists). Expect a short cascade, same shape as every other
      gap-closure pass here.

d18. **Possible duplicate `_getenv` inside `libsystem_c.dylib` (filed
    2026-08-30, unverified).** `nm -a libc_build/system/libsystem_c.dylib`
    reports **two** `_getenv` symbols — one external (`T`, `0x21984`) and
    one local (`t`, `0x2fc48`). Two implementations of `getenv` in one
    image is the same duplicate-static-copy shape that produced bug #6 and
    the duplicate-`pthread_create` bug, and `getenv`/`setenv` share
    process-global `environ` state, so it is exactly the risky kind. Not
    investigated at all — noticed in passing while checking that image for
    `__environ_lock_fork_child` (see **d15**). Confirm with
    `tools/userland_staging/dup_symbol_audit.sh` and find which two
    objects define it before assuming it is benign.


---

**Note on duplicate IDs**: the two `### d10`/`### d11` headed sections
below predate the numbered `d10.`/`d11.` items in the "Userland library
debt" list above and are *different* items with the same short IDs. Source
comments referring to "item d10"/"item d11" may mean either — the
`libSystem.B.dylib`/Libm-gap and locale/`strftime_l` references mean the
`###` sections below; the chained-fixups and launchd-bug-#6 references mean
the numbered items above. Both are kept resolvable deliberately.

### d10: launchd PID-1 boot-path integration, in progress 2026-08-28

Real Milestone 4/5 work (docs/daemon-roadmap.md Phase 6.8): `/sbin/launchd`
on the SD image is now real, dynamically-linked `launchd.macho` (explicit
user choice: outright replace `tools/init_binary/`'s custom static PID-1,
not exec-into). Kernel needed zero changes -- `load_init_program()`
already hardcodes `/sbin/launchd`, stock unmodified XNU behavior.
`pd_launchd_load_daemons_dir()` (real PureDarwin source, previously
defined but never called anywhere upstream) is now wired into
`launchd.c`'s `main()`; `pd_launchd_boot()` now also remounts `/` rw
(same real kernel quirk `init.c`'s own `remount_root_rw()` already
documented). `pd_console_login.c` (real PureDarwin getty/login-equivalent
helper, built standalone as `/usr/libexec/pd-console-login`) had its
hardcoded `/bin/zsh` swapped for this project's real `/bin/bash` --
**note this drops password authentication entirely** (no login prompt,
auto-execs a root shell) versus `init.c`'s own real login/su support;
scoped as a known simplification to get first boot working, not a
permanent design choice -- revisit once launchd boots reliably.

Iterating real dyld "Symbol not found" aborts one at a time via real
QEMU boots (`_abs`/`_llabs` missing from `libSystem.B.dylib`'s real
export chain, then `_atol`/`_bsearch`/`_readlink`/`_getpeername`/
`_gethostuuid`/`_setiopolicy_np`/`_audit_session_self`/
`_audit_session_port`/`_audit_session_join`/`_asctime`/`_asctime_r`/
`_strtoll`/`_strtoll_l`/`_strtoul_l`) -- same shape as every other real
gap-closure pass in this project, see `tools/userland_staging/
launchd_audit_syscalls_compat.c` (new) for the honest-stub writeups on
the three `audit_session_*` routines (no real userland client source
exists anywhere in this project's pinned repos, only the kernel side).

**Real, not-yet-implemented follow-up surfaced by this pass**: this
project has no `launchctl` client at all. Real macOS's actual job-load
flow is `launchctl load <plist>` sending an IPC/XPC message to a running
launchd, not launchd reading `/Library/LaunchDaemons` itself at boot --
the `pd_launchd_load_daemons_dir()` wiring above is a real, working, but
simplified stand-in for that (loads once at boot, no dynamic
load/unload/list/start/stop after that). A real `launchctl` port is a
separate, sizeable follow-up (needs the real `vproc_mig_*`/job-control
MIG client side `libvproc.c` already has some of) -- filed here so it
isn't rediscovered from scratch; not blocking first boot.

### d11: real locale (xlocale) source exists and is pinned -- partially wired, `strftime_l` still a real wall

In response to a real question raised while closing d10's `asctime`/
`atol`/`strtoll_l`/`strtoul_l` gaps: **locale is not a "never
open-sourced" wall like ASL/corecrypto** -- real Apple/FreeBSD source is
already pinned at `third_party/Libc/locale/` (`xlocale.c`, `isctype.c`,
`FreeBSD/*.c`) and partially built into `libc_locale.a`
(`build_libc_locale.sh`): `isspace_l`/`toupper_l`/`__global_locale`/
`__locale_key`/`__current_locale()` (a header-inline function,
`xlocale_private.h`) all already compile and link real, confirmed by
`nm`. A real, already-committed patch (`third_party-patches/Libc.patch`)
fixed `__locale_key`'s init-order bug (added a real ctor calling
`__xlocale_init()`, since nothing in this project calls Apple's real
`_libc_initializer()`). **Good news, re-tested 2026-08-28**: with that
fix already in place, real `atol.c`/`strtoll.c`/`strtoul.c` (which all
route through `isspace_l`/`__current_locale()`) now compile and link
completely clean against `libc_locale.a` with zero changes needed --
the "tried and reverted" history below predates that fix landing.
`atol`/`strtoll`/`strtoll_l`/`strtoul`/`strtoul_l` are now real,
unmodified Apple/FreeBSD source in `libc_gen.a`, not a workaround.

**TEMPORARY FIX, revisit later -- what's still genuinely missing**:
`strftime_l` itself -- a large, locale-aware date/time formatter
(`stdtime/FreeBSD/strftime.c`) that `asctime_r()` calls internally just
to format a 4-digit year (a real implementation shortcut on FreeBSD's
part, not a functional need -- `asctime`'s output format is fixed by
the C standard regardless of locale). A prior pass tried pulling
`strftime_l` in for real and reverted it as a deep, disproportionate
dependency chain for what it bought at the time; unlike atol/strtoll/
strtoul above, this one is NOT yet re-tested against the current
`libc_locale.a` state, so it may or may not still be as deep a wall --
genuinely unknown, not re-attempted this pass. d10 worked around it for
`asctime`/`asctime_r` specifically with a small, honest,
locale-independent from-scratch implementation instead
(`launchd_audit_syscalls_compat.c`'s `asctime`/`asctime_r`/`put2`/
`wday_name`/`mon_name`) -- this is a real, deliberately temporary
stand-in, not a permanent design choice: it duplicates real FreeBSD
logic by hand instead of linking the real `asctime.c`, and should be
replaced with the real source the moment `strftime_l` is actually
ported (re-test it first -- it may already "just work" the same way
atol/strtoll did). `strftime_l`/full `strftime` itself remains a real,
open, not-yet-closed gap for any future caller that needs actual
locale-aware formatting (not just a fixed year digit
string) -- real source is right there, pinned, just needs the same
kind of dedicated pass CF's own locale work got.

---

## P3 — hygiene / cleanup

1. **Two headers overclaim a nonexistent IOKit matching-engine bug.**
    `iokit/BCM2837ArmIO/BCM2837ArmIO.h` and
    `iokit/BCM2837InterruptController/BCM2837InterruptController.h` both
    assert (one claims "empirically confirmed via instance-pointer-tagged
    logging") that `IOService::matchInternal()`'s `IONameMatch` handling
    over-matches personalities against sibling DT nodes. Tracing the real
    path (`IOPlatformDevice::compareName` → `IODTCompareNubName` →
    `CompareKey()`) shows an exact string match with no such defect. The
    actual bug in the commit that added these `probe()` guards
    (`c5fa3c6`) was a blind cast through the wrong vtable in
    `BCM2837ArmIO::start()` (since fixed, same commit) — a real, different
    bug that produced the same symptom. The `probe()` guards themselves are
    harmless belt-and-braces, worth keeping; just correct the stated
    reason in both header comments.

2. **Stray nested git repo at `iokit/.git/`** (own `config`/`HEAD`, no
    remote configured) inside a directory the parent repo tracks normally.
    Makes `git status` run from inside `iokit/` misleading/wrong. Probably
    a leftover from early scaffolding before the top-level repo existed —
    verify there's no uncommitted work in it, then remove it.

3. **`iokit/Kernel/IOService.cpp` still fires unconditional `TEMP
    DIAGNOSTIC` prints on every `registerService`/`startMatching`** (136
    lines in a single partial boot capture). At real mini-UART baud rates
    (115200) each line costs milliseconds of synchronous busy-wait —
    combined with P0 item 3's slow clock, this would make a working
    real-hardware boot look like a hang. Prune once no longer needed for
    active debugging.

    `bsd/msdosfs/msdosfs_vnops.c`'s `TEMP DIAGNOSTIC` read/blockmap prints
    and `third_party/qemu-zhuowei`'s `hw/sd/sdhci.c` per-command/per-block
    `DIAG` prints (the dominant source of console noise — 471 lines in the
    same capture, entirely a QEMU emulator-side trace, never runs on real
    hardware) were both gated behind a default-off flag **[FIXED
    2026-08-19]** (`xnu-7195` commit `89d131a6b`, `qemu-zhuowei` commit
    `05daaa2`, both local/unpushed) rather than pruned outright, so they're
    a one-line flip away if that SD path needs debugging again.

4. **`BCM2837CPU::quiesceCPU()`/`haltCPU()` are silent no-ops** — correctly
    hedged as fine for now (single core, no sleep/shutdown ever attempted),
    but will need real implementations before any power-management work.

5. **`bsd/miscfs/devfs/devfs_vfsops.c`'s `TEMP DIAGNOSTIC: devfs_mount ...`
    prints are still live, and are committed on `bcm2837-bringup`** — not
    a dirty working tree, so `git status` in `third_party/xnu-7195` looks
    clean and hides them. They fire on every `devfs` mount and are all
    over every recent boot log (e.g.
    `qemu/launchd_boot_test109_clean.log`), which is exactly the noise P3
    item 3 above describes. They are a leftover from the launchd PID-1
    devfs bring-up, whose real work is long since done. Same treatment as
    item 3's msdosfs/sdhci prints: gate behind a default-off flag or drop.
    Noticed 2026-08-30 while reading launchd boot logs; the surrounding
    launchd bug #6 kernel instrumentation (`IOKITDIAG15`/`17`/`18`) was
    already correctly stripped in `a1678ab`/xnu `54de1e85c`, this one was
    simply missed.
```
