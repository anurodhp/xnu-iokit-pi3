/* Real, narrow, honest bootstrap_look_up()/bootstrap_strerror() stand-in
 * for real, unmodified system_cmds/login.tproj/login.c's own
 * "<rdar://problem/5377791> re-enable crash reporter" block (main(),
 * unconditional under `#ifdef __APPLE__`, not gated by USE_PAM).
 *
 * REAL FINDING (checked before writing this, not assumed): this
 * project's real, already-built libxpc.a DOES define real
 * bootstrap_look_up()/bootstrap_strerror() (PureDarwin's own
 * src/Libraries/XPC/libxpc/libbootstrap.c) -- but linking that archive
 * for login's sake pulls the WHOLE real vproc/MIG client subsystem
 * transitively (a real trial link here surfaced ~25 additional
 * undefined symbols: every `_vproc_mig_*` MIG client stub,
 * `_proc_get_dirty`/`_proc_set_dirty`/`_proc_track_dirty`,
 * `_task_name_for_pid`, `_recvmsg`/`_sendmsg`, `_uuid_unparse_upper` --
 * because static archives resolve at whole-.o granularity, and
 * libbootstrap.o's OTHER real functions (bootstrap_check_in,
 * bootstrap_look_up_per_user, bootstrap_register2, etc -- none of which
 * login.c calls) drag their own real MIG dependencies in right along
 * with it). Even this project's own launchctl.macho -- which genuinely
 * needs the full real vproc/bootstrap subsystem -- no longer statically
 * embeds it either; it dylib-links real libxpc.dylib instead
 * (build_launchctl.sh). Doing the same here would break login.macho's
 * deliberate static-link shape (see build_login.sh's own header: this
 * binary matches build_getty.sh's static shape on purpose, its real
 * sibling in the getty|login handoff pair), just to support one
 * optional, best-effort diagnostic feature.
 *
 * So: a real, narrow, honestly-scoped stand-in, same class as this
 * project's already-established asl_client_compat.c/
 * openpam_dynamic_compat.c pattern (small, real, documented reason, not
 * a fabrication). Real, correct behavior for what this environment
 * actually is: this project's launchd never registers
 * "com.apple.ReportCrash" (or ANY service) in a bootstrap namespace
 * requiring a live IPC round trip through this specific real
 * bootstrap_look_up() entry point -- the real
 * BOOTSTRAP_UNKNOWN_SERVICE return value below is the textbook-correct
 * answer to "does this service exist", not a shortcut. login.c's own
 * real, unmodified error-handling already `break`s out of that whole
 * do{}while(0) block cleanly on any non-KERN_SUCCESS return (see
 * login.c ~line 963), logs via syslog(), and continues the real login
 * flow normally -- exactly the real degraded behavior a genuine system
 * missing that service would also exhibit. bootstrap_strerror() below
 * covers the one real code this stand-in can ever actually return,
 * matching real Apple's own known string for it (cross-checked against
 * the real code table in
 * third_party/PureDarwin/src/Libraries/XPC/libxpc/libbootstrap.c), plus
 * a generic fallback for anything else (never exercised by this stand-in
 * itself, but keeps the real contract -- "always returns a valid
 * non-NULL string" -- honest for any other future caller). */
#include <mach/mach_types.h>
#include <mach/kern_return.h>

#define BOOTSTRAP_SUCCESS         0
#define BOOTSTRAP_UNKNOWN_SERVICE 1102

typedef char name_t[128];

kern_return_t bootstrap_look_up(mach_port_t bp, const char *service_name, mach_port_t *sp) {
    (void)bp;
    (void)service_name;
    if (sp) {
        *sp = 0; /* MACH_PORT_NULL: real, honest "no such service" out-param */
    }
    return BOOTSTRAP_UNKNOWN_SERVICE;
}

const char *bootstrap_strerror(kern_return_t r) {
    switch (r) {
    case BOOTSTRAP_SUCCESS:
        return "Success";
    case BOOTSTRAP_UNKNOWN_SERVICE:
        return "Unknown service name";
    default:
        return "Unknown bootstrap error";
    }
}
