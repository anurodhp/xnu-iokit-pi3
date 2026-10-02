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
documented reason to do that. `HW_CFG_SBP_` is "Stall Bulk Out Pipe Dis."
(Linux `drivers/net/usb/smsc95xx.h:78`, v6.6), so by default the chip STALLs
bulk OUT when its transmitter flags an error (`INT_STS_TXE_`, :56). Neither
this driver nor Linux sets SBP. `ifconfig down/up` cannot clear the state
because `SMSC9514Ethernet::enable()`/`disable()` only start and stop timers
(`SMSC9514Ethernet.cpp:766-793`). A reboot resets the chip, which is why it
recovered.

The open question is what corrupted the TX stream in the first place.

## Root cause (defect proven in source; trigger is the leading hypothesis)

**The DWC2 engine drops partial progress on a chunk that halts before
XFERCOMP, and keeps the stale data-toggle PID.**

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

A full-size Ethernet frame is a single chunk of 1522+8 bytes: three 512-byte
high-speed packets plus a short one. Suppose the LAN9514 accepts *k* packets
and then NAKs because its TX FIFO is full. The host re-sends packet 1 with
the old PID:

- **k odd:** that PID differs from what the device expects. The device
  ACKs the packet and discards it as a duplicate, the host flips its
  toggle, and the stream realigns. No harm done.
- **k even (2):** the old PID is exactly the one the device expects
  next, so the device accepts packet 1 again as new data. That is a
  duplicated 512-byte block inside the frame.

The LAN9514 delimits frames by the TX_CMD_A/B lengths, so the extra bytes
are parsed as the next command words. That is a TX error, so the chip
STALLs bulk OUT. Its TX FIFO and error state are not touched by
CLEAR_FEATURE, so every later frame re-STALLs.

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

**Not proven:** that a partial NAK with k even actually happened on this
board. The DAR-465 diagnostics below log exactly that.

## Other suspects, ranked

1. *(above)* Partial-chunk rewind with a stale PID. Proven defect, likely
   trigger.
2. **A UIM OUT timeout abandons a frame part-way.** After 5 s of NAKs the UIM
   fails the transfer with `kIOUSBTransactionTimeout`
   (`BCM2837USBHostController.cpp:191, 908-921`). A truncated frame would
   misframe the TX stream the same way. That would have logged
   `failed (0xe0004051)` first. The new sampled log shows the first
   failure's code.
3. **SMP race (suspect 1).** Reviewed and found no plausible race on the TX
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
4. **The retry never gives up (suspect 5).** True: it is unbounded, once
   per frame, with two log lines per frame. But that is the consequence,
   not the cause. Fixed below.
5. **CLEAR_FEATURE does not reset the host toggle (suspect 3).** Refuted;
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
   state machine). The `kIOReturnTimeout` case (the channel never settled)
   is unchanged.
2. **Diagnostics, sampled (1st to 4th occurrence, then every power of
   two), quiet in the normal case:**
   - `BCM2837DWC2: DAR-465 partial chunk #N carried forward: dev/ep/dir,
     bytes, pkts, cause, PID old->new, cpu, HCINT, HCTSIZ`. Each line marks
     an event the old code would have mishandled. On an OUT with an even
     packet count, it would have corrupted the stream.
   - `BCM2837DWC2: DAR-465 STALL #N on channel C (dir, cpu): HCINT
     HCCHAR HCTSIZ (PID, PKTCNT) xferLen`.
3. **Transport (`SMSC9514USBDriver.cpp`):** the stall/timeout line now
   includes the CLEAR_FEATURE result and the retry's code, and both lines
   are sampled instead of printed per frame.
4. **Recovery (`SMSC9514Ethernet.cpp`):**
   - **TX-path restart.** Runs when a send fails even after the transport's
     clear-and-retry: on the first failure of a streak and every 8th after.
     It reads and logs `INT_STS` (TXE/TDFU/TDFO/TX_STOP), `TX_CFG` and
     `HW_CFG` (SBP) first, which is the decisive evidence for the
     TX-error theory. Then it does `TX_CFG` STOP, FIFO flush, write-1-clear
     of the INT_STS TX bits, and `TX_CFG` ON, and retries the frame.
   - **Full chip re-init.** After 32 consecutive failures, `resetChip()`,
     the MAC address and the RX filter modes are re-applied, at most once
     per 5 s.
   - **`enable()`** restarts the TX path when the last send failed, so
     `ifconfig en0 down; up` is a real recovery step now.

   The STOP, flush, clear, ON sequence is this driver's own. Linux never
   restarts TX at runtime, and the register meanings are from
   `smsc95xx.h:51-78`. This is the least-verified part of the change.

## Testing it on the Pi (coordinator)

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
- PASS, and the log has `DAR-465 partial chunk ... OUT ... NAK` lines: the
  trigger was real and the fix handled it. Then repeat with the
  **release** kernel and the same script to confirm it wedges there.
- PASS with no partial-chunk lines: the trigger was not reproduced. Raise
  `MB`/`ITER`, or run two stress loops in parallel.
- A `DAR-465 TX recovery` line with `TXE 1`: confirms the device was in a TX
  error state. Whether `TX recovered after N failed send(s)` follows shows
  whether the restart is enough.
- Still wedging, with STALLs and no partial-chunk lines: the corruption
  comes from somewhere else. Look at the first failure code in the sampled
  `bulk OUT ... failed` line (0xe0004051 means suspect 2).

## Uncertain

- No hardware run yet; QEMU cannot test any of this (it has no LAN9514).
- That the LAN9514 NAKs mid-frame under load, and with an even packet
  count, is inferred, not observed.
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
