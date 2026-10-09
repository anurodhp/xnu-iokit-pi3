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
	// brcm,dma-channel-mask, see the header.
	kArmChannelMask = 0x7f35,
};

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
	fFreeMask = 0;
	fIrqMask = 0;
	bzero(fCallback, sizeof(fCallback));
	bzero(fCallbackRef, sizeof(fCallbackRef));
	bzero((void *)fIrqCount, sizeof(fIrqCount));

	fAllocLock = IOLockAlloc();
	if (fAllocLock == NULL) {
		return false;
	}

	fRegMap = provider->mapDeviceMemoryWithIndex(0);
	if (fRegMap == NULL) {
		IOLog("BCM2837DMA: cannot map the register block\n");
		return false;
	}
	fRegs = (volatile uint8_t *)fRegMap->getVirtualAddress();

	// Enable the channels (ENABLE bit n, reset default: all on) and reset
	// the ones we will hand out so none starts from firmware-left state.
	*(volatile uint32_t *)(fRegs + kRegEnable) = 0x7fff;
	for (uint32_t ch = 0; ch < kMaxIrqChannels; ch++) {
		if (!(kArmChannelMask & (1U << ch))) {
			continue;
		}
		*chan(ch) = kDMACsReset;
		// RESET self-clears; bounded wait.
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

void
BCM2837DMA::stop(IOService *provider)
{
	for (uint32_t ch = 0; ch < kMaxIrqChannels; ch++) {
		if (fIrqMask & (1U << ch)) {
			*chan(ch) = kDMACsReset;
			provider->disableInterrupt((int)ch);
			provider->unregisterInterrupt((int)ch);
		}
	}
	fIrqMask = 0;
	OSSafeReleaseNULL(fRegMap);
	if (fAllocLock) {
		IOLockFree(fAllocLock);
		fAllocLock = NULL;
	}
	super::stop(provider);
}

IOReturn
BCM2837DMA::allocateChannel(uint32_t *channelOut, BCM2837DMACallback cb, void *ref)
{
	IOLockLock(fAllocLock);
	if (fFreeMask == 0) {
		IOLockUnlock(fAllocLock);
		return kIOReturnNoResources;
	}
	uint32_t ch = __builtin_ctz(fFreeMask);
	fFreeMask &= ~(1U << ch);
	IOLockUnlock(fAllocLock);

	*chan(ch) = kDMACsReset;
	for (int i = 0; i < 1000 && (*chan(ch) & kDMACsReset); i++) {
		IODelay(1);
	}
	fCallbackRef[ch] = ref;
	fCallback[ch] = cb;      // last: the handler checks this
	*channelOut = ch;
	return kIOReturnSuccess;
}

void
BCM2837DMA::freeChannel(uint32_t channel)
{
	if (channel >= kMaxIrqChannels || !(fIrqMask & (1U << channel))) {
		return;
	}
	stopChannel(channel);
	fCallback[channel] = NULL;
	fCallbackRef[channel] = NULL;
	IOLockLock(fAllocLock);
	fFreeMask |= 1U << channel;
	IOLockUnlock(fAllocLock);
}

IOReturn
BCM2837DMA::startChannel(uint32_t channel, uint32_t firstCbBus, uint32_t csFlags)
{
	if (channel >= kMaxIrqChannels || !(fIrqMask & (1U << channel)) || (firstCbBus & 0x1f)) {
		return kIOReturnBadArgument;
	}
	*chan(channel) = kDMACsReset;
	for (int i = 0; i < 1000 && (*chan(channel) & kDMACsReset); i++) {
		IODelay(1);
	}
	chan(channel)[kRegConblkAd] = firstCbBus;
	*chan(channel) = kDMACsActive | csFlags;
	return kIOReturnSuccess;
}

void
BCM2837DMA::stopChannel(uint32_t channel)
{
	if (channel >= kMaxIrqChannels || !(fIrqMask & (1U << channel))) {
		return;
	}
	// Abort the current control block first so RESET does not race a write
	// in flight (Linux bcm2835_dma_abort()).
	volatile uint32_t *cs = chan(channel);
	if (*cs & kDMACsActive) {
		*cs = kDMACsAbort;
		IODelay(100);
	}
	*cs = kDMACsReset;
	for (int i = 0; i < 1000 && (*cs & kDMACsReset); i++) {
		IODelay(1);
	}
}

uint32_t
BCM2837DMA::readChannelReg(uint32_t channel, uint32_t offset)
{
	if (channel >= kMaxIrqChannels || offset >= kChanStride) {
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
	if (v & kDMACsInt) {
		*cs = kDMACsInt;            // acknowledge, as Linux bcm2835_dma_callback()
	}
	fIrqCount[channel]++;
	BCM2837DMACallback cb = fCallback[channel];
	if (cb) {
		cb(fCallbackRef[channel], channel, v);
	}
}

IOBufferMemoryDescriptor *
BCM2837DMA::allocateBuffer(uint32_t size, uint32_t align, void **virtOut, uint32_t *busOut)
{
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
