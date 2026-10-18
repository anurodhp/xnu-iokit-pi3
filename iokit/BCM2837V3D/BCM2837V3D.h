/*
 * BCM2837V3D - VideoCore IV 3D engine driver (DARW-295,
 * docs/gpu-opengl-plan.md Tier B).
 *
 * ================================ SCOPE ================================
 * What this does, written from Linux's drivers/gpu/drm/vc4/{vc4_v3d.c,
 * vc4_irq.c,vc4_gem.c,vc4_bo.c} and drivers/pmdomain/bcm/bcm2835-power.c
 * as the reference (project owner's decision, DARW-299). Linux
 * behaviour each piece follows is cited at the piece.
 *
 *   - powers the V3D domain (V3D clock via the firmware mailbox, PM_GRAFX
 *     reset release, ASB master/slave bridges) and checks V3D_IDENT0
 *     before touching anything else;
 *   - owns the V3D interrupt and the two-stage job pipeline (binner
 *     CT0, renderer CT1), finished-seqno accounting, waiters, hang
 *     detection and reset;
 *   - owns buffer objects: physically contiguous, write-combined memory
 *     below the 0x3f000000 VideoCore bus-alias window, handed to
 *     clients through BCM2837V3DUserClient.
 *
 * What it deliberately does NOT do yet:
 *   - validated command submission (kV3DMethodSubmitCL). The GPU DMAs with
 *     whatever addresses its control lists contain, so a client-supplied
 *     list must be parsed and rewritten (Linux: vc4_validate.c,
 *     vc4_validate_shaders.c, vc4_render_cl.c). Until that lands, the only
 *     way to run work is kV3DMethodRawSubmit, root-only and gated by boot-arg
 *     v3d-raw=1, for hardware bring-up.
 *   - binner out-of-memory handling (V3D_INT_OUTOMEM stays masked): the
 *     overflow memory is owned by the validated-submit path.
 *
 * ============================== TESTING ===============================
 * QEMU raspi3 has no V3D. start() therefore checks the ASB bridge id
 * first and declines cleanly when the hardware is absent. Everything
 * past that check is unverified on real hardware.
 */
#ifndef _BCM2837V3D_H
#define _BCM2837V3D_H

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include "BCM2837V3DUAPI.h"
#include "BCM2837V3DRegs.h"
#include "V3DQueue.h"

class BCM2837V3DUserClient;
class BCM2837V3D;
struct vc4_validated_shader_info;

/* One buffer object. Refcounted so an in-flight job keeps it alive after
 * the client closes its handle. */
class V3DBuffer : public OSObject {
	OSDeclareFinalStructors(V3DBuffer)
public:
	IOMemoryDescriptor *fMem;   /* its own IOBufferMemoryDescriptor, or a sub-range of the GPU pool */
	volatile uint8_t *fKva;     /* kernel write-combined mapping of the BO */
	uint64_t fPhys;             /* physical address (contiguous) */
	uint32_t fGpuPoolFirst, fGpuPoolPages;   /* fGpuPoolPages != 0: a slice of the GPU pool */
	uint32_t fSize;
	uint32_t fGpuAddr;       /* bus address the GPU uses */
	volatile uint64_t fBusySeqno;  /* highest seqno of a job using this BO */
	volatile uint64_t fWriteSeqno; /* seqno of the last job whose render stage wrote this BO */
	struct vc4_validated_shader_info *fShader;   /* non-NULL: a validated, immutable shader BO */
	BCM2837V3D *fPoolOwner;  /* non-NULL: a slice of the binner pool (fMem is NULL) */
	uint32_t fPoolSlot, fPoolSlots;

	static V3DBuffer *withSize(uint32_t size);
	static V3DBuffer *empty(void);   /* no memory yet: for binner-pool slices */
	/* The GPU memory pool (see initGpuPool() in BCM2837V3D.cpp): BOs come from
	 * one contiguous block reserved at driver start. */
	static bool initGpuPool(uint32_t bytes);
	static void freeGpuPool(void);
	/* Read-only cacheable kernel alias of the BO's (contiguous) pages for
	 * readCached(), made on first use with io_map_with_prot(VM_WIMG_COPYBACK).
	 * The BO itself stays write-combined. */
	vm_offset_t fCachedVA;
	vm_size_t fCachedLen;
	IOReturn readCached(uint32_t offset, uint32_t size, uint64_t userDst);
	/* kV3DMethodShareBO / kV3DMethodPrepareRead (BCM2837V3DUAPI.h) */
	IOReturn shareWithShm(int shmid);
	IOReturn invalidateCached(uint32_t offset, uint32_t size);
	virtual void free(void) APPLE_KEXT_OVERRIDE;
private:
	IOReturn mapCachedAlias(void);
};

class BCM2837V3D : public IOService {
	OSDeclareFinalStructors(BCM2837V3D)

public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void free(void) APPLE_KEXT_OVERRIDE;

	/* Interface for BCM2837V3DUserClient */
	IOReturn getParam(uint32_t param, uint64_t *value);
	IOReturn submitRaw(const v3d_raw_submit *in, V3DBuffer **bos, uint64_t *seqnoOut);
	// Validated submit: `bos` are the caller's looked-up BOs (retained by the
	// caller), the three buffers are kernel copies of the client's data.
	// Validates a QPU program (kernel copy, `size` a multiple of 8) and wraps it
	// in an immutable shader BO. Returns a BO with one reference.
	IOReturn createShaderBO(const void *code, uint32_t size, V3DBuffer **out);
	IOReturn submitValidated(v3d_submit_cl *args, V3DBuffer **bos, uint32_t boCount,
	    const void *binCl, const void *shaderRec, const void *uniforms, uint64_t *seqnoOut);
	IOReturn waitSeqno(uint64_t seqno, uint64_t timeoutNs);
	void getHangState(v3d_hang_state *out);
	bool rawSubmitAllowed(void) const { return fRawAllowed; }
	uint64_t finishedSeqno(void) const { return fQueue->finishedSeqno(); }

public:
	// For the V3DQueue hardware callbacks (BCM2837V3D.cpp).
	void v3dWriteReg(uint32_t off, uint32_t v) { v3dWrite(off, v); }
	void flushCaches(void);
	void flushTextureCaches(void);

private:
	static void interruptOccurred(OSObject *target, void *refCon, IOService *nub, int source);
	static void hangcheckTick(OSObject *owner, IOTimerEventSource *sender);

	uint32_t v3dRead(uint32_t off) const { return *(volatile uint32_t *)(fRegs + off); }
	void v3dWrite(uint32_t off, uint32_t v) { *(volatile uint32_t *)(fRegs + off) = v; }
	uint32_t pmRead(uint32_t off) const { return *(volatile uint32_t *)(fPM + off); }
	void pmWrite(uint32_t off, uint32_t v) { *(volatile uint32_t *)(fPM + off) = kPMPassword | v; }

	bool asbControl(uint32_t reg, bool enable);
	bool powerOn(void);
	void powerOff(void);
	bool powerOnPM(void);
	void powerOffPM(void);
	void initHW(void);
	void resetHW(void);          /* thread context only */

	void saveHangState(void);
	void queueHangcheck(void);
	void freeJob(V3DJob *j);
	IOReturn queueJob(V3DJob *j, uint64_t *seqnoOut, V3DBuffer *const *writes = NULL, uint32_t nWrites = 0);
	void resetBinnerOverflow(void);   // job lock held, binner idle

	static const uint32_t kPMPassword = 0x5a000000U;

	IOMemoryMap *fRegMap, *fPMMap, *fASBMap;
	volatile uint8_t *fRegs, *fPM, *fASB;
	IOService *fProvider;
	IOWorkLoop *fWorkLoop;
	IOTimerEventSource *fHangTimer;

	IOSimpleLock *fJobLock;       /* protects the lists and seqno counters; taken at interrupt level */
	IOLock *fWaitLock;
	V3DQueue *fQueue;             // job pipeline state; used with fJobLock held

	V3DBuffer *fBinPool;          // binner memory: overflow + per-job tile slots
	IOSimpleLock *fBinLock;       // protects fBinSlotsUsed
	uint32_t fBinSlotsUsed;       // bit i: slot i handed to a job
public:
	V3DBuffer *allocBinSlice(uint32_t size);
	V3DBuffer *allocBinSliceWaiting(uint32_t size);
	void freeBinSlots(uint32_t first, uint32_t n);
private:
	uint32_t fIdent0, fIdent1, fIdent2;
	bool fPowered;
	bool fFwPower;          /* the firmware owns V3D power (Raspberry Pi firmware) */
	bool fFwPowerChecked;
	bool fIRQRegistered;
	bool fRawAllowed;
	bool fStopping;
	uint32_t fLastCt0ca, fLastCt1ca;
	v3d_hang_state fHang;
};

#endif /* _BCM2837V3D_H */
