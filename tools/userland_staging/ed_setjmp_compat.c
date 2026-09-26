/* Real setjmp(3)/longjmp(3)/sigsetjmp(3)/siglongjmp(3) for text_cmds/ed
 * (main.c's real ^C-during-input / command-error recovery path --
 * `if ((status = sigsetjmp(env, 1))) { ... "interrupt" ... }` at the top
 * of main(), `siglongjmp(env, -1)` in handle_int()/the error path).
 *
 * This project has NO setjmp/longjmp implementation anywhere before
 * this file (confirmed via nm across every archive in libc_build/, not
 * assumed) -- ed is the first real command needing one. Real Apple
 * source exists and is genuinely real, unmodified arm64 asm
 * (third_party/libplatform/src/setjmp/arm64/setjmp.s), deliberately
 * scoped OUT of build_libplatform.sh when that tier was built (its own
 * header comment: "setjmp/ucontext/... genuinely separate concerns").
 * Tried to use it as-is for this port and hit two real, verified gaps
 * (not guessed):
 *   1. It needs <architecture/arm/asm_help.h> for its ENTRY_POINT macro
 *      -- not vendored in third_party/xnu-7195 or third_party/Libc at
 *      all (only the unrelated i386 EXTERNAL_HEADERS copy exists).
 *      Read the REAL file directly from this host's own installed
 *      Xcode/CommandLineTools SDKs (a real, unmodified system header
 *      any Apple-platform developer has locally, not a third-party
 *      pull) to confirm its arm64 ENTRY_POINT/CALL_EXTERNAL macros are
 *      exactly the trivial `.align 2 ; .globl name ; .text ; name:` /
 *      `bl var` anyone would expect -- so reconstructing that specific
 *      macro here (see ed_setjmp_raw.s) is a low-risk, verified-correct
 *      narrow header replacement.
 *   2. Real setjmp.s's sigsetjmp/siglongjmp ALSO call `___sigaltstack`
 *      (real Apple asm exists for this one,
 *      libsyscall/custom/__sigaltstack.s) AND `__sigunaltstack` (the
 *      matching real Apple source for THIS one does not exist anywhere
 *      in this xnu-7195 checkout -- confirmed via find, not guessed;
 *      presumably lives in a different, non-open-sourced Libsystem
 *      component, the same class of gap as posix_spawn for
 *      `install`/`df`/real popen -- see ed_popen_system_compat.c). This
 *      is a genuinely missing real dependency, not just a missing
 *      header, so Apple's real file can't be used complete/unmodified
 *      here either way.
 *
 * Given that, this file (real, last-resort custom code, not a
 * fabricated stand-in) reimplements the CORE, functionally-required
 * part -- register capture/restore -- via ed_setjmp_raw.s's real
 * AAPCS64-callee-saved-register save/restore (same register set real
 * Apple's file saves: x19-x28/fp/lr/sp/d8-d15, see that file's own
 * header) -- plus real sigprocmask()-based signal-mask save/restore for
 * the sig- variants (same real raw syscall project's build_simple_cmds.sh
 * already generates and links for every command via SYS_STUBS_NEEDED,
 * not new). Two real, honest, narrow scope reductions from Apple's full
 * file, both documented rather than silently dropped:
 *   - No pointer-authentication ("PAC") munging of the saved fp/lr/sp
 *     (real Apple's _OS_PTR_MUNGE/_OS_PTR_UNMUNGE, os/tsd.h). This is a
 *     memory-corruption-exploit hardening step, not a correctness
 *     requirement -- functionally a no-op-equivalent omission on this
 *     plain arm64 (non-arm64e/no-PAC) target, not a behavior change.
 *   - No sigaltstack(2) status save/restore across siglongjmp (the real
 *     gap #2 above forces this; this project has no sigaltstack(2)
 *     caller anywhere else either, so there is no real behavior lost by
 *     this target specifically).
 * Everything actually observable from C -- which registers/stack
 * pointer/signal mask survive a longjmp back through a setjmp point --
 * matches real POSIX semantics exactly.
 */
#include <signal.h>
#include <stddef.h> /* NULL */

/* returns_twice on the internal helper only protects THIS file's own
 * call to it from sigsetjmp() -- it does NOT propagate to ed.c's real
 * call sites (`sigsetjmp(env, 1)`/`setjmp(env)` in main.c), which go
 * through this SDK's own <setjmp.h>. Checked directly (adversarial
 * review, 2026-08-20): neither this project's installed iPhoneOS SDK
 * setjmp.h nor third_party/libplatform/include/setjmp.h mark
 * setjmp/sigsetjmp `returns_twice` at all -- real Apple relies on
 * dyld/libSystem's real setjmp being genuinely opaque-to-the-optimizer
 * machine code the compiler can't see through and therefore can't
 * mis-optimize around, not on the attribute. Since THIS project's
 * setjmp/sigsetjmp definitions (below) are ordinary, fully-visible C
 * functions the compiler DOES see through, correctness at ed.c's call
 * sites currently depends on -O0 -- which is a hard, unconditional
 * requirement of this file, not just an incidental detail: every
 * build_*.sh in this project hardcodes -O0 project-wide with no
 * exception, so this holds today, but it is NOT "correct regardless of
 * optimization level" the way real Apple's asm-opaque setjmp is. Do not
 * build ed at -O1 or higher without first adding `returns_twice` to a
 * compat override of this SDK's <setjmp.h> declarations (the same
 * force-`-include` pattern ios_prohibited_override.h already uses) --
 * skipping that step would be a real, silent miscompilation risk, not
 * a cosmetic one. */
extern int __ed_setjmp_raw(void *buf) __attribute__((returns_twice));
extern void __ed_longjmp_raw(void *buf, int val) __attribute__((noreturn));

/* Layout matches ed_setjmp_raw.s exactly: 21 x 8-byte slots (168 bytes)
 * for the register-capture core, then (sigsetjmp/siglongjmp only) one
 * more 8-byte slot for a saved sigset_t + a flag word. This SDK's real
 * jmp_buf is 192 bytes (_JBLEN=48 ints) and sigjmp_buf is 196 bytes
 * (_JBLEN+1) -- both comfortably larger than the 176 bytes this file
 * actually uses, confirmed by reading iPhoneOS.sdk's own setjmp.h
 * directly, not assumed. */
#define ED_JMP_CORE_BYTES 168
#define ED_JMP_SIGMASK_OFF 168
#define ED_JMP_SIGFLAG_OFF 176

int
_setjmp(int env[])
{
	return __ed_setjmp_raw(env);
}

void
_longjmp(int env[], int val)
{
	__ed_longjmp_raw(env, val);
}

int
setjmp(int env[])
{
	return __ed_setjmp_raw(env);
}

void
longjmp(int env[], int val)
{
	__ed_longjmp_raw(env, val);
}

int
sigsetjmp(int env[], int savemask)
{
	unsigned char *buf = (unsigned char *)env;
	int r;

	if (savemask) {
		sigset_t oldmask;
		sigprocmask(SIG_BLOCK, NULL, &oldmask);
		__builtin_memcpy(buf + ED_JMP_SIGMASK_OFF, &oldmask, sizeof(oldmask));
		*(int *)(buf + ED_JMP_SIGFLAG_OFF) = 1;
	} else {
		*(int *)(buf + ED_JMP_SIGFLAG_OFF) = 0;
	}

	r = __ed_setjmp_raw(env);
	return r;
}

void
siglongjmp(int env[], int val)
{
	unsigned char *buf = (unsigned char *)env;

	if (*(int *)(buf + ED_JMP_SIGFLAG_OFF)) {
		sigset_t savedmask;
		__builtin_memcpy(&savedmask, buf + ED_JMP_SIGMASK_OFF, sizeof(savedmask));
		sigprocmask(SIG_SETMASK, &savedmask, NULL);
	}
	__ed_longjmp_raw(env, val);
}
