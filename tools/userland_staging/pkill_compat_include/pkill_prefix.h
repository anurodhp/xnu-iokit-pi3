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

/* pkill.c's FreeBSD path needs two names Darwin's SDK headers lack. Both
 * were confirmed absent from this project's Xcode-12 iPhoneOS SDK
 * (usr/include/sys/sysctl.h, sys/proc.h) and from
 * third_party/xnu-7195/bsd/sys/{sysctl.h,proc.h}.
 *
 * P_KTHREAD: used only by PSKIP() (pkill.c lines 86-87) to skip kernel
 * threads unless -S. XNU has no kernel-thread struct proc: the only
 * kernel "process" is kernproc, whose flag is set to exactly P_SYSTEM at
 * xnu-7195/bsd/kern/bsd_init.c:673 (`kernproc->p_flag = P_SYSTEM;`), and
 * P_SYSTEM is defined at bsd/sys/proc.h:173. The Apple branch of the same
 * macro skips PROC_FLAG_SYSTEM (pkill.c lines 80-83), i.e. the same
 * "system process" notion, so P_SYSTEM is the faithful mapping.
 *
 * KERN_PROC_PROC: used at pkill.c line 419. FreeBSD's KERN_PROC_PROC is
 * "processes only, no kernel threads" (see pkill.c's comment at lines
 * 416-418). XNU's kern.proc sysctl handler (bsd/kern/kern_sysctl.c
 * sysctl_prochandle, lines 791-891) only knows ALL/PID/PGRP/TTY/UID/RUID
 * and returns ENOTSUP for anything else (lines 849-851); its ALL case
 * walks proc_iterate(PROC_ALLPROCLIST|PROC_ZOMBPROCLIST) (lines 869-870),
 * which holds only real processes (plus kernproc, filtered by P_SYSTEM
 * above) -- there are no per-thread entries to exclude, so ALL is
 * equivalent. KERN_PROC_ALL is defined at bsd/sys/sysctl.h:720 (SDK
 * sys/sysctl.h:429). */
#ifndef P_KTHREAD
#define P_KTHREAD P_SYSTEM
#endif
#ifndef KERN_PROC_PROC
#define KERN_PROC_PROC KERN_PROC_ALL
#endif

#undef __APPLE__
