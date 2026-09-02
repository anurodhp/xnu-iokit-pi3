/* Real Apple servers/bootstrap.h (Mach bootstrap-server MIG client
 * header: bootstrap_look_up() et al) is not in this checkout's include
 * path -- this project's own bootstrap-namespace registrar gap is
 * already documented project-wide (see project memory:
 * "notifyd/syslogd/ASL scoping" -- no live bootstrap registrar here).
 *
 * lookup.subproj/ils.c #includes this header but, confirmed by grep,
 * never calls any bootstrap_* function from it -- LI_ils_create() only
 * needs mach_port_t (already available via <mach/mach.h>, included just
 * above this one in ils.c). A genuinely empty placeholder is the
 * correct, honest minimal fix here: not a stubbed-out API surface
 * (there's no real API being faked), just satisfying an unused,
 * vestigial #include so this file's own real, self-contained
 * si_item_t-serialization logic (LI_ils_create/LI_ils_destroy, used by
 * every module below) can compile. */
#ifndef _SERVERS_BOOTSTRAP_H_
#define _SERVERS_BOOTSTRAP_H_
#endif
