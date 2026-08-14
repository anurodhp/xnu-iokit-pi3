/* Real Apple/FreeBSD stdio smoketest: proves fopen/fwrite/fread/fgets/
 * fclose (the FILE*-infrastructure tier build_libc_stdio.sh builds, see
 * that script's own header comment for exactly what is/isn't in scope)
 * actually work end to end against a real file, not just that the
 * archive links. printf/scanf-family functions are out of scope for this
 * tier (see build_libc_stdio.sh) so this uses fwrite() for its own
 * "did it work" output, not fprintf().
 */

#include <stdio.h>

static void put(const char *s) {
    while (*s) {
        putchar(*s);
        s++;
    }
}

int main(void) {
    const char *path = "/tmp/libc_stdio_smoketest.tmp";
    const char *msg = "real Apple stdio round-trip\n";

    FILE *wf = fopen(path, "w");
    if (!wf) {
        put("FAIL: fopen(write) returned NULL\n");
        return 1;
    }
    size_t wrote = fwrite(msg, 1, 29, wf);
    fclose(wf);
    if (wrote != 29) {
        put("FAIL: fwrite short write\n");
        return 1;
    }

    FILE *rf = fopen(path, "r");
    if (!rf) {
        put("FAIL: fopen(read) returned NULL\n");
        return 1;
    }
    char buf[64];
    char *got = fgets(buf, sizeof(buf), rf);
    fclose(rf);
    if (!got) {
        put("FAIL: fgets returned NULL\n");
        return 1;
    }

    int match = 1;
    for (int i = 0; i < 29; i++) {
        if (buf[i] != msg[i]) {
            match = 0;
            break;
        }
    }
    if (!match) {
        put("FAIL: read-back mismatch\n");
        return 1;
    }

    put("PASS: real fopen/fwrite/fclose/fgets round-trip via stdout, stderr, stdin's own __sF table\n");
    return 0;
}
