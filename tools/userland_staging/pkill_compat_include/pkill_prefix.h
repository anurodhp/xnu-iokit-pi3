/* DAR-343: forced-include prefix (-include) for building real
 * adv_cmds/pkill/pkill.c's FreeBSD (`#ifndef __APPLE__`) code path on
 * Darwin. See pkill_kvm.h for why that path, not the sysmon one.
 *
 * Order matters: (1) pull in every SDK header pkill.c includes while
 * __APPLE__ is still defined, so the SDK headers configure themselves
 * normally and their include guards then make pkill.c's own #includes
 * no-ops; (2) install the kvm/ki_* shim; (3) only then #undef __APPLE__ so
 * pkill.c's preprocessor conditionals select the FreeBSD branches.
 * Nothing in pkill.c re-includes an SDK header that isn't in this list
 * (its include block, lines 35-75, is exactly this set plus kvm.h/jail.h,
 * provided by this directory).
 */
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/user.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <paths.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <regex.h>
#include <ctype.h>
#include <fcntl.h>
#include <err.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <locale.h>

typedef struct kinfo_proc darwin_kinfo_proc;
#include "pkill_kvm.h"

/* FreeBSD's P_KPROC (kernel process) has no Darwin twin; P_SYSTEM is the
 * flag Darwin's proc.h gives kernel_task and other system procs, which is
 * what pkill.c's PSKIP() wants to skip (line 92-93; the Apple branch's
 * equivalent is PROC_FLAG_SYSTEM, line 89). */
#ifndef P_KPROC
#define P_KPROC P_SYSTEM
#endif

#undef __APPLE__
