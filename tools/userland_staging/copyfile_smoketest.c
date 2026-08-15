/* Real copyfile-tier smoketest: writes a real file with real content on
 * the real HFS+ root, copies it via real copyfile(COPYFILE_DATA), reads
 * both back and compares byte-for-byte, then cleans up via real
 * removefile() (previous tier).
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "copyfile.h"
#include "removefile.h"

static const char *kContent = "the quick brown fox jumps over the lazy dog\n";

int main(void) {
    FILE *f = fopen("/tmp/cf_src.txt", "w");
    if (!f) {
        fputs("FAIL: fopen src for write\n", stdout);
        return 1;
    }
    fputs(kContent, f);
    fclose(f);
    fputs("PASS: real source file written\n", stdout);

    int rc = copyfile("/tmp/cf_src.txt", "/tmp/cf_dst.txt", NULL, COPYFILE_DATA);
    if (rc != 0) {
        printf("FAIL: copyfile returned %d\n", rc);
        return 1;
    }
    fputs("PASS: real copyfile(COPYFILE_DATA) returned 0\n", stdout);

    FILE *d = fopen("/tmp/cf_dst.txt", "r");
    if (!d) {
        fputs("FAIL: fopen dst for read\n", stdout);
        return 1;
    }
    char buf[128];
    size_t n = fread(buf, 1, sizeof(buf) - 1, d);
    fclose(d);
    buf[n] = '\0';

    if (n != strlen(kContent) || strcmp(buf, kContent) != 0) {
        fputs("FAIL: copied content does not match source byte-for-byte\n", stdout);
        return 1;
    }
    fputs("PASS: copied content matches source byte-for-byte\n", stdout);

    removefile("/tmp/cf_src.txt", NULL, 0);
    removefile("/tmp/cf_dst.txt", NULL, 0);

    fputs("ALL PASS: copyfile smoketest\n", stdout);
    return 0;
}
