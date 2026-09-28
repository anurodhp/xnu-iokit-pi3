/*
 * libm_accuracy_smoketest (DAR-447): libsystem_m.dylib's sin, cos, tan,
 * atan, atan2 and log, and sinf, cosf and atan2f (real FreeBSD msun,
 * build_libm_dylib.sh), against
 * correctly rounded expected values, plus exact floor/ceil/round cases.
 * The table was generated with mpmath at 200 bits, each value rounded to
 * the nearest double. Inputs include huge arguments (1e22, 1e300) that
 * take __kernel_rem_pio2's large-argument path. Passes if every result
 * is within 1 ULP. Before DAR-447, libsystem_c's hand-written sin/cos/tan
 * were off by up to ~7e12 ULP at |x| ~ 5e8, and libsystem_m paired msun
 * callers with Apple's six-argument __kernel_rem_pio2 (sin(1e9) = 0.736).
 */
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

enum { F_SIN, F_COS, F_TAN, F_ATAN, F_ATAN2, F_LOG };
static const char *names[] = { "sin", "cos", "tan", "atan", "atan2", "log" };
static const struct { int f; double x, y, expect; } cases[] = {
    { F_SIN, 0x1.0000000000000p-1, 0x0.0p+0, 0x1.eaee8744b05f0p-2 },
    { F_COS, 0x1.0000000000000p-1, 0x0.0p+0, 0x1.c1528065b7d50p-1 },
    { F_TAN, 0x1.0000000000000p-1, 0x0.0p+0, 0x1.17b4f5bf3474ap-1 },
    { F_ATAN, 0x1.0000000000000p-1, 0x0.0p+0, 0x1.dac670561bb4fp-2 },
    { F_ATAN2, 0x1.0000000000000p-1, 0x1.3ef3cfb552d1ap+2, 0x1.999325147cb3dp-4 },
    { F_LOG, 0x1.0000000000000p-1, 0x0.0p+0, -0x1.62e42fefa39efp-1 },
    { F_SIN, -0x1.0000000000000p-1, 0x0.0p+0, -0x1.eaee8744b05f0p-2 },
    { F_COS, -0x1.0000000000000p-1, 0x0.0p+0, 0x1.c1528065b7d50p-1 },
    { F_TAN, -0x1.0000000000000p-1, 0x0.0p+0, -0x1.17b4f5bf3474ap-1 },
    { F_ATAN, -0x1.0000000000000p-1, 0x0.0p+0, -0x1.dac670561bb4fp-2 },
    { F_ATAN2, -0x1.0000000000000p-1, -0x1.2b92d2a58cd8fp+2, -0x1.8480b3856479fp+1 },
    { F_SIN, 0x1.0000000000000p+0, 0x0.0p+0, 0x1.aed548f090ceep-1 },
    { F_COS, 0x1.0000000000000p+0, 0x0.0p+0, 0x1.14a280fb5068cp-1 },
    { F_TAN, 0x1.0000000000000p+0, 0x0.0p+0, 0x1.8eb245cbee3a6p+0 },
    { F_ATAN, 0x1.0000000000000p+0, 0x0.0p+0, 0x1.921fb54442d18p-1 },
    { F_ATAN2, 0x1.0000000000000p+0, 0x1.2860bd82a4dd0p-1, 0x1.0bcad48b63b00p+0 },
    { F_LOG, 0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0 },
    { F_SIN, 0x1.0000000000000p+1, 0x0.0p+0, 0x1.d18f6ead1b446p-1 },
    { F_COS, 0x1.0000000000000p+1, 0x0.0p+0, -0x1.aa22657537205p-2 },
    { F_TAN, 0x1.0000000000000p+1, 0x0.0p+0, -0x1.17af62e0950f8p+1 },
    { F_ATAN, 0x1.0000000000000p+1, 0x0.0p+0, 0x1.1b6e192ebbe44p+0 },
    { F_ATAN2, 0x1.0000000000000p+1, -0x1.b18b50b83992ep+1, 0x1.4dd98919fa79ap+1 },
    { F_LOG, 0x1.0000000000000p+1, 0x0.0p+0, 0x1.62e42fefa39efp-1 },
    { F_SIN, 0x1.8000000000000p+1, 0x0.0p+0, 0x1.210386db6d55bp-3 },
    { F_COS, 0x1.8000000000000p+1, 0x0.0p+0, -0x1.fae04be85e5d2p-1 },
    { F_TAN, 0x1.8000000000000p+1, 0x0.0p+0, -0x1.23ef71254b86fp-3 },
    { F_ATAN, 0x1.8000000000000p+1, 0x0.0p+0, 0x1.3fc176b7a8560p+0 },
    { F_ATAN2, 0x1.8000000000000p+1, 0x1.a7d4c46869968p+1, 0x1.78e644dd6fa42p-1 },
    { F_LOG, 0x1.8000000000000p+1, 0x0.0p+0, 0x1.193ea7aad030bp+0 },
    { F_SIN, -0x1.8cccccccccccdp+1, 0x0.0p+0, -0x1.54a0f8298102ep-5 },
    { F_COS, -0x1.8cccccccccccdp+1, 0x0.0p+0, -0x1.ff8ea4756a624p-1 },
    { F_TAN, -0x1.8cccccccccccdp+1, 0x0.0p+0, 0x1.54ec734d41690p-5 },
    { F_ATAN, -0x1.8cccccccccccdp+1, 0x0.0p+0, -0x1.423db7312cebcp+0 },
    { F_ATAN2, -0x1.8cccccccccccdp+1, -0x1.51a4c0b1a87fap+1, -0x1.234e2347fd316p+1 },
    { F_SIN, 0x1.999999999999ap-4, 0x0.0p+0, 0x1.98eaecb8bcb2cp-4 },
    { F_COS, 0x1.999999999999ap-4, 0x0.0p+0, 0x1.fd712f9a817c1p-1 },
    { F_TAN, 0x1.999999999999ap-4, 0x0.0p+0, 0x1.9af8877430b80p-4 },
    { F_ATAN, 0x1.999999999999ap-4, 0x0.0p+0, 0x1.983e282e2cc4dp-4 },
    { F_ATAN2, 0x1.999999999999ap-4, 0x1.0aac5877259d8p+1, 0x1.88e79f96e4a6fp-5 },
    { F_LOG, 0x1.999999999999ap-4, 0x0.0p+0, -0x1.26bb1bbb55515p+1 },
    { F_SIN, 0x1.5798ee2308c3ap-27, 0x0.0p+0, 0x1.5798ee2308c3ap-27 },
    { F_COS, 0x1.5798ee2308c3ap-27, 0x0.0p+0, 0x1.0000000000000p+0 },
    { F_TAN, 0x1.5798ee2308c3ap-27, 0x0.0p+0, 0x1.5798ee2308c3ap-27 },
    { F_ATAN, 0x1.5798ee2308c3ap-27, 0x0.0p+0, 0x1.5798ee2308c3ap-27 },
    { F_ATAN2, 0x1.5798ee2308c3ap-27, 0x1.81d03b813d684p+0, 0x1.c7f9fe1dc7c20p-28 },
    { F_LOG, 0x1.5798ee2308c3ap-27, 0x0.0p+0, -0x1.26bb1bbb55516p+4 },
    { F_SIN, 0x1.4000000000000p+3, 0x0.0p+0, -0x1.1689ef5f34f52p-1 },
    { F_COS, 0x1.4000000000000p+3, 0x0.0p+0, -0x1.ad9ac890c6b1fp-1 },
    { F_TAN, 0x1.4000000000000p+3, 0x0.0p+0, 0x1.4bf5f34be3782p-1 },
    { F_ATAN, 0x1.4000000000000p+3, 0x0.0p+0, 0x1.789bd2c160054p+0 },
    { F_ATAN2, 0x1.4000000000000p+3, 0x1.fc4f5ebe733e0p+1, 0x1.3159deffee12bp+0 },
    { F_LOG, 0x1.4000000000000p+3, 0x0.0p+0, 0x1.26bb1bbb55516p+1 },
    { F_SIN, 0x1.9000000000000p+6, 0x0.0p+0, -0x1.03425b78c4db8p-1 },
    { F_COS, 0x1.9000000000000p+6, 0x0.0p+0, 0x1.b981dbf665fdfp-1 },
    { F_TAN, 0x1.9000000000000p+6, 0x0.0p+0, -0x1.2ca74d62b5d38p-1 },
    { F_ATAN, 0x1.9000000000000p+6, 0x0.0p+0, 0x1.8f905eb2def22p+0 },
    { F_ATAN2, 0x1.9000000000000p+6, 0x1.0b4b8673d93a4p+0, 0x1.8f73752adfe47p+0 },
    { F_LOG, 0x1.9000000000000p+6, 0x0.0p+0, 0x1.26bb1bbb55516p+2 },
    { F_SIN, 0x1.f400000000000p+9, 0x0.0p+0, 0x1.a75cc150a206bp-1 },
    { F_COS, 0x1.f400000000000p+9, 0x0.0p+0, 0x1.1ff026793f1bbp-1 },
    { F_TAN, 0x1.f400000000000p+9, 0x0.0p+0, 0x1.786729f34311ap+0 },
    { F_ATAN, 0x1.f400000000000p+9, 0x0.0p+0, 0x1.91de2c0e658bdp+0 },
    { F_ATAN2, 0x1.f400000000000p+9, -0x1.a3e3c32183770p-1, 0x1.92557435893efp+0 },
    { F_LOG, 0x1.f400000000000p+9, 0x0.0p+0, 0x1.ba18a998fffa0p+2 },
    { F_SIN, -0x1.81cd6c8b43958p+13, 0x0.0p+0, 0x1.687d5890974a5p-1 },
    { F_COS, -0x1.81cd6c8b43958p+13, 0x0.0p+0, 0x1.6b94c3bbe24b8p-1 },
    { F_TAN, -0x1.81cd6c8b43958p+13, 0x0.0p+0, 0x1.fba5836323a4ep-1 },
    { F_ATAN, -0x1.81cd6c8b43958p+13, 0x0.0p+0, -0x1.921a664fe1dc4p+0 },
    { F_ATAN2, -0x1.81cd6c8b43958p+13, 0x1.741c970da1ec0p+1, -0x1.9210469e8416ap+0 },
    { F_SIN, 0x1.e848000000000p+19, 0x0.0p+0, -0x1.6664b2568d867p-2 },
    { F_COS, 0x1.e848000000000p+19, 0x0.0p+0, 0x1.df9df9906d32cp-1 },
    { F_TAN, 0x1.e848000000000p+19, 0x0.0p+0, -0x1.7e9768ab734c0p-2 },
    { F_ATAN, 0x1.e848000000000p+19, 0x0.0p+0, 0x1.921fa47d4b30dp+0 },
    { F_ATAN2, 0x1.e848000000000p+19, 0x1.1411e5fade51ap+1, 0x1.921f9114e44d4p+0 },
    { F_LOG, 0x1.e848000000000p+19, 0x0.0p+0, 0x1.ba18a998fffa0p+3 },
    { F_SIN, 0x1.dcd6500000000p+29, 0x0.0p+0, 0x1.1778cae83c69bp-1 },
    { F_COS, 0x1.dcd6500000000p+29, 0x0.0p+0, 0x1.acff8c7364234p-1 },
    { F_TAN, 0x1.dcd6500000000p+29, 0x0.0p+0, 0x1.4d8b249e3dba7p-1 },
    { F_ATAN, 0x1.dcd6500000000p+29, 0x0.0p+0, 0x1.921fb53ff74e9p+0 },
    { F_ATAN2, 0x1.dcd6500000000p+29, 0x1.29432da83521ep+2, 0x1.921fb5304fe39p+0 },
    { F_LOG, 0x1.dcd6500000000p+29, 0x0.0p+0, 0x1.4b927f32bffb8p+4 },
    { F_SIN, 0x1.921fb54442d18p+1, 0x0.0p+0, 0x1.1a62633145c07p-53 },
    { F_COS, 0x1.921fb54442d18p+1, 0x0.0p+0, -0x1.0000000000000p+0 },
    { F_TAN, 0x1.921fb54442d18p+1, 0x0.0p+0, -0x1.1a62633145c07p-53 },
    { F_ATAN, 0x1.921fb54442d18p+1, 0x0.0p+0, 0x1.433b8a322ddd3p+0 },
    { F_ATAN2, 0x1.921fb54442d18p+1, 0x1.6cd00a5ff02bcp+1, 0x1.ab034af6fef41p-1 },
    { F_LOG, 0x1.921fb54442d18p+1, 0x0.0p+0, 0x1.250d048e7a1bdp+0 },
    { F_SIN, 0x1.921fb54442d18p+0, 0x0.0p+0, 0x1.0000000000000p+0 },
    { F_COS, 0x1.921fb54442d18p+0, 0x0.0p+0, 0x1.1a62633145c07p-54 },
    { F_TAN, 0x1.921fb54442d18p+0, 0x0.0p+0, 0x1.d02967c31cdb5p+53 },
    { F_ATAN, 0x1.921fb54442d18p+0, 0x0.0p+0, 0x1.00fe987ed02ffp+0 },
    { F_ATAN2, 0x1.921fb54442d18p+0, -0x1.5cc78d27c840cp+1, 0x1.4f2ffe6402c03p+1 },
    { F_LOG, 0x1.921fb54442d18p+0, 0x0.0p+0, 0x1.ce6bb25aa1315p-2 },
    { F_SIN, 0x1.921fb54442d18p-1, 0x0.0p+0, 0x1.6a09e667f3bccp-1 },
    { F_COS, 0x1.921fb54442d18p-1, 0x0.0p+0, 0x1.6a09e667f3bcdp-1 },
    { F_TAN, 0x1.921fb54442d18p-1, 0x0.0p+0, 0x1.fffffffffffffp-1 },
    { F_ATAN, 0x1.921fb54442d18p-1, 0x0.0p+0, 0x1.54e04c05d06a0p-1 },
    { F_ATAN2, 0x1.921fb54442d18p-1, 0x1.4ac28ecb8faa0p+0, 0x1.17a68a638a6cfp-1 },
    { F_LOG, 0x1.921fb54442d18p-1, 0x0.0p+0, -0x1.eeb95b094c193p-3 },
    { F_SIN, 0x1.921fb54442d18p+2, 0x0.0p+0, -0x1.1a62633145c07p-52 },
    { F_COS, 0x1.921fb54442d18p+2, 0x0.0p+0, 0x1.0000000000000p+0 },
    { F_TAN, 0x1.921fb54442d18p+2, 0x0.0p+0, -0x1.1a62633145c07p-52 },
    { F_ATAN, 0x1.921fb54442d18p+2, 0x0.0p+0, 0x1.69b8154baf42ep+0 },
    { F_ATAN2, 0x1.921fb54442d18p+2, -0x1.b8289091f4240p-1, 0x1.b4ef35430478dp+0 },
    { F_LOG, 0x1.921fb54442d18p+2, 0x0.0p+0, 0x1.d67f1c864beb4p+0 },
    { F_SIN, 0x1.c6bf526340000p+49, 0x0.0p+0, 0x1.b76f88136cebap-1 },
    { F_COS, 0x1.c6bf526340000p+49, 0x0.0p+0, -0x1.06c154609d33fp-1 },
    { F_TAN, 0x1.c6bf526340000p+49, 0x0.0p+0, -0x1.ac23600a95be4p+0 },
    { F_ATAN, 0x1.c6bf526340000p+49, 0x0.0p+0, 0x1.921fb54442d14p+0 },
    { F_ATAN2, 0x1.c6bf526340000p+49, -0x1.1a5008b0b70acp+0, 0x1.921fb54442d1dp+0 },
    { F_LOG, 0x1.c6bf526340000p+49, 0x0.0p+0, 0x1.144f69ff9ffc4p+5 },
    { F_SIN, 0x1.0f0cf064dd592p+73, 0x0.0p+0, -0x1.b453ab76bf397p-1 },
    { F_COS, 0x1.0f0cf064dd592p+73, 0x0.0p+0, 0x1.0be2cef01c8f4p-1 },
    { F_TAN, 0x1.0f0cf064dd592p+73, 0x0.0p+0, -0x1.a0f79c1b6b257p+0 },
    { F_ATAN, 0x1.0f0cf064dd592p+73, 0x0.0p+0, 0x1.921fb54442d18p+0 },
    { F_ATAN2, 0x1.0f0cf064dd592p+73, -0x1.524d9abdc2a14p+1, 0x1.921fb54442d18p+0 },
    { F_LOG, 0x1.0f0cf064dd592p+73, 0x0.0p+0, 0x1.95414621954fep+5 },
    { F_SIN, 0x1.249ad2594c37dp+332, 0x0.0p+0, -0x1.85c5e5b929359p-2 },
    { F_COS, 0x1.249ad2594c37dp+332, 0x0.0p+0, 0x1.d9757496841f5p-1 },
    { F_TAN, 0x1.249ad2594c37dp+332, 0x0.0p+0, -0x1.a5807d6f76f7dp-2 },
    { F_ATAN, 0x1.249ad2594c37dp+332, 0x0.0p+0, 0x1.921fb54442d18p+0 },
    { F_ATAN2, 0x1.249ad2594c37dp+332, -0x1.c609babd24c9cp+1, 0x1.921fb54442d18p+0 },
    { F_LOG, 0x1.249ad2594c37dp+332, 0x0.0p+0, 0x1.cc845b54b54f2p+7 },
    { F_SIN, 0x1.7e43c8800759cp+996, 0x0.0p+0, -0x1.a2c16b010e385p-1 },
    { F_COS, 0x1.7e43c8800759cp+996, 0x0.0p+0, -0x1.2699022adc4c1p-1 },
    { F_TAN, 0x1.7e43c8800759cp+996, 0x0.0p+0, 0x1.6be411f37ac77p+0 },
    { F_ATAN, 0x1.7e43c8800759cp+996, 0x0.0p+0, 0x1.921fb54442d18p+0 },
    { F_ATAN2, 0x1.7e43c8800759cp+996, -0x1.0285b8fcee28ap+0, 0x1.921fb54442d18p+0 },
    { F_LOG, 0x1.7e43c8800759cp+996, 0x0.0p+0, 0x1.5963447f87fb5p+9 },
    { F_SIN, -0x1.0f0cf064dd592p+73, 0x0.0p+0, 0x1.b453ab76bf397p-1 },
    { F_COS, -0x1.0f0cf064dd592p+73, 0x0.0p+0, 0x1.0be2cef01c8f4p-1 },
    { F_TAN, -0x1.0f0cf064dd592p+73, 0x0.0p+0, 0x1.a0f79c1b6b257p+0 },
    { F_ATAN, -0x1.0f0cf064dd592p+73, 0x0.0p+0, -0x1.921fb54442d18p+0 },
    { F_ATAN2, -0x1.0f0cf064dd592p+73, -0x1.2ebf2834c07ecp+2, -0x1.921fb54442d18p+0 },
    { F_SIN, 0x1.6300000000000p+8, 0x0.0p+0, -0x1.f9bd0307d1de3p-16 },
    { F_COS, 0x1.6300000000000p+8, 0x0.0p+0, -0x1.fffffffc18e4cp-1 },
    { F_TAN, 0x1.6300000000000p+8, 0x0.0p+0, 0x1.f9bd030bacc16p-16 },
    { F_ATAN, 0x1.6300000000000p+8, 0x0.0p+0, 0x1.916719a0d5f4ep+0 },
    { F_ATAN2, 0x1.6300000000000p+8, -0x1.1add1b4645ba0p-1, 0x1.9285b2c291d05p+0 },
    { F_LOG, 0x1.6300000000000p+8, 0x0.0p+0, 0x1.77d0c72203a16p+2 },
    { F_SIN, 0x1.979c000000000p+15, 0x0.0p+0, -0x1.ffffffffdea62p-1 },
    { F_COS, 0x1.979c000000000p+15, 0x0.0p+0, 0x1.719a47d413c35p-18 },
    { F_TAN, 0x1.979c000000000p+15, 0x0.0p+0, -0x1.62a125d994c4ap+17 },
    { F_ATAN, 0x1.979c000000000p+15, 0x0.0p+0, 0x1.921e73b431142p+0 },
    { F_ATAN2, 0x1.979c000000000p+15, 0x1.97e2c1e7078a4p+1, 0x1.921bb49280fd9p+0 },
    { F_LOG, 0x1.979c000000000p+15, 0x0.0p+0, 0x1.5b984924fb0b3p+3 },
    { F_SIN, -0x1.c7945a4cde730p+0, 0x0.0p+0, -0x1.f4e0dca536999p-1 },
    { F_COS, -0x1.c7945a4cde730p+0, 0x0.0p+0, -0x1.a88b5168aaa1cp-3 },
    { F_TAN, -0x1.c7945a4cde730p+0, 0x0.0p+0, 0x1.2e078b2bf0806p+2 },
    { F_ATAN, -0x1.c7945a4cde730p+0, 0x0.0p+0, -0x1.0f108cf2513dep+0 },
    { F_ATAN2, -0x1.c7945a4cde730p+0, 0x1.06cf8450d4050p+1, -0x1.6da326532ae6dp-1 },
    { F_SIN, 0x1.03315c13bd420p-1, 0x0.0p+0, 0x1.f086ad395f77ap-2 },
    { F_COS, 0x1.03315c13bd420p-1, 0x0.0p+0, 0x1.bfc8678b2c583p-1 },
    { F_TAN, 0x1.03315c13bd420p-1, 0x0.0p+0, 0x1.1bdde641cb2d9p-1 },
    { F_ATAN, 0x1.03315c13bd420p-1, 0x0.0p+0, 0x1.dfdef315b9f79p-2 },
    { F_ATAN2, 0x1.03315c13bd420p-1, 0x1.7dc8f05f34c9cp+0, 0x1.4f191fb631a48p-2 },
    { F_LOG, 0x1.03315c13bd420p-1, 0x0.0p+0, -0x1.5c8b93f5a59b5p-1 },
    { F_SIN, 0x1.f18e33f0fc6e0p+0, 0x0.0p+0, 0x1.dcd5c88e84b32p-1 },
    { F_COS, 0x1.f18e33f0fc6e0p+0, 0x0.0p+0, -0x1.74f255bf73066p-2 },
    { F_TAN, 0x1.f18e33f0fc6e0p+0, 0x0.0p+0, -0x1.474fc9fa05fb0p+1 },
    { F_ATAN, 0x1.f18e33f0fc6e0p+0, 0x0.0p+0, 0x1.18797ee0791b8p+0 },
    { F_ATAN2, 0x1.f18e33f0fc6e0p+0, -0x1.83adee58ede48p+0, 0x1.1dc9493f63932p+1 },
    { F_LOG, 0x1.f18e33f0fc6e0p+0, 0x0.0p+0, 0x1.543d3a114b005p-1 },
    { F_SIN, -0x1.c4c49ffb802e0p+0, 0x0.0p+0, -0x1.f60384aeb20a4p-1 },
    { F_COS, -0x1.c4c49ffb802e0p+0, 0x0.0p+0, -0x1.9284203a20896p-3 },
    { F_TAN, -0x1.c4c49ffb802e0p+0, 0x0.0p+0, 0x1.3f47ca2bedb4fp+2 },
    { F_ATAN, -0x1.c4c49ffb802e0p+0, 0x0.0p+0, -0x1.0e63042dcb30fp+0 },
    { F_ATAN2, -0x1.c4c49ffb802e0p+0, -0x1.dac4f5fab741ap+1, -0x1.592bea5225382p+1 },
    { F_SIN, 0x1.6655a5e855b7cp+1, 0x0.0p+0, 0x1.578564fa78711p-2 },
    { F_COS, 0x1.6655a5e855b7cp+1, 0x0.0p+0, -0x1.e25480f377a6cp-1 },
    { F_TAN, 0x1.6655a5e855b7cp+1, 0x0.0p+0, -0x1.6ca6fea9788ffp-2 },
    { F_ATAN, 0x1.6655a5e855b7cp+1, 0x0.0p+0, 0x1.3a4b801ee8f9fp+0 },
    { F_ATAN2, 0x1.6655a5e855b7cp+1, 0x1.a01c399cb5d3cp+0, 0x1.0b74cd1511c81p+0 },
    { F_LOG, 0x1.6655a5e855b7cp+1, 0x0.0p+0, 0x1.07892bdf0c1cfp+0 },
    { F_SIN, -0x1.0da616992380cp+1, 0x0.0p+0, -0x1.b83d4e245cc94p-1 },
    { F_COS, -0x1.0da616992380cp+1, 0x0.0p+0, -0x1.0567fd490331ep-1 },
    { F_TAN, -0x1.0da616992380cp+1, 0x0.0p+0, 0x1.af228031c1160p+0 },
    { F_ATAN, -0x1.0da616992380cp+1, 0x0.0p+0, -0x1.20aa5f727f2c3p+0 },
    { F_ATAN2, -0x1.0da616992380cp+1, -0x1.1697f6ed8e244p+0, -0x1.06188fefba57cp+1 },
    { F_SIN, 0x1.f5e7c9fc74240p+2, 0x0.0p+0, 0x1.fff703b8047abp-1 },
    { F_COS, 0x1.f5e7c9fc74240p+2, 0x0.0p+0, 0x1.7faef3221d580p-7 },
    { F_TAN, 0x1.f5e7c9fc74240p+2, 0x0.0p+0, 0x1.5597713400f15p+6 },
    { F_ATAN, 0x1.f5e7c9fc74240p+2, 0x0.0p+0, 0x1.71a7cd869c9f8p+0 },
    { F_ATAN2, 0x1.f5e7c9fc74240p+2, 0x1.de70ff89c2130p-2, 0x1.82e3ce3b58487p+0 },
    { F_LOG, 0x1.f5e7c9fc74240p+2, 0x0.0p+0, 0x1.079ea24a2db42p+1 },
    { F_SIN, -0x1.e2f31ba6f087ap+2, 0x0.0p+0, -0x1.e7ec376579e0ap-1 },
    { F_COS, -0x1.e2f31ba6f087ap+2, 0x0.0p+0, 0x1.36533aa518eb8p-2 },
    { F_TAN, -0x1.e2f31ba6f087ap+2, 0x0.0p+0, -0x1.9281f20277ffcp+1 },
    { F_ATAN, -0x1.e2f31ba6f087ap+2, 0x0.0p+0, -0x1.706540baa7f08p+0 },
    { F_ATAN2, -0x1.e2f31ba6f087ap+2, -0x1.44122850f6f07p+1, -0x1.e4fe610cbd0c9p+0 },
    { F_SIN, 0x1.256d344d88902p+3, 0x0.0p+0, 0x1.027eae99d194fp-2 },
    { F_COS, 0x1.256d344d88902p+3, 0x0.0p+0, -0x1.ef6b093428d03p-1 },
    { F_TAN, 0x1.256d344d88902p+3, 0x0.0p+0, -0x1.0b2594e3608a3p-2 },
    { F_ATAN, 0x1.256d344d88902p+3, 0x0.0p+0, 0x1.7650bb5bf98c9p+0 },
    { F_ATAN2, 0x1.256d344d88902p+3, 0x1.b6415731fccdcp+1, 0x1.36a39ec1101cfp+0 },
    { F_LOG, 0x1.256d344d88902p+3, 0x0.0p+0, 0x1.1ba255946e1bbp+1 },
    { F_SIN, 0x1.c9a11e4e86a08p+2, 0x0.0p+0, 0x1.866f51180a2c4p-1 },
    { F_COS, 0x1.c9a11e4e86a08p+2, 0x0.0p+0, 0x1.4b378787469c3p-1 },
    { F_TAN, 0x1.c9a11e4e86a08p+2, 0x0.0p+0, 0x1.2dc51c9b0f279p+0 },
    { F_ATAN, 0x1.c9a11e4e86a08p+2, 0x0.0p+0, 0x1.6e8d7bb5cbac1p+0 },
    { F_ATAN2, 0x1.c9a11e4e86a08p+2, -0x1.cbfd19405ea9bp+1, 0x1.04ac29be774abp+1 },
    { F_LOG, 0x1.c9a11e4e86a08p+2, 0x0.0p+0, 0x1.f798e1a11286bp+0 },
    { F_SIN, -0x1.0c8030d80fa3dp+3, 0x0.0p+0, -0x1.b80598f055970p-1 },
    { F_COS, -0x1.0c8030d80fa3dp+3, 0x0.0p+0, -0x1.05c5b8202e9f8p-1 },
    { F_TAN, -0x1.0c8030d80fa3dp+3, 0x0.0p+0, 0x1.ae51a65ac3c6bp+0 },
    { F_ATAN, -0x1.0c8030d80fa3dp+3, 0x0.0p+0, -0x1.73c1c6b6b633ep+0 },
    { F_ATAN2, -0x1.0c8030d80fa3dp+3, -0x1.a213700f37280p-3, -0x1.9859d6c18b00bp+0 },
    { F_SIN, -0x1.ef34aa3eea300p-1, 0x0.0p+0, -0x1.a5876eb3056dap-1 },
    { F_COS, -0x1.ef34aa3eea300p-1, 0x0.0p+0, 0x1.229d86c36340ap-1 },
    { F_TAN, -0x1.ef34aa3eea300p-1, 0x0.0p+0, -0x1.73520c47a0522p+0 },
    { F_ATAN, -0x1.ef34aa3eea300p-1, 0x0.0p+0, -0x1.8996663612a56p-1 },
    { F_ATAN2, -0x1.ef34aa3eea300p-1, -0x1.7f59224b34133p+1, -0x1.6a23d14d49347p+1 },
    { F_SIN, 0x1.0c81709d94d20p+3, 0x0.0p+0, 0x1.b7fb60bfb661cp-1 },
    { F_COS, 0x1.0c81709d94d20p+3, 0x0.0p+0, -0x1.05d6e500f5f59p-1 },
    { F_TAN, 0x1.0c81709d94d20p+3, 0x0.0p+0, -0x1.ae2b6e7a8058fp+0 },
    { F_ATAN, 0x1.0c81709d94d20p+3, 0x0.0p+0, 0x1.73c1ea8a52ea2p+0 },
    { F_ATAN2, 0x1.0c81709d94d20p+3, 0x1.ea27d145c28b8p+0, 0x1.58b0fe6603451p+0 },
    { F_LOG, 0x1.0c81709d94d20p+3, 0x0.0p+0, 0x1.1045fdb99601bp+1 },
    { F_SIN, 0x1.bcbc9d47760c0p+2, 0x0.0p+0, 0x1.3c446ceb9cd72p-1 },
    { F_COS, 0x1.bcbc9d47760c0p+2, 0x0.0p+0, 0x1.92a3d9c5cf025p-1 },
    { F_TAN, 0x1.bcbc9d47760c0p+2, 0x0.0p+0, 0x1.922af38149ad3p-1 },
    { F_ATAN, 0x1.bcbc9d47760c0p+2, 0x0.0p+0, 0x1.6d890808f7a00p+0 },
    { F_ATAN2, 0x1.bcbc9d47760c0p+2, 0x1.1b2597d3e1cdcp+0, 0x1.69b769ff1c13ep+0 },
    { F_LOG, 0x1.bcbc9d47760c0p+2, 0x0.0p+0, 0x1.f0480ea944f05p+0 },
    { F_SIN, -0x1.8f9ae50ca6d43p+2, 0x0.0p+0, 0x1.4252cda71b1f8p-5 },
    { F_COS, -0x1.8f9ae50ca6d43p+2, 0x0.0p+0, 0x1.ff9a80d692583p-1 },
    { F_TAN, -0x1.8f9ae50ca6d43p+2, 0x0.0p+0, 0x1.4292bfb76d32dp-5 },
    { F_ATAN, -0x1.8f9ae50ca6d43p+2, 0x0.0p+0, -0x1.6977f8ea7dc05p+0 },
    { F_ATAN2, -0x1.8f9ae50ca6d43p+2, 0x1.bf0f9af2bb2b0p+1, -0x1.0f8f5c3e06eadp+0 },
};

enum { F_SINF, F_COSF, F_ATAN2F };
static const char *fnames[] = { "sinf", "cosf", "atan2f" };
static const struct { int f; float x, y, expect; } fcases[] = {
    { F_SINF, 0x1.0000000000000p-1f, 0x0p+0f, 0x1.eaee880000000p-2f },
    { F_COSF, 0x1.0000000000000p-1f, 0x0p+0f, 0x1.c152800000000p-1f },
    { F_ATAN2F, 0x1.0000000000000p-1f, 0x1.1317ee0000000p+2f, 0x1.da54ec0000000p-4f },
    { F_SINF, -0x1.0000000000000p-1f, 0x0p+0f, -0x1.eaee880000000p-2f },
    { F_COSF, -0x1.0000000000000p-1f, 0x0p+0f, 0x1.c152800000000p-1f },
    { F_ATAN2F, -0x1.0000000000000p-1f, -0x1.3beede0000000p+2f, -0x1.8534020000000p+1f },
    { F_SINF, 0x1.0000000000000p+0f, 0x0p+0f, 0x1.aed5480000000p-1f },
    { F_COSF, 0x1.0000000000000p+0f, 0x0p+0f, 0x1.14a2800000000p-1f },
    { F_ATAN2F, 0x1.0000000000000p+0f, -0x1.2a08a60000000p+2f, 0x1.770c580000000p+1f },
    { F_SINF, 0x1.8000000000000p+1f, 0x0p+0f, 0x1.2103860000000p-3f },
    { F_COSF, 0x1.8000000000000p+1f, 0x0p+0f, -0x1.fae04c0000000p-1f },
    { F_ATAN2F, 0x1.8000000000000p+1f, 0x1.3eef5e0000000p-1f, 0x1.5db6780000000p+0f },
    { F_SINF, 0x1.4000000000000p+3f, 0x0p+0f, -0x1.1689f00000000p-1f },
    { F_COSF, 0x1.4000000000000p+3f, 0x0p+0f, -0x1.ad9ac80000000p-1f },
    { F_ATAN2F, 0x1.4000000000000p+3f, 0x1.327ce40000000p+2f, 0x1.1fca560000000p+0f },
    { F_SINF, 0x1.9000000000000p+6f, 0x0p+0f, -0x1.03425c0000000p-1f },
    { F_COSF, 0x1.9000000000000p+6f, 0x0p+0f, 0x1.b981dc0000000p-1f },
    { F_ATAN2F, 0x1.9000000000000p+6f, 0x1.2b1d420000000p+2f, 0x1.862b020000000p+0f },
    { F_SINF, 0x1.3880000000000p+13f, 0x0p+0f, -0x1.38f2fa0000000p-2f },
    { F_COSF, 0x1.3880000000000p+13f, 0x0p+0f, -0x1.e780e80000000p-1f },
    { F_ATAN2F, 0x1.3880000000000p+13f, 0x1.1ea19c0000000p+1f, 0x1.9211080000000p+0f },
    { F_SINF, 0x1.e848000000000p+19f, 0x0p+0f, -0x1.6664b20000000p-2f },
    { F_COSF, 0x1.e848000000000p+19f, 0x0p+0f, 0x1.df9dfa0000000p-1f },
    { F_ATAN2F, 0x1.e848000000000p+19f, -0x1.47a6040000000p+1f, 0x1.921fe00000000p+0f },
    { F_SINF, 0x1.faa3b40000000p+34f, 0x0p+0f, 0x1.9dccba0000000p-1f },
    { F_COSF, 0x1.faa3b40000000p+34f, 0x0p+0f, 0x1.2d84e60000000p-1f },
    { F_ATAN2F, 0x1.faa3b40000000p+34f, -0x1.3dfe9e0000000p+2f, 0x1.921fb60000000p+0f },
    { F_SINF, 0x1.5af1d80000000p+66f, 0x0p+0f, 0x1.502ad20000000p-1f },
    { F_COSF, 0x1.5af1d80000000p+66f, 0x0p+0f, 0x1.822e460000000p-1f },
    { F_ATAN2F, 0x1.5af1d80000000p+66f, -0x1.c2ab440000000p-5f, 0x1.921fb60000000p+0f },
    { F_SINF, -0x1.93e5940000000p+99f, 0x0p+0f, 0x1.9513600000000p-1f },
    { F_COSF, -0x1.93e5940000000p+99f, 0x0p+0f, -0x1.3924440000000p-1f },
    { F_ATAN2F, -0x1.93e5940000000p+99f, -0x1.214ec60000000p+0f, -0x1.921fb60000000p+0f },
    { F_SINF, 0x1.c363cc0000000p+127f, 0x0p+0f, 0x1.bff3880000000p-1f },
    { F_COSF, 0x1.c363cc0000000p+127f, 0x0p+0f, -0x1.efeaf80000000p-2f },
    { F_ATAN2F, 0x1.c363cc0000000p+127f, -0x1.ac87d40000000p+0f, 0x1.921fb60000000p+0f },
    { F_SINF, 0x1.e240ca0000000p+16f, 0x0p+0f, -0x1.ff507a0000000p-1f },
    { F_COSF, 0x1.e240ca0000000p+16f, 0x0p+0f, 0x1.a7d00e0000000p-5f },
    { F_ATAN2F, 0x1.e240ca0000000p+16f, 0x1.74648a0000000p+1f, 0x1.921e2a0000000p+0f },
    { F_SINF, 0x1.0a43260000000p+4f, 0x0p+0f, -0x1.9b7a1e0000000p-1f },
    { F_COSF, 0x1.0a43260000000p+4f, 0x0p+0f, -0x1.30ae680000000p-1f },
    { F_ATAN2F, 0x1.0a43260000000p+4f, -0x1.8ca75a0000000p+0f, 0x1.a9e4060000000p+0f },
    { F_SINF, 0x1.87feee0000000p+2f, 0x0p+0f, -0x1.42bf0a0000000p-3f },
    { F_COSF, 0x1.87feee0000000p+2f, 0x0p+0f, 0x1.f99a300000000p-1f },
    { F_ATAN2F, 0x1.87feee0000000p+2f, -0x1.58fa200000000p+1f, 0x1.fc3e480000000p+0f },
    { F_SINF, -0x1.07d8080000000p+4f, 0x0p+0f, 0x1.68e8560000000p-1f },
    { F_COSF, -0x1.07d8080000000p+4f, 0x0p+0f, -0x1.6b2a900000000p-1f },
    { F_ATAN2F, -0x1.07d8080000000p+4f, 0x1.d33a220000000p-1f, -0x1.83f8b60000000p+0f },
    { F_SINF, 0x1.bf78ac0000000p+3f, 0x0p+0f, 0x1.f9f71e0000000p-1f },
    { F_COSF, 0x1.bf78ac0000000p+3f, 0x0p+0f, 0x1.39832a0000000p-3f },
    { F_ATAN2F, 0x1.bf78ac0000000p+3f, -0x1.9607dc0000000p+1f, 0x1.cb3af60000000p+0f },
    { F_SINF, 0x1.1e57260000000p-1f, 0x0p+0f, 0x1.0fa54a0000000p-1f },
    { F_COSF, 0x1.1e57260000000p-1f, 0x0p+0f, 0x1.b1ff020000000p-1f },
    { F_ATAN2F, 0x1.1e57260000000p-1f, -0x1.3157500000000p+2f, 0x1.83300a0000000p+1f },
    { F_SINF, 0x1.27fae40000000p+3f, 0x0p+0f, 0x1.65622c0000000p-3f },
    { F_COSF, 0x1.27fae40000000p+3f, 0x0p+0f, -0x1.f824e60000000p-1f },
    { F_ATAN2F, 0x1.27fae40000000p+3f, -0x1.0fb1400000000p+1f, 0x1.cbdf4a0000000p+0f },
    { F_SINF, 0x1.3d0ff60000000p+2f, 0x0p+0f, -0x1.f11dbe0000000p-1f },
    { F_COSF, 0x1.3d0ff60000000p+2f, 0x0p+0f, 0x1.ea37640000000p-3f },
    { F_ATAN2F, 0x1.3d0ff60000000p+2f, 0x1.1120b20000000p+2f, 0x1.b82af00000000p-1f },
    { F_SINF, -0x1.7e61e60000000p+0f, 0x0p+0f, -0x1.fe7a780000000p-1f },
    { F_COSF, -0x1.7e61e60000000p+0f, 0x0p+0f, 0x1.3b8cd60000000p-4f },
    { F_ATAN2F, -0x1.7e61e60000000p+0f, 0x1.2237fc0000000p-1f, -0x1.3545dc0000000p+0f },
    { F_SINF, 0x1.c8deba0000000p+2f, 0x0p+0f, 0x1.827a460000000p-1f },
    { F_COSF, 0x1.c8deba0000000p+2f, 0x0p+0f, 0x1.4fd36e0000000p-1f },
    { F_ATAN2F, 0x1.c8deba0000000p+2f, 0x1.34603e0000000p+0f, 0x1.67541e0000000p+0f },
    { F_SINF, 0x1.2af4680000000p+2f, 0x0p+0f, -0x1.ff90a80000000p-1f },
    { F_COSF, 0x1.2af4680000000p+2f, 0x0p+0f, -0x1.51977e0000000p-5f },
    { F_ATAN2F, 0x1.2af4680000000p+2f, 0x1.e079bc0000000p+0f, 0x1.30516c0000000p+0f },
};

static int32_t fordinal(float v)
{
    int32_t i;
    memcpy(&i, &v, sizeof i);
    return i < 0 ? INT32_MIN - i : i;
}

static int64_t ordinal(double v)
{
    int64_t i;
    memcpy(&i, &v, sizeof i);
    return i < 0 ? INT64_MIN - i : i;   /* monotonic across zero */
}

int main(void)
{
    int failures = 0;
    unsigned i;
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        double x = cases[i].x, r = 0;
        switch (cases[i].f) {
        case F_SIN: r = sin(x); break;
        case F_COS: r = cos(x); break;
        case F_TAN: r = tan(x); break;
        case F_ATAN: r = atan(x); break;
        case F_ATAN2: r = atan2(x, cases[i].y); break;
        case F_LOG: r = log(x); break;
        }
        int64_t d = ordinal(r) - ordinal(cases[i].expect);
        if (d > 1 || d < -1) {
            printf("FAIL %s(%a%s) = %a, expected %a (%lld ulp)\n", names[cases[i].f], x,
                   cases[i].f == F_ATAN2 ? ", y" : "", r, cases[i].expect, (long long)d);
            failures++;
        }
    }
    for (i = 0; i < sizeof fcases / sizeof fcases[0]; i++) {
        float x = fcases[i].x, r = 0;
        switch (fcases[i].f) {
        case F_SINF: r = sinf(x); break;
        case F_COSF: r = cosf(x); break;
        case F_ATAN2F: r = atan2f(x, fcases[i].y); break;
        }
        int32_t d = fordinal(r) - fordinal(fcases[i].expect);
        if (d > 1 || d < -1) {
            printf("FAIL %s(%a) = %a, expected %a (%d ulp)\n", fnames[fcases[i].f], (double)x,
                   (double)r, (double)fcases[i].expect, (int)d);
            failures++;
        }
    }
    static const struct { double x, fl, ce, ro; } exact[] = {
        { 2.5, 2, 3, 3 }, { -2.5, -3, -2, -3 }, { 0.49999999999999994, 0, 1, 0 },
        { -0.0, -0.0, -0.0, -0.0 }, { 4503599627370497.0, 4503599627370497.0, 4503599627370497.0, 4503599627370497.0 },
        { -1e-300, -1, -0.0, -0.0 }, { 1e300, 1e300, 1e300, 1e300 },
    };
    for (i = 0; i < sizeof exact / sizeof exact[0]; i++) {
        double x = exact[i].x;
        if (floor(x) != exact[i].fl || ceil(x) != exact[i].ce || round(x) != exact[i].ro ||
            signbit(ceil(x)) != signbit(exact[i].ce)) {
            printf("FAIL floor/ceil/round(%a) = %a/%a/%a\n", x, floor(x), ceil(x), round(x));
            failures++;
        }
    }
    printf("libm_accuracy_smoketest: %s (%u double + %u float cases, %d failed)\n", failures ? "FAIL" : "PASS",
           (unsigned)(sizeof cases / sizeof cases[0]), (unsigned)(sizeof fcases / sizeof fcases[0]), failures);
    return failures != 0;
}
