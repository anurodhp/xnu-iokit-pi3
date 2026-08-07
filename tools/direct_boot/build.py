#!/usr/bin/env python3
"""
Replicates what FlorentRevest/u-boot's cmd/bootxnu.c does (Mach-O load +
boot_args + ADT), but on the host at build time instead of in a bootloader
at boot time. Produces two flat binaries QEMU's `-device loader` can place
directly into guest RAM, plus a tiny hand-generated EL2->EL1 trampoline
(transcribed from u-boot's arch/arm/include/asm/macro.h
armv8_switch_to_el1_m, ES_TO_AARCH64 path) to jump into XNU.
"""
import struct
import subprocess
import sys
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
KERNEL_PATH = os.path.join(ROOT, "precompiled/kernel.development.bcm2837.unstripped")
DTB_PATH = os.path.join(ROOT, "devicetrees/rpi3.dtb")
OUT_DIR = os.path.join(ROOT, "qemu/direct_boot")

# Matches include/configs/rpi.h: CONFIG_SYS_LOAD_ADDR / CONFIG_SYS_SDRAM_SIZE
XNU_LOAD_BASE = 0x1000000
XNU_LOAD_OFFSET = 0x4000
XNU_LOAD_ADDR = XNU_LOAD_BASE + XNU_LOAD_OFFSET
SDRAM_SIZE = 128 * 1024 * 1024
TRAMPOLINE_ADDR = 0x80000

MACH_O_MAGIC_64 = 0xfeedfacf
LC_SEGMENT_64 = 0x19
LC_UNIXTHREAD = 0x5


def parse_macho(data):
    magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved = \
        struct.unpack_from('<8I', data, 0)
    if magic != MACH_O_MAGIC_64:
        sys.exit(f"not a 64-bit mach-o (magic={magic:#x})")
    off = 32
    segments = []
    entry = None
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from('<2I', data, off)
        if cmd == LC_SEGMENT_64:
            segname, vmaddr, vmsize, fileoff, filesize, maxprot, initprot, nsects, sflags = \
                struct.unpack_from('<16s4Q4I', data, off + 8)
            segments.append((vmaddr, vmsize, fileoff, filesize,
                              segname.split(b'\0')[0].decode()))
        elif cmd == LC_UNIXTHREAD:
            pc_off = off + 16 + 29 * 8 + 8 + 8 + 8  # flavor,count,x[29],fp,lr,sp -> pc
            (entry,) = struct.unpack_from('<Q', data, pc_off)
        off += cmdsize
    if entry is None:
        sys.exit("no LC_UNIXTHREAD found, can't determine entry point")
    return segments, entry


def build_kernel_image(data, segments, entry):
    base = min(s[0] for s in segments)
    end = max(s[0] + s[1] for s in segments)
    image = bytearray(end - base)
    for vmaddr, vmsize, fileoff, filesize, name in segments:
        dst = vmaddr - base
        image[dst:dst + filesize] = data[fileoff:fileoff + filesize]
        print(f"  segment {name:<16} vmaddr={vmaddr:#010x} vmsize={vmsize:#x} "
              f"fileoff={fileoff:#x} filesize={filesize:#x}")
    xnu_entry_phys = entry - base + XNU_LOAD_ADDR
    xnu_end_phys = end - base + XNU_LOAD_ADDR
    return bytes(image), base, xnu_entry_phys, xnu_end_phys


# Confirmed via offsets.c (native compile of the real struct - same AArch64
# LP64 alignment rules as the target).
BA_SIZE = 736
BA_OFF_VIRT_BASE = 8
BA_OFF_PHYS_BASE = 16
BA_OFF_MEM_SIZE = 24
BA_OFF_PHYS_END = 32
BA_OFF_MACHINE_TYPE = 88
BA_OFF_AFDT = 96
BA_OFF_AFDT_LENGTH = 104
BA_OFF_COMMAND_LINE = 108
CMDLINE_LEN = 608


def build_boot_args(virt_base, phys_base, phys_end_after_kernel, dtb_data, command_line):
    buf = bytearray(BA_SIZE)
    struct.pack_into('<H', buf, 0, 2)   # revision
    struct.pack_into('<H', buf, 2, 2)   # version
    struct.pack_into('<Q', buf, BA_OFF_VIRT_BASE, virt_base)
    struct.pack_into('<Q', buf, BA_OFF_PHYS_BASE, phys_base)
    struct.pack_into('<Q', buf, BA_OFF_MEM_SIZE, SDRAM_SIZE)
    # boot_args itself sits right after the kernel image, per bootxnu.c
    boot_args_phys = phys_end_after_kernel
    afdt_phys = boot_args_phys + BA_SIZE
    struct.pack_into('<Q', buf, BA_OFF_PHYS_END, afdt_phys + len(dtb_data))
    struct.pack_into('<I', buf, BA_OFF_MACHINE_TYPE, 0)
    struct.pack_into('<Q', buf, BA_OFF_AFDT, afdt_phys)
    struct.pack_into('<I', buf, BA_OFF_AFDT_LENGTH, len(dtb_data))
    cl = command_line.encode()[:CMDLINE_LEN - 1]
    buf[BA_OFF_COMMAND_LINE:BA_OFF_COMMAND_LINE + len(cl)] = cl
    return bytes(buf), boot_args_phys, afdt_phys


def movk_movz_seq(reg, value):
    lines = []
    chunks = [(value >> (16 * i)) & 0xFFFF for i in range(4)]
    first = True
    for i, chunk in enumerate(chunks):
        if chunk == 0 and not (first and all(c == 0 for c in chunks)):
            continue
        op = "movz" if first else "movk"
        shift = f", lsl #{16*i}" if i else ""
        lines.append(f"    {op} {reg}, #{chunk:#x}{shift}")
        first = False
    return lines


def build_trampoline(boot_args_phys, xnu_entry_phys):
    lines = ["    .text", "    .global _start", "_start:"]
    lines += ["    // x0 = boot_args physical pointer (XNU calling convention)"]
    lines += movk_movz_seq("x0", boot_args_phys)
    lines += ["    // x1 = xnu entry point (target EL1 address)"]
    lines += movk_movz_seq("x1", xnu_entry_phys)
    lines += [
        "    // --- armv8_switch_to_el2_m, ep=x1, flag=ES_TO_AARCH64, tmp=x2 ---",
        "    // Confirmed empirically (via QEMU monitor) that raspi3b resets at EL3,",
        "    // not EL2 as initially assumed - this stage is required.",
        "    msr cptr_el3, xzr",
    ]
    lines += movk_movz_seq("x2", 0x33ff)           # CPTR_EL2_RES1 = (3<<12 | 0x3ff)
    lines += ["    msr cptr_el2, x2"]
    lines += [
        "    mrs x2, cnthctl_el2",
        "    orr x2, x2, #3",
        "    msr cnthctl_el2, x2",
        "    msr cntvoff_el2, xzr",
    ]
    lines += movk_movz_seq("x2", 0x30C50830)       # SCTLR_EL2_RES1 (3<<28|3<<22|1<<18|1<<16|1<<11|3<<4)
    lines += ["    msr sctlr_el2, x2"]
    lines += [
        "    mov x2, sp",
        "    msr sp_el2, x2",
        "    mrs x2, vbar_el3",
        "    msr vbar_el2, x2",
    ]
    lines += movk_movz_seq("x2", 0x5B1)            # SCR_EL3_RW_AARCH64|HCE_EN|SMD_DIS|RES1|NS_EN
    lines += ["    msr scr_el3, x2"]
    lines += movk_movz_seq("x2", 0x3C9)            # SPSR: masked debug/serr/irq/fiq, AArch64 EL2h
    lines += [
        "    msr spsr_el3, x2",
        "    adr x2, el2_continue",
        "    msr elr_el3, x2",
        "    eret",
        "",
        "el2_continue:",
        "    // --- armv8_switch_to_el1_m, ep=x1, flag=ES_TO_AARCH64, tmp=x2 ---",
        "    // transcribed from arch/arm/include/asm/macro.h in FlorentRevest/u-boot",
        "    mrs x2, cnthctl_el2",
        "    orr x2, x2, #3",                  # CNTHCTL_EL2_EL1PCEN_EN | EL1PCTEN_EN
        "    msr cnthctl_el2, x2",
        "    msr cntvoff_el2, xzr",
        "    mrs x2, midr_el1",
        "    msr vpidr_el2, x2",
        "    mrs x2, mpidr_el1",
        "    msr vmpidr_el2, x2",
    ]
    lines += movk_movz_seq("x2", 0x33ff)           # CPTR_EL2_RES1 = (3<<12 | 0x3ff)
    lines += ["    msr cptr_el2, x2", "    msr hstr_el2, xzr"]
    lines += movk_movz_seq("x2", 0x300000)         # CPACR_EL1_FPEN_EN = 3<<20
    lines += ["    msr cpacr_el1, x2"]
    lines += movk_movz_seq("x2", 0x30D00800)       # SCTLR_EL1_RES1 (3<<28|3<<22|1<<20|1<<11)
    lines += ["    msr sctlr_el1, x2"]
    lines += [
        "    mov x2, sp",
        "    msr sp_el1, x2",
        "    mrs x2, vbar_el2",
        "    msr vbar_el1, x2",
    ]
    lines += movk_movz_seq("x2", 0xA0000000)       # HCR_EL2_RW_AARCH64 | HCR_EL2_HCD_DIS
    lines += ["    msr hcr_el2, x2"]
    lines += movk_movz_seq("x2", 0x3C5)            # SPSR: masked debug/serr/irq/fiq, AArch64 EL1h
    lines += [
        "    msr spsr_el2, x2",
        "    msr elr_el2, x1",
        "    eret",
    ]
    return "\n".join(lines) + "\n"


def assemble_trampoline(asm_text):
    s_path = os.path.join(OUT_DIR, "trampoline.s")
    o_path = os.path.join(OUT_DIR, "trampoline.o")
    with open(s_path, "w") as f:
        f.write(asm_text)
    subprocess.run(
        ["clang", "-target", "aarch64-none-elf", "-c", s_path, "-o", o_path],
        check=True,
    )
    return extract_elf_text(o_path)


def extract_elf_text(o_path):
    data = open(o_path, "rb").read()
    assert data[:4] == b'\x7fELF'
    is64 = data[4] == 2
    assert is64
    e_shoff, = struct.unpack_from('<Q', data, 0x28)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', data, 0x3a)
    shstr_off = struct.unpack_from('<Q', data, e_shoff + e_shstrndx * e_shentsize + 0x18)[0]
    for i in range(e_shnum):
        base = e_shoff + i * e_shentsize
        name_off, sh_type = struct.unpack_from('<II', data, base)
        sh_offset, sh_size = struct.unpack_from('<QQ', data, base + 0x18)
        name_end = data.index(b'\0', shstr_off + name_off)
        name = data[shstr_off + name_off:name_end].decode()
        if name == ".text":
            return data[sh_offset:sh_offset + sh_size]
    sys.exit("no .text section found in trampoline.o")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    kernel_data = open(KERNEL_PATH, "rb").read()
    dtb_data = open(DTB_PATH, "rb").read()

    print(f"Parsing Mach-O kernel ({len(kernel_data)} bytes)...")
    segments, entry = parse_macho(kernel_data)
    image, vbase, xnu_entry_phys, xnu_end_phys = build_kernel_image(kernel_data, segments, entry)
    print(f"  vbase={vbase:#x} entry(virt)={entry:#x} -> entry(phys)={xnu_entry_phys:#x}")
    print(f"  image size={len(image):#x} bytes, loaded at phys {XNU_LOAD_ADDR:#x}")

    command_line = "debug=0x8 kextlog=0xfff -noprogress -v"
    boot_args, boot_args_phys, afdt_phys = build_boot_args(
        vbase, XNU_LOAD_ADDR, xnu_end_phys, dtb_data, command_line)
    print(f"  boot_args at phys {boot_args_phys:#x} ({BA_SIZE} bytes)")
    print(f"  afdt at phys {afdt_phys:#x} ({len(dtb_data)} bytes)")

    combined = image + boot_args + dtb_data
    xnu_image_path = os.path.join(OUT_DIR, "xnu_image.bin")
    with open(xnu_image_path, "wb") as f:
        f.write(combined)
    print(f"Wrote {xnu_image_path} ({len(combined)} bytes) for load at {XNU_LOAD_ADDR:#x}")

    print("Building EL2->EL1 trampoline...")
    asm_text = build_trampoline(boot_args_phys, xnu_entry_phys)
    with open(os.path.join(OUT_DIR, "trampoline.s"), "w") as f:
        f.write(asm_text)
    trampoline_bytes = assemble_trampoline(asm_text)
    trampoline_path = os.path.join(OUT_DIR, "trampoline.bin")
    with open(trampoline_path, "wb") as f:
        f.write(trampoline_bytes)
    print(f"Wrote {trampoline_path} ({len(trampoline_bytes)} bytes) for load at {TRAMPOLINE_ADDR:#x}")

    print("\n--- QEMU command ---")
    print(f"""qemu-system-aarch64 -M raspi3b -m 1024 -display none \\
  -serial null -serial stdio \\
  -device loader,file={xnu_image_path},addr={XNU_LOAD_ADDR:#x} \\
  -device loader,file={trampoline_path},addr={TRAMPOLINE_ADDR:#x} \\
  -device loader,addr={TRAMPOLINE_ADDR:#x},cpu-num=0 \\
  -monitor unix:{os.path.join(ROOT, "qemu/mon.sock")},server,nowait
""")


if __name__ == "__main__":
    main()
