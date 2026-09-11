/*
 * NOT a real header - exists only to type-check BCM2837SDHCI's own logic
 * against the exact real IOBlockStorageDevice pure-virtual signatures
 * (copied verbatim from third_party/IOStorageFamily/IOBlockStorageDevice.h)
 * without pulling in that header's transitive <mach/mach_interface.h>
 * chain, which hits an unrelated, already-documented SDK/xnu version-skew
 * wall on this machine (see project memory - same class of gap as the
 * MIG-generated headers, an open-ended tail of newer mach/*.h types this
 * old xnu checkout doesn't define). This file's only job is to prove
 * BCM2837SDHCI.cpp's own code (register bit math, control flow, override
 * signatures) is real and correct; it does NOT substitute for eventually
 * compiling against the genuine IOBlockStorageDevice.h once that SDK skew
 * is resolved (e.g. via a real `make installhdrs`).
 */
#ifndef _VERIFY_ONLY_STUB_H
#define _VERIFY_ONLY_STUB_H

#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>

struct IOStorageAttributes {
	uint32_t options;
	uint32_t priority;
	uint8_t  reserved0024;
	uint32_t reserved0032;
	uint64_t reserved0064;
	int64_t  adjustedOffset;
	void    *bufattr;
};

typedef void (*IOStorageCompletionAction)(void *target, void *parameter, IOReturn status, uint64_t actualByteCount);

struct IOStorageCompletion {
	void                      *target;
	IOStorageCompletionAction  action;
	void                      *parameter;
};

class IOStorage : public IOService {
	OSDeclareAbstractStructors(IOStorage);
public:
	static void complete(IOStorageCompletion *completion, IOReturn status, UInt64 actualByteCount = 0)
	{
		if (completion && completion->action) {
			(*completion->action)(completion->target, completion->parameter, status, actualByteCount);
		}
	}
};

class IOBlockStorageDevice : public IOService {
	OSDeclareAbstractStructors(IOBlockStorageDevice);
public:
	virtual IOReturn doEjectMedia(void) = 0;
	virtual IOReturn doFormatMedia(UInt64 byteCapacity) = 0;
	virtual UInt32   doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const = 0;
	virtual char    *getVendorString(void) = 0;
	virtual char    *getProductString(void) = 0;
	virtual char    *getRevisionString(void) = 0;
	virtual char    *getAdditionalDeviceInfoString(void) = 0;
	virtual IOReturn reportBlockSize(UInt64 *blockSize) = 0;
	virtual IOReturn reportEjectability(bool *isEjectable) = 0;
	virtual IOReturn reportMaxValidBlock(UInt64 *maxBlock) = 0;
	virtual IOReturn reportMediaState(bool *mediaPresent, bool *changedState = 0) = 0;
	virtual IOReturn reportRemovability(bool *isRemovable) = 0;
	virtual IOReturn reportWriteProtection(bool *isWriteProtected) = 0;
	virtual IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks,
	                                   IOStorageAttributes *attributes, IOStorageCompletion *completion) = 0;
};

#endif
