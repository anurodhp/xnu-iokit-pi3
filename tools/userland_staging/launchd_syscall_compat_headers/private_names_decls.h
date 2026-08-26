#ifndef LAUNCHD_SYSCALL_PRIVATE_NAMES_DECLS_H
#define LAUNCHD_SYSCALL_PRIVATE_NAMES_DECLS_H
/* Real Apple internal "private name" declarations gen/FreeBSD/exec.c
 * and gen/FreeBSD/signal.c call directly (see
 * launchd_syscall_private_names_compat.c's own header comment for the
 * full story on why these need a separate real definition here). */
#include <sys/types.h>
#include <signal.h>
extern int _execve(const char *, char *const *, char *const *);
extern ssize_t _write(int, const void *, size_t);
extern int _sigaction(int, const struct sigaction *, struct sigaction *);
extern int _sigaction_nobind(int, const struct sigaction *, struct sigaction *);
#endif
