/*
 * BCM43430WifiUserClient - external methods of BCM43430Wifi for
 * /usr/sbin/wifi (ABI in BCM43430WifiShared.h). Root only: joining a
 * network and loading firmware change system state. Same shape as
 * BCM2837V3DUserClient (initWithTask privilege check, externalMethod switch
 * with exact struct-size checks).
 */
#ifndef _BCM43430WIFIUSERCLIENT_H
#define _BCM43430WIFIUSERCLIENT_H

#include <IOKit/IOUserClient.h>
#include "BCM43430Wifi.h"

class BCM43430WifiUserClient : public IOUserClient
{
	OSDeclareFinalStructors(BCM43430WifiUserClient);

public:
	virtual bool initWithTask(task_t owningTask, void *securityID, UInt32 type,
	    OSDictionary *properties) APPLE_KEXT_OVERRIDE;
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	virtual IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target, void *reference) APPLE_KEXT_OVERRIDE;

private:
	BCM43430Wifi *fDriver;
	bool fIsAdmin;
	bool fClosed;
};

#endif /* _BCM43430WIFIUSERCLIENT_H */
