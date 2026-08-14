/*
 * Host-side test harness for init.c's Phase 5 login logic
 * (attempt_login()), following the exact pattern documented in this
 * directory's README.md ("Testing the shell logic without a kernel") and
 * already established for real by tools/libinfo_custom/host_test.c and
 * tools/pwtool/host_test.c: forward the raw-syscall layer to real host
 * libc, `#define INIT_HOST_TEST 1`, `#include "init.c"` directly (same
 * translation unit, so this file can call init.c's static functions), and
 * drive the interesting C logic without touching QEMU or a real kernel.
 *
 * Scope: attempt_login() specifically -- the actual security-relevant
 * decision logic (accept/reject, anti-enumeration, bootstrap-vs-corrupted
 * distinction). do_login()'s own interactive prompt loop and cmd_su()'s
 * fork() are NOT exercised here (real tty input / real process forking
 * aren't meaningfully host-testable the way pure decision logic is) --
 * that's why attempt_login() was deliberately split out as a separate,
 * directly-callable function in the first place (see its own comment in
 * init.c). Real end-to-end verification of do_login()/cmd_su() is via a
 * real QEMU boot (see this project's runvm.sh-based pty test pattern).
 *
 * Build: clang -DINIT_HOST_TEST -Wall -Wextra -I../libinfo_custom \
 *          -o init_host_test host_test.c ../libinfo_custom/libinfo_custom.c \
 *          ../libinfo_custom/crypt_des.c
 * Run:   ./init_host_test
 */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

/* init.c's own generic 3-arg syscall shim -- only the numbers
 * attempt_login()'s own call graph actually needs (open/read/write/close;
 * exit is defined for completeness/parity but attempt_login() itself
 * never calls it). Real numbers cross-checked against
 * third_party/xnu-7195/bsd/kern/syscalls.master by init.c itself already;
 * this shim just forwards those same numbers to real host libc. */
long host_shim_syscall3(long num, long a0, long a1, long a2) {
    switch (num) {
    case 1: /* SYS_exit */
        _exit((int)a0);
    case 3: /* SYS_read */
        return read((int)a0, (void *)a1, (size_t)a2);
    case 4: /* SYS_write */
        return write((int)a0, (const void *)a1, (size_t)a2);
    case 5: { /* SYS_open -- init.c's own O_RDONLY=0/O_RDWR=2 happen to
                 match real host values already (confirmed independently
                 by tools/pwtool/host_test.c's own equivalent shim), so no
                 flag translation needed for the flags attempt_login()
                 actually uses (O_RDONLY only). */
        long r = open((const char *)a0, (int)a1, (int)a2);
        if (r < 0) return -errno;
        return r;
    }
    case 6: /* SYS_close */
        return close((int)a0);
    default:
        return -1;
    }
}

/* libinfo_custom.c's own LIBINFO_HOST_TEST shim requirements (same
 * pattern tools/libinfo_custom/host_test.c already established) -- needed
 * because attempt_login() calls li_pw_load(), which is compiled here
 * against real libinfo_custom.c built with -DLIBINFO_HOST_TEST. */
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

#define INIT_HOST_TEST 1
#include "init.c"

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

int main(void) {
    const char *pw_ok = "/tmp/init_test_master.passwd";
    const char *pw_missing = "/tmp/init_test_does_not_exist.passwd";
    unlink(pw_ok);

    /* Build a real fixture with a REAL crypt-hashed password (via this
     * project's own li_hash_password(), not a hand-typed fake hash --
     * this exercises the exact real code path attempt_login() itself
     * uses to verify, giving genuine end-to-end coverage of the
     * libinfo_custom + crypt_des + init.c integration, not just each
     * piece in isolation). */
    char hash[32];
    check(li_hash_password("swordfish", hash, sizeof(hash)) == 1,
        "fixture setup: li_hash_password succeeds for a real password");

    char content[512];
    snprintf(content, sizeof(content),
        "root:*:0:0::0:0:System Administrator:/var/root:/bin/sh\n"
        "alice:%s:501:20::0:0:Alice Example:/home/alice:/bin/sh\n"
        "locked:*:502:20::0:0:Locked Account:/home/locked:/bin/sh\n"
        "bangbang:!:503:20::0:0:Bang Locked:/home/bangbang:/bin/sh\n"
        "empty::504:20::0:0:Empty Hash:/home/empty:/bin/sh\n",
        hash);
    write_file(pw_ok, content);

    struct li_passwd *pw = 0;
    int rc;

    /* ---- 1. Correct login succeeds and returns the real account. ---- */
    rc = attempt_login(pw_ok, "alice", "swordfish", &pw);
    check(rc == LOGIN_OK, "attempt_login: correct name+password returns LOGIN_OK");
    check(pw != 0 && pw->pw_uid == 501, "attempt_login: returned struct is the real alice entry (uid 501)");

    /* ---- 2. Wrong password rejected. ---- */
    pw = 0;
    rc = attempt_login(pw_ok, "alice", "wrong password", &pw);
    check(rc == LOGIN_BAD_CREDENTIALS, "attempt_login: wrong password returns LOGIN_BAD_CREDENTIALS");
    check(pw == 0, "attempt_login: *out_pw is NULL on failure, not left pointing at anything");

    /* ---- 3. Nonexistent username rejected the SAME way as a wrong
     * password (anti-enumeration -- caller can't tell these apart from
     * the return code alone). ---- */
    pw = 0;
    rc = attempt_login(pw_ok, "nosuchuser", "whatever", &pw);
    check(rc == LOGIN_BAD_CREDENTIALS,
        "attempt_login: nonexistent username ALSO returns LOGIN_BAD_CREDENTIALS "
        "(not a distinguishable 'no such user' code -- anti-enumeration)");

    /* ---- 4. THE FAIL-OPEN CASES -- the actual security-critical
     * surface, real classic Unix footguns explicitly NOT replicated. ---- */
    pw = 0;
    rc = attempt_login(pw_ok, "locked", "anything at all", &pw);
    check(rc == LOGIN_BAD_CREDENTIALS,
        "attempt_login: '*'-locked account rejects EVERY password, including a guess");
    pw = 0;
    rc = attempt_login(pw_ok, "bangbang", "anything at all", &pw);
    check(rc == LOGIN_BAD_CREDENTIALS,
        "attempt_login: '!'-locked account rejects every password");
    pw = 0;
    rc = attempt_login(pw_ok, "empty", "", &pw);
    check(rc == LOGIN_BAD_CREDENTIALS,
        "attempt_login: an EMPTY stored password field + an EMPTY typed password "
        "STILL rejects -- the single most dangerous classic Unix fail-open shape "
        "(some historic crypt()-based systems treat this as 'no password required') "
        "is explicitly NOT reproduced here");
    pw = 0;
    rc = attempt_login(pw_ok, "empty", "some random guess", &pw);
    check(rc == LOGIN_BAD_CREDENTIALS,
        "attempt_login: an empty stored password field also rejects a NON-empty guess");
    pw = 0;
    rc = attempt_login(pw_ok, "alice", "", &pw);
    check(rc == LOGIN_BAD_CREDENTIALS,
        "attempt_login: an empty PASSWORD ATTEMPT against a real account with a real "
        "hash is rejected (not e.g. treated as 'skip the check')");

    /* ---- 5. Bootstrap vs. corruption -- must NOT be confused with each
     * other (see attempt_login()'s own comment on why this distinction
     * matters). ---- */
    pw = 0;
    rc = attempt_login(pw_missing, "alice", "swordfish", &pw);
    check(rc == LOGIN_NO_ACCOUNTS,
        "attempt_login: a genuinely nonexistent master.passwd path returns LOGIN_NO_ACCOUNTS "
        "(the real bootstrap case), not LOGIN_AUTH_BROKEN or a crash");
    check(pw == 0, "attempt_login: LOGIN_NO_ACCOUNTS also leaves *out_pw NULL");

    {
        const char *pw_corrupt = "/tmp/init_test_corrupt.passwd";
        /* A file that EXISTS but is not readable at all (permission-
         * denied) -- simulates a real I/O error distinct from "just
         * doesn't exist". This is the exact case that caught a REAL
         * fail-open bug while writing this test (see attempt_login()'s
         * own comment in init.c): an earlier version of attempt_login()
         * treated this the SAME as LOGIN_NO_ACCOUNTS (silently falling
         * back to an implicit root shell for a damaged/inaccessible
         * account database) -- fixed to check the real errno (ENOENT
         * specifically means bootstrap; anything else is
         * LOGIN_AUTH_BROKEN, refuse all logins). This check requires NOT
         * running as root (root ignores permission bits, so chmod 0000
         * wouldn't actually block the open()) -- skip cleanly rather than
         * giving a misleading result if that's ever not true. */
        write_file(pw_corrupt, "root:*:0:0::0:0:root:/var/root:/bin/sh\n");
        chmod(pw_corrupt, 0000);
        if (geteuid() == 0) {
            printf("skip: permission-denied fail-open regression test (running as root, "
                "chmod 0000 doesn't block root)\n");
        } else {
            pw = 0;
            rc = attempt_login(pw_corrupt, "root", "whatever", &pw);
            check(rc == LOGIN_AUTH_BROKEN,
                "REGRESSION: attempt_login() on an existing-but-permission-denied "
                "master.passwd returns LOGIN_AUTH_BROKEN (refuse all logins), "
                "NOT LOGIN_NO_ACCOUNTS (which would silently fall back to an "
                "implicit root shell -- a real fail-open bug caught by this exact test)");
            check(pw == 0, "LOGIN_AUTH_BROKEN also leaves *out_pw NULL");
        }
        chmod(pw_corrupt, 0644);
        unlink(pw_corrupt);
    }

    unlink(pw_ok);

    if (g_failures == 0) {
        printf("init.c login host_test: ALL CHECKS PASSED\n");
        return 0;
    } else {
        printf("init.c login host_test: %d CHECK(S) FAILED\n", g_failures);
        return 1;
    }
}
