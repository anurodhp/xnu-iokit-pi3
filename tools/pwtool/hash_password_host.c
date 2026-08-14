/* Host-side (real macOS toolchain/libc, NOT the freestanding arm64 target)
 * one-shot password hasher, used by tools/init_binary/inject_into_sd_image.sh
 * to provision root's /etc/master.passwd entry directly onto a mounted SD
 * image from the host -- the only way to get a FIRST account onto an image
 * now that init.c's do_login() no longer has an implicit-root bootstrap
 * fallback (see init.c's LOGIN_NO_ACCOUNTS handling). Real pwtool.c can't
 * do this itself: it's a freestanding arm64 Mach-O meant to run ON the
 * booted target, not on the host, and running it there would need a
 * shell to exec it from -- exactly the account that doesn't exist yet.
 *
 * Deliberately tiny and separate from pwtool.c's own freestanding build:
 * links normally against host libc, reuses crypt_des.c/.h directly (that
 * file makes zero syscalls -- see crypt_des_host_test.c's own header
 * comment -- so it compiles and runs as an ordinary host program with no
 * shim needed, same as that test file does).
 *
 * Usage: hash_password_host <password>   -- prints the extended-format
 * DES-crypt hash to stdout, nothing else (script-friendly, no extra
 * output to strip).
 *
 * Build: clang -Wall -Wextra -I. -o hash_password_host hash_password_host.c \
 *          ../libinfo_custom/crypt_des.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "../libinfo_custom/crypt_des.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <password>\n", argv[0]);
        return 2;
    }
    /* Real per-invocation entropy source (matches pwtool.c's own
     * getpid()-based seeding rationale in crypt_des.h -- salt only needs
     * to vary run-to-run, not resist prediction; see that file's header
     * for why this is acceptable here). */
    li_crypt_seed((unsigned long)time(NULL) ^ (unsigned long)getpid());

    char hash[32];
    if (!li_hash_password(argv[1], hash, sizeof(hash))) {
        fprintf(stderr, "%s: li_hash_password failed\n", argv[0]);
        return 1;
    }
    printf("%s\n", hash);
    return 0;
}
