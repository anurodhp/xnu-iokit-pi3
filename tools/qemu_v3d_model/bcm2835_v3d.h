/*
 * BCM2835/6/7 VideoCore IV 3D ("V3D") test model for QEMU raspi3
 * (DARW-295). NOT a GPU: there is no QPU, binner or rasteriser. It models
 * just enough of the hardware's CPU-visible behaviour to run the guest's
 * BCM2837V3D driver through power-up, the interrupt path, the bin/render job
 * pipeline and hang recovery:
 *
 *   - the ASB AXI bridges (ARM 0x3f00a000): AXI_BRDG_ID, V3D master/slave
 *     REQ_STOP/ACK handshake;
 *   - the V3D register block (ARM 0x3fc00000): IDENT0-2, INTCTL/INTENA/
 *     INTDIS, CT0/CT1 CS/CA/EA, L2CACTL/SLCACTL, BPOA/BPOS (stored);
 *   - jobs: writing CTnEA with CTnCA != CTnEA starts a job that completes
 *     after a short delay and raises FLDONE (thread 0) / FRDONE (thread 1);
 *     for a render list containing TILE_RENDERING_MODE_CONFIG (linear
 *     RGBA8888) and CLEAR_COLORS, the target is filled with the clear
 *     colour, so a clear-only job produces its pixels;
 *   - hang injection: property "hang-job" makes the Nth job on thread
 *     "hang-thread" never complete until the block is power-cycled.
 *
 * The block is only reachable while the guest has released the V3D reset
 * (PM_GRAFX.V3DRSTN, see bcm2835_powermgt.c), enabled both ASB bridges and
 * turned the V3D clock on (mailbox tag 0x00038001, clock id 5). Accessing it
 * otherwise reads 0 and logs a guest error: on a real Pi that access hangs
 * the bus.
 *
 * Written from the register facts in Linux's vc4_regs.h and bcm2835-power.c,
 * the same sources the driver follows, so it checks the driver's logic and
 * plumbing, not the hardware's truth.
 */
#ifndef BCM2835_V3D_H
#define BCM2835_V3D_H

#include "hw/sysbus.h"
#include "qemu/timer.h"

#define TYPE_BCM2835_V3D "bcm2835-v3d"
#define BCM2835_V3D(obj) OBJECT_CHECK(BCM2835V3DState, (obj), TYPE_BCM2835_V3D)

typedef struct BCM2835V3DState {
    SysBusDevice busdev;
    MemoryRegion v3d_iomem;
    MemoryRegion asb_iomem;
    qemu_irq irq;

    MemoryRegion *dma_mr;
    AddressSpace dma_as;

    /* power state inputs */
    bool pm_v3d_reset_released;   /* PM_GRAFX.V3DRSTN */
    bool clock_on;                /* mailbox clock id 5 */
    uint32_t asb_s_ctrl, asb_m_ctrl;
    uint32_t asb_other[8];

    /* V3D registers */
    uint32_t intctl, intena;
    uint32_t l2cactl, slcactl;
    uint32_t bpoa, bpos, vpmbase, scratch;
    uint32_t ctca[2], ctea[2];
    bool ctrun[2];
    uint32_t ctcs_err[2];

    QEMUTimer *timer[2];
    uint32_t jobs_started[2];
    bool hung[2];

    /* properties */
    uint32_t hang_job;            /* 0 = never */
    uint32_t hang_thread;         /* 0 = binner, 1 = renderer */
    uint32_t job_delay_us;
} BCM2835V3DState;

/* Inputs from the other devices (single instance). */
void bcm2835_v3d_pm_changed(uint32_t grafx);
void bcm2835_v3d_set_clock(bool on);

#endif
