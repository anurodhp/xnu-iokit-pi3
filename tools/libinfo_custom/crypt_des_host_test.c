/*
 * Host-side test harness for crypt_des.c -- same host-test discipline as
 * libinfo_custom.c's host_test.c / init_binary's README-documented
 * pattern, but this file needs NO shim at all: crypt_des.c makes zero
 * syscalls (pure computation, deliberately -- see crypt_des.h), so it
 * compiles and runs directly as a normal host program.
 *
 * Verification strategy: cross-check against the HOST'S OWN real crypt()
 * (this dev machine is genuine macOS, so its libc crypt() is either this
 * exact Apple source or a closely-related descendant) for the traditional
 * 2-char-salt format, PLUS self-consistency checks (li_hash_password's
 * output must itself verify via li_crypt_verify, wrong passwords must
 * not, the documented fail-closed sentinels must all reject) that don't
 * depend on having a reference implementation available.
 *
 * Build: clang -Wall -Wextra -o crypt_des_host_test crypt_des_host_test.c \
 *          crypt_des.c
 * Run:   ./crypt_des_host_test
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h> /* host's own real crypt() -- cross-check reference only */

#include "crypt_des.h"

static int g_failures = 0;

static void check(int cond, const char *what) {
    if (cond) {
        printf("ok: %s\n", what);
    } else {
        printf("FAIL: %s\n", what);
        g_failures++;
    }
}

int main(void) {
    /* ---- 1. Cross-check against the host's own real crypt() for the
     * traditional (non-extended) format -- if this project's freestanding
     * port and the host's real libc crypt() ever disagree on the same
     * key+salt, that's a real algorithm bug in the port, not a style
     * nit. */
    {
        const char *host_result = crypt("abc", "ab");
        char *port_result = li_crypt("abc", "ab");
        printf("host   crypt(\"abc\",\"ab\") = %s\n", host_result ? host_result : "(null)");
        printf("port li_crypt(\"abc\",\"ab\") = %s\n", port_result ? port_result : "(null)");
        check(port_result != 0, "li_crypt returns non-NULL for a traditional 2-char salt");
        if (port_result && host_result) {
            check(strcmp(port_result, host_result) == 0,
                "li_crypt(\"abc\",\"ab\") EXACTLY matches the host's real libc crypt() -- "
                "confirms the DES port is byte-for-byte algorithmically correct, not just "
                "'looks plausible'");
        }
    }

    /* A second, different key+salt pair, same cross-check. */
    {
        const char *host_result = crypt("hunter2", "XY");
        char *port_result = li_crypt("hunter2", "XY");
        check(port_result != 0 && host_result != 0 &&
              strcmp(port_result, host_result) == 0,
            "li_crypt(\"hunter2\",\"XY\") matches host crypt() (second key/salt pair)");
    }

    /* Determinism: same key+salt must always produce the same hash. */
    {
        char *r1 = li_crypt("determinism", "zz");
        char first[32];
        strcpy(first, r1);
        char *r2 = li_crypt("determinism", "zz");
        check(strcmp(first, r2) == 0, "li_crypt is deterministic for the same key+salt");
    }

    /* Different salts for the same key must produce different hashes
     * (this is the entire point of salting). */
    {
        char *r1 = li_crypt("sameword", "aa");
        char first[32];
        strcpy(first, r1);
        char *r2 = li_crypt("sameword", "bb");
        check(strcmp(first, r2) != 0, "different salts produce different hashes for the same key");
    }

    /* ---- 2. Extended ("_") format: self-consistency (no host reference
     * needed -- host crypt() may or may not even support this format the
     * same way, so don't rely on it matching). ---- */
    {
        char *r1 = li_crypt("longer_extended_password_test", "_J9..zzzz");
        check(r1 != 0, "li_crypt with an extended-format setting returns non-NULL");
        if (r1) {
            check(r1[0] == '_', "extended-format result starts with '_'");
            check(strlen(r1) == 20, "extended-format result is exactly 20 chars "
                "(1 + 4 iter + 4 salt + 11 hash)");
        }
    }

    /* ---- 3. li_hash_password + li_crypt_verify round trip (the actual
     * API pwtool.c/init.c use). ---- */
    {
        char hash[32];
        int ok = li_hash_password("correct horse battery staple", hash, sizeof(hash));
        check(ok, "li_hash_password succeeds for a real password");
        check(hash[0] == '_', "li_hash_password always generates the stronger extended format, "
            "never the weak 2-char-salt traditional format");
        check(li_crypt_verify("correct horse battery staple", hash) == 1,
            "li_crypt_verify accepts the correct password against its own freshly-generated hash");
        check(li_crypt_verify("wrong password", hash) == 0,
            "li_crypt_verify rejects an incorrect password");
        check(li_crypt_verify("correct horse battery staple ", hash) == 0,
            "li_crypt_verify rejects a near-miss (trailing space) password");
    }

    /* Two separate calls must produce different salts (weak RNG, but must
     * not be a CONSTANT -- that would make every account's hash trivially
     * linkable/comparable even without cracking anything). */
    {
        char h1[32], h2[32];
        li_hash_password("same password twice", h1, sizeof(h1));
        li_hash_password("same password twice", h2, sizeof(h2));
        check(strcmp(h1, h2) != 0,
            "li_hash_password produces a different hash each call for the same password "
            "(salt is not a hardcoded constant)");
    }

    /* ---- 4. Fail-closed sentinels -- the actual security-critical
     * surface this whole file exists to get right (see crypt_des.h's own
     * extensive comment on why these specific cases matter). ---- */
    check(li_crypt_verify("anything", "") == 0,
        "li_crypt_verify: empty stored hash is REJECT (not 'no password required') "
        "-- the classic fail-open bug class this file explicitly does not replicate");
    check(li_crypt_verify("", "") == 0,
        "li_crypt_verify: empty password AND empty stored hash together still REJECT "
        "(the most dangerous fail-open shape -- must not silently succeed)");
    check(li_crypt_verify("", "_J9..zzzzzzzzzzzzzzzz") == 0,
        "li_crypt_verify: empty password against a real-looking hash is REJECT");
    check(li_crypt_verify("anything", 0) == 0,
        "li_crypt_verify: NULL stored hash is REJECT, not a crash");
    check(li_crypt_verify(0, "somehash") == 0,
        "li_crypt_verify: NULL password is REJECT, not a crash");
    check(li_crypt_verify("anything", "*") == 0,
        "li_crypt_verify: '*' locked-account sentinel is REJECT");
    check(li_crypt_verify("anything", "!") == 0,
        "li_crypt_verify: '!' locked-account sentinel is REJECT");
    check(li_hash_password("", (char[32]){0}, 32) == 0,
        "li_hash_password refuses to hash an empty password at all (can't create a "
        "no-password account through this API even by accident)");
    {
        char tiny[5];
        check(li_hash_password("password", tiny, sizeof(tiny)) == 0,
            "li_hash_password refuses an output buffer too small to hold a real hash "
            "(no silent truncation)");
    }

    if (g_failures == 0) {
        printf("crypt_des host_test: ALL CHECKS PASSED\n");
        return 0;
    } else {
        printf("crypt_des host_test: %d CHECK(S) FAILED\n", g_failures);
        return 1;
    }
}
