/*
 * Driver for the BCM2835/BCM2837 DMA controller (DT node "dma" in
 * devicetrees/rpi3.dts; Linux: drivers/dma/bcm2835-dma.c, "brcm,bcm2835-dma").
 *
 * DARW-313. A small service for kernel clients (the PWM audio engine,
 * DARW-315): it hands out channels, starts a control-block chain and reports
 * per-channel completion interrupts. It is not a general dmaengine.
 *
 * Facts and where they come from:
 *   - Register layout (CS 0x00, CONBLK_AD 0x04, TI 0x08, SOURCE_AD 0x0c,
 *     DEST_AD 0x10, TXFR_LEN 0x14, STRIDE 0x18, NEXTCONBK 0x1c, DEBUG 0x20;
 *     INT_STATUS 0xfe0, ENABLE 0xff0; channel n at 0x100 * n; 32-byte,
 *     32-byte-aligned control blocks): BCM2835 ARM Peripherals, chapter 4,
 *     and Linux bcm2835-dma.c (BCM2835_DMA_* / struct bcm2835_dma_cb).
 *     third_party/qemu-zhuowei/hw/dma/bcm2835_dma.c implements the same
 *     layout, which is what QEMU runs.
 *   - Interrupts: u-boot's bcm283x.dtsi dma@7e007000 (third_party/u-boot/
 *     arch/arm/dts/bcm283x.dtsi:76) gives channels 0-10 their own GPU IRQ
 *     16..26 (bank 1, so BCM2837InterruptController vector 16 + channel) and
 *     channels 11-14 one shared IRQ 27. Only the dedicated ones are used.
 *   - Channels the ARM may use: brcm,dma-channel-mask = <0x7f35> (the
 *     firmware owns the others), third_party/u-boot/arch/arm/dts/
 *     .bcm2836-rpi-2-b.dtb.dts.tmp:136. Intersected with channels 0-10.
 *   - Addresses are VideoCore bus addresses: RAM through the uncached alias
 *     0xc0000000 (as BCM2837DWC2 and BCM2837Mailbox do), peripherals at
 *     0x7e000000 + offset.
 */
#ifndef BCM2837DMA_H
#define BCM2837DMA_H

#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

#define kBCM2837DMABusAlias   0xc0000000ULL
#define kBCM2837DMAPeriphBus  0x7e000000ULL

// CS register
enum {
	kDMACsActive      = 1U << 0,
	kDMACsEnd         = 1U << 1,
	kDMACsInt         = 1U << 2,
	kDMACsDreq        = 1U << 3,
	kDMACsPaused      = 1U << 4,
	kDMACsError       = 1U << 8,
	kDMACsWaitOutstanding = 1U << 28,
	kDMACsAbort       = 1U << 30,
	kDMACsReset       = 1U << 31,
};

// TI register
enum {
	kDMATiIntEn     = 1U << 0,
	kDMATiWaitResp  = 1U << 3,
	kDMATiDestInc   = 1U << 4,
	kDMATiDestDreq  = 1U << 6,
	kDMATiSrcInc    = 1U << 8,
	kDMATiSrcDreq   = 1U << 10,
};
#define kDMATiPerMap(x)  (((uint32_t)(x) & 0x1f) << 16)
#define kDMATiBurst(x)   (((uint32_t)(x) & 0xf) << 12)

// Hardware control block: 32 bytes, 32-byte aligned, bus addresses.
struct BCM2837DMAControlBlock {
	uint32_t ti;
	uint32_t srcBus;
	uint32_t dstBus;
	uint32_t length;
	uint32_t stride;
	uint32_t nextBus;
	uint32_t reserved[2];
} __attribute__((aligned(32)));

// Completion callback, run from the channel's primary interrupt handler:
// keep it short, no locks that sleep, no IOLog. `cs` is CS as read before
// the interrupt was acknowledged.
typedef void (*BCM2837DMACallback)(void *ref, uint32_t channel, uint32_t cs);

class BCM2837DMA : public IOService
{
	OSDeclareDefaultStructors(BCM2837DMA)

public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;

	// Takes a free, ARM-usable channel with its own interrupt. The callback
	// fires on every channel interrupt until freeChannel().
	// needNormalChannel: only channels 0-6 (full DMA; 7-14 are DMA-Lite with a
	// 16-bit length and no 2D mode).
	IOReturn allocateChannel(uint32_t *channelOut, BCM2837DMACallback cb, void *ref,
	    bool needNormalChannel = false);
	// Stops the channel, waits for a running callback to return, then releases it.
	void     freeChannel(uint32_t channel);

	// Resets the channel, points it at a control-block chain (bus address of
	// the first block, in the 0xc0000000 alias, 32-byte aligned) and sets
	// ACTIVE. `csFlags` may carry only the priority fields and
	// WAIT_FOR_OUTSTANDING_WRITES. The channel must be allocated.
	IOReturn startChannel(uint32_t channel, uint32_t firstCbBus, uint32_t csFlags = 0);
	// Abort + reset; safe on an idle channel.
	void     stopChannel(uint32_t channel);

	uint32_t readChannelReg(uint32_t channel, uint32_t offset);

	// Physically contiguous, low-1GB, write-combine (Normal Non-Cacheable)
	// memory the controller can read and write without cache maintenance;
	// same recipe as BCM2837DWC2::allocateDMABuffer(). align >= 32 for control
	// blocks. Release with bmd->complete(); bmd->release().
	static IOBufferMemoryDescriptor *allocateBuffer(uint32_t size, uint32_t align,
	    void **virtOut, uint32_t *busOut);

private:
	static void interruptOccurred(OSObject *target, void *refCon, IOService *nub, int source);
	void handleChannelInterrupt(uint32_t channel);
	volatile uint32_t *chan(uint32_t channel) const;
	void selfTest();
	void unwind();

	IOService               *fProvider;
	IOMemoryMap             *fRegMap;
	volatile uint8_t        *fRegs;
	IOLock                  *fAllocLock;
	IOSimpleLock            *fCbLock;          // guards fCallback/fCallbackRef/fInFlight
	uint32_t                 fFreeMask;        // channels available to allocate
	uint32_t                 fAllocatedMask;   // channels handed out
	volatile uint32_t        fInFlight[16];    // callbacks currently running
	volatile uint32_t        fErrorCount[16];  // channel ERROR seen in the interrupt
	uint32_t                 fIrqMask;         // channels with a registered interrupt
	BCM2837DMACallback       fCallback[16];
	void                    *fCallbackRef[16];
	volatile uint32_t        fIrqCount[16];
};

#endif
