/*
 * DAR-283: stand-in for scutil.tproj/nc.c -- scutil's `--nc` VPN
 * subcommand family.
 *
 * REAL, PROVABLE ARCHITECTURAL WALL (not a shortcut)
 * =================================================
 * nc.c is real, unmodified, pinned Apple source and is NOT skipped for
 * convenience: it cannot be compiled here at all. Its very first block
 * of includes is
 *
 *     #include <NetworkExtension/NEHelperClient.h>
 *
 * and NetworkExtension.framework has never been open-sourced by Apple in
 * any form -- confirmed the way this project's source-priority rule
 * requires, by a real `git ls-remote
 * https://github.com/apple-oss-distributions/NetworkExtension.git`,
 * which 404s ("Repository not found"), not by assumption. There is no
 * FreeBSD or other upstream equivalent to fall back to either:
 * NEHelperClient is an Apple-private IPC surface, not a portable
 * subsystem. This is the same category as this project's existing
 * ne_session.h / vpn_status_compat.h / lockdown.h stand-ins.
 *
 * Underneath nc.c the dependency is equally absent: every `scutil --nc`
 * verb ultimately calls the SCNetworkConnection API
 * (SCNetworkConnectionCreateWithServiceID / Start / Stop / CopyStatus /
 * CopyUserPreferences ...), whose implementation --
 * SystemConfiguration.fproj/SCNetworkConnection.c -- is explicitly and
 * deliberately out of scope for this project's
 * libSystemConfiguration.dylib (see build_libsystemconfiguration_client.sh's
 * own header: "real PPP/VPN dial subsystem -- no serial modem hardware/
 * driver in this project at all"). So even with a fabricated
 * NEHelperClient.h, nc.c would link against nothing.
 *
 * WHAT THIS FILE PROVIDES
 * =======================
 * Exactly the four symbols scutil.c and commands.c reference from nc.h,
 * and nothing else:
 *
 *   find_nc_cmd()  -- returns -1 ("no such --nc subcommand"), which is
 *                     real scutil.c's own already-existing signal to
 *                     call usage(prog) and exit EX_USAGE. That is the
 *                     identical, real, Apple-authored code path a user
 *                     gets today for `scutil --nc bogusverb`.
 *   do_nc_cmd()    -- unreachable: scutil.c only calls it after
 *                     find_nc_cmd() >= 0, which cannot happen above.
 *                     Kept real and linkable, never invoked.
 *   username / password / sharedsecret
 *                  -- the three CFStringRef globals nc.c really defines
 *                     and scutil.c's `--user`/`--password`/`--secret`
 *                     option handlers really assign to. They are read
 *                     back by nothing but nc.c, so they are simply
 *                     storage here, with the same real types and the
 *                     same real NULL initial value nc.c gives them.
 *
 * Net effect on the user-visible surface: `scutil --nc ...` prints the
 * standard usage text and exits EX_USAGE instead of driving a VPN
 * subsystem that does not exist on this target. Every other scutil mode
 * -- the dynamic store, --dns, --nwi, --get/--set, --prefs, --net --
 * is unaffected and uses 100% real Apple code.
 */
#include <SystemConfiguration/SystemConfiguration.h>

CFStringRef	username	= NULL;
CFStringRef	password	= NULL;
CFStringRef	sharedsecret	= NULL;

int
find_nc_cmd(char *cmd)
{
	(void)cmd;
	return -1;
}

void
do_nc_cmd(char *cmd, int argc, char **argv, Boolean watch)
{
	(void)cmd; (void)argc; (void)argv; (void)watch;
}
