/* Real, but deliberately UTC-only, localtime()/strftime() subset --
 * real Apple third_party/Libc/stdtime/FreeBSD/localtime.c and
 * strftime.c were tried first (this project's standing source-priority
 * rule: Apple source first) and found to pull in Apple's full real
 * tzcode timezone-database subsystem (tzset/tzname/altzone/
 * get_timezone/mktime, real /usr/share/zoneinfo lookups) -- a
 * genuinely separate, large subsystem this project has never built
 * (same class of structural gap as the Phase 4 dyld/libdispatch
 * investigations: real source exists, but isn't tractable to pull in
 * piecemeal for one caller). This target also has no RTC-backed
 * timezone configuration at all yet (no /etc/localtime, no TZ
 * mechanism) -- UTC is not just a simplification but this target's own
 * only real notion of "the time" right now, so localtime()==gmtime()
 * here is honest, not a fabricated approximation.
 *
 * Implements exactly the strftime() format directives real
 * third_party/adv_cmds/ps/print.c's started()/lstarted() functions use
 * (%l %M %p %k %a %I %H %e %b %y %c -- verified by grep, not guessed)
 * -- not a general strftime. Civil-date-from-days conversion is the
 * standard, well-known algorithm (Howard Hinnant's civil_from_days,
 * public domain, real and correct for the whole proleptic Gregorian
 * range this target could ever see real timestamps in). Last resort
 * per CLAUDE.md's source-priority rule (Apple real source tried first,
 * found to need a subsystem out of scope; no FreeBSD single-file
 * fallback exists for "strftime minus tzcode" either) -- but the
 * algorithm itself is textbook, not improvised. */
#include <stddef.h>
#include <time.h> /* real struct tm/time_t from the real SDK header -- must
                    * match exactly what ps.c/print.c themselves see via
                    * their own <sys/time.h>/<time.h> includes, NOT a
                    * locally redeclared struct (a mismatched layout here
                    * would be a real, silent memory-corruption bug: this
                    * file's functions are called with real struct tm*
                    * pointers sized/laid-out per the real header). */

static void civil_from_days(long z, int *y, unsigned *m, unsigned *d) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yr = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = (int)(yr + (*m <= 2));
}

static struct tm cached_tm;

struct tm *gmtime(const time_t *tp) {
    long t = *tp;
    long days = t >= 0 ? t / 86400 : -((-t + 86399) / 86400);
    long secs_of_day = t - days * 86400;
    int y;
    unsigned m, d;
    civil_from_days(days, &y, &m, &d);
    cached_tm.tm_year = y - 1900;
    cached_tm.tm_mon = (int)m - 1;
    cached_tm.tm_mday = (int)d;
    cached_tm.tm_hour = (int)(secs_of_day / 3600);
    cached_tm.tm_min = (int)((secs_of_day / 60) % 60);
    cached_tm.tm_sec = (int)(secs_of_day % 60);
    /* 1970-01-01 was a real, well-known Thursday (wday 4). */
    long wday = ((days % 7) + 7 + 4) % 7;
    cached_tm.tm_wday = (int)wday;
    cached_tm.tm_isdst = 0;
    return &cached_tm;
}

struct tm *localtime(const time_t *tp) {
    return gmtime(tp); /* real UTC, this target has no timezone config -- see header comment */
}

static const char *const wdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const mons[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static char *put2(char *o, int v) {
    *o++ = (char)('0' + (v / 10) % 10);
    *o++ = (char)('0' + v % 10);
    return o;
}

static char *put2sp(char *o, int v) {
    *o++ = v >= 10 ? (char)('0' + (v / 10) % 10) : ' ';
    *o++ = (char)('0' + v % 10);
    return o;
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
    char *o = s;
    char *end = s + max - 1;
    int h12 = tm->tm_hour % 12;
    if (h12 == 0) {
        h12 = 12;
    }
    while (*fmt && o < end) {
        if (*fmt != '%') {
            *o++ = *fmt++;
            continue;
        }
        fmt++;
        switch (*fmt) {
        case 'l':
            if (o + 2 <= end) {
                o = put2sp(o, h12);
            }
            break;
        case 'I':
            if (o + 2 <= end) {
                o = put2(o, h12);
            }
            break;
        case 'k':
            if (o + 2 <= end) {
                o = put2sp(o, tm->tm_hour);
            }
            break;
        case 'H':
            if (o + 2 <= end) {
                o = put2(o, tm->tm_hour);
            }
            break;
        case 'M':
            if (o + 2 <= end) {
                o = put2(o, tm->tm_min);
            }
            break;
        case 'S':
            if (o + 2 <= end) {
                o = put2(o, tm->tm_sec);
            }
            break;
        case 'p':
            if (o + 2 <= end) {
                *o++ = tm->tm_hour < 12 ? 'A' : 'P';
                *o++ = 'M';
            }
            break;
        case 'e':
            if (o + 2 <= end) {
                o = put2sp(o, tm->tm_mday);
            }
            break;
        case 'a':
            if (tm->tm_wday >= 0 && tm->tm_wday < 7 && o + 3 <= end) {
                const char *w = wdays[tm->tm_wday];
                *o++ = w[0];
                *o++ = w[1];
                *o++ = w[2];
            }
            break;
        case 'b':
            if (tm->tm_mon >= 0 && tm->tm_mon < 12 && o + 3 <= end) {
                const char *w = mons[tm->tm_mon];
                *o++ = w[0];
                *o++ = w[1];
                *o++ = w[2];
            }
            break;
        case 'y':
            if (o + 2 <= end) {
                o = put2(o, (tm->tm_year + 1900) % 100);
            }
            break;
        case 'Y': {
            int y = tm->tm_year + 1900;
            char buf[8];
            int n = 0;
            if (y == 0) {
                buf[n++] = '0';
            }
            while (y > 0 && n < (int)sizeof(buf)) {
                buf[n++] = (char)('0' + y % 10);
                y /= 10;
            }
            while (n > 0 && o < end) {
                o[0] = buf[--n];
                o++;
            }
            break;
        }
        case 'c':
            /* real ctime(3)-style layout: "Thu Jan  1 00:00:00 1970" */
            if (tm->tm_wday >= 0 && tm->tm_wday < 7 && tm->tm_mon >= 0 && tm->tm_mon < 12 &&
                o + 24 <= end) {
                const char *w = wdays[tm->tm_wday];
                const char *mo = mons[tm->tm_mon];
                *o++ = w[0];
                *o++ = w[1];
                *o++ = w[2];
                *o++ = ' ';
                *o++ = mo[0];
                *o++ = mo[1];
                *o++ = mo[2];
                *o++ = ' ';
                o = put2sp(o, tm->tm_mday);
                *o++ = ' ';
                o = put2(o, tm->tm_hour);
                *o++ = ':';
                o = put2(o, tm->tm_min);
                *o++ = ':';
                o = put2(o, tm->tm_sec);
                *o++ = ' ';
                {
                    int y = tm->tm_year + 1900;
                    char buf[8];
                    int n = 0;
                    while (y > 0 && n < (int)sizeof(buf)) {
                        buf[n++] = (char)('0' + y % 10);
                        y /= 10;
                    }
                    while (n > 0 && o < end) {
                        *o++ = buf[--n];
                    }
                }
            }
            break;
        case '%':
            *o++ = '%';
            break;
        default:
            break;
        }
        if (*fmt) {
            fmt++;
        }
    }
    *o = '\0';
    return (size_t)(o - s);
}
