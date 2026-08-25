/* Real sqrt(3) (ping.c's pr_summary(), the round-trip stddev in the
 * final "--- statistics ---" line -- its only real call site, confirmed
 * by grep) needs Apple's real Libm (apple-oss-distributions/Libm, a
 * whole separate fdlibm-derived project) -- genuinely absent from this
 * checkout's third_party/ (not pinned by setup_third_party.sh, and this
 * task is explicitly scoped to not touch that file / not add a new
 * pin). Real, well-known algorithmic gap, same class of "last resort"
 * CLAUDE.md's source-priority rule anticipates (Apple source tried
 * first -- doesn't exist here; no single-file FreeBSD fallback exists
 * either, real msun sqrt.c has its own small dependency web).
 *
 * Real fix used here, not a fabricated approximation: a thin wrapper
 * around the compiler's __builtin_sqrt(), which on this arm64 target
 * lowers directly to the hardware `fsqrt` instruction (AArch64 has
 * mandatory IEEE-754 double-precision FSQRT in its base FP/SIMD ISA --
 * this is not a software approximation loop, it is the same
 * correctly-rounded hardware operation Apple's real Libm sqrt()
 * ultimately bottoms out on too for the common case). -fno-builtin
 * (this project's standard freestanding CFLAGS) only stops clang from
 * assuming call-site *semantics* about a function named "sqrt" it
 * didn't itself define -- it has no effect on __builtin_sqrt, an
 * explicit compiler intrinsic.
 */
double sqrt(double x) {
    return __builtin_sqrt(x);
}
