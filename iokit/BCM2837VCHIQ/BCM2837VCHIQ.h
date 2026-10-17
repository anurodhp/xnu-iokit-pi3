/*
 * BCM2837VCHIQ - the ARM side of VCHIQ, the message queue between the ARM
 * and the VideoCore VPU firmware. Everything the firmware offers the ARM
 * beyond the property mailbox goes over it: on the Pi 3 the H.264/MPEG-4
 * hardware decoder and encoder, the ISP, the camera, audio. Its clients
 * here are userland's (MMAL, through the raspberrypi/userland VCHIQ library
 * and BCM2837VCHIQUserClient).
 *
 * ================================ SCOPE ================================
 * The protocol core is Linux's (drivers/staging/vc04_services/interface/
 * vchiq_arm/vchiq_core.c, vchiq_arm.c, vchiq_dev.c, raspberrypi/linux
 * rpi-6.6.y, dual GPL-2.0/BSD-3-Clause), vendored under vchiq/ on top of a
 * Linux API shim (vchiq/vchiq_xnu.h). What this class does is the platform
 * half Linux's vchiq_arm.c does with the DT, the DMA API and the firmware
 * driver:
 *
 *   - the shared memory: TOTAL_SLOTS 4 KiB slots plus the bulk "fragments"
 *     area, one physically contiguous, non-cacheable block below 1 GiB,
 *     addressed by the VPU through the 0xC0000000 bus alias (Linux:
 *     dmam_alloc_coherent() with the vchiq node's dma-ranges);
 *   - handing its bus address to the firmware, property tag
 *     RPI_FIRMWARE_VCHIQ_INIT (0x00048010) through BCM2837Mailbox; the
 *     firmware answers 0 on success;
 *   - the doorbells: reg 0 of the "vchiq" node is the ARM's doorbell
 *     block at bus 0x7e00b840; BELL0 (read, clears) is rung by the VPU and
 *     raises ARM basic interrupt 2 ("ARM Doorbell 0"), BELL2 (write) rings
 *     the VPU;
 *   - bulk transfers: a client buffer is wired, cleaned/invalidated to the
 *     point of coherency, and described to the VPU as a struct pagelist
 *     (Linux create_pagelist()/free_pagelist(), 4 KiB VPU pages whatever
 *     XNU's page size), including the partial-cache-line "fragments" the
 *     VPU writes into the shared block instead of the client's memory.
 *
 * ============================== TESTING ===============================
 * QEMU raspi3 has no VPU: the firmware property is unanswered and start()
 * declines. The "vchiq" DT node is only present in builds that define
 * RPI3_HAVE_VCHIQ (rpi3-realhw.dts). Boot-arg vchiq=0 disables the driver;
 * vchiq-log=<0-7> sets the core's log levels (Linux's module parameters,
 * VCHIQ_LOG_*; default 4, warnings).
 */
#ifndef _BCM2837VCHIQ_H
#define _BCM2837VCHIQ_H

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "BCM2837VCHIQUAPI.h"

class BCM2837VCHIQ : public IOService {
	OSDeclareDefaultStructors(BCM2837VCHIQ)

public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;

	/* The doorbell registers, for vchiq_xnu_ring_doorbell(). */
	static volatile uint32_t *sBells;

private:
	static void interruptOccurred(OSObject *target, void *refCon, IOService *nub, int source);

	IOMemoryMap *fRegMap;
	bool fIRQRegistered;
};

#endif /* _BCM2837VCHIQ_H */
