#!/bin/bash
# Phase 6.5 Phase B (docs/daemon-roadmap.md): builds dyld_test_exec.macho, a
# real dynamically-linked (non-static) executable -- the first one this
# project has ever produced. Its only purpose is to carry a real
# LC_LOAD_DYLINKER /usr/lib/dyld load command past the kernel's exec path;
# see dyld_stub_main.c/build_dyld_stub.sh for the /usr/lib/dyld side of this
# test and docs/daemon-roadmap.md Phase 6.5 for the full writeup.
#
# Confirmed experimentally this session: ld64 (this project's pinned
# Xcode-12 toolchain) emits LC_LOAD_DYLINKER automatically for any non
# -static link with at least one dylib on the command line -- no explicit
# -dylinker/-execute flag needed on this side (those are for building dyld
# itself, see build_dyld_stub.sh). Linking against build_dylib_smoketest.sh's
# libtest.dylib is solely to give ld64 a dylib to record, forcing that
# default (dynamic) link mode; the symbol it exports is never called.
#
# Csu's start.s/crt.c ARE linked in here even though the kernel's
# load_dylinker() overwrites this binary's own thread state/PC with the
# dylinker's entry, discarding wherever this file's own entry points --
# a real, first-exercise kernel bug found via this exact test 2026-08-25:
# third_party/xnu-7195/bsd/kern/mach_loader.c's load_dylinker() copies
# myresult->{threadstate,entry_point,validentry,...} from the dylinker's
# own parse back onto the shared `result`, but NOT myresult->unixproc.
# bsd/kern/kern_exec.c only calls exec_copyout_strings()/
# thread_setuserstack() -- which is what actually initializes the new
# thread's SP -- when result->unixproc is TRUE. A dyld_test_exec.macho
# built WITHOUT its own valid LC_UNIXTHREAD/LC_MAIN (as an early version
# of this script did, linking only dyld_test_exec_main.o against the
# dylib) never sets result->unixproc on the outer image's own parse, and
# since the dylinker's TRUE value never propagates, the new thread starts
# with SP uninitialized garbage -- Csu's first instruction
# (`ldr x0, [sp]`, reading argc) then faults at a garbage address
# (confirmed via a temporary bsd/uxkern/ux_exception.c diagnostic:
# EXC_BAD_ACCESS/KERN_INVALID_ADDRESS at an address unrelated to any real
# segment or the stack). Giving this binary its own real LC_UNIXTHREAD via
# Csu -- even though its PC is never actually used -- makes
# result->unixproc TRUE independently of the dylinker, which is enough to
# fix SP initialization for the whole process. Real, upstream Apple dyld
# never hits this because every real dylib1.o/crt1.o-linked executable
# always carries its own valid entry command; this is specific to a
# minimal from-scratch test binary, not a bug real userland will ever
# exercise once real crt1-equivalent glue exists (Phase C).
set -e
cd "$(dirname "$0")"

OUT="libc_build"
if [ ! -f "$OUT/libtest.dylib" ]; then
    echo "error: $OUT/libtest.dylib not found -- run build_dylib_smoketest.sh first" >&2
    exit 1
fi

CSU="../../third_party/Csu"
if [ ! -d "$CSU" ]; then
    echo "error: $CSU not found -- run ./setup_third_party.sh first" >&2
    exit 1
fi

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

# Real, verified fix (2026-08-28, see docs/backlog.md item d9 for the
# full trail and tools/userland_staging/build_simple_cmds.sh's own
# NEWLD_BINDIR header comment for the isolated repro this is based on):
# Xcode 12's own bundled ld64-609 has a real internal crash under
# -Wl,-fixup_chains that a much newer ld does not. Compiling still uses
# Xcode 12's clang/SDK above (untouched -- that vintage match is a
# separate, still-load-bearing invariant); only the final link step's
# linker binary changes, via clang's -B override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding \
    -fno-builtin -fno-stack-protector -O0 -Wall dyld_test_exec_main.c -o dyld_test_exec_main.o

# -Wl,-fixup_chains: real dyld-940 (this project's currently pinned
# tag, see docs/daemon-roadmap.md's Phase C retarget) was built and
# tested by Apple against modern chained-fixups binaries (its own real
# configs/dyld.xcconfig even links dyld ITSELF with this same flag) --
# without it, ld64 emits the older LC_DYLD_INFO_ONLY bind-opcode format,
# which real dyld-940's JustInTimeLoader real bind/fixup code hit a
# real crash on (EXC_BAD_ACCESS, fault address 0x10, found via a real
# temporary bsd/uxkern/ux_exception.c diagnostic, 2026-08-25) -- see
# docs/daemon-roadmap.md for the full writeup.
# Real, unconditional requirement (dyldMain.cpp, found+closed 2026-08-25
# via build_libdyld.sh/build_libsystem.sh): dyld halts every launch
# unless the process loaded BOTH /usr/lib/system/libdyld.dylib and
# /usr/lib/libSystem.B.dylib (RuntimeState::add() matches those exact
# install_names). Linking directly against the built .dylib files embeds
# their own real LC_ID_DYLIB install_names into this executable's
# LC_LOAD_DYLIB entries, regardless of the local build path referenced
# here -- same mechanism already proven by libtest.dylib.
if [ ! -f "$OUT/system/libdyld.dylib" ] || [ ! -f "$OUT/system/libSystem.B.dylib" ]; then
    echo "error: $OUT/system/{libdyld,libSystem.B}.dylib not found -- run build_libdyld.sh and build_libsystem.sh first" >&2
    exit 1
fi
# -DCRT_DYNAMIC_LINKING (DAR-161, 2026-08-31): this is a genuinely
# dynamically-linked (LC_LOAD_DYLINKER) consumer, so Csu's own
# NXArgc/NXArgv/environ/__progname global storage must NOT be linked in
# -- see crt.c's own header comment on those four globals. Without this
# flag, crt.c's unconditional local definitions silently shadowed the
# real, canonical copies libdyld.dylib's libdyldGlue.o exports (confirmed
# via `nm -m`: a local `(__DATA,__common) external _environ` etc instead
# of a bind), meaning any code in this binary that read `environ`/
# `NXArgc`/`NXArgv`/`__progname` directly (not through getenv()/
# setenv()) saw a permanently-stale, never-updated local copy instead of
# dyld's real one. With this flag, those four become plain `extern`
# references that bind against libdyld.dylib via a normal chained-fixups
# GOT bind, verified via `xcrun dyld_info -fixups` showing
# `bind  libdyld/_NXArgc` etc. This is a NEW, project-owned macro, not
# real Apple's `__DYNAMIC__` -- this project's pinned Xcode-12/iPhoneOS
# SDK toolchain predefines `__DYNAMIC__=1` unconditionally for every
# arm64-apple-ios* compile regardless of -static (confirmed via
# `clang -dM -E -`), so reusing it would have silently broken every
# existing static Csu consumer too; see crt.c's own header comment for
# the full first-attempt-caught-it-first writeup. Csu's start.s has its
# own matching `#if !CRT_DYNAMIC_LINKING` guard on the direct-store/
# crt_init_program_vars() sequence this same flag also disables (harmless
# either way here since the kernel's real load_dylinker() handoff
# overwrites this binary's own thread state/PC before start.s's `start:`
# label ever runs at all, per this script's own header comment above).
rm -f dyld_test_exec.macho
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o dyld_test_exec.macho \
    "$CSU/start.s" "$CSU/crt.c" dyld_test_exec_main.o "$OUT/libtest.dylib" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib"
rm -f dyld_test_exec_main.o

echo "wrote dyld_test_exec.macho"
otool -l dyld_test_exec.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: no LC_LOAD_DYLINKER /usr/lib/dyld -- not a real dynamic executable" >&2
    exit 1
}
otool -l dyld_test_exec.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: no LC_LOAD_DYLIB -- not a real dynamic executable" >&2
    exit 1
}
echo "confirmed: dyld_test_exec.macho has real LC_LOAD_DYLINKER /usr/lib/dyld + LC_LOAD_DYLIB"
