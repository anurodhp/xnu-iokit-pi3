// Phase 6.5 Phase B (docs/daemon-roadmap.md): custom entry for dyld_stub.macho.
// NOT real dyld's entry -- real dyld/src/dyldStartup.s's __dyld_start pops a
// [mach_header, argc, argv[], 0, envp[], 0, apple[], 0] stack (confirmed by
// reading that file this session) because bsd/kern/kern_exec.c pushes an
// extra mach_header pointer below argc whenever load_result.dynlinker is
// true -- unconditionally, real Apple kernel behavior, not something this
// stub can opt out of. third_party/Csu's start.s has no idea that extra
// slot exists (ordinary static/non-dylinker binaries never get it), so
// reusing it here -- as an earlier version of this file did -- silently
// misreads the mach_header pointer as argc, corrupting every pointer
// computation downstream (root-caused via a temporary
// bsd/uxkern/ux_exception.c diagnostic this session: EXC_BAD_ACCESS at an
// address matching exactly that off-by-one-slot arithmetic).
//
// dyld_stub_main.c's main() never uses argc/argv, so unlike real dyld this
// stub does not need to actually parse them -- it only needs to consume
// (not necessarily even read) the extra slot and hand off to main() with a
// valid, aligned stack pointer.
.text
.align 2
.globl start
start:
    add sp, sp, #8      // skip the mach_header slot (see comment above)
    mov x2, sp
    and x2, x2, #0xfffffffffffffff0   // 16-byte-align, matching the AArch64 ABI
    mov sp, x2
    mov x0, #0
    mov x1, #0
    bl _main
    bl _exit
1:
    b 1b
