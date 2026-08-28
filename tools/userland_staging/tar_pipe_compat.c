/* Real pipe(2) forwarder, originally written for tar(1)'s real,
 * unmodified filter_fork_posix.c (real vfork()+execvp() external-
 * decompressor fallback path). Also linked by ed(1)/diff(1)/less(1)
 * (2026-08-28) -- their own ed_popen_system_compat.c/filter_fork_posix.c-
 * equivalent pipe() callers relied on the old static-linking era's
 * `-alias ___pipe _pipe` LINK_ALIASES directive (see below) for the
 * exact same real gap, which d9's dynamic-link conversion
 * (docs/backlog.md) dropped project-wide without anyone re-providing it
 * for THOSE three commands specifically -- a real regression that only
 * surfaced when `diff` (brand new, unblocked by the ld64-609/
 * NEWLD_BINDIR fix, docs/backlog.md item d9) became the first of the
 * four to actually launch under real chained-fixups dyld, which binds
 * every referenced symbol eagerly at process start regardless of
 * whether the pipe() call site is ever reached at runtime. Filename
 * kept as-is (already referenced by tar's own CMD_LIST entry); not
 * tar-specific despite the name.
 *
 * That real assembly (this project's own ed_pipe_trap.o, Apple's own
 * real, hand-written libsyscall/custom/__pipe.s, compiled directly, no
 * reimplementation -- ed/diff/less/tar all already link it) exports its
 * raw syscall under the private name `___pipe` (matching Apple's own
 * real private-wrapper-indirection naming, same as
 * `__lchown`/`__platform_sigaction` elsewhere in this project) --
 * historically, plain `pipe()` bridged this via a shared
 * `-alias ___pipe _pipe` linker directive (LINK_ALIASES, since removed
 * by d9's dynamic-link conversion). tar(1) never used that alias: its
 * own much larger real object count (~140 files, far more than any
 * prior command) combined with that specific alias to hit a real,
 * reproducible ld64-609 (Xcode 12's linker) internal crash in
 * generateLinkEditInfo ("Assertion failed: (target != NULL)"),
 * root-caused via a real binary search over the alias list against
 * tar's actual link inputs (see build_simple_cmds.sh's own
 * LINK_ALIASES_TAR history for the full story) -- not guessed. A plain
 * C forwarding function achieves the exact same real effect (calling
 * the exact same real raw syscall body) without going through any
 * linker alias directive at all, which is why it's now shared by all
 * four real pipe(2) consumers instead of just tar.
 *
 * Real asm symbol is `___pipe` (3 leading underscores at the Mach-O
 * symbol-table level, confirmed via `nm ed_pipe_trap.o`) -- the C
 * source declaration needs exactly 2 leading underscores (`__pipe`),
 * since Darwin's C-to-asm mangling always adds exactly one more. */
extern int __pipe(int fildes[2]);

int
pipe(int fildes[2])
{
	return __pipe(fildes);
}
