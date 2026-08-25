/* Phase 6.5 Phase A (docs/daemon-roadmap.md): trivial real .dylib to prove
 * this project's pinned Xcode-12 ld64 can emit MH_DYLIB/LC_ID_DYLIB at all
 * on this toolchain -- nothing here has ever produced a dylib before (every
 * other build_*.sh in this directory is deliberately -static). Not linked
 * against by any real program; build_dyld_test_exec.sh links against it
 * purely to give ld64 a dylib to record an LC_LOAD_DYLIB dependency on, in
 * turn forcing a default (non-static) link, which is what makes ld64 emit
 * LC_LOAD_DYLINKER -- see that script's own comment.
 */
int libtest_answer(void) { return 42; }
