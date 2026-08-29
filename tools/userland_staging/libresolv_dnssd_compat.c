/* Real, honest "no mDNSResponder daemon available" stand-in for the 4
 * real DNS-SD client entry points res_query.c's res_query_mDNSResponder()
 * calls (DNSServiceQueryRecord/_ProcessResult/_RefDeallocate/_RefSockFD)
 * -- already documented as a genuine, deliberate scope exclusion in
 * build_libresolv.sh's own header (this project has no mDNSResponder
 * daemon, same class of decision as dnsinfo/notify/ASL) and in
 * build_libresolv_dylib.sh's own header (deferred via `-Wl,-U` there).
 *
 * That deferral doesn't actually work, though: real dyld's eager
 * chained-fixups binding fails at real process load time the instant
 * ANY code in the process references these symbols, regardless of
 * whether the call path ever executes (confirmed via a real QEMU boot
 * of launchd: "symbol not found in flat namespace
 * '_DNSServiceProcessResult'", pid 1 exited before main() ran at all).
 * Real fix, same honest-fallback shape as this project's own
 * dnsinfo_compat.c (`dns_configuration_copy()` returning NULL) and
 * voucher_mach_msg_set (`return FALSE`): these 4 functions are real,
 * locally-defined, and immediately/honestly report "service not
 * available" using the real public dns_sd.h API (types/constants
 * included directly from the real Apple SDK header already on this
 * project's include path -- not hand-guessed), rather than being left
 * undefined. res_query_mDNSResponder() (the only real caller) already
 * has a real, correct error path for exactly this return value -- see
 * res_query.c's own h_errno_for_dnssd_err(). */
#include <dns_sd.h>

DNSServiceErrorType DNSSD_API
DNSServiceQueryRecord(DNSServiceRef *sdRef, DNSServiceFlags flags,
    uint32_t interfaceIndex, const char *fullname, uint16_t rrtype,
    uint16_t rrclass, DNSServiceQueryRecordReply callBack, void *context)
{
	(void)flags;
	(void)interfaceIndex;
	(void)fullname;
	(void)rrtype;
	(void)rrclass;
	(void)callBack;
	(void)context;
	if (sdRef) {
		*sdRef = 0;
	}
	return kDNSServiceErr_Unknown;
}

DNSServiceErrorType DNSSD_API
DNSServiceProcessResult(DNSServiceRef sdRef)
{
	(void)sdRef;
	return kDNSServiceErr_Unknown;
}

void DNSSD_API
DNSServiceRefDeallocate(DNSServiceRef sdRef)
{
	(void)sdRef;
}

dnssd_sock_t DNSSD_API
DNSServiceRefSockFD(DNSServiceRef sdRef)
{
	(void)sdRef;
	return (dnssd_sock_t)-1;
}
