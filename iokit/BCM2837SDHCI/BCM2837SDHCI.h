/*
 * Driver for the BCM2837 Arasan SDHCI controller (Linux: sdhci-iproc.c's
 * "brcm,bcm2835-sdhci" entry, `bcm2835_data`; DT node "sdhci" in
 * devicetrees/rpi3.dts). This is the *Arasan/eMMC-style* controller at VC
 * offset 0x300000, not the Broadcom-proprietary "sdhost" controller - see
 * devicetrees/rpi3.dts and project memory for why (sdhost has no public
 * register reference outside Linux's own driver; this one is close to
 * standard SDHCI). GPIO48-53 pinmux to ALT3 (routing this controller to
 * the physical SD slot) AND GPIO34-39 pinmux to input (explicitly
 * disconnecting the onboard SDIO WiFi from this same controller - the
 * firmware's power-on default leaves it at ALT3 too, which would otherwise
 * contend on the shared bus with the physical slot; found+fixed
 * 2026-08-13, see pinmuxSDSlot()'s own comment) is done in start() via
 * pinmuxSDSlot() - real-hardware correctness only, confirmed NOT required
 * for QEMU testing (see that method's own comment for why).
 *
 * BCM2835-specific quirks confirmed from Linux's sdhci-iproc.c
 * (bcm2835_data / sdhci_bcm2835_pltfm_data), baked into this driver's
 * design rather than left as TODOs:
 *   - SDHCI_QUIRK_BROKEN_CARD_DETECTION: no usable card-detect line: this
 *     driver always assumes media is present rather than polling
 *     PRESENT_STATE's CARD_PRESENT bit.
 *   - missing_caps=true: the CAPABILITIES register (0x40) can't be
 *     trusted on this SoC; capabilities below are hardcoded to a
 *     conservative default-speed / 3.3V / no-DMA profile instead of
 *     being read from hardware. That includes its base-clock field,
 *     which is why the real base clock is asked of the VideoCore
 *     firmware over the mailbox property channel instead
 *     (BCM2837Mailbox::getClockRateHz(kClockIdEMMC), DAR-103) - the same
 *     source Linux uses on this SoC, where the sdhci DT node's
 *     `clocks = <&clocks BCM2835_CLOCK_EMMC>` provider is itself a
 *     mailbox client. HOST_VERSION (0xFE) *is* read from hardware, since
 *     it selects which of the two SDHCI clock-divider encodings applies
 *     and guessing that wrong programs a completely different rate. See
 *     setClock()/calcDivider().
 *   - SDHCI_QUIRK_NO_HISPD_BIT / SDHCI_QUIRK2_PRESET_VALUE_BROKEN: no
 *     high-speed or UHS preset support attempted - default-speed
 *     (~25MHz/CMD-mode) clock only.
 *
 * Deliberately scoped to PIO (programmed I/O), not SDMA/ADMA: each block
 * transfer's actual data movement is a per-block register copy loop inside
 * readBlocksPIO/writeBlocksPIO, not a DMA engine. This is a real, correct
 * SDHCI transfer mode (just slower than DMA) and avoids needing IOKit DMA
 * plumbing (IODMACommand/IOMemoryCursor) to get a first working driver -
 * worth revisiting once basic reads work. Waiting for each block/command to
 * be ready is interrupt-driven, though (see below) - only the copy itself
 * is PIO.
 *
 * Command completion and buffer-ready waits are interrupt-driven:
 * start() calls registerInterrupt()/enableInterrupt() against the provider
 * (the "sdhci" DT node's interrupt-parent, BCM2837InterruptController),
 * and interruptOccurred() is a primary (hard-IRQ-equivalent) handler
 * mirroring Linux's sdhci_irq() (drivers/mmc/host/sdhci.c) - read
 * INT_STATUS, ack (write-1-to-clear) exactly the bits understood, stash
 * them, wake any waiter. See waitForIntStatus32()'s implementation comment
 * for why this needs an IOSimpleLock *and* an IOLock rather than just one.
 * PIO data movement itself (the actual per-block register copy loop) is
 * still unchanged - only the "wait for the next block/command to be ready"
 * step now blocks on the interrupt instead of busy-polling the register.
 *
 * UNVERIFIED / not yet done, flagged rather than guessed:
 *   - GPIO pinmux is done (see above, pinmuxSDSlot()) but only ever
 *     exercised in QEMU (where it's a no-op either way, confirmed) - not
 *     yet verified against real hardware.
 *   - Real ARM64 IRQ delivery through this board's interrupt cascade
 *     (BCM2837CPU/IOCPUInterruptController -> BCM2837LocalIntc ->
 *     BCM2837InterruptController) to THIS driver is now CONFIRMED AND
 *     REAL (2026-08-13), for real this time - full story, since this exact
 *     comment was wrong twice in one day before landing here:
 *     (1) An earlier version of fRealInterruptHits/fPollFallbackHits
 *     credited a hit to "genuine interrupt" at the *consuming* side rather
 *     than the true source, producing a false-positive "confirmed" claim.
 *     Fixed the counters to credit each source unconditionally at the true
 *     point of observation (interruptOccurred() vs the poll block in
 *     waitForIntStatus32()).
 *     (2) Re-measured correctly, the honest result was fRealInterruptHits
 *     staying at 0 for an entire boot - real interrupt delivery to this
 *     driver had genuinely never been observed, root cause unknown.
 *     (3) Root cause found by tracing the actual DT interrupt-cell path
 *     through real xnu source (IODeviceTreeSupport.cpp's
 *     IODTMapOneInterrupt() + IOInterruptController.cpp's
 *     registerInterrupt()) rather than assuming: devicetrees/rpi3.dts's
 *     "intc" node used a 2-cell {bank, bit} interrupt encoding
 *     (`#interrupt-cells = <2>`, sdhci's `interrupts = <2 30>`), but
 *     IODTMapOneInterrupt() stores a device's `interrupts` property
 *     completely unprocessed, and the base IOInterruptController::
 *     registerInterrupt() just takes the FIRST 32-bit cell as the vector
 *     number - nothing decodes the 2-cell {bank,bit} convention unless the
 *     controller subclass explicitly overrides registerInterrupt() to do
 *     so (BCM2837InterruptController never did - see that class's own
 *     header, which had already flagged this exact gap as unverified).
 *     So this driver's real registered vector was **2** (the bare first
 *     cell), not 62 (bank 2 bit 30, this project's own intended
 *     bank*32+bit numbering) - nowhere near where the real hardware
 *     interrupt actually lands, so it was silently dropped every time.
 *     Independently corroborated the same day by a from-scratch
 *     adversarial review of the interrupt controllers, which reached the
 *     identical root cause by pure static reading, without seeing this
 *     investigation.
 *     Fixed by making "intc" single-cell (`#interrupt-cells = <1>`) and
 *     pre-flattening sdhci's `interrupts` to `<62>` directly (see
 *     devicetrees/rpi3.dts's own header comment for the full trace) -
 *     matches what "local-intc"'s own single-cell `interrupts = <8>`
 *     already did correctly, without needing to override
 *     IOInterruptController::registerInterrupt() at all.
 *     **Re-verified on a real QEMU boot with the DT fix in place:
 *     fRealInterruptHits fires, and fPollFallbackHits does NOT appear at
 *     all for the whole boot** - real interrupt-driven I/O, not polling,
 *     for the first time. The hybrid poll+interrupt design in
 *     waitForIntStatus32() is kept regardless (real interrupts still win
 *     immediately via IOLockWakeup() when they fire; the poll tick is now
 *     just a safety net, not the only mechanism that was ever working).
 *   - Card-insertion/removal interrupts (SDHCI_INT_CARD_INSERT/REMOVE) are
 *     unimplemented - reportMediaState() never reports a changed state.
 *   - **Bus width widening (DAR-218): DONE.** widenBusTo4Bit(), called
 *     from cardIdentify() after CMD7 (SELECT_CARD), reads the card's SCR
 *     via a real ACMD51 data transfer, checks SD_BUS_WIDTHS (bits
 *     [51:48]) for 4-bit DAT[3:0] support, negotiates via a real ACMD6
 *     (SET_BUS_WIDTH), and only on the card's own success flips
 *     kRegHostControl's 4-bit bit - never a host-side-only write, which
 *     would leave the two ends disagreeing (data corruption, not a clean
 *     failure). QEMU-verified: its sd_set_scr() (hw/sd/sd.c) advertises
 *     4-bit support and this driver's decode of it matches.
 *   - High speed (>25MHz, CMD6 function switch) is likewise not
 *     attempted; the clock stops at SD default speed (25MHz).
 *   - Voltage/power sequencing (SDHCI_POWER_CONTROL) uses the standard
 *     3.3V value; BCM2837_BRINGUP-era pexpert may not have working power
 *     rail control for the SD slot at all yet (separate, deeper hardware
 *     question, not something this driver can resolve alone).
 */

#ifndef _BCM2837_SDHCI_H
#define _BCM2837_SDHCI_H

#include "BCM2837SDHCIDivider.h"

#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>

class BCM2837SDHCI : public IOBlockStorageDevice
{
	OSDeclareDefaultStructors(BCM2837SDHCI);

private:
	IOMemoryMap *fRegMap;
	volatile void *fRegBase;

	// Interrupt-driven completion state (see waitForIntStatus32()).
	IOLock       *fInterruptLock;   // block/wake coordination only - never touched from interrupt context
	IOSimpleLock *fStatusLock;      // protects fPendingIntStatus between interruptOccurred() and waiters
	volatile uint32_t fPendingIntStatus; // INT_STATUS bits acked but not yet consumed by a waitFor call

	// Serializes doAsyncReadWrite() end-to-end (command issue through
	// completion) against itself. Backlog P0#1: two concurrent I/O
	// requests (now plausible with two mounted partitions, disk0s1 +
	// disk0s2) previously had nothing stopping their register writes
	// from interleaving and both consuming from the same
	// fPendingIntStatus, so one request could eat another's completion
	// bits - silent corruption or a hang. A real IOCommandGate/workloop
	// would be the fuller architectural answer (the backlog's original
	// ask), but this driver's doAsyncReadWrite() is already synchronous
	// under the hood (it blocks the calling thread on
	// waitForIntStatus32() rather than completing from interrupt
	// context - see that function's own comment), so a plain mutex held
	// for the whole command-issue-through-completion span is a real,
	// sufficient critical section for this specific driver shape: it
	// guarantees only one thread is ever touching the command/transfer
	// registers or fPendingIntStatus's *producer* side (waitForIntStatus32's
	// poll-fallback path also touches it) at a time. IOLockLock may block,
	// which is fine - doAsyncReadWrite() is called from an ordinary
	// (non-interrupt) IOKit context, same as it already blocks inside
	// waitForIntStatus32() today.
	IOLock       *fTransferLock;

	// Diagnostic-turned-permanent counters (see waitForIntStatus32() and
	// interruptOccurred()). fRealInterruptHits is incremented ONLY inside
	// interruptOccurred() itself, fPollFallbackHits ONLY inside this
	// function's own direct-register-read poll block - each credited
	// unconditionally at its true source, not at whichever wait call later
	// happens to consume the bits (an earlier version counted at the
	// consuming side and was wrong - see both increment sites' comments).
	// This is what let Phase 3's "is real interrupt delivery reaching this
	// driver" question actually get answered from a real boot log instead
	// of guessed at.
	uint32_t fRealInterruptHits;
	uint32_t fPollFallbackHits;
	bool     fLoggedRealInterrupt; // one-shot latches for waitForIntStatus32()'s summary IOLog
	bool     fLoggedPollFallback;

	// SD card state, populated by cardIdentify() during start().
	bool     fCardPresent;
	bool     fCardIsSDHC;     // block-addressed (SDHC/SDXC) vs byte-addressed (SDSC)
	uint32_t fRCA;            // relative card address, from CMD3
	uint64_t fBlockCount;     // total addressable 512-byte blocks
	uint32_t fBlockSize;      // always 512 for the block-storage-visible geometry

	// Clock state (DAR-103). fBaseClockHz is the controller's real base
	// clock as reported by the VideoCore firmware over the mailbox
	// property channel (BCM2837Mailbox::getClockRateHz(kClockIdEMMC)) -
	// NOT read from CAPABILITIES, whose base-clock field is explicitly
	// untrustworthy on this SoC (missing_caps, see header). Zero means
	// "the firmware did not tell us", which is a real, expected case
	// (e.g. a board/firmware that doesn't answer the tag) and makes
	// setClock() take its always-conservative fallback path instead of
	// computing a divisor from a value it does not have.
	// fSpecVersion is HOST_VERSION's spec field, which selects the
	// divider encoding. fCurrentClockHz is the rate setClock() last
	// actually programmed (0 while on the fallback path, where the true
	// rate is unknown by construction).
	uint32_t fBaseClockHz;
	uint16_t fSpecVersion;
	uint32_t fCurrentClockHz;

	// Register offsets (drivers/mmc/host/sdhci.h in Linux). Access widths
	// (8/16/32-bit) match what Linux's sdhci_readb/w/l use for each -
	// getting these wrong is a classic source of adjacent-register
	// corruption on real SDHCI hardware.
	enum {
		kRegArgument2       = 0x00, // 32-bit
		kRegBlockSize       = 0x04, // 16-bit
		kRegBlockCount      = 0x06, // 16-bit
		kRegArgument        = 0x08, // 32-bit
		kRegTransferMode    = 0x0C, // 16-bit
		kRegCommand         = 0x0E, // 16-bit
		kRegResponse0       = 0x10, // 32-bit (+0x10,0x14,0x18,0x1C for R2's 128 bits)
		kRegBuffer          = 0x20, // 32-bit, PIO data port
		kRegPresentState    = 0x24, // 32-bit
		kRegHostControl     = 0x28, // 8-bit
		kRegPowerControl    = 0x29, // 8-bit
		kRegClockControl    = 0x2C, // 16-bit
		kRegTimeoutControl  = 0x2E, // 8-bit
		kRegSoftwareReset   = 0x2F, // 8-bit
		kRegIntStatus       = 0x30, // 32-bit
		kRegIntEnable       = 0x34, // 32-bit
		kRegSignalEnable    = 0x38, // 32-bit
		kRegCapabilities    = 0x40, // 32-bit (untrustworthy on this SoC - missing_caps)
		kRegHostVersion     = 0xFE, // 16-bit ([7:0] spec version, [15:8] vendor)
	};

	// HOST_VERSION's spec-version field values live in
	// BCM2837SDHCIDivider.h (kBCM2837SDHCISpecVer100/200/300), next to
	// the divider math they select between.

	// The two SD clock rates this driver ever asks for.
	enum {
		// SD Physical Layer spec, card identification mode: 100-400kHz.
		// setClock() always lands at or below the target, and the divider
		// step sizes involved keep it well above 100kHz for any plausible
		// base clock, so both ends of that range are respected.
		kIdentificationClockHz = 400000,

		// SD default speed: up to 25MHz, which every card supports after
		// identification with no CMD6 function switch and no bus-width
		// change. High speed (50MHz) is deliberately NOT attempted - it
		// needs a real CMD6 negotiation this driver does not implement,
		// and this SoC carries SDHCI_QUIRK_NO_HISPD_BIT anyway.
		kDefaultSpeedClockHz = 25000000,
	};

	// SDHCI_HOST_CONTROL bits we actually use (drivers/mmc/host/sdhci.h).
	enum {
		kHostControl4BitBus = 1U << 1, // SDHCI_CTRL_4BITBUS
	};

	// SDHCI_PRESENT_STATE bits we actually use.
	enum {
		kPresentCmdInhibit  = 1U << 0,
		kPresentDatInhibit  = 1U << 1,
		kPresentBufWriteRdy = 1U << 10,
		kPresentBufReadRdy  = 1U << 11,
	};

	// SDHCI_INT_STATUS / SDHCI_INT_ENABLE bits we actually use.
	enum {
		kIntCmdComplete  = 1U << 0,
		kIntTransferDone = 1U << 1,
		kIntBufWriteRdy  = 1U << 4,
		kIntBufReadRdy   = 1U << 5,
		// Linux's SDHCI_INT_ERROR_MASK (drivers/mmc/host/sdhci.h) is
		// 0xFFFF8000, not 0xFFFF0000 - bit 15 (SDHCI_INT_ERROR) is the
		// generic "an error occurred" summary bit, separate from the
		// specific bits 16-31. Missing it here would have meant a real
		// error could set only bit 15 and never be noticed by any of the
		// waitForIntStatus32(..., kIntErrorMask, ...) checks below.
		kIntErrorMask    = 0xFFFF8000,
	};

	// SDHCI_TRANSFER_MODE bits we actually use.
	enum {
		kCmdTransBlkCntEn = 1U << 1,
		kCmdTransRead     = 1U << 4,
		kCmdTransMulti    = 1U << 5,
	};

	// SDHCI_COMMAND response-type field (low 2 bits) + flags.
	enum {
		kCmdRespNone      = 0x00,
		kCmdRespLong      = 0x01, // R2 (136-bit)
		kCmdRespShort     = 0x02, // R1/R6/R7 (48-bit, real CRC7 + command-index echo)
		kCmdRespShortBusy = 0x03, // R1b
		// R3/R4 (ACMD41's OCR response is R3): also a 48-bit short response
		// (low 2 bits below still decode to kCmdRespShort's 0x02 once
		// sendCommand() masks this down to the hardware field), but the SD
		// spec defines R3/R4 with NO real CRC7 (bits [39:8] are the OCR;
		// bits[7:1] are reserved, read as all 1s, not a computed CRC) and no
		// command-index echo - unlike this enum's own previous comment
		// implied by lumping R3 in with R1/R6/R7. Bit 6 here is a
		// software-only marker (never written to hardware; sendCommand()'s
		// flags computation masks to the low 2 bits first) so this can't be
		// confused with kCmdRespShort by the CRC/index-enable decision.
		// Getting this wrong makes real hardware flag CMD_CRC_ERR/
		// CMD_INDEX_ERR on every ACMD41 (QEMU's SDHCI model doesn't check
		// either, so this was invisible in emulation) - see project memory/
		// docs/roadmap.md for the adversarial-review finding this fixes.
		kCmdRespShortNoChecks = 0x02 | (1U << 6),
		kCmdCRCCheckEn    = 1U << 3,
		kCmdIndexCheckEn  = 1U << 4,
		kCmdDataPresent   = 1U << 5,
	};

	uint8_t  readReg8(uint32_t offset);
	uint16_t readReg16(uint32_t offset);
	uint32_t readReg32(uint32_t offset);
	void     writeReg8(uint32_t offset, uint8_t value);
	void     writeReg16(uint32_t offset, uint16_t value);
	void     writeReg32(uint32_t offset, uint32_t value);

	bool     waitClear32(uint32_t offset, uint32_t mask, uint32_t timeoutUs);

	// Generic busy-poll for callers whose condition isn't a plain
	// register/mask check (e.g. checking a bit is *set* rather than
	// clear). Same IODelay(50)-based loop as waitClear32.
	bool     pollUntil(bool (^predicate)(void), uint32_t timeoutUs);

	// Primary (hard-IRQ-equivalent) interrupt handler, installed via
	// registerInterrupt() in start(). Must match IOInterruptAction exactly
	// (IOKit/IOService.h) - not a normal virtual method, since registerInterrupt
	// takes a plain function pointer, not a bound C++ method.
	static void interruptOccurred(OSObject *target, void *refCon, IOService *nub, int source);

	// Blocks the calling thread until INT_STATUS bits matching `mask` have
	// been observed (and acked) by interruptOccurred(), or `timeoutUs`
	// elapses. On success, returns true and clears the matched bits from
	// fPendingIntStatus (leaving any other pending bits, e.g. an error bit
	// a different waiter still needs, untouched); *outStatus (if non-NULL)
	// gets the matched bits. Replaces a waitSet32(kRegIntStatus, ...) busy
	// loop with an actual sleep, same role as Linux's
	// wait_for_completion(&host->cmd_done) after sdhci_send_command().
	bool     waitForIntStatus32(uint32_t mask, uint32_t timeoutUs, uint32_t *outStatus);

	// Issues one SD command, waits for completion, and reads the response
	// into resp[0..3] (only resp[0] valid unless respType==kCmdRespLong).
	// Returns false on any error/timeout status bit or CMD_INHIBIT stuck.
	bool     sendCommand(uint8_t cmdIndex, uint32_t arg, uint32_t respType,
	                      bool hasData, uint32_t resp[4]);

	// Pin-muxes GPIO48-53 to ALT3 ("emmc_gpio48" in Linux DT), routing
	// this controller to the physical SD slot instead of leaving it on
	// whatever the stock-firmware default is (see header). Maps the GPIO
	// controller's FSEL4/FSEL5 registers directly by physical address
	// (0x3f200000 + arm-io's translation - independent of whether an
	// IOKit "gpio" driver/nub exists at all) rather than going through
	// this driver's own provider, since GPIO isn't this driver's parent.
	// Real-hardware correctness only - confirmed NOT required for QEMU
	// testing (third_party/qemu-zhuowei's hw/gpio/bcm2835_gpio.c parents
	// the emulated SD card onto the real SDHCI bus by default at reset,
	// with or without this).
	bool     pinmuxSDSlot(void);

	bool     resetHost(void);

	// SDHCI-spec error recovery (backlog P0#2): previously every error
	// path here was just IOLog()+return false, leaving CMD_INHIBIT/
	// DAT_INHIBIT asserted forever after the first real CRC error,
	// timeout, or card removal - the classic real-hardware bring-up
	// cliff QEMU's SD model never reproduces (it doesn't model these
	// errors, so a reset was never needed for a QEMU boot to keep
	// working). resetCmdLine() issues SOFTWARE_RESET_CMD (SDHCI spec
	// 3.7.1, kRegSoftwareReset bit1) and polls for it to self-clear;
	// resetDatLine() does the same for SOFTWARE_RESET_DAT (bit2) and
	// additionally issues CMD12 (STOP_TRANSMISSION) to formally abort
	// an in-flight data transfer, matching Linux's sdhci_end_tx() /
	// sdhci_error_recovery() split between a plain command-line reset
	// and a full data-abort. Called from sendCommand()'s own timeout/
	// error paths and from readBlocksPIO()/writeBlocksPIO()'s data-phase
	// error paths so a subsequent command always starts from a clean
	// controller state instead of dying on a stuck INHIBIT bit.
	void     resetCmdLine(void);
	void     resetDatLine(void);

	// Real ACMD51 (SEND_SCR) + ACMD6 (SET_BUS_WIDTH) negotiation (DAR-218):
	// reads the card's SCR to confirm 4-bit DAT[3:0] support, tells the
	// card to switch via ACMD6, and only on success flips
	// kRegHostControl's 4-bit bit. Returns false (not an error - just
	// "stayed at 1-bit") if the card doesn't advertise 4-bit support, or
	// a real failure if any command in the sequence fails. Called once
	// from cardIdentify(), after CMD7 (SELECT_CARD) and before CMD16
	// (SET_BLOCKLEN) restores kRegBlockSize to the real 512-byte
	// geometry this function borrows for its own 8-byte SCR read.
	bool     widenBusTo4Bit(void);

	// Programs the SD clock to the highest rate that is <= targetHz and
	// that this controller's divider can actually express. Requires
	// fBaseClockHz/fSpecVersion to have been established first
	// (readClockConfig()); without a real base clock it falls back to the
	// pre-DAR-103 behaviour of writing the largest 8-bit divisor value,
	// which is slow but never faster than intended. Never programs a rate
	// above targetHz - if no divisor can achieve that, it takes the
	// fallback rather than the closest-anyway value.
	bool     setClock(uint32_t targetHz);

	// Pure function: given a real base clock, a target rate, and the
	// controller's SDHCI spec version, produces the CLOCK_CONTROL divider
	// field for the fastest expressible rate <= targetHz, or false if no
	// divider can get down to targetHz. Static and side-effect-free so it
	// can be reasoned about (and, if it ever matters, unit-tested) in
	// isolation from any hardware - see its own comment in the .cpp for
	// the two divider encodings and why the choice between them is
	// load-bearing.
	static bool calcDivider(uint32_t baseHz, uint32_t targetHz,
	                         uint16_t specVersion, uint16_t *outClkField,
	                         uint32_t *outActualHz);

	// Reads HOST_VERSION and asks the VideoCore firmware for the EMMC
	// base clock, once, at start(). Both are inputs to setClock()'s
	// divisor math; both failing is survivable (setClock() falls back).
	void     readClockConfig(void);

	// Post-identification clock raise (DAR-103). Runs once at start()
	// after cardIdentify(), and only if a real base clock is known.
	// Reads a reference block at the identification clock, raises the
	// clock to default-speed, then re-reads the same block and compares -
	// a real end-to-end data-path check at the new rate, on whatever
	// board this is actually running on, rather than trusting the divisor
	// math alone. Any failure or mismatch reverts to the identification
	// clock and leaves it there.
	bool     tryRaiseDataPhaseClock(void);
	bool     cardIdentify(void); // CMD0/8/ACMD41/CMD2/CMD3/CMD7/CMD9-derived geometry, CMD16 block length
	bool     readBlocksPIO(uint64_t startBlock, uint32_t nblks, void *buf);
	bool     writeBlocksPIO(uint64_t startBlock, uint32_t nblks, const void *buf);

public:
	virtual bool   init(OSDictionary *properties) APPLE_KEXT_OVERRIDE;

	// Defensive re-check that provider is actually named "sdhci" - see
	// iokit/Drivers/BCM2837/BCM2837ArmIO.h (xnu-7195 tree) for why this is
	// needed (IOService::matchInternal()'s generic IONameMatch handling
	// was empirically observed matching a sibling personality against
	// every node under "arm-io", not just its own; this class inherits
	// IOBlockStorageDevice/IOService::probe(), not IODTPlatformExpert's
	// own defensive compareNames() re-check).
	virtual IOService *probe(IOService *provider, SInt32 *score) APPLE_KEXT_OVERRIDE;

	virtual bool   start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void   stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void   free(void) APPLE_KEXT_OVERRIDE;

	/* IOBlockStorageDevice pure-virtual methods (all 13; signatures
	 * verified against the real header, third_party/IOStorageFamily's
	 * IOBlockStorageDevice.h - see project memory for why that header
	 * isn't part of the base xnu source tree and had to be pulled from
	 * apple-oss-distributions separately). */
	virtual IOReturn doEjectMedia(void) APPLE_KEXT_OVERRIDE;
	virtual IOReturn doFormatMedia(UInt64 byteCapacity) APPLE_KEXT_OVERRIDE;
	virtual UInt32   doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const APPLE_KEXT_OVERRIDE;
	virtual char    *getVendorString(void) APPLE_KEXT_OVERRIDE;
	virtual char    *getProductString(void) APPLE_KEXT_OVERRIDE;
	virtual char    *getRevisionString(void) APPLE_KEXT_OVERRIDE;
	virtual char    *getAdditionalDeviceInfoString(void) APPLE_KEXT_OVERRIDE;
	virtual IOReturn reportBlockSize(UInt64 *blockSize) APPLE_KEXT_OVERRIDE;
	virtual IOReturn reportEjectability(bool *isEjectable) APPLE_KEXT_OVERRIDE;
	virtual IOReturn reportMaxValidBlock(UInt64 *maxBlock) APPLE_KEXT_OVERRIDE;
	virtual IOReturn reportMediaState(bool *mediaPresent, bool *changedState = 0) APPLE_KEXT_OVERRIDE;
	virtual IOReturn reportRemovability(bool *isRemovable) APPLE_KEXT_OVERRIDE;
	virtual IOReturn reportWriteProtection(bool *isWriteProtected) APPLE_KEXT_OVERRIDE;
	virtual IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks,
	                                   IOStorageAttributes *attributes,
	                                   IOStorageCompletion *completion) APPLE_KEXT_OVERRIDE;

	// Not one of the 13 pure-virtuals above - IOBlockStorageDevice::doSynchronize()
	// has a real default body (see IOBlockStorageDevice.cpp) that falls back to
	// the deprecated doSynchronizeCache(), whose own default returns
	// kIOReturnUnsupported. Left un-overridden, every HFS journal transaction
	// commit's DKIOCSYNCHRONIZE (bsd/hfs/hfs_journal.c write_journal_header())
	// hit that default and logged "jnl: ... flushing fs disk buffer returned
	// 0x2d" (ENOTSUP) on every commit. See doSynchronize()'s own comment in the
	// .cpp for why returning success here is the real fix, not a suppression.
	virtual IOReturn doSynchronize(UInt64 block, UInt64 nblks,
	                                IOStorageSynchronizeOptions options = 0) APPLE_KEXT_OVERRIDE;
};

#endif /* _BCM2837_SDHCI_H */
