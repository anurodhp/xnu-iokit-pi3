#include <stdint.h>

/* DAR-242 (2026-09-04): this header comment used to claim "this port has
 * no launchd at all -- the correct, honest answer... is always no". That
 * became factually wrong once real, dynamically-linked launchd.macho
 * became this project's real PID-1 (see CLAUDE.md) -- confirmed the hard
 * way when real configd.tproj called vproc_swap_integer(NULL,
 * VPROC_GSK_IS_MANAGED, ...), got this stub's unconditional 0 ("not
 * managed") even while genuinely running as a real launchd-spawned
 * com.apple.configd job, and took its own fork_child() self-daemonize
 * path -- a path real Apple's configd.m only takes for standalone,
 * non-launchd invocation -- whose forked child then SIGSEGVs. Real fix
 * for that class of bug is NOT here: libxpc.dylib's libvproc.o (real
 * PureDarwin source) already has a real, functioning implementation --
 * a genuine MIG round-trip (vproc_mig_swap_integer()) to bootstrap_port/
 * launchd, meaningful now that real launchd is real PID-1 -- restored as
 * a real export by DAR-235 earlier the same day. The actual fix for
 * cross-image consumers (configd.tproj, launchctl.macho) is a link-order
 * correction in their own build_*.sh (list libxpc.dylib before
 * libdyld.dylib, so ld64's two-level namespace binds the undefined
 * _vproc_swap_integer symbol to libxpc's real definition instead of this
 * stub) -- see build_configd.sh's and build_launchctl.sh's own DAR-242/
 * DAR-235 comments at their libxpc.dylib link line.
 *
 * This stub itself is intentionally left as-is (behavior unchanged): it
 * remains the correct fallback for any SAME-IMAGE caller compiled
 * directly into libdyld.dylib (e.g. dyld's own
 * LibSystemHelpers::isLaunchdOwned(), third_party/dyld/libdyld/
 * LibSystemHelpers.cpp) -- a same-image call binds directly to this
 * local definition regardless of any other dylib's link order, so no
 * link-order fix can reach it. Confirmed this call site is cosmetic
 * (dyld's own syslog-vs-stderr logging fallback,
 * third_party/dyld/dyld/DyldRuntimeState.cpp's setUpLogging()), not a
 * self-daemonization decision like configd.m's, so it does not exhibit
 * DAR-242's crash. Making dyld's OWN internal copy do a real MIG
 * round-trip would require dyld to link libxpc.dylib as a dependency of
 * itself/libdyld.dylib, which is a real, larger, separate architectural
 * question -- out of DAR-242's scope; not attempted here. */
void *vproc_swap_integer(void *vp, unsigned int key, int64_t *inval, int64_t *outval) {
    (void)vp;
    (void)key;
    (void)inval;
    if (outval) {
        *outval = 0;
    }
    return 0;
}
