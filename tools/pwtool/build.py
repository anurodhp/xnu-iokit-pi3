#!/usr/bin/env python3
"""
Build pwtool -- the freestanding Phase 5 account-provisioning tool (see
pwtool.c's own header comment for what it covers and why it's one binary).

Fully static arm64 Mach-O, LC_UNIXTHREAD entry, zero LC_LOAD_DYLIB, -O0 --
this project hit a real miscompile at -O2 for a similarly-shaped
freestanding binary (the retired tools/init_binary/init.c, 2026-08-31;
see docs/static-linking-cleanup-plan.md for the retirement, project
memory for the original empirical bisection), so -O0 is kept here too as
the safe default rather than re-deriving that risk independently. This
one links three source files together (pwtool.c itself, plus
libinfo_custom.c and crypt_des.c from tools/libinfo_custom/ -- the account
resolver and the DES-crypt port this tool builds on).

Usage: tools/pwtool/build.py [-o OUTPUT]
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LIBINFO_DIR = os.path.join(os.path.dirname(HERE), "libinfo_custom")
DEFAULT_OUT = os.path.join(HERE, "pwtool.macho")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", default=DEFAULT_OUT)
    args = ap.parse_args()

    srcs = [
        os.path.join(HERE, "pwtool.c"),
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
        "-I", LIBINFO_DIR,
        "-O0",  # see header comment -- an empirically-motivated choice,
                # not copied blindly
        "-e", "__start",
        "-o", args.output,
    ] + srcs
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, check=True)

    size = os.path.getsize(args.output)
    print(f"wrote {args.output} ({size} bytes)")

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
