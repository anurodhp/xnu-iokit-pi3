# Backlog

**Migrated to Linear, 2026-08-31.** Every open bug this file tracked (all
P0/P1/P2/P3 numbered items and all `d`-prefixed userland-library-debt
items) now has its own individually-detailed Linear issue in the **Darwin**
project (team **DAR**), each with a `bug` label, a native priority
(P0→Urgent, P1→High, P2→Medium, P3→Low), and at least one system-area
label (`kernel`, `storage`, `usb`, `network`, `crypto`, `launchd`, `dyld`,
`pid1-init`, `static-linking`, `daemon`, `userland-libc`,
`userland-commands`, `iokit`, `auth`, `duplicate-symbols`). Search Linear
for these instead of this file going forward.

Real dylib-vs-dylib and binary-vs-loaded-dylib duplicate-symbol counts
(from `tools/userland_staging/dup_symbol_audit.sh --summary`, as of
2026-08-30) also each got their own Linear issue — one per shipped dylib
with a nonzero duplicate count, one per shipped binary with a nonzero
shadow count.

The full verbatim final content of this file (everything that was here
before migration) is archived in `docs/historic/backlog.md`'s "Fifth
archive pass, 2026-08-31" section.

**Not migrated, and not covered by this pointer**: `docs/static-linking-cleanup-plan.md`
is a separate, actively-maintained document (a concurrent effort owns it)
— it was deliberately left untouched by this migration except for a
handful of its own "Consciously deferred, not silently skipped" items,
which were filed to Linear as their own tracked issues without editing
that source file. Check that file directly for current static-linking
work in progress.
