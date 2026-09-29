/* DAR-343: kvm(3)-shaped view of the Darwin process table for pkill.
 *
 * Real Apple adv_cmds/pkill/pkill.c (third_party/adv_cmds, adv_cmds-176)
 * has two complete code paths. Its `#ifdef __APPLE__` path pulls process
 * data from sysmon_request_create()/xpc_*() -- private Apple libsysmon +
 * libxpc + libdispatch blocks, none of which this project ships (pkill.c
 * lines 71-76 include <xpc/xpc.h>, <sysmon.h>, <os/assumes.h>). Its
 * `#ifndef __APPLE__` path is the FreeBSD one: kvm_openfiles()/
 * kvm_getprocs()/kvm_getargv() returning `struct kinfo_proc` with ki_*
 * fields (pkill.c lines 452-466, 528). We compile the second path,
 * unmodified, and supply those four kvm calls and the ki_* struct here,
 * backed by sysctl(KERN_PROC)/KERN_PROCARGS2 (pkill_kvm_compat.c) -- the
 * same sysctl route killall and ps already use.
 *
 * pkill_prefix.h includes this AFTER the SDK headers, so the `kinfo_proc`
 * macro below only redirects pkill.c's own uses of the name; Darwin's real
 * struct stays reachable as darwin_kinfo_proc.
 */
#ifndef _PKILL_KVM_H_
#define _PKILL_KVM_H_
#include <sys/types.h>
#include <sys/time.h>
#include <sys/param.h>

struct pk_kinfo_proc {
	pid_t	ki_pid, ki_ppid, ki_pgid, ki_sid;
	uid_t	ki_uid, ki_ruid;
	gid_t	ki_rgid;
	dev_t	ki_tdev;
	int	ki_flag;
	int	ki_jid;			/* no jails on Darwin: always 0 */
	struct timeval ki_start;
	char	ki_comm[MAXCOMLEN + 1];
	char	ki_loginclass[2];	/* no login classes: always "" */
};
#define kinfo_proc pk_kinfo_proc

typedef struct pk_kvm pk_kvm_t;
#define kvm_t pk_kvm_t
#define KVM_ERRBUFLEN 256

kvm_t *kvm_openfiles(const char *, const char *, char *, int, char *);
struct pk_kinfo_proc *kvm_getprocs(kvm_t *, int, int, int *);
char **kvm_getargv(kvm_t *, const struct pk_kinfo_proc *, int);
char *kvm_geterr(kvm_t *);
#endif
