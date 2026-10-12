#include <IOKit/IOLib.h>
#include "BCM43430WifiUserClient.h"

#define super IOUserClient
OSDefineMetaClassAndFinalStructors(BCM43430WifiUserClient, IOUserClient)

bool
BCM43430WifiUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type,
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
BCM43430WifiUserClient::start(IOService *provider)
{
	fDriver = OSDynamicCast(BCM43430Wifi, provider);
	return fDriver != NULL && super::start(provider);
}

IOReturn
BCM43430WifiUserClient::clientClose(void)
{
	fClosed = true;
	if (!isInactive()) {
		terminate();
	}
	return kIOReturnSuccess;
}

IOReturn
BCM43430WifiUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    IOExternalMethodDispatch *, OSObject *, void *)
{
	if (fClosed || fDriver == NULL || isInactive()) {
		return kIOReturnNotAttached;
	}
	// Status is readable by anyone; everything else changes system state.
	if (selector != kWifiMethodStatus && !fIsAdmin) {
		return kIOReturnNotPrivileged;
	}
#define NEED_IN(T)  do { if (args->structureInputSize != sizeof(T) || args->structureInput == NULL) return kIOReturnBadArgument; } while (0)
#define NEED_OUT(T) do { if (args->structureOutput == NULL || args->structureOutputSize != sizeof(T)) return kIOReturnBadArgument; } while (0)

	IOReturn r;
	switch (selector) {
	case kWifiMethodUploadBegin:
		IOLockLock(fDriver->fOpLock);
		r = fDriver->uploadBegin();
		IOLockUnlock(fDriver->fOpLock);
		return r;
	case kWifiMethodUploadChunk:
		if (args->scalarInputCount != 2 || args->structureInput == NULL ||
		    args->structureInputSize == 0 || args->structureInputSize > kWifiChunkMax) {
			return kIOReturnBadArgument;
		}
		IOLockLock(fDriver->fOpLock);
		r = fDriver->uploadChunk((uint32_t)args->scalarInput[0], (uint32_t)args->scalarInput[1],
		    args->structureInput, args->structureInputSize);
		IOLockUnlock(fDriver->fOpLock);
		return r;
	case kWifiMethodBoot:
		IOLockLock(fDriver->fOpLock);
		r = fDriver->boot();
		IOLockUnlock(fDriver->fOpLock);
		return r;
	case kWifiMethodScan: {
		NEED_OUT(wifi_scan_results);
		IOLockLock(fDriver->fOpLock);
		r = fDriver->scan((wifi_scan_results *)args->structureOutput);
		IOLockUnlock(fDriver->fOpLock);
		return r;
	}
	case kWifiMethodJoin: {
		NEED_IN(wifi_join);
		wifi_join req = *(const wifi_join *)args->structureInput;
		// Userland may send fields without a NUL; join() uses strlcpy.
		req.ssid[sizeof(req.ssid) - 1] = 0;
		req.passphrase[sizeof(req.passphrase) - 1] = 0;
		IOLockLock(fDriver->fOpLock);
		r = fDriver->join(&req);
		IOLockUnlock(fDriver->fOpLock);
		bzero(&req, sizeof(req));
		return r;
	}
	case kWifiMethodLeave:
		IOLockLock(fDriver->fOpLock);
		r = fDriver->leave();
		IOLockUnlock(fDriver->fOpLock);
		return r;
	case kWifiMethodStatus:
		NEED_OUT(wifi_status);
		fDriver->getStatus((wifi_status *)args->structureOutput);
		return kIOReturnSuccess;
	}
	return kIOReturnUnsupported;
}
