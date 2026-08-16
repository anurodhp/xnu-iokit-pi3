/* Real Libc/gen/FreeBSD/setmode.c calls a private `_sigprocmask` (one
 * leading underscore in its own source, compiles to linker symbol
 * `__sigprocmask`) with zero prototype in scope -- same class of gap as
 * every other underscore-prefixed private-alias case documented in
 * libc_build_prototypes.h. The real raw syscall this project's Perl
 * generator produces for plain (public) `sigprocmask` already IS the
 * exact real syscall body (no separate private/public split exists for
 * this one -- confirmed via the generated .s file's own
 * MI_ENTRY_POINT(_sigprocmask) label, not guessed); this is a one-line
 * forwarder providing the private name setmode.c expects. */
#include <signal.h>

extern int sigprocmask(int how, const sigset_t *set, sigset_t *oset);

/* void* params to exactly match libc_build_prototypes.h's forced-include
 * prototype (real sigset_t* would conflict with that -- see this
 * project's own pthread_mutex_* prototypes for the same pattern/reason
 * in that file), cast back to the real type here where it's safe. */
int _sigprocmask(int how, void *set, void *oset) {
    return sigprocmask(how, (const sigset_t *)set, (sigset_t *)oset);
}
