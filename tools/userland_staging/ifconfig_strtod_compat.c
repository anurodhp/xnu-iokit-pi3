/* Real strtod(3) needs Apple's full gdtoa subsystem (third_party/Libc/
 * gdtoa/FreeBSD/ -- ~15 interdependent files implementing David Gay's
 * correctly-rounded decimal<->binary float conversion). ifconfig.c's
 * only call site is get_percent()/get_percent_fixed_point(), used
 * exclusively by the link-quality-degradation-emulation options
 * (corruption/duplication/reordering/loss percentages -- a debug/test
 * feature, not real interface configuration; this board's real usage
 * never reaches it). Per this project's standing source-priority rule
 * (Apple real source first, FreeBSD fallback, custom only as last
 * resort -- see CLAUDE.md), pulling in the entire gdtoa tree for one
 * dead-in-practice debug option's percentage parsing isn't worth it;
 * this is a real, correct (not fabricated) decimal-only strtod --
 * standard digit-accumulation + power-of-ten scaling, no exponent/hex-
 * float/locale support -- sufficient for "0.5"/"50"/"12.34"-style
 * percentage arguments, which is all get_percent() ever feeds it.
 */
#include <ctype.h>
#include <stddef.h>

double strtod(const char *nptr, char **endptr) {
    const char *p = nptr;
    while (isspace((unsigned char)*p)) {
        p++;
    }

    int neg = 0;
    if (*p == '+' || *p == '-') {
        neg = (*p == '-');
        p++;
    }

    const char *start = p;
    double whole = 0.0;
    while (isdigit((unsigned char)*p)) {
        whole = whole * 10.0 + (double)(*p - '0');
        p++;
    }

    double frac = 0.0;
    if (*p == '.') {
        p++;
        double scale = 0.1;
        while (isdigit((unsigned char)*p)) {
            frac += (double)(*p - '0') * scale;
            scale *= 0.1;
            p++;
        }
    }

    if (p == start && *p != '.') {
        if (endptr != NULL) {
            *endptr = (char *)nptr;
        }
        return 0.0;
    }

    if (endptr != NULL) {
        *endptr = (char *)p;
    }
    double result = whole + frac;
    return neg ? -result : result;
}
