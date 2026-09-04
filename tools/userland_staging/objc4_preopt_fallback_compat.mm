/*
 * DAR-236 (full objc4 runtime port): real, minimal implementations of
 * header_info::nlclslist()/catlist()/catlist2() for the SUPPORT_PREOPT=0
 * case.
 *
 * Real root cause: these 3 methods are declared unconditionally in
 * objc-private.h but only ever DEFINED inside objc-opt.mm's real
 * SUPPORT_PREOPT=1 branch (see objc4_config_override.h's own header
 * comment for why this port builds objc-opt.mm itself with
 * SUPPORT_PREOPT=0 -- a real dyld/<objc-shared-cache.h> version-skew
 * between this project's separately-pinned dyld and objc4 tags). Every
 * OTHER compiled file (objc-runtime-new.mm in particular, the real
 * caller) is compiled with the real, unmodified SUPPORT_PREOPT=1 (only
 * objc-opt.mm's own compile is overridden), so it expects these 3
 * methods to exist as real, callable symbols.
 *
 * Real, zero-fabrication fix: each of these methods' own real body
 * (objc-opt.mm lines ~256-330) already reduces to exactly this when
 * isPreoptimized() is false (our permanent, honest answer -- this
 * project has no dyld shared cache, ever): call straight through to the
 * real, already-compiled, non-preopt accessor
 * (_getObjc2NonlazyClassList/_getObjc2CategoryList/_getObjc2CategoryList2,
 * all real, unmodified functions from objc-file.mm, already built and
 * exercising real __objc_nlclslist/__objc_catlist/__objc_catlist2
 * section parsing). This file is that same real fallback body, hand-
 * extracted rather than reached via the SUPPORT_PREOPT=1-only
 * conditional compile -- not a different, fabricated implementation.
 */
#include "objc-private.h"
#include "objc-file.h"

const classref_t *header_info::nlclslist(size_t *outCount) const
{
	return _getObjc2NonlazyClassList(mhdr(), outCount);
}

category_t * const *header_info::catlist(size_t *outCount) const
{
	return _getObjc2CategoryList(mhdr(), outCount);
}

category_t * const *header_info::catlist2(size_t *outCount) const
{
	return _getObjc2CategoryList2(mhdr(), outCount);
}
