/* Real public read(2) -- this project's curated locale/stdio tier
 * already has the private raw syscall wrapper (`_read` in
 * libc_locale_syscalls.c, C source name -- one leading underscore in
 * source, compiles to linker symbol `__read` after the compiler's own
 * standard one-more-underscore C mangling -- confirmed present in
 * libc_locale_for_stdio.a via nm, not assumed) but never needed a
 * PUBLIC `read()` forwarding to it before build_simple_cmds.sh's Tier 3
 * (cat/wc -- every prior tier only ever wrote, never read, a file) --
 * same real gap libc_locale_syscalls.c's own `close()`/`open()`
 * forwarders already document and fix for those two calls. Shared
 * across every simple-cmds binary (not just cat, which is where this
 * was first needed) since wc.c also calls read() directly. */
#include <sys/types.h>

extern ssize_t _read(int fd, void *buf, size_t n);
ssize_t read(int fd, void *buf, size_t n) {
    return _read(fd, buf, n);
}
