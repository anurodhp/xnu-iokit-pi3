/* Real struct kevent_qos_s / kqueue_id_t, verbatim from
 * third_party/xnu-7195/bsd/sys/event.h:138-153 (the kernel-internal
 * copy) -- confirmed absent from this project's pinned Xcode-12
 * iPhoneOS14.4 SDK's own <sys/event.h> even under -DPRIVATE=1 (grepped
 * directly, zero mentions of "kevent_qos_s" anywhere in that file).
 * Same "PRIVATE-gated, stripped from this project's exported userspace
 * headers" gap already documented in docs/daemon-roadmap.md's real
 * proc_pidinfo/pthread_setname_np extraction (which deliberately avoided
 * this by not including <sys/proc_info.h> at all) -- daemon.c genuinely
 * does `#include <libproc.h>` -> `<sys/proc_info.h>` -> needs the real
 * struct by value (struct kevent_extinfo's kqext_kev field), so this
 * time it can't be avoided, only supplied for real. -include'd before
 * <sys/proc_info.h> is reached so its own #ifdef PRIVATE block sees a
 * complete type.
 */
#ifndef _SYS_KEVENT_QOS_COMPAT_H_
#define _SYS_KEVENT_QOS_COMPAT_H_

#include <stdint.h>

#ifndef _KEVENT_QOS_S_DECLARED
#define _KEVENT_QOS_S_DECLARED
struct kevent_qos_s {
	uint64_t        ident;
	int16_t         filter;
	uint16_t        flags;
	int32_t         qos;
	uint64_t        udata;
	uint32_t        fflags;
	uint32_t        xflags;
	int64_t         data;
	uint64_t        ext[4];
};

typedef uint64_t kqueue_id_t;
#endif /* _KEVENT_QOS_S_DECLARED */

#endif /* _SYS_KEVENT_QOS_COMPAT_H_ */
