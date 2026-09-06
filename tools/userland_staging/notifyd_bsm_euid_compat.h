#ifndef _NOTIFYD_BSM_EUID_COMPAT_H_
#define _NOTIFYD_BSM_EUID_COMPAT_H_
/* Real, well-known, public Apple macro from <bsm/audit.h>/<bsm/libbsm.h> --
 * same real gap class as configd_bsm_pid_compat.h's own
 * audit_token_to_pid() (that header's own comment has the full real-value
 * citation; not duplicated here). Real value: the audit token's effective
 * uid slot is val[1] (matches the kernel's own real assignment,
 * audit_token.val[1] = euid, same real openbsm/bsm_wrappers.c layout
 * configd_bsm_pid_compat.h's val[5]=pid already documents -- val[0]=auid,
 * val[1]=euid, val[2]=egid, val[3]=ruid, val[4]=rgid, val[5]=pid,
 * val[6]=asid, val[7]=tid.port).
 *
 * -include'd by build_notifyd.sh: notify_proc.c/pathwatch.c both call
 * audit_token_to_euid()/audit_token_to_egid() directly (real per-caller
 * entitlement/ownership checks), two real BSM accessors build_configd.sh's
 * own pass never needed.
 */
#define audit_token_to_euid(atoken) ((atoken).val[1])
#define audit_token_to_egid(atoken) ((atoken).val[2])
#endif /* _NOTIFYD_BSM_EUID_COMPAT_H_ */
