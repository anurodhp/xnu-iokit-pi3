/* Real open(2)/read(2) $UNIX2003-suffixed forwarders -- third_party/Libc/
 * gen/FreeBSD/arc4random.c's own _my_getentropy() (see
 * libresolv_getentropy_syscall.c's header comment for why this tier
 * compiles arc4random.c at all) has a real fallback path that opens and
 * reads /dev/urandom directly if the getentropy(2) syscall itself ever
 * fails, going through this project's real Apple/FreeBSD Libc headers'
 * standard $UNIX2003 symbol-suffix renaming (namespace.h's real
 * mechanism, same one build_libc_regex.sh's own header comment already
 * documents for regcomp) rather than plain open()/read().
 *
 * Real ld -alias (the mechanism every other $UNIX2003 case in this
 * project's build scripts uses, e.g. -Wl,-alias,_read,'_read$UNIX2003')
 * only works if the *base* symbol (_read) is already pulled into the
 * link for some other reason -- confirmed by hitting a real "undefined
 * base symbol '_read' for alias" linker warning/failure while developing
 * this tier's smoketest, since nothing else in this build calls plain
 * read()/open() directly. Defining real forwarders under the exact
 * linker-visible name via GCC/Clang's standard `asm("name")` symbol-
 * renaming extension sidesteps that ordering problem entirely -- these
 * are real, correct, unconditionally present definitions, not an alias
 * hoping something else pulls the base symbol in first. Both forward to
 * this project's already-real raw syscall wrappers (`_open`/`_read` in
 * libc_locale_syscalls.c, confirmed present in libc_locale_for_stdio.a
 * via nm).
 */
#include <sys/types.h>

extern int _open(const char *path, int flags, ...);
extern ssize_t _read(int fd, void *buf, size_t n);

int open_u2003(const char *path, int flags, int mode) asm("_open$UNIX2003");
int open_u2003(const char *path, int flags, int mode) {
    return _open(path, flags, mode);
}

ssize_t read_u2003(int fd, void *buf, size_t n) asm("_read$UNIX2003");
ssize_t read_u2003(int fd, void *buf, size_t n) {
    return _read(fd, buf, n);
}
