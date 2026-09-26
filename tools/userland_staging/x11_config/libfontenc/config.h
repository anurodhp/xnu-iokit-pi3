/* libfontenc-1.1.9 config.h for the target build (DAR-387, build_libfontenc.sh).
 *   HAVE_REALLOCARRAY  left undefined: configure.ac:71
 *                      AC_REPLACE_FUNCS([reallocarray]) builds the library's
 *                      own src/reallocarray.c instead, the same choice the
 *                      host build makes (fontstack_config/mkfontscale/config.h)
 * No other define is read by encparse.c or fontenc.c. FONT_ENCODINGS_DIRECTORY
 * comes from src/Makefile.am's FONTENCDEFS and is passed on the compile line. */
