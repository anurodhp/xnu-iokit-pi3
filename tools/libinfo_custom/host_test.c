/* Host-side test harness: real execution verification, not just "compiles
 * standalone" -- writes real test master.passwd/group files, loads them
 * through the real (LIBINFO_HOST_TEST-shimmed) code path, and checks real
 * field values come back correctly. Same host-shim pattern
 * tools/init_binary/init.c's own documented host-test harness
 * (tools/init_binary/README.md) established -- real freestanding
 * statically-linked Mach-O binaries get killed by this host's hardened
 * runtime before main() runs (confirmed this session, see
 * project_xnu_rpi3_userspace_libc_dyld.md), so this compiles as a normal,
 * dynamically-linked host executable instead: real correctness signal for
 * the shared parsing/lookup logic, with only the raw-syscall I/O layer
 * (already a thin, separately-reasoned-about 3-syscall shim) not exercised
 * in its real freestanding form here.
 *
 * Build: clang -DLIBINFO_HOST_TEST -Wall -Wextra -o host_test host_test.c
 *          libinfo_custom.c
 * Run:   ./host_test
 */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

long li_host_shim_open(const char *path, int flags, int mode) {
    (void)flags;
    return open(path, O_RDONLY, mode);
}
long li_host_shim_read(int fd, void *buf, unsigned long n) {
    return read(fd, buf, n);
}
long li_host_shim_close(int fd) {
    return close(fd);
}

#include "libinfo_custom.h"

static int g_failures = 0;

static void check(int cond, const char *what) {
    if (cond) {
        printf("ok: %s\n", what);
    } else {
        printf("FAIL: %s\n", what);
        g_failures++;
    }
}

static void write_test_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) {
        printf("FAIL: could not create test fixture %s\n", path);
        g_failures++;
        return;
    }
    fwrite(content, 1, strlen(content), f);
    fclose(f);
}

int main(void) {
    const char *pw_path = "/tmp/libinfo_custom_test_master.passwd";
    const char *gr_path = "/tmp/libinfo_custom_test_group";

    /* Real master.passwd shape: name:passwd:uid:gid:class:change:expire:
     * gecos:dir:shell -- two real-looking accounts, one root-like entry,
     * one regular user, plus a deliberately malformed line (missing a
     * field) to confirm bad lines are skipped, not fatal. */
    write_test_file(pw_path,
        "root:*:0:0:daemon:0:0:System Administrator:/var/root:/bin/sh\n"
        "alice:$6$abcd$hashvalue:501:20:staff:0:0:Alice Example:/home/alice:/bin/sh\n"
        "this:is:not:enough:fields\n"
        "bob:*:502:20::0:0:Bob Example:/home/bob:/bin/csh\n"
        "evil:*:99999999999999999999999999:20:::: :/home/evil:/bin/sh\n");

    /* Real /etc/group shape: name:passwd:gid:member,member -- includes an
     * empty-member-list group and a multi-member group. */
    write_test_file(gr_path,
        "wheel:*:0:root,alice\n"
        "staff:*:20:alice,bob\n"
        "empty_group:*:99:\n");

    int pw_n = li_pw_load(pw_path);
    check(pw_n == 3, "li_pw_load: 3 valid entries loaded, 2 malformed lines skipped");
    check(li_getpwnam("evil") == 0,
        "li_getpwnam(\"evil\"): oversized/overflowing uid field correctly rejected the whole line, "
        "not silently wrapped into a real (potentially privileged) uid");

    struct li_passwd *root = li_getpwnam("root");
    check(root != 0, "li_getpwnam(\"root\") found");
    if (root) {
        check(root->pw_uid == 0, "root pw_uid == 0");
        check(root->pw_gid == 0, "root pw_gid == 0");
        check(strcmp(root->pw_dir, "/var/root") == 0, "root pw_dir == /var/root");
        check(strcmp(root->pw_shell, "/bin/sh") == 0, "root pw_shell == /bin/sh");
        check(strcmp(root->pw_gecos, "System Administrator") == 0, "root pw_gecos correct");
    }

    struct li_passwd *alice = li_getpwuid(501);
    check(alice != 0, "li_getpwuid(501) found");
    if (alice) {
        check(strcmp(alice->pw_name, "alice") == 0, "uid 501 pw_name == alice");
        check(alice->pw_gid == 20, "alice pw_gid == 20");
        check(strcmp(alice->pw_dir, "/home/alice") == 0, "alice pw_dir correct");
        check(strcmp(alice->pw_passwd, "$6$abcd$hashvalue") == 0, "alice pw_passwd (hash) round-trips exactly");
    }

    check(li_getpwnam("nonexistent") == 0, "li_getpwnam(\"nonexistent\") correctly returns NULL");
    check(li_getpwuid(9999) == 0, "li_getpwuid(9999) correctly returns NULL");

    struct li_passwd *bob = li_getpwnam("bob");
    check(bob != 0, "li_getpwnam(\"bob\") found (empty optional fields didn't break parsing)");
    if (bob) {
        check(strcmp(bob->pw_shell, "/bin/csh") == 0, "bob pw_shell == /bin/csh");
    }

    int gr_n = li_gr_load(gr_path);
    check(gr_n == 3, "li_gr_load: 3 entries loaded");

    struct li_group *wheel = li_getgrnam("wheel");
    check(wheel != 0, "li_getgrnam(\"wheel\") found");
    if (wheel) {
        check(wheel->gr_gid == 0, "wheel gr_gid == 0");
        check(wheel->gr_nmem == 2, "wheel has 2 members");
        check(strcmp(wheel->gr_mem[0], "root") == 0, "wheel member[0] == root");
        check(strcmp(wheel->gr_mem[1], "alice") == 0, "wheel member[1] == alice");
        check(wheel->gr_mem[2] == 0, "wheel gr_mem is NULL-terminated after real members");
    }

    struct li_group *staff = li_getgrgid(20);
    check(staff != 0, "li_getgrgid(20) found");
    if (staff) {
        check(strcmp(staff->gr_name, "staff") == 0, "gid 20 gr_name == staff");
    }

    struct li_group *empty = li_getgrnam("empty_group");
    check(empty != 0, "li_getgrnam(\"empty_group\") found");
    if (empty) {
        check(empty->gr_nmem == 0, "empty_group has 0 members");
        check(empty->gr_mem[0] == 0, "empty_group gr_mem[0] is NULL");
    }

    check(li_getgrnam("nosuchgroup") == 0, "li_getgrnam(\"nosuchgroup\") correctly returns NULL");

    unlink(pw_path);
    unlink(gr_path);

    if (g_failures == 0) {
        printf("libinfo_custom host_test: ALL CHECKS PASSED\n");
        return 0;
    } else {
        printf("libinfo_custom host_test: %d CHECK(S) FAILED\n", g_failures);
        return 1;
    }
}
