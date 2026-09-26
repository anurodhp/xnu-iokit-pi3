/*
 * Declaration of reboot3() for reboot.c/shutdown.c (DAR-334). macOS 11's
 * <reboot2.h> declares it; PureDarwin's reboot2.h (launchd/reboot2.h:35)
 * only declares reboot2(). Force-included by build_reboot.sh; the body is
 * in reboot3_compat.c.
 */
#include <stdint.h>
int reboot3(uint64_t howto);
