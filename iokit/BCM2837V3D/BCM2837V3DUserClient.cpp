#include <IOKit/IOLib.h>
#include "BCM2837V3DUserClient.h"

#define super IOUserClient
OSDefineMetaClassAndFinalStructors(BCM2837V3DUserClient, IOUserClient)

bool
BCM2837V3DUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type,
    OSDictionary *properties)
{
	if (!super::initWithTask(owningTask, securityID, type, properties)) {
		return false;
	}
	fTask = owningTask;
	// Raw submit is the only operation that needs privilege, and it is
	// additionally gated by the v3d-raw=1 boot-arg inside the driver.
	fIsAdmin = (clientHasPrivilege(securityID, kIOClientPrivilegeAdministrator) == kIOReturnSuccess);
	fLock = IOLockAlloc();
	return fLock != NULL;
}

bool
BCM2837V3DUserClient::start(IOService *provider)
{
	fDevice = OSDynamicCast(BCM2837V3D, provider);
	if (fDevice == NULL || !super::start(provider)) {
		return false;
	}
	return true;
}

IOReturn
BCM2837V3DUserClient::clientClose(void)
{
	fClosed = true;
	// Release handles now; jobs in flight keep their own BO references.
	IOLockLock(fLock);
	for (int i = 0; i < kMaxHandles; i++) {
		if (fHandles[i] != NULL) {
			fHandles[i]->release();
			fHandles[i] = NULL;
		}
	}
	fBytes = 0;
	IOLockUnlock(fLock);
	if (!isInactive()) {
		terminate();
	}
	return kIOReturnSuccess;
}

void
BCM2837V3DUserClient::free(void)
{
	if (fLock != NULL) {
		IOLockFree(fLock);
		fLock = NULL;
	}
	super::free();
}

V3DBuffer *
BCM2837V3DUserClient::lookup(uint32_t handle)
{
	if (handle == 0 || handle > kMaxHandles) {
		return NULL;
	}
	IOLockLock(fLock);
	V3DBuffer *b = fHandles[handle - 1];
	if (b != NULL) {
		b->retain();
	}
	IOLockUnlock(fLock);
	return b;
}

IOReturn
BCM2837V3DUserClient::createBO(const v3d_create_bo *in, v3d_create_bo *out)
{
	if (in->flags != 0 || in->size == 0) {
		return kIOReturnBadArgument;
	}
	V3DBuffer *b = V3DBuffer::withSize(in->size);
	if (b == NULL) {
		return kIOReturnNoMemory;
	}
	uint32_t size = b->fSize, handle = 0;
	IOReturn ret = installBO(b, &handle);
	if (ret == kIOReturnSuccess) {
		bzero(out, sizeof(*out));
		out->size = size;
		out->handle = handle;
	}
	return ret;
}

/* Puts `b` in this connection's handle table, consuming the caller's
 * reference (released on failure). */
IOReturn
BCM2837V3DUserClient::installBO(V3DBuffer *b, uint32_t *handleOut)
{
	IOReturn ret = kIOReturnNoSpace;
	IOLockLock(fLock);
	if (!fClosed && fBytes + b->fSize <= kMaxClientBytes) {
		for (int i = 0; i < kMaxHandles; i++) {
			if (fHandles[i] == NULL) {
				fHandles[i] = b;
				fBytes += b->fSize;
				*handleOut = (uint32_t)i + 1;
				ret = kIOReturnSuccess;
				break;
			}
		}
	}
	IOLockUnlock(fLock);
	if (ret != kIOReturnSuccess) {
		b->release();
	}
	return ret;
}

/* Copies from the calling task's address space (never trusts the pointer:
 * the memory descriptor validates and wires the range against the task). */
bool
BCM2837V3DUserClient::copyIn(uint64_t userAddr, void *dst, size_t len)
{
	if (len == 0) {
		return true;
	}
	IOMemoryDescriptor *md = IOMemoryDescriptor::withAddressRange(userAddr, len, kIODirectionOut, fTask);
	if (md == NULL) {
		return false;
	}
	bool ok = false;
	if (md->prepare() == kIOReturnSuccess) {
		ok = (md->readBytes(0, dst, len) == len);
		md->complete();
	}
	md->release();
	return ok;
}

IOReturn
BCM2837V3DUserClient::submitCL(const v3d_submit_cl *in, v3d_submit_cl *out)
{
	enum { kMaxBytes = 16 * 1024 * 1024 };
	if (in->bo_handle_count > kMaxHandles || in->bin_cl_size > kMaxBytes ||
	    in->shader_rec_size > kMaxBytes || in->uniforms_size > kMaxBytes ||
	    (uint64_t)in->bin_cl_size + in->shader_rec_size + in->uniforms_size > kMaxBytes) {
		return kIOReturnBadArgument;
	}
	v3d_submit_cl args = *in;      // validated from this private copy
	args.seqno = 0;

	IOReturn ret = kIOReturnNoMemory;
	V3DBuffer **bos = NULL;
	uint32_t nbos = 0;
	uint32_t *handles = NULL;
	uint8_t *bin = NULL, *rec = NULL, *uni = NULL;
	uint64_t seqno = 0;
	// 8-byte alignment of each copy: the validators read 64-bit QPU words
	// and 32-bit fields.
	size_t binAlloc = (args.bin_cl_size + 7) & ~(size_t)7;
	size_t recAlloc = (args.shader_rec_size + 7) & ~(size_t)7;
	size_t uniAlloc = (args.uniforms_size + 7) & ~(size_t)7;

	if (args.bo_handle_count != 0) {
		handles = (uint32_t *)IOMalloc(args.bo_handle_count * sizeof(uint32_t));
		bos = (V3DBuffer **)IOMalloc(args.bo_handle_count * sizeof(V3DBuffer *));
		if (handles == NULL || bos == NULL) {
			goto done;
		}
		if (!copyIn(args.bo_handles, handles, args.bo_handle_count * sizeof(uint32_t))) {
			ret = kIOReturnVMError;
			goto done;
		}
	}
	bin = (uint8_t *)IOMalloc(binAlloc + 8);
	rec = (uint8_t *)IOMalloc(recAlloc + 8);
	uni = (uint8_t *)IOMalloc(uniAlloc + 8);
	if (bin == NULL || rec == NULL || uni == NULL) {
		goto done;
	}
	bzero(bin, binAlloc + 8); bzero(rec, recAlloc + 8); bzero(uni, uniAlloc + 8);
	if (!copyIn(args.bin_cl, bin, args.bin_cl_size) ||
	    !copyIn(args.shader_rec, rec, args.shader_rec_size) ||
	    !copyIn(args.uniforms, uni, args.uniforms_size)) {
		ret = kIOReturnVMError;
		goto done;
	}
	for (; nbos < args.bo_handle_count; nbos++) {
		bos[nbos] = lookup(handles[nbos]);
		if (bos[nbos] == NULL) {
			ret = kIOReturnNotFound;
			goto done;
		}
	}
	ret = fDevice->submitValidated(&args, bos, nbos, bin, rec, uni, &seqno);
	if (ret == kIOReturnSuccess) {
		*out = args;
		out->seqno = seqno;
	}
done:
	for (uint32_t i = 0; i < nbos; i++) {
		bos[i]->release();
	}
	if (bos != NULL) IOFree(bos, args.bo_handle_count * sizeof(V3DBuffer *));
	if (handles != NULL) IOFree(handles, args.bo_handle_count * sizeof(uint32_t));
	if (bin != NULL) IOFree(bin, binAlloc + 8);
	if (rec != NULL) IOFree(rec, recAlloc + 8);
	if (uni != NULL) IOFree(uni, uniAlloc + 8);
	return ret;
}

IOReturn
BCM2837V3DUserClient::createShaderBO(const v3d_create_shader_bo *in, v3d_create_shader_bo *out)
{
	if (in->flags != 0 || in->size == 0 || (in->size & 7) != 0 || in->size > 64 * 1024) {
		return kIOReturnBadArgument;
	}
	uint32_t size = in->size;
	void *code = IOMalloc(size);
	if (code == NULL) {
		return kIOReturnNoMemory;
	}
	V3DBuffer *b = NULL;
	IOReturn ret;
	if (!copyIn(in->data, code, size)) {
		ret = kIOReturnVMError;
	} else {
		ret = fDevice->createShaderBO(code, size, &b);
	}
	IOFree(code, size);
	if (ret != kIOReturnSuccess) {
		return ret;
	}
	uint32_t handle = 0;
	ret = installBO(b, &handle);
	if (ret == kIOReturnSuccess) {
		bzero(out, sizeof(*out));
		out->size = size;
		out->handle = handle;
	}
	return ret;
}

IOReturn
BCM2837V3DUserClient::closeBO(uint32_t handle)
{
	if (handle == 0 || handle > kMaxHandles) {
		return kIOReturnBadArgument;
	}
	IOLockLock(fLock);
	V3DBuffer *b = fHandles[handle - 1];
	fHandles[handle - 1] = NULL;
	if (b != NULL) {
		fBytes -= b->fSize;
	}
	IOLockUnlock(fLock);
	if (b == NULL) {
		return kIOReturnNotFound;
	}
	b->release();   // an in-flight job (or a live mapping) keeps its own reference
	return kIOReturnSuccess;
}

IOReturn
BCM2837V3DUserClient::waitBO(const v3d_wait_bo *in)
{
	V3DBuffer *b = lookup(in->handle);
	if (b == NULL) {
		return kIOReturnNotFound;
	}
	uint64_t seqno = b->fBusySeqno;
	b->release();
	return seqno == 0 ? kIOReturnSuccess : fDevice->waitSeqno(seqno, in->timeout_ns);
}

/*
 * Fast CPU read-back (struct v3d_read_bo). Waits for the BO's last job, then
 * copies out through a read-only cacheable kernel alias of the BO after a
 * clean+invalidate of the range by VA; V3DBuffer::readCached() has why the
 * alias must be mapped from the physical address, and why it is coherent.
 */
IOReturn
BCM2837V3DUserClient::readBO(const v3d_read_bo *in)
{
	V3DBuffer *b = lookup(in->handle);
	if (b == NULL) {
		return kIOReturnNotFound;
	}
	IOReturn ret = kIOReturnSuccess;
	if (b->fShader != NULL || b->fMem == NULL) {
		ret = kIOReturnNotPermitted;
	} else if (in->offset > b->fSize || in->size > b->fSize - in->offset) {
		ret = kIOReturnBadArgument;
	} else if (in->size != 0) {
		if (b->fBusySeqno != 0) {
			ret = fDevice->waitSeqno(b->fBusySeqno, 2ULL * 1000 * 1000 * 1000);
		}
		if (ret == kIOReturnSuccess) {
			ret = b->readCached(in->offset, in->size, in->dst);
		}
	}
	b->release();
	return ret;
}

/* kV3DMethodShareBO; V3DBuffer::shareWithShm() has how. */
IOReturn
BCM2837V3DUserClient::shareBO(const v3d_share_bo *in)
{
	V3DBuffer *b = lookup(in->handle);
	if (b == NULL) {
		return kIOReturnNotFound;
	}
	IOReturn ret = b->shareWithShm(in->shmid);
	b->release();
	return ret;
}

/* kV3DMethodPrepareRead: wait for the BO's last job, then drop the range
 * from the caches (V3DBuffer::invalidateCached). */
IOReturn
BCM2837V3DUserClient::prepareRead(const v3d_prepare_read *in)
{
	V3DBuffer *b = lookup(in->handle);
	if (b == NULL) {
		return kIOReturnNotFound;
	}
	IOReturn ret = kIOReturnSuccess;
	if (b->fShader != NULL || b->fMem == NULL) {
		ret = kIOReturnNotPermitted;
	} else if (in->offset > b->fSize || in->size > b->fSize - in->offset) {
		ret = kIOReturnBadArgument;
	} else if (in->size != 0) {
		if (b->fBusySeqno != 0) {
			ret = fDevice->waitSeqno(b->fBusySeqno, 2ULL * 1000 * 1000 * 1000);
		}
		if (ret == kIOReturnSuccess) {
			ret = b->invalidateCached(in->offset, in->size);
		}
	}
	b->release();
	return ret;
}

IOReturn
BCM2837V3DUserClient::rawSubmit(const v3d_raw_submit *in, v3d_raw_submit *out)
{
	if (!fIsAdmin || !fDevice->rawSubmitAllowed()) {
		return kIOReturnNotPrivileged;
	}
	if (in->bo_count > V3D_RAW_MAX_BOS) {
		return kIOReturnBadArgument;
	}
	V3DBuffer *bos[V3D_RAW_MAX_BOS];
	uint32_t n = 0;
	IOReturn ret = kIOReturnSuccess;
	for (; n < in->bo_count; n++) {
		bos[n] = lookup(in->bo_handles[n]);
		if (bos[n] == NULL) {
			ret = kIOReturnNotFound;
			break;
		}
	}
	uint64_t seqno = 0;
	if (ret == kIOReturnSuccess) {
		ret = fDevice->submitRaw(in, bos, &seqno);
	}
	for (uint32_t i = 0; i < n; i++) {
		bos[i]->release();       // the job took its own references
	}
	if (ret == kIOReturnSuccess) {
		*out = *in;
		out->seqno = seqno;
	}
	return ret;
}

IOReturn
BCM2837V3DUserClient::clientMemoryForType(UInt32 type, IOOptionBits *options, IOMemoryDescriptor **memory)
{
	V3DBuffer *b = lookup(type);
	if (b == NULL) {
		return kIOReturnNotFound;
	}
	if (b->fShader != NULL) {
		// Shader BOs are immutable once validated: a mapping would let the
		// client rewrite the program after the check.
		b->release();
		return kIOReturnNotPermitted;
	}
	// The descriptor already carries the write-combine cache mode it was
	// allocated with; don't override it. Cache bits returned here would be
	// discarded anyway: IOUserClient::mapClientMemory64 replaces everything
	// in kIOMapUserOptionsMask (0xfff, which includes kIOMapCacheMask) with
	// the caller's mapFlags (iokit/Kernel/IOUserClient.cpp:2020). DARW-303
	// tried cacheable BOs + an explicit WC client mapping; that flag was
	// dropped, the client mapped the BOs cacheable, and v3dprobe read stale
	// pixels (4080/4096 wrong) on a Pi 3.
	*options = 0;
	b->fMem->retain();
	*memory = b->fMem;
	b->release();
	return kIOReturnSuccess;
}

IOReturn
BCM2837V3DUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    IOExternalMethodDispatch *, OSObject *, void *)
{
	if (fClosed || fDevice == NULL || isInactive()) {
		return kIOReturnNotAttached;
	}
	// Struct sizes are checked exactly: a short struct is a caller bug, a
	// long one is read as if it were the short one by nobody.
#define NEED_IN(T)  do { if (args->structureInputSize != sizeof(T) || args->structureInput == NULL) return kIOReturnBadArgument; } while (0)
#define NEED_OUT(T) do { if (args->structureOutput == NULL || args->structureOutputSize != sizeof(T)) return kIOReturnBadArgument; } while (0)

	switch (selector) {
	case kV3DMethodGetParam: {
		NEED_IN(v3d_get_param);
		NEED_OUT(v3d_get_param);
		v3d_get_param p = *(const v3d_get_param *)args->structureInput;
		IOReturn r = fDevice->getParam(p.param, &p.value);
		if (r == kIOReturnSuccess) {
			*(v3d_get_param *)args->structureOutput = p;
		}
		return r;
	}
	case kV3DMethodCreateBO: {
		NEED_IN(v3d_create_bo);
		NEED_OUT(v3d_create_bo);
		return createBO((const v3d_create_bo *)args->structureInput, (v3d_create_bo *)args->structureOutput);
	}
	case kV3DMethodCloseBO:
		if (args->scalarInputCount != 1) {
			return kIOReturnBadArgument;
		}
		return closeBO((uint32_t)args->scalarInput[0]);
	case kV3DMethodWaitSeqno: {
		NEED_IN(v3d_wait_seqno);
		const v3d_wait_seqno *w = (const v3d_wait_seqno *)args->structureInput;
		return fDevice->waitSeqno(w->seqno, w->timeout_ns);
	}
	case kV3DMethodWaitBO:
		NEED_IN(v3d_wait_bo);
		return waitBO((const v3d_wait_bo *)args->structureInput);
	case kV3DMethodSubmitCL: {
		NEED_IN(v3d_submit_cl);
		NEED_OUT(v3d_submit_cl);
		return submitCL((const v3d_submit_cl *)args->structureInput, (v3d_submit_cl *)args->structureOutput);
	}
	case kV3DMethodCreateShaderBO: {
		NEED_IN(v3d_create_shader_bo);
		NEED_OUT(v3d_create_shader_bo);
		return createShaderBO((const v3d_create_shader_bo *)args->structureInput,
		    (v3d_create_shader_bo *)args->structureOutput);
	}
	case kV3DMethodReadBO:
		NEED_IN(v3d_read_bo);
		return readBO((const v3d_read_bo *)args->structureInput);
	case kV3DMethodShareBO:
		NEED_IN(v3d_share_bo);
		return shareBO((const v3d_share_bo *)args->structureInput);
	case kV3DMethodPrepareRead:
		NEED_IN(v3d_prepare_read);
		return prepareRead((const v3d_prepare_read *)args->structureInput);
	case kV3DMethodGetHangState: {
		NEED_OUT(v3d_hang_state);
		fDevice->getHangState((v3d_hang_state *)args->structureOutput);
		return kIOReturnSuccess;
	}
	case kV3DMethodRawSubmit: {
		NEED_IN(v3d_raw_submit);
		NEED_OUT(v3d_raw_submit);
		return rawSubmit((const v3d_raw_submit *)args->structureInput, (v3d_raw_submit *)args->structureOutput);
	}
	case kV3DMethodGetBOAddress: {
		if (!fIsAdmin || args->scalarInputCount != 1 || args->scalarOutputCount != 1) {
			return fIsAdmin ? kIOReturnBadArgument : kIOReturnNotPrivileged;
		}
		V3DBuffer *b = lookup((uint32_t)args->scalarInput[0]);
		if (b == NULL) {
			return kIOReturnNotFound;
		}
		args->scalarOutput[0] = b->fGpuAddr;
		b->release();
		return kIOReturnSuccess;
	}
	default:
		return kIOReturnUnsupported;
	}
#undef NEED_IN
#undef NEED_OUT
}
