/* DAR-343: pkill.c's `#include <jail.h>` (FreeBSD path). Darwin has no
 * jails; jail_getid() is only reached by pkill -j, and always fails so
 * that option reports "no such jail" instead of matching anything. */
#ifndef _PKILL_JAIL_H_
#define _PKILL_JAIL_H_
static inline int jail_getid(const char *name) { (void)name; return -1; }
#endif
