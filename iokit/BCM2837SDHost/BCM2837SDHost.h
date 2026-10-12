/*
 * Driver for the BCM2835-family "sdhost" SD controller (ARM physical
 * 0x3f202000, GPU IRQ 56; DT node "sdhost" in devicetrees/rpi3.dts), the
 * controller Linux and Circle use for the Pi 3's SD card slot.
 *
 * Why it exists (docs/wifi-plan.md): the on-board BCM43438 WiFi chip is
 * wired to GPIO34-39, which only the Arasan controller ("sdhci") can reach.
 * BCM2837SDHCI has driven the SD card from the Arasan controller since the
 * start of this port, so the card has to move to sdhost (GPIO48-53, ALT0)
 * before the Arasan controller can talk to the WiFi chip.
 *
 * It only runs with boot-arg `wifi=1`. Without it, probe() declines and the
 * SD card stays on BCM2837SDHCI exactly as before.
 *
 * Register semantics come from Linux drivers/mmc/host/bcm2835.c (mainline
 * sdhost driver): bcm2835_reset_internal() for reset/thresholds,
 * bcm2835_set_clock() for SDCDIV, bcm2835_send_command()/
 * bcm2835_finish_command() for SDCMD NEW/FAIL handling, and
 * bcm2835_transfer_block_pio()/bcm2835_wait_transfer_complete() for PIO and
 * the SDEDM state machine. The card protocol (CMD0/8/ACMD41/2/3/9/7/ACMD51/
 * ACMD6/16, CMD17/18/24/25/12) is the same one BCM2837SDHCI sends, copied
 * rather than shared so the proven root-disk driver is not touched.
 *
 * All waits are polled from the calling thread (no interrupt handler).
 * BCM2837SDHCI keeps an interrupt-plus-poll scheme; this driver does not,
 * because every wait here is short and only ever made from an IOKit thread
 * context, and polling keeps the first real-hardware run free of interrupt
 * routing questions. Long waits (card busy after a write) sleep with
 * IOSleep(1) between polls instead of spinning.
 *
 * Fallback contract with BCM2837SDHCI: start() always publishes the IOKit
 * resource "BCM2837SDCardHost" with value "sdhost" (card initialised here)
 * or "sdhci" (it was not; GPIO48-53 handed back to the Arasan controller).
 * BCM2837SDHCI, under wifi=1, waits for that resource to decide whether it
 * drives the SD card or the WiFi chip.
 */
#ifndef _BCM2837SDHOST_H
#define _BCM2837SDHOST_H

#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>

class BCM2837SDHost : public IOBlockStorageDevice
{
	OSDeclareDefaultStructors(BCM2837SDHost);

public:
	// Resource published by start(); see the header comment.
	static constexpr const char *kCardHostResource = "BCM2837SDCardHost";

private:
	IOMemoryMap *fRegMap;
	volatile void *fRegBase;
	IOLock *fTransferLock;     // serializes doAsyncReadWrite (same shape as BCM2837SDHCI)

	uint32_t fCoreClockHz;     // sdhost's input clock (VPU core clock, mailbox clock id 4)
	uint32_t fHcfg;            // shadow of SDHCFG
	uint32_t fCdiv;            // shadow of SDCDIV
	uint32_t fClockHz;         // actual SD clock programmed

	bool     fCardPresent;
	bool     fCardIsSDHC;
	bool     fBus4Bit;
	uint32_t fRCA;
	uint64_t fBlockCount;
	uint32_t fBlockSize;

	enum {
		kRegCmd   = 0x00,
		kRegArg   = 0x04,
		kRegTout  = 0x08,
		kRegCdiv  = 0x0c,
		kRegRsp0  = 0x10,
		kRegHsts  = 0x20,
		kRegVdd   = 0x30,
		kRegEdm   = 0x34,
		kRegHcfg  = 0x38,
		kRegHbct  = 0x3c,
		kRegData  = 0x40,
		kRegHblc  = 0x50,
	};
	enum {
		kCmdNew        = 0x8000,
		kCmdFail       = 0x4000,
		kCmdBusyWait   = 0x0800,
		kCmdNoResponse = 0x0400,
		kCmdLongResp   = 0x0200,
		kCmdWrite      = 0x0080,
		kCmdRead       = 0x0040,
		kCmdMask       = 0x003f,
	};
	enum {
		kHstsBusyIrpt   = 0x400,
		kHstsBlockIrpt  = 0x200,
		kHstsSdioIrpt   = 0x100,
		kHstsRewTimeout = 0x080,
		kHstsCmdTimeout = 0x040,
		kHstsCrc16      = 0x020,
		kHstsCrc7       = 0x010,
		kHstsFifoError  = 0x008,
		kHstsDataFlag   = 0x001,
		kHstsTransferErrorMask = kHstsCrc7 | kHstsCrc16 | kHstsRewTimeout | kHstsFifoError,
		kHstsErrorMask  = kHstsCmdTimeout | kHstsTransferErrorMask,
	};
	enum {
		kHcfgBusyIrptEn = 1U << 10,
		kHcfgBlockIrptEn = 1U << 8,
		kHcfgSdioIrptEn = 1U << 5,
		kHcfgDataIrptEn = 1U << 4,
		kHcfgSlowCard   = 1U << 3,
		kHcfgWideExtBus = 1U << 2,
		kHcfgWideIntBus = 1U << 1,
		kHcfgRelCmdLine = 1U << 0,
	};
	enum {
		kEdmForceDataMode = 1U << 19,
		kEdmReadThresholdShift = 14,
		kEdmWriteThresholdShift = 9,
		kEdmThresholdMask = 0x1f,
		kEdmFsmMask = 0xf,
		kFsmIdentMode = 0x0,
		kFsmDataMode = 0x1,
		kFsmReadData = 0x2,
		kFsmWriteData = 0x3,
		kFsmReadWait = 0x4,
		kFsmReadCrc = 0x5,
		kFsmWriteCrc = 0x6,
		kFsmWriteWait1 = 0x7,
		kFsmWriteStart1 = 0xa,
		kFsmWriteStart2 = 0xb,
		kFifoReadThreshold = 4,  // Linux FIFO_READ_THRESHOLD ("silicon bug")
		kFifoWriteThreshold = 4,
		kFifoWords = 16,
	};

	// Response type for sendCommand().
	enum {
		kRespNone = 0,
		kRespShort,       // R1/R6/R7
		kRespShortNoCrc,  // R3 (ACMD41): sdhost may flag CRC7, ignored like Linux does for CMD1
		kRespShortBusy,   // R1b
		kRespLong,        // R2
	};
	enum {
		kIdentClockHz = 400000,
		kDataClockHz = 25000000,
	};

	uint32_t readReg(uint32_t off) { return *(volatile uint32_t *)((uintptr_t)fRegBase + off); }
	void writeReg(uint32_t off, uint32_t v) { *(volatile uint32_t *)((uintptr_t)fRegBase + off) = v; }

	void pinmux(bool toSDHost);
	void resetHost(void);
	void setClock(uint32_t hz);
	bool waitCmdIdle(uint32_t timeoutUs, uint32_t *outCmd);
	bool waitFsmIdle(bool isRead);
	bool sendCommand(uint8_t idx, uint32_t arg, int respType, uint32_t resp[4],
	    uint32_t dataFlags = 0, uint32_t blockSize = 0, uint32_t blockCount = 0);
	bool pioTransfer(bool isRead, uint32_t *buf, uint32_t words);
	bool waitCardReady(uint32_t timeoutMs);
	bool widenBusTo4Bit(void);
	bool cardIdentify(void);
	bool readBlocks(uint64_t start, uint32_t n, void *buf);
	bool writeBlocks(uint64_t start, uint32_t n, const void *buf);
	void dumpRegs(const char *why);
	void publishVerdict(const char *host);

public:
	virtual IOService *probe(IOService *provider, SInt32 *score) APPLE_KEXT_OVERRIDE;
	virtual bool init(OSDictionary *properties = NULL) APPLE_KEXT_OVERRIDE;
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free(void) APPLE_KEXT_OVERRIDE;

	virtual IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks,
	    IOStorageAttributes *attributes, IOStorageCompletion *completion) APPLE_KEXT_OVERRIDE;
	virtual IOReturn doEjectMedia(void) APPLE_KEXT_OVERRIDE;
	virtual IOReturn doFormatMedia(UInt64 byteCapacity) APPLE_KEXT_OVERRIDE;
	virtual UInt32 doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const APPLE_KEXT_OVERRIDE;
	virtual char *getVendorString(void) APPLE_KEXT_OVERRIDE;
	virtual char *getProductString(void) APPLE_KEXT_OVERRIDE;
	virtual char *getRevisionString(void) APPLE_KEXT_OVERRIDE;
	virtual char *getAdditionalDeviceInfoString(void) APPLE_KEXT_OVERRIDE;
	virtual IOReturn reportBlockSize(UInt64 *blockSize) APPLE_KEXT_OVERRIDE;
	virtual IOReturn reportEjectability(bool *isEjectable) APPLE_KEXT_OVERRIDE;
	virtual IOReturn reportMaxValidBlock(UInt64 *maxBlock) APPLE_KEXT_OVERRIDE;
	virtual IOReturn reportMediaState(bool *mediaPresent, bool *changedState) APPLE_KEXT_OVERRIDE;
	virtual IOReturn reportRemovability(bool *isRemovable) APPLE_KEXT_OVERRIDE;
	virtual IOReturn reportWriteProtection(bool *isWriteProtected) APPLE_KEXT_OVERRIDE;
	virtual IOReturn doSynchronize(UInt64 block, UInt64 nblks,
	    IOStorageSynchronizeOptions options) APPLE_KEXT_OVERRIDE;
};

#endif /* _BCM2837SDHOST_H */
