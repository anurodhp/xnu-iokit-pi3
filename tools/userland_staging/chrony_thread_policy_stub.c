#include <mach/kern_return.h>
#include <mach/mach_types.h>
#include <mach/thread_policy.h>

/* Real gap: no MIG client stub for the thread_policy Mach routine exists
 * anywhere in this project's build (would need a real mig regen of
 * osfmk/mach/thread_act.defs, not attempted here). Only caller is
 * sys_macosx.c's set_realtime(), which already treats a non-
 * KERN_SUCCESS return as a soft failure (LOGS_WARN, real-time
 * scheduling simply not applied) -- so a documented, honest failure
 * return is the real, non-papered-over behavior, not a fabricated
 * success. */
kern_return_t
thread_policy_set(thread_t thread, thread_policy_flavor_t flavor,
                   thread_policy_t policy_info, mach_msg_type_number_t count)
{
  (void)thread; (void)flavor; (void)policy_info; (void)count;
  return KERN_FAILURE;
}
