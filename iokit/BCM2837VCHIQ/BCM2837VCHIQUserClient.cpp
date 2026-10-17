#include <IOKit/IOLib.h>
#include <sys/ioccom.h>
#include "BCM2837VCHIQUserClient.h"
#include "vchiq_xnu_platform.h"

#define super IOUserClient
OSDefineMetaClassAndFinalStructors(BCM2837VCHIQUserClient, IOUserClient)

#define VCHIQ_IOC_MAGIC 0xc4

/*
 * The VCHIQ_IOC_* value for a selector: the core switches on the full
 * command, and BSD's encoding of it carries the argument size, which is
 * vchiq_xnu_ioctl_size() (the sizeof of the Linux struct, 0 for _IO).
 */
static uint32_t
command_for(uint32_t selector)
{
	uint32_t size = vchiq_xnu_ioctl_size(selector);

	switch (selector) {
	case kVCHIQMethodQueueMessage:
	case kVCHIQMethodSetServiceOption:
	case kVCHIQMethodDumpPhysMem:
		return (uint32_t)_IOC(IOC_IN, VCHIQ_IOC_MAGIC, selector, size);
	default:
		if (size != 0) {
			return (uint32_t)_IOC(IOC_INOUT, VCHIQ_IOC_MAGIC, selector, size);
		}
		return (uint32_t)_IO(VCHIQ_IOC_MAGIC, selector);
	}
}

bool
BCM2837VCHIQUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type,
    OSDictionary *properties)
{
	if (!super::initWithTask(owningTask, securityID, type, properties)) {
		return false;
	}
	fLock = IOLockAlloc();
	return fLock != NULL;
}

bool
BCM2837VCHIQUserClient::start(IOService *provider)
{
	if (OSDynamicCast(BCM2837VCHIQ, provider) == NULL || !super::start(provider)) {
		return false;
	}
	/* Linux: vchiq_open() */
	int err = 0;
	fInstance = vchiq_xnu_open(&err);
	if (fInstance == NULL) {
		IOLog("BCM2837VCHIQUserClient: open failed (%d)\n", err);
		return false;
	}
	return true;
}

/*
 * Linux: vchiq_release(). It wakes the instance's blocked threads (closing,
 * insert_event) and waits for its services to die, so it is called once the
 * last thread is out of externalMethod().
 */
void
BCM2837VCHIQUserClient::closeInstance(void)
{
	struct vchiq_instance *inst;

	IOLockLock(fLock);
	fClosing = true;
	inst = fInstance;
	IOLockUnlock(fLock);
	if (inst == NULL) {
		return;
	}
	/* Shutdown first, as a dying Linux client's vchiq_lib would not:
	 * this ends AWAIT_COMPLETION waits so fBusy drains. */
	(void)vchiq_xnu_ioctl(inst, _IO(VCHIQ_IOC_MAGIC, kVCHIQMethodShutdown), 0, NULL);
	IOLockLock(fLock);
	while (fBusy != 0) {
		IOLockSleep(fLock, &fBusy, THREAD_UNINT);
	}
	inst = fInstance;
	fInstance = NULL;
	IOLockUnlock(fLock);
	if (inst != NULL) {
		vchiq_xnu_release(inst);
	}
}

IOReturn
BCM2837VCHIQUserClient::clientClose(void)
{
	closeInstance();
	if (!isInactive()) {
		terminate();
	}
	return kIOReturnSuccess;
}

void
BCM2837VCHIQUserClient::free(void)
{
	if (fLock != NULL) {
		closeInstance();        /* no-op after clientClose() */
		IOLockFree(fLock);
		fLock = NULL;
	}
	super::free();
}

IOReturn
BCM2837VCHIQUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    IOExternalMethodDispatch *, OSObject *, void *)
{
	if (selector >= kVCHIQMethodCount) {
		return kIOReturnBadArgument;
	}
	uint32_t size = vchiq_xnu_ioctl_size(selector);
	uint8_t kargs[64] __attribute__((aligned(16)));
	if (size > sizeof(kargs)) {
		return kIOReturnInternalError;
	}
	if (size != 0) {
		if (args->structureInput == NULL || args->structureInputSize != size ||
		    args->structureOutput == NULL || args->structureOutputSize != size) {
			return kIOReturnBadArgument;
		}
		memcpy(kargs, args->structureInput, size);
	}
	if (args->scalarOutputCount != 1) {
		return kIOReturnBadArgument;
	}
	uint64_t arg = args->scalarInputCount >= 1 ? args->scalarInput[0] : 0;

	IOLockLock(fLock);
	if (fClosing || fInstance == NULL) {
		IOLockUnlock(fLock);
		return kIOReturnNotAttached;
	}
	struct vchiq_instance *inst = fInstance;
	fBusy++;
	IOLockUnlock(fLock);

	long ret = vchiq_xnu_ioctl(inst, command_for(selector), (unsigned long)arg,
	    size != 0 ? kargs : NULL);

	IOLockLock(fLock);
	if (--fBusy == 0) {
		IOLockWakeup(fLock, &fBusy, false);
	}
	IOLockUnlock(fLock);

	/* Written back whatever the result: Linux's put_user()s of handles and
	 * of msgbufcount happen on failure paths too. */
	if (size != 0) {
		memcpy(args->structureOutput, kargs, size);
	}
	args->scalarOutput[0] = (uint64_t)(int64_t)ret;
	return kIOReturnSuccess;
}
