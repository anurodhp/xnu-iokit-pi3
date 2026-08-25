/* Real gap: real, unmodified pthread_atfork.c's __pthread_atfork_child()
 * (the fork-child-side handler _pthread_atfork() registers) calls
 * __pthread_bsdthread_init()/__pthread_main_thread_postfork_init(),
 * both real Apple source but only defined in pthread.c -- this
 * project's own build_libpthread.sh deliberately never compiles that
 * file (its own header comment: real pthread_create()/thread-lifecycle
 * logic was ported piecemeal elsewhere in this project instead, not as
 * a straight port of pthread.c wholesale).
 *
 * Both are only ever reached from the CHILD side of an actual fork(2)
 * inside a process that has pthread state to re-initialize -- this
 * project's only fork() caller is tools/init_binary/init.c's PID-1, a
 * fully static binary with no pthread/dyld involvement at all (see
 * cerror_stub.c's own comment on this project's single-threaded-per-
 * process reality). No dyld-loaded, dylib-based process in this
 * project ever calls fork(), so this code path is genuinely
 * unreachable today -- these exist only because the linker needs
 * *something* defined for pthread_atfork.c's own real source to
 * reference, not because anything calls them. Honest, minimal no-op
 * stand-ins, not a real implementation -- a real fork()-from-a-dylib-
 * process consumer would need real pthread.c ported first. */
void _pthread_bsdthread_init(void *arg) {
    (void)arg;
}

void _pthread_main_thread_postfork_init(void) {
}
