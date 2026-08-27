#!/bin/bash
# Real-gap triage for a built dylib or Mach-O executable in this
# project's dyld-based userland stack. Run this BEFORE a real QEMU boot
# test on any new/changed binary that links libdispatch/libsystem_*/etc
# -- it answers the same questions a real QEMU crash-and-inspect cycle
# answers (which symbol is really missing, and is it actually available
# somewhere already), just without spending a boot cycle to find out.
# See CLAUDE.md's "Working conventions specific to this repo" for the
# standing rule this exists to support.
#
# Usage:
#   tools/userland_staging/gap_audit.sh <target> [sibling-image ...]
#
#   <target>          the dylib or .macho to audit (its own real
#                      undefined-symbol table is what gets triaged)
#   [sibling-image...] other images that will be loaded in the SAME real
#                      process at runtime (e.g. an executable's other
#                      dylib dependencies) -- a symbol only "dynamically
#                      looked up" in <target> but really DEFINED in one
#                      of these is not a gap at all, just a normal real
#                      flat-namespace/two-level-namespace resolution
#                      (same distinction build_libdispatch_dylib.sh's own
#                      undefined-symbol report already makes for a single
#                      image; this widens it to the whole real process).
#
# Example (syslogd + everything it dynamically loads):
#   tools/userland_staging/gap_audit.sh libc_build/syslogd.macho \
#       libc_build/system/libdispatch.dylib libc_build/system/libdyld.dylib \
#       libc_build/system/libSystem.B.dylib libc_build/system/libsystem_pthread.dylib
#
# What it does, per real undefined symbol in <target> (nm -m, filtering
# to entries marked "(dynamically looked up)" -- entries marked
# "(from libX)" are already real, safe two-level-namespace bindings, not
# gaps, and are skipped):
#   1. Whole-process check: already defined in one of [sibling-image...]?
#      -> not a real gap, just report where.
#   2. Already-built-archive check: already a real, defined `T` export
#      of ANY .a/.dylib already sitting under libc_build/, just not
#      linked into <target> yet? -> free win, report which archive.
#   3. Neither: classify against real Apple source so the next real port
#      step is obvious --
#        - bsd/kern/syscalls.master: real BSD syscall (report number,
#          flag NO_SYSCALL_STUB if present -- that means a hand raw-trap
#          is needed, not a create-syscalls.pl-generated stub)
#        - osfmk/mach/syscall_sw.h: real Mach kernel_trap (report trap
#          number)
#        - osfmk/mach/*.defs: real MIG routine (report which .defs)
#        - none of the above: genuinely new/unclear, needs a real,
#          by-hand source investigation (same as this pass's own
#          mach_eventlink_* family -- flag as "no real trap/syscall/MIG
#          hit, needs manual investigation", don't guess further)
#
# This script only reports. It never edits a build script or writes new
# source -- closing a real gap still means following this project's own
# established loop (check real source, write/extract the narrowest real
# implementation, verify via nm before wiring in, real QEMU reboot to
# confirm), same as every existing tools/userland_staging/build_*.sh.

set -u
cd "$(dirname "$0")/../.."

TARGET="${1:-}"
if [ -z "$TARGET" ] || [ ! -f "$TARGET" ]; then
    echo "usage: $0 <dylib-or-macho> [sibling-image ...]" >&2
    exit 1
fi
shift || true
SIBLINGS=("$@")

XNU="third_party/xnu-7195"
STAGING="tools/userland_staging"

# Every already-built archive/dylib in this project's staging tree --
# the "is this a free win" search space.
ARCHIVES=()
while IFS= read -r f; do
    ARCHIVES+=("$f")
done < <(find "$STAGING/libc_build" -maxdepth 1 \( -name '*.a' -o -name '*.dylib' \) 2>/dev/null; \
          find "$STAGING/libc_build/system" -maxdepth 1 -name '*.dylib' 2>/dev/null)

echo "target: $TARGET"
echo "sibling images checked: ${SIBLINGS[*]:-none given}"
echo "already-built archives/dylibs scanned: ${#ARCHIVES[@]}"
echo

GAPS=$(nm -m "$TARGET" 2>/dev/null | grep undefined | grep -v "(from " \
    | sed -E 's/.*external //' | sed -E 's/ \(dynamically looked up\)//')

if [ -z "$GAPS" ]; then
    echo "no real gaps -- every undefined symbol in $TARGET is already a" \
         "safe two-level-namespace binding to a sibling dylib."
    exit 0
fi

n_total=0
n_process=0
n_freewin=0
n_syscall=0
n_trap=0
n_mig=0
n_unknown=0

while IFS= read -r sym; do
    [ -z "$sym" ] && continue
    n_total=$((n_total + 1))
    cname="${sym#_}"   # strip one leading underscore for source grep (C-level name)

    found_in=""
    for img in "${SIBLINGS[@]}"; do
        if nm -g "$img" 2>/dev/null | grep -q " T $sym\$"; then
            found_in="$img"
            break
        fi
    done
    if [ -n "$found_in" ]; then
        echo "OK      $sym -- already defined in sibling image $found_in (real flat/two-level-namespace resolution, not a gap)"
        n_process=$((n_process + 1))
        continue
    fi

    found_archive=""
    for a in "${ARCHIVES[@]}"; do
        if nm -g "$a" 2>/dev/null | grep -q " T $sym\$"; then
            found_archive="$a"
            break
        fi
    done
    if [ -n "$found_archive" ]; then
        echo "FREEWIN $sym -- already a real, defined export of $found_archive -- just needs linking/isolating, not new porting work"
        n_freewin=$((n_freewin + 1))
        continue
    fi

    # Classification against real Apple source, in priority order.
    sc_line=$(grep -E "^\s*[0-9]+\s.*[^A-Za-z_]${cname}\(" "$XNU/bsd/kern/syscalls.master" 2>/dev/null | head -1)
    if [ -n "$sc_line" ]; then
        num=$(echo "$sc_line" | awk '{print $1}')
        stub_note=""
        echo "$sc_line" | grep -q NO_SYSCALL_STUB && stub_note=" (NO_SYSCALL_STUB -- needs a hand raw-trap, not a generated stub; see pthread_bsdthread_register_syscall.c for the established pattern)"
        echo "SYSCALL $sym -- real BSD syscall #$num in bsd/kern/syscalls.master$stub_note"
        n_syscall=$((n_syscall + 1))
        continue
    fi

    trap_line=$(grep -E "kernel_trap\(${cname},|kernel_trap\(_${cname}," "$XNU/osfmk/mach/syscall_sw.h" 2>/dev/null | head -1)
    if [ -n "$trap_line" ]; then
        echo "MACHTRAP $sym -- real Mach kernel_trap in osfmk/mach/syscall_sw.h: $trap_line"
        n_trap=$((n_trap + 1))
        continue
    fi

    mig_file=$(grep -lE "\\broutine\\s+${cname}\\(" "$XNU"/osfmk/mach/*.defs 2>/dev/null | head -1)
    if [ -n "$mig_file" ]; then
        echo "MIG     $sym -- real MIG routine declared in $mig_file -- needs a real mig regen + client-stub extraction (see build_corefoundation.sh's mach_vm_region precedent, or this project's host_priv.defs precedent for host_get_special_port/host_create_mach_voucher)"
        n_mig=$((n_mig + 1))
        continue
    fi

    echo "UNKNOWN $sym -- no real syscall/Mach-trap/MIG hit found -- needs manual source investigation (same class as the mach_eventlink_* family)"
    n_unknown=$((n_unknown + 1))
done <<< "$GAPS"

echo
echo "summary: $n_total real gaps -- $n_process already resolved elsewhere in the process, $n_freewin free wins (already built, not linked), $n_syscall real syscalls, $n_trap real Mach traps, $n_mig real MIG routines, $n_unknown need manual investigation"
