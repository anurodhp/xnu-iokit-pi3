/*
 * BCM2837VCHIQ user-client ABI. Plain C, shared by the kernel and by
 * userland's VCHIQ library (the Darwin transport in our raspberrypi/userland
 * fork, interface/vchiq_arm/vchiq_lib.c).
 *
 * Linux exposes VCHIQ as /dev/vchiq and its ioctls (vchiq_dev.c). Here
 * each ioctl is an IOConnectCallMethod selector of the "BCM2837VCHIQ"
 * service, with the ioctl's own number and argument struct, so the Linux
 * argument layouts (vchiq_ioctl.h, struct vchiq_create_service etc.; arm64,
 * natural alignment) are the ABI unchanged:
 *
 *   selector          = _IOC_NR(VCHIQ_IOC_xxx), 0 .. kVCHIQMethodCount-1
 *   scalarInput[0]    = the ioctl's integer argument (the handle, the
 *                       library version) for the commands that take one
 *                       (_IO), 0 otherwise
 *   structureInput    = the argument struct, for the _IOW/_IOWR commands
 *   structureOutput   = the same struct, written back (handles, the
 *                       remaining message-buffer count, the bulk mode)
 *   scalarOutput[0]   = the ioctl's return value as Linux returns it:
 *                       >= 0 (a completion count, a message size, a client
 *                       id) or -errno
 *
 * The call itself returns kIOReturnSuccess whenever the command ran; a
 * failure of the command is the negative scalarOutput[0]. Linux's ioctl()
 * returns the value directly, which Darwin's ioctl() cannot (it returns 0
 * or -1), hence the user client.
 *
 * Opening the connection is Linux's open("/dev/vchiq"); closing it (or the
 * process dying) is release().
 */
#ifndef _BCM2837VCHIQUAPI_H
#define _BCM2837VCHIQUAPI_H

#include <stdint.h>

#define kBCM2837VCHIQServiceName "BCM2837VCHIQ"

/* _IOC_NR of each VCHIQ_IOC_* (Linux vchiq_ioctl.h, VCHIQ_IOC_MAGIC 0xc4) */
enum {
	kVCHIQMethodConnect = 0,
	kVCHIQMethodShutdown = 1,
	kVCHIQMethodCreateService = 2,
	kVCHIQMethodRemoveService = 3,
	kVCHIQMethodQueueMessage = 4,
	kVCHIQMethodQueueBulkTransmit = 5,
	kVCHIQMethodQueueBulkReceive = 6,
	kVCHIQMethodAwaitCompletion = 7,
	kVCHIQMethodDequeueMessage = 8,
	kVCHIQMethodGetClientId = 9,
	kVCHIQMethodGetConfig = 10,
	kVCHIQMethodCloseService = 11,
	kVCHIQMethodUseService = 12,
	kVCHIQMethodReleaseService = 13,
	kVCHIQMethodSetServiceOption = 14,
	kVCHIQMethodDumpPhysMem = 15,        /* not implemented: -ENOTTY, as on current Linux */
	kVCHIQMethodLibVersion = 16,
	kVCHIQMethodCloseDelivered = 17,
	kVCHIQMethodCount
};

#endif /* _BCM2837VCHIQUAPI_H */
