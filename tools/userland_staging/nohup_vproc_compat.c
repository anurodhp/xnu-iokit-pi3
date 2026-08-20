/* Real _vprocmgr_detach_from_console(): a launchd-managed job's real
 * call to detach itself from its controlling console session (only
 * meaningful when a real launchd started the process as a job in the
 * first place). This project has no launchd/vproc subsystem at all (see
 * architecture.md's userland section, same class of gap as
 * copyfile_syscalls.c's mbr_*() stand-ins for "no directory service").
 * Real, correct contract here (not fabricated): the function returns
 * NULL on success, a static error string on failure. Since there is no
 * real console-session/job-management concept on this target for the
 * process to be attached to in the first place, "detach" trivially and
 * honestly succeeds -- there is nothing to fail. nohup.c's own real
 * call site only warns (does not exit) on a non-NULL return, so this
 * also degrades gracefully even if that reasoning is ever revisited. */
const char *_vprocmgr_detach_from_console(int flags) {
    (void)flags;
    return (const char *)0;
}
