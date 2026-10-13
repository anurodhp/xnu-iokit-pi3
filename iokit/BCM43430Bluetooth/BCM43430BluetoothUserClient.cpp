#include <IOKit/IOLib.h>
#include "BCM43430BluetoothUserClient.h"

#define super IOUserClient
OSDefineMetaClassAndFinalStructors(BCM43430BluetoothUserClient, IOUserClient)

bool
BCM43430BluetoothUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type,
    OSDictionary *properties)
{
	if (!super::initWithTask(owningTask, securityID, type, properties)) {
		return false;
	}
	fIsAdmin = (clientHasPrivilege(securityID, kIOClientPrivilegeAdministrator) == kIOReturnSuccess);
	fClosed = false;
	fDriver = NULL;
	return true;
}

bool
BCM43430BluetoothUserClient::start(IOService *provider)
{
	fDriver = OSDynamicCast(BCM43430Bluetooth, provider);
	return fDriver != NULL && super::start(provider);
}

IOReturn
BCM43430BluetoothUserClient::clientClose(void)
{
	fClosed = true;
	if (fDriver != NULL) {
		fDriver->releaseReader(this);
	}
	if (!isInactive()) {
		terminate();
	}
	return kIOReturnSuccess;
}

IOReturn
BCM43430BluetoothUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    IOExternalMethodDispatch *, OSObject *, void *)
{
	if (fClosed || fDriver == NULL || isInactive()) {
		return kIOReturnNotAttached;
	}
	// Status is readable by anyone (the launchd job checks it); everything
	// else talks to the controller.
	if (selector != kBtMethodStatus && !fIsAdmin) {
		return kIOReturnNotPrivileged;
	}
#define NEED_IN(T)  do { if (args->structureInputSize != sizeof(T) || args->structureInput == NULL) return kIOReturnBadArgument; } while (0)
#define NEED_OUT(T) do { if (args->structureOutput == NULL || args->structureOutputSize != sizeof(T)) return kIOReturnBadArgument; } while (0)

	IOReturn r;
	switch (selector) {
	case kBtMethodUploadBegin:
		IOLockLock(fDriver->fOpLock);
		r = fDriver->uploadBegin();
		IOLockUnlock(fDriver->fOpLock);
		return r;
	case kBtMethodUploadChunk:
		if (args->scalarInputCount != 1 || args->structureInput == NULL ||
		    args->structureInputSize == 0 || args->structureInputSize > kBtChunkMax ||
		    args->scalarInput[0] > kBtPatchMax) {
			return kIOReturnBadArgument;
		}
		IOLockLock(fDriver->fOpLock);
		r = fDriver->uploadChunk((uint32_t)args->scalarInput[0], args->structureInput,
		    args->structureInputSize);
		IOLockUnlock(fDriver->fOpLock);
		return r;
	case kBtMethodUp:
		IOLockLock(fDriver->fOpLock);
		r = fDriver->up();
		IOLockUnlock(fDriver->fOpLock);
		return r;
	case kBtMethodCommand: {
		NEED_IN(bt_command);
		NEED_OUT(bt_packet);
		const bt_command *c = (const bt_command *)args->structureInput;
		// Under fOpLock, like every WiFi operation: a command must never go
		// out in the middle of up()'s power cycle, patch or speed switch.
		IOLockLock(fDriver->fOpLock);
		r = fDriver->userCommand(c, (bt_packet *)args->structureOutput);
		IOLockUnlock(fDriver->fOpLock);
		return r;
	}
	case kBtMethodReadEvent:
		NEED_OUT(bt_packet);
		if (args->scalarInputCount != 1 || args->scalarInput[0] > 60000) {
			return kIOReturnBadArgument;
		}
		if (!fDriver->claimReader(this)) {
			return kIOReturnExclusiveAccess;
		}
		return fDriver->readEvent((uint32_t)args->scalarInput[0], (bt_packet *)args->structureOutput);
	case kBtMethodStatus:
		NEED_OUT(bt_status);
		fDriver->getStatus((bt_status *)args->structureOutput);
		return kIOReturnSuccess;
	}
	return kIOReturnUnsupported;
}
