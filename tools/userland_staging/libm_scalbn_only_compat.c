/* Real, unmodified FreeBSD lib/msun/src/s_scalbn.c (double-precision
 * case only), extracted from this project's existing
 * libm_freebsd_fallback.c (that file's own header has the full real-
 * source citation and rationale) into its own standalone file.
 *
 * Why a second copy instead of just linking libm.a: libm.a's
 * libm_freebsd_fallback.o bundles scalbn together with pow/modf/fabs/
 * trunc in one object file -- launchd's own trial link only needs
 * scalbn (docs/daemon-roadmap.md's 2026-08-28 "mechanical" gap group),
 * but CoreFoundation's own cf_compat_headers/libm_compat.c already
 * provides real pow()/modf() definitions of its own (a separate, older
 * compat shim, predating this project's real libm.a). Pulling in the
 * whole libm_freebsd_fallback.o via `ar`'s archive-member granularity
 * force-loads its pow/modf too, which then hard-conflicts as duplicate
 * symbols against CF's own already-linked libm_compat.o (found via a
 * real trial link, not assumed) -- reconciling which of the two
 * pow/modf implementations "wins" project-wide is a real, separate
 * decision out of scope for this gap-closing pass. This standalone
 * scalbn-only file sidesteps the conflict entirely for the one symbol
 * actually needed here. */
#include <stdint.h>

typedef union {
    double value;
    struct {
        uint32_t lsw;
        uint32_t msw;
    } parts;
} ieee_double_shape_type;

#define EXTRACT_WORDS(ix0,ix1,d)               \
do {                                            \
    ieee_double_shape_type ew_u;                \
    ew_u.value = (d);                           \
    (ix0) = ew_u.parts.msw;                     \
    (ix1) = ew_u.parts.lsw;                     \
} while (0)

#define SET_HIGH_WORD(d,v)                      \
do {                                             \
    ieee_double_shape_type sh_u;                 \
    sh_u.value = (d);                            \
    sh_u.parts.msw = (v);                        \
    (d) = sh_u.value;                            \
} while (0)

/* ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this software is
 * freely granted, provided that this notice is preserved.
 * ====================================================
 */
double scalbn(double x, int n)
{
    int32_t k, hx, lx;
    static const double
        two54   = 1.80143985094819840000e+16, /* 0x43500000, 0x00000000 */
        twom54  = 5.55111512312578270212e-17, /* 0x3C900000, 0x00000000 */
        huge    = 1.0e+300,
        tiny    = 1.0e-300;
    EXTRACT_WORDS(hx, lx, x);
    k = (hx & 0x7ff00000) >> 20;
    if (k == 0) {
        if ((lx | (hx & 0x7fffffff)) == 0) return x;
        x *= two54;
        EXTRACT_WORDS(hx, lx, x);
        k = ((hx & 0x7ff00000) >> 20) - 54;
        if (n < -50000) return tiny * x;
    }
    if (k == 0x7ff) return x + x;
    k = k + n;
    if (k > 0x7fe) return huge * ((hx > 0) ? huge : -huge);
    if (k > 0) {
        SET_HIGH_WORD(x, (hx & 0x800fffff) | (k << 20));
        return x;
    }
    if (k <= -54) {
        if (n > 50000) return huge * ((hx > 0) ? huge : -huge);
        else return tiny * ((hx > 0) ? tiny : -tiny);
    }
    k += 54;
    SET_HIGH_WORD(x, (hx & 0x800fffff) | (k << 20));
    return x * twom54;
}
