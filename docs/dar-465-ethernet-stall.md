# DAR-465: LAN9514 Ethernet wedges after a large transfer (real Pi 3)

Status: candidate fix built, **not yet run on hardware**. The defect fixed
below is proven from the source. That it is what triggered the observed
wedge is a hypothesis until a Pi run with the diagnostics added here.

## Symptom

Real Pi 3, image `pi3-20261001-2251` (xnu `bcm2837-bringup` 5fdb3f186,
`cpus=4`). The board took a 1.5 MB push over ssh, then sent about 3.7 MB
back. Around then the serial console started repeating, for every frame:

    SMSC9514FamilyTransport: bulk OUT stalled, clearing the pipe and retrying
    SMSC9514FamilyTransport: bulk OUT of 82/106 bytes failed (0xe000404f)

The kernel stayed up. ssh and ping timed out. `ifconfig en0 down; up` changed
nothing, and only a reboot brought it back. Earlier single-core sessions had
not shown it, but they also moved less traffic, and less of it from the Pi.

## What the log proves

`0xe000404f` is `kIOUSBPipeStalled`. Reading the code path
(`SMSC9514USBDriver.cpp` `bulkOut()`, main 489a296):

1. `Write()` returns `kIOUSBPipeStalled`.
2. `ClearPipeStall(true)` runs: the family clears its software stall flag,
   the UIM resets the host toggle to DATA0
   (`BCM2837USBHostController.cpp:1528-1554`, `ep->dep.toggle =
   kHCPID_DATA0` at :1551), and a CLEAR_FEATURE(ENDPOINT_HALT) goes to the
   device (`third_party/IOUSBFamily/.../IOUSBPipe.cpp:393-520`).
3. The retried `Write()` fails with `kIOUSBPipeStalled` again.

That second error has to come from the hardware. The family's
software-stalled flag was cleared in step 2 (`IOUSBPipe.cpp:403`), and the
only UIM source of `kIOUSBPipeStalled` is `kIOReturnNotResponding`, which
`decodeChannelResult()` returns only for `HCINT.STALL`
(`BCM2837DWC2.cpp:1066-1067`). The UIM translates it at
`BCM2837USBHostController.cpp:929-931`. So the LAN9514 really answered the
retried OUT with a STALL, even though its halt had just been cleared. It did
that for every frame afterwards. Note also that any other synchronous-write
error marks the pipe stalled in software
(`IOUSBPipe.cpp:906-913`, and `IOUSBPipeV2.cpp:695-701` for a V2 pipe:
"any err coming back in the callback indicates a
stalled pipe"), so the *first* failure need not have been a STALL. The log
the coordinator saw does not show what the first failure was.

So this is not a host toggle that clear-stall failed to reset (suspect 3):
the host half is reset, and the device half is reset by the CLEAR_FEATURE.
It is a device that keeps refusing its bulk OUT stream. The LAN9514 has a
plausible reason to do that. `HW_CFG_SBP_` is "Stall Bulk Out Pipe Dis."
(Linux `drivers/net/usb/smsc95xx.h:78`, v6.6), so by default the chip can
STALL bulk OUT. *Inferred, not from a datasheet (DAR-313):* that it does so
when its transmitter flags an error (`INT_STS_TXE_`, :56), and keeps doing
so after CLEAR_FEATURE until the TX path is restarted. Neither this driver
nor Linux sets SBP. `ifconfig down/up` cannot clear the state
because `SMSC9514Ethernet::enable()`/`disable()` only start and stop timers
(`SMSC9514Ethernet.cpp:766-793`). A reboot resets the chip, which is why it
recovered.

The open question is what corrupted the TX stream in the first place.

## Root cause (defect proven in source; trigger is the leading hypothesis)

**The DWC2 engine drops partial progress on a chunk that halts before
XFERCOMP, and keeps the stale data-toggle PID.** (Line numbers in this
section are main 489a296.)

- `waitForChannelSettled()` wakes on `CHHLTD | NAK`
  (`BCM2837DWC2.cpp:833`), with every HCINT bit unmasked (:1252).
- A NAK on a non-periodic channel does not halt it, so `transferChunk()`
  force-halts the channel (:975).
- `decodeChannelResult()` maps NAK to `kIOReturnNotReady` with
  `actualLen = 0` (:1063-1064).
- `doTransfer()` then `break`s (:1333) before the only place it advances
  `done` and takes the next PID from HCTSIZ (:1346-1348).
- The UIM calls with a zero NAK budget, so `doTransferRetrying()` returns
  `kIOReturnTimeout` straight away. The next engine attempt re-sends the
  chunk from its start, with the old PID.

A full-size frame is one bulk OUT transfer of at most 1522 bytes: 8 bytes
of TX_CMD_A/B plus a 1514-byte frame (`SMSC9514Ethernet::outputPacket()`).
That is two full 512-byte high-speed packets plus a short one, all in one
chunk. Suppose the chunk starts at PID P and the device accepts *k* packets
and then NAKs, so it now expects P ^ (k & 1). The host re-sends from
packet 1 with P:

- **k = 1:** packet 1 goes out with P while the device expects P ^ 1. The
  device ACKs and discards it as a duplicate, which is correct because it
  already has packet 1. The host's toggle advances, packet 2 matches, and
  nothing is lost or duplicated.
- **k = 2** (the only other case for a 3-packet frame; in general any
  k >= 2): packet 1 goes out with P and the device expects P, so it is
  accepted again as new data, and so is packet 2. That is duplicated
  512-byte blocks inside the frame.

The LAN9514 delimits frames by the TX_CMD_A/B lengths, so the extra bytes
are parsed as the next command words. *Inferred (DAR-313):* that this
raises a TX error, that the error STALLs bulk OUT, and that CLEAR_FEATURE
does not touch the TX FIFO or error state, so every later frame re-STALLs.

The XACTERR retry inside `doTransfer()` (:1229-1279) has the same flaw: it
re-sends the chunk from its start with the same PID after packets may have
been ACKed. The IN side loses data the same way (its NAKed partial chunk is
discarded), but bulk IN is normally parked on an async channel, whose
`asyncFinish()` already carries progress and PID correctly (:1689-1708).

Linux shows what the code should do. On an abnormally halted channel it
counts packets from `HCTSIZ.PKTCNT` and saves the PID from HCTSIZ:

- `dwc2_get_actual_xfer_length()` (`drivers/usb/dwc2/hcd_intr.c:430-439`,
  v6.6): "Must use the hctsiz.pktcnt field ... always an integral number of
  packets if the transfer was halted before its normal completion".
- `dwc2_hcd_save_data_toggle()` (:510-536).

Both are called from the NAK handler (:1249-1251) and the XACTERR handler
(:1644-1646). Linux's halt handler also says bulk OUT channels "halt on a
NAK to allow rewinding the buffer pointer" (:1902).

Why the trigger fits:
- It needs the Pi to be **transmitting** full-size frames faster than the
  LAN9514 drains them. That is the 3.7 MB pull, not the 1.5 MB push, whose
  Pi-side TX is small ACKs.
- More cores make the sender faster (ssh crypto, TCP and the USB workloop
  run in parallel), so the FIFO-full NAK is likelier. That would explain
  why the single-core sessions never hit it.
- It explains the STALL surviving CLEAR_FEATURE and `ifconfig down/up`, and
  why only a reboot cleared it.

**Not proven:** that a mid-chunk NAK with k >= 2 actually happened on this
board. The DAR-465 diagnostics below log exactly that.

**Reviewer's caveat (adversarial review, 2026-10-02):** on a DWC2 core of
2.71a or later, the "OUT NAK enhancement" may mean `HCINT.NAK` is never
raised for a high-speed bulk OUT (Linux handles that case separately,
`out_nak_enh`, `hcd_intr.c:1814-1822`). If so, this partial-chunk path
never fires for the LAN9514's bulk OUT, and the trigger is elsewhere:
the XACTERR path, the timeout path, or the MAC duplex mismatch below. The
diagnostics decide which.

## Other suspects, ranked

1. *(above)* Partial-chunk rewind with a stale PID. Proven defect, likely
   trigger.
2. **MAC half duplex behind a full-duplex PHY** (reviewer finding). The
   driver never set `MAC_CR_FDPX_` (`smsc95xx.h:215`) or programmed flow
   control after link-up, while the PHY negotiates 100Full. Linux does both
   in `smsc95xx_mac_update_fullduplex()` (`smsc95xx.c:576-603`). A
   half-duplex MAC on a full-duplex link sees collisions and late
   collisions under load, which plausibly raise TX errors and so STALL bulk
   OUT. That this produces TXE is inferred. Fixed below; the
   `TX recovery` line now logs `MAC_CR (FDPX)`.
3. **A UIM OUT timeout abandons a frame part-way.** After 5 s of NAKs the UIM
   fails the transfer with `kIOUSBTransactionTimeout`
   (`BCM2837USBHostController.cpp:191, 908-921`). A truncated frame would
   misframe the TX stream the same way. That would have logged
   `failed (0xe0004051)` first. The new sampled log shows the first
   failure's code. Also, the DWC2's own 2 s channel timeout returned with
   the channel still enabled (fixed below).
4. **SMP race (suspect 1).** Reviewed and found no plausible race on the TX
   path. Shared mutable state and its guards:
   - Channel 0 registers, `fDMAVirt`, and the `fPendingHcInt` consumer:
     `fTransferLock` (IOLock), taken by every `transfer()`,
     `controlTransfer()` and `transferStage()` (`BCM2837DWC2.cpp:1418,
     1446, 1518`).
   - `fPendingHcInt[]`: `fStatusLock` (spinlock, interrupts off) in both
     the interrupt handler, which may run on any core, and the waiter
     (:209, :252, :272).
   - The UIM's endpoint lists, toggles, `fStaging`, async-channel state
     and `fAsyncBusy` writes: the controller's IOUSBWorkLoop gate. Timers,
     the async event source and the command gate all run on it.
   - The DMA buffers are Normal non-cacheable (write-combine). A `dsb sy`
     orders the buffer writes against the channel enable (:1255-1266),
     and reads are preceded by `dsb sy`.
   - `SMSC9514FamilyTransport`: `fTxLock` around `fTxMD`/Write, and
     `fLock` around RX state. `outputPacket()`, the RX timer and the link
     timer all run gated on the driver's own workloop (IOGatedOutputQueue).
   - Unlocked state: the `fFailLog*` counters and the new DAR-465 counters
     (diagnostics only), and the async-channel `HAINTMSK` read-modify-writes,
     which only the workloop does.
5. **The retry never gives up (suspect 5).** True: it is unbounded, once
   per frame, with two log lines per frame. But that is the consequence,
   not the cause. Fixed below.
6. **CLEAR_FEATURE does not reset the host toggle (suspect 3).** Refuted;
   see "What the log proves".

## Fix (branch `dar-465-eth`, iokit-eth worktree)

1. **DWC2 (`BCM2837DWC2.cpp`):** `carryPartialChunk()` and
   `channelPacketsDone()`. When a chunk ends without XFERCOMP (NAK
   force-halt, STALL, or XACTERR) on a non-split channel, the driver now:
   - counts the packets from HCTSIZ.PKTCNT;
   - copies an IN chunk's packets out of the DMA buffer;
   - advances `done`;
   - takes the next PID from HCTSIZ.

   The XACTERR retry resumes from the first packet that was not
   accepted. The UIM then resumes at `xfer->done` with the right toggle.
   Split transfers are unchanged (one packet per chunk, and their own
   state machine).

   The 2 s channel timeout in `transferChunk()` used to return with the
   channel still enabled (DAR-282's class). It now halts the channel
   (`haltChannel()`) and ORs the halt's HCINT into the result. The carry
   above also runs after a timeout, but only if the channel was still
   enabled at the timeout and the halt completed; otherwise HCTSIZ is not
   known to be a halted channel's. One side effect for split transfers:
   the extra HCINT bits from the halt can reach the SSPLIT/CSPLIT state
   machine (ACK, NYET) after a timeout, where before only the bits seen at
   the timeout did.
2. **Diagnostics, sampled (1st to 4th occurrence, then every power of
   two), quiet in the normal case:**
   - `BCM2837DWC2: DAR-465 partial chunk #N carried forward: dev/ep/dir,
     bytes moved of the chunk, pkts programmed, cause, PID old->new, cpu,
     HCINT, HCTSIZ`. Each line marks an event the old code mishandled. On
     an OUT with 2 or more packets moved, the old code would have corrupted
     the stream.
   - `BCM2837DWC2: DAR-465 STALL #N on channel C (dir, cpu): HCINT
     HCCHAR HCTSIZ (PID, PKTCNT) xferLen`.
3. **Transport (`SMSC9514USBDriver.cpp`):** only a stall is cleared and
   retried. A timeout (`kIOReturnTimeout`, or the UIM's own
   `kIOUSBTransactionTimeout`) is cleared and returned as a failure without
   a retry: part of the frame may already be in the TX FIFO, so
   `outputPacket()` restarts the TX path (which flushes the FIFO) before
   the frame is sent again. This also means a timeout no longer holds the
   driver workloop for a second 5 s attempt; the hold was up to about 8 s
   before. The log lines include the CLEAR_FEATURE result and the retry's
   code, and are sampled instead of printed per frame.
4. **Recovery (`SMSC9514Ethernet.cpp`):**
   - **TX-path restart.** Runs when a send still fails after the
     transport's handling: on the first four failures of a streak and every
     8th after. It reads and logs `INT_STS` (TXE/TDFU/TDFO/TX_STOP),
     `TX_CFG`, `HW_CFG` (SBP) and `MAC_CR` (FDPX) first, which is the
     decisive evidence for the TX-error and duplex theories. Then it does `TX_CFG` STOP, FIFO flush, write-1-clear
     of the INT_STS TX bits, and `TX_CFG` ON, and retries the frame.
   - **Full chip re-init.** After 32 consecutive failures, `resetChip()`,
     the MAC address and the RX filter modes are re-applied, and the duplex
     is re-applied on the next link-up. Spacing backs off 5, 10, 20, 40,
     60 s (capped) within one failure streak, and resets on the first
     successful send.
   - **`enable()`** restarts the TX path when the last send failed, so
     `ifconfig en0 down; up` is a real recovery step now.

   The STOP, flush, clear, ON sequence is this driver's own. Linux only
   enables TX at start (`smsc95xx_start_tx_path()`, `smsc95xx.c:850-864`),
   and on a bulk OUT `-EPIPE` it only clears the halt (`usbnet.c:1262`,
   :1141-1160). The register meanings are from `smsc95xx.h:51-78`. This is
   the least-verified part of the change.
5. **MAC duplex (`SMSC9514Ethernet.cpp` `applyDuplex()`, its own commit).**
   On each link-up with a resolved duplex, does what Linux does:
   - `MAC_CR` FDPX set and RCVOWN cleared for full duplex, the reverse for
     half (`smsc95xx.c:576-603`);
   - `FLOW` and the low nibble of `AFC_CFG` from the resolved pause modes
     (`smsc95xx_phy_update_flowcontrol()`, :538-574), with pause resolved
     as `phy_get_pause()` (`phy_device.c:2876-2887`) and
     `linkmode_resolve_pause()` (`linkmode.c:24-43`) do it from
     ANAR/ANLPAR bits 10-11 (`mii.h:83-84, 106-107`).

   `MAC_CR` is read-modify-written, so only FDPX and RCVOWN change. One
   line is logged per link change:
   `DAR-465 link full duplex: MAC_CR=... FLOW=... AFC_CFG=...`.
   Unlike Linux, `AFC_CFG` is never initialised to `AFC_CFG_DEFAULT`
   (`smsc95xx.c:1018`), so its watermark fields keep their reset values.

## Testing it on the Pi (coordinator)

Candidate kernel (built 2026-10-02 from iokit `dar-465-eth` 60b3580, synced
into xnu `dar-465-eth` 4dde4af05 on top of `bcm2837-bringup` 5fdb3f186):
`/Users/anurodhp/Development/iokit-eth/build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped`,
sha256 `2747ddd4cbf2fd4d85d80481ce546ca4464a2ac694f7319f28d332caea97a508`.
Check it with `strings <kernel> | grep DAR-465`.

The change is kernel-only. All of these drivers are compiled into the kernel.
The dtb and uEnv.txt stay as they are in the release (`cpus=4` stays: the bug
needs SMP-speed traffic).

1. Copy the candidate kernel to the BOOT partition as
   `kernel.development.bcm2837.unstripped` (the name u-boot's bootcmd
   loads; see `tools/make_realhw_sd_image.sh:131`). Keep the release kernel
   aside to switch back.
2. Start the serial capture first (port free, no minicom):

       tools/realhw_serial_test.py --raw --timeout 1800 --log /tmp/dar465-serial.log

3. From the host:

       PI_PASS=... MB=4 ITER=20 tools/eth_stress_test.sh      # or key auth, no PI_PASS

Reading the result:
- First, check the `DAR-465 link full duplex: MAC_CR=...` line at link-up.
  If FDPX was the culprit, the release kernel's MAC ran half duplex.
- PASS, and the log has `DAR-465 partial chunk ... OUT ... NAK` lines: the
  stale-PID trigger was real and the fix handled it. Then repeat with the
  **release** kernel and the same script to confirm it wedges there.
- PASS with no partial-chunk lines: either the trigger was not reproduced,
  or the duplex fix removed it, or the OUT NAK enhancement means NAK is
  never reported (see the reviewer's caveat). Raise `MB`/`ITER`, and
  compare against the release kernel with the same script.
- A `DAR-465 TX recovery` line with `TXE 1`: confirms the device was in a TX
  error state. Whether `TX recovered after N failed send(s)` follows shows
  whether the restart is enough.
- Still wedging, with STALLs and no partial-chunk lines: the corruption
  comes from somewhere else. Look at the first failure code in the sampled
  `bulk OUT ... failed` line (0xe0004051 means suspect 2).

## Uncertain

- No hardware run yet; QEMU cannot test any of this (it has no LAN9514).
- That the LAN9514 NAKs mid-frame under load with 2 or more packets
  already accepted is inferred, not observed. It may never be reported to
  this driver at all (OUT NAK enhancement).
- Which fix actually matters, the PID carry or the duplex, is unknown
  until the diagnostics come back.
- The TX_CFG STOP/flush semantics come from register names, not a
  datasheet that was read.
- The forced halt-on-NAK of a high-speed bulk OUT is still used (only the
  accounting is fixed). Linux treats high-speed bulk/control OUT on 2.71a+
  cores specially ("OUT NAK enhancement", `out_nak_enh`,
  `hcd_intr.c:1814-1822`), and in that case never takes the NAK rewind
  branch in `dwc2_hc_chhltd_intr_dma()`. The Pi's core is expected to be
  2.80a; `GSNPSID` is checked at boot (`BCM2837DWC2.cpp`, around :1948) but
  the value was not looked up for this. Not halting on NAK would block the
  USB workloop while the device NAKs, so this change does not do that.
