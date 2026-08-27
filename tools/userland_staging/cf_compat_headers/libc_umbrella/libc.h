/* Compat stub for <libc.h> -- a real macOS-only umbrella header
 * (aggregates many standard headers under one name) that Xcode 12's
 * iPhoneOS SDK never shipped. RunLoop.subproj/CFSocket.c is the only
 * file in this checkout that includes it; confirmed via a real trial
 * compile that re-including just the specific standard headers it
 * actually pulls symbols from is sufficient (0 errors) -- no need to
 * replicate the umbrella header's full real content.
 */
#ifndef __CF_COMPAT_LIBC_UMBRELLA_H__
#define __CF_COMPAT_LIBC_UMBRELLA_H__
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#endif
