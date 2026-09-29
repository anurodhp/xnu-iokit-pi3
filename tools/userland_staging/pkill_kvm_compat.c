/* DAR-343: kvm_openfiles()/kvm_getprocs()/kvm_getargv()/kvm_geterr() for
 * real adv_cmds/pkill/pkill.c's FreeBSD code path -- see
 * pkill_compat_include/pkill_kvm.h for why that path is compiled.
 *
 * Process list: sysctl({CTL_KERN, KERN_PROC, KERN_PROC_ALL|KERN_PROC_PID})
 * -> array of Darwin struct kinfo_proc, the same call killall.c and ps.c
 * make (build_simple_cmds.sh's killall entry, build_ps.sh's header).
 * Field mapping is Darwin's own <sys/sysctl.h> extern_proc/eproc layout.
 *
 * Arguments: sysctl({CTL_KERN, KERN_PROCARGS2, pid}) returns
 * [int argc][exec_path\0][NUL padding][argv[0]\0 ... argv[argc-1]\0][env...]
 * (layout as consumed by ps.c's own argument printing, adv_cmds/ps/print.c).
 *
 * Unsupported on Darwin, hence constant: ki_jid (0), ki_loginclass ("").
 * ki_sid comes from getsid(2). It cannot be lazier than once per
 * kvm_getprocs(): pkill.c reads kp->ki_sid as a plain field (line 663),
 * with no hook to intercept, and XNU offers no sid in kinfo_proc nor a
 * KERN_PROC_SESSION (sysctl_prochandle, bsd/kern/kern_sysctl.c:825-852).
 * It is cheap and safe to do eagerly: getsid (bsd/kern/kern_prot.c:276-300)
 * does one proc_find and no permission check, so it never fails for a
 * live pid and returns ESRCH (-> -1) only for one that just exited. The
 * pid==0 special case matters: getsid(0) means "the caller"
 * (kern_prot.c:283), so pid 0 (kernproc) must not be asked.
 */
#include <sys/types.h>
#include <sys/sysctl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

struct pk_kvm {
	char	errbuf[KVM_ERRBUFLEN];
	struct pk_kinfo_proc *procs;
	char	*argbuf;
	char	**argv;
};
static struct pk_kvm the_kvm;

kvm_t *
kvm_openfiles(const char *execf, const char *coref, char *swapf, int flags,
    char *errbuf)
{
	(void)execf; (void)coref; (void)swapf; (void)flags;
	if (errbuf != NULL)
		errbuf[0] = '\0';
	return (&the_kvm);
}

char *
kvm_geterr(kvm_t *kd)
{
	return (kd->errbuf);
}

struct pk_kinfo_proc *
kvm_getprocs(kvm_t *kd, int op, int arg, int *cnt)
{
	int mib[4] = { CTL_KERN, KERN_PROC, op, arg };
	u_int miblen = (op == KERN_PROC_ALL) ? 3 : 4;
	darwin_kinfo_proc *dp = NULL;
	size_t len = 0;
	int tries, n, i;

	/* Size, then fetch; the table can grow in between, so retry. */
	for (tries = 0; tries < 8; tries++) {
		if (sysctl(mib, miblen, NULL, &len, NULL, 0) < 0)
			goto fail;
		len += len / 8 + 4 * sizeof(*dp);
		free(dp);
		if ((dp = malloc(len)) == NULL)
			goto fail;
		if (sysctl(mib, miblen, dp, &len, NULL, 0) == 0)
			break;
		if (errno != ENOMEM)
			goto fail;
	}
	if (tries == 8)
		goto fail;

	n = len / sizeof(*dp);
	free(kd->procs);
	if ((kd->procs = calloc(n > 0 ? n : 1, sizeof(*kd->procs))) == NULL)
		goto fail;
	for (i = 0; i < n; i++) {
		struct pk_kinfo_proc *k = &kd->procs[i];
		const darwin_kinfo_proc *d = &dp[i];

		k->ki_pid = d->kp_proc.p_pid;
		k->ki_ppid = d->kp_eproc.e_ppid;
		k->ki_pgid = d->kp_eproc.e_pgid;
		k->ki_sid = (k->ki_pid == 0) ? 0 : getsid(k->ki_pid);
		k->ki_uid = d->kp_eproc.e_ucred.cr_uid;
		k->ki_ruid = d->kp_eproc.e_pcred.p_ruid;
		k->ki_rgid = d->kp_eproc.e_pcred.p_rgid;
		k->ki_tdev = d->kp_eproc.e_tdev;
		k->ki_flag = d->kp_proc.p_flag;
		k->ki_start = d->kp_proc.p_starttime;
		strlcpy(k->ki_comm, d->kp_proc.p_comm, sizeof(k->ki_comm));
	}
	free(dp);
	*cnt = n;
	return (kd->procs);
fail:
	snprintf(kd->errbuf, sizeof(kd->errbuf), "%s", strerror(errno));
	free(dp);
	return (NULL);
}

char **
kvm_getargv(kvm_t *kd, const struct pk_kinfo_proc *kp, int nchr)
{
	int mib[3] = { CTL_KERN, KERN_PROCARGS2, kp->ki_pid };
	size_t len = 0, argmax;
	int argc, i;
	char *p, *end;

	(void)nchr;
	free(kd->argbuf);
	free(kd->argv);
	kd->argbuf = NULL;
	kd->argv = NULL;

	argmax = (size_t)sysconf(_SC_ARG_MAX);
	if ((kd->argbuf = malloc(argmax + 1)) == NULL)
		return (NULL);
	len = argmax;
	if (sysctl(mib, 3, kd->argbuf, &len, NULL, 0) < 0 ||
	    len < sizeof(argc))
		return (NULL);	/* exited, or not ours to read */
	kd->argbuf[len] = '\0';
	memcpy(&argc, kd->argbuf, sizeof(argc));
	p = kd->argbuf + sizeof(argc);
	end = kd->argbuf + len;
	while (p < end && *p != '\0')		/* skip exec_path */
		p++;
	while (p < end && *p == '\0')		/* skip padding NULs */
		p++;
	if (argc < 0 || (kd->argv = calloc(argc + 1, sizeof(char *))) == NULL)
		return (NULL);
	for (i = 0; i < argc && p < end; i++) {
		kd->argv[i] = p;
		p += strlen(p) + 1;
	}
	kd->argv[i] = NULL;
	return (kd->argv);
}
