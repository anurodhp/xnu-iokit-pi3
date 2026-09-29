/* xdm-1.1.17 config.h. configure.ac (CA) lines cited (line numbers are
 * those of third_party/xdm/configure.ac at the xdm-1.1.17 tag). */
#include "../common_autoconf.h"
/* CA:XTRANS_CONNECTION_FLAGS (configure.ac "Find needed libraries for TCP
 * sockets"): UNIXCONN TCPCONN IPv6 BSD44SOCKETS HAVE_GETADDRINFO
 * HAVE_INET_NTOP HAVE_STRUCT_SOCKADDR_STORAGE, same results as libX11's. */
#include "../xtrans_flags.h"
#define PACKAGE_VERSION "1.1.17"
#define PACKAGE_STRING "xdm 1.1.17"

/* AC_CHECK_HEADERS([grp.h syslog.h sys/param.h]): all in the SDK. */
#define HAVE_GRP_H 1
#define HAVE_SYSLOG_H 1
#define HAVE_SYS_PARAM_H 1

/* AC_CHECK_FUNCS([daemon sigaction openlog asprintf setproctitle
 *   setusercontext mkstemp getifaddrs getspnam arc4random getaddrinfo
 *   inet_ntop]), checked against this tree's dylibs (nm -gU):
 *   sigaction, openlog, asprintf, mkstemp, arc4random: libsystem_c
 *   getifaddrs: libresolv          (exported)
 *   daemon: NOT exported by any dylib -> undefined, and daemon.c:49
 *     then uses xdm's own BecomeDaemon body;
 *   setproctitle, setusercontext, getspnam: not exported (no login_cap,
 *     no shadow database on Darwin) -> undefined. Without
 *     HAVE_SETUSERCONTEXT, session.c:598-624 does setgid + setlogin +
 *     initgroups + setuid, the same sequence Apple's login.c:872-884 does
 *     in its non-PAM __APPLE__ branch. Without HAVE_GETSPNAM,
 *     greeter/verify.c:236,340 uses getpwnam()'s pw_passwd, as
 *     login.c:514 does.
 *   getaddrinfo, inet_ntop: from xtrans_flags.h above. */
#define HAVE_SIGACTION 1
#define HAVE_OPENLOG 1
#define HAVE_ASPRINTF 1
#define HAVE_MKSTEMP 1
#define HAVE_GETIFADDRS 1
#define HAVE_ARC4RANDOM 1   /* CA:AC_SEARCH_LIBS([arc4random]) -> prngc.c not built */

/* PAM (CA:"--with-pam", USE_PAM=auto): the SDK has no security/pam_appl.h
 * and OpenPAM here has no service modules, so USE_PAM stays undefined.
 * SELinux, systemd, SECURE_RPC (needs rpc/key_prot.h, CA:key_setnet),
 * Kerberos (CA:AM_CONDITIONAL(HAS_KERBEROS_FIVE, false)): undefined. */

/* CA:AC_CHECK_FILE([/dev/urandom]) -> DEV_RANDOM. */
#define DEV_RANDOM "/dev/urandom"

/* CA:--enable-xdm-auth (auto): libXdmcp here exports XdmcpWrap
 * (x11_config/libXdmcp/config.h HASXDMAUTH), so HAS_XDM_AUTH. The
 * -DHASXDMAUTH the Makefile adds is passed by build_xdm.sh. */

/* CA:--with-xft (default yes if pkg-config finds xft xrender): libXft and
 * libXrender are built here, and the greeter then needs no core X fonts
 * (Login.c:169-178 vs :178-). CA:--with-xinerama: no libXinerama here. */
#define USE_XFT 1
/* CA:--enable-xpm-logos (default yes if xpm found): libXpm is built. */
#define XPM 1

/* Paths, --prefix=/usr/X11 with XDMLIBDIR=${libdir}/X11/xdm (CA:xdmlibdir)
 * and the AX_DEFINE_DIR defaults spelled out at CA:"Defaults for X
 * session". DEF_USER_PATH/DEF_SYSTEM_PATH: the CA `*)` (non-BSD-named)
 * case, since host_os is darwin. */
#define DEF_SERVER_LINE ":0 local /usr/X11/bin/X :0"
#define XRDB_PROGRAM "/usr/X11/bin/xrdb"
#define DEF_SESSION "/usr/X11/bin/xterm -ls"
#define DEF_USER_PATH "/bin:/usr/bin:/usr/X11/bin:/usr/ucb"
#define DEF_SYSTEM_PATH "/etc:/bin:/usr/bin:/usr/X11/bin:/usr/ucb"
#define DEF_SYSTEM_SHELL "/bin/sh"
#define DEF_FAILSAFE_CLIENT "/usr/X11/bin/xterm"
#define DEF_XDM_CONFIG "/usr/X11/lib/X11/xdm/xdm-config"
#define DEF_CHOOSER "/usr/X11/lib/X11/xdm/chooser"
#define DEF_AUTH_DIR "/var/lib/xdm"
#define DEF_GREETER_LIB "/usr/X11/lib/X11/xdm/libXdmGreet.so"
