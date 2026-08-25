/* Real smoketest for libc_build/libiconv.a (build_libiconv.sh) -- exercises
 * real iconv_open/iconv/iconv_close (not fabricated: this links directly
 * against Apple's real, unmodified lib/iconv.c, compiled straight from
 * third_party/libiconv, tag libiconv-59). Converts a real UTF-8 string
 * containing a non-ASCII character to ISO-8859-1, then converts a real
 * ASCII string through UTF-8 -> UTF-16LE -> UTF-8 round-trip, to prove
 * the real conversion-table/loop machinery (not just iconv_open()
 * returning successfully) actually runs.
 *
 * Same host-has-no-console-to-assert-through convention as
 * libc_regex_smoketest.c: prints PASS/FAIL markers via the
 * already-proven printf() path, QEMU boot log is the real verification
 * evidence.
 */
#include <iconv.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int convert(const char *tocode, const char *fromcode,
                    const char *in, size_t inlen,
                    char *out, size_t outcap, size_t *outlen) {
    iconv_t cd = iconv_open(tocode, fromcode);
    if (cd == (iconv_t)-1) {
        printf("iconv_open(%s, %s) FAILED: errno=%d\n", tocode, fromcode, errno);
        return 0;
    }
    char *inbuf = (char *)in;
    size_t inbytesleft = inlen;
    char *outbuf = out;
    size_t outbytesleft = outcap;
    size_t rc = iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
    iconv_close(cd);
    if (rc == (size_t)-1) {
        printf("iconv(%s -> %s) FAILED: errno=%d\n", fromcode, tocode, errno);
        return 0;
    }
    *outlen = outcap - outbytesleft;
    return 1;
}

int main(void) {
    int all_ok = 1;

    /* "caf\xc3\xa9" is UTF-8 for "café" (e-acute as 2-byte UTF-8
     * sequence 0xC3 0xA9); ISO-8859-1 encodes the same character as a
     * single byte 0xE9. A correct real conversion collapses 5 input
     * bytes to 4 output bytes with the last byte == 0xE9. */
    {
        const char utf8_cafe[] = { 'c', 'a', 'f', (char)0xC3, (char)0xA9 };
        char out[16];
        size_t outlen = 0;
        int ok = convert("ISO-8859-1", "UTF-8", utf8_cafe, sizeof(utf8_cafe), out, sizeof(out), &outlen);
        ok = ok && (outlen == 4) && (out[0] == 'c') && (out[1] == 'a') && (out[2] == 'f') && (out[3] == (char)0xE9);
        printf("UTF-8 -> ISO-8859-1 (\"caf\\xc3\\xa9\"): %s\n", ok ? "PASS" : "FAIL");
        all_ok &= ok;
    }

    /* Round-trip ASCII through UTF-16LE and back; must come back
     * byte-identical if the real conversion loops (not stubs) ran both
     * directions. */
    {
        const char ascii[] = "Hello, iconv!";
        char utf16[64];
        size_t utf16len = 0;
        int ok = convert("UTF-16LE", "UTF-8", ascii, strlen(ascii), utf16, sizeof(utf16), &utf16len);
        char back[64];
        size_t backlen = 0;
        ok = ok && convert("UTF-8", "UTF-16LE", utf16, utf16len, back, sizeof(back), &backlen);
        ok = ok && (backlen == strlen(ascii)) && (memcmp(back, ascii, backlen) == 0);
        printf("UTF-8 -> UTF-16LE -> UTF-8 round-trip (\"%s\"): %s\n", ascii, ok ? "PASS" : "FAIL");
        all_ok &= ok;
    }

    printf(all_ok ? "LIBICONV_SMOKETEST: ALL PASS\n" : "LIBICONV_SMOKETEST: FAIL\n");
    return all_ok ? 0 : 1;
}
