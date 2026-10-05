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
	IOReturn ret = kIOReturnNoSpace;
	IOLockLock(fLock);
	if (!fClosed && fBytes + b->fSize <= kMaxClientBytes) {
		for (int i = 0; i < kMaxHandles; i++) {
			if (fHandles[i] == NULL) {
				fHandles[i] = b;      // table owns the creation reference
				fBytes += b->fSize;
				bzero(out, sizeof(*out));
				out->size = b->fSize;
				out->handle = (uint32_t)i + 1;
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
	// The descriptor already carries the write-combine cache mode it was
	// allocated with; don't override it.
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
	case kV3DMethodSubmitCL:
		// Validated submission is DARW-295 phase 2 (see BCM2837V3D.h).
		return kIOReturnUnsupported;
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
