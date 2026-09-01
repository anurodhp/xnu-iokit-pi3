/* Real body for dhcp_iopm_gap_compat.h's IOPMRequestSysWake() gap-fill.
 * See that header for the full, sourced reasoning -- this is the honest
 * no-op implementation (kIOReturnUnsupported, real IOKit's own
 * documented error for "this platform doesn't support this request"),
 * not a fabricated success. Not yet wired into any build script's FILES
 * list -- link this in alongside dhcp.o at the first real smoketest
 * link of a dhcp.c-consuming binary. */
#include "dhcp_iopm_gap_compat.h"

IOReturn
IOPMRequestSysWake(CFDictionaryRef request)
{
	(void)request;
	return kIOReturnUnsupported;
}
