/*
 * Driver for the BCM2837 Arasan SDHCI controller (Linux: sdhci-iproc.c's
 * "brcm,bcm2835-sdhci" entry, `bcm2835_data`; DT node "sdhci" in
 * devicetrees/rpi3.dts). This is the *Arasan/eMMC-style* controller at VC
 * offset 0x300000, not the Broadcom-proprietary "sdhost" controller - see
 * devicetrees/rpi3.dts and project memory for why (sdhost has no public
 * register reference outside Linux's own driver; this one is close to
 * standard SDHCI). NOT YET DONE: nothing pin-muxes GPIO48-53 to ALT3 to
 * route this controller to the physical SD slot instead of the onboard
 * SDIO WiFi (GPIO34-39 by default) - see BCM2837_FSEL_* in
 * third_party/xnu-7195/pexpert/pexpert/arm64/BCM2837.h. That has to happen
 * (in this driver's start(), or earlier platform init) before any command
 * on this controller reaches a real SD card.
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
 *     being read from hardware.
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
 *   - GPIO pinmux (see above) - reads/writes will simply time out against
 *     nothing until this exists. Consequently the interrupt path above has
 *     never actually fired on real or emulated hardware yet either.
 *   - Card-insertion/removal interrupts (SDHCI_INT_CARD_INSERT/REMOVE) are
 *     unimplemented - reportMediaState() never reports a changed state.
 *   - Voltage/power sequencing (SDHCI_POWER_CONTROL) uses the standard
 *     3.3V value; BCM2837_BRINGUP-era pexpert may not have working power
 *     rail control for the SD slot at all yet (separate, deeper hardware
 *     question, not something this driver can resolve alone).
 */

#ifndef _BCM2837_SDHCI_H
#define _BCM2837_SDHCI_H

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

	// SD card state, populated by cardIdentify() during start().
	bool     fCardPresent;
	bool     fCardIsSDHC;     // block-addressed (SDHC/SDXC) vs byte-addressed (SDSC)
	uint32_t fRCA;            // relative card address, from CMD3
	uint64_t fBlockCount;     // total addressable 512-byte blocks
	uint32_t fBlockSize;      // always 512 for the block-storage-visible geometry

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
		kCmdRespShort     = 0x02, // R1/R3/R6/R7 (48-bit)
		kCmdRespShortBusy = 0x03, // R1b
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

	bool     resetHost(void);
	bool     setClock(uint32_t targetHz);
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
};

#endif /* _BCM2837_SDHCI_H */
