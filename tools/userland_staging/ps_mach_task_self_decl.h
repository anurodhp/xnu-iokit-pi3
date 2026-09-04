#ifndef _PS_MACH_TASK_SELF_DECL_H_
#define _PS_MACH_TASK_SELF_DECL_H_
/* DAR-143: real adv_cmds/ps/tasks.c calls the mach_task_self() MACRO
 * (mach/mach_init.h: `#define mach_task_self() mach_task_self_`), not
 * the raw `mach_task_self_` global this project's other Mach-IPC code
 * (mach_msg_real.c/mach_ipc_clock_init.c) already uses directly to avoid
 * this exact problem. Root cause: build_ps.sh's `-I $XNU/osfmk` makes
 * `#include <mach/mach.h>` (pulled in by ps.h) resolve against XNU's own
 * kernel-side osfmk/mach/mach.h (a real but much more minimal file that
 * does NOT include mach_init.h) instead of the iPhoneOS SDK's real
 * user-facing mach/mach.h (which does) -- confirmed via `clang -E`, not
 * guessed. tasks.c itself never includes mach/mach_init.h directly (nor
 * does ps.h), so the macro is otherwise never declared. This is the same
 * class of fix as mach_reply_port_decl.h/mach_msg_trap_decl.h: declare
 * exactly the real, correct symbol/macro this one file needs rather than
 * fighting the XNU-vs-SDK header shadowing. `mach_task_self_` itself is
 * real, already defined by third_party/xnu-7195/libsyscall/mach/
 * mach_init.c (already linked into libc_build/libmalloc.a). */
#include <mach/mach_types.h>
extern mach_port_t mach_task_self_;
#define mach_task_self() mach_task_self_
#endif
