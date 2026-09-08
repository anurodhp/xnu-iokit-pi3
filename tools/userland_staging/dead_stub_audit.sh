#!/bin/bash
# Grades every image passed in for mach_ipc_dead_stubs.c's three
# KERN_FAILURE stand-ins: ERROR if something real calls one, NOTE if the
# stand-in is carried but every call site is the provably-unreachable
# path it was written for.
#
# Usage: ./dead_stub_audit.sh <image> [<image> ...]
#
# ---- Why this exists (DAR-261, 2026-09-07) ----
#
# tools/userland_staging/mach_ipc_dead_stubs.c defines
# _mach_port_insert_right / _mach_port_extract_right / _mach_port_unguard
# as unconditional `return KERN_FAILURE` stand-ins. Its own header is
# explicit that they exist only to satisfy mach_msg_destroy_port()'s
# provably-unreachable MAKE_SEND / GUARDED_PORT_DESCRIPTOR reply-
# validation paths for three specific MIG routines -- they are NOT
# general-purpose implementations, and the real bodies live in
# libsystem_kernel.dylib (mach_port_gap_closures2.c).
#
# The object rides into binaries that never wanted it: mach_msg_real.o
# lives in the SAME archive (libc_build/libmach_ipc.a) and references all
# three symbols, so ld extracts mach_ipc_dead_stubs.o whenever that
# archive is scanned before libsystem_kernel.dylib on the link line (for
# a dynamic link), or whenever the archive is scanned at all with no
# other definition available (for a static link, where there is no
# dylib to shadow in the first place). The result is a
# `mov w0, #0x5; ret` body baked into the executable as the ONLY
# definition.
#
# Nothing else catches this. The link is clean, `nm -m` is clean (the
# symbol is defined, just wrongly), and bind_target_audit.sh passes --
# there is no bind to audit for a locally-resolved symbol. The only
# visible symptom, if the stub is ever actually called, is a runtime
# KERN_FAILURE from a call that should have worked. In notifyd that was
# __notify_generate_common_port's `assert(kstatus == KERN_SUCCESS)`
# after mach_port_insert_right(mach_task_self(), port, port,
# MACH_MSG_TYPE_MAKE_SEND), which killed the MIG handler thread and hung
# every notify client forever while the daemon itself stayed resident.
#
# Fix for an ERROR-graded image: list libsystem_kernel.dylib EARLIER
# than libmach_ipc.a on that binary's link line, IF the binary links a
# dylib at all -- do not delete the reference and do not weaken the real
# body. This is not always safe or even available:
#   - A fully static binary (no LC_LOAD_DYLIB at all) cannot bind
#     libsystem_kernel.dylib by reordering; there is nothing to reorder
#     onto. build_login.sh/build_getty.sh are REQUIRED to stay fully
#     static (CLAUDE.md) -- the reorder fix is categorically unavailable
#     to them.
#   - Even a dynamic binary's reorder needs a real boot test, not just a
#     clean link: configd regressed into a crash loop the first time
#     this exact reorder was tried against it, and separately,
#     build_simple_cmds.sh's own sleep/tail case (DAR-204) documents a
#     confirmed WORSE regression from listing libsystem_kernel.dylib
#     ahead of a narrowed libmach_ipc.a copy -- clock_get_time/
#     host_get_clock_service/semaphore_create silently started
#     resolving to the dylib instead of this project's own local MIG
#     stubs the instant the dylib was listed first. Reordering fixes one
#     bug and can reopen another; always re-verify with a real boot.
#
# ---- Carrying the stub vs. actually calling it (DAR-263) ----
#
# "Defines the stub locally" and "is broken by the stub" are NOT the same
# thing, and treating them as one number badly overstated DAR-263's
# original scope. A full sweep of every shipped image on 2026-09-07
# found nine carrying it; resolving every call site (nm -n + otool -tV)
# showed that eight of them reach it ONLY from
# `mach_msg_destroy_port`/`mach_msg_destroy` -- the exact provably-
# unreachable reply-validation path mach_ipc_dead_stubs.c was written
# for. In those eight the stand-in is doing its job. configd.macho was
# the only image with a genuine caller (___serverSessionCreate + 0x28c,
# every SCDynamicStore client session) -- fixed separately.
#
# So this script grades, rather than just detecting:
#
#   ERROR  - a call site outside mach_msg_destroy_port()/mach_msg_destroy():
#            a real caller is getting KERN_FAILURE right now. Fix the link.
#   NOTE   - carried, but every call site is inside that dead path:
#            working as designed. Not a build failure.
#
# The NOTE case is deliberately not fatal (exit 0). Making it fatal
# would fail the builds of login.macho/getty.macho, which CLAUDE.md
# requires to stay fully static and therefore cannot bind
# libsystem_kernel.dylib at all -- the reorder fix is not even available
# to them, and there is no live bug to fix.
#
# Deliberately checks the built image with nm/otool rather than
# inspecting the link line: the ordering that matters can be changed in
# more than one place (a shared variable, an inserted archive, a reused
# .o list), and only the artifact tells the truth.
set -u

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <image> [<image> ...]" >&2
    exit 2
fi

# A missing image is reported and marked as a failure, but does NOT
# stop the rest of the batch from being graded, 2026-09-07 (code
# review): the earlier form exited on the first missing image before
# the python step ever ran, so `./dead_stub_audit.sh missing.macho
# real_bug.macho` silently never reported real_bug.macho's genuine
# ERROR. Present images are passed through to the grader below;
# PRESENT_IMAGES tracks which ones so the python step never has to
# stat() a path this loop already knows is absent.
rc_missing=0
PRESENT_IMAGES=()
for img in "$@"; do
    if [ ! -f "$img" ]; then
        echo "dead_stub_audit: error: $img not found" >&2
        rc_missing=1
    else
        PRESENT_IMAGES+=("$img")
    fi
done
if [ "${#PRESENT_IMAGES[@]}" -eq 0 ]; then
    exit 1
fi

# The grading itself: resolve every `bl <stub>` call site in the image
# back to its containing function (by walking `nm -n`'s sorted symbol
# table the same way a crash backtrace address is resolved), and report
# ERROR only if some resolved caller is not mach_msg_destroy_port/
# mach_msg_destroy. Written in Python rather than awk: this project's
# shipped /usr/bin/awk has no strtonum(), which every portable hex-
# address-comparison approach needs.
python3 - "${PRESENT_IMAGES[@]}" <<'PYEOF'
import subprocess
import sys

STUBS = ("_mach_port_insert_right", "_mach_port_extract_right", "_mach_port_unguard")
DEAD_CALLERS = ("_mach_msg_destroy_port", "_mach_msg_destroy")

overall_rc = 0

for img in sys.argv[1:]:
    nm_out = subprocess.run(["nm", img], capture_output=True, text=True)
    if nm_out.returncode != 0:
        # nm's own failure must not read as "clean" -- a missing/corrupt/
        # wrong-arch image would otherwise yield an empty symbol table
        # that looks exactly like a pass.
        print(f"dead_stub_audit: error: nm failed on {img}:", file=sys.stderr)
        print(nm_out.stderr or nm_out.stdout, file=sys.stderr)
        overall_rc = 1
        continue

    carried = [s for s in STUBS
               if any(line.split()[1:3] == ["T", s] for line in nm_out.stdout.splitlines()
                      if len(line.split()) >= 3)]
    if not carried:
        continue

    nm_n_run = subprocess.run(["nm", "-n", img], capture_output=True, text=True)
    if nm_n_run.returncode != 0:
        print(f"dead_stub_audit: error: nm -n failed on {img}:", file=sys.stderr)
        print(nm_n_run.stderr or nm_n_run.stdout, file=sys.stderr)
        overall_rc = 1
        continue
    syms = []
    for line in nm_n_run.stdout.splitlines():
        parts = line.split()
        if len(parts) < 3 or parts[1] not in ("T", "t"):
            continue
        try:
            addr = int(parts[0], 16)
        except ValueError:
            continue
        syms.append((addr, parts[2]))
    syms.sort()

    dis_run = subprocess.run(["otool", "-tV", img], capture_output=True, text=True)
    if dis_run.returncode != 0:
        # Must not read as "clean": an empty `dis` below would make
        # every carried stub look dead-path-only, silently downgrading
        # a real ERROR (a genuine live caller, exactly DAR-263's configd
        # bug) to a false NOTE -- the one failure mode this whole script
        # exists to rule out.
        print(f"dead_stub_audit: error: otool -tV failed on {img}:", file=sys.stderr)
        print(dis_run.stderr or dis_run.stdout, file=sys.stderr)
        overall_rc = 1
        continue
    dis = dis_run.stdout

    def resolve(addr):
        # None (no symbol at or before addr) is a real, if rare,
        # possibility -- not every address a `bl` targets from is
        # guaranteed to fall after some T/t symbol (a stripped image, or
        # nm -n emitting nothing usable). Treated as its own case below
        # rather than compared/sorted against real name strings, which
        # would either crash (str/NoneType are unorderable in Python 3)
        # or silently print "None" as if it were a function name.
        last = None
        for a, n in syms:
            if a <= addr:
                last = n
            else:
                break
        return last

    for s in carried:
        callers = set()
        unresolved = False
        for line in dis.splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[1] == "bl" and parts[2] == s:
                try:
                    caller = resolve(int(parts[0], 16))
                except ValueError:
                    continue
                if caller is None:
                    unresolved = True
                else:
                    callers.add(caller)
        live = sorted(c for c in callers if c not in DEAD_CALLERS)
        if unresolved:
            # An unresolved call site is treated as live (ERROR), not
            # silently dropped: "cannot prove this is the dead path" is
            # the same actionable state as "this is not the dead path"
            # for a check whose whole job is to rule out silent
            # KERN_FAILURE. False positives here cost a `otool -tV -p`
            # by hand; false negatives cost another DAR-263.
            print(f"dead_stub_audit: error: {img} calls {s} from an address that", file=sys.stderr)
            print("    resolve()d to no symbol -- cannot prove this is the dead", file=sys.stderr)
            print("    mach_msg_destroy_port()/mach_msg_destroy() path. Treating as live.", file=sys.stderr)
            print(f"        otool -tV {img} | grep -B20 'bl.*{s}'  # find the real caller by hand", file=sys.stderr)
            overall_rc = 1
        if live:
            print(f"dead_stub_audit: error: {img} defines {s} locally AND is called from:",
                  file=sys.stderr)
            for c in live:
                print(f"        {c}", file=sys.stderr)
            print("    That is mach_ipc_dead_stubs.o's KERN_FAILURE stand-in shadowing",
                  file=sys.stderr)
            print("    libsystem_kernel.dylib's real body (DAR-261). Confirm with:",
                  file=sys.stderr)
            print(f"        otool -tV -p {s} {img}      # a bare 'mov w0, #0x5' is the tell",
                  file=sys.stderr)
            print("    See this script's own header for the fix and its real constraints.",
                  file=sys.stderr)
            overall_rc = 1
        elif not unresolved:
            print(f"dead_stub_audit: note: {img} carries {s}, but every call site is")
            print("    inside mach_msg_destroy_port()/mach_msg_destroy() -- the")
            print("    provably-unreachable path mach_ipc_dead_stubs.c was written for.")
            print("    Working as designed (DAR-263); not a build failure.")

if overall_rc == 0:
    print("dead_stub_audit: no image has a REACHABLE mach_ipc_dead_stubs.o stand-in")
sys.exit(overall_rc)
PYEOF
py_rc=$?
[ "$py_rc" -ne 0 ] && rc_missing=1
exit "$rc_missing"
