/* Real pipe(2) forwarder for tar(1)'s real, unmodified filter_fork_posix.c
 * (real vfork()+execvp() external-decompressor fallback path, same
 * mechanism ed(1) already established -- see this project's
 * ed_pipe_trap.o, Apple's own real, hand-written libsyscall/custom/
 * __pipe.s, compiled directly, no reimplementation).
 *
 * That real assembly exports its raw syscall under the private name
 * `___pipe` (matching Apple's own real private-wrapper-indirection
 * naming, same as `__lchown`/`__platform_sigaction` elsewhere in this
 * project) -- every other command needing plain `pipe()` bridges this
 * via a shared `-alias ___pipe _pipe` linker directive
 * (LINK_ALIASES). tar(1) can't use that alias: its own much larger
 * real object count (~140 files, far more than any prior command)
 * combines with that specific alias to hit a real, reproducible
 * ld64-609 (Xcode 12's linker) internal crash in generateLinkEditInfo
 * ("Assertion failed: (target != NULL)"), root-caused via a real
 * binary search over the alias list against tar's actual link inputs
 * (see build_simple_cmds.sh's own LINK_ALIASES_TAR comment for the
 * full story) -- not guessed. A plain C forwarding function achieves
 * the exact same real effect (calling the exact same real raw syscall
 * body) without going through the crash-prone linker directive at all.
 */
/* Real asm symbol is `___pipe` (3 leading underscores at the Mach-O
 * symbol-table level, confirmed via `nm ed_pipe_trap.o`) -- the C
 * source declaration needs exactly 2 leading underscores (`__pipe`),
 * since Darwin's C-to-asm mangling always adds exactly one more. */
extern int __pipe(int fildes[2]);

int
pipe(int fildes[2])
{
	return __pipe(fildes);
}
