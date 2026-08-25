/* Real Apple __res_opcodes[] string table AND res_state_new() -- both
 * byte-for-byte copies of third_party/libresolv/res_data.c's own
 * definitions (that file's other content, not shown here). res_mkquery.c
 * unconditionally `#define DEBUG`s itself (its own source, line ~93,
 * "Options. Leave them on." --
 * not something this project's CFLAGS can turn off, since it's a plain
 * #define with no #ifndef guard), which unconditionally compiles a
 * `RES_DEBUG`-gated trace-print branch into res_9_nmkquery() referencing
 * this array -- dead at runtime for this project (nothing here ever
 * sets RES_DEBUG on a res_state), but still a real link-time dependency
 * of the compiled function body regardless of that runtime branch.
 *
 * The real array lives in res_data.c, but that file's OTHER public
 * entry points (res_query()/res_send()/res_sendsigned(), all real,
 * unmodified, and all compiled into libresolv.a -- see build_libresolv.sh)
 * call straight into res_query.c's res_9_nquery() and res_send.c's
 * res_9_nsend()/res_9_nsend_2(), which is where Apple's own res_query.c
 * separately defines res_query_mDNSResponder() (a real
 * DNSServiceQueryRecord/mDNSResponder-client code path) and res_send.c's
 * send_dg()/send_vc() pull in real getifaddrs(3)/notify(3)/kqueue(2)
 * dependencies. Real static linking (no dyld here, unlike Apple's own
 * libresolv.dylib, where two-level-namespace lazy binding means an
 * unused res_query_mDNSResponder() never needs those symbols resolved at
 * all) pulls in an entire referenced .o's undefined-symbol set the
 * moment ANY symbol from it is needed -- so merely wanting res_data.o's
 * __res_opcodes for this dead debug branch would otherwise force-couple
 * this smoketest to that much larger, genuinely-out-of-scope real
 * subsystem (see libresolv_smoketest.c's own header comment for the
 * full citation).
 *
 * Copying this one hidden-visibility (real Apple attribute, confirmed in
 * res_data.c: `__attribute__((__visibility__("hidden")))`) data table
 * into its own translation unit sidesteps that coupling without touching
 * res_mkquery.c or fabricating any resolver behavior -- it is the exact
 * same real strings Apple's own res_data.c defines, just given a second,
 * independent home so this tier's smoketest doesn't have to link
 * res_data.o to get them. libresolv.a's own res_data.o (compiled from
 * the real, complete, unmodified res_data.c) still separately defines
 * the real one for any consumer that goes through the public res_init()/
 * res_query()/res_send() -- this file changes nothing about that.
 *
 * res_state_new() is the exact same situation, one level deeper:
 * res_init.c's real res_build_start() (called unconditionally from
 * __res_vinit(), the internal entry point this tier's smoketest calls
 * directly -- see libresolv_smoketest.c's own comment on why) contains a
 * real `if (x == NULL) res = res_state_new();` call. Our smoketest always
 * passes a real non-NULL local res_state, so that branch never actually
 * executes -- but, same as __res_opcodes above, the reference is still
 * compiled into res_build_start()'s body and must resolve at real static-
 * link time regardless of which runtime branch fires. res_state_new()'s
 * real body (res_data.c) is a small, self-contained calloc()-based
 * allocator with zero DNS-network logic of its own -- copied verbatim
 * for the same reason and by the same reasoning as __res_opcodes above.
 */
const char *__res_opcodes[] = {
	"QUERY",
	"IQUERY",
	"CQUERYM",
	"CQUERYU",	/* experimental */
	"NOTIFY",	/* experimental */
	"UPDATE",
	"6",
	"7",
	"8",
	"9",
	"10",
	"11",
	"12",
	"13",
	"ZONEINIT",
	"ZONEREF",
};

#include <resolv.h>
#include "res_private.h"
#include <stdlib.h>

res_state
res_state_new(void)
{
	res_state x;

	x = (res_state)calloc(1, sizeof(struct __res_state));
	if (x == NULL) return NULL;

	/* We use _pad (normally unused) to hold a version number. We use it
	 * provide limited compatibility between versions. */
	x->_pad = 9;

	x->_u._ext.ext = (struct __res_state_ext *)calloc(1, sizeof(struct __res_state_ext));
	if (x->_u._ext.ext == NULL)
	{
		free(x);
		return NULL;
	}

	return x;
}
