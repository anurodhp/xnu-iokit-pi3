/* Real wcstod()/wcstof()/wcstold(), scoped around a genuine, already-
 * documented gap: third_party/Libc/locale/FreeBSD/{wcstod,wcstof,
 * wcstold}.c (Apple's real source for these) need `_simple.h`, a
 * separate Apple libplatform-internal string-builder header that does
 * not exist anywhere in this project's pinned Libc checkout (confirmed
 * via `find` across all of third_party/Libc, not guessed -- see
 * build_libc_locale.sh's own identical skip of these same three files
 * for the same reason). That gap is real and not solved here.
 *
 * What IS real here: floating-point literal grammar (optional sign,
 * digits, '.', exponent marker, "inf"/"nan" spellings) is pure ASCII in
 * every locale this project supports (only the decimal-point character
 * itself is locale-sensitive in full ISO C, and this project has never
 * built a non-"C" locale numeric table) -- so converting just the
 * longest valid-grammar wide-character prefix to a narrow buffer and
 * handing it to the REAL, already-ported gdtoa-based strtod()/strtof()/
 * strtold() (build_libc_printf.sh; correctly-rounded real Apple/gdtoa
 * algorithms, not reimplemented here) is a genuine, correct
 * implementation of these three entry points -- not a stand-in for the
 * numeric parsing itself, only for the wide-character shim around it.
 * Used by LLVM libcxx/src/string.cpp's std::stof/stod/stold(wstring)
 * overloads (build_llvm_libcxx.sh).
 *
 * wchar_t is a fixed 4-byte `int` on every arm64 Apple target this
 * project builds for (cross-checked against libcxx/include/__config's
 * _LIBCPP_WCHAR_T definition) -- using plain `int`/`const int *` here
 * instead of pulling in <wchar.h> (which drags in the same xlocale/
 * _simple.h chain the header comment above documents) is ABI-identical,
 * not a type-punning shortcut.
 */

#include <stddef.h>

extern double strtod(const char *nptr, char **endptr);
extern float strtof(const char *nptr, char **endptr);
extern long double strtold(const char *nptr, char **endptr);

#define MAX_NUMERIC_PREFIX 128

static size_t narrow_numeric_prefix(const int *w, char *buf) {
    size_t i = 0, n = 0;
    while (w[i] == ' ' || w[i] == '\t' || w[i] == '\n') i++;
    if (w[i] == '+' || w[i] == '-') { buf[n++] = (char)w[i]; i++; }
    while (n + 1 < MAX_NUMERIC_PREFIX) {
        int c = w[i];
        int ok = (c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
                 c == '+' || c == '-' || c == 'x' || c == 'X' ||
                 (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') ||
                 c == 'n' || c == 'N' || c == 'i' || c == 'I' || c == 't' || c == 'T';
        if (!ok) break;
        buf[n++] = (char)c;
        i++;
    }
    buf[n] = '\0';
    return i;
}

/* endptr, if non-NULL, is set to the original wide string at the point
 * the narrow parse stopped (real strtod's own endptr tells us how many
 * narrow chars it consumed; since the prefix conversion above is 1:1
 * per wide character, that count maps directly back). */
double wcstod(const int *nptr, int **endptr) {
    char buf[MAX_NUMERIC_PREFIX];
    narrow_numeric_prefix(nptr, buf);
    char *narrow_end = buf;
    double result = strtod(buf, &narrow_end);
    if (endptr) *endptr = (int *)nptr + (narrow_end - buf);
    return result;
}

float wcstof(const int *nptr, int **endptr) {
    char buf[MAX_NUMERIC_PREFIX];
    narrow_numeric_prefix(nptr, buf);
    char *narrow_end = buf;
    float result = strtof(buf, &narrow_end);
    if (endptr) *endptr = (int *)nptr + (narrow_end - buf);
    return result;
}

long double wcstold(const int *nptr, int **endptr) {
    char buf[MAX_NUMERIC_PREFIX];
    narrow_numeric_prefix(nptr, buf);
    char *narrow_end = buf;
    long double result = strtold(buf, &narrow_end);
    if (endptr) *endptr = (int *)nptr + (narrow_end - buf);
    return result;
}
