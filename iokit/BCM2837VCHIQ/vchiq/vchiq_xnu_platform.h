/*
 * The seam between the vendored C core (vchiq_arm.c, vchiq_dev.c) and the
 * IOKit driver (BCM2837VCHIQ.cpp, BCM2837VCHIQUserClient.cpp). Plain C.
 */
#ifndef VCHIQ_XNU_PLATFORM_H
#define VCHIQ_XNU_PLATFORM_H

#include <stdint.h>
#include <stddef.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

struct vchiq_instance;

/* ---- implemented by BCM2837VCHIQ.cpp ---- */

/* Linux: writel(0, g_regs + BELL2). */
void vchiq_xnu_ring_doorbell(void);

/*
 * Linux: create_pagelist(). Describes `size` bytes at kbuf (kernel memory)
 * or ubuf (the calling task's memory, exactly one of the two non-NULL) to
 * the VPU: wires the pages, does the cache maintenance for the direction
 * (`receive`: the VPU writes them), and builds a struct pagelist in memory
 * the VPU can read. *bus is the pagelist's VideoCore bus address, *cookie
 * what vchiq_xnu_free_pagelist() takes back. Returns 0 or an errno.
 */
int vchiq_xnu_create_pagelist(void *kbuf, void *ubuf, int size, int receive,
    uint32_t *bus, void **cookie);

/* Linux: free_pagelist(); `actual` bytes were transferred (< 0: aborted). */
void vchiq_xnu_free_pagelist(void *cookie, int actual);

/* ---- implemented by vchiq_arm.c ---- */

/* The doorbell interrupt fired (BELL0 has been read and was ringing). */
void vchiq_xnu_doorbell(void);

/*
 * Lays out the shared slots in slot_mem (VPU-visible, non-cacheable) and
 * starts the core's threads. Then the caller tells the firmware the slots'
 * bus address (RPI_FIRMWARE_VCHIQ_INIT). Returns 0 or -errno.
 */
int vchiq_xnu_init_state(void *slot_mem, int slot_mem_size, uint32_t fragments_bus,
    int fragments_count);

/* PAGE_ALIGN(TOTAL_SLOTS * VCHIQ_SLOT_SIZE): the slot part of the block. */
int vchiq_xnu_slot_mem_size(void);

/* Sets every log level of the core (VCHIQ_LOG_*, 0-7). */
void vchiq_xnu_set_log_level(int level);

/* ---- implemented by vchiq_dev.c (Linux: the file operations) ---- */

/* open(): a new instance, or NULL with *err set (-ENOTCONN: no VideoCore). */
struct vchiq_instance *vchiq_xnu_open(int *err);

/* release(): tears the instance down and frees it. */
void vchiq_xnu_release(struct vchiq_instance *instance);

/*
 * ioctl(): `cmd` is a VCHIQ_IOC_* value. For commands with an argument
 * struct, `args` is a kernel copy of it (in and out: the caller copies it
 * back to the client); for the scalar ones (_IO), `arg` is the value.
 * Returns what Linux's vchiq_ioctl() returns: >= 0 (a count, a size, an
 * id) or -errno.
 */
long vchiq_xnu_ioctl(struct vchiq_instance *instance, unsigned int cmd,
    unsigned long arg, void *args);

/*
 * sizeof the argument struct of the ioctl numbered `nr` (VCHIQ_IOC_*'s
 * _IOC_NR), 0 for the commands without one (and for unknown numbers).
 */
uint32_t vchiq_xnu_ioctl_size(uint32_t nr);

/* ---- implemented by vchiq_xnu.c ---- */
void vchiq_xnu_compat_init(void);

__END_DECLS

#endif /* VCHIQ_XNU_PLATFORM_H */
