/* config-paths.h for Window Maker 0.96.0, DAR-424. The top-level
 * Makefile.am:10-34 generates this file from make variables. Here it is
 * checked in with --prefix=/usr/X11 (this port's X11 layout, see
 * x11_common.sh):
 *   datadir    = /usr/X11/share
 *   sysconfdir = /usr/X11/etc
 *   pkgconfdir = ${sysconfdir}/${PACKAGE_TARNAME} (configure.ac:922)
 *   pixmapdir  = ${datadir}/pixmaps (configure.ac:873)
 */

/* define where the translations are stored */
#define LOCALEDIR "/usr/X11/share/locale"

/* define an extra path for pixmaps */
#define PIXMAPDIR "/usr/X11/share/pixmaps"

/* where shared data is stored */
#define PKGDATADIR "/usr/X11/share/WindowMaker"

/* where the global defaults are stored */
#define PKGCONFDIR "/usr/X11/etc/WindowMaker"

/* where the user data root is located */
#define GSUSER_SUBDIR "GNUstep"

/* where the user defaults are stored */
#define DEFAULTS_SUBDIR "Defaults"

/* where the user data are stored */
#define USERDATA_SUBDIR "Library"
