/*
 * Results of xtrans-1.6.0's XTRANS_CONNECTION_FLAGS / XTRANS_TCP_FLAGS
 * (third_party/xtrans/xtrans.m4) for this target, shared by every library
 * whose configure.ac calls that macro: libX11 (configure.ac:98), libICE
 * (:54) and libSM (:58, its no-libuuid client-id fallback). DAR-383.
 * "exported" = a real export of a dylib under libc_build/system/.
 */
#ifndef X11_XTRANS_FLAGS_H
#define X11_XTRANS_FLAGS_H
#define UNIXCONN 1                       /* unixdef=yes on non-mingw */
#define TCPCONN 1                        /* default yes */
#define HAVE_GETADDRINFO 1               /* exported by libinfo.dylib */
#define HAVE_INET_NTOP 1                 /* exported by libresolv.dylib */
#define IPv6 1                           /* IPV6CONN defaults to the getaddrinfo result */
#define BSD44SOCKETS 1                   /* struct sockaddr_in has sin_len (SDK netinet/in.h) */
#define HAVE_SOCKLEN_T 1
#define HAVE_STRUCT_SOCKADDR_STORAGE 1
#define HAVE_STRCASECMP 1                /* exported by libsystem_c.dylib */
#define HAVE_STRLCPY 1                   /* exported by libsystem_c.dylib */
/* LOCALCONN: solaris only. HAVE_ABSTRACT_SOCKETS: never set by xtrans.m4.
 * struct msghdr has msg_control without _XOPEN_SOURCE (SDK sys/socket.h). */
#endif
