/*
 * BCM2837VCHIQUserClient - one VCHIQ instance per IOServiceOpen(), the
 * equivalent of an open /dev/vchiq on Linux (vchiq_dev.c). The ABI is in
 * BCM2837VCHIQUAPI.h: selector = the ioctl's number, its argument struct in
 * and out, its return value in scalarOutput[0].
 *
 * Calls block as the ioctls do (AWAIT_COMPLETION waits for the VPU), and
 * several of one client's threads can be inside at once, as on Linux;
 * clientClose() is release(). A thread blocked here when the process dies
 * is woken by the abort (the core's waits are THREAD_ABORTSAFE).
 */
#ifndef _BCM2837VCHIQUSERCLIENT_H
#define _BCM2837VCHIQUSERCLIENT_H

#include <IOKit/IOUserClient.h>
#include "BCM2837VCHIQ.h"

struct vchiq_instance;

class BCM2837VCHIQUserClient : public IOUserClient {
	OSDeclareFinalStructors(BCM2837VCHIQUserClient)

public:
	virtual bool initWithTask(task_t owningTask, void *securityID, UInt32 type,
	    OSDictionary *properties) APPLE_KEXT_OVERRIDE;
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	virtual void free(void) APPLE_KEXT_OVERRIDE;

	virtual IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target, void *reference) APPLE_KEXT_OVERRIDE;

private:
	void closeInstance(void);

	struct vchiq_instance *fInstance;
	IOLock *fLock;
	uint32_t fBusy;         /* threads inside externalMethod() */
	bool fClosing;
};

#endif /* _BCM2837VCHIQUSERCLIENT_H */
