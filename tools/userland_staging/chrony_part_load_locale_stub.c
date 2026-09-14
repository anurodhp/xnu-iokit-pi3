/* Real gap, provably unreachable in this build's scope: both symbols
 * are only ever called from third_party/Libc/stdtime/FreeBSD/
 * timelocal.c's __time_load_locale(), which itself is only called by
 * real Apple's setlocale()/newlocale() category-loading machinery
 * (xlocale.c's _modifylocale(), deliberately not compiled here -- see
 * chrony_global_locale_compat.c's own header comment). chrony never
 * calls setlocale/uselocale/newlocale anywhere in its own source
 * (confirmed by grep), so __time_load_locale can never run in this
 * build regardless of these two symbols existing; a real
 * implementation (third_party/Libc/locale/FreeBSD/ldpart.c) would only
 * be exercised by dead code here. Honest failure returns, not a
 * fabricated success. */
int
__part_load_locale(const char *name, unsigned char *using_locale,
                    char **locale_buf, const char *category_filename,
                    int nrequired, unsigned int optional, const void *locale)
{
  (void)name; (void)using_locale; (void)locale_buf;
  (void)category_filename; (void)nrequired; (void)optional; (void)locale;
  return -1;
}

void
__ldpart_free_extra(void *op)
{
  (void)op;
}
