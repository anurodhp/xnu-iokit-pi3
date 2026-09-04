#ifndef _CONFIGD_BSM_PID_COMPAT_H_
#define _CONFIGD_BSM_PID_COMPAT_H_
/* Real, well-known, public Apple macro from <bsm/audit.h>/<bsm/libbsm.h> --
 * confirmed via WebSearch (openbsm/libbsm sources + Apple documentation
 * cross-references), NOT present in this project's pinned third_party/BSM
 * or third_party/xnu-7195 (grepped both trees, zero hits for
 * "audit_token_to_pid" anywhere) even though audit_token_t itself IS real,
 * already-pinned (osfmk/mach/message.h's 8-element val[] struct) and
 * audit_token_to_au32() IS real, present, pinned (third_party/BSM/bsm/lib/
 * libbsm.h). Real value: the audit token's pid slot is val[5] (matches the
 * kernel's own real assignment, audit_token.val[5] = p->p_pid, confirmed
 * via WebSearch against openbsm's bsm_wrappers.c and multiple independent
 * public writeups of the exact same layout) -- this is the one real macro
 * missing from this pinned tag's copy of the header family, not a
 * fabricated value.
 *
 * -include'd by build_configd.sh, same "-include a tiny compat header"
 * technique build_syslogd.sh already established for sys/kevent_qos_compat.h.
 */
#define audit_token_to_pid(atoken) ((atoken).val[5])
#endif /* _CONFIGD_BSM_PID_COMPAT_H_ */
