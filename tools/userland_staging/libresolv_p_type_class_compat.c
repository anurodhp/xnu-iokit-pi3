/* Real Apple p_type()/p_class() -- byte-for-byte copies of
 * third_party/libresolv/res_debug.c's own my_p_type_syms[]/
 * my_p_class_syms[] tables and sym_ntos()/p_type()/p_class() bodies
 * (that file's other, unrelated content not shown here).
 *
 * Same real reason as libresolv_res_data_compat.c's __res_opcodes/
 * res_state_new: res_mkquery.c's unconditional `#define DEBUG` compiles
 * a dead (RES_DEBUG never set on this project's res_state) trace-print
 * call to p_class()/p_type() into res_9_nmkquery()'s body regardless.
 * Both real functions live in res_debug.c, but pulling res_debug.o in
 * turns out to cascade much further than __res_opcodes/res_state_new
 * did: res_debug.c's own do_section()/p_fqname() (compiled into the same
 * .o, needed or not) reference ns_print.c's real res_9_ns_sprintrr(),
 * which itself needs ns_samedomain.c's res_9_ns_samename(),
 * dst_support.c's res_9_dst_s_dns_key_id() (which needs a real on-disk
 * DST key-file path resolver, res_9_dst_path(), a genuinely different
 * real subsystem -- private DNSSEC key storage, nothing to do with
 * sending a query), and net/FreeBSD/nsap_addr.c's inet_nsap_ntoa() --
 * and separately, p_type()'s STATIC typebuf plus this whole chain
 * dragged in gmtime() (stdtime/FreeBSD/localtime.c) via an unrelated
 * res_debug.c function in the same translation unit, which has its own
 * real timezone-database/pthread_rwlock/tzfile machinery. None of that
 * is reachable from any code path this tier's smoketest (or, for that
 * matter, real callers of res_mkquery()/res_send() who never enable
 * RES_DEBUG) ever actually exercises -- it is a pure by-product of
 * static linking pulling in whole .o files rather than functions (see
 * libresolv_smoketest.c's header comment for the same phenomenon with
 * res_data.o/DNSServiceQueryRecord).
 *
 * Copying just these two small, real, self-contained formatter functions
 * (and the two small real tables they read) into their own translation
 * unit avoids that entire unrelated cascade without touching
 * res_mkquery.c or fabricating any DNS-resolution behavior -- p_type()/
 * p_class() have zero effect on how a query is built or sent, they only
 * format debug trace text nothing here ever prints. libresolv.a's own
 * res_debug.o (compiled from the real, complete, unmodified res_debug.c)
 * still separately defines the real ones for any consumer that actually
 * turns on RES_DEBUG -- this file changes nothing about that.
 */
#include "resolv.h"
#include <stdio.h>

static const struct res_sym my_p_type_syms[] = {
	{ns_t_a,	"A",		"address"},
	{ns_t_ns,	"NS",		"name server"},
	{ns_t_md,	"MD",		"mail destination (deprecated)"},
	{ns_t_mf,	"MF",		"mail forwarder (deprecated)"},
	{ns_t_cname,	"CNAME",	"canonical name"},
	{ns_t_soa,	"SOA",		"start of authority"},
	{ns_t_mb,	"MB",		"mailbox"},
	{ns_t_mg,	"MG",		"mail group member"},
	{ns_t_mr,	"MR",		"mail rename"},
	{ns_t_null,	"NULL",		"null"},
	{ns_t_wks,	"WKS",		"well-known service (deprecated)"},
	{ns_t_ptr,	"PTR",		"domain name pointer"},
	{ns_t_hinfo,	"HINFO",	"host information"},
	{ns_t_minfo,	"MINFO",	"mailbox information"},
	{ns_t_mx,	"MX",		"mail exchanger"},
	{ns_t_txt,	"TXT",		"text"},
	{ns_t_rp,	"RP",		"responsible person"},
	{ns_t_afsdb,	"AFSDB",	"DCE or AFS server"},
	{ns_t_x25,	"X25",		"X25 address"},
	{ns_t_isdn,	"ISDN",		"ISDN address"},
	{ns_t_rt,	"RT",		"router"},
	{ns_t_nsap,	"NSAP",		"nsap address"},
	{ns_t_nsap_ptr,	"NSAP_PTR",	"domain name pointer"},
	{ns_t_sig,	"SIG",		"signature"},
	{ns_t_key,	"KEY",		"key"},
	{ns_t_px,	"PX",		"mapping information"},
	{ns_t_gpos,	"GPOS",		"geographical position (withdrawn)"},
	{ns_t_aaaa,	"AAAA",		"IPv6 address"},
	{ns_t_loc,	"LOC",		"location"},
	{ns_t_nxt,	"NXT",		"next valid name (unimplemented)"},
	{ns_t_eid,	"EID",		"endpoint identifier (unimplemented)"},
	{ns_t_nimloc,	"NIMLOC",	"NIMROD locator (unimplemented)"},
	{ns_t_srv,	"SRV",		"server selection"},
	{ns_t_atma,	"ATMA",		"ATM address (unimplemented)"},
	{ns_t_tkey,	"TKEY",		"tkey"},
	{ns_t_tsig,	"TSIG",		"transaction signature"},
	{ns_t_ixfr,	"IXFR",		"incremental zone transfer"},
	{ns_t_axfr,	"AXFR",		"zone transfer"},
	{ns_t_zxfr,	"ZXFR",		"compressed zone transfer"},
	{ns_t_mailb,	"MAILB",	"mailbox-related data (deprecated)"},
	{ns_t_maila,	"MAILA",	"mail agent (deprecated)"},
	{ns_t_naptr,	"NAPTR",	"URN Naming Authority"},
	{ns_t_kx,	"KX",		"Key Exchange"},
	{ns_t_cert,	"CERT",		"Certificate"},
	{ns_t_a6,	"A6",		"IPv6 Address"},
	{ns_t_dname,	"DNAME",	"dname"},
	{ns_t_sink,	"SINK",		"Kitchen Sink (experimental)"},
	{ns_t_opt,	"OPT",		"EDNS Options"},
	{ns_t_any,	"ANY",		"\"any\""},
	{0, 		NULL,		NULL}
};

static const struct res_sym my_p_class_syms[] = {
	{ns_c_in,	"IN",		(char *)0},
	{ns_c_chaos,	"CHAOS",	(char *)0},
	{ns_c_hs,	"HS",		(char *)0},
	{ns_c_hs,	"HESIOD",	(char *)0},
	{ns_c_any,	"ANY",		(char *)0},
	{ns_c_none,	"NONE",		(char *)0},
	{ns_c_in,	(char *)0,	(char *)0}
};

static const char *
local_sym_ntos(const struct res_sym *syms, int number, int *success) {
	static char unname[20];

	for ((void)NULL; syms->name != 0; syms++) {
		if (number == syms->number) {
			if (success)
				*success = 1;
			return (syms->name);
		}
	}

	sprintf(unname, "%d", number);
	if (success)
		*success = 0;
	return (unname);
}

const char *
p_type(int type) {
	int success;
	const char *result;
	static char typebuf[20];

	result = local_sym_ntos(my_p_type_syms, type, &success);
	if (success)
		return (result);
	if (type < 0 || type > 0xfff)
		return ("BADTYPE");
	sprintf(typebuf, "TYPE%d", type);
	return (typebuf);
}

const char *
p_class(int class) {
	int success;
	const char *result;
	static char classbuf[20];

	result = local_sym_ntos(my_p_class_syms, class, &success);
	if (success)
		return (result);
	sprintf(classbuf, "CLASS%d", class);
	return (classbuf);
}
