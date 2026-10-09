#include "BCM2837DMA.h"

#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>

#define super IOService
OSDefineMetaClassAndStructors(BCM2837DMA, IOService)

enum {
	kRegCs        = 0x00 / 4,
	kRegConblkAd  = 0x04 / 4,
	kRegDebug     = 0x20 / 4,
	kChanStride   = 0x100,
	kRegIntStatus = 0xfe0,
	kRegEnable    = 0xff0,

	kMaxIrqChannels = 11,       // channels 0-10 have a dedicated IRQ
	kNormalChannels = 7,        // 0-6 are full channels, 7-14 DMA-Lite
	kCsDisDebug     = 1U << 29,
	// CS fields a client may set: PRIORITY [19:16], PANIC_PRIORITY [23:20],
	// WAIT_FOR_OUTSTANDING_WRITES [28].
	kCsClientMask   = (0xfU << 16) | (0xfU << 20) | (1U << 28),
	// DEBUG register error flags (write 1 to clear): READ_LAST_NOT_SET_ERROR,
	// FIFO_ERROR, READ_ERROR.
	kDebugErrors    = 0x7,
	// brcm,dma-channel-mask, see the header.
	kArmChannelMask = 0x7f35,
};

// Orders the control-block stores (Normal non-cacheable mapping) before the
// register writes that make the engine fetch them (Device mapping), as
// BCM2837DWC2 does before kicking a channel ("dsb sy").
static inline void
dmaBarrier(void)
{
	__asm__ volatile ("dsb sy" ::: "memory");
}

volatile uint32_t *
BCM2837DMA::chan(uint32_t channel) const
{
	return (volatile uint32_t *)(fRegs + kChanStride * channel);
}

bool
BCM2837DMA::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}
	fProvider = provider;
	fRegMap = NULL;
	fRegs = NULL;
	fAllocLock = NULL;
	fCbLock = NULL;
	fFreeMask = 0;
	fIrqMask = 0;
	fAllocatedMask = 0;
	bzero(fCallback, sizeof(fCallback));
	bzero(fCallbackRef, sizeof(fCallbackRef));
	bzero((void *)fIrqCount, sizeof(fIrqCount));
	bzero((void *)fInFlight, sizeof(fInFlight));
	bzero((void *)fErrorCount, sizeof(fErrorCount));

	fAllocLock = IOLockAlloc();
	fCbLock = IOSimpleLockAlloc();
	if (fAllocLock == NULL || fCbLock == NULL) {
		unwind();
		return false;
	}

	fRegMap = provider->mapDeviceMemoryWithIndex(0);
	if (fRegMap == NULL) {
		IOLog("BCM2837DMA: cannot map the register block\n");
		unwind();
		return false;
	}
	fRegs = (volatile uint8_t *)fRegMap->getVirtualAddress();

	// Enable only the channels we may use (ENABLE bit n), leaving the
	// firmware's alone, and reset the ones we will hand out so none starts
	// from firmware-left state.
	volatile uint32_t *enable = (volatile uint32_t *)(fRegs + kRegEnable);
	*enable = *enable | (kArmChannelMask & 0x7fff);
	for (uint32_t ch = 0; ch < kMaxIrqChannels; ch++) {
		if (!(kArmChannelMask & (1U << ch))) {
			continue;
		}
		*chan(ch) = kDMACsReset;
		for (int i = 0; i < 1000 && (*chan(ch) & kDMACsReset); i++) {
			IODelay(1);
		}
		IOReturn r = provider->registerInterrupt((int)ch, this, &BCM2837DMA::interruptOccurred,
		    (void *)(uintptr_t)ch);
		if (r != kIOReturnSuccess) {
			IOLog("BCM2837DMA: channel %u: registerInterrupt failed %#x, not offered\n", ch, r);
			continue;
		}
		r = provider->enableInterrupt((int)ch);
		if (r != kIOReturnSuccess) {
			IOLog("BCM2837DMA: channel %u: enableInterrupt failed %#x, not offered\n", ch, r);
			provider->unregisterInterrupt((int)ch);
			continue;
		}
		fIrqMask |= 1U << ch;
		fFreeMask |= 1U << ch;
	}
	if (fFreeMask == 0) {
		IOLog("BCM2837DMA: no usable channel\n");
		unwind();
		return false;
	}
	IOLog("BCM2837DMA: channels available %#x\n", fFreeMask);

	registerService();

	uint32_t test = 0;
	if (PE_parse_boot_argn("dma-test", &test, sizeof(test)) && test) {
		selfTest();
	}
	return true;
}

// Releases whatever start() acquired; safe on any partial state.
void
BCM2837DMA::unwind()
{
	if (fProvider && fRegs) {
		for (uint32_t ch = 0; ch < kMaxIrqChannels; ch++) {
			if (fIrqMask & (1U << ch)) {
				*chan(ch) = kDMACsReset;
				fProvider->disableInterrupt((int)ch);
				fProvider->unregisterInterrupt((int)ch);
			}
		}
	}
	fIrqMask = 0;
	fFreeMask = 0;
	OSSafeReleaseNULL(fRegMap);
	fRegs = NULL;
	if (fCbLock) {
		IOSimpleLockFree(fCbLock);
		fCbLock = NULL;
	}
	if (fAllocLock) {
		IOLockFree(fAllocLock);
		fAllocLock = NULL;
	}
}

void
BCM2837DMA::stop(IOService *provider)
{
	// Clients should have freed their channels. Force-free any that did not
	// so no interrupt can call into a client that is going away.
	for (uint32_t ch = 0; ch < kMaxIrqChannels; ch++) {
		if (fAllocatedMask & (1U << ch)) {
			IOLog("BCM2837DMA: channel %u still allocated at stop, freeing it\n", ch);
			freeChannel(ch);
		}
	}
	unwind();
	super::stop(provider);
}

IOReturn
BCM2837DMA::allocateChannel(uint32_t *channelOut, BCM2837DMACallback cb, void *ref,
    bool needNormalChannel)
{
	if (channelOut == NULL || cb == NULL || fAllocLock == NULL) {
		return kIOReturnBadArgument;
	}
	IOLockLock(fAllocLock);
	uint32_t avail = fFreeMask;
	if (needNormalChannel) {
		avail &= (1U << kNormalChannels) - 1;
	}
	if (avail == 0) {
		IOLockUnlock(fAllocLock);
		return kIOReturnNoResources;
	}
	uint32_t ch = __builtin_ctz(avail);
	fFreeMask &= ~(1U << ch);
	fAllocatedMask |= 1U << ch;
	IOLockUnlock(fAllocLock);

	*chan(ch) = kDMACsReset;
	for (int i = 0; i < 1000 && (*chan(ch) & kDMACsReset); i++) {
		IODelay(1);
	}
	IOInterruptState s = IOSimpleLockLockDisableInterrupt(fCbLock);
	fCallback[ch] = cb;
	fCallbackRef[ch] = ref;
	IOSimpleLockUnlockEnableInterrupt(fCbLock, s);
	*channelOut = ch;
	return kIOReturnSuccess;
}

void
BCM2837DMA::freeChannel(uint32_t channel)
{
	if (channel >= kMaxIrqChannels || !(fAllocatedMask & (1U << channel))) {
		return;
	}
	stopChannel(channel);
	// No new callback from here on...
	IOInterruptState s = IOSimpleLockLockDisableInterrupt(fCbLock);
	fCallback[channel] = NULL;
	fCallbackRef[channel] = NULL;
	IOSimpleLockUnlockEnableInterrupt(fCbLock, s);
	// ...and wait for one that was already running on another CPU, so the
	// client's context can be destroyed when this returns.
	for (int i = 0; i < 1000 && fInFlight[channel]; i++) {
		IOSleep(1);
	}
	IOLockLock(fAllocLock);
	fAllocatedMask &= ~(1U << channel);
	fFreeMask |= 1U << channel;
	IOLockUnlock(fAllocLock);
}

IOReturn
BCM2837DMA::startChannel(uint32_t channel, uint32_t firstCbBus, uint32_t csFlags)
{
	if (channel >= kMaxIrqChannels || !(fAllocatedMask & (1U << channel)) ||
	    (firstCbBus & 0x1f) || firstCbBus < kBCM2837DMABusAlias || (csFlags & ~kCsClientMask)) {
		return kIOReturnBadArgument;
	}
	*chan(channel) = kDMACsReset;
	for (int i = 0; i < 1000 && (*chan(channel) & kDMACsReset); i++) {
		IODelay(1);
	}
	dmaBarrier();       // control blocks (NC memory) visible before the engine fetches them
	chan(channel)[kRegConblkAd] = firstCbBus;
	dmaBarrier();
	*chan(channel) = kDMACsActive | csFlags;
	return kIOReturnSuccess;
}

void
BCM2837DMA::stopChannel(uint32_t channel)
{
	if (channel >= kMaxIrqChannels || !(fAllocatedMask & (1U << channel))) {
		return;
	}
	// A looping ring would otherwise continue into the next control block on
	// ABORT (QEMU bcm2835_dma.c:18 documents ABORT as "stop the current CB,
	// go to the next"), so zero the chain pointer first, abort, wait for
	// ACTIVE to drop, then reset. The CS/CONBLK_AD semantics are those of the
	// BCM2835 datasheet chapter 4 and the QEMU model; the exact Linux
	// bcm2835-dma.c sequence is not vendored here and was not checked.
	volatile uint32_t *cs = chan(channel);
	chan(channel)[kRegConblkAd] = 0;
	dmaBarrier();
	*cs = kDMACsAbort | kCsDisDebug;
	for (int i = 0; i < 1000 && (*cs & kDMACsActive); i++) {
		IODelay(1);
	}
	*cs = kDMACsReset;
	for (int i = 0; i < 1000 && (*cs & kDMACsReset); i++) {
		IODelay(1);
	}
}

uint32_t
BCM2837DMA::readChannelReg(uint32_t channel, uint32_t offset)
{
	if (channel >= kMaxIrqChannels || !(fAllocatedMask & (1U << channel)) || offset >= kChanStride) {
		return 0;
	}
	return *(volatile uint32_t *)(fRegs + kChanStride * channel + offset);
}

void
BCM2837DMA::interruptOccurred(OSObject *target, void *refCon, IOService *, int)
{
	BCM2837DMA *me = OSDynamicCast(BCM2837DMA, target);
	if (me) {
		me->handleChannelInterrupt((uint32_t)(uintptr_t)refCon);
	}
}

void
BCM2837DMA::handleChannelInterrupt(uint32_t channel)
{
	volatile uint32_t *cs = chan(channel);
	uint32_t v = *cs;
	if (v & kDMACsError) {
		// Clear the DEBUG error flags (write 1 to clear) so the channel is
		// not left wedged; the count lets a client notice (cs has ERROR set).
		chan(channel)[kRegDebug] = kDebugErrors;
		fErrorCount[channel]++;
	}
	if (v & kDMACsInt) {
		*cs = kDMACsInt;            // acknowledge by writing INT only
	}
	fIrqCount[channel]++;

	// Take cb and ref together, and mark the call in flight, so freeChannel()
	// can wait for it.
	IOInterruptState s = IOSimpleLockLockDisableInterrupt(fCbLock);
	BCM2837DMACallback cb = fCallback[channel];
	void *ref = fCallbackRef[channel];
	if (cb) {
		fInFlight[channel]++;
	}
	IOSimpleLockUnlockEnableInterrupt(fCbLock, s);
	if (cb) {
		cb(ref, channel, v);
		__sync_fetch_and_sub(&fInFlight[channel], 1);
	}
}

IOBufferMemoryDescriptor *
BCM2837DMA::allocateBuffer(uint32_t size, uint32_t align, void **virtOut, uint32_t *busOut)
{
	if (size == 0 || (align & (align - 1))) {
		return NULL;
	}
	if (align < 4) {
		align = 4;
	}
	IOBufferMemoryDescriptor *bmd = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
	    kernel_task,
	    kIODirectionInOut | kIOMemoryPhysicallyContiguous |
	    kIOMapWriteCombineCache | kIOMemoryMapperNone,
	    size,
	    0x3FFFFFFFULL & ~((uint64_t)align - 1));
	if (bmd == NULL) {
		return NULL;
	}
	if (bmd->prepare() != kIOReturnSuccess) {
		bmd->release();
		return NULL;
	}
	IOByteCount segLen = 0;
	IOPhysicalAddress phys = bmd->getPhysicalSegment(0, &segLen, 0);
	if (phys == 0 || segLen < size || (phys & (align - 1))) {
		bmd->complete();
		bmd->release();
		return NULL;
	}
	*virtOut = bmd->getBytesNoCopy();
	// The 0x3fffffff physical mask above keeps this inside the low 1GB, so the
	// bus address fits 32 bits; assert that rather than assume it.
	if ((uint64_t)phys + size > 0x40000000ULL) {
		bmd->complete();
		bmd->release();
		return NULL;
	}
	*busOut = (uint32_t)(kBCM2837DMABusAlias | (uint64_t)phys);
	return bmd;
}

// Boot-arg dma-test=1: copy a buffer memory to memory on a channel and wait
// for the completion interrupt. DARW-313 acceptance.
namespace {
struct SelfTestState {
	volatile uint32_t done;
	volatile uint32_t cs;
};
void
selfTestCallback(void *ref, uint32_t, uint32_t cs)
{
	SelfTestState *st = (SelfTestState *)ref;
	st->cs = cs;
	st->done = 1;
}
}

void
BCM2837DMA::selfTest()
{
	enum { kLen = 4096 };
	void *srcV = NULL, *dstV = NULL, *cbV = NULL;
	uint32_t srcB = 0, dstB = 0, cbB = 0;
	IOBufferMemoryDescriptor *src = allocateBuffer(kLen, 32, &srcV, &srcB);
	IOBufferMemoryDescriptor *dst = allocateBuffer(kLen, 32, &dstV, &dstB);
	IOBufferMemoryDescriptor *cbm = allocateBuffer(sizeof(BCM2837DMAControlBlock), 32, &cbV, &cbB);
	SelfTestState st = { 0, 0 };
	uint32_t ch = 0;
	bool ok = false;

	if (src && dst && cbm && allocateChannel(&ch, selfTestCallback, &st) == kIOReturnSuccess) {
		uint8_t *s = (uint8_t *)srcV, *d = (uint8_t *)dstV;
		for (uint32_t i = 0; i < kLen; i++) {
			s[i] = (uint8_t)(i * 7 + 3);
			d[i] = 0;
		}
		BCM2837DMAControlBlock *cb = (BCM2837DMAControlBlock *)cbV;
		bzero(cb, sizeof(*cb));
		cb->ti = kDMATiIntEn | kDMATiSrcInc | kDMATiDestInc | kDMATiWaitResp;
		cb->srcBus = srcB;
		cb->dstBus = dstB;
		cb->length = kLen;
		cb->nextBus = 0;
		if (startChannel(ch, cbB) == kIOReturnSuccess) {
			for (int i = 0; i < 1000 && !st.done; i++) {
				IOSleep(1);
			}
			ok = st.done && memcmp(s, d, kLen) == 0;
		}
		IOLog("BCM2837DMA: self-test channel %u: %s (interrupt %s, cs=%#x, irqs=%u)\n", ch,
		    ok ? "PASS" : "FAIL", st.done ? "seen" : "NOT seen", st.cs, fIrqCount[ch]);
		freeChannel(ch);
	} else {
		IOLog("BCM2837DMA: self-test: setup failed\n");
	}
	IOBufferMemoryDescriptor *bufs[3] = { src, dst, cbm };
	for (int i = 0; i < 3; i++) {
		if (bufs[i]) {
			bufs[i]->complete();
			bufs[i]->release();
		}
	}
}
