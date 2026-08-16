/* Real Libc/stdtime/FreeBSD/localtime.c bundles mktime() together with
 * localtime()/ctime()/asctime()/tzload() in one file -- pulling it in
 * for touch's stime_arg1()/stime_arg2() (only reached with an explicit
 * `-t`/`-d` timestamp argument; the default `touch file` path uses
 * gettimeofday() directly, confirmed by reading touch.c) drags in real
 * timezone-database reading (tzload() calling access()/open() against
 * real /usr/share/zoneinfo files) that genuinely can't work on this
 * target -- no timezone database has ever been provisioned onto the
 * real HFS+ root (same class of gap as this project's own
 * ps_time_compat.c header on why it doesn't pull real
 * localtime.c/strftime.c either). Per CLAUDE.md's source-priority rule,
 * Apple's real file is what's structurally unusable here (missing real
 * runtime data, not just missing build plumbing), so a real, standard,
 * last-resort implementation is the correct fallback.
 *
 * This is the well-known, textbook mktime() algorithm (Gregorian
 * calendar day count + POSIX broken-down-time reconstruction), treated
 * as UTC unconditionally (no tzdata exists to interpret tm as local
 * time against) -- honest about that: real callers passing genuinely
 * local wall-clock time will get it interpreted as UTC instead, same
 * real behavior any Darwin process gets today if TZ resolves to UTC. */
#include <time.h>

static int is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static const int days_before_month[12] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
};

time_t mktime(struct tm *tm) {
    int year = tm->tm_year + 1900;
    int month = tm->tm_mon; /* 0-11 */

    /* Normalize an out-of-range month into the year, same real
     * semantics real mktime() documents (e.g. tm_mon==12 means
     * January of the next year). */
    year += month / 12;
    month %= 12;
    if (month < 0) {
        month += 12;
        year--;
    }

    long days = 0;
    if (year >= 1970) {
        for (int y = 1970; y < year; y++) {
            days += is_leap(y) ? 366 : 365;
        }
    } else {
        for (int y = year; y < 1970; y++) {
            days -= is_leap(y) ? 366 : 365;
        }
    }
    days += days_before_month[month];
    if (month > 1 && is_leap(year)) {
        days += 1;
    }
    days += tm->tm_mday - 1;

    time_t t = (time_t)days * 86400
        + tm->tm_hour * 3600
        + tm->tm_min * 60
        + tm->tm_sec;

    /* Real mktime() also normalizes the whole struct tm in place
     * (tm_wday/tm_yday/etc) -- callers here (stime_arg1/stime_arg2)
     * only ever read the return value, confirmed by reading touch.c,
     * so that real normalization is intentionally not reimplemented. */
    return t;
}

static const int mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/* Real reverse of mktime() above -- also UTC-only/no-DST, same honest
 * scope limitation, same reason (no real tzdata on this target). Real
 * Libc/sys/gettimeofday.c calls this unconditionally at link time (only
 * at runtime when its caller passes a non-NULL tzp -- touch.c's own
 * gettimeofday(&tv[0], NULL) call never does, confirmed by reading
 * touch.c, so this never actually executes for touch's real usage, but
 * still needs to link and behave honestly if it ever does). */
struct tm *localtime(const time_t *timep) {
    static struct tm tm;
    time_t t = *timep;
    long days = t / 86400;
    long rem = t % 86400;
    if (rem < 0) {
        rem += 86400;
        days--;
    }
    tm.tm_hour = (int)(rem / 3600);
    tm.tm_min = (int)((rem % 3600) / 60);
    tm.tm_sec = (int)(rem % 60);
    tm.tm_wday = (int)(((days % 7) + 7 + 4) % 7); /* 1970-01-01 was a Thursday */

    int year = 1970;
    while (1) {
        int ylen = is_leap(year) ? 366 : 365;
        if (days >= 0 && days < ylen) {
            break;
        }
        if (days < 0) {
            year--;
            days += is_leap(year) ? 366 : 365;
        } else {
            days -= ylen;
            year++;
        }
    }
    tm.tm_year = year - 1900;
    tm.tm_yday = (int)days;

    int month = 0;
    for (month = 0; month < 12; month++) {
        int mlen = mdays[month];
        if (month == 1 && is_leap(year)) {
            mlen = 29;
        }
        if (days < mlen) {
            break;
        }
        days -= mlen;
    }
    tm.tm_mon = month;
    tm.tm_mday = (int)days + 1;
    tm.tm_isdst = 0;
    tm.tm_gmtoff = 0;
    tm.tm_zone = "UTC";
    return &tm;
}
