/*
 * BCM2837V3D - see BCM2837V3D.h for scope. Real hardware only; QEMU has no
 * V3D, so start() declines when the ASB bridge id does not read back.
 */
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <libkern/OSAtomic.h>
#include <pexpert/pexpert.h>
#include "BCM2837V3D.h"
#include "BCM2837V3DRegs.h"
#include "../BCM2837Mailbox/BCM2837Mailbox.h"

#define super IOService
OSDefineMetaClassAndFinalStructors(BCM2837V3D, IOService)
OSDefineMetaClassAndFinalStructors(V3DBuffer, OSObject)

enum {
	kMaxBOSize   = 16U * 1024 * 1024,
	// Hang detector period. Linux vc4_queue_hangcheck(): 100 ms
	// (vc4_gem.c, the mod_timer in vc4_queue_hangcheck).
	kHangcheckMs = 100,
};

static inline void
cpuToGpuBarrier(void)
{
	// BOs are write-combined: drain the CPU write buffer before the GPU is
	// told to read them.
	__asm__ volatile ("dsb sy" ::: "memory");
}

/* ============================ V3DBuffer ============================ */

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
	b->fMem = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
	    kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMapWriteCombineCache,
	    size, 0x3FFFF000ULL);
	if (b->fMem == NULL) {
		b->release();
		return NULL;
	}
	b->fSize = size;
	// Never hand a client someone else's stale memory.
	bzero(b->fMem->getBytesNoCopy(), size);
	cpuToGpuBarrier();

	IOPhysicalAddress pa = b->fMem->getPhysicalAddress();
	if ((uint64_t)pa + size > kV3D_BusAliasSize) {
		IOLog("V3DBuffer: physical %#llx+%#x outside the GPU window\n", (uint64_t)pa, size);
		b->release();
		return NULL;
	}
	b->fGpuAddr = (uint32_t)pa + kV3D_BusAliasBase;
	b->fBusySeqno = 0;
	return b;
}

void
V3DBuffer::free(void)
{
	if (fMem != NULL) {
		fMem->release();
		fMem = NULL;
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
 * The V3D power-on path of Linux's bcm2835_power_pd_power_on() for
 * BCM2835_POWER_DOMAIN_GRAFX_V3D (bcm2835-power.c:366-380), preceded by the
 * GRAFX domain's own bcm2835_power_power_on(PM_GRAFX) (:214-275) which
 * Linux runs because the V3D domain's parent is GRAFX.
 */
bool
BCM2837V3D::powerOn(void)
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
			IOLog("BCM2837V3D: GRAFX power never reported POWOK\n");
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
BCM2837V3D::powerOff(void)
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

static void
listPopHead(V3DJob **head, V3DJob **tail)
{
	V3DJob *j = *head;
	if (j != NULL) {
		*head = j->next;
		if (*head == NULL) {
			*tail = NULL;
		}
		j->next = NULL;
	}
}

static void
listAppend(V3DJob **head, V3DJob **tail, V3DJob *j)
{
	j->next = NULL;
	if (*tail != NULL) {
		(*tail)->next = j;
	} else {
		*head = j;
	}
	*tail = j;
}

// Completed jobs wait on fDoneHead/fDoneTail until the timer (thread context)
// frees them: releasing an OSObject can block, which the interrupt handler
// must not.

/* vc4_submit_next_bin_job() (vc4_gem.c:476-519). Job lock held. */
void
BCM2837V3D::submitNextBinJob(void)
{
	for (;;) {
		V3DJob *j = fBinHead;
		if (j == NULL) {
			return;
		}
		flushCaches();
		if (j->ct0ca != j->ct0ea) {
			cpuToGpuBarrier();
			// Writing the end address is what starts the job (submit_cl(),
			// vc4_gem.c:376-385).
			v3dWrite(kV3D_CT0CA, j->ct0ca);
			v3dWrite(kV3D_CT0EA, j->ct0ea);
			return;
		}
		// No binner work: go straight to the render queue.
		listPopHead(&fBinHead, &fBinTail);
		moveJobToRender(j);
	}
}

/* vc4_submit_next_render_job() (vc4_gem.c:521-541). Job lock held. */
void
BCM2837V3D::submitNextRenderJob(void)
{
	V3DJob *j = fRenderHead;
	if (j == NULL) {
		return;
	}
	flushTextureCaches();
	cpuToGpuBarrier();
	v3dWrite(kV3D_CT1CA, j->ct1ca);
	v3dWrite(kV3D_CT1EA, j->ct1ea);
}

/* vc4_move_job_to_render() (vc4_gem.c:543-553). Job lock held. */
void
BCM2837V3D::moveJobToRender(V3DJob *j)
{
	bool wasEmpty = (fRenderHead == NULL);
	listAppend(&fRenderHead, &fRenderTail, j);
	if (wasEmpty) {
		submitNextRenderJob();
	}
}

/* vc4_irq_finish_bin_job() (vc4_irq.c). Job lock held. */
void
BCM2837V3D::finishBinJob(void)
{
	V3DJob *j = fBinHead;
	if (j == NULL) {
		return;
	}
	listPopHead(&fBinHead, &fBinTail);
	moveJobToRender(j);
	submitNextBinJob();
}

/* vc4_irq_finish_render_job() (vc4_irq.c). Job lock held. */
void
BCM2837V3D::finishRenderJob(void)
{
	V3DJob *j = fRenderHead;
	if (j == NULL) {
		return;
	}
	listPopHead(&fRenderHead, &fRenderTail);
	fFinishedSeqno = j->seqno;
	OSMemoryBarrier();
	listAppend(&fDoneHead, &fDoneTail, j);
	if (fRenderHead != NULL) {
		submitNextRenderJob();
	}
}

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
		self->finishBinJob();
		IOSimpleLockUnlockEnableInterrupt(self->fJobLock, s);
	}
	if (intctl & kV3D_INT_FRDONE) {
		IOInterruptState s = IOSimpleLockLockDisableInterrupt(self->fJobLock);
		self->finishRenderJob();
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
	h.seqno = fRenderHead ? fRenderHead->seqno : (fBinHead ? fBinHead->seqno : 0);
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
	if (fRenderHead != NULL) {
		finishRenderJob();                   // fails the hung render job
	} else if (fBinHead != NULL) {
		V3DJob *j = fBinHead;
		listPopHead(&fBinHead, &fBinTail);
		fFinishedSeqno = j->seqno;
		OSMemoryBarrier();
		listAppend(&fDoneHead, &fDoneTail, j);
	}
	if (fPowered) {
		submitNextRenderJob();
		submitNextBinJob();
	} else {
		// Hardware is gone: fail everything still queued.
		for (;;) {
			V3DJob *j = fRenderHead;
			if (j != NULL) {
				listPopHead(&fRenderHead, &fRenderTail);
			} else if ((j = fBinHead) != NULL) {
				listPopHead(&fBinHead, &fBinTail);
			} else {
				break;
			}
			fFinishedSeqno = j->seqno;
			listAppend(&fDoneHead, &fDoneTail, j);
		}
	}
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
	V3DJob *done = self->fDoneHead;
	self->fDoneHead = self->fDoneTail = NULL;
	bool busy = (self->fBinHead != NULL || self->fRenderHead != NULL);
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
	case kV3DParamSupportsFixedRCLOrder:
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
	j->boCount = in->bo_count;
	for (uint32_t i = 0; i < in->bo_count; i++) {
		j->bos[i] = bos[i];
		bos[i]->retain();
	}

	IOInterruptState s = IOSimpleLockLockDisableInterrupt(fJobLock);
	j->seqno = ++fNextSeqno;
	for (uint32_t i = 0; i < j->boCount; i++) {
		j->bos[i]->fBusySeqno = j->seqno;
	}
	bool wasIdle = (fBinHead == NULL && fRenderHead == NULL);
	bool kick = (fBinHead == NULL);
	listAppend(&fBinHead, &fBinTail, j);
	if (kick) {
		submitNextBinJob();
	}
	IOSimpleLockUnlockEnableInterrupt(fJobLock, s);

	*seqnoOut = j->seqno;
	if (wasIdle) {
		fLastCt0ca = fLastCt1ca = 0xffffffffU;
		queueHangcheck();
	}
	return kIOReturnSuccess;
}

IOReturn
BCM2837V3D::waitSeqno(uint64_t seqno, uint64_t timeoutNs)
{
	if (fFinishedSeqno >= seqno) {
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
	while (fFinishedSeqno < seqno) {
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
	V3DJob *lists[3] = { fBinHead, fRenderHead, fDoneHead };
	fBinHead = fBinTail = fRenderHead = fRenderTail = NULL;
	fDoneHead = fDoneTail = NULL;
	for (int i = 0; i < 3; i++) {
		while (lists[i] != NULL) {
			V3DJob *n = lists[i]->next;
			freeJob(lists[i]);
			lists[i] = n;
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
	if (fRegMap != NULL) { fRegMap->release(); fRegMap = NULL; }
	if (fPMMap != NULL) { fPMMap->release(); fPMMap = NULL; }
	if (fASBMap != NULL) { fASBMap->release(); fASBMap = NULL; }
	super::free();
}
