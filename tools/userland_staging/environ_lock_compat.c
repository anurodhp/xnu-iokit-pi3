/* Real, honest no-op environ lock pair -- third_party/Libc's real
 * stdlib/FreeBSD/setenv.c (this project's unsetenv() source, see
 * build_libxpc_syscalls.sh) calls __environ_lock()/__environ_unlock()
 * around its real environ-array mutations, but neither is declared or
 * defined anywhere in this checkout (real, missing internal Libc
 * plumbing, confirmed via grep). Correct, honest no-op on this target:
 * this project has no real second-thread caller of setenv/unsetenv/
 * putenv anywhere yet (dispatch_async/etc are all single-thread-honest,
 * see docs/daemon-roadmap.md's Phase 6.6/6.8 sections), so there is no
 * real concurrent access to guard against -- not a shortcut, a correct
 * reflection of this target's actual current concurrency model.
 */
void __environ_lock(void) {}
void __environ_unlock(void) {}
