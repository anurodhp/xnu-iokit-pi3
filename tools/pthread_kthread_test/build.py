#!/usr/bin/env python3
"""
Build pthread_kthread_test.macho - the real-second-Mach-thread smoke test
for third_party/xnu-7195/bsd/kern/bsd_pthread_stub.c's real bsdthread_create()
(see pthread_kthread_test.c's own header comment for what it proves and how).

Same build shape as tools/init_binary/build.py: fully static arm64 Mach-O,
LC_UNIXTHREAD entry, zero LC_LOAD_DYLIB, -O0 (this project hit a real -O2
miscompile on a similarly-shaped freestanding binary before - see
tools/init_binary/build.py's own comment - so -O0 is kept here too as the
safe default rather than re-deriving that risk independently).

Usage: tools/pthread_kthread_test/build.py [-o OUTPUT]
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_OUT = os.path.join(HERE, "pthread_kthread_test.macho")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", default=DEFAULT_OUT)
    args = ap.parse_args()

    srcs = [os.path.join(HERE, "pthread_kthread_test.c")]
    cmd = [
        "clang",
        "-target", "arm64-apple-macos11",
        "-ffreestanding",
        "-fno-stack-protector",
        "-fno-builtin",
        "-nostdlib",
        "-static",
        "-Wall", "-Wextra",
        "-O0",
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
