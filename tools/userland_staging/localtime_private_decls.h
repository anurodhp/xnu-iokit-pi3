#ifndef LOCALTIME_PRIVATE_DECLS_H
#define LOCALTIME_PRIVATE_DECLS_H
/* Real internal "private name" declarations stdtime/FreeBSD/localtime.c
 * calls directly (same real gap class as exec.c/signal.c's own
 * _execve/_write/_sigaction, see launchd_syscall_compat_headers/
 * private_names_decls.h) -- all already real, already-linked functions
 * in this project's curated stack (libc_gen.a's real open/fstat/close/
 * read, libsystem_pthread.dylib's real rwlock functions, libc_locale.a's
 * real pthread_key_init_np/TSD functions), just missing their
 * declarations in this exact compile's include chain.
 */
#include <sys/types.h>
#include <sys/stat.h>
extern int _open(const char *, int, ...);
extern int _fstat(int, struct stat *);
extern int _close(int);
extern long _read(int, void *, unsigned long);
extern int _pthread_rwlock_rdlock(void *);
extern int _pthread_rwlock_unlock(void *);
extern int _pthread_rwlock_wrlock(void *);
extern int __isthreaded;
extern int pthread_key_init_np(int, void (*)(void *));
extern int _pthread_once(int *, void (*)(void));
extern void *_pthread_getspecific(unsigned long);
extern int _pthread_setspecific(unsigned long, const void *);
#endif
