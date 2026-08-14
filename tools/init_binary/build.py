#!/usr/bin/env python3
"""
Build the freestanding init/shell binary that mockfs execs as PID 1.

Produces a fully static arm64 Mach-O: LC_UNIXTHREAD entry, zero
LC_LOAD_DYLIB commands, so the kernel exec path never invokes dyld (see
init.c's header comment for why that matters here). This relies on
Apple's own clang/ld64 (`-target arm64-apple-macos11 ... -static -e
__start`) to do real relocation/segment layout -- confirmed experimentally
that ld64 still emits LC_UNIXTHREAD for a fully static arm64 executable
(not documented/guaranteed Apple behavior, but observed and stable enough
to build on for this bootstrap milestone).

Usage: tools/init_binary/build.py [-o OUTPUT]
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LIBINFO_DIR = os.path.join(os.path.dirname(HERE), "libinfo_custom")
DEFAULT_OUT = os.path.join(HERE, "init.macho")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", default=DEFAULT_OUT)
    args = ap.parse_args()

    srcs = [
        os.path.join(HERE, "init.c"),
        # Phase 5: real login/su -- init.c's own #include lines pull in
        # libinfo_custom.h/crypt_des.h by relative path
        # ("../libinfo_custom/..."), but the corresponding .c
        # implementations still need to be compiled+linked in as sibling
        # translation units, same as tools/pwtool/build.py already does
        # for the same two files.
        os.path.join(LIBINFO_DIR, "libinfo_custom.c"),
        os.path.join(LIBINFO_DIR, "crypt_des.c"),
    ]
    cmd = [
        "clang",
        "-target", "arm64-apple-macos11",
        "-ffreestanding",
        "-fno-stack-protector",
        "-fno-builtin",
        "-nostdlib",
        "-static",
        "-Wall", "-Wextra",
        # -O0, not -O2: -O2 tail-merges/interleaves the builtin-dispatch
        # string comparisons and cmd_* handlers into one large, heavily
        # cross-jumping function. That codegen reproducibly (3/3 runs)
        # SIGSEGVs partway through `ls /`'s output loop, and reproducibly
        # (3/3 runs) does NOT at -O0 - see project memory for the full
        # empirical bisection (adding filler put() calls also avoided it,
        # implicating code layout/demand-paging of msdosfs-backed text
        # pages, not the C source itself). No performance requirement here
        # (tiny bootstrap shell), so -O0 is the real fix, not a workaround.
        "-O0",
        "-e", "__start",
        "-o", args.output,
    ] + srcs
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, check=True)

    size = os.path.getsize(args.output)
    print(f"wrote {args.output} ({size} bytes)")

    # Sanity checks that matter for mockfs/load_machfile: no dylib deps, has
    # a real LC_UNIXTHREAD. Doesn't require otool to succeed to build, but
    # surfaces a loud warning if the toolchain's behavior ever changes.
    try:
        out = subprocess.run(["otool", "-l", args.output],
                              capture_output=True, text=True, check=True).stdout
        if "LC_LOAD_DYLIB" in out:
            print("WARNING: output has LC_LOAD_DYLIB -- it will need dyld, "
                  "this build is no longer freestanding", file=sys.stderr)
        if "LC_UNIXTHREAD" not in out:
            print("WARNING: no LC_UNIXTHREAD in output -- kernel exec won't "
                  "know where to start without dyld", file=sys.stderr)
    except FileNotFoundError:
        pass


if __name__ == "__main__":
    main()
