/* Real ld2xpc()/xpc2ld() -- recursive launch_data_t <-> xpc_object_t
 * converters real launchd's own core.c calls (job_import_defaults(),
 * job_apply_defaults()) but never defines anywhere.
 *
 * Confirmed genuinely missing from every real Apple source this
 * project has access to, not just this project's own gap: checked
 * PureDarwin's own `next` branch (this project's actual source, via
 * `git ls-tree` across the whole repo, not just the sparse checkout)
 * AND a fresh clone of the real, historically-open-sourced
 * apple-oss-distributions/launchd tag `launchd-842.92.1` (the last
 * launchd Apple ever published source for) -- both call `ld2xpc`/
 * `xpc2ld` the exact same way, and neither defines them anywhere in
 * their own tree (`src/`, `liblaunch/`, `support/launchctl.c`). They
 * must come from a private, closed-source part of Apple's real libxpc
 * (never open-sourced, same class of gap as corecrypto) that real
 * launchd links against but this project's own PureDarwin-sourced
 * libxpc.a doesn't reimplement either.
 *
 * Real, from-scratch implementation (last resort per this project's
 * own source-priority convention, only after confirming no real Apple
 * source exists anywhere) -- a straightforward recursive mapping
 * between the two real, well-documented, structurally similar tagged-
 * union type systems (both real Apple public APIs, `launch.h`/
 * `xpc/xpc.h`, both already fully working in this project). Every real
 * `launch_data_type_t`/XPC type pair is handled:
 *   LAUNCH_DATA_DICTIONARY <-> XPC_TYPE_DICTIONARY
 *   LAUNCH_DATA_ARRAY      <-> XPC_TYPE_ARRAY
 *   LAUNCH_DATA_STRING     <-> XPC_TYPE_STRING
 *   LAUNCH_DATA_INTEGER    <-> XPC_TYPE_INT64
 *   LAUNCH_DATA_REAL       <-> XPC_TYPE_DOUBLE
 *   LAUNCH_DATA_BOOL       <-> XPC_TYPE_BOOL
 *   LAUNCH_DATA_OPAQUE     <-> XPC_TYPE_DATA
 * LAUNCH_DATA_FD/LAUNCH_DATA_ERRNO/LAUNCH_DATA_MACHPORT are real
 * launch_data_t leaf types with no natural XPC dictionary-value
 * equivalent (real Apple's own XPC type system has no analogous
 * standalone "raw fd"/"raw mach port" value type outside of an actual
 * live XPC connection/message) -- ld2xpc() maps all three to a plain
 * XPC_TYPE_INT64 carrying the raw fd/errno/port name, which is lossy
 * (the xpc2ld() direction can't tell which of the three it originally
 * was) but correct for this project's only two real call sites
 * (job_import_defaults()/job_apply_defaults(), which only ever
 * exercise plist-shaped Defaults dictionaries -- dict/array/string/
 * bool/integer/real -- confirmed via grep of real Defaults-plist
 * examples; fd/errno/machport values never appear in that data, so this
 * lossy fallback is never actually exercised on this target).
 */
#include <xpc/xpc.h>
#include <launch.h>
#include <stdlib.h>
#include <string.h>

static xpc_object_t ld2xpc_one(const launch_data_t ld);

struct ld2xpc_dict_ctx {
	xpc_object_t xd;
};

static void
ld2xpc_dict_iter(const launch_data_t lval, const char *key, void *ctxp)
{
	struct ld2xpc_dict_ctx *ctx = ctxp;
	xpc_object_t xv = ld2xpc_one(lval);
	if (xv) {
		xpc_dictionary_set_value(ctx->xd, key, xv);
		xpc_release(xv);
	}
}

static xpc_object_t
ld2xpc_one(const launch_data_t ld)
{
	if (!ld) {
		return NULL;
	}

	switch (launch_data_get_type(ld)) {
	case LAUNCH_DATA_DICTIONARY: {
		xpc_object_t xd = xpc_dictionary_create(NULL, NULL, 0);
		struct ld2xpc_dict_ctx ctx = { .xd = xd };
		launch_data_dict_iterate(ld, ld2xpc_dict_iter, &ctx);
		return xd;
	}
	case LAUNCH_DATA_ARRAY: {
		size_t count = launch_data_array_get_count(ld);
		xpc_object_t xa = xpc_array_create(NULL, 0);
		size_t i;
		for (i = 0; i < count; i++) {
			xpc_object_t xv = ld2xpc_one(launch_data_array_get_index(ld, i));
			if (xv) {
				xpc_array_set_value(xa, XPC_ARRAY_APPEND, xv);
				xpc_release(xv);
			}
		}
		return xa;
	}
	case LAUNCH_DATA_STRING:
		return xpc_string_create(launch_data_get_string(ld));
	case LAUNCH_DATA_INTEGER:
		return xpc_int64_create(launch_data_get_integer(ld));
	case LAUNCH_DATA_REAL:
		return xpc_double_create(launch_data_get_real(ld));
	case LAUNCH_DATA_BOOL:
		return xpc_bool_create(launch_data_get_bool(ld));
	case LAUNCH_DATA_OPAQUE:
		return xpc_data_create(launch_data_get_opaque(ld), launch_data_get_opaque_size(ld));
	case LAUNCH_DATA_FD:
		return xpc_int64_create(launch_data_get_fd(ld));
	case LAUNCH_DATA_ERRNO:
		return xpc_int64_create(launch_data_get_errno(ld));
	case LAUNCH_DATA_MACHPORT:
		return xpc_int64_create((int64_t)launch_data_get_machport(ld));
	default:
		return NULL;
	}
}

xpc_object_t
ld2xpc(launch_data_t pload)
{
	return ld2xpc_one(pload);
}

static launch_data_t xpc2ld_one(xpc_object_t xo);

static bool
xpc2ld_dict_applier(const char *key, xpc_object_t value, void *ctxp)
{
	launch_data_t ldict = ctxp;
	launch_data_t lval = xpc2ld_one(value);
	if (lval) {
		launch_data_dict_insert(ldict, lval, key);
	}
	return true;
}

static bool
xpc2ld_array_applier(size_t index, xpc_object_t value, void *ctxp)
{
	launch_data_t larray = ctxp;
	launch_data_t lval = xpc2ld_one(value);
	(void)index;
	if (lval) {
		launch_data_array_set_index(larray, lval, launch_data_array_get_count(larray));
	}
	return true;
}

static launch_data_t
xpc2ld_one(xpc_object_t xo)
{
	xpc_type_t t;

	if (!xo) {
		return NULL;
	}

	t = xpc_get_type(xo);
	if (t == XPC_TYPE_DICTIONARY) {
		launch_data_t ld = launch_data_alloc(LAUNCH_DATA_DICTIONARY);
		xpc_dictionary_apply(xo, ^(const char *key, xpc_object_t value) {
			return xpc2ld_dict_applier(key, value, ld);
		});
		return ld;
	} else if (t == XPC_TYPE_ARRAY) {
		launch_data_t ld = launch_data_alloc(LAUNCH_DATA_ARRAY);
		xpc_array_apply(xo, ^(size_t index, xpc_object_t value) {
			return xpc2ld_array_applier(index, value, ld);
		});
		return ld;
	} else if (t == XPC_TYPE_STRING) {
		return launch_data_new_string(xpc_string_get_string_ptr(xo));
	} else if (t == XPC_TYPE_INT64) {
		return launch_data_new_integer(xpc_int64_get_value(xo));
	} else if (t == XPC_TYPE_DOUBLE) {
		return launch_data_new_real(xpc_double_get_value(xo));
	} else if (t == XPC_TYPE_BOOL) {
		return launch_data_new_bool(xpc_bool_get_value(xo));
	} else if (t == XPC_TYPE_DATA) {
		return launch_data_new_opaque(xpc_data_get_bytes_ptr(xo), xpc_data_get_length(xo));
	}
	return NULL;
}

launch_data_t
xpc2ld(xpc_object_t xobj)
{
	return xpc2ld_one(xobj);
}
