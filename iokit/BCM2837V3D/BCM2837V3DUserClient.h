/*
 * BCM2837V3DUserClient - the userland face of BCM2837V3D (DARW-295).
 * Selectors and structs: BCM2837V3DUAPI.h. One instance per
 * IOServiceOpen(); owns a private handle table of BOs.
 *
 * Mapping a BO: IOConnectMapMemory64(connect, type = handle, ...) lands in
 * clientMemoryForType(), the standard IOUserClient path (the same one
 * IOFramebufferUserClient uses for VRAM, docs/framebuffer-mapping.md).
 */
#ifndef _BCM2837V3DUSERCLIENT_H
#define _BCM2837V3DUSERCLIENT_H

#include <IOKit/IOUserClient.h>
#include "BCM2837V3D.h"

class BCM2837V3DUserClient : public IOUserClient {
	OSDeclareFinalStructors(BCM2837V3DUserClient)

public:
	virtual bool initWithTask(task_t owningTask, void *securityID, UInt32 type,
	    OSDictionary *properties) APPLE_KEXT_OVERRIDE;
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	virtual void free(void) APPLE_KEXT_OVERRIDE;

	virtual IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target, void *reference) APPLE_KEXT_OVERRIDE;
	virtual IOReturn clientMemoryForType(UInt32 type, IOOptionBits *options,
	    IOMemoryDescriptor **memory) APPLE_KEXT_OVERRIDE;

private:
	enum {
		kMaxHandles = 1024,
		// Per-client cap: a client can otherwise pin all of RAM as
		// contiguous wired pages.
		kMaxClientBytes = 64U * 1024 * 1024,
	};

	V3DBuffer *lookup(uint32_t handle);      // returns retained, or NULL
	IOReturn createBO(const v3d_create_bo *in, v3d_create_bo *out);
	IOReturn closeBO(uint32_t handle);
	IOReturn waitBO(const v3d_wait_bo *in);
	IOReturn rawSubmit(const v3d_raw_submit *in, v3d_raw_submit *out);
	IOReturn readBO(const v3d_read_bo *in);
	IOReturn shareBO(const v3d_share_bo *in);
	IOReturn prepareRead(const v3d_prepare_read *in);
	IOReturn submitCL(const v3d_submit_cl *in, v3d_submit_cl *out);
	IOReturn createShaderBO(const v3d_create_shader_bo *in, v3d_create_shader_bo *out);
	bool copyIn(uint64_t userAddr, void *dst, size_t len);
	IOReturn installBO(V3DBuffer *b, uint32_t *handleOut);   // consumes the caller's reference

	BCM2837V3D *fDevice;
	task_t fTask;
	bool fIsAdmin;
	bool fClosed;
	IOLock *fLock;
	V3DBuffer *fHandles[kMaxHandles];    // handle = index + 1
	uint64_t fBytes;
};

#endif /* _BCM2837V3DUSERCLIENT_H */
