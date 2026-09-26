/*
 * reboot3() for system_cmds' reboot(8)/halt(8)/shutdown(8) (DAR-334).
 *
 * Apple's reboot.c hands the reboot to launchd and exits
 * (third_party/system_cmds/reboot.tproj/reboot.c:262-264: "launchd(8)
 * handles reboot. This call returns NULL on success.";
 * shutdown.tproj/shutdown.c:498 likewise). reboot3() belongs to macOS 11's
 * launchd, which is not open source. This project's PID 1 is PureDarwin's
 * launchd, whose client call for the same request is the older reboot2():
 *
 *   - declared  PureDarwin/src/Libraries/XPC/launchd/reboot2.h:35,
 *               void *reboot2(uint64_t flags);
 *   - defined   PureDarwin/src/Libraries/XPC/libxpc/libvproc.c:918-929:
 *               vproc_mig_reboot2() to the root bootstrap, returning NULL
 *               on success and non-NULL on failure;
 *   - handled   PureDarwin/src/Libraries/XPC/launchd/core.c:8853-8903
 *               job_mig_reboot2(): root only, sets root_jobmgr->reboot_flags
 *               to the caller's RB_* flags and calls launchd_shutdown(),
 *               which stops every job and then calls reboot(2) with those
 *               flags (core.c:1386-1388).
 *
 * Both take the same RB_* howto flags. reboot3() reports success as 0, so
 * that is the only translation done here.
 */
#include <stddef.h>
#include <stdint.h>

extern void *reboot2(uint64_t flags);

int reboot3(uint64_t howto);

int
reboot3(uint64_t howto)
{
	return (reboot2(howto) == NULL) ? 0 : -1;
}
