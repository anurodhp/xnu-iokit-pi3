#include <IOKit/IOLib.h>
#include <sys/errno.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <libkern/OSAtomic.h>
#include <mach/vm_param.h>
#include "BCM2837VCHIQ.h"
#include "BCM2837Mailbox.h"
#include "vchiq_xnu_platform.h"

#define super IOService
OSDefineMetaClassAndStructors(BCM2837VCHIQ, IOService)

extern "C" vm_map_address_t phystokv(uint64_t pa);
extern "C" int vchiq_xnu_slot_mem_size(void);
extern "C" void vchiq_xnu_set_log_level(int level);

/* Linux include/soc/bcm2835/raspberrypi-firmware.h: RPI_FIRMWARE_VCHIQ_INIT */
static const uint32_t kTagVCHIQInit = 0x00048010;

/* The doorbell block (vchiq_arm.c): BELL0 read-to-clear, BELL2 rings the VPU. */
static const uint32_t kBell0 = 0x00 / 4;
static const uint32_t kBell2 = 0x08 / 4;
static const uint32_t kArmDSActive = 1U << 2;

/*
 * The VPU sees ARM RAM at 0xC0000000 (L2-uncached alias): the vchiq node's
 * dma-ranges on Linux, kV3D_BusAliasBase for the V3D here.
 */
static const uint32_t kBusAlias = 0xC0000000U;
static const uint64_t kBusWindow = 0x3F000000ULL;

/*
 * The cache line size the VPU firmware assumes for the pagelist fragments
 * (the partial lines at the ends of a receive, which it writes into the
 * shared block instead of the client's memory). Linux's vchiq_arm.c:
 * "Modern VPU firmware looks for a DT "cache-line-size" property in the
 * VCHIQ node and will overwrite it with the actual L2 cache size ... we
 * have to use the VPU firmware's compatibility value of 32". The firmware
 * here is given no Linux device tree, so it uses 32 -- measured on a real
 * Pi 3 (2026-10-17): with 64 (bcm2836_drvdata's value) the last 32 bytes
 * but 8 of every decoded 1280x720 frame were stale (32 bytes off by tens,
 * at end-40 .. end-9); with 32 every sample is within 1 of FFmpeg's
 * software decoder (hwdec_compare, 297 frames). The fragments are two lines (head,
 * tail) each, MAX_FRAGMENTS of them (VCHIQ_NUM_CURRENT_BULKS * 2).
 */
static const uint32_t kCacheLine = 32;
static const uint32_t kFragmentSize = 2 * kCacheLine;
static const uint32_t kMaxFragments = 32 * 2;

/* The VPU's pagelist granule is 4 KiB (the 12 run-length bits in addrs[]). */
static const uint32_t kVCPage = 4096;

/*
 * Pagelists live in VPU-readable memory. Linux takes them from a
 * PAGE_SIZE dma_pool and falls back to dma_alloc_coherent() for bigger
 * ones; the same here with 16 KiB chunks (4092 pages: a 16 MiB transfer)
 * carved from one block, 32 of them (VCHIQ_NUM_CURRENT_BULKS).
 */
static const uint32_t kPLChunk = 16384;
static const uint32_t kPLChunks = 32;

volatile uint32_t *BCM2837VCHIQ::sBells;

/*
 * The shared block. Once the core's threads run (vchiq_xnu_init_state) it
 * is never freed: they use it until the machine stops, and the firmware
 * keeps its address after VCHIQ_INIT (nothing takes it back).
 */
static IOBufferMemoryDescriptor *sSlots;
static uint8_t *sSlotKva;           /* write-combined */
static uint32_t sSlotBus;
static int sSlotMemSize;            /* slots only; the fragments follow */
static uint8_t *sFragments;
static uint32_t sFragmentsBus;

static IOLock *sLock;               /* fragments + pagelist chunks */
static uint64_t sFreeFragmentBits;  /* 1 = free (kMaxFragments == 64) */
static IOBufferMemoryDescriptor *sPLPool;
static uint8_t *sPLKva;
static uint32_t sPLBus;
static uint32_t sPLFree;            /* bitmap, 1 = free */

/* Clean, or clean and invalidate, [va, va+len) to the point of coherency.
 * By VA: broadcast to all four cores (see BCM2837V3D.cpp v3d_dcache_civac,
 * and why xnu's own FlushPoC_DcacheRegion cannot be used here). */
static void
vchiq_dcache(vm_offset_t va, vm_size_t len, bool invalidate)
{
	uint64_t ctr;
	__asm__ volatile ("mrs %0, ctr_el0" : "=r" (ctr));
	const vm_offset_t line = 4UL << ((ctr >> 16) & 0xf);
	vm_offset_t p = va & ~(line - 1), end = va + len;
	__asm__ volatile ("dmb sy" ::: "memory");
	if (invalidate) {
		for (; p < end; p += line) {
			__asm__ volatile ("dc civac, %0" :: "r" (p) : "memory");
		}
	} else {
		for (; p < end; p += line) {
			__asm__ volatile ("dc cvac, %0" :: "r" (p) : "memory");
		}
	}
	__asm__ volatile ("dsb sy" ::: "memory");
}

/* The same over every physical segment of a prepared descriptor, through the
 * kernel's physical aperture, so it works from any thread (the completion of
 * a receive runs on the core's slot handler thread, not in the client). */
static void
vchiq_dcache_md(IOMemoryDescriptor *md, bool invalidate)
{
	IOByteCount off = 0, len = md->getLength();
	while (off < len) {
		IOByteCount seg = 0;
		addr64_t pa = md->getPhysicalSegment(off, &seg, kIOMemoryMapperNone);
		if (pa == 0 || seg == 0) {
			break;
		}
		if (seg > len - off) {
			seg = len - off;
		}
		vchiq_dcache((vm_offset_t)phystokv(pa), seg, invalidate);
		off += seg;
	}
}

/* ============================== pagelists ============================== */

/* struct pagelist (vchiq_pagelist.h) */
struct VCPagelist {
	uint32_t length;
	uint16_t type;
	uint16_t offset;
	uint32_t addrs[1];
};
enum { kPagelistWrite = 0, kPagelistRead = 1, kPagelistReadWithFragments = 2 };

struct VCHIQBulkInfo {
	IOMemoryDescriptor *md;
	VCPagelist *pl;
	int chunk;                          /* sPLPool chunk, or -1 */
	IOBufferMemoryDescriptor *plMem;    /* when chunk == -1 */
	uint32_t size;
	uint32_t offset;                    /* of the buffer in its first 4 KiB page */
	bool receive;
	int fragment;                       /* index, or -1 */
};

static int
take_bit(uint64_t *bits, unsigned n)
{
	for (unsigned i = 0; i < n; i++) {
		if (*bits & (1ULL << i)) {
			*bits &= ~(1ULL << i);
			return (int)i;
		}
	}
	return -1;
}

/*
 * A free fragment slot. Linux waits on a semaphore (down_interruptible);
 * MAX_FRAGMENTS is twice the number of bulks the core lets be outstanding,
 * so running out means a leak, not load.
 */
static int
fragment_get(void)
{
	IOLockLock(sLock);
	int i = take_bit(&sFreeFragmentBits, kMaxFragments);
	IOLockUnlock(sLock);
	return i;
}

static void
fragment_put(int i)
{
	IOLockLock(sLock);
	sFreeFragmentBits |= 1ULL << i;
	IOLockUnlock(sLock);
}

extern "C" int
vchiq_xnu_create_pagelist(void *kbuf, void *ubuf, int size, int receive,
    uint32_t *bus, void **cookie)
{
	if (size < 0 || (uint32_t)size > 0x7fffffffU - kVCPage || (kbuf == NULL) == (ubuf == NULL)) {
		return EINVAL;
	}
	mach_vm_address_t addr = (mach_vm_address_t)(uintptr_t)(kbuf != NULL ? kbuf : ubuf);
	task_t task = kbuf != NULL ? kernel_task : current_task();
	uint32_t offset = (uint32_t)(addr & (kVCPage - 1));
	uint32_t npages = (uint32_t)(((uint64_t)offset + (uint32_t)size + kVCPage - 1) / kVCPage);
	if (npages == 0) {
		npages = 1;     /* a zero-length transfer still names a page (Linux: DIV_ROUND_UP(0 + offset)) */
	}
	size_t plSize = offsetof(VCPagelist, addrs) + (size_t)npages * sizeof(uint32_t);

	VCHIQBulkInfo *bi = (VCHIQBulkInfo *)IOMallocZero(sizeof(*bi));
	if (bi == NULL) {
		return ENOMEM;
	}
	bi->chunk = -1;
	bi->fragment = -1;
	bi->size = (uint32_t)size;
	bi->offset = offset;
	bi->receive = receive != 0;

	/* Wire the buffer (Linux: pin_user_pages_fast). kIODirectionIn: the
	 * device writes memory. A zero-length buffer still gets one byte so the
	 * descriptor has a page. */
	bi->md = IOMemoryDescriptor::withAddressRange(addr, size > 0 ? (mach_vm_size_t)size : 1,
	    receive ? kIODirectionIn : kIODirectionOut, task);
	if (bi->md == NULL || bi->md->prepare() != kIOReturnSuccess) {
		if (bi->md != NULL) {
			bi->md->release();
		}
		IOFree(bi, sizeof(*bi));
		return EFAULT;
	}

	/* The pagelist itself. */
	if (plSize <= kPLChunk && sPLPool != NULL) {
		uint64_t bits;
		IOLockLock(sLock);
		bits = sPLFree;
		int c = take_bit(&bits, kPLChunks);
		sPLFree = (uint32_t)bits;
		IOLockUnlock(sLock);
		if (c >= 0) {
			bi->chunk = c;
			bi->pl = (VCPagelist *)(void *)(sPLKva + (size_t)c * kPLChunk);
			*bus = sPLBus + (uint32_t)c * kPLChunk;
		}
	}
	if (bi->pl == NULL) {
		bi->plMem = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
		    kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMapWriteCombineCache,
		    plSize, 0x3FFFF000ULL);
		if (bi->plMem == NULL || bi->plMem->getPhysicalAddress() + plSize > kBusWindow) {
			OSSafeReleaseNULL(bi->plMem);
			bi->md->complete();
			bi->md->release();
			IOFree(bi, sizeof(*bi));
			return ENOMEM;
		}
		bi->pl = (VCPagelist *)bi->plMem->getBytesNoCopy();
		*bus = (uint32_t)bi->plMem->getPhysicalAddress() + kBusAlias;
	}

	/*
	 * addrs[]: runs of physically consecutive 4 KiB pages, each entry the
	 * run's first page's bus address with (pages - 1) in the low 12 bits
	 * (create_pagelist()'s non-36-bit branch). Page i of the buffer is the
	 * one holding buffer byte i*4096 - offset.
	 */
	uint32_t k = 0;
	for (uint32_t i = 0; i < npages; i++) {
		IOByteCount moff = (i == 0) ? 0 : (IOByteCount)i * kVCPage - offset;
		IOByteCount seg = 0;
		addr64_t pa = bi->md->getPhysicalSegment(moff, &seg, kIOMemoryMapperNone);
		uint64_t page = pa & ~(uint64_t)(kVCPage - 1);
		if (pa == 0 || page + kVCPage > kBusWindow) {
			IOLog("BCM2837VCHIQ: bulk page %#llx outside the VPU window\n", (uint64_t)pa);
			vchiq_xnu_free_pagelist(bi, -1);
			return EFAULT;
		}
		uint32_t b = (uint32_t)page + kBusAlias;
		if (k > 0) {
			uint32_t prev = bi->pl->addrs[k - 1];
			uint32_t run = prev & (kVCPage - 1);
			if ((prev & ~(kVCPage - 1)) + (run + 1) * kVCPage == b && run < kVCPage - 1) {
				bi->pl->addrs[k - 1] = prev + 1;
				continue;
			}
		}
		bi->pl->addrs[k++] = b;
	}
	bi->pl->length = (uint32_t)size;
	bi->pl->type = receive ? kPagelistRead : kPagelistWrite;
	bi->pl->offset = (uint16_t)offset;

	/* Partial cache lines (fragments) require special measures */
	if (receive && ((offset & (kCacheLine - 1)) || ((offset + (uint32_t)size) & (kCacheLine - 1)))) {
		int f = fragment_get();
		if (f < 0) {
			IOLog("BCM2837VCHIQ: out of bulk fragments\n");
			vchiq_xnu_free_pagelist(bi, -1);
			return ENOMEM;
		}
		bi->fragment = f;
		bi->pl->type = (uint16_t)(kPagelistReadWithFragments + f);
	}

	/*
	 * Linux: dma_map_sg(). To the device: clean. From the device: clean and
	 * invalidate, so no dirty line is evicted over what the VPU writes (the
	 * partial lines at either end are not written by the VPU: fragments).
	 */
	if (size > 0) {
		vchiq_dcache_md(bi->md, receive != 0);
	}
	__asm__ volatile ("dsb sy" ::: "memory");

	*cookie = bi;
	return 0;
}

extern "C" void
vchiq_xnu_free_pagelist(void *cookie, int actual)
{
	VCHIQBulkInfo *bi = (VCHIQBulkInfo *)cookie;

	if (bi->receive && bi->size > 0) {
		/* Linux: dma_unmap_sg() before the CPU touches the data. Lines the
		 * CPU fetched during the transfer (speculatively) are stale. */
		vchiq_dcache_md(bi->md, true);
	}

	/* Deal with any partial cache lines (fragments) */
	if (bi->fragment >= 0) {
		const uint8_t *frag = sFragments + (size_t)bi->fragment * kFragmentSize;
		uint32_t head = (kCacheLine - bi->offset) & (kCacheLine - 1);
		uint32_t tail = (bi->offset + (uint32_t)actual) & (kCacheLine - 1);

		if (actual >= 0 && head != 0) {
			if (head > (uint32_t)actual) {
				head = (uint32_t)actual;
			}
			bi->md->writeBytes(0, frag, head);
		}
		if (actual >= 0 && head < (uint32_t)actual && tail != 0) {
			bi->md->writeBytes((IOByteCount)actual - tail, frag + kCacheLine, tail);
		}
		fragment_put(bi->fragment);
	}

	bi->md->complete();
	bi->md->release();
	if (bi->chunk >= 0) {
		IOLockLock(sLock);
		sPLFree |= 1U << bi->chunk;
		IOLockUnlock(sLock);
	}
	OSSafeReleaseNULL(bi->plMem);
	IOFree(bi, sizeof(*bi));
}

extern "C" void
vchiq_xnu_ring_doorbell(void)
{
	BCM2837VCHIQ::sBells[kBell2] = 0;
}

/* ============================== lifecycle ============================== */

void
BCM2837VCHIQ::interruptOccurred(OSObject *, void *, IOService *, int)
{
	/* Read (and clear) the doorbell */
	uint32_t status = sBells[kBell0];
	if (status & kArmDSActive) {
		vchiq_xnu_doorbell();
	}
}

bool
BCM2837VCHIQ::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}
	uint32_t arg;
	if (PE_parse_boot_argn("vchiq", &arg, sizeof(arg)) && arg == 0) {
		IOLog("BCM2837VCHIQ: disabled by boot-arg vchiq=0\n");
		return false;
	}
	if (PE_parse_boot_argn("vchiq-log", &arg, sizeof(arg))) {
		vchiq_xnu_set_log_level((int)arg);
	}
	if (sSlotKva != NULL) {
		/* The core keeps one global state (g_state); the firmware has one
		 * set of slots per boot. A second start (rematch) cannot redo it. */
		IOLog("BCM2837VCHIQ: already initialised\n");
		return false;
	}

	IODeviceMemory *mem = provider->getDeviceMemoryWithIndex(0);
	if (mem == NULL || (fRegMap = mem->map()) == NULL) {
		IOLog("BCM2837VCHIQ: no doorbell registers\n");
		return false;
	}
	sBells = (volatile uint32_t *)fRegMap->getVirtualAddress();

	sLock = IOLockAlloc();
	if (sLock == NULL) {
		return false;
	}

	/* Allocate space for the channels in coherent memory */
	sSlotMemSize = vchiq_xnu_slot_mem_size();
	uint32_t fragMemSize = (kFragmentSize * kMaxFragments + kVCPage - 1) & ~(kVCPage - 1);
	uint32_t total = (uint32_t)sSlotMemSize + fragMemSize;
	sSlots = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
	    kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMapWriteCombineCache,
	    total, 0x3FFFF000ULL);
	if (sSlots == NULL || sSlots->getPhysicalAddress() + total > kBusWindow) {
		IOLog("BCM2837VCHIQ: could not allocate %u bytes of VPU-visible memory\n", total);
		OSSafeReleaseNULL(sSlots);
		return false;
	}
	uint8_t *kva = (uint8_t *)sSlots->getBytesNoCopy();
	/* Lines of these pages cached before they became write-combined would
	 * otherwise be written back over the VPU's data some day. */
	vchiq_dcache((vm_offset_t)kva, total, true);
	uint32_t slotBus = (uint32_t)sSlots->getPhysicalAddress() + kBusAlias;

	sPLPool = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
	    kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMapWriteCombineCache,
	    kPLChunk * kPLChunks, 0x3FFFF000ULL);
	if (sPLPool != NULL && sPLPool->getPhysicalAddress() + kPLChunk * kPLChunks <= kBusWindow) {
		sPLKva = (uint8_t *)sPLPool->getBytesNoCopy();
		vchiq_dcache((vm_offset_t)sPLKva, kPLChunk * kPLChunks, true);
		sPLBus = (uint32_t)sPLPool->getPhysicalAddress() + kBusAlias;
		sPLFree = 0xffffffffU;
	} else {
		OSSafeReleaseNULL(sPLPool);     /* every pagelist gets its own allocation */
	}

	sSlotKva = kva;
	sSlotBus = slotBus;
	sFragments = kva + sSlotMemSize;
	sFragmentsBus = slotBus + (uint32_t)sSlotMemSize;
	sFreeFragmentBits = ~0ULL;          /* kMaxFragments == 64 */

	int err = vchiq_xnu_init_state(kva, sSlotMemSize, sFragmentsBus, (int)kMaxFragments);
	if (err != 0) {
		IOLog("BCM2837VCHIQ: vchiq_init_state failed (%d)\n", err);
		return false;
	}

	IOReturn r = provider->registerInterrupt(0, this, &BCM2837VCHIQ::interruptOccurred);
	if (r != kIOReturnSuccess || provider->enableInterrupt(0) != kIOReturnSuccess) {
		IOLog("BCM2837VCHIQ: doorbell interrupt registration failed (%#x)\n", r);
		return false;
	}
	fIRQRegistered = true;

	/* Send the base address of the slots to VideoCore */
	uint32_t channelbase = slotBus, resp = 0;
	if (!BCM2837Mailbox::propertyTagWords(kTagVCHIQInit, &channelbase, 1, &resp)) {
		IOLog("BCM2837VCHIQ: VCHIQ_INIT: no answer from the firmware\n");
		return false;
	}
	if (channelbase != 0) {
		IOLog("BCM2837VCHIQ: VCHIQ_INIT refused (response %#x, tag code %#x)\n", channelbase, resp);
		return false;
	}

	IOLog("BCM2837VCHIQ: slots at bus %#x (%d bytes), %u fragments, pagelist pool %s\n",
	    slotBus, sSlotMemSize, kMaxFragments, sPLPool != NULL ? "on" : "off");
	setProperty("IOUserClientClass", "BCM2837VCHIQUserClient");
	registerService();
	return true;
}

void
BCM2837VCHIQ::stop(IOService *provider)
{
	/*
	 * The VPU keeps the slots' address until the next boot: there is no
	 * firmware call that takes it back, so the memory is never freed and
	 * the core's threads keep running. Only the interrupt goes.
	 */
	if (fIRQRegistered) {
		provider->disableInterrupt(0);
		provider->unregisterInterrupt(0);
		fIRQRegistered = false;
	}
	super::stop(provider);
}
