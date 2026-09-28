/* cairo-1.18.4 config.h (DAR-384). Stands in for the file meson.build
 * configure_file()s from its `conf` object. Each define cites the
 * meson.build line that sets it; the values are what `meson setup` gave
 * when its checks ran against this project's own Xcode 12 iPhoneOS 14.4
 * toolchain and its own libc_build/system dylibs (has_function links
 * against them), with the options build_cairo.sh documents (xlib + png +
 * freetype + fontconfig on, zlib on since DAR-390 (PS/PDF/script surfaces;
 * its CAIRO_HAS_INTERPRETER define, :583-585, is read only by util/);
 * xcb, quartz, glib/gobject, tee, lzo,
 * spectre, symbol-lookup, tests off).
 *
 * check_funcs (:151-166) that came back NO, and what cairo does instead:
 *   drand48 -- only test/ and util/ use it.
 *   flockfile, funlockfile -- cairo-misc.c/cairo-output-stream.c fall back
 *       to plain stdio (the lock only matters for multi-threaded
 *       writers to one FILE).
 *   raise -- not exported by this libSystem; only test/ uses it.
 *   sched_getaffinity, clock_gettime (:126-128 check-funcs) --
 *       Linux-only / not exported here; cairo-time.c uses gettimeofday.
 *   feenableexcept/fedisableexcept/feclearexcept (:136) -- test/ only.
 * check_headers that came back NO: sys/int_types.h, byteswap.h, io.h,
 *   intsafe.h (Solaris/glibc/Windows).
 */
#pragma once
#define ATOMIC_OP_NEEDS_MEMORY_BARRIER 1        /* :770-772, AArch64 is not x86 */
#define CAIRO_HAS_DLSYM 1                       /* :207-216, dlsym in libdyld */
#define CAIRO_HAS_PTHREAD 1                     /* :715-731, -D_REENTRANT -lpthread links */
#define CAIRO_HAS_REAL_PTHREAD 1                /* :725 */
#define HAS_PIXMAN_GLYPHS 1                     /* :639 */
#define HAS_PIXMAN_r8g8b8_sRGB 1                /* :640-642, pixman 0.46.4 >= 0.42.3 */
#define HAVE_ALARM 1                            /* :151-166 check_funcs, :704-708 */
#define HAVE_ALLOCA_H 1                         /* :114-143 check_headers, :676-689 */
#define HAVE_C11_ATOMIC_PRIMITIVES 1            /* :738-739 */
#define HAVE_CTIME_R 1
#define HAVE_FCFINI 1                           /* :276-290, fc_check_funcs */
#define HAVE_FCINIT 1
#define HAVE_FCNTL_H 1
#define HAVE_FENV_H 1
#define HAVE_FORK 1
#define HAVE_FT_COLR_V1 1                       /* :340-342, FreeType 2.14.3 */
#define HAVE_FT_LOAD_NO_SVG 1                   /* :337-338 */
#define HAVE_FT_SVG_DOCUMENT 1                  /* :330-332 */
#define HAVE_GETLINE 1
#define HAVE_GMTIME_R 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIBGEN_H 1
#define HAVE_LINK 1
#define HAVE_LOCALTIME_R 1
#define HAVE_MKDIR 2                            /* :758-768, POSIX 2-argument mkdir */
#define HAVE_MMAP 1                             /* :127 check-funcs */
#define HAVE_NEWLOCALE 1
#define HAVE_POLL_H 1
#define HAVE_SCHED_H 1
#define HAVE_SETJMP_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_STDINT_H 1
#define HAVE_STRNDUP                            /* :711, set to a bool: defined empty */
#define HAVE_STRTOD_L 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_MMAN_H 1
#define HAVE_SYS_POLL_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_UN_H 1
#define HAVE_SYS_WAIT_H 1
#define HAVE_TERMIOS_H 1
#define HAVE_TIME_H 1
#define HAVE_UINT64_T 1                         /* :145-149 check_types, :691-702 */
#define HAVE_UNISTD_H 1
#define HAVE_WAITPID 1
#define HAVE_X11_EXTENSIONS_SHMPROTO_H 1        /* :358-363 xlib extra check_headers */
#define HAVE_X11_EXTENSIONS_SHMSTR_H 1
#define HAVE_X11_EXTENSIONS_XSHM_H 1
#define HAVE_XLOCALE_H 1
#define HAVE_XRENDERCREATECONICALGRADIENT 1     /* :395-398 xrender check_funcs */
#define HAVE_XRENDERCREATELINEARGRADIENT 1
#define HAVE_XRENDERCREATERADIALGRADIENT 1
#define HAVE_XRENDERCREATESOLIDFILL 1
/* HAVE___UINT128_T (:145-149 check_types) is left undefined although
 * meson finds the type: with it, cairo-wideint.c's 128-bit divides
 * compile to calls to compiler-rt's __udivti3/__umodti3, and no dylib
 * here exports those (Apple's live in libcompiler_rt.dylib, which this
 * project does not build). Without it cairo-wideint-private.h uses its
 * own portable 128-bit arithmetic, the path upstream uses on every
 * compiler without __uint128_t. HAVE_UINT64_T stays on. */
#define IPC_RMID_DEFERRED_RELEASE 0             /* :367-386, cross build: property defaults to false */
#define SIZEOF_INT 4                            /* :106-112 check_sizeofs, :667-674 */
#define SIZEOF_LONG 8
#define SIZEOF_LONG_LONG 8
#define SIZEOF_SIZE_T 8
#define SIZEOF_VOID_P 8
#define WARN_UNUSED_RESULT __attribute__((__warn_unused_result__))  /* :80-91 */
