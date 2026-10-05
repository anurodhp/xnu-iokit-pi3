/*
 * VideoCore IV 3D ("V3D") register map and the BCM283x power-manager /
 * ASB-bridge registers needed to bring the V3D domain up. Offsets are
 * written from Linux's drivers/gpu/drm/vc4/vc4_regs.h (V3D block, lines
 * 36-143 of the tree read 2026-10-05) and drivers/pmdomain/bcm/
 * bcm2835-power.c (PM_/ASB_ defines, lines ~70-125). Linux is the
 * reference per the project owner (DARW-299); these are register facts,
 * re-expressed here rather than copied driver code.
 */
#ifndef _BCM2837V3DREGS_H
#define _BCM2837V3DREGS_H

#include <stdint.h>

/* ---- V3D block: bus 0x7ec00000, ARM physical 0x3fc00000, 4 KiB ---- */
enum {
	kV3D_IDENT0   = 0x00000,
	kV3D_IDENT1   = 0x00004,
	kV3D_IDENT2   = 0x00008,
	kV3D_L2CACTL  = 0x00020,
	kV3D_SLCACTL  = 0x00024,
	kV3D_INTCTL   = 0x00030,
	kV3D_INTENA   = 0x00034,
	kV3D_INTDIS   = 0x00038,
	kV3D_CT0CS    = 0x00100,
	kV3D_CT1CS    = 0x00104,
	kV3D_CT0EA    = 0x00108,
	kV3D_CT1EA    = 0x0010c,
	kV3D_CT0CA    = 0x00110,
	kV3D_CT1CA    = 0x00114,
	kV3D_CT00RA0  = 0x00118,
	kV3D_CT01RA0  = 0x0011c,
	kV3D_PCS      = 0x00130,
	kV3D_BFC      = 0x00134,
	kV3D_RFC      = 0x00138,
	kV3D_BPCA     = 0x00300,
	kV3D_BPCS     = 0x00304,
	kV3D_BPOA     = 0x00308,
	kV3D_BPOS     = 0x0030c,
	kV3D_VPMBASE  = 0x00504,
	kV3D_DBGE     = 0x00f00,
	kV3D_FDBGO    = 0x00f04,
	kV3D_FDBGB    = 0x00f08,
	kV3D_FDBGR    = 0x00f0c,
	kV3D_FDBGS    = 0x00f10,
	kV3D_ERRSTAT  = 0x00f20,
	kV3D_RegWindow = 0x1000,
};

/* IDENT0 reads (2 << 24) | 'V' | '3' << 8 | 'D' << 16 (vc4_regs.h:37-41) */
#define kV3D_ExpectedIdent0 ((2U << 24) | ('V' << 0) | ('3' << 8) | ('D' << 16))

#define kV3D_L2CACTL_L2CCLR (1U << 2)
/* SLCACTL: four 4-bit clear fields, T1CC[27:24] T0CC[19:16] UCC[11:8] ICC[3:0] */
#define kV3D_SLCACTL_ClearAll  ((0xfU << 24) | (0xfU << 16) | (0xfU << 8) | 0xfU)
#define kV3D_SLCACTL_ClearTex  ((0xfU << 24) | (0xfU << 16))

#define kV3D_INT_OUTOMEM (1U << 2)
#define kV3D_INT_FLDONE  (1U << 1)   /* binner (flush list) done */
#define kV3D_INT_FRDONE  (1U << 0)   /* renderer (frame) done */
#define kV3D_DriverIRQs  (kV3D_INT_OUTOMEM | kV3D_INT_FLDONE | kV3D_INT_FRDONE)

/* ---- Power manager (bus 0x7e100000 / ARM 0x3f100000, 0x114 bytes) ---- */
enum {
	kPM_GRAFX = 0x10c,
};
#define kPM_Password  0x5a000000U
#define kPM_V3DRSTN   (1U << 6)
#define kPM_MRDONE    (1U << 4)
#define kPM_MEMREP    (1U << 3)
#define kPM_ISPOW     (1U << 2)
#define kPM_POWOK     (1U << 1)
#define kPM_POWUP     (1U << 0)
#define kPM_ISFUNC    (1U << 5)
#define kPM_InrushShift 13
#define kPM_InrushMask  (3U << kPM_InrushShift)

/* ---- ASB bridges (bus 0x7e00a000 / ARM 0x3f00a000, 0x24 bytes) ---- */
enum {
	kASB_V3D_S_CTRL  = 0x08,
	kASB_V3D_M_CTRL  = 0x0c,
	kASB_AXI_BRDG_ID = 0x20,
};
#define kASB_REQ_STOP 1U
#define kASB_ACK      2U
#define kASB_BrdgID   0x62726467U    /* 'brdg' (bcm2835-power.c BCM2835_BRDG_ID) */

/* VideoCore bus alias of ARM physical RAM: dma-ranges = <0xc0000000 0 0x3f000000>
 * (third_party/u-boot/arch/arm/dts/bcm2837.dtsi:9). */
#define kV3D_BusAliasBase 0xC0000000U
#define kV3D_BusAliasSize 0x3F000000U

#endif /* _BCM2837V3DREGS_H */
