/*
 * Entry point for QEMU's `-device loader,addr=0x80000,cpu-num=0`, which
 * sets CPU0's initial PC directly here - no ELF/Mach-O header, no boot
 * protocol negotiation. raspi3b always resets its CPUs at EL3 (confirmed
 * repeatedly elsewhere in this project); we deliberately stay at whatever
 * EL we're started at rather than dropping to EL1 (unlike direct_boot's
 * trampoline) - physical MMIO reads/writes work fine at EL3 with the MMU
 * off, and this program never needs anything EL1-specific (no exception
 * vectors, no interrupt controller, pure polling).
 */
	.section .text._start, "ax"
	.global _start
_start:
	/* Stack: 256MB mark, far above our own tiny footprint and this
	 * project's -m 1024 QEMU RAM size, far below the top of RAM. */
	ldr	x0, =0x10000000
	mov	sp, x0

	/* Zero .bss - nothing assumes zero-init C globals otherwise. */
	ldr	x0, =__bss_start
	ldr	x1, =__bss_end
1:	cmp	x0, x1
	b.ge	2f
	str	xzr, [x0], #8
	b	1b
2:
	bl	main

	/* main() shouldn't return, but if it does, spin rather than run
	 * off into whatever garbage follows. */
3:	wfe
	b	3b
