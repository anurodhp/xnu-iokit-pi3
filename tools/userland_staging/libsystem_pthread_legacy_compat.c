/* Legacy entry point kept in libsystem_pthread.dylib (DAR-416 step 1b).
 *
 * Before DAR-416, this port did not build Apple's pthread.c. The main
 * thread was set up by a hand-written pthread_main_thread_bootstrap()
 * (pthread_main_thread_bootstrap.c), which the libSystem initializer called,
 * and which generic_main_wrapper.c and the other *_main_wrapper.c files
 * called again from each command's own main(). About 105 shipped binaries
 * bind it (libsystem_pthread/_pthread_main_thread_bootstrap).
 *
 * Now libSystem's initializer calls Apple's real __pthread_init()
 * (libpthread/src/pthread.c), as Libsystem init.c:221 does. That adopts the
 * main thread the TSD already points at (pthread.c:1753-1757, __pthread_init:
 * _main_thread_ptr = _pthread_self_direct()). A second bootstrap from a
 * command's main() must not re-point the TSD at a different struct, so this
 * is a no-op that reports success: the wrappers check for a nonzero
 * return. Removing the call from the main wrappers and relinking those
 * binaries retires this file (DAR-426).
 */
int pthread_main_thread_bootstrap(void);

int
pthread_main_thread_bootstrap(void)
{
	return 1;
}
