#!/usr/bin/env python3
"""Installs the V3D test model into third_party/qemu-zhuowei (the fork's working
tree; commit it there). Idempotent. Run from the repo root."""
import os, re, shutil, sys

Q = "third_party/qemu-zhuowei"
M = "qemu/v3d_model"


def edit(path, pairs):
    s = open(path).read()
    for old, new, marker in pairs:
        if marker in s:
            continue
        assert old in s, "%s: missing anchor %r" % (path, old[:60])
        s = s.replace(old, new, 1)
    open(path, "w").write(s)


# 1. model files (qdev-properties.h does not exist in this 2.12-based tree)
shutil.copy(os.path.join(M, "bcm2835_v3d.h"), os.path.join(Q, "include/hw/misc/bcm2835_v3d.h"))
c = open(os.path.join(M, "bcm2835_v3d.c")).read().replace('#include "hw/qdev-properties.h"\n', "")
open(os.path.join(Q, "hw/misc/bcm2835_v3d.c"), "w").write(c)

# 2. build
edit(os.path.join(Q, "hw/misc/Makefile.objs"), [
    ("obj-$(CONFIG_RASPI) += bcm2835_powermgt.o\n",
     "obj-$(CONFIG_RASPI) += bcm2835_powermgt.o\nobj-$(CONFIG_RASPI) += bcm2835_v3d.o\n",
     "bcm2835_v3d.o"),
])

# 3. peripherals
edit(os.path.join(Q, "include/hw/arm/bcm2835_peripherals.h"), [
    ('#include "hw/misc/bcm2835_powermgt.h"\n', '#include "hw/misc/bcm2835_powermgt.h"\n#include "hw/misc/bcm2835_v3d.h"\n', "bcm2835_v3d.h"),
    ("    BCM2835PowerMgtState powermgt;\n", "    BCM2835PowerMgtState powermgt;\n    BCM2835V3DState v3d;\n", "BCM2835V3DState v3d"),
])
edit(os.path.join(Q, "hw/arm/bcm2835_peripherals.c"), [
    ("""    object_property_add_const_link(OBJECT(&s->dwc2), "dma-mr",
                                   OBJECT(&s->gpu_bus_mr), &error_abort);
}""", """    object_property_add_const_link(OBJECT(&s->dwc2), "dma-mr",
                                   OBJECT(&s->gpu_bus_mr), &error_abort);

    /* VideoCore IV 3D test model (DARW-295, hw/misc/bcm2835_v3d.c): reads
     * command lists from GPU bus addresses like the DWC2 does. */
    object_initialize(&s->v3d, sizeof(s->v3d), TYPE_BCM2835_V3D);
    object_property_add_child(obj, "v3d", OBJECT(&s->v3d), NULL);
    qdev_set_parent_bus(DEVICE(&s->v3d), sysbus_get_default());
    object_property_add_const_link(OBJECT(&s->v3d), "dma-mr",
                                   OBJECT(&s->gpu_bus_mr), &error_abort);
}""", "TYPE_BCM2835_V3D"),
    ("""    /* DMA Channels */
    object_property_set_bool(OBJECT(&s->dma), true, "realized", &err);""", """    /* V3D: the ASB bridges (peripheral offset 0xa000, bus 0x7e00a000) and the
     * 3D block (0xc00000, bus 0x7ec00000), IRQ 74 = GPU bank 1 bit 10. */
    object_property_set_bool(OBJECT(&s->v3d), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(&s->peri_mr, 0xc00000,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->v3d), 0));
    memory_region_add_subregion(&s->peri_mr, 0xa000,
                sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->v3d), 1));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->v3d), 0,
        qdev_get_gpio_in_named(DEVICE(&s->ic), BCM2835_IC_GPU_IRQ,
                               INTERRUPT_3D));

    /* DMA Channels */
    object_property_set_bool(OBJECT(&s->dma), true, "realized", &err);""", "OBJECT(&s->v3d), true"),
])

# 4. power manager: PM_GRAFX (bcm2835-power.c PM_GRAFX 0x10c)
edit(os.path.join(Q, "include/hw/misc/bcm2835_powermgt.h"), [
    ("    uint32_t wdog;\n} BCM2835PowerMgtState;", "    uint32_t wdog;\n    uint32_t grafx;           /* PM_GRAFX (0x10c), V3D domain, see bcm2835_v3d.c */\n    bool grafx_boot_powered;  /* firmware left GRAFX powered (the real behaviour) */\n} BCM2835PowerMgtState;", "grafx_boot_powered"),
])
pm = os.path.join(Q, "hw/misc/bcm2835_powermgt.c")
edit(pm, [
    ('#include "migration/vmstate.h"\n', '#include "migration/vmstate.h"\n#include "hw/misc/bcm2835_v3d.h"\n', "bcm2835_v3d.h"),
    ("#define R_WDOG 0x24\n", """#define R_WDOG 0x24
#define R_GRAFX 0x10c
/* PM_GRAFX bits (drivers/pmdomain/bcm/bcm2835-power.c) */
#define GRAFX_POWUP  (1u << 0)
#define GRAFX_POWOK  (1u << 1)
#define GRAFX_ISPOW  (1u << 2)
#define GRAFX_MEMREP (1u << 3)
#define GRAFX_MRDONE (1u << 4)
#define GRAFX_ISFUNC (1u << 5)
""", "R_GRAFX"),
    ("""    case R_WDOG:
        res = s->wdog;
        break;
""", """    case R_WDOG:
        res = s->wdog;
        break;
    case R_GRAFX:
        /* the handshake bits follow the request bits at once */
        res = s->grafx | ((s->grafx & GRAFX_POWUP) ? GRAFX_POWOK : 0)
                       | ((s->grafx & GRAFX_MEMREP) ? GRAFX_MRDONE : 0);
        break;
""", "case R_GRAFX:\n        /* the handshake"),
    ("""    case R_WDOG:
        qemu_log_mask(LOG_UNIMP,
                      "bcm2835_powermgt_write: WDOG\\n");
        s->wdog = value;
        break;
""", """    case R_WDOG:
        qemu_log_mask(LOG_UNIMP,
                      "bcm2835_powermgt_write: WDOG\\n");
        s->wdog = value;
        break;
    case R_GRAFX:
        s->grafx = value & ~(GRAFX_POWOK | GRAFX_MRDONE);
        bcm2835_v3d_pm_changed(s->grafx);
        break;
""", "s->grafx = value &"),
    ("""    s->wdog = 0x00000000;
}""", """    s->wdog = 0x00000000;
    /* The firmware powers the GRAFX domain at boot; V3D itself is held in
     * reset (V3DRSTN clear) until a driver releases it. */
    s->grafx = s->grafx_boot_powered ? (GRAFX_POWUP | GRAFX_ISPOW | GRAFX_ISFUNC) : 0;
}

static Property bcm2835_powermgt_props[] = {
    DEFINE_PROP_BOOL("grafx-boot-powered", BCM2835PowerMgtState, grafx_boot_powered, true),
    DEFINE_PROP_END_OF_LIST(),
};""", "bcm2835_powermgt_props"),
    ("""    dc->reset = bcm2835_powermgt_reset;
    dc->vmsd = &vmstate_bcm2835_powermgt;""", """    dc->reset = bcm2835_powermgt_reset;
    dc->props = bcm2835_powermgt_props;
    dc->vmsd = &vmstate_bcm2835_powermgt;""", "dc->props = bcm2835_powermgt_props"),
])

# 5. mailbox property: set clock state (tag 0x00038001)
prop = os.path.join(Q, "hw/misc/bcm2835_property.c")
edit(prop, [
    ('#include "hw/misc/bcm2835_property.h"\n', '#include "hw/misc/bcm2835_property.h"\n#include "hw/misc/bcm2835_v3d.h"\n', "bcm2835_v3d.h"),
    ("""        case 0x00038001: /* Set clock state */
            qemu_log_mask(LOG_UNIMP,
                          "bcm2835_property: %x set clock state NYI\\n", tag);
            resplen = 8;
            break;""", """        case 0x00038001: { /* Set clock state: {id, state} */
            uint32_t clk_id = ldl_le_phys(&s->dma_as, value + 12);
            uint32_t clk_state = ldl_le_phys(&s->dma_as, value + 16);

            stl_le_phys(&s->dma_as, value + 16, clk_state & 1);  /* present, now in that state */
            if (clk_id == 5) {                                    /* RPI_FIRMWARE_V3D_CLK_ID */
                bcm2835_v3d_set_clock(clk_state & 1);
            }
            resplen = 8;
            break;
        }""", "bcm2835_v3d_set_clock"),
])
print("installed")
