#!/bin/bash
# Fails if an image carries a LOCAL definition of one of
# mach_ipc_dead_stubs.c's three KERN_FAILURE stand-ins, instead of
# binding libsystem_kernel.dylib's real implementation.
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
# archive is scanned before libsystem_kernel.dylib on the link line. The
# result is a `mov w0, #0x5; ret` body baked into the executable as the
# ONLY definition, shadowing the real one, which the linker then never
# even consults.
#
# Nothing else catches this. The link is clean, `nm -m` is clean (the
# symbol is defined, just wrongly), and bind_target_audit.sh passes --
# there is no bind to audit, because the reference was resolved
# statically. The only visible symptom is a runtime KERN_FAILURE from a
# call that should have worked. In notifyd that was
# __notify_generate_common_port's
# `assert(kstatus == KERN_SUCCESS)` after
# mach_port_insert_right(mach_task_self(), port, port,
# MACH_MSG_TYPE_MAKE_SEND), which killed the MIG handler thread and hung
# every notify client forever while the daemon itself stayed resident.
#
# Fix when this fires: list libsystem_kernel.dylib EARLIER than
# libmach_ipc.a on that binary's link line. Do not delete the reference
# and do not weaken the real body. Note that this is not always a
# mechanical change -- configd regressed into a crash loop when the same
# reorder was applied to it (see DAR-263/DAR-265), so a reorder needs a
# real boot test, not just a clean link.
#
# Deliberately checks the built image with nm rather than inspecting the
# link line: the ordering that matters can be changed in more than one
# place (a shared variable, an inserted archive, a reused .o list), and
# only the artifact tells the truth.
set -u

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <image> [<image> ...]" >&2
    exit 2
fi

STUBS="_mach_port_insert_right _mach_port_extract_right _mach_port_unguard"
rc=0

for img in "$@"; do
    if [ ! -f "$img" ]; then
        echo "dead_stub_audit: error: $img not found" >&2
        rc=1
        continue
    fi
    # nm's own failure must not read as "clean" -- capture it rather than
    # piping straight into grep, where a missing/corrupt/wrong-arch image
    # yields an empty stream that looks exactly like a pass.
    if ! syms="$(nm "$img" 2>&1)"; then
        echo "dead_stub_audit: error: nm failed on $img:" >&2
        echo "$syms" >&2
        rc=1
        continue
    fi
    for s in $STUBS; do
        if printf '%s\n' "$syms" | grep -q "^[0-9a-f]* T $s\$"; then
            echo "dead_stub_audit: error: $img defines $s locally" >&2
            echo "    That is mach_ipc_dead_stubs.o's KERN_FAILURE stand-in shadowing" >&2
            echo "    libsystem_kernel.dylib's real body (DAR-261). Confirm with:" >&2
            echo "        otool -tV -p $s $img      # a bare 'mov w0, #0x5' is the tell" >&2
            echo "    Fix by listing libsystem_kernel.dylib ahead of libmach_ipc.a on" >&2
            echo "    that binary's link line, then re-verify with a real boot." >&2
            rc=1
        fi
    done
done

if [ "$rc" -eq 0 ]; then
    echo "dead_stub_audit: no image defines a mach_ipc_dead_stubs.o stand-in locally"
fi
exit "$rc"
