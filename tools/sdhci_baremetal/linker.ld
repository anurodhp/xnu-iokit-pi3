/*
 * Flat freestanding image, loaded whole via QEMU's `-device loader`
 * (no ELF/Mach-O parsing on the QEMU side - ld/objcopy do all the work
 * here). LOAD_ADDR must match run_qemu.sh's `-device loader,addr=...`.
 * Same technique/address convention as tools/direct_boot's trampoline.
 */
LOAD_ADDR = 0x80000;

ENTRY(_start)

SECTIONS
{
    . = LOAD_ADDR;
    .text : { *(.text._start) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : {
        __bss_start = .;
        *(.bss*) *(COMMON)
        __bss_end = .;
    }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
