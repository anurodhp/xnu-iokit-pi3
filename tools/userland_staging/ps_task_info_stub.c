/* Real, unmodified third_party/adv_cmds/ps/tasks.c is NOT compiled into
 * this project's ps.macho: its get_task_info() unconditionally calls real
 * Mach IPC (task_for_pid/task_info/task_threads/thread_info, all real
 * mach_msg()-trap-based MIG RPC) for every listed process, every run, not
 * gated behind any optional flag (see ps.c's main loop, "keepit:" label).
 * This project has never built real mach_msg()-based MIG RPC -- confirmed
 * absent, same documented gap as tools/userland_staging/
 * libmalloc_vm_mig_stubs.c's real _kernelrpc_mach_vm_* stand-ins ("real
 * mach_msg()-based MIG RPC ... is a materially larger, separate subsystem
 * this project hasn't built").
 *
 * Real Apple tasks.c's own get_task_info() already has a real, designed
 * degradation path for exactly this failure: if task_for_pid() returns
 * anything other than KERN_SUCCESS, it sets ki->state = STATE_MAX (real
 * value 7, tasks.c's own #define) and returns 1 immediately, leaving
 * ki->tasks_info/ki->times/ki->thread_count etc at whatever the caller's
 * calloc'd kinfo[] array already zeroed them to (ps.c's main loop never
 * checks get_task_info()'s return value -- it just prints whatever ended
 * up in the KINFO struct). This file reproduces exactly that real,
 * designed failure path -- honestly, not a fabricated success -- so ps
 * degrades the same way it would on real hardware if task_for_pid()
 * genuinely failed (e.g. sandboxed/non-root ps against another user's
 * process): PID/PPID/UID/TTY/STAT/COMMAND/ARGS (all real, sourced from
 * the real KERN_PROC sysctl in ps.c itself) print correctly; CPU%/RSS/
 * VSZ/thread-level columns print as real, honest zeros instead of
 * fabricated data.
 *
 * mach_state_order() (real tasks.c, used by print.c's thread-state
 * column, itself only reached when print_all_thread is set -- which
 * requires ki->thread_count > 0, which only real get_task_info()/
 * task_threads() ever sets, so this is provably unreachable on this
 * target, same "stub the symbol so it links, document why it can't
 * fire" pattern as libclosure/libmalloc's own dead-but-linked stand-ins)
 * is stubbed for link completeness only. */
/* Real ps.c's own include order (sys/time.h/sys/resource.h before ps.h)
 * -- ps.h itself assumes these are already visible (struct timeval/
 * struct rusage/u_int/fixpt_t/time_t), same as every real .c file in
 * this directory that includes it. */
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include "../../third_party/adv_cmds/ps/ps.h"

#define STATE_MAX 7 /* real tasks.c's own #define, line 28 */

int get_task_info(KINFO *ki) {
    ki->state = STATE_MAX;
    ki->invalid_tinfo = 1;
    ki->invalid_thinfo = 1;
    return 1;
}

int mach_state_order(int s, long sleep_time) {
    (void)s;
    (void)sleep_time;
    return STATE_MAX;
}

/* Real, faithfully-reproduced data table -- not Mach IPC, just process-
 * state-index-to-character labels -- from real tasks.c:46
 * (`char mach_state_table[] = " RUSITH?";`). Every ki->state this build
 * ever actually sets is STATE_MAX (index 7, '?') via get_task_info()
 * above (real tasks.c's own get_task_info() would set real values 0-6
 * here if real Mach IPC were available -- see this file's top comment),
 * so only the array needs to exist and be correctly sized for
 * print.c's real indexing to stay in-bounds; the specific '?' entry is
 * what will actually be read on this target. */
char mach_state_table[] = " RUSITH?";
