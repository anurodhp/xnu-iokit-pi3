/* DAR-340/C1 regression probe. A constructor that appends its uid/euid/gid to
 * /tmp/dyld_inject_probe.out. Load it with DYLD_INSERT_LIBRARIES against a
 * setuid image (su, sudo): the file must NOT be written. Against an ordinary
 * binary (id, ls) it MUST be written -- that proves the probe works and that
 * dyld still honors DYLD_* for non-restricted processes. See
 * docs/sudo-su-dyld-regression.md. Uses only libSystem. */
#include <fcntl.h>
#include <unistd.h>

static void put_num(char **p, long v) {
    char t[24]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v > 0) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) *(*p)++ = t[--n];
}

__attribute__((constructor)) static void dyld_inject_probe_ctor(void) {
    char buf[96], *p = buf;
    *p++ = 'u'; *p++ = 'i'; *p++ = 'd'; *p++ = '='; put_num(&p, getuid());
    *p++ = ' '; *p++ = 'e'; *p++ = 'u'; *p++ = 'i'; *p++ = 'd'; *p++ = '='; put_num(&p, geteuid());
    *p++ = ' '; *p++ = 'p'; *p++ = 'i'; *p++ = 'd'; *p++ = '='; put_num(&p, getpid());
    *p++ = '\n';
    int fd = open("/tmp/dyld_inject_probe.out", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) { write(fd, buf, (size_t)(p - buf)); close(fd); }
}
