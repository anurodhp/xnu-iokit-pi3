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
 * third_party/xnu/pexpert/pexpert/arm64/BCM2837.h. That has to happen
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
 * transfer busy-waits on BUFFER_READ_READY/BUFFER_WRITE_READY per block
 * inside doAsyncReadWrite rather than using DMA + an interrupt-driven
 * completion. This is a real, correct SDHCI transfer mode (just slow) and
 * avoids needing IOKit DMA plumbing (IODMACommand/IOMemoryCursor) to get
 * a first working driver - worth revisiting once basic reads work.
 *
 * UNVERIFIED / not yet done, flagged rather than guessed:
 *   - GPIO pinmux (see above) - reads/writes will simply time out against
 *     nothing until this exists.
 *   - Interrupt wiring: this class never calls registerInterrupt against
 *     its provider. Since transfers are PIO/polled (not interrupt-driven
 *     completion), this may be unnecessary for a first working version;
 *     card-insertion/removal interrupts are unimplemented.
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

class BCM2837SDHCI : public IOBlockStorageDevice
{
	OSDeclareDefaultStructors(BCM2837SDHCI);

private:
	IOMemoryMap *fRegMap;
	volatile void *fRegBase;

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
		kIntErrorMask    = 0xFFFF0000, // bits 16-31 are all error conditions
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
	bool     waitSet32(uint32_t offset, uint32_t mask, uint32_t timeoutUs);

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
