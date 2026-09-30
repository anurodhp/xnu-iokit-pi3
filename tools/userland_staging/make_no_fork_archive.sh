#!/bin/bash
# DAR-292: derive libc_build/liblaunchd_syscalls_no_fork.a from
# liblaunchd_syscalls.a by dropping every member that DEFINES a member of the
# fork/wait/exec family libsystem_c.dylib now owns canonically
# (libsystem_c_fork_compat.c + real exec.c + wait4/waitpid; raw __fork is
# libsystem_kernel's). Consumers other than launchd/launchctl (which
# deliberately keep their private dispatch-aware fork_compat.o, see
# docs/fork-owner-audit.md) link THIS archive, so an undefined _fork/_waitpid/
# _execlp/_execve/_wait4 falls through to the dylib instead of being satisfied
# by an archive member (archives precede dylibs on these link lines).
# Membership is detected with nm, not hard-coded, so a newly added member
# that defines one of these is dropped too.
# usage: make_no_fork_archive.sh [SRC.a DST.a]   (default: liblaunchd_syscalls)
set -e
cd "$(dirname "$0")"
OUT=libc_build
SRC="${1:-$OUT/liblaunchd_syscalls.a}"
DST="${2:-$OUT/liblaunchd_syscalls_no_fork.a}"
[ -f "$SRC" ] || { echo "error: $SRC missing -- run build_launchd_syscalls.sh" >&2; exit 1; }
if [ -f "$DST" ] && [ "$DST" -nt "$SRC" ] && [ "$DST" -nt "$0" ]; then exit 0; fi
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
rm -f "$DST"; cp "$SRC" "$DST"
FAM='^(_fork|___fork|_vfork|_waitpid|_wait4|_wait|_execl|_execle|_execlp|_execv|_execvp|_execvP|_execve)$'
dropped=""
for m in $(ar t "$SRC"); do
    ar p "$SRC" "$m" >| "$TMP/m.o"
    if nm -m "$TMP/m.o" 2>/dev/null | grep -E '\(__TEXT,__text\) external ' | awk '{print $NF}' | grep -qE "$FAM"; then
        ar d "$DST" "$m"; dropped="$dropped $m"
    fi
done
ranlib "$DST" 2>/dev/null || true
echo "make_no_fork_archive: $DST (dropped:$dropped)"
