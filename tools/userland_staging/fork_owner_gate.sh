#!/bin/bash
# DAR-292 gate: no shipped image may DEFINE the fork/wait/exec family.
# libsystem_c.dylib is the single owner of _fork/_waitpid/_wait4/_execl*/
# _execv*/_execve (libsystem_c_fork_compat.c: real atfork handlers +
# _libc_fork_child()); libsystem_kernel.dylib owns the raw ___fork trap.
# A private copy has no/NULL atfork handlers (libmalloc/libpthread/
# libdispatch/dyld fork-child fix-ups never run), desyncs the cached
# __current_pid, and is a duplicate definition (dup_symbol_audit class).
#
# usage: fork_owner_gate.sh [image ...]   (default: every built .macho)
# exit 1 on any definition not in EXCEPTIONS below.
#
# EXCEPTIONS (image:symbol, reason) -- every entry needs a documented reason
# (docs/fork-owner-audit.md):
#   pwtool.macho        -- must stay fully static (DAR-314); defines none today,
#                          listed so a future static libc there is not flagged.
#   launchd.macho       -- PID 1 keeps its private, dispatch-aware fork_compat.o
#                          (build_launchd_syscalls.sh): libsystem_c's fork cannot
#                          call dispatch_atfork_*/_dyld_fork_child (libdispatch is
#                          built AFTER libsystem_c -> ld64 cycle; -U deferral
#                          aborts every non-dispatch binary), launchd needs those.
#   kqueue_smoketest.macho:_execve -- test links simple_obj/sys_*.o wholesale.
#   libsystem_c.dylib / libsystem_kernel.dylib -- the owners.
cd "$(dirname "$0")"
FAM='^(_fork|___fork|_waitpid|_wait4|_execl|_execle|_execlp|_execv|_execvp|_execvP|_execve)$'
is_exception() { # image sym
    case "$(basename "$1")" in
        pwtool.macho|launchd.macho|libsystem_c.dylib) return 0 ;;
        libsystem_kernel.dylib) [ "$2" = ___fork ] && return 0 ;;
        kqueue_smoketest.macho) [ "$2" = _execve ] && return 0 ;;
    esac
    return 1
}
if [ $# -gt 0 ]; then imgs=("$@"); else
    imgs=(*.macho libc_build/*.macho ../pwtool/pwtool.macho libc_build/system/libsystem_c.dylib libc_build/system/libsystem_kernel.dylib); fi
bad=0
for f in "${imgs[@]}"; do
    [ -f "$f" ] || continue
    for s in $(nm -m "$f" 2>/dev/null | grep -E '\(__TEXT,__text\) (non-external |external |private external )' | awk '{print $NF}' | grep -E "$FAM"); do
        is_exception "$f" "$s" && continue
        echo "FAIL: $f defines $s locally (must bind libsystem_c.dylib)" >&2; bad=1
    done
done
[ $bad -eq 0 ] && echo "fork_owner_gate: no private fork/wait/exec definitions outside the documented exceptions" || exit 1
