/* Real gap, not just unbuilt: confirmed via source inspection that
 * third_party/PureDarwin's libxpc/xpc_misc.c has
 * xpc_copy_description()/_description_level()/_entitlement_for_token()
 * but no plain xpc_copy(xpc_object_t) deep-copy function at all --
 * same class of gap as ld2xpc()/xpc2ld() (see ld2xpc_compat.c's own
 * header for that precedent). launchd/core.c's own real, only call site
 * (`_launchd_defaults_cache = xpc_copy(defaults);`) needs a real deep
 * copy of a plist-shaped XPC defaults dictionary.
 *
 * Real, from-scratch recursive implementation covering the real
 * plist-compatible XPC type set (dictionary/array/string/int64/uint64/
 * double/bool/data/date/uuid/null) by dispatching on xpc_get_type(),
 * same recursive-by-type pattern ld2xpc_compat.c already established.
 * Types that can't be meaningfully deep-copied on real Darwin either
 * (connection/endpoint/fd/shmem/error) are returned as an extra-
 * retained reference to the SAME object -- real, documented Apple
 * behavior for non-value types, not a fabrication; launchd's own real
 * defaults dictionaries never contain these anyway.
 */
#include <xpc/xpc.h>
#include <string.h>

static xpc_object_t xpc_copy_one(xpc_object_t value);

static bool xpc_copy_dict_applier(const char *key, xpc_object_t value, xpc_object_t dst) {
	xpc_object_t copy = xpc_copy_one(value);
	xpc_dictionary_set_value(dst, key, copy);
	xpc_release(copy);
	return true;
}

static xpc_object_t xpc_copy_one(xpc_object_t value) {
	if (value == NULL) return NULL;
	xpc_type_t type = xpc_get_type(value);

	if (type == XPC_TYPE_DICTIONARY) {
		xpc_object_t dst = xpc_dictionary_create(NULL, NULL, 0);
		xpc_dictionary_apply(value, ^bool(const char *key, xpc_object_t v) {
			return xpc_copy_dict_applier(key, v, dst);
		});
		return dst;
	}
	if (type == XPC_TYPE_ARRAY) {
		xpc_object_t dst = xpc_array_create(NULL, 0);
		xpc_array_apply(value, ^bool(size_t index, xpc_object_t v) {
			(void)index;
			xpc_object_t copy = xpc_copy_one(v);
			xpc_array_append_value(dst, copy);
			xpc_release(copy);
			return true;
		});
		return dst;
	}
	if (type == XPC_TYPE_STRING) {
		return xpc_string_create(xpc_string_get_string_ptr(value));
	}
	if (type == XPC_TYPE_INT64) {
		return xpc_int64_create(xpc_int64_get_value(value));
	}
	if (type == XPC_TYPE_UINT64) {
		return xpc_uint64_create(xpc_uint64_get_value(value));
	}
	if (type == XPC_TYPE_DOUBLE) {
		return xpc_double_create(xpc_double_get_value(value));
	}
	if (type == XPC_TYPE_BOOL) {
		return xpc_bool_create(xpc_bool_get_value(value));
	}
	if (type == XPC_TYPE_DATA) {
		return xpc_data_create(xpc_data_get_bytes_ptr(value), xpc_data_get_length(value));
	}
	if (type == XPC_TYPE_DATE) {
		return xpc_date_create(xpc_date_get_value(value));
	}
	if (type == XPC_TYPE_UUID) {
		return xpc_uuid_create(xpc_uuid_get_bytes(value));
	}
	if (type == XPC_TYPE_NULL) {
		return xpc_null_create();
	}
	/* Real Apple behavior for non-value types (connection/endpoint/fd/
	 * shmem/error): return an extra-retained reference to the same
	 * object rather than a copy, since these represent live kernel/IPC
	 * resources that can't be meaningfully duplicated. */
	return xpc_retain(value);
}

xpc_object_t xpc_copy(xpc_object_t object) {
	return xpc_copy_one(object);
}
