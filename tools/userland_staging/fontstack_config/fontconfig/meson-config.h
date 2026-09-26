/* fontconfig-2.17.1 meson-config.h (DAR-384). Stands in for the file
 * meson.build:579 configure_file()s from its `conf` object; the source
 * tree's own meson-config.h.in (copied to config.h, meson.build:580)
 * includes this and then config-fixups.h, unchanged.
 *
 * Each define cites the meson.build line that sets it. The values are what
 * `meson setup` gave when its checks ran against this project's own Xcode
 * 12 iPhoneOS 14.4 toolchain and its own libc_build/system dylibs (each
 * has_function check links against those dylibs), with the options
 * build_fontconfig.sh documents: prefix /usr/X11, sysconfdir /etc,
 * localstatedir /var, default-fonts-dirs=/usr/share/fonts,
 * additional-fonts-dirs=no, xml-backend=expat, iconv/nls disabled.
 *
 * check_funcs (:114-144) that came back NO, and why:
 *   getopt, getopt_long -- no dylib here exports them yet (this tree
 *       compiles Libc's getopt.c into each command instead, e.g.
 *       build_configd.sh). The fc-* tools do the same; see
 *       build_fontconfig.sh, which passes -DHAVE_GETOPT(_LONG)=1 for the
 *       tools only. The library never calls getopt.
 *   readlink -- only libcopyfile.dylib exports it here (not its real
 *       owner, libsystem_kernel). Undefined, fccfg.c's
 *       FcConfigRealFilename skips resolving a symlinked config path and
 *       uses the path as given.
 *   lrand48, random_r -- fccache.c's FcRandom falls through to random(),
 *       which exists (HAVE_RANDOM).
 *   fstatvfs -- fcstat.c's FcFStatFs uses fstatfs instead (HAVE_FSTATFS).
 *   _mktemp_s, getexecname, dcgettext, gettext -- Windows/Solaris/libintl.
 * check_struct_members (:158-165): statfs f_flags/f_fstypename come back
 *   NO because their prefix includes <sys/vfs.h>, which Darwin lacks --
 *   the same result as a native macOS configure. st_mtim is Linux-only
 *   (Darwin has st_mtimespec).
 */
#pragma once
#define ALIGNOF_DOUBLE 8                        /* :171-174, :291-298 check_alignofs */
#define ALIGNOF_VOID_P 8                        /* :171-174, :291-298 */
#define CONFIGDIR "/etc/fonts/conf.d"           /* :409-412, :419 */
#define EXEEXT                                  /* :192-196 */
#define FC_CACHEDIR "/var/cache/fontconfig"     /* :382-390, :420 */
#define FC_DEFAULT_FONTS "\t<dir>/usr/share/fonts</dir>\n"  /* :327-350 */
#define FC_FONTPATH ""                          /* :355-379, additional-fonts-dirs=no */
#define FC_GPERF_SIZE_T unsigned                /* :432-462, host /usr/bin/gperf 3.0.3 */
#define FC_TEMPLATEDIR "/usr/X11/share/fontconfig/conf.avail"  /* :399-402, :421 */
#define FLEXIBLE_ARRAY_MEMBER /**/              /* :300-304 */
#define FONTCONFIG_PATH "/etc/fonts"            /* :404-407, :422 */
#undef FREETYPE_PCF_LONG_FAMILY_NAMES           /* :46-54, test does not compile vs FreeType 2.14.3 */
#define GETTEXT_PACKAGE "fontconfig"            /* :467 */
#define HAVE_DIRENT_H 1                         /* :92-112 check_headers, :239-245 */
#define HAVE_DLFCN_H 1
#define HAVE_FCNTL_H 1
#define HAVE_FSTATFS 1                          /* :114-144 check_funcs, :247-256 */
#define HAVE_FT_DONE_MM_VAR 1                   /* :146-152 check_freetype_funcs */
#define HAVE_FT_GET_BDF_PROPERTY 1
#define HAVE_FT_GET_PS_FONT_INFO 1
#define HAVE_FT_GET_X11_FONT_FORMAT 1
#define HAVE_FT_HAS_PS_GLYPH_NAMES 1
#define HAVE_GETPAGESIZE 1
#define HAVE_GETPID 1
#define HAVE_GETPROGNAME 1
#define HAVE_INTEL_ATOMIC_PRIMITIVES 1          /* :314-316 */
#define HAVE_INTTYPES_H 1
#define HAVE_LINK 1
#define HAVE_LOCALTIME_R 1
#define HAVE_LSTAT 1
#define HAVE_MKDTEMP 1
#define HAVE_MKOSTEMP 1
#define HAVE_MKSTEMP 1
#define HAVE_MMAP 1
#define HAVE_PTHREAD 1                          /* :392-395 */
#define HAVE_PTHREAD_PRIO_INHERIT 1             /* :306-308 */
#define HAVE_RAND 1
#define HAVE_RANDOM 1
#define HAVE_RAND_R 1
#define HAVE_STDATOMIC_PRIMITIVES 1             /* :310-312 */
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRERROR 1
#define HAVE_STRERROR_R 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_STRUCT_DIRENT_D_TYPE 1             /* :158-165 check_struct_members */
#define HAVE_SYS_MOUNT_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_STATVFS_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_TIME_H 1
#define HAVE_UNISTD_H 1
#define HAVE_VPRINTF 1
#define HAVE_VSNPRINTF 1
#define HAVE_VSPRINTF 1
#define HAVE_WCHAR_H 1
#define PACKAGE_BUGREPORT "https://gitlab.freedesktop.org/fontconfig/fontconfig/issues/new"  /* :184-189 */
#define PACKAGE_NAME "fontconfig"
#define PACKAGE_STRING "fontconfig 2.17.1"
#define PACKAGE_TARNAME "fontconfig"
#define PACKAGE_URL ""
#define PACKAGE_VERSION "2.17.1"
#define SIZEOF_VOID_P 8                         /* :167-169, :282-289 check_sizeofs */
#define USE_ICONV 0                             /* :226, -Diconv=disabled */
#define WORDS_BIGENDIAN 0                       /* :190 */
#define _GNU_SOURCE                             /* :465 */
