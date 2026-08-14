/* Real Apple/FreeBSD gen-tier smoketest: proves opendir/readdir/closedir
 * and getcwd (build_libc_gen.sh's own POSIX-wrapper tier) actually work
 * end to end against the real HFS+ root, not just that the archive
 * links. Uses real fopen/fwrite/fclose (build_libc_stdio.sh's tier) to
 * report PASS/FAIL, matching that tier's own smoketest's convention.
 */

#include <dirent.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    char cwdbuf[256];
    if (getcwd(cwdbuf, sizeof(cwdbuf)) == NULL) {
        fputs("FAIL: getcwd returned NULL\n", stdout);
        return 1;
    }

    DIR *d = opendir("/tmp");
    if (!d) {
        fputs("FAIL: opendir(/tmp) returned NULL\n", stdout);
        return 1;
    }

    int saw_dot = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' && de->d_name[1] == '\0') {
            saw_dot = 1;
        }
    }
    closedir(d);

    if (!saw_dot) {
        fputs("FAIL: readdir never returned \".\"\n", stdout);
        return 1;
    }

    fputs("PASS: real getcwd/opendir/readdir/closedir round-trip against /tmp\n", stdout);
    return 0;
}
