/*
 * BCM43430BluetoothUserClient - external methods of BCM43430Bluetooth for
 * /usr/sbin/bt (ABI in BCM43430BluetoothShared.h). Same shape as
 * BCM43430WifiUserClient: privilege checked in initWithTask, externalMethod
 * switch with exact struct-size checks. Everything but Status is root-only.
 * Only one client at a time may read events (the driver has one queue); it
 * is claimed on the first ReadEvent and released on close.
 */
#ifndef _BCM43430BLUETOOTHUSERCLIENT_H
#define _BCM43430BLUETOOTHUSERCLIENT_H

#include <IOKit/IOUserClient.h>
#include "BCM43430Bluetooth.h"

class BCM43430BluetoothUserClient : public IOUserClient
{
	OSDeclareFinalStructors(BCM43430BluetoothUserClient);

public:
	virtual bool initWithTask(task_t owningTask, void *securityID, UInt32 type,
	    OSDictionary *properties) APPLE_KEXT_OVERRIDE;
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	virtual IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target, void *reference) APPLE_KEXT_OVERRIDE;

private:
	BCM43430Bluetooth *fDriver;
	bool fIsAdmin;
	bool fClosed;
};

#endif /* _BCM43430BLUETOOTHUSERCLIENT_H */
