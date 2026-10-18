/*
 * BCM2837V3D - see BCM2837V3D.h for scope. Real hardware only; QEMU has no
 * V3D, so start() declines when the ASB bridge id does not read back.
 */
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOSubMemoryDescriptor.h>
#include <libkern/OSAtomic.h>
#include <kern/clock.h>
#include <sys/errno.h>
#include <pexpert/pexpert.h>
#include "BCM2837V3D.h"
#include "BCM2837V3DRegs.h"
#include "BCM2837Mailbox.h"
#include "v3d_kglue.h"

#define super IOService
OSDefineMetaClassAndFinalStructors(BCM2837V3D, IOService)
OSDefineMetaClassAndFinalStructors(V3DBuffer, OSObject)

// Binner pool layout (see allocBinSlice()).
enum {
	kBinPoolSize  = 16U * 1024 * 1024,   // vc4_v3d_allocate_bin_bo: 16 MiB
	kBinOverflow  = 4U * 1024 * 1024,
	kBinSlotSize  = 512U * 1024,          // vc4->bin_alloc_size
	kBinSlotCount = (kBinPoolSize - kBinOverflow) / kBinSlotSize,   // 24
};

enum {
	kMaxBOSize   = 16U * 1024 * 1024,
	// Hang detector period. Linux vc4_queue_hangcheck(): 100 ms
	// (vc4_gem.c, the mod_timer in vc4_queue_hangcheck).
	kHangcheckMs = 100,
	// GPU memory pool, MiB (boot-arg v3d-pool): see initGpuPool().
	kGpuPoolDefaultMB = 128,
};

static inline void
cpuToGpuBarrier(void)
{
	// BOs are write-combined: drain the CPU write buffer before the GPU is
	// told to read them.
	__asm__ volatile ("dsb sy" ::: "memory");
}

/* ====================== validator allocation shims ======================
 * Declared in validate/vc4_simulator_validate.h; the vendored C validators
 * (built into this kext) allocate through these. Sizes are kept in a
 * 16-byte header because IOFree needs the size. Zero-filled. */
extern "C" {
void *
v3d_kalloc(size_t n)
{
	size_t total = n + 16;
	if (total < n) {
		return NULL;
	}
	uint8_t *p = (uint8_t *)IOMalloc(total);
	if (p == NULL) {
		return NULL;
	}
	bzero(p, total);
	*(size_t *)p = total;
	return p + 16;
}

void
v3d_kfree(void *q)
{
	if (q != NULL) {
		uint8_t *p = (uint8_t *)q - 16;
		IOFree(p, *(size_t *)p);
	}
}

void *
v3d_krealloc_nosize(void *q, size_t n)
{
	if (q == NULL) {
		return v3d_kalloc(n);
	}
	size_t old = *(size_t *)((uint8_t *)q - 16) - 16;
	void *r = v3d_kalloc(n);
	if (r != NULL) {
		memcpy(r, q, old < n ? old : n);
		v3d_kfree(q);
	}
	return r;
}

// BUG_ON() in the validators guards invariants the host fuzzer has never
// seen violated (see validate/README); if one ever is, continuing would
// write outside a buffer.
void
v3d_bug_on(const char *file, int line)
{
	panic("BCM2837V3D: validator invariant violated at %s:%d", file, line);
}
}

/* ============================ V3DBuffer ============================ */

/* io_map_with_prot (osfmk/arm/io_map.c) after VM init: kmem_alloc_pageable()
 * plus pmap_map() with the given WIMG flags, which pmap_enter_options honours
 * even for a managed page whose own attribute is different (osfmk/arm/pmap.c,
 * "if ((flags & (VM_WIMG_MASK | VM_WIMG_USE_DEFAULT)))"). This driver is
 * compiled into the kernel, so it can use the kernel's own primitives. */
#include <mach/vm_param.h>
#ifndef VM_WIMG_COPYBACK
#define VM_WIMG_COPYBACK 0x2   /* osfmk/arm/memory_types.h: VM_MEM_COHERENT, normal cacheable */
#endif
extern "C" vm_offset_t io_map_with_prot(vm_map_offset_t phys_addr, vm_size_t size, unsigned int flags, vm_prot_t prot);
extern "C" void ml_io_unmap(vm_offset_t addr, vm_size_t sz);

/* Clean and invalidate [va, va+len) to the point of coherency, by VA. Not
 * xnu's FlushPoC_DcacheRegion: that is empty on APPLE_ARM64_ARCH_FAMILY
 * builds ("Fully Coherent"), which the Cortex-A53 is not. By-VA maintenance
 * is broadcast to every core in the inner-shareable domain, so a line any of
 * the four cores fetched through the alias is dropped too (set/way would only
 * reach this core's L1). */
static void
v3d_dcache_civac(vm_offset_t va, vm_size_t len)
{
	uint64_t ctr;
	__asm__ volatile ("mrs %0, ctr_el0" : "=r" (ctr));
	const vm_offset_t line = 4UL << ((ctr >> 16) & 0xf);      /* DminLine: log2 words */
	vm_offset_t p = va & ~(line - 1), end = va + len;
	__asm__ volatile ("dmb sy" ::: "memory");
	for (; p < end; p += line) {
		__asm__ volatile ("dc civac, %0" :: "r" (p) : "memory");
	}
	__asm__ volatile ("dsb sy" ::: "memory");
}

/*
 * The fast path of BCM2837V3DUserClient::readBO(); the caller has waited for
 * the GPU and range-checked offset/size.
 *
 * The BO is allocated write-combined (kIOMapWriteCombineCache), and that is
 * an attribute of its physical pages: xnu applies it to every mapping made
 * from the BO's memory object, the kernel's physical aperture included
 * (pmap_update_cache_attributes_locked). So a kIOMapCopybackCache mapping of
 * fMem, which is what this used to read through, still came out
 * write-combined: the read-back ran at the uncached rate, 98 MB/s on a Pi 3
 * against 957 MB/s for RAM (v3d_membench, 2026-10-17). The cached alias here
 * is mapped from the physical address with an explicit VM_WIMG_COPYBACK, so
 * it really is cacheable, while the BO and every client mapping of it stay
 * write-combined.
 *
 * Coherence of the two aliases: the GPU writes RAM directly, and nothing
 * writes through this alias (read-only), so no line can be dirty. A line
 * fetched through it (by the copy, or speculatively) can be stale once the
 * GPU writes again, which is why every read starts with a clean+invalidate
 * of the range through the alias, after the GPU is done; lines fetched after
 * that hold the finished data.
 */
IOReturn
V3DBuffer::mapCachedAlias(void)
{
	if (fMem == NULL) {
		return kIOReturnNotPermitted;
	}
	if (fCachedVA == 0) {
		vm_size_t len = round_page(fSize);
		vm_offset_t va = io_map_with_prot(fPhys, len, VM_WIMG_COPYBACK, VM_PROT_READ);
		if (va == 0) {
			return kIOReturnNoMemory;
		}
		// Two readers racing here would each make a mapping; keep one.
		if (!OSCompareAndSwapPtr(NULL, (void *)va, (void * volatile *)&fCachedVA)) {
			ml_io_unmap(va, len);
		} else {
			fCachedLen = len;
		}
	}
	return kIOReturnSuccess;
}

IOReturn
V3DBuffer::readCached(uint32_t offset, uint32_t size, uint64_t userDst)
{
	IOReturn ret = mapCachedAlias();
	if (ret != kIOReturnSuccess) {
		return ret;
	}
	uint64_t t0 = mach_absolute_time();
	v3d_dcache_civac(fCachedVA + offset, size);
	uint64_t t1 = mach_absolute_time();
	const void *src = (const void *)(fCachedVA + offset);
	IOReturn r = copyout(src, (user_addr_t)userDst, size) == 0 ? kIOReturnSuccess : kIOReturnVMError;
	uint64_t t2 = mach_absolute_time();
	if (v3d_kglue_debug) {
		// v3d-debug=1: average cost of each step every 64 reads (DARW-303).
		static uint64_t sFlush, sCopy, sBytes;
		static uint32_t sCount;
		sFlush += t1 - t0;
		sCopy += t2 - t1;
		sBytes += size;
		if (++sCount == 64) {
			uint64_t fl, cp;
			absolutetime_to_nanoseconds(sFlush, &fl);
			absolutetime_to_nanoseconds(sCopy, &cp);
			IOLog("V3D readBO: %llu KB/read, flush %llu us, copyout %llu us (avg of 64)\n",
			    sBytes / 64 / 1024, fl / 64 / 1000, cp / 64 / 1000);
			sFlush = sCopy = sBytes = 0;
			sCount = 0;
		}
	}
	return r;
}

/*
 * kV3DMethodPrepareRead: drop the range from every core's cache, so a
 * reader of the shared segment (shareWithShm) fetches what the GPU wrote.
 * The caller has waited for the GPU. By-VA maintenance through the cached
 * alias reaches the segment's lines too: the A53's data caches are
 * physically indexed and tagged, and the operation is broadcast to the
 * inner-shareable domain (v3d_dcache_civac). Nothing writes through any
 * cacheable alias, so the clean half never writes back.
 */
IOReturn
V3DBuffer::invalidateCached(uint32_t offset, uint32_t size)
{
	IOReturn ret = mapCachedAlias();
	if (ret == kIOReturnSuccess) {
		v3d_dcache_civac(fCachedVA + offset, size);
	}
	return ret;
}

/*
 * kV3DMethodShareBO. The segment gets a named entry of its own over the
 * BO's physical pages: a physical IOMemoryDescriptor with an explicit
 * copyback cache mode, whose device pager maps the pages with that mode
 * (memoryReferenceCreate's physical branch; the pmap takes an explicit
 * WIMG per mapping, osfmk/arm/pmap.c pmap_enter_options_internal), so the
 * BO's other mappings keep write-combining. The segment holds a reference
 * on this buffer and on the descriptor until it is destroyed
 * (shm_share_released): until then no mapping of the pages can remain, so
 * the pages cannot go back to the pool while someone can still read them.
 */
struct V3DShmShare {
	V3DBuffer *buffer;
	IOMemoryDescriptor *desc;
};

extern "C" IOReturn IOGeneralMemoryDescriptorCopyNamedEntry(IOMemoryDescriptor *md, ipc_port_t *entry, mach_vm_size_t *size);
extern "C" int sysv_shm_set_backing(int shmid, ipc_port_t entry, mach_vm_size_t size,
    void (*release)(void *), void *arg);
extern "C" void mach_memory_entry_port_release(ipc_port_t port);

static void
shm_share_released(void *arg)
{
	V3DShmShare *s = (V3DShmShare *)arg;
	s->desc->release();
	s->buffer->release();
	IODelete(s, V3DShmShare, 1);
}

IOReturn
V3DBuffer::shareWithShm(int shmid)
{
	if (fMem == NULL || fShader != NULL) {
		return kIOReturnNotPermitted;
	}
	IOMemoryDescriptor *desc = IOMemoryDescriptor::withAddressRange(fPhys, round_page(fSize),
	    kIODirectionInOut | (kIOCopybackCache << kIOMemoryBufferCacheShift), TASK_NULL);
	if (desc == NULL) {
		return kIOReturnNoMemory;
	}
	ipc_port_t entry = IPC_PORT_NULL;
	mach_vm_size_t size = 0;
	IOReturn ret = IOGeneralMemoryDescriptorCopyNamedEntry(desc, &entry, &size);
	if (ret != kIOReturnSuccess) {
		desc->release();
		return ret;
	}
	V3DShmShare *s = IONew(V3DShmShare, 1);
	if (s == NULL) {
		mach_memory_entry_port_release(entry);
		desc->release();
		return kIOReturnNoMemory;
	}
	retain();
	s->buffer = this;
	s->desc = desc;
	int err = sysv_shm_set_backing(shmid, entry, size, shm_share_released, s);
	if (err != 0) {
		mach_memory_entry_port_release(entry);
		shm_share_released(s);
		return err == EBUSY ? kIOReturnBusy : err == EACCES || err == EPERM ? kIOReturnNotPrivileged : kIOReturnBadArgument;
	}
	return kIOReturnSuccess;
}

V3DBuffer *
V3DBuffer::empty(void)
{
	V3DBuffer *b = new V3DBuffer;
	if (b != NULL && !b->init()) {
		b->release();
		b = NULL;
	}
	return b;
}

/*
 * The GPU memory pool. The V3D has no MMU, so every BO is physically
 * contiguous. Allocated one by one, the large ones stopped being available
 * once memory was fragmented: on a real Pi 3 (2026-10-17) a 1918x1057
 * window's 8 MB buffers failed in vm_page_find_contiguous ("found 0 pages",
 * after scanning 233729 pages and a zone_gc) on every frame, so the window
 * stayed black, and the scan cost every GL client its frame rate (glxgears
 * at 1280x720: 45 fps on unfragmented memory, 14 after). Linux reserves CMA
 * for the same reason. So: one contiguous block, reserved when the driver
 * starts (before memory fragments), the BOs carved out of it in page-sized
 * units (PAGE_SIZE: a slice must map into a client on page boundaries); a
 * BO falls back to its own allocation only when the pool is full. Size:
 * boot-arg v3d-pool=<MiB>, default kGpuPoolDefaultMB; 0 disables it.
 * Like every BO it is write-combined, below 1 GiB and inside one 256 MiB
 * window (see withSize()).
 */
static IOBufferMemoryDescriptor *sGpuPool;
static volatile uint8_t *sGpuPoolKva;
static uint64_t sGpuPoolPhys;
static uint32_t sGpuPoolPages;
static uint32_t *sGpuPoolBits;          /* one bit per page, 1 = used */
static IOSimpleLock *sGpuPoolLock;

static bool
pool_page_used(uint32_t i)
{
	return (sGpuPoolBits[i / 32] >> (i % 32)) & 1;
}

static void
pool_mark(uint32_t first, uint32_t n, bool used)
{
	for (uint32_t i = first; i < first + n; i++) {
		if (used) {
			sGpuPoolBits[i / 32] |= 1U << (i % 32);
		} else {
			sGpuPoolBits[i / 32] &= ~(1U << (i % 32));
		}
	}
}

/* First fit; returns the first page, or sGpuPoolPages if no run is free. */
static uint32_t
pool_alloc(uint32_t n)
{
	uint32_t found = sGpuPoolPages;
	IOInterruptState s = IOSimpleLockLockDisableInterrupt(sGpuPoolLock);
	uint32_t run = 0;
	for (uint32_t i = 0; i < sGpuPoolPages; i++) {
		if (pool_page_used(i)) {
			run = 0;
			continue;
		}
		if (++run == n) {
			found = i + 1 - n;
			pool_mark(found, n, true);
			break;
		}
	}
	IOSimpleLockUnlockEnableInterrupt(sGpuPoolLock, s);
	return found;
}

static void
pool_free(uint32_t first, uint32_t n)
{
	if (sGpuPoolBits == NULL) {
		return;          // the driver stopped (freeGpuPool) while the client still held this BO
	}
	IOInterruptState s = IOSimpleLockLockDisableInterrupt(sGpuPoolLock);
	pool_mark(first, n, false);
	IOSimpleLockUnlockEnableInterrupt(sGpuPoolLock, s);
}

bool
V3DBuffer::initGpuPool(uint32_t bytes)
{
	if (sGpuPool != NULL || bytes == 0) {
		return sGpuPool != NULL;
	}
	bytes = (bytes + PAGE_SIZE - 1) & ~(uint32_t)(PAGE_SIZE - 1);
	const uint64_t mask = 0x3FFFF000ULL;
	IOBufferMemoryDescriptor *rejected[8];
	uint32_t nRejected = 0;
	IOBufferMemoryDescriptor *pool = NULL;
	for (;;) {
		IOBufferMemoryDescriptor *m = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
		    kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMapWriteCombineCache, bytes, mask);
		if (m == NULL) {
			break;
		}
		uint64_t pa0 = m->getPhysicalAddress();
		if ((pa0 & 0xf0000000ULL) == ((pa0 + bytes - 1) & 0xf0000000ULL) && pa0 + bytes <= kV3D_BusAliasSize) {
			pool = m;
			break;
		}
		if (nRejected == sizeof(rejected) / sizeof(rejected[0])) {
			m->release();
			break;
		}
		rejected[nRejected++] = m;
	}
	while (nRejected > 0) {
		rejected[--nRejected]->release();
	}
	if (pool == NULL) {
		return false;
	}
	uint32_t pages = bytes / PAGE_SIZE;
	uint32_t *bits = (uint32_t *)IOMallocZero((pages + 31) / 32 * sizeof(uint32_t));
	IOSimpleLock *lock = IOSimpleLockAlloc();
	if (bits == NULL || lock == NULL) {
		if (bits != NULL) {
			IOFree(bits, (pages + 31) / 32 * sizeof(uint32_t));
		}
		if (lock != NULL) {
			IOSimpleLockFree(lock);
		}
		pool->release();
		return false;
	}
	sGpuPoolBits = bits;
	sGpuPoolLock = lock;
	sGpuPoolPages = pages;
	sGpuPoolKva = (volatile uint8_t *)pool->getBytesNoCopy();
	sGpuPoolPhys = pool->getPhysicalAddress();
	sGpuPool = pool;
	return true;
}

void
V3DBuffer::freeGpuPool(void)
{
	if (sGpuPool == NULL) {
		return;
	}
	IOFree(sGpuPoolBits, (sGpuPoolPages + 31) / 32 * sizeof(uint32_t));
	IOSimpleLockFree(sGpuPoolLock);
	sGpuPool->release();
	sGpuPool = NULL;
	sGpuPoolBits = NULL;
	sGpuPoolLock = NULL;
	sGpuPoolPages = 0;
}

V3DBuffer *
V3DBuffer::withSize(uint32_t size)
{
	if (size == 0 || size > kMaxBOSize) {
		return NULL;
	}
	size = (size + PAGE_SIZE - 1) & ~(uint32_t)(PAGE_SIZE - 1);

	V3DBuffer *b = new V3DBuffer;
	if (b == NULL) {
		return NULL;
	}
	if (!b->init()) {
		b->release();
		return NULL;
	}
	// Physically contiguous (the GPU has no MMU), write-combined, and below
	// the 0x3f000000 VideoCore bus-alias window. Mask 0x3FFFF000: page-
	// aligned addresses up to 1 GiB, inside the dma-ranges window.
	//
	// And not crossing a 256 MiB boundary: vc4_v3d_allocate_bin_bo()
	// (vc4_v3d.c:247-273) retries until the binner's memory "won't trigger
	// the addressing bug" ((addr & 0xf0000000) == ((addr + size - 1) &
	// 0xf0000000)). Every BO gets that rule; like Linux, failed candidates
	// are held until one fits so the next attempt cannot return the same
	// memory.
	//
	// Binner memory (tile state, tile alloc, overflow) is not allocated
	// here: it comes from one pool, see allocBinSlice().
	const uint64_t mask = 0x3FFFF000ULL;
	if (sGpuPool != NULL) {
		uint32_t n = size / PAGE_SIZE;
		uint32_t first = pool_alloc(n);
		if (first < sGpuPoolPages) {
			IOMemoryDescriptor *sub = IOSubMemoryDescriptor::withSubRange(sGpuPool,
			    (IOByteCount)first * PAGE_SIZE, (IOByteCount)n * PAGE_SIZE, kIODirectionInOut);
			if (sub != NULL) {
				b->fMem = sub;
				b->fKva = sGpuPoolKva + (size_t)first * PAGE_SIZE;
				b->fPhys = sGpuPoolPhys + (uint64_t)first * PAGE_SIZE;
				b->fGpuPoolFirst = first;
				b->fGpuPoolPages = n;
			} else {
				pool_free(first, n);
			}
		}
	}
	if (b->fMem == NULL) {
		IOBufferMemoryDescriptor *rejected[8];
		uint32_t nRejected = 0;
		for (;;) {
			IOBufferMemoryDescriptor *m = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
			    kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMapWriteCombineCache,
			    size, mask);
			if (m == NULL) {
				break;
			}
			uint64_t pa0 = m->getPhysicalAddress();
			if ((pa0 & 0xf0000000ULL) == ((pa0 + size - 1) & 0xf0000000ULL)) {
				b->fMem = m;
				b->fKva = (volatile uint8_t *)m->getBytesNoCopy();
				b->fPhys = pa0;
				break;
			}
			if (nRejected == sizeof(rejected) / sizeof(rejected[0])) {
				m->release();
				break;
			}
			rejected[nRejected++] = m;
		}
		while (nRejected > 0) {
			rejected[--nRejected]->release();
		}
	}
	if (b->fMem == NULL) {
		b->release();
		return NULL;
	}
	b->fSize = size;
	// Never hand a client someone else's stale memory. Note this does NOT
	// avoid the QEMU-only DC ZVA stall seen in testing: that stall is in
	// IOBufferMemoryDescriptor's own bzero() (IOBufferMemoryDescriptor.cpp:331)
	// during allocation, before this loop runs (docs/gpu-opengl-plan.md).
	{
		volatile uint64_t *w = (volatile uint64_t *)b->fKva;
		for (uint32_t i = 0; i < size / 8; i++) {
			w[i] = 0;
		}
	}
	cpuToGpuBarrier();

	IOPhysicalAddress pa = (IOPhysicalAddress)b->fPhys;
	if ((uint64_t)pa + size > kV3D_BusAliasSize) {
		IOLog("V3DBuffer: physical %#llx+%#x outside the GPU window\n", (uint64_t)pa, size);
		b->release();
		return NULL;
	}
	b->fGpuAddr = (uint32_t)pa + kV3D_BusAliasBase;
	b->fBusySeqno = 0;
	b->fWriteSeqno = 0;
	return b;
}

void
V3DBuffer::free(void)
{
	if (fShader != NULL) {
		v3d_kglue_free_shader(fShader);
		fShader = NULL;
	}
	if (fCachedVA != 0) {
		ml_io_unmap(fCachedVA, fCachedLen);
		fCachedVA = 0;
	}
	if (fPoolOwner != NULL) {
		fPoolOwner->freeBinSlots(fPoolSlot, fPoolSlots);
		fPoolOwner->release();
		fPoolOwner = NULL;
	}
	if (fMem != NULL) {
		fMem->release();
		fMem = NULL;
	}
	if (fGpuPoolPages != 0) {
		pool_free(fGpuPoolFirst, fGpuPoolPages);
		fGpuPoolPages = 0;
	}
	OSObject::free();
}

/* ============================== power ============================== */

/*
 * Linux bcm2835_asb_control() (bcm2835-power.c:154-182): clear/set REQ_STOP
 * with the PM password in the top byte, then wait for ACK to follow.
 */
bool
BCM2837V3D::asbControl(uint32_t reg, bool enable)
{
	volatile uint32_t *p = (volatile uint32_t *)(fASB + reg);
	uint32_t val = *p;
	val = enable ? (val & ~kASB_REQ_STOP) : (val | kASB_REQ_STOP);
	*p = kPMPassword | val;
	for (int i = 0; i < 1000; i++) {
		if (((*p & kASB_ACK) != 0) == enable) {
			return true;
		}
		IODelay(1);
	}
	return false;
}

/*
 * Who powers the V3D.
 *
 * On a board running the Raspberry Pi firmware the VideoCore owns the PM
 * block, and Linux never touches PM_GRAFX or the ASB bridges: every Pi DT
 * (bcm2837-rpi-3-b.dts -> bcm2836-rpi.dtsi -> bcm2835-rpi-common.dtsi:23-25,
 * and the downstream bcm270x.dtsi:140-144) gives the v3d node
 * `power-domains = <&power RPI_POWER_DOMAIN_V3D>`, served by
 * drivers/pmdomain/bcm/raspberrypi-power.c, which asks the firmware with
 * RPI_FIRMWARE_SET_DOMAIN_STATE. The PM-block sequence (bcm2835-power.c) is
 * only for boards without that firmware. Found on the first real Pi 3 boot:
 * with the PM-block sequence GRAFX took POWUP but POWOK never rose
 * (PM_GRAFX 0x1000 -> 0x1001 at every inrush step, also after a 10 ms wait
 * and with the firmware's V3D clock on).
 *
 * The firmware path is used when the firmware has the domain interface;
 * otherwise (no Raspberry Pi firmware, e.g. the QEMU V3D model) the
 * PM-block path.
 */
enum {
	kTagGetDomainState = 0x00030030,   // RPI_FIRMWARE_GET_DOMAIN_STATE
	kTagSetDomainState = 0x00038030,   // RPI_FIRMWARE_SET_DOMAIN_STATE
	// RPI_POWER_DOMAIN_V3D is 10 in dt-bindings/power/raspberrypi-power.h,
	// and the firmware's index is that plus one (raspberrypi-power.c:112-113,
	// "The DT binding index is the firmware's domain index minus one").
	kFwDomainV3D = 10 + 1,
	kRpiPowerDomainARM = 22,           // RPI_POWER_DOMAIN_ARM, used as-is by the probe
};

/*
 * rpi_has_new_domain_support() (raspberrypi-power.c:130-152): the firmware
 * does not fail an unknown tag, it skips it, so put ~0 in the answer word
 * and see whether the firmware overwrote it.
 */
static bool
firmwareHasDomainInterface(void)
{
	uint32_t resp = 0, a = 0, b = 0;
	if (!BCM2837Mailbox::propertyTag2(kTagGetDomainState, kRpiPowerDomainARM, ~0U, &resp, &a, &b)) {
		return false;
	}
	return b != ~0U;
}

/* rpi_firmware_set_power() (raspberrypi-power.c:51-62), new interface. */
static bool
firmwareSetV3DPower(bool on)
{
	uint32_t resp = 0, a = 0, b = 0;
	if (!BCM2837Mailbox::propertyTag2(kTagSetDomainState, kFwDomainV3D, on ? 1 : 0, &resp, &a, &b)) {
		return false;
	}
	return (resp & 0x80000000U) != 0;
}

bool
BCM2837V3D::powerOn(void)
{
	if (fFwPowerChecked == false) {
		fFwPower = firmwareHasDomainInterface();
		fFwPowerChecked = true;
		IOLog("BCM2837V3D: power through %s\n", fFwPower ?
		    "the firmware (domain V3D)" : "the PM block (no firmware domain interface)");
	}
	if (fFwPower) {
		if (!firmwareSetV3DPower(true)) {
			IOLog("BCM2837V3D: firmware would not power the V3D domain\n");
			return false;
		}
		fPowered = true;
		return true;
	}
	return powerOnPM();
}

void
BCM2837V3D::powerOff(void)
{
	if (fFwPower) {
		fPowered = false;
		if (!firmwareSetV3DPower(false)) {
			IOLog("BCM2837V3D: firmware would not power the V3D domain off\n");
		}
		return;
	}
	powerOffPM();
}

/*
 * Boards without the Raspberry Pi firmware: the V3D power-on path of Linux's
 * bcm2835_power_pd_power_on() for BCM2835_POWER_DOMAIN_GRAFX_V3D
 * (bcm2835-power.c:366-380), preceded by the GRAFX domain's own
 * bcm2835_power_power_on(PM_GRAFX) (:214-275) which Linux runs because the
 * V3D domain's parent is GRAFX.
 */
bool
BCM2837V3D::powerOnPM(void)
{
	if ((pmRead(kPM_GRAFX) & kPM_POWUP) == 0) {
		// Firmware did not leave GRAFX powered. Ramp the inrush current up
		// until POWOK sets (:236-254; Linux loops all four values without a
		// break, here the first success ends the ramp).
		bool powok = false;
		for (uint32_t inrush = 0; inrush <= 3 && !powok; inrush++) {
			pmWrite(kPM_GRAFX, (pmRead(kPM_GRAFX) & ~kPM_InrushMask) |
			    (inrush << kPM_InrushShift) | kPM_POWUP);
			for (int i = 0; i < 100; i++) {
				if (pmRead(kPM_GRAFX) & kPM_POWOK) {
					powok = true;
					break;
				}
				IODelay(1);
			}
		}
		if (!powok) {
			IOLog("BCM2837V3D: GRAFX power never reported POWOK (PM_GRAFX %#x)\n", pmRead(kPM_GRAFX));
			pmWrite(kPM_GRAFX, pmRead(kPM_GRAFX) & ~(kPM_POWUP | kPM_InrushMask));
			return false;
		}
		pmWrite(kPM_GRAFX, pmRead(kPM_GRAFX) | kPM_ISPOW);
		pmWrite(kPM_GRAFX, pmRead(kPM_GRAFX) | kPM_MEMREP);
		bool mrdone = false;
		for (int i = 0; i < 100; i++) {
			if (pmRead(kPM_GRAFX) & kPM_MRDONE) {
				mrdone = true;
				break;
			}
			IODelay(1);
		}
		if (!mrdone) {
			IOLog("BCM2837V3D: GRAFX memory repair timed out\n");
			pmWrite(kPM_GRAFX, pmRead(kPM_GRAFX) & ~kPM_ISPOW);
			pmWrite(kPM_GRAFX, pmRead(kPM_GRAFX) & ~(kPM_POWUP | kPM_InrushMask));
			return false;
		}
		pmWrite(kPM_GRAFX, pmRead(kPM_GRAFX) | kPM_ISFUNC);
	}

	// bcm2835_asb_power_on() (:277-330): pulse the V3D clock so the reset
	// propagates (Linux: "Wait 32 clocks ... 1 us will be enough"), release
	// the reset, re-enable the clock, then enable the master and slave
	// AXI bridges.
	if (!BCM2837Mailbox::setClockState(BCM2837Mailbox::kClockIdV3D, true)) {
		IOLog("BCM2837V3D: firmware would not enable the V3D clock\n");
		return false;
	}
	IODelay(1);
	BCM2837Mailbox::setClockState(BCM2837Mailbox::kClockIdV3D, false);
	pmWrite(kPM_GRAFX, pmRead(kPM_GRAFX) | kPM_V3DRSTN);
	if (!BCM2837Mailbox::setClockState(BCM2837Mailbox::kClockIdV3D, true)) {
		IOLog("BCM2837V3D: firmware would not re-enable the V3D clock\n");
		pmWrite(kPM_GRAFX, pmRead(kPM_GRAFX) & ~kPM_V3DRSTN);
		return false;
	}
	if (!asbControl(kASB_V3D_M_CTRL, true)) {
		IOLog("BCM2837V3D: ASB master did not ack\n");
		goto fail_clk;
	}
	if (!asbControl(kASB_V3D_S_CTRL, true)) {
		IOLog("BCM2837V3D: ASB slave did not ack\n");
		asbControl(kASB_V3D_M_CTRL, false);
		goto fail_clk;
	}
	fPowered = true;
	return true;

fail_clk:
	BCM2837Mailbox::setClockState(BCM2837Mailbox::kClockIdV3D, false);
	pmWrite(kPM_GRAFX, pmRead(kPM_GRAFX) & ~kPM_V3DRSTN);
	return false;
}

/* bcm2835_asb_power_off() (:332-358): slave, master, clock, reset. */
void
BCM2837V3D::powerOffPM(void)
{
	fPowered = false;
	if (!asbControl(kASB_V3D_S_CTRL, false)) {
		IOLog("BCM2837V3D: ASB slave did not stop\n");
	}
	if (!asbControl(kASB_V3D_M_CTRL, false)) {
		IOLog("BCM2837V3D: ASB master did not stop\n");
	}
	BCM2837Mailbox::setClockState(BCM2837Mailbox::kClockIdV3D, false);
	pmWrite(kPM_GRAFX, pmRead(kPM_GRAFX) & ~kPM_V3DRSTN);
}

/* vc4_v3d_init_hw() (vc4_v3d.c:164-173) + vc4_irq_enable() (vc4_irq.c). */
void
BCM2837V3D::initHW(void)
{
	// Hand the QPU-user-program VPM space to the rasteriser; there is no
	// interface for running user QPU programs.
	v3dWrite(kV3D_VPMBASE, 0);
	if (fBinPool != NULL) {
		v3dWrite(kV3D_BPOA, fBinPool->fGpuAddr);
		v3dWrite(kV3D_BPOS, kBinOverflow);
	}
	v3dWrite(kV3D_INTCTL, kV3D_DriverIRQs);          // clear stale
	v3dWrite(kV3D_INTENA, kV3D_INT_FLDONE | kV3D_INT_FRDONE);
}

/* ============================ job pipeline ============================ */

/* vc4_flush_caches() / vc4_flush_texture_caches() (vc4_gem.c:439-465) */
void
BCM2837V3D::flushCaches(void)
{
	v3dWrite(kV3D_L2CACTL, kV3D_L2CACTL_L2CCLR);
	v3dWrite(kV3D_SLCACTL, kV3D_SLCACTL_ClearAll);
}

void
BCM2837V3D::flushTextureCaches(void)
{
	v3dWrite(kV3D_L2CACTL, kV3D_L2CACTL_L2CCLR);
	v3dWrite(kV3D_SLCACTL, kV3D_SLCACTL_ClearTex);
}

// Hardware callbacks for V3DQueue. Called with the job lock held.
static void hwStartBin(void *ctx, uint32_t ca, uint32_t ea)
{
	BCM2837V3D *d = (BCM2837V3D *)ctx;
	cpuToGpuBarrier();
	// Writing the end address is what starts the job (submit_cl(),
	// vc4_gem.c:376-385).
	d->v3dWriteReg(kV3D_CT0CA, ca);
	d->v3dWriteReg(kV3D_CT0EA, ea);
}

static void hwStartRender(void *ctx, uint32_t ca, uint32_t ea)
{
	BCM2837V3D *d = (BCM2837V3D *)ctx;
	cpuToGpuBarrier();
	d->v3dWriteReg(kV3D_CT1CA, ca);
	d->v3dWriteReg(kV3D_CT1EA, ea);
}

static void hwFlushCaches(void *ctx) { ((BCM2837V3D *)ctx)->flushCaches(); }
static void hwFlushTextureCaches(void *ctx) { ((BCM2837V3D *)ctx)->flushTextureCaches(); }

void
BCM2837V3D::interruptOccurred(OSObject *target, void *, IOService *, int)
{
	BCM2837V3D *self = OSDynamicCast(BCM2837V3D, target);
	if (self == NULL || !self->fPowered) {
		return;
	}
	uint32_t intctl = self->v3dRead(kV3D_INTCTL);
	if (intctl == 0) {
		return;
	}
	self->v3dWrite(kV3D_INTCTL, intctl);     // write-1-to-clear what we saw

	if (intctl & kV3D_INT_FLDONE) {
		IOInterruptState s = IOSimpleLockLockDisableInterrupt(self->fJobLock);
		self->fQueue->binDone();
		IOSimpleLockUnlockEnableInterrupt(self->fJobLock, s);
	}
	if (intctl & kV3D_INT_FRDONE) {
		IOInterruptState s = IOSimpleLockLockDisableInterrupt(self->fJobLock);
		self->fQueue->renderDone();
		IOSimpleLockUnlockEnableInterrupt(self->fJobLock, s);
	}
	// kV3D_INT_OUTOMEM is never enabled (see header).
}

void
BCM2837V3D::freeJob(V3DJob *j)
{
	for (uint32_t i = 0; i < j->boCount; i++) {
		if (j->bos[i] != NULL) {
			j->bos[i]->release();
		}
	}
	if (j->bos != NULL) {
		IOFree(j->bos, j->boCount * sizeof(V3DBuffer *));
	}
	IOFree(j, sizeof(*j));
}

/* =============================== hang =============================== */

/* vc4_save_hang_state() (vc4_gem.c) reduced to the register snapshot. */
void
BCM2837V3D::saveHangState(void)
{
	v3d_hang_state h;
	bzero(&h, sizeof(h));
	h.valid = 1;
	h.ct0ca = v3dRead(kV3D_CT0CA); h.ct0ea = v3dRead(kV3D_CT0EA);
	h.ct1ca = v3dRead(kV3D_CT1CA); h.ct1ea = v3dRead(kV3D_CT1EA);
	h.ct0cs = v3dRead(kV3D_CT0CS); h.ct1cs = v3dRead(kV3D_CT1CS);
	h.ct0ra0 = v3dRead(kV3D_CT00RA0); h.ct1ra0 = v3dRead(kV3D_CT01RA0);
	h.bpca = v3dRead(kV3D_BPCA); h.bpcs = v3dRead(kV3D_BPCS);
	h.bpoa = v3dRead(kV3D_BPOA); h.bpos = v3dRead(kV3D_BPOS);
	h.vpmbase = v3dRead(kV3D_VPMBASE);
	h.dbge = v3dRead(kV3D_DBGE); h.fdbgo = v3dRead(kV3D_FDBGO);
	h.fdbgb = v3dRead(kV3D_FDBGB); h.fdbgr = v3dRead(kV3D_FDBGR);
	h.fdbgs = v3dRead(kV3D_FDBGS); h.errstat = v3dRead(kV3D_ERRSTAT);
	IOInterruptState s = IOSimpleLockLockDisableInterrupt(fJobLock);
	const V3DJob *oldest = fQueue->oldestInFlight();
	h.seqno = oldest ? oldest->seqno : 0;
	fHang = h;
	IOSimpleLockUnlockEnableInterrupt(fJobLock, s);
}

/*
 * vc4_reset() + vc4_irq_reset() (vc4_gem.c:293-318, vc4_irq.c). Power-cycle
 * the block, then fail the job that was on the hardware. Linux completes the
 * render job (vc4_irq_finish_render_job) and re-runs the bin job; the same
 * is done here, except that a hung bin job with an empty render queue is
 * failed as well (there is nothing else it could be waiting on).
 */
void
BCM2837V3D::resetHW(void)
{
	IOLog("BCM2837V3D: GPU hang, resetting\n");
	saveHangState();
	v3dWrite(kV3D_INTDIS, kV3D_DriverIRQs);
	powerOff();
	if (!powerOn()) {
		IOLog("BCM2837V3D: power-up after reset FAILED; GPU left off\n");
	} else {
		initHW();
	}

	IOInterruptState s = IOSimpleLockLockDisableInterrupt(fJobLock);
	fQueue->resetDone(fPowered);
	IOSimpleLockUnlockEnableInterrupt(fJobLock, s);
}

void
BCM2837V3D::queueHangcheck(void)
{
	if (fHangTimer != NULL && !fStopping) {
		fHangTimer->setTimeoutMS(kHangcheckMs);
	}
}

/* vc4_hangcheck_elapsed() (vc4_gem.c:330-372), plus freeing finished jobs. */
void
BCM2837V3D::hangcheckTick(OSObject *owner, IOTimerEventSource *)
{
	BCM2837V3D *self = OSDynamicCast(BCM2837V3D, owner);
	if (self == NULL || self->fStopping) {
		return;
	}

	IOInterruptState s = IOSimpleLockLockDisableInterrupt(self->fJobLock);
	V3DJob *done = self->fQueue->takeDone();
	bool busy = !self->fQueue->idle();
	IOSimpleLockUnlockEnableInterrupt(self->fJobLock, s);

	while (done != NULL) {
		V3DJob *n = done->next;
		self->freeJob(done);
		done = n;
	}
	if (!busy) {
		return;      // submitRaw() re-arms the timer on the next job
	}

	uint32_t ct0ca = self->v3dRead(kV3D_CT0CA);
	uint32_t ct1ca = self->v3dRead(kV3D_CT1CA);
	if (ct0ca == self->fLastCt0ca && ct1ca == self->fLastCt1ca) {
		self->resetHW();
	} else {
		self->fLastCt0ca = ct0ca;
		self->fLastCt1ca = ct1ca;
	}
	self->queueHangcheck();
}

/* ============================ client interface ============================ */

IOReturn
BCM2837V3D::getParam(uint32_t param, uint64_t *value)
{
	switch (param) {
	case kV3DParamIdent0: *value = fIdent0; break;
	case kV3DParamIdent1: *value = fIdent1; break;
	case kV3DParamIdent2: *value = fIdent2; break;
	// The hardware can do all three (Linux reports 1 for each on vc4).
	case kV3DParamSupportsBranches:
	case kV3DParamSupportsETC1:
	case kV3DParamSupportsThreadedFS: *value = 1; break;
	// Only once the validated-submit path exists.
	case kV3DParamSupportsFixedRCLOrder: *value = 1; break;   // the vendored render-list generator honours it
	case kV3DParamSupportsMadvise:
	case kV3DParamSupportsPerfmon: *value = 0; break;
	default: return kIOReturnBadArgument;
	}
	return kIOReturnSuccess;
}

static bool
rangeInBO(uint32_t start, uint32_t end, V3DBuffer **bos, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		uint64_t lo = bos[i]->fGpuAddr, hi = lo + bos[i]->fSize;
		if (start >= lo && end <= hi) {
			return true;
		}
	}
	return false;
}

IOReturn
BCM2837V3D::submitRaw(const v3d_raw_submit *in, V3DBuffer **bos, uint64_t *seqnoOut)
{
	if (!fPowered || fStopping) {
		return kIOReturnNotReady;
	}
	if (in->bo_count > V3D_RAW_MAX_BOS) {
		return kIOReturnBadArgument;
	}
	// Even an unvalidated submit must not point the GPU outside the
	// buffers the caller named: both lists have to lie inside one of them,
	// 4-byte aligned, and the render list is mandatory (an empty one never
	// raises FRDONE, so the job could never complete).
	if (in->bin_start > in->bin_end || in->render_start >= in->render_end ||
	    ((in->bin_start | in->bin_end | in->render_start | in->render_end) & 3) != 0) {
		return kIOReturnBadArgument;
	}
	if (in->bin_start != in->bin_end && !rangeInBO(in->bin_start, in->bin_end, bos, in->bo_count)) {
		return kIOReturnBadArgument;
	}
	if (!rangeInBO(in->render_start, in->render_end, bos, in->bo_count)) {
		return kIOReturnBadArgument;
	}

	V3DJob *j = (V3DJob *)IOMalloc(sizeof(V3DJob));
	if (j == NULL) {
		return kIOReturnNoMemory;
	}
	bzero(j, sizeof(*j));
	j->ct0ca = in->bin_start;   j->ct0ea = in->bin_end;
	j->ct1ca = in->render_start; j->ct1ea = in->render_end;
	if (in->bo_count != 0) {
		j->bos = (V3DBuffer **)IOMalloc(in->bo_count * sizeof(V3DBuffer *));
		if (j->bos == NULL) {
			IOFree(j, sizeof(*j));
			return kIOReturnNoMemory;
		}
		j->boCount = in->bo_count;
		for (uint32_t i = 0; i < in->bo_count; i++) {
			j->bos[i] = bos[i];
			bos[i]->retain();
		}
	}
	return queueJob(j, seqnoOut);
}

/*
 * Takes ownership of `j` (freed on every path), assigns its seqno and starts
 * it. The seqno is read before the lock is dropped: once the job is queued
 * it can complete, and be reaped, at any time.
 */
IOReturn
BCM2837V3D::queueJob(V3DJob *j, uint64_t *seqnoOut, V3DBuffer *const *writes, uint32_t nWrites)
{
	if (!fPowered || fStopping) {
		freeJob(j);
		return kIOReturnNotReady;
	}
	IOInterruptState s = IOSimpleLockLockDisableInterrupt(fJobLock);
	bool wasIdle = fQueue->idle();
	if (wasIdle) {
		// Nothing is binning, so the overflow memory can be handed out from
		// the start again (see resetBinnerOverflow()).
		resetBinnerOverflow();
	}
	uint64_t seqno = fQueue->submit(j);
	for (uint32_t i = 0; i < j->boCount; i++) {
		j->bos[i]->fBusySeqno = seqno;
	}
	for (uint32_t i = 0; i < nWrites; i++) {
		writes[i]->fWriteSeqno = seqno;
	}
	IOSimpleLockUnlockEnableInterrupt(fJobLock, s);

	*seqnoOut = seqno;
	if (wasIdle) {
		fLastCt0ca = fLastCt1ca = 0xffffffffU;
		queueHangcheck();
	}
	return kIOReturnSuccess;
}

static void copyStagingToBO(V3DBuffer *b, const void *src, size_t n);

IOReturn
BCM2837V3D::createShaderBO(const void *code, uint32_t size, V3DBuffer **out)
{
	if (size == 0 || (size & 7) != 0 || size > 64 * 1024) {
		return kIOReturnBadArgument;
	}
	struct vc4_validated_shader_info *info = v3d_kglue_validate_shader(code, size);
	if (info == NULL) {
		return kIOReturnNotPermitted;           // the program is not provably safe
	}
	V3DBuffer *b = V3DBuffer::withSize(size);
	if (b == NULL) {
		v3d_kglue_free_shader(info);
		return kIOReturnNoMemory;
	}
	copyStagingToBO(b, code, size);
	b->fShader = info;
	*out = b;
	return kIOReturnSuccess;
}

/*
 * Binner overflow memory. When a frame's tile lists outgrow the job's own
 * tile BO the binner takes blocks from here (BPOA = base, BPOS = size, as
 * vc4_v3d.c / vc4_irq.c program them). Linux reallocates on the OUTOMEM
 * interrupt; this driver keeps one pool and rewinds it whenever the binner
 * is idle, and leaves OUTOMEM masked: a frame that needs more than the pool
 * stalls and is reset by the hang detector instead of being served.
 * Job lock held.
 */
void
BCM2837V3D::resetBinnerOverflow(void)
{
	if (fBinPool != NULL) {
		v3dWrite(kV3D_BPOA, fBinPool->fGpuAddr);
		v3dWrite(kV3D_BPOS, kBinOverflow);
	}
}

/* ---- validated submit ---- */

namespace {
struct V3DCreated {
	V3DBuffer *buf;
	void *staging;
	size_t stagingSize;
};
struct V3DAllocCtx {
	BCM2837V3D *dev;
	V3DCreated list[8];       // exec, tile and render-list BOs: three in practice
	uint32_t n;
};
}

/*
 * Binner memory, Linux's design (553c942f8b2c, vc4_v3d.c
 * vc4_v3d_allocate_bin_bo / bin_alloc_size, vc4_gem.c
 * vc4_v3d_get_bin_slot): ONE buffer allocated when the driver starts, not
 * crossing a 256 MiB boundary (the binner takes the top 4 address bits of
 * tile alloc and overflow memory from the tile state array), handed out in
 * 512 KiB slots. Each job's tile state + tile alloc BO is a run of slots;
 * the first 4 MiB are the overflow pool (BPOA/BPOS). A fresh contiguous
 * allocation per job does not work: on the real Pi 3 the 1 MiB tile BOs
 * glxgears needs failed (vm_page_find_contiguous found nothing) once memory
 * was fragmented, and the job was rejected.
 */
V3DBuffer *
BCM2837V3D::allocBinSlice(uint32_t size)
{
	if (fBinPool == NULL || size == 0) {
		return NULL;
	}
	uint32_t n = (size + kBinSlotSize - 1) / kBinSlotSize;
	if (n > kBinSlotCount) {
		return NULL;
	}
	uint32_t first = kBinSlotCount;
	IOInterruptState s = IOSimpleLockLockDisableInterrupt(fBinLock);
	uint32_t want = (n == 32) ? ~0U : ((1U << n) - 1);
	for (uint32_t i = 0; i + n <= kBinSlotCount; i++) {
		if ((fBinSlotsUsed & (want << i)) == 0) {
			fBinSlotsUsed |= want << i;
			first = i;
			break;
		}
	}
	IOSimpleLockUnlockEnableInterrupt(fBinLock, s);
	if (first == kBinSlotCount) {
		return NULL;
	}
	V3DBuffer *b = V3DBuffer::empty();
	if (b == NULL) {
		freeBinSlots(first, n);
		return NULL;
	}
	uint32_t off = kBinOverflow + first * kBinSlotSize;
	b->fMem = NULL;
	b->fSize = n * kBinSlotSize;
	b->fGpuAddr = fBinPool->fGpuAddr + off;
	b->fBusySeqno = 0;
	b->fWriteSeqno = 0;
	b->fPoolSlot = first;
	b->fPoolSlots = n;
	b->fPoolOwner = this;
	retain();
	// The tile state data array must start zeroed for every bin job (Linux
	// 48a570c964d8: the PTB acts on stale tile state left by a slot's
	// previous user despite the auto-initialise flag). Slots are reused, so
	// clear what the job asked for, with plain stores on the write-combined
	// pool mapping.
	volatile uint64_t *w = (volatile uint64_t *)(fBinPool->fKva + off);
	for (uint32_t i = 0; i < (size + 7) / 8; i++) {
		w[i] = 0;
	}
	cpuToGpuBarrier();
	return b;
}

/*
 * vc4_v3d_get_bin_slot() (vc4_v3d.c): "Couldn't find an open slot. Wait for
 * render to complete and try again." Finished jobs only give their slots
 * back when they are freed, which hangcheckTick() does every 100 ms; at
 * 60+ frames a second that left the pool empty and about a third of
 * glxgears' frames were rejected on the real Pi 3. So: free what has
 * finished, and if that is not enough, wait for the oldest job in flight.
 * Submit path only (no locks held).
 */
V3DBuffer *
BCM2837V3D::allocBinSliceWaiting(uint32_t size)
{
	for (int attempt = 0; attempt < 64; attempt++) {
		V3DBuffer *b = allocBinSlice(size);
		if (b != NULL) {
			return b;
		}
		IOInterruptState s = IOSimpleLockLockDisableInterrupt(fJobLock);
		V3DJob *done = fQueue->takeDone();
		uint64_t finished = fQueue->finishedSeqno();
		uint64_t last = fQueue->lastSeqno();
		IOSimpleLockUnlockEnableInterrupt(fJobLock, s);
		if (done != NULL) {
			while (done != NULL) {
				V3DJob *n = done->next;
				freeJob(done);
				done = n;
			}
			continue;
		}
		if (finished >= last) {
			return NULL;   // nothing in flight holds a slot: the pool is too small
		}
		if (waitSeqno(finished + 1, 2ULL * 1000 * 1000 * 1000) != kIOReturnSuccess) {
			return NULL;
		}
	}
	return NULL;
}

void
BCM2837V3D::freeBinSlots(uint32_t first, uint32_t n)
{
	uint32_t want = (n == 32) ? ~0U : ((1U << n) - 1);
	IOInterruptState s = IOSimpleLockLockDisableInterrupt(fBinLock);
	fBinSlotsUsed &= ~(want << first);
	IOSimpleLockUnlockEnableInterrupt(fBinLock, s);
}

static int
allocJobBO(void *ctxp, size_t size, bool stage, uint32_t *gpu, void **staging)
{
	V3DAllocCtx *ctx = (V3DAllocCtx *)ctxp;
	if (ctx->n >= 8 || size == 0 || size > kMaxBOSize) {
		return ENOMEM;
	}
	// The validators allocate exactly one BO without a staging copy: the
	// tile alloc/state BO (vc4_validate.c, drm_gem_cma_create_nostage()).
	// That is binner memory, so it comes from the binner pool.
	V3DBuffer *b = stage ? V3DBuffer::withSize((uint32_t)size) : ctx->dev->allocBinSliceWaiting((uint32_t)size);
	if (b == NULL) {
		return ENOMEM;
	}
	V3DCreated *c = &ctx->list[ctx->n];
	c->buf = b;
	c->staging = NULL;
	c->stagingSize = 0;
	if (stage) {
		c->stagingSize = (size + 15) & ~(size_t)15;
		c->staging = IOMalloc(c->stagingSize);
		if (c->staging == NULL) {
			b->release();
			return ENOMEM;
		}
		bzero(c->staging, c->stagingSize);
	}
	ctx->n++;
	*gpu = b->fGpuAddr;
	*staging = c->staging;
	return 0;
}

// Staging memory is ordinary cacheable memory the validators wrote
// unaligned fields into; the BO is write-combined GPU memory, so it gets
// the finished image in one pass of aligned word stores.
static void
copyStagingToBO(V3DBuffer *b, const void *src, size_t n)
{
	volatile uint32_t *d = (volatile uint32_t *)b->fKva;
	const uint32_t *s = (const uint32_t *)src;
	for (size_t i = 0; i < (n + 3) / 4; i++) {
		d[i] = s[i];
	}
	cpuToGpuBarrier();
}

IOReturn
BCM2837V3D::submitValidated(v3d_submit_cl *args, V3DBuffer **bos, uint32_t boCount,
    const void *binCl, const void *shaderRec, const void *uniforms, uint64_t *seqnoOut)
{
	if (!fPowered || fStopping) {
		return kIOReturnNotReady;
	}
	if (boCount > 1024) {
		return kIOReturnBadArgument;
	}
	v3d_kbo_desc *descs = NULL;
	V3DAllocCtx ctx;
	bzero(&ctx, sizeof(ctx));
	ctx.dev = this;
	IOReturn ret = kIOReturnNoMemory;
	V3DJob *j = NULL;

	if (boCount != 0) {
		descs = (v3d_kbo_desc *)IOMalloc(boCount * sizeof(v3d_kbo_desc));
		if (descs == NULL) {
			return kIOReturnNoMemory;
		}
		for (uint32_t i = 0; i < boCount; i++) {
			descs[i].size = bos[i]->fSize;
			descs[i].gpu_addr = bos[i]->fGpuAddr;
			descs[i].shader = bos[i]->fShader;
		}
	}

	v3d_kops ops = { &ctx, allocJobBO };
	v3d_kresult res;
	int err = v3d_kglue_validate(args, descs, boCount, binCl, shaderRec, uniforms, &ops, &res);
	if (err != 0) {
		ret = (err == ENOMEM) ? kIOReturnNoMemory : kIOReturnBadArgument;
		goto fail;
	}

	// Bin dependency (Linux 7edabee06a56, vc4_gem.c:905-911): with bin and
	// render pipelined, this job's binning can start while an earlier job's
	// render is still writing a BO this job reads (VBO, IB, coordinate-
	// shader textures), and bin from stale memory. Linux tracks exactly
	// those reads (exec->bin_dep_seqno); the vendored validators predate
	// that, so this waits on the superset: every BO of the job that an
	// earlier render wrote. Correct, at the cost of not overlapping a job
	// with an earlier render of a BO it also renders to.
	{
		uint64_t dep = 0;
		for (uint32_t i = 0; i < boCount; i++) {
			if (bos[i]->fWriteSeqno > dep) {
				dep = bos[i]->fWriteSeqno;
			}
		}
		if (dep != 0) {
			IOReturn w = waitSeqno(dep, 10ULL * 1000 * 1000 * 1000);
			if (w != kIOReturnSuccess) {
				ret = w;
				goto fail;
			}
		}
	}

	// Validation succeeded: publish the finished images to the GPU memory.
	for (uint32_t i = 0; i < ctx.n; i++) {
		if (ctx.list[i].staging != NULL) {
			copyStagingToBO(ctx.list[i].buf, ctx.list[i].staging, ctx.list[i].stagingSize);
		}
	}

	j = (V3DJob *)IOMalloc(sizeof(V3DJob));
	if (j == NULL) {
		goto fail;
	}
	bzero(j, sizeof(*j));
	j->ct0ca = res.ct0ca; j->ct0ea = res.ct0ea;
	j->ct1ca = res.ct1ca; j->ct1ea = res.ct1ea;
	j->boCount = boCount + ctx.n;
	j->bos = (V3DBuffer **)IOMalloc(j->boCount * sizeof(V3DBuffer *));
	if (j->bos == NULL) {
		IOFree(j, sizeof(*j));
		j = NULL;
		goto fail;
	}
	for (uint32_t i = 0; i < boCount; i++) {
		j->bos[i] = bos[i];
		bos[i]->retain();
	}
	for (uint32_t i = 0; i < ctx.n; i++) {
		j->bos[boCount + i] = ctx.list[i].buf;      // ownership moves to the job
		ctx.list[i].buf = NULL;
	}
	for (uint32_t i = 0; i < ctx.n; i++) {
		if (ctx.list[i].staging != NULL) {
			IOFree(ctx.list[i].staging, ctx.list[i].stagingSize);
			ctx.list[i].staging = NULL;
		}
	}
	if (descs != NULL) {
		IOFree(descs, boCount * sizeof(v3d_kbo_desc));
	}
	{
		// The render stage's surface writes (vc4_render_cl.c: color_write,
		// zs_write, msaa_color_write, msaa_zs_write); the validators have
		// already checked these indices against the BO list.
		V3DBuffer *writes[4];
		uint32_t nWrites = 0;
		const uint32_t idx[4] = { args->color_write.hindex, args->zs_write.hindex,
		    args->msaa_color_write.hindex, args->msaa_zs_write.hindex };
		for (uint32_t k = 0; k < 4; k++) {
			if (idx[k] < boCount) {
				writes[nWrites++] = bos[idx[k]];
			}
		}
		return queueJob(j, seqnoOut, writes, nWrites);
	}

fail:
	for (uint32_t i = 0; i < ctx.n; i++) {
		if (ctx.list[i].buf != NULL) {
			ctx.list[i].buf->release();
		}
		if (ctx.list[i].staging != NULL) {
			IOFree(ctx.list[i].staging, ctx.list[i].stagingSize);
		}
	}
	if (descs != NULL) {
		IOFree(descs, boCount * sizeof(v3d_kbo_desc));
	}
	return ret;
}

IOReturn
BCM2837V3D::waitSeqno(uint64_t seqno, uint64_t timeoutNs)
{
	if (fQueue->finishedSeqno() >= seqno) {
		return kIOReturnSuccess;
	}
	if (timeoutNs == 0) {
		return kIOReturnTimeout;
	}
	// Polled in 1 ms steps rather than slept on an event: a wakeup posted
	// between the check and the sleep would otherwise be lost, and this is
	// not a hot path (Mesa waits per frame/fence).
	uint64_t now, deadline;
	clock_get_uptime(&now);
	nanoseconds_to_absolutetime(timeoutNs, &deadline);
	deadline += now;
	while (fQueue->finishedSeqno() < seqno) {
		if (fStopping) {
			return kIOReturnAborted;
		}
		clock_get_uptime(&now);
		if (now >= deadline) {
			return kIOReturnTimeout;
		}
		IOSleep(1);
	}
	return kIOReturnSuccess;
}

void
BCM2837V3D::getHangState(v3d_hang_state *out)
{
	IOInterruptState s = IOSimpleLockLockDisableInterrupt(fJobLock);
	*out = fHang;
	IOSimpleLockUnlockEnableInterrupt(fJobLock, s);
}

/* ============================== lifecycle ============================== */

bool
BCM2837V3D::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}
	fProvider = provider;

	uint32_t arg;
	if (PE_parse_boot_argn("v3d", &arg, sizeof(arg)) && arg == 0) {
		IOLog("BCM2837V3D: disabled by boot-arg v3d=0\n");
		return false;
	}
	fRawAllowed = PE_parse_boot_argn("v3d-raw", &arg, sizeof(arg)) && arg == 1;
	// v3d-debug=1: print why the validators reject a submission (their
	// DRM_ERROR/DRM_INFO messages; off by default, user-triggerable).
	arg = 0;
	v3d_kglue_debug = PE_parse_boot_argn("v3d-debug", &arg, sizeof(arg)) && arg == 1;

	IODeviceMemory *mem0 = provider->getDeviceMemoryWithIndex(0);   // V3D
	IODeviceMemory *mem1 = provider->getDeviceMemoryWithIndex(1);   // power manager
	IODeviceMemory *mem2 = provider->getDeviceMemoryWithIndex(2);   // ASB bridges
	if (mem0 == NULL || mem1 == NULL || mem2 == NULL) {
		IOLog("BCM2837V3D: provider lacks the three reg ranges (V3D, PM, ASB)\n");
		return false;
	}
	fRegMap = mem0->map();
	fPMMap = mem1->map();
	fASBMap = mem2->map();
	if (fRegMap == NULL || fPMMap == NULL || fASBMap == NULL) {
		IOLog("BCM2837V3D: register map() failed\n");
		return false;
	}
	fRegs = (volatile uint8_t *)fRegMap->getVirtualAddress();
	fPM = (volatile uint8_t *)fPMMap->getVirtualAddress();
	fASB = (volatile uint8_t *)fASBMap->getVirtualAddress();

	// The ASB block identifies itself (bcm2835-power.c probe checks the
	// same register). Absent on QEMU raspi3: decline before touching the
	// power manager or the V3D window.
	if (*(volatile uint32_t *)(fASB + kASB_AXI_BRDG_ID) != kASB_BrdgID) {
		IOLog("BCM2837V3D: no ASB bridge (id %#x) - no V3D on this machine\n",
		    *(volatile uint32_t *)(fASB + kASB_AXI_BRDG_ID));
		return false;
	}

	fJobLock = IOSimpleLockAlloc();
	fWorkLoop = IOWorkLoop::workLoop();
	if (fJobLock == NULL || fWorkLoop == NULL) {
		return false;
	}
	fHangTimer = IOTimerEventSource::timerEventSource(this,
	    (IOTimerEventSource::Action)&BCM2837V3D::hangcheckTick);
	if (fHangTimer == NULL || fWorkLoop->addEventSource(fHangTimer) != kIOReturnSuccess) {
		return false;
	}

	{
		V3DHw hw = { this, hwStartBin, hwStartRender, hwFlushCaches, hwFlushTextureCaches };
		fQueue = new V3DQueue(hw);
	}
	if (fQueue == NULL) {
		return false;
	}
	{
		uint32_t mb = kGpuPoolDefaultMB;
		PE_parse_boot_argn("v3d-pool", &mb, sizeof(mb));
		if (mb != 0) {
			if (V3DBuffer::initGpuPool(mb * 1024U * 1024U)) {
				IOLog("BCM2837V3D: GPU memory pool %u MiB\n", mb);
			} else {
				IOLog("BCM2837V3D: no contiguous %u MiB for the GPU memory pool; BOs are allocated one by one\n", mb);
			}
		}
	}
	fBinLock = IOSimpleLockAlloc();
	fBinPool = (fBinLock != NULL) ? V3DBuffer::withSize(kBinPoolSize) : NULL;
	if (fBinPool == NULL) {
		IOLog("BCM2837V3D: no memory for the %u MiB binner pool\n", kBinPoolSize >> 20);
		return false;
	}

	if (!powerOn()) {
		return false;
	}
	fIdent0 = v3dRead(kV3D_IDENT0);
	if (fIdent0 != kV3D_ExpectedIdent0) {
		// Linux vc4_v3d_bind(): "V3D_IDENT0 read 0x%08x instead of 0x%08x"
		IOLog("BCM2837V3D: IDENT0 %#x, expected %#x\n", fIdent0, kV3D_ExpectedIdent0);
		powerOff();
		return false;
	}
	fIdent1 = v3dRead(kV3D_IDENT1);
	fIdent2 = v3dRead(kV3D_IDENT2);
	// Field decode as vc4_v3d.c's v3d_ident debugfs (lines 100-120): revision
	// [3:0], slices [7:4], QPUs/slice [11:8], TMUs/slice [15:12].
	{
		uint32_t nslc = (fIdent1 >> 4) & 0xf, qups = (fIdent1 >> 8) & 0xf, tups = (fIdent1 >> 12) & 0xf;
		IOLog("BCM2837V3D: V3D rev %u, %u slices, %u QPUs, %u TMUs, raw submit %s\n",
		    fIdent1 & 0xf, nslc, nslc * qups, nslc * tups, fRawAllowed ? "ENABLED" : "off");
	}

	initHW();

	IOReturn r = provider->registerInterrupt(0, this, &BCM2837V3D::interruptOccurred);
	if (r != kIOReturnSuccess || provider->enableInterrupt(0) != kIOReturnSuccess) {
		IOLog("BCM2837V3D: interrupt registration failed (%#x)\n", r);
		powerOff();
		return false;
	}
	fIRQRegistered = true;

	setProperty("IOUserClientClass", "BCM2837V3DUserClient");
	setProperty("V3D_IDENT0", (uint64_t)fIdent0, 32);
	setProperty("V3D_IDENT1", (uint64_t)fIdent1, 32);
	setProperty("V3D_IDENT2", (uint64_t)fIdent2, 32);
	registerService();
	return true;
}

void
BCM2837V3D::stop(IOService *provider)
{
	fStopping = true;
	if (fHangTimer != NULL) {
		fHangTimer->cancelTimeout();
	}
	if (fIRQRegistered) {
		provider->disableInterrupt(0);
		provider->unregisterInterrupt(0);
		fIRQRegistered = false;
	}
	if (fPowered) {
		v3dWrite(kV3D_INTDIS, kV3D_DriverIRQs);
		powerOff();
	}
	// Anything still queued can never finish now; release it.
	if (fQueue != NULL) {
		V3DJob *j = fQueue->takeAll();
		while (j != NULL) {
			V3DJob *n = j->next;
			freeJob(j);
			j = n;
		}
	}
	super::stop(provider);
}

void
BCM2837V3D::free(void)
{
	if (fHangTimer != NULL) {
		if (fWorkLoop != NULL) {
			fWorkLoop->removeEventSource(fHangTimer);
		}
		fHangTimer->release();
		fHangTimer = NULL;
	}
	if (fWorkLoop != NULL) {
		fWorkLoop->release();
		fWorkLoop = NULL;
	}
	if (fJobLock != NULL) {
		IOSimpleLockFree(fJobLock);
		fJobLock = NULL;
	}
	if (fBinPool != NULL) {
		fBinPool->release();
		fBinPool = NULL;
	}
	// BOs still alive (clients' handles) keep their sub-descriptors, which
	// retain the pool's descriptor, so its memory outlives this.
	V3DBuffer::freeGpuPool();
	if (fBinLock != NULL) {
		IOSimpleLockFree(fBinLock);
		fBinLock = NULL;
	}
	if (fQueue != NULL) {
		delete fQueue;
		fQueue = NULL;
	}
	if (fRegMap != NULL) { fRegMap->release(); fRegMap = NULL; }
	if (fPMMap != NULL) { fPMMap->release(); fPMMap = NULL; }
	if (fASBMap != NULL) { fASBMap->release(); fASBMap = NULL; }
	super::free();
}
