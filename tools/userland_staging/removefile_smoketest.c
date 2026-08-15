/* Real removefile-tier smoketest: builds a real 2-level directory tree
 * with real files on the real HFS+ root under /tmp, calls real
 * removefile() with REMOVEFILE_RECURSIVE, then verifies via real stat()
 * that every path is actually gone. Uses real fopen/fclose (stdio tier)
 * and real mkdir (already a working init.c shell builtin, this smoketest
 * calls the real mkdir(2) syscall directly instead) to build the tree.
 */

#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>

#include "removefile.h"

static void diag_exists(const char *label, const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        printf("  DIAG: %s (%s) STILL EXISTS\n", label, path);
    } else {
        printf("  DIAG: %s (%s) gone (errno %d)\n", label, path, errno);
    }
}

static int touch(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs("hello\n", f);
    fclose(f);
    return 0;
}

int main(void) {
    if (mkdir("/tmp/rf_smoketest", 0755) != 0) {
        fputs("FAIL: mkdir /tmp/rf_smoketest\n", stdout);
        return 1;
    }
    if (mkdir("/tmp/rf_smoketest/sub", 0755) != 0) {
        fputs("FAIL: mkdir /tmp/rf_smoketest/sub\n", stdout);
        return 1;
    }
    if (touch("/tmp/rf_smoketest/a.txt") != 0 ||
        touch("/tmp/rf_smoketest/sub/b.txt") != 0) {
        fputs("FAIL: touch\n", stdout);
        return 1;
    }

    struct stat st;
    if (stat("/tmp/rf_smoketest/sub/b.txt", &st) != 0) {
        fputs("FAIL: pre-check stat on b.txt\n", stdout);
        return 1;
    }
    fputs("PASS: real directory tree built (mkdir/fopen/fwrite)\n", stdout);

    int rc = removefile("/tmp/rf_smoketest", NULL, REMOVEFILE_RECURSIVE);
    if (rc != 0) {
        printf("FAIL: removefile returned %d (errno %d)\n", rc, errno);
        return 1;
    }
    fputs("PASS: real removefile(REMOVEFILE_RECURSIVE) returned 0\n", stdout);

    if (stat("/tmp/rf_smoketest", &st) == 0 || errno != ENOENT) {
        fputs("FAIL: /tmp/rf_smoketest still exists after removefile\n", stdout);
        diag_exists("top dir", "/tmp/rf_smoketest");
        diag_exists("a.txt", "/tmp/rf_smoketest/a.txt");
        diag_exists("sub dir", "/tmp/rf_smoketest/sub");
        diag_exists("b.txt", "/tmp/rf_smoketest/sub/b.txt");
        DIR *d = opendir("/tmp/rf_smoketest");
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                printf("  DIAG: top dir contains: %s\n", de->d_name);
            }
            closedir(d);
        } else {
            printf("  DIAG: opendir(/tmp/rf_smoketest) failed, errno %d\n", errno);
        }
        return 1;
    }
    fputs("PASS: real stat confirms the whole tree is actually gone\n", stdout);

    fputs("ALL PASS: removefile smoketest\n", stdout);
    return 0;
}
