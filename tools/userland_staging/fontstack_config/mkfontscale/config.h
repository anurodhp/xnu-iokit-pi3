/* mkfontscale-1.2.3 + libfontenc-1.1.9 config.h for the HOST build of
 * mkfontscale that build_fonts.sh runs at image-build time (DAR-384).
 * Host = the macOS machine building the SD image, not the Pi.
 *   PACKAGE_STRING   mkfontscale configure.ac:25 AC_INIT([mkfontscale], [1.2.3])
 *                    (printed by `mkfontscale -v`, mkfontscale.c:266)
 *   HAVE_VASPRINTF   mkfontscale configure.ac:55 AC_CHECK_FUNCS([vasprintf]);
 *                    macOS libc has it (list.c:70)
 *   X_BZIP2_FONT_COMPRESSION  left undefined: configure.ac:41-49's
 *                    --with-bzip2 defaults to no
 *   HAVE_REALLOCARRAY left undefined, so libfontenc uses its own
 *                    src/reallocarray.c (built as its LTLIBOBJS
 *                    replacement), which keeps the output independent of
 *                    the host's libc
 * (libfontenc configure.ac:71 AC_REPLACE_FUNCS([reallocarray]).)
 * libfontenc's configure.ac defines nothing else a source file reads;
 * FONT_ENCODINGS_DIRECTORY comes from its src/Makefile.am FONTENCDEFS and
 * is passed on the compile line. Both configure.ac files also run the
 * AC_USE_SYSTEM_EXTENSIONS/autoconf header probes, whose results no
 * mkfontscale/libfontenc source reads. */
#define PACKAGE_STRING "mkfontscale 1.2.3"
#define HAVE_VASPRINTF 1
