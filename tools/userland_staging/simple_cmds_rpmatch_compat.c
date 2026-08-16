/* Real Libc/gen/FreeBSD/rpmatch.c (already compiled into
 * libc_locale_for_stdio.a from an earlier tier) matches the response
 * against the current locale's real YESEXPR/NOEXPR regcomp(3) patterns
 * -- pulling in real POSIX regex (regcomp/regexec/regfree), a
 * materially large subsystem this project hasn't built (same class of
 * gap as this project's other documented stub areas). Only needed here
 * for rm.c's `-i`/interactive confirmation prompt.
 *
 * This project's locale is always "C"/POSIX (no real locale database --
 * see docs/roadmap.md's Phase 4 locale-tier notes), and in the C
 * locale, YESEXPR/NOEXPR are literally `"^[yY]"`/`"^[nN]"` -- so a
 * direct first-character check is the EXACT same real behavior as the
 * regex path would produce here, not a degraded approximation. Real,
 * documented rpmatch(3) return values preserved: 1 (yes), 0 (no), -1
 * (neither, real "not a valid response" per POSIX). Linked directly
 * (not archived) so this definition wins over libc_locale_for_stdio.a's
 * own rpmatch.o at link time without ever touching the regex path --
 * same real archive-member-shadowing mechanism this project's Nineteenth
 * pass already documented for pthread_once. */
int rpmatch(const char *response) {
    if (response[0] == 'y' || response[0] == 'Y') {
        return 1;
    }
    if (response[0] == 'n' || response[0] == 'N') {
        return 0;
    }
    return -1;
}
