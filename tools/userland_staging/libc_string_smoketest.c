/* Smoketest: links real Apple/FreeBSD-sourced libc string functions
 * (tools/userland_staging/libc_build/libc_string.a - see
 * build_libc_string.sh) against real Csu crt0 and real libsyscall
 * trampolines, and actually calls several of them, to prove the archive
 * is genuinely usable, not just "compiles standalone". No libc beyond
 * that archive - main() below issues one raw syscall directly (write)
 * only to report the result, same minimal-dependency approach as
 * crt0_smoketest.c/libsyscall_smoketest.c.
 */
#include <string.h>
#include <strings.h>

typedef unsigned long size_t;

static long syscall3(long num, long a0, long a1, long a2) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x16 asm("x16") = num;
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
    return x0;
}

static void put(const char *s) {
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    syscall3(4 /* SYS_write */, 1, (long)s, (long)n);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    char buf[64];
    bzero(buf, sizeof(buf));
    strcat(buf, "hello");
    strcat(buf, " libc");

    int ok = 1;
    ok &= (strlen(buf) == 10);
    ok &= (strcmp(buf, "hello libc") == 0);

    char copy[16];
    memset(copy, 0, sizeof(copy));
    memcpy(copy, buf, strlen(buf) + 1);
    ok &= (strncmp(copy, "hello libc", 10) == 0);

    char tokbuf[16];
    stpcpy(tokbuf, "a,b,c");
    char *tok = strtok(tokbuf, ",");
    ok &= (tok != 0 && strcmp(tok, "a") == 0);

    if (ok) {
        put("libc_string_smoketest: real string functions work: hello libc\n");
    } else {
        put("libc_string_smoketest: FAILED\n");
    }

    return 0;
}

void exit(int code) {
    syscall3(1 /* SYS_exit */, code, 0, 0);
    for (;;) { }
}
