# System daemon roadmap

**Migrated to Linear, 2026-08-31.** This file's real remaining work (the
launchd PID-1 boot-path follow-ons — forked children not reaching `exec`,
the real `login(1)` port, wiring the real getty→login→PAM→shell chain,
`tools/init_binary/init.c`'s disposition decision; `notifyd`; `cron`'s
unresolved startup SIGSEGV; `ntpd`; and all of Phase 10's Tier 2/3
daemons — `configd`, `DiskArbitration`, `mDNSResponder`, `PowerManagement`)
now each have their own individually-detailed Linear issue in the
**Darwin** project (team **DAR**), with a `bug` label, a native priority,
and at least one system-area label (`launchd`, `daemon`, `dyld`,
`pid1-init`, `auth`, `network`, `storage`). Search Linear for these
instead of this file going forward.

Most of this document's content (Phases A-D's real dyld port, the full
static-to-dylib migration steps 1-5, Phase 6.6/6.7/6.8's critical-path
gap-closing passes, and Phase 9's CoreFoundation port) was already marked
DONE/CLOSED at the time of migration — real, valuable debugging/decision
history, not new work, so it was archived rather than re-filed.

The full verbatim final content of this file is archived in
`docs/historic/daemon-roadmap.md`.
