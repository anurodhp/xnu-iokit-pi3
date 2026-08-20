/*
 * Real AAPCS64 register-save/restore primitives for text_cmds/ed's
 * setjmp(3)/longjmp(3)/sigsetjmp(3)/siglongjmp(3) calls (main.c's real
 * ^C/error-recovery path -- see ed_setjmp_compat.c's own header comment
 * for the full story of why this project needs its own copy rather than
 * Apple's real third_party/libplatform/src/setjmp/arm64/setjmp.s).
 *
 * Saves/restores exactly the AAPCS64 callee-saved register set: x19-x28
 * (10 GPRs), x29/x30 (fp/lr), sp, and d8-d15 (the low 64 bits of v8-v15,
 * the only part of those SIMD/FP registers AAPCS64 requires a callee to
 * preserve) -- 21 x 8 bytes = 168 bytes total, well within this SDK's
 * real jmp_buf (192 bytes, _JBLEN=48 ints) and sigjmp_buf (196 bytes,
 * _JBLEN+1) capacity (third_party/xnu-7195's iPhoneOS.sdk setjmp.h,
 * confirmed by reading it directly). Deliberately NOT the ABI Apple's
 * own real setjmp.s uses internally (that file additionally does
 * pointer-authentication "munging" of fp/lr/sp via os/tsd.h's
 * _OS_PTR_MUNGE and saves/restores the sigaltstack status) -- neither
 * matters for correctness here: PAC munging is a defense-in-depth
 * hardening step against memory-corruption exploits, a no-op-equivalent
 * omission (not a wrong-behavior one) on a plain (non-arm64e, no PAC)
 * target like this one, and this project has no sigaltstack(2) caller
 * anywhere (ed.c never calls sigaltstack() itself either) to need that
 * bookkeeping preserved. Only the register-capture core is duplicated
 * here, in plain non-obfuscated form -- everything actually observable
 * from C (which registers survive a jump back) matches real semantics
 * exactly.
 *
 * Deliberately uses individual STR/LDR, never STP/LDP: real jmp_buf is
 * a plain `typedef int jmp_buf[48]` (4-byte C alignment, not
 * guaranteed 8-byte-aligned at its actual runtime stack address --
 * confirmed by reading this SDK's setjmp.h directly, not assumed).
 * ARMv8's STP/LDP (register-pair) instructions fault on a misaligned
 * base address REGARDLESS of SCTLR_ELx.A (the general unaligned-access
 * permission bit most kernels, including this one, run with clear) --
 * that relaxation only applies to single-register LDR/STR, not the
 * paired forms. Using single-register accesses throughout avoids ever
 * depending on `env`'s actual stack alignment at any real call site,
 * present or future, rather than relying on today's specific compiler/
 * frame-layout behavior happening to 8-byte-align it.
 *
 * Layout (byte offsets into the caller-supplied buffer):
 *   0x00 x19  0x08 x20  0x10 x21  0x18 x22  0x20 x23  0x28 x24
 *   0x30 x25  0x38 x26  0x40 x27  0x48 x28  0x50 x29(fp)  0x58 x30(lr)
 *   0x60 sp   0x68 d8   0x70 d9   0x78 d10  0x80 d11  0x88 d12
 *   0x90 d13  0x98 d14  0xA0 d15
 */
	.text
	.align 2
	.globl ___ed_setjmp_raw
___ed_setjmp_raw:
	mov	x9, sp
	str	x19, [x0, #0x00]
	str	x20, [x0, #0x08]
	str	x21, [x0, #0x10]
	str	x22, [x0, #0x18]
	str	x23, [x0, #0x20]
	str	x24, [x0, #0x28]
	str	x25, [x0, #0x30]
	str	x26, [x0, #0x38]
	str	x27, [x0, #0x40]
	str	x28, [x0, #0x48]
	str	x29, [x0, #0x50]
	str	x30, [x0, #0x58]
	str	x9,  [x0, #0x60]
	str	d8,  [x0, #0x68]
	str	d9,  [x0, #0x70]
	str	d10, [x0, #0x78]
	str	d11, [x0, #0x80]
	str	d12, [x0, #0x88]
	str	d13, [x0, #0x90]
	str	d14, [x0, #0x98]
	str	d15, [x0, #0xA0]
	mov	w0, #0
	ret

	.text
	.align 2
	.globl ___ed_longjmp_raw
___ed_longjmp_raw:
	mov	x9, x1
	ldr	x19, [x0, #0x00]
	ldr	x20, [x0, #0x08]
	ldr	x21, [x0, #0x10]
	ldr	x22, [x0, #0x18]
	ldr	x23, [x0, #0x20]
	ldr	x24, [x0, #0x28]
	ldr	x25, [x0, #0x30]
	ldr	x26, [x0, #0x38]
	ldr	x27, [x0, #0x40]
	ldr	x28, [x0, #0x48]
	ldr	x29, [x0, #0x50]
	ldr	x30, [x0, #0x58]
	ldr	x10, [x0, #0x60]
	ldr	d8,  [x0, #0x68]
	ldr	d9,  [x0, #0x70]
	ldr	d10, [x0, #0x78]
	ldr	d11, [x0, #0x80]
	ldr	d12, [x0, #0x88]
	ldr	d13, [x0, #0x90]
	ldr	d14, [x0, #0x98]
	ldr	d15, [x0, #0xA0]
	mov	sp, x10
	cmp	w9, #0
	csinc	w0, w9, wzr, ne
	ret
