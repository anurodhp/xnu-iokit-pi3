/* Honest stub, not a redirect: real launchctl.c (PureDarwin) includes
 * <editline/readline.h> (its own comment says readline.h/history.h are
 * "both symlinks to this" on real Darwin), but never actually calls
 * readline()/add_history()/any rl_* symbol anywhere in the file --
 * confirmed via a real grep of the whole 4700+-line source before
 * writing this stub, not assumed. This project has no interactive
 * command-line-editing library ported (no libedit/editline source is
 * pinned anywhere in setup_third_party.sh) and doesn't need one for
 * that reason: an empty header satisfies the #include with zero real
 * behavior lost, since nothing in launchctl.c actually reaches through
 * it. If a future real caller needs actual readline() here, this stub
 * is the place that would need a real libedit port, not a fabricated
 * inline implementation.
 */
#ifndef _LAUNCHCTL_COMPAT_EDITLINE_READLINE_H_
#define _LAUNCHCTL_COMPAT_EDITLINE_READLINE_H_
#endif
