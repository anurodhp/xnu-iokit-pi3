/*
 * BCM2835/6/7 V3D test model; see include/hw/misc/bcm2835_v3d.h for what it
 * is and is not.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/misc/bcm2835_v3d.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "exec/address-spaces.h"

/* register offsets inside the V3D block (vc4_regs.h) */
#define V3D_IDENT0   0x000
#define V3D_IDENT1   0x004
#define V3D_IDENT2   0x008
#define V3D_SCRATCH  0x010
#define V3D_L2CACTL  0x020
#define V3D_SLCACTL  0x024
#define V3D_INTCTL   0x030
#define V3D_INTENA   0x034
#define V3D_INTDIS   0x038
#define V3D_CT0CS    0x100
#define V3D_CT1CS    0x104
#define V3D_CT0EA    0x108
#define V3D_CT1EA    0x10c
#define V3D_CT0CA    0x110
#define V3D_CT1CA    0x114
#define V3D_PCS      0x130
#define V3D_BPCA     0x300
#define V3D_BPCS     0x304
#define V3D_BPOA     0x308
#define V3D_BPOS     0x30c
#define V3D_VPMBASE  0x504
#define V3D_ERRSTAT  0xf20

#define V3D_INT_FLDONE (1u << 1)
#define V3D_INT_FRDONE (1u << 0)
#define V3D_CTRSTA     (1u << 15)
#define V3D_CTRUN      (1u << 5)

/* ASB bridges (bcm2835-power.c) */
#define ASB_V3D_S_CTRL  0x08
#define ASB_V3D_M_CTRL  0x0c
#define ASB_AXI_BRDG_ID 0x20
#define ASB_REQ_STOP    1u
#define ASB_ACK         2u
#define PASSWORD        0x5a000000u
#define PASSWORD_MASK   0xff000000u

static BCM2835V3DState *g_v3d;

static bool v3d_reachable(BCM2835V3DState *s)
{
    return s->pm_v3d_reset_released && s->clock_on &&
           !(s->asb_s_ctrl & ASB_REQ_STOP) && !(s->asb_m_ctrl & ASB_REQ_STOP);
}

static void v3d_update_irq(BCM2835V3DState *s)
{
    qemu_set_irq(s->irq, (s->intctl & s->intena) != 0);
}

/* A power cycle (reset asserted, a bridge stopped, or the clock gated) kills
 * whatever the block was doing and resets its registers. */
static void v3d_hw_reset(BCM2835V3DState *s)
{
    int t;

    for (t = 0; t < 2; t++) {
        timer_del(s->timer[t]);
        s->ctrun[t] = false;
        s->hung[t] = false;
        s->ctca[t] = s->ctea[t] = 0;
        s->ctcs_err[t] = 0;
    }
    s->intctl = 0;
    s->intena = 0;
    s->l2cactl = s->slcactl = 0;
    s->bpoa = s->bpos = 0;
    s->vpmbase = 0xffffffff;
    v3d_update_irq(s);
}

static void v3d_check_power(BCM2835V3DState *s, bool was_reachable)
{
    if (was_reachable && !v3d_reachable(s)) {
        v3d_hw_reset(s);
    }
}

void bcm2835_v3d_pm_changed(uint32_t grafx)
{
    BCM2835V3DState *s = g_v3d;
    bool was;

    if (!s) {
        return;
    }
    was = v3d_reachable(s);
    s->pm_v3d_reset_released = (grafx & (1u << 6)) != 0;      /* PM_V3DRSTN */
    v3d_check_power(s, was);
}

void bcm2835_v3d_set_clock(bool on)
{
    BCM2835V3DState *s = g_v3d;
    bool was;

    if (!s) {
        return;
    }
    was = v3d_reachable(s);
    s->clock_on = on;
    v3d_check_power(s, was);
}

/* ---- guest memory helpers (bus addresses, 0xC0000000 alias) ---- */

static bool v3d_bus_read(BCM2835V3DState *s, uint32_t addr, void *buf, int len)
{
    if ((addr >> 30) != 3 && (addr >> 30) != 1) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "bcm2835_v3d: GPU access to bus address 0x%08x outside "
                      "the 0xC0000000/0x40000000 RAM aliases\n", addr);
    }
    return address_space_rw(&s->dma_as, addr, MEMTXATTRS_UNSPECIFIED, buf, len, false) == MEMTX_OK;
}

static bool v3d_bus_write(BCM2835V3DState *s, uint32_t addr, const void *buf, int len)
{
    return address_space_rw(&s->dma_as, addr, MEMTXATTRS_UNSPECIFIED, (void *)buf, len, true) == MEMTX_OK;
}

/* Packet lengths for the render-list packets (vc4_packet.h *_SIZE). */
static int rcl_packet_len(uint8_t op)
{
    switch (op) {
    case 1: case 4: case 5: case 6: case 7: case 8: return 1;       /* NOP, FLUSH, ..., WAIT_ON_SEMAPHORE */
    case 16: case 17: return 5;                                     /* BRANCH, BRANCH_TO_SUB_LIST */
    case 24: case 25: return 1;                                     /* STORE_MS_TILE_BUFFER(_AND_EOF) */
    case 26: case 27: return 5;                                     /* FULL_RES store/load */
    case 28: case 29: return 7;                                     /* GENERAL store/load */
    case 113: return 11;                                            /* TILE_RENDERING_MODE_CONFIG */
    case 114: return 14;                                            /* CLEAR_COLORS */
    case 115: return 3;                                             /* TILE_COORDINATES */
    default: return 0;
    }
}

/*
 * The one piece of "rendering" the model does: a clear-only job (what the
 * kernel generates for a submit without a binner list) is a render list
 * with CLEAR_COLORS + TILE_RENDERING_MODE_CONFIG. If the target is linear
 * RGBA8888, write the clear colour over width x height pixels, which is
 * what the tile buffer clear plus store produces.
 */
static void v3d_run_render_list(BCM2835V3DState *s, uint32_t ca, uint32_t ea)
{
    uint8_t buf[256];
    uint32_t off = 0, len = ea - ca;
    bool have_clear = false, have_cfg = false;
    uint32_t clear = 0, target = 0, w = 0, h = 0, bits = 0;

    if (len > sizeof(buf)) {
        len = sizeof(buf);              /* the clear/config packets come first */
    }
    if (!v3d_bus_read(s, ca, buf, len)) {
        qemu_log_mask(LOG_GUEST_ERROR, "bcm2835_v3d: cannot read render list at 0x%08x\n", ca);
        return;
    }
    while (off < len) {
        uint8_t op = buf[off];
        int n = rcl_packet_len(op);

        if (n == 0 || off + n > len) {
            break;
        }
        if (op == 114) {                 /* CLEAR_COLORS: colors[2], z, s */
            memcpy(&clear, &buf[off + 1], 4);
            have_clear = true;
        } else if (op == 113) {          /* TILE_RENDERING_MODE_CONFIG: addr, w, h, bits */
            memcpy(&target, &buf[off + 1], 4);
            w = buf[off + 5] | (buf[off + 6] << 8);
            h = buf[off + 7] | (buf[off + 8] << 8);
            bits = buf[off + 9] | (buf[off + 10] << 8);
            have_cfg = true;
        }
        off += n;
    }
    /* format RGBA8888 (1) at bits [3:2], memory format linear (0) at [7:6] */
    if (have_clear && have_cfg && target && w && h &&
        ((bits >> 2) & 3) == 1 && ((bits >> 6) & 3) == 0) {
        uint32_t row[256];
        uint32_t x, y;

        for (x = 0; x < 256; x++) {
            row[x] = clear;
        }
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x += 256) {
                uint32_t n = w - x < 256 ? w - x : 256;
                if (!v3d_bus_write(s, target + (y * w + x) * 4, row, n * 4)) {
                    qemu_log_mask(LOG_GUEST_ERROR, "bcm2835_v3d: clear write failed at row %u\n", y);
                    return;
                }
            }
        }
    }
}

static void v3d_job_done(BCM2835V3DState *s, int thread)
{
    if (!s->ctrun[thread]) {
        return;
    }
    if (thread == 1) {
        v3d_run_render_list(s, s->ctca[1], s->ctea[1]);
    }
    s->ctca[thread] = s->ctea[thread];
    s->ctrun[thread] = false;
    s->intctl |= thread == 0 ? V3D_INT_FLDONE : V3D_INT_FRDONE;
    v3d_update_irq(s);
}

static void v3d_timer0(void *opaque) { v3d_job_done(opaque, 0); }
static void v3d_timer1(void *opaque) { v3d_job_done(opaque, 1); }

static void v3d_start_job(BCM2835V3DState *s, int thread)
{
    if (s->ctca[thread] == s->ctea[thread]) {
        return;
    }
    if (s->ctrun[thread]) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "bcm2835_v3d: CT%d end address written while a list is running "
                      "(the driver must not restart a busy thread)\n", thread);
        return;
    }
    s->jobs_started[thread]++;
    s->ctrun[thread] = true;
    if (s->hang_job && s->hang_thread == (uint32_t)thread &&
        s->jobs_started[thread] == s->hang_job) {
        s->hung[thread] = true;                  /* never completes until a power cycle */
        qemu_log_mask(LOG_GUEST_ERROR, "bcm2835_v3d: injected hang on thread %d, job %u\n",
                      thread, s->jobs_started[thread]);
        return;
    }
    timer_mod(s->timer[thread], qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              (int64_t)s->job_delay_us * 1000);
}

/* ---- V3D register access ---- */

static uint64_t v3d_read(void *opaque, hwaddr off, unsigned size)
{
    BCM2835V3DState *s = opaque;

    if (!v3d_reachable(s)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "bcm2835_v3d: read of V3D+0x%x while the block is unpowered/"
                      "unclocked/behind a stopped bridge (hangs the bus on hardware)\n",
                      (unsigned)off);
        return 0;
    }
    switch (off) {
    case V3D_IDENT0: return 0x02443356;      /* (2 << 24) | 'V' | '3' << 8 | 'D' << 16 */
    case V3D_IDENT1: return 0x80102431;      /* VPM 8, 16 sems, 2 TUPS, 4 QUPS, 3 slices, rev 1 */
    case V3D_IDENT2: return 0;
    case V3D_SCRATCH: return s->scratch;
    case V3D_L2CACTL: return s->l2cactl;
    case V3D_SLCACTL: return s->slcactl;
    case V3D_INTCTL: return s->intctl;
    case V3D_INTENA: return s->intena;
    case V3D_CT0CS: case V3D_CT1CS: {
        int t = off == V3D_CT1CS;
        return (s->ctrun[t] ? V3D_CTRUN : 0) | s->ctcs_err[t];
    }
    case V3D_CT0CA: return s->ctca[0];
    case V3D_CT1CA: return s->ctca[1];
    case V3D_CT0EA: return s->ctea[0];
    case V3D_CT1EA: return s->ctea[1];
    case V3D_PCS: return 0;
    case V3D_BPCA: case V3D_BPCS: return 0;
    case V3D_BPOA: return s->bpoa;
    case V3D_BPOS: return s->bpos;
    case V3D_VPMBASE: return s->vpmbase;
    case V3D_ERRSTAT: return 0;
    default:
        qemu_log_mask(LOG_UNIMP, "bcm2835_v3d: read of unmodelled register V3D+0x%x\n", (unsigned)off);
        return 0;
    }
}

static void v3d_write(void *opaque, hwaddr off, uint64_t val, unsigned size)
{
    BCM2835V3DState *s = opaque;
    uint32_t v = val;

    if (!v3d_reachable(s)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "bcm2835_v3d: write of V3D+0x%x while the block is unpowered/"
                      "unclocked/behind a stopped bridge (hangs the bus on hardware)\n",
                      (unsigned)off);
        return;
    }
    switch (off) {
    case V3D_SCRATCH: s->scratch = v; break;
    case V3D_L2CACTL: s->l2cactl = v & ~(1u << 2); break;      /* L2CCLR self-clears */
    case V3D_SLCACTL: s->slcactl = 0; break;                   /* all fields are clear commands */
    case V3D_INTCTL: s->intctl &= ~v; v3d_update_irq(s); break;     /* write 1 to clear */
    case V3D_INTENA: s->intena |= v; v3d_update_irq(s); break;
    case V3D_INTDIS: s->intena &= ~v; v3d_update_irq(s); break;
    case V3D_CT0CS: case V3D_CT1CS: {
        int t = off == V3D_CT1CS;
        if (v & V3D_CTRSTA) {                 /* thread reset */
            timer_del(s->timer[t]);
            s->ctrun[t] = false;
            s->hung[t] = false;
        }
        break;
    }
    case V3D_CT0CA: s->ctca[0] = v; break;
    case V3D_CT1CA: s->ctca[1] = v; break;
    case V3D_CT0EA: s->ctea[0] = v; v3d_start_job(s, 0); break;
    case V3D_CT1EA: s->ctea[1] = v; v3d_start_job(s, 1); break;
    case V3D_BPOA: s->bpoa = v; break;
    case V3D_BPOS: s->bpos = v; break;
    case V3D_VPMBASE: s->vpmbase = v; break;
    default:
        qemu_log_mask(LOG_UNIMP, "bcm2835_v3d: write of unmodelled register V3D+0x%x = 0x%x\n",
                      (unsigned)off, v);
        break;
    }
}

static const MemoryRegionOps v3d_ops = {
    .read = v3d_read,
    .write = v3d_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/* ---- ASB bridge registers ---- */

static uint64_t asb_read(void *opaque, hwaddr off, unsigned size)
{
    BCM2835V3DState *s = opaque;
    uint32_t v;

    switch (off) {
    case ASB_V3D_S_CTRL:
        v = s->asb_s_ctrl;
        return v | ((v & ASB_REQ_STOP) ? 0 : ASB_ACK);          /* ACK follows !REQ_STOP */
    case ASB_V3D_M_CTRL:
        v = s->asb_m_ctrl;
        return v | ((v & ASB_REQ_STOP) ? 0 : ASB_ACK);
    case ASB_AXI_BRDG_ID:
        return 0x62726467;                                       /* 'brdg' */
    case 0x00:
        return 0;                                                /* ASB_BRDG_VERSION */
    default:
        if (off < 0x24) {
            return s->asb_other[off >> 2];
        }
        return 0;
    }
}

static void asb_write(void *opaque, hwaddr off, uint64_t val, unsigned size)
{
    BCM2835V3DState *s = opaque;
    bool was = v3d_reachable(s);

    if ((val & PASSWORD_MASK) != PASSWORD) {
        qemu_log_mask(LOG_GUEST_ERROR, "bcm2835_v3d: ASB write 0x%"PRIx64" at +0x%x lacks the password\n",
                      val, (unsigned)off);
        return;
    }
    val &= ~PASSWORD_MASK;
    switch (off) {
    case ASB_V3D_S_CTRL: s->asb_s_ctrl = val & ASB_REQ_STOP; break;
    case ASB_V3D_M_CTRL: s->asb_m_ctrl = val & ASB_REQ_STOP; break;
    default:
        if (off < 0x24) {
            s->asb_other[off >> 2] = val;
        }
        break;
    }
    v3d_check_power(s, was);
}

static const MemoryRegionOps asb_ops = {
    .read = asb_read,
    .write = asb_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/* ---- device plumbing ---- */

static void bcm2835_v3d_reset(DeviceState *dev)
{
    BCM2835V3DState *s = BCM2835_V3D(dev);

    s->pm_v3d_reset_released = false;
    s->clock_on = false;
    s->asb_s_ctrl = s->asb_m_ctrl = ASB_REQ_STOP;      /* bridges come up stopped */
    memset(s->asb_other, 0, sizeof(s->asb_other));
    s->jobs_started[0] = s->jobs_started[1] = 0;
    v3d_hw_reset(s);
}

static void bcm2835_v3d_init(Object *obj)
{
    BCM2835V3DState *s = BCM2835_V3D(obj);

    memory_region_init_io(&s->v3d_iomem, obj, &v3d_ops, s, "bcm2835-v3d", 0x1000);
    memory_region_init_io(&s->asb_iomem, obj, &asb_ops, s, "bcm2835-asb", 0x24);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->v3d_iomem);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->asb_iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq);
}

static void bcm2835_v3d_realize(DeviceState *dev, Error **errp)
{
    BCM2835V3DState *s = BCM2835_V3D(dev);
    Object *obj = object_property_get_link(OBJECT(dev), "dma-mr", &error_abort);

    s->dma_mr = MEMORY_REGION(obj);
    address_space_init(&s->dma_as, s->dma_mr, "bcm2835-v3d");
    s->timer[0] = timer_new_ns(QEMU_CLOCK_VIRTUAL, v3d_timer0, s);
    s->timer[1] = timer_new_ns(QEMU_CLOCK_VIRTUAL, v3d_timer1, s);
    g_v3d = s;
}

static Property bcm2835_v3d_props[] = {
    DEFINE_PROP_UINT32("hang-job", BCM2835V3DState, hang_job, 0),
    DEFINE_PROP_UINT32("hang-thread", BCM2835V3DState, hang_thread, 1),
    DEFINE_PROP_UINT32("job-delay-us", BCM2835V3DState, job_delay_us, 200),
    DEFINE_PROP_END_OF_LIST(),
};

static void bcm2835_v3d_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = bcm2835_v3d_realize;
    dc->reset = bcm2835_v3d_reset;
    dc->props = bcm2835_v3d_props;
    /* test model: no migration state */
}

static const TypeInfo bcm2835_v3d_info = {
    .name          = TYPE_BCM2835_V3D,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835V3DState),
    .instance_init = bcm2835_v3d_init,
    .class_init    = bcm2835_v3d_class_init,
};

static void bcm2835_v3d_register_types(void)
{
    type_register_static(&bcm2835_v3d_info);
}

type_init(bcm2835_v3d_register_types)
