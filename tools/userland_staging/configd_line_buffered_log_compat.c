/* DAR-170: make configd's own diagnostic log actually usable.
 *
 * THE PROBLEM (real, cost a whole boot to notice)
 * ===============================================
 * This project deliberately runs configd with its own real `-d` flag and
 * points StandardOutPath/StandardErrorPath at /var/log/configd.log
 * (tools/init_binary/inject_into_sd_image.sh -- see its own comment for why:
 * every SC_log() goes through os_log(3), which os_log_compat.c honestly
 * no-ops, so `-d`'s kSCLogDestinationFile printf path is the ONLY way
 * anything configd says is visible on this target).
 *
 * But stdout to a *file* is block-buffered by C99 default. A process's
 * printf output therefore sits in its stdio buffer until the process exits.
 * configd is a daemon: the instance that is actually working never exits, so
 * its entire log is permanently invisible, while the short-lived instances
 * that die immediately (DAR-270's bootstrap_check_in losers, which print one
 * line and exit(EX_UNAVAILABLE)) flush theirs and dominate the file.
 *
 * Concretely: /var/log/configd.log read back after a real boot showed only
 * loser instances plus previously-exited winners. The live, resident configd
 * -- the one every DHCP question is about -- contributed nothing at all, so
 * "IPConfiguration logged no errors" could not be distinguished from
 * "IPConfiguration's log is still in a buffer". Both readings were available
 * for the same evidence, which makes the log worse than useless.
 *
 * THE FIX
 * =======
 * One constructor, setting line buffering on stdout and stderr before main().
 * After this, every SC_log() line reaches the file as it is produced, from
 * the resident instance too.
 *
 * WHY IT LIVES HERE AND NOT IN configd.m
 * ======================================
 * configd.m is real, unmodified, pinned Apple source in a shared
 * third_party/ checkout (gitignored, one clone shared by every worktree on
 * this machine). Patching it would mean touching third_party-patches/
 * configd.patch and re-applying it to a tree other agents are using
 * concurrently. This file is compiled into configd.macho by
 * build_configd.sh's own FILES list instead: same effect, zero blast radius,
 * and it is honestly this project's own diagnostic-plumbing choice rather
 * than a claim about what Apple's configd does.
 *
 * This is not a behavior change to configd's logic. Line- vs block-buffering
 * changes only when bytes reach the fd, and real Apple configd run with `-d`
 * from a terminal is line-buffered anyway (a tty is line-buffered by
 * default) -- this restores that same behavior when the destination happens
 * to be a file.
 */
#include <stdio.h>

__attribute__((constructor))
static void
configd_line_buffer_stdio(void)
{
	(void)setvbuf(stdout, NULL, _IOLBF, 0);
	(void)setvbuf(stderr, NULL, _IOLBF, 0);
}
