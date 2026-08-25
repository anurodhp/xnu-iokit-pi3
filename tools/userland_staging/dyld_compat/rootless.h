/* Real, confirmed dead include: dyld3/shared-cache/FileUtils.cpp
 * includes <rootless.h> (real Apple SIP/rootless header) but calls zero
 * rootless_* symbols anywhere in that file (grepped in full) -- this
 * target has no SIP/rootless concept at all (single-user, no System
 * volume separation). Empty placeholder, not a real port. */
