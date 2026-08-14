/*
 * Host-side test harness for pwtool.c -- same host-shim discipline as
 * tools/libinfo_custom/host_test.c and tools/init_binary's documented
 * pattern: forwards the freestanding raw-syscall layer to real host libc
 * so the actual provisioning logic (validation, locking, atomic rewrite,
 * bootstrap-vs-corruption distinction) gets exercised for real, against
 * real temp files, without needing QEMU.
 *
 * Build: clang -DPWTOOL_HOST_TEST -Wall -Wextra -o pwtool_host_test \
 *          host_test.c pwtool.c ../libinfo_custom/libinfo_custom.c \
 *          ../libinfo_custom/crypt_des.c -I../libinfo_custom
 * Run:   ./pwtool_host_test
 */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

long pt_host_open(const char *path, int flags, int mode) {
    int hf = 0;
    if (flags & 0x0001) hf |= O_WRONLY;
    if (flags & 0x0200) hf |= O_CREAT;
    if (flags & 0x0400) hf |= O_TRUNC;
    if (flags & 0x0800) hf |= O_EXCL;
    if (hf == 0) hf = O_RDONLY;
    long r = open(path, hf, mode);
    if (r < 0) return -errno;
    return r;
}
long pt_host_read(int fd, void *buf, unsigned long n) { return read(fd, buf, n); }
long pt_host_write(int fd, const void *buf, unsigned long n) { return write(fd, buf, n); }
long pt_host_close(int fd) { return close(fd); }
long pt_host_unlink(const char *path) { return unlink(path); }
long pt_host_rename(const char *from, const char *to) { return rename(from, to); }
long pt_host_getpid(void) { return getpid(); }
long pt_host_fsync(int fd) { return fsync(fd); }

#include "pwtool.c"

static int g_failures = 0;
static void check(int cond, const char *what) {
    if (cond) { printf("ok: %s\n", what); }
    else { printf("FAIL: %s\n", what); g_failures++; }
}

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    fwrite(content, 1, strlen(content), f);
    fclose(f);
}

static char *read_file_alloc(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    fread(buf, 1, (size_t)n, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}

int main(void) {
    const char *pw = "/tmp/pwtool_test_master.passwd";
    const char *gr = "/tmp/pwtool_test_group";
    unlink(pw); unlink(gr);
    unlink("/tmp/pwtool_test_master.passwd.pwtool.lock");
    unlink("/tmp/pwtool_test_group.pwtool.lock");

    /* ---- 1. Bootstrap: useradd on a completely fresh (nonexistent) file. */
    {
        int rc = pw_useradd(pw, "alice", "501", "20", "Alice Example", "/home/alice", "/bin/sh");
        check(rc == 0, "useradd: bootstrap (no pre-existing file) succeeds");
        char *content = read_file_alloc(pw);
        check(content != 0, "useradd: file was actually created");
        if (content) {
            check(strstr(content, "alice:*:501:20::0:0:Alice Example:/home/alice:/bin/sh\n") != 0,
                "useradd: new entry has the exact expected field layout, password locked ('*')");
            free(content);
        }
    }

    /* ---- 2. A second useradd appends without disturbing the first. */
    {
        int rc = pw_useradd(pw, "bob", "502", "20", "Bob Example", "/home/bob", "/bin/csh");
        check(rc == 0, "useradd: second account added");
        li_pw_load(pw);
        check(li_getpwnam("alice") != 0, "useradd: first account (alice) still present after second add");
        check(li_getpwnam("bob") != 0, "useradd: second account (bob) present");
    }

    /* ---- 3. Duplicate name/uid rejected. */
    {
        int rc = pw_useradd(pw, "alice", "600", "20", "Dup Name", "/home/x", "/bin/sh");
        check(rc != 0, "useradd: duplicate name rejected");
        rc = pw_useradd(pw, "someoneelse", "501", "20", "Dup Uid", "/home/y", "/bin/sh");
        check(rc != 0, "useradd: duplicate uid rejected");
        li_pw_load(pw);
        check(li_getpwnam("someoneelse") == 0, "useradd: rejected duplicate-uid attempt did not partially write anything");
    }

    /* ---- 4. Field-injection safety: colon and newline in gecos/home/shell
     * rejected outright, never silently written. */
    {
        int rc = pw_useradd(pw, "evil1", "503", "20", "Evil:Injected:Fields", "/home/evil", "/bin/sh");
        check(rc != 0, "useradd: colon in gecos field rejected (would desync field boundaries)");
        rc = pw_useradd(pw, "evil2", "504", "20", "Evil\nfake::0:0:root:/root:/bin/sh", "/home/evil2", "/bin/sh");
        check(rc != 0, "useradd: newline in gecos field rejected (would inject a fake extra line)");
        li_pw_load(pw);
        check(li_getpwnam("evil1") == 0, "useradd: colon-injection attempt not present in the file");
        check(li_getpwuid(0) == 0 || (li_getpwuid(0) != 0 && strcmp(li_getpwuid(0)->pw_name, "fake") != 0),
            "useradd: no injected uid-0 'fake' root entry exists in the file");
    }

    /* ---- 5. Invalid name charset rejected. */
    {
        int rc = pw_useradd(pw, "bad name!", "505", "20", "x", "/home/x", "/bin/sh");
        check(rc != 0, "useradd: name with space/'!' rejected (outside allowed charset)");
    }

    /* ---- 6. groupadd, same duplicate/injection checks. */
    {
        int rc = pw_groupadd(gr, "staff", "20", "alice,bob");
        check(rc == 0, "groupadd: bootstrap group created");
        rc = pw_groupadd(gr, "staff", "21", "");
        check(rc != 0, "groupadd: duplicate group name rejected");
        rc = pw_groupadd(gr, "evilgrp", "20", "");
        check(rc != 0, "groupadd: duplicate gid rejected");
        rc = pw_groupadd(gr, "hacked", "22", "root:extra");
        check(rc != 0, "groupadd: colon in members field rejected");
        li_gr_load(gr);
        struct li_group *staff = li_getgrnam("staff");
        check(staff != 0 && staff->gr_nmem == 2, "groupadd: real group has exactly the 2 real members, no injected extras");
    }

    /* ---- 7. passwd: set + verify round trip via the real crypt path. */
    {
        int rc = pw_passwd_set(pw, "alice", "correct horse battery staple");
        check(rc == 0, "passwd: set succeeds for an existing account");
        li_pw_load(pw);
        struct li_passwd *alice = li_getpwnam("alice");
        check(alice != 0, "passwd: alice still present after password change");
        if (alice) {
            check(alice->pw_passwd[0] == '_', "passwd: stored hash uses the stronger extended DES-crypt format");
            check(li_crypt_verify("correct horse battery staple", alice->pw_passwd) == 1,
                "passwd: the real crypt_verify path accepts the password that was just set");
            check(li_crypt_verify("wrong guess", alice->pw_passwd) == 0,
                "passwd: the real crypt_verify path rejects an incorrect password");
            check(strcmp(alice->pw_dir, "/home/alice") == 0,
                "passwd: changing the password did not disturb alice's OTHER fields (pw_dir unchanged)");
        }
        check(li_getpwnam("bob") != 0, "passwd: changing alice's password did not disturb bob's entry");
    }

    /* ---- 8. passwd on a nonexistent account fails cleanly. */
    {
        int rc = pw_passwd_set(pw, "nosuchuser", "whatever");
        check(rc != 0, "passwd: nonexistent account rejected");
    }

    /* ---- 9. passwd refuses an empty password (never creates a
     * fail-open empty-hash account). */
    {
        int rc = pw_passwd_set(pw, "bob", "");
        check(rc != 0, "passwd: empty password rejected outright");
        li_pw_load(pw);
        struct li_passwd *bob = li_getpwnam("bob");
        check(bob != 0 && strcmp(bob->pw_passwd, "*") == 0,
            "passwd: bob's password is still the original locked '*' sentinel, not silently changed to empty");
    }

    /* ---- 10. check: reports real counts, does not mutate anything. */
    {
        int rc = pw_check(pw, gr);
        check(rc == 0, "check: a well-formed real file reports success");
    }

    /* ---- 11. Corruption safety: an existing-but-unparseable file must
     * NOT be silently replaced by a fresh single-entry file (real data
     * loss risk flagged in this file's own header comment). Simulate
     * corruption by truncating mid-line in a way li_pw_load can't fully
     * parse... actually li_pw_load tolerates malformed LINES gracefully
     * (by design) - so "failed to load" in practice only really happens
     * on a real I/O error, which is hard to simulate portably in a host
     * test. Instead, confirm the WEAKER but still real property: adding
     * to an existing file preserves every prior real entry (already
     * covered above by check #2), and that a permission-denied file
     * (simulating a real I/O error) is refused rather than silently
     * treated as "start fresh". */
    {
        const char *denied = "/tmp/pwtool_test_denied.passwd";
        write_file(denied, "root:*:0:0::0:0:root:/var/root:/bin/sh\n");
        chmod(denied, 0000);
        int rc = pw_useradd(denied, "newguy", "999", "999", "x", "/home/newguy", "/bin/sh");
        check(rc != 0,
            "useradd: an existing-but-UNREADABLE (permission-denied) file is refused, "
            "not silently treated as a fresh/empty bootstrap case");
        chmod(denied, 0644);
        unlink(denied);
        unlink("/tmp/pwtool_test_denied.passwd.pwtool.lock");
    }

    /* ---- 12. Lock contention: a pre-existing lock file blocks a new
     * edit rather than racing it. */
    {
        char lockpath[512];
        snprintf(lockpath, sizeof(lockpath), "%s.pwtool.lock", pw);
        int lfd = open(lockpath, O_CREAT | O_EXCL | O_WRONLY, 0600);
        check(lfd >= 0, "lock test: could pre-create the lock file to simulate a concurrent edit");
        int rc = pw_useradd(pw, "raceuser", "777", "777", "x", "/home/race", "/bin/sh");
        check(rc != 0, "useradd: refuses to proceed while the lock file already exists (TOCTOU protection)");
        close(lfd);
        unlink(lockpath);
        li_pw_load(pw);
        check(li_getpwnam("raceuser") == 0, "useradd: the lock-blocked attempt wrote nothing");
    }

    unlink(pw); unlink(gr);
    unlink("/tmp/pwtool_test_master.passwd.pwtool.lock");
    unlink("/tmp/pwtool_test_group.pwtool.lock");

    if (g_failures == 0) {
        printf("pwtool host_test: ALL CHECKS PASSED\n");
        return 0;
    } else {
        printf("pwtool host_test: %d CHECK(S) FAILED\n", g_failures);
        return 1;
    }
}
