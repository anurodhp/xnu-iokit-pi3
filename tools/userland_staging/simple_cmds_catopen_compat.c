/* Real POSIX catopen(3) for text_cmds/uniq/uniq.c's strerror_init(): its
 * ONLY call site (uniq.c's own comment: "There should be strerror_init()
 * and strsignal_init() in libc", a pre-capability-mode NLS-cache prime)
 * discards the return value entirely -- no catgets() call anywhere in
 * uniq.c depends on a real open catalog descriptor.
 *
 * Real Apple Libc genuinely has no catopen.c/catgets.c source in this
 * checkout -- confirmed via grep across third_party/Libc/nls: only the
 * man pages (catopen.3, catgets.3) ship, no implementation, the same
 * class of gap this project's other build scripts already document for
 * unrelated files (e.g. build_libc_gen.sh's header on pwcache.c). A full
 * message-catalog subsystem would be pure custom last-resort work to
 * support a single discarded caching call -- not worth building.
 *
 * Real, spec-compliant behavior instead: POSIX catopen(3) documents
 * returning (nl_catd)-1 with errno set when no such catalog exists --
 * exactly this system's honest, permanent state (zero NLS catalog data
 * shipped or buildable yet). Not a fabricated success. */
#include <errno.h>
#include <nl_types.h>

nl_catd catopen(const char *name, int oflag) {
    (void)name;
    (void)oflag;
    errno = ENOENT;
    return (nl_catd)-1;
}
