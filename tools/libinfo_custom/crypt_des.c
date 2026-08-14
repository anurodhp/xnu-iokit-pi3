/*
 * Freestanding port of Apple's real third_party/Libc/gen/crypt.c. See
 * crypt_des.h for the full port-vs-original diff rationale. The algorithm
 * itself (tables, permutation/substitution structure, salt handling) is
 * UNCHANGED from the real Apple/BSD source below -- only calloc()->static
 * arrays, K&R->ANSI signatures, and dropping the unused DARWIN_UNIX03
 * setkey()/encrypt() wrappers + DEBUG prtab().
 *
 * Original copyright notices preserved (BSD license requires this for
 * derived source):
 *
 * Copyright (c) 1999 Apple Computer, Inc. All rights reserved.
 * @APPLE_LICENSE_HEADER_START@
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file. The Original Code and all software distributed under the License
 * are distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH
 * WARRANTIES, INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR
 * NON-INFRINGEMENT. Please see the License for the specific language
 * governing rights and limitations under the License.
 * @APPLE_LICENSE_HEADER_END@
 *
 * Copyright (c) 1989, 1993 The Regents of the University of California.
 * All rights reserved. This code is derived from software contributed to
 * Berkeley by Tom Truscott. Redistribution and use in source and binary
 * forms, with or without modification, are permitted provided that the
 * following conditions are met: (1) Redistributions of source code must
 * retain the above copyright notice, this list of conditions and the
 * following disclaimer. (2) Redistributions in binary form must reproduce
 * the above copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided with
 * the distribution. (3) Neither the name of the University nor the names
 * of its contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission. THIS
 * SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.
 */

#include "crypt_des.h"

/* real _PASSWORD_EFMT1, cross-checked against
 * third_party/Libinfo/lookup.subproj/pwd.h:70 -- see crypt_des.h header. */
#define _PASSWORD_EFMT1 '_'

/* Freestanding typedefs, same convention as libinfo_custom.c/init.c. */
typedef unsigned long li_cd_size_t;

/* MUST_ALIGN: real source's own "!defined(vax)" guard -- kept as-is, this
 * target is never vax. LONG_IS_32_BITS deliberately NOT defined: `long` is
 * 8 bytes on this LP64 arm64 target, matching real 64-bit Darwin's own
 * behavior (real crypt.c only defines this when sizeof(long)==4, via
 * <limits.h>'s LONG_BIT==32 -- not the case on any 64-bit Darwin target,
 * this port matches that exactly, not a deviation). */
#define MUST_ALIGN

typedef union {
    unsigned char b[8];
    struct {
        long i0: 32;
        long i1: 32;
    } b32;
} C_block;

/* Convert twenty-four-bit long in host-order to six bits (and 2 low-order
 * zeroes) per char little-endian format. Unchanged from real source. */
#define TO_SIX_BIT(rslt, src) {                        \
        C_block cvt;                                   \
        cvt.b[0] = (unsigned char)(src); (src) >>= 6;   \
        cvt.b[1] = (unsigned char)(src); (src) >>= 6;   \
        cvt.b[2] = (unsigned char)(src); (src) >>= 6;   \
        cvt.b[3] = (unsigned char)(src);                \
        rslt = (cvt.b32.i0 & 0x3f3f3f3fL) << 2;         \
    }

#define ZERO(d,d0,d1)                   d0 = 0, d1 = 0
#define LOAD(d,d0,d1,bl)                d0 = (bl).b32.i0, d1 = (bl).b32.i1
#define LOADREG(d,d0,d1,s,s0,s1)        d0 = s0, d1 = s1
#define OR(d,d0,d1,bl)                  d0 |= (bl).b32.i0, d1 |= (bl).b32.i1
#define STORE(s,s0,s1,bl)               (bl).b32.i0 = s0, (bl).b32.i1 = s1
#define DCL_BLOCK(d,d0,d1)              long d0, d1

/* "small data" branch (LARGEDATA not defined, matching real source at this
 * vintage): CHUNKBITS=4. */
#define LGCHUNKBITS 2
#define CHUNKBITS (1<<LGCHUNKBITS)
#define PERM6464(d,d0,d1,cpp,p)                                \
    { C_block tblk; permute(cpp,&tblk,p,8); LOAD (d,d0,d1,tblk); }
#define PERM3264(d,d0,d1,cpp,p)                                \
    { C_block tblk; permute(cpp,&tblk,p,4); LOAD (d,d0,d1,tblk); }

static void permute(unsigned char *cp, C_block *out, C_block *p, int chars_in) {
    DCL_BLOCK(D, D0, D1);
    C_block *tp;
    int t;

    ZERO(D, D0, D1);
    do {
        t = *cp++;
        tp = &p[t & 0xf]; OR(D, D0, D1, *tp); p += (1 << CHUNKBITS);
        tp = &p[t >> 4];  OR(D, D0, D1, *tp); p += (1 << CHUNKBITS);
    } while (--chars_in > 0);
    STORE(D, D0, D1, *out);
}

/* ===== Standard DES tables -- verbatim from real Apple/BSD crypt.c ===== */

static const unsigned char IP[] = {            /* initial permutation */
    58, 50, 42, 34, 26, 18, 10,  2,
    60, 52, 44, 36, 28, 20, 12,  4,
    62, 54, 46, 38, 30, 22, 14,  6,
    64, 56, 48, 40, 32, 24, 16,  8,
    57, 49, 41, 33, 25, 17,  9,  1,
    59, 51, 43, 35, 27, 19, 11,  3,
    61, 53, 45, 37, 29, 21, 13,  5,
    63, 55, 47, 39, 31, 23, 15,  7,
};

/* The final permutation is the inverse of IP -- no table needed. */

static const unsigned char ExpandTr[] = {      /* expansion operation */
    32,  1,  2,  3,  4,  5,
     4,  5,  6,  7,  8,  9,
     8,  9, 10, 11, 12, 13,
    12, 13, 14, 15, 16, 17,
    16, 17, 18, 19, 20, 21,
    20, 21, 22, 23, 24, 25,
    24, 25, 26, 27, 28, 29,
    28, 29, 30, 31, 32,  1,
};

static const unsigned char PC1[] = {           /* permuted choice table 1 */
    57, 49, 41, 33, 25, 17,  9,
     1, 58, 50, 42, 34, 26, 18,
    10,  2, 59, 51, 43, 35, 27,
    19, 11,  3, 60, 52, 44, 36,

    63, 55, 47, 39, 31, 23, 15,
     7, 62, 54, 46, 38, 30, 22,
    14,  6, 61, 53, 45, 37, 29,
    21, 13,  5, 28, 20, 12,  4,
};

static const unsigned char Rotates[] = {       /* PC1 rotation schedule */
    1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1,
};

/* note: each "row" of PC2 is left-padded with bits that make it invertible */
static const unsigned char PC2[] = {           /* permuted choice table 2 */
     9, 18,    14, 17, 11, 24,  1,  5,
    22, 25,     3, 28, 15,  6, 21, 10,
    35, 38,    23, 19, 12,  4, 26,  8,
    43, 54,    16,  7, 27, 20, 13,  2,

     0,  0,    41, 52, 31, 37, 47, 55,
     0,  0,    30, 40, 51, 45, 33, 48,
     0,  0,    44, 49, 39, 56, 34, 53,
     0,  0,    46, 42, 50, 36, 29, 32,
};

static const unsigned char S[8][64] = {        /* 48->32 bit substitution tables */
    {                                          /* S[1] */
    14,  4, 13,  1,  2, 15, 11,  8,  3, 10,  6, 12,  5,  9,  0,  7,
     0, 15,  7,  4, 14,  2, 13,  1, 10,  6, 12, 11,  9,  5,  3,  8,
     4,  1, 14,  8, 13,  6,  2, 11, 15, 12,  9,  7,  3, 10,  5,  0,
    15, 12,  8,  2,  4,  9,  1,  7,  5, 11,  3, 14, 10,  0,  6, 13,
    },
    {                                          /* S[2] */
    15,  1,  8, 14,  6, 11,  3,  4,  9,  7,  2, 13, 12,  0,  5, 10,
     3, 13,  4,  7, 15,  2,  8, 14, 12,  0,  1, 10,  6,  9, 11,  5,
     0, 14,  7, 11, 10,  4, 13,  1,  5,  8, 12,  6,  9,  3,  2, 15,
    13,  8, 10,  1,  3, 15,  4,  2, 11,  6,  7, 12,  0,  5, 14,  9,
    },
    {                                          /* S[3] */
    10,  0,  9, 14,  6,  3, 15,  5,  1, 13, 12,  7, 11,  4,  2,  8,
    13,  7,  0,  9,  3,  4,  6, 10,  2,  8,  5, 14, 12, 11, 15,  1,
    13,  6,  4,  9,  8, 15,  3,  0, 11,  1,  2, 12,  5, 10, 14,  7,
     1, 10, 13,  0,  6,  9,  8,  7,  4, 15, 14,  3, 11,  5,  2, 12,
    },
    {                                          /* S[4] */
     7, 13, 14,  3,  0,  6,  9, 10,  1,  2,  8,  5, 11, 12,  4, 15,
    13,  8, 11,  5,  6, 15,  0,  3,  4,  7,  2, 12,  1, 10, 14,  9,
    10,  6,  9,  0, 12, 11,  7, 13, 15,  1,  3, 14,  5,  2,  8,  4,
     3, 15,  0,  6, 10,  1, 13,  8,  9,  4,  5, 11, 12,  7,  2, 14,
    },
    {                                          /* S[5] */
     2, 12,  4,  1,  7, 10, 11,  6,  8,  5,  3, 15, 13,  0, 14,  9,
    14, 11,  2, 12,  4,  7, 13,  1,  5,  0, 15, 10,  3,  9,  8,  6,
     4,  2,  1, 11, 10, 13,  7,  8, 15,  9, 12,  5,  6,  3,  0, 14,
    11,  8, 12,  7,  1, 14,  2, 13,  6, 15,  0,  9, 10,  4,  5,  3,
    },
    {                                          /* S[6] */
    12,  1, 10, 15,  9,  2,  6,  8,  0, 13,  3,  4, 14,  7,  5, 11,
    10, 15,  4,  2,  7, 12,  9,  5,  6,  1, 13, 14,  0, 11,  3,  8,
     9, 14, 15,  5,  2,  8, 12,  3,  7,  0,  4, 10,  1, 13, 11,  6,
     4,  3,  2, 12,  9,  5, 15, 10, 11, 14,  1,  7,  6,  0,  8, 13,
    },
    {                                          /* S[7] */
     4, 11,  2, 14, 15,  0,  8, 13,  3, 12,  9,  7,  5, 10,  6,  1,
    13,  0, 11,  7,  4,  9,  1, 10, 14,  3,  5, 12,  2, 15,  8,  6,
     1,  4, 11, 13, 12,  3,  7, 14, 10, 15,  6,  8,  0,  5,  9,  2,
     6, 11, 13,  8,  1,  4, 10,  7,  9,  5,  0, 15, 14,  2,  3, 12,
    },
    {                                          /* S[8] */
    13,  2,  8,  4,  6, 15, 11,  1, 10,  9,  3, 14,  5,  0, 12,  7,
     1, 15, 13,  8, 10,  3,  7,  4, 12,  5,  6, 11,  0, 14,  9,  2,
     7, 11,  4,  1,  9, 12, 14,  2,  0,  6, 10, 13, 15,  3,  5,  8,
     2,  1, 14,  7,  4, 10,  8, 13, 15, 12,  9,  0,  3,  5,  6, 11,
    },
};

static const unsigned char P32Tr[] = { /* 32-bit permutation function */
    16,  7, 20, 21,
    29, 12, 28, 17,
     1, 15, 23, 26,
     5, 18, 31, 10,
     2,  8, 24, 14,
    32, 27,  3,  9,
    19, 13, 30,  6,
    22, 11,  4, 25,
};

static const unsigned char CIFP[] = {  /* compressed/interleaved permutation */
     1,  2,  3,  4,   17, 18, 19, 20,
     5,  6,  7,  8,   21, 22, 23, 24,
     9, 10, 11, 12,   25, 26, 27, 28,
    13, 14, 15, 16,   29, 30, 31, 32,

    33, 34, 35, 36,   49, 50, 51, 52,
    37, 38, 39, 40,   53, 54, 55, 56,
    41, 42, 43, 44,   57, 58, 59, 60,
    45, 46, 47, 48,   61, 62, 63, 64,
};

static const unsigned char itoa64[] =  /* 0..63 => ascii-64 */
    "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

/* ascii-64 => 0..63 */
static const unsigned char a64toi[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 0, 0, 0, 0, 0,
    0, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
    27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 0, 0, 0, 0, 0,
    0, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
    53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 0, 0, 0, 0, 0,
};

/* ===== Runtime-initialized tables -- calloc() -> static BSS arrays =====
 * Sizes are the exact same constant-expression element counts the real
 * source's calloc() calls compute (CHUNKBITS==4 fixed, per the "small
 * data" branch above): (64/4)*(1<<4)=256, (32/4)*(1<<4)=128, 2*8*64=1024.
 * BSS is zero-initialized already, matching calloc()'s zeroing -- no
 * explicit memset needed. */
static C_block PC1ROT_storage[256];
static C_block PC2ROT_storage[2][256];
static C_block IE3264_storage[128];
static C_block CF6464_storage[256];
static long     SPE_storage[1024];

static C_block *PC1ROT;
static C_block *PC2ROT[2];
static C_block *IE3264;
static long     *SPE;
static C_block  *CF6464;

static C_block constdatablock;                 /* encryption constant, zero */
static char cryptresult[1 + 4 + 4 + 11 + 1];    /* encrypted result */

#define KS_SIZE 16
static C_block KS[KS_SIZE];

static void init_perm(C_block *perm, const unsigned char *p, int chars_in, int chars_out) {
    int i, j, k, l;

    for (k = 0; k < chars_out * 8; k++) {       /* each output bit position */
        l = p[k] - 1;                           /* where this bit comes from */
        if (l < 0) {
            continue;                           /* output bit is always 0 */
        }
        i = l >> LGCHUNKBITS;                   /* which chunk this bit is from */
        l = 1 << (l & (CHUNKBITS - 1));         /* mask for this bit */
        for (j = 0; j < (1 << CHUNKBITS); j++) {        /* each chunk value */
            if ((j & l) != 0) {
                perm[(i * (1 << CHUNKBITS)) + j].b[k >> 3] |= (unsigned char)(1 << (k & 07));
            }
        }
    }
}

static void init_des(void) {
    int i, j;
    long k;
    int tableno;
    /* Manually zeroed (not `= {0}`) -- a C aggregate initializer this
     * size is a real, observed case where clang lowers to a call to the
     * external `memset` symbol even under -ffreestanding -nostdlib (this
     * file has NO libc, deliberately -- see crypt_des.h's file header --
     * so that call would go unresolved at link time; confirmed by hitting
     * exactly this real link error before adding this comment, not
     * guessed preemptively). Explicit loop avoids the implicit libc call
     * entirely, keeping this file's "zero external dependencies, pure
     * computation" property real rather than aspirational. */
    unsigned char perm[64];
    for (i = 0; i < 64; i++) {
        perm[i] = 0;
    }

    /* PC1ROT: bit reverse, then PC1, then Rotate, then PC2. */
    for (i = 0; i < 64; i++) {
        if ((k = PC2[i]) == 0) {
            continue;
        }
        k += Rotates[0] - 1;
        if ((k % 28) < Rotates[0]) {
            k -= 28;
        }
        k = PC1[k];
        if (k > 0) {
            k--;
            k = (k | 07) - (k & 07);
            k++;
        }
        perm[i] = (unsigned char)k;
    }
    PC1ROT = PC1ROT_storage;
    PC2ROT[0] = PC2ROT_storage[0];
    PC2ROT[1] = PC2ROT_storage[1];
    init_perm(PC1ROT, perm, 8, 8);

    /* PC2ROT: PC2 inverse, then Rotate (once or twice), then PC2. */
    for (j = 0; j < 2; j++) {
        unsigned char pc2inv[64];
        for (i = 0; i < 64; i++) {
            perm[i] = pc2inv[i] = 0;
        }
        for (i = 0; i < 64; i++) {
            if ((k = PC2[i]) == 0) {
                continue;
            }
            pc2inv[k - 1] = (unsigned char)(i + 1);
        }
        for (i = 0; i < 64; i++) {
            if ((k = PC2[i]) == 0) {
                continue;
            }
            k += j;
            if ((k % 28) <= j) {
                k -= 28;
            }
            perm[i] = pc2inv[k];
        }
        init_perm(PC2ROT[j], perm, 8, 8);
    }

    /* Bit reverse, then initial permutation, then expansion. */
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            k = (j < 2) ? 0 : IP[ExpandTr[i * 6 + j - 2] - 1];
            if (k > 32) {
                k -= 32;
            } else if (k > 0) {
                k--;
            }
            if (k > 0) {
                k--;
                k = (k | 07) - (k & 07);
                k++;
            }
            perm[i * 8 + j] = (unsigned char)k;
        }
    }
    IE3264 = IE3264_storage;
    init_perm(IE3264, perm, 4, 8);

    /* Compression, then final permutation, then bit reverse. */
    for (i = 0; i < 64; i++) {
        k = IP[CIFP[i] - 1];
        if (k > 0) {
            k--;
            k = (k | 07) - (k & 07);
            k++;
        }
        perm[k - 1] = (unsigned char)(i + 1);
    }
    CF6464 = CF6464_storage;
    SPE = SPE_storage;
    init_perm(CF6464, perm, 8, 8);

    /* SPE table. */
    for (i = 0; i < 48; i++) {
        perm[i] = P32Tr[ExpandTr[i] - 1];
    }
    for (tableno = 0; tableno < 8; tableno++) {
        for (j = 0; j < 64; j++) {
            /* see this function's earlier comment on why this is a manual
             * loop, not `= {0}` */
            unsigned char tmp32[32];
            for (i = 0; i < 32; i++) {
                tmp32[i] = 0;
            }
            k = (((j >> 0) & 01) << 5) |
                (((j >> 1) & 01) << 3) |
                (((j >> 2) & 01) << 2) |
                (((j >> 3) & 01) << 1) |
                (((j >> 4) & 01) << 0) |
                (((j >> 5) & 01) << 4);
            k = S[tableno][k];
            k = (((k >> 3) & 01) << 0) |
                (((k >> 2) & 01) << 1) |
                (((k >> 1) & 01) << 2) |
                (((k >> 0) & 01) << 3);
            for (i = 0; i < 4; i++) {
                tmp32[4 * tableno + i] = (unsigned char)((k >> i) & 01);
            }
            k = 0;
            for (i = 24; --i >= 0;) {
                k = (k << 1) | tmp32[perm[i] - 1];
            }
            TO_SIX_BIT(SPE[(tableno * 64) + j], k);
            k = 0;
            for (i = 24; --i >= 0;) {
                k = (k << 1) | tmp32[perm[i + 24] - 1];
            }
            TO_SIX_BIT(SPE[(8 * 64) + (tableno * 64) + j], k);
        }
    }
}

static int crypt_des_setkey(const unsigned char *key) {
    DCL_BLOCK(K, K0, K1);
    C_block *ptabp;
    int i;
    static int des_ready = 0;

    if (!des_ready) {
        init_des();
        des_ready = 1;
    }

    PERM6464(K, K0, K1, (unsigned char *)key, PC1ROT);
    {
        C_block *kp = &KS[0];
        STORE(K & ~0x03030303L, K0 & ~0x03030303L, K1, *kp);
        for (i = 1; i < 16; i++) {
            kp = &KS[i];
            STORE(K, K0, K1, *kp);
            ptabp = PC2ROT[Rotates[i] - 1];
            PERM6464(K, K0, K1, (unsigned char *)kp, ptabp);
            STORE(K & ~0x03030303L, K0 & ~0x03030303L, K1, *kp);
        }
    }
    return 0;
}

static int crypt_des_cipher(const char *in, char *out, long salt, int num_iter) {
    long L0, L1, R0, R1, k;
    C_block *kp;
    int loop_count;
    long ks_inc;
    C_block B;

    L0 = salt;
    TO_SIX_BIT(salt, L0); /* convert to 4*(6+2) format */

#define SALT salt

#if defined(MUST_ALIGN)
    B.b[0] = (unsigned char)in[0]; B.b[1] = (unsigned char)in[1];
    B.b[2] = (unsigned char)in[2]; B.b[3] = (unsigned char)in[3];
    B.b[4] = (unsigned char)in[4]; B.b[5] = (unsigned char)in[5];
    B.b[6] = (unsigned char)in[6]; B.b[7] = (unsigned char)in[7];
    LOAD(L, L0, L1, B);
#else
    LOAD(L, L0, L1, *(C_block *)in);
#endif
    LOADREG(R, R0, R1, L, L0, L1);
    L0 &= 0x55555555L;
    L1 &= 0x55555555L;
    L0 = (L0 << 1) | L1;
    R0 &= 0xaaaaaaaaL;
    R1 = (R1 >> 1) & 0x55555555L;
    L1 = R0 | R1;
    STORE(L, L0, L1, B);
    PERM3264(L, L0, L1, B.b, IE3264);
    PERM3264(R, R0, R1, B.b + 4, IE3264);

    if (num_iter >= 0) {
        kp = &KS[0];
        ks_inc = (long)sizeof(*kp);
    } else {
        num_iter = -num_iter;
        kp = &KS[KS_SIZE - 1];
        ks_inc = -(long)sizeof(*kp);
    }

    while (--num_iter >= 0) {
        loop_count = 8;
        do {
#define SPTAB(t, i) (*(long *)((unsigned char *)(t) + (i) * (sizeof(long) / 4)))
#define DOXOR(x, y, i) k = B.b[i]; \
            x ^= SPTAB(&SPE[(i) * 64], k); \
            y ^= SPTAB(&SPE[(8 * 64) + ((i) * 64)], k);

#define CRUNCH(p0, p1, q0, q1) \
            k = (q0 ^ q1) & SALT; \
            B.b32.i0 = (long)(k ^ q0 ^ kp->b32.i0); \
            B.b32.i1 = (long)(k ^ q1 ^ kp->b32.i1); \
            kp = (C_block *)((char *)kp + ks_inc); \
            DOXOR(p0, p1, 0); \
            DOXOR(p0, p1, 1); \
            DOXOR(p0, p1, 2); \
            DOXOR(p0, p1, 3); \
            DOXOR(p0, p1, 4); \
            DOXOR(p0, p1, 5); \
            DOXOR(p0, p1, 6); \
            DOXOR(p0, p1, 7);

            CRUNCH(L0, L1, R0, R1);
            CRUNCH(R0, R1, L0, L1);
        } while (--loop_count != 0);
        kp = (C_block *)((char *)kp - (ks_inc * KS_SIZE));

        /* swap L and R */
        L0 ^= R0; L1 ^= R1;
        R0 ^= L0; R1 ^= L1;
        L0 ^= R0; L1 ^= R1;
    }

    L0 = ((L0 >> 3) & 0x0f0f0f0fL) | ((L1 << 1) & 0xf0f0f0f0L);
    L1 = ((R0 >> 3) & 0x0f0f0f0fL) | ((R1 << 1) & 0xf0f0f0f0L);
    STORE(L, L0, L1, B);
    PERM6464(L, L0, L1, B.b, CF6464);
#if defined(MUST_ALIGN)
    STORE(L, L0, L1, B);
    out[0] = (char)B.b[0]; out[1] = (char)B.b[1]; out[2] = (char)B.b[2]; out[3] = (char)B.b[3];
    out[4] = (char)B.b[4]; out[5] = (char)B.b[5]; out[6] = (char)B.b[6]; out[7] = (char)B.b[7];
#else
    STORE(L, L0, L1, *(C_block *)out);
#endif
    return 0;
}

char *li_crypt(const char *key, const char *setting) {
    char *encp;
    long i;
    int t;
    long salt;
    int num_iter, salt_size;
    C_block keyblock, rsltblock;

    for (i = 0; i < 8; i++) {
        if ((t = 2 * (unsigned char)(*key)) != 0) {
            key++;
        }
        keyblock.b[i] = (unsigned char)t;
    }
    if (crypt_des_setkey(keyblock.b)) {
        return 0;
    }

    encp = &cryptresult[0];
    if (*setting == _PASSWORD_EFMT1) {
        /* Involve the rest of the password 8 characters at a time. */
        while (*key) {
            if (crypt_des_cipher((char *)&keyblock, (char *)&keyblock, 0L, 1)) {
                return 0;
            }
            for (i = 0; i < 8; i++) {
                if ((t = 2 * (unsigned char)(*key)) != 0) {
                    key++;
                }
                keyblock.b[i] ^= (unsigned char)t;
            }
            if (crypt_des_setkey(keyblock.b)) {
                return 0;
            }
        }

        *encp++ = *setting++;

        /* get iteration count */
        num_iter = 0;
        for (i = 4; --i >= 0;) {
            if ((t = (unsigned char)setting[i]) == '\0') {
                t = '.';
            }
            encp[i] = (char)t;
            num_iter = (num_iter << 6) | a64toi[t];
        }
        setting += 4;
        encp += 4;
        salt_size = 4;
    } else {
        num_iter = 25;
        salt_size = 2;
    }

    salt = 0;
    for (i = salt_size; --i >= 0;) {
        if ((t = (unsigned char)setting[i]) == '\0') {
            t = '.';
        }
        encp[i] = (char)t;
        salt = (salt << 6) | a64toi[t];
    }
    encp += salt_size;
    if (crypt_des_cipher((char *)&constdatablock, (char *)&rsltblock, salt, num_iter)) {
        return 0;
    }

    /* Encode the 64 cipher bits as 11 ascii characters. */
    i = ((long)((rsltblock.b[0] << 8) | rsltblock.b[1]) << 8) | rsltblock.b[2];
    encp[3] = (char)itoa64[i & 0x3f]; i >>= 6;
    encp[2] = (char)itoa64[i & 0x3f]; i >>= 6;
    encp[1] = (char)itoa64[i & 0x3f]; i >>= 6;
    encp[0] = (char)itoa64[i]; encp += 4;
    i = ((long)((rsltblock.b[3] << 8) | rsltblock.b[4]) << 8) | rsltblock.b[5];
    encp[3] = (char)itoa64[i & 0x3f]; i >>= 6;
    encp[2] = (char)itoa64[i & 0x3f]; i >>= 6;
    encp[1] = (char)itoa64[i & 0x3f]; i >>= 6;
    encp[0] = (char)itoa64[i]; encp += 4;
    i = ((long)((rsltblock.b[6]) << 8) | rsltblock.b[7]) << 2;
    encp[2] = (char)itoa64[i & 0x3f]; i >>= 6;
    encp[1] = (char)itoa64[i & 0x3f]; i >>= 6;
    encp[0] = (char)itoa64[i];

    encp[3] = 0;

    return cryptresult;
}

/* ===================== everything below is new, not from Apple's source
 * (li_hash_password/li_crypt_verify -- the caller-facing API this port was
 * built for). ===================== */

static li_cd_size_t cd_strlen(const char *s) {
    li_cd_size_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

/* Constant-time compare over a fixed-size, zero-padded window -- avoids
 * both an early-exit timing side channel AND any OOB read from mismatched
 * real string lengths (every real hash this file deals with -- 13-char
 * traditional, 20-char extended -- is well under MAXHASH, so this never
 * truncates a real comparison, only pads it). Real length itself is not
 * treated as secret (computed via plain cd_strlen() above, non-constant-
 * time) -- acceptable, matching real bcrypt-class implementations' own
 * usual tradeoff: distinguishing hash *shapes* isn't a meaningful oracle
 * for THIS attacker model (an attacker who can already read /etc/
 * master.passwd's stored hash doesn't gain anything from also learning
 * its length via timing). */
#define MAXHASH 32

static int cd_consttime_eq(const char *a, const char *b) {
    unsigned char bufa[MAXHASH];
    unsigned char bufb[MAXHASH];
    li_cd_size_t la = cd_strlen(a);
    li_cd_size_t lb = cd_strlen(b);
    li_cd_size_t i;

    for (i = 0; i < MAXHASH; i++) {
        bufa[i] = (i < la && la <= MAXHASH) ? (unsigned char)a[i] : 0;
        bufb[i] = (i < lb && lb <= MAXHASH) ? (unsigned char)b[i] : 0;
    }

    unsigned char diff = 0;
    /* also fold the (non-secret, but still worth not obviously branching
     * on) length difference into the same accumulator */
    diff |= (unsigned char)(la ^ lb);
    for (i = 0; i < MAXHASH; i++) {
        diff |= (unsigned char)(bufa[i] ^ bufb[i]);
    }
    return diff == 0;
}

/* Weak-but-adequate salt source -- see crypt_des.h's file header. Callers
 * (pwtool.c) seed this once at startup from getpid()+a coarse timer;
 * li_hash_password() itself has no syscall access (this file is pure
 * computation, deliberately -- see crypt_des.h), so it takes the seed as
 * a parameter rather than sourcing entropy itself. */
static unsigned long cd_rand_state = 0x2545F4914F6CDD1DUL; /* nonzero default */

void li_crypt_seed(unsigned long seed) {
    cd_rand_state ^= seed;
    if (cd_rand_state == 0) {
        cd_rand_state = 1; /* xorshift-class generators must never be 0 */
    }
}

static unsigned long cd_rand_next(void) {
    /* xorshift64* -- not cryptographic, fine for a crypt salt (see file
     * header comment: salt needs uniqueness, not secrecy). */
    unsigned long x = cd_rand_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    cd_rand_state = x;
    return x * 0x2545F4914F6CDD1DUL;
}

static char cd_rand_b64char(void) {
    return (char)itoa64[cd_rand_next() & 0x3f];
}

int li_hash_password(const char *password, char *out, unsigned long outsz) {
    if (!password || password[0] == 0) {
        return 0; /* reject empty password outright -- see crypt_des.h */
    }
    if (outsz < 21) {
        return 0;
    }

    char setting[10];
    setting[0] = _PASSWORD_EFMT1;
    /* Iteration count: a real, deliberately-chosen positive value (1024),
     * not left at whatever a zeroed buffer would decode to (0 rounds
     * would defeat the entire point of the extended format). Encoded
     * across 4 base64 (itoa64) chars -- verified digit order against
     * li_crypt()'s own decode loop, NOT assumed: that loop consumes
     * `setting[0..3]` (its local `setting` pointer is already past the
     * leading '_', i.e. maps to this array's setting[1..4]) via
     * `for (i = 4; --i >= 0;) { num_iter = (num_iter<<6) | a64toi[t]; }`
     * -- that loop body runs for i=3,2,1,0 IN THAT ORDER (i is
     * predecremented before the first use), so the digit at its
     * setting[3] (== this array's setting[4]) is consumed FIRST and ends
     * up most-shifted, i.e. MOST significant; its setting[0] (== this
     * array's setting[1]) is consumed LAST, i.e. LEAST significant. An
     * earlier version of this loop had this backwards (assigned MSB into
     * setting[1] instead of setting[4]) -- caught before ever being
     * exercised by a real hash by manually tracing the decode order
     * above; the sequential-shift form below is written to match that
     * traced order directly rather than repeat the mistake in a
     * different shape. */
    {
        unsigned long iter = 1024;
        setting[1] = (char)itoa64[iter & 0x3f]; iter >>= 6; /* least significant */
        setting[2] = (char)itoa64[iter & 0x3f]; iter >>= 6;
        setting[3] = (char)itoa64[iter & 0x3f]; iter >>= 6;
        setting[4] = (char)itoa64[iter & 0x3f];             /* most significant */
    }
    setting[5] = cd_rand_b64char();
    setting[6] = cd_rand_b64char();
    setting[7] = cd_rand_b64char();
    setting[8] = cd_rand_b64char();
    setting[9] = 0;

    char *result = li_crypt(password, setting);
    if (!result) {
        return 0;
    }
    li_cd_size_t rlen = cd_strlen(result);
    if (rlen + 1 > outsz) {
        return 0;
    }
    for (li_cd_size_t i = 0; i <= rlen; i++) {
        out[i] = result[i];
    }
    return 1;
}

int li_crypt_verify(const char *password, const char *stored_hash) {
    if (!password || password[0] == 0) {
        return 0;
    }
    if (!stored_hash || stored_hash[0] == 0) {
        return 0; /* fail-closed: no stored hash means "cannot authenticate
                    * with a password", never "any password is accepted" */
    }
    if (stored_hash[0] == '*' || stored_hash[0] == '!') {
        return 0; /* classic BSD locked-account sentinels */
    }

    char *result = li_crypt(password, stored_hash);
    if (!result) {
        return 0;
    }
    return cd_consttime_eq(result, stored_hash);
}
