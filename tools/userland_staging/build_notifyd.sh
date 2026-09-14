#!/bin/bash
# Builds tools/userland_staging/notifyd.macho: DAR-167 -- the real
# notifyd SERVER daemon (apple-oss-distributions/Libnotify's notifyd/,
# real source at third_party/PureDarwin/src/Libraries/XPC/notify/, a
# byte-identical copy of third_party/Libnotify/notifyd/notifyd.c plus
# PureDarwin's own real compat glue for the one genuine gap this project
# still has, see below). build_libnotify.sh/build_libnotify_client.sh/
# build_libsystem_notify.sh already built the real CLIENT half
# (libsystem_notify.dylib, notify_register_*()/notify_post()); this
# script is the missing SERVER half those scripts' own header comments
# explicitly deferred ("this project also doesn't have [a live notifyd]
# yet, exactly as this task's own brief anticipated").
#
# ============================================================
# DAR-167's OWN CENTRAL QUESTION, ANSWERED FOR REAL (read this first)
# ============================================================
# DAR-167 asked whether real launchd's own bootstrap namespace (now
# running as PID 1, docs/launchd-pid1-postmortem.md) is already
# sufficient for notify_register_*() to resolve against a live notifyd,
# without a separate bootstrap-namespace registrar shim (the "fastest
# real path" identified back when this was scoped, before real launchd
# existed). Answer: YES, confirmed by reading real source, not assumed:
#
#   - Real Apple's own notifyd/com.apple.notifyd.plist
#     (third_party/Libnotify/notifyd/com.apple.notifyd.plist) advertises
#     exactly one Mach service via the standard launchd MachServices
#     mechanism: `MachServices => { com.apple.system.notification_center
#     => true }`. Real notifyd.c:847 calls
#     `bootstrap_check_in(bootstrap_port, name, &global.server_port)` to
#     claim it; real notify_client.c:894 calls
#     `bootstrap_look_up2(bootstrap_port, NOTIFY_SERVICE_NAME, ...)` to
#     find it. Nothing here is notifyd-specific -- it is the exact same
#     generic launchd MachServices check-in/look-up mechanism every
#     other real MachServices-advertising daemon in this project already
#     uses.
#   - This project's own real configd.macho (DAR-170, build_configd.sh)
#     is living proof this mechanism already works end-to-end under this
#     project's real launchd: its own LaunchDaemons plist
#     (third_party/configd/configd.tproj/com.apple.configd.plist)
#     advertises `com.apple.SystemConfiguration.configd` the same way,
#     configd.macho really `bootstrap_check_in()`s it via real
#     libxpc.dylib, and it boots as a real launchd job today.
#   - Confirmed directly by `nm -gU` on this project's own real, already-
#     built libxpc.dylib: `_bootstrap_check_in`, `_bootstrap_look_up2`,
#     `_bootstrap_look_up`, and `_bootstrap_register2` are all real,
#     already-exported symbols (backed by liblaunchd_mig.a's real
#     vproc_mig_* MIG client stubs to real launchd, build_launchd_mig.sh)
#     -- not stubs, not `-Wl,-U`-deferred gaps.
#
# So: no standalone bootstrap-namespace registrar shim is built or
# needed here. notifyd.macho check-in's directly against real launchd's
# real bootstrap namespace, exactly like real Apple's notifyd does, and
# exactly like this project's own configd.macho already does.
#
# ============================================================
# REAL FILE LIST (verified against PureDarwin's own real
# src/Libraries/XPC/notify/CMakeLists.txt `mig()`/`target_sources()`
# calls, not guessed -- that project's own real build recipe for this
# exact daemon, upstream and unmodified by this project)
# ============================================================
# Daemon-only sources (notifyd.macho's own CMake target): notifyd.c,
# notify_proc.c, pathwatch.c, service.c, pd_sandbox_check.c.
#
# Shared "notify core" sources (CMake's own XPC_notify_client_static,
# force_load'd directly into the notifyd EXECUTABLE, not merely linked
# as a dylib dependency -- see "WHY THIS PROJECT DOES NOT LINK
# libsystem_notify.dylib INTO notifyd.macho" below for why that force_load
# shape is preserved here): notify_client.c, libnotify.c, table.c,
# pd_xpc_event_publisher.c.
#
# Real MIG glue (CMake's own XPC_notifyd_mig_static): notify_ipc.defs'
# real generated notify_ipcUser.c + notify_ipcServer.c/.h (SERVER side
# this time -- build_libnotify.sh only ever generated the CLIENT side).
# PLUS one real MIG subsystem CMake's own recipe never generates at all
# (a genuine gap in PureDarwin's own upstream CMakeLists, confirmed by
# grepping that whole tree for "notifyServer"/"mach/notify.defs" --
# zero hits anywhere): notifyd.c's own `#include "notifyServer.h"` and
# its do_mach_notify_port_deleted/_destroyed/_no_senders/_send_once
# handlers are the real MIG SERVER side of xnu's own real
# osfmk/mach/notify.defs (the standard Mach dead-name/no-senders
# notification subsystem, subsystem 64, `serverprefix do_; serverdemux
# notify_server;`) -- confirmed real and already regenerated successfully
# by this project's own build_launchd_mig.sh (which leaves it
# UNcompiled, its own header comment says exactly why: "left as raw
# generated output ... for a future launchd build script to compile
# directly", i.e. this one). Regenerated fresh here from PureDarwin's own
# copy of that same real .defs file (third_party/PureDarwin/src/
# Libraries/XPC/launchd/notify.defs), for the same reason
# build_launchd_mig.sh used PureDarwin's copy rather than
# third_party/xnu-7195's own -- one real, already-proven-to-mig-clean
# source of truth for this project's whole launchd/notifyd MIG surface,
# not two independently-drifting ones.
#
# ============================================================
# WHY THIS PROJECT DOES NOT LINK libsystem_notify.dylib INTO
# notifyd.macho (a deliberate departure from configd.macho's own choice,
# not an oversight)
# ============================================================
# configd.macho links libsystem_notify.dylib because it only ever calls
# the small real PUBLIC client API (notify_post() et al) to announce
# store changes -- it never touches Libnotify's internal state engine.
# notifyd IS that internal state engine: `_notify_lib_init_locked()`
# marks itself `static` (notify_client.c:1017), so it is not even
# reachable through a dylib boundary, and PureDarwin's own real
# CMakeLists.txt force_load's the WHOLE client-core archive directly
# into the notifyd executable for exactly this reason (the real product
# split), not for any dyld-vintage reason of PureDarwin's own. Since
# notifyd.macho therefore already carries its own private, statically-
# compiled copy of notify_client.o/libnotify.o/table.o (real external
# symbols and all -- notify_post/notify_register_check/etc are genuinely
# defined a second time inside this image), ALSO linking
# libsystem_notify.dylib (directly, or transitively via libSystem.B.dylib's
# own real `-Wl,-reexport_library`) would recreate exactly the "two
# competing copies of one subsystem in one process" bug class
# docs/launchd-pid1-postmortem.md's Bug B and DAR-217 already root-caused
# and fixed for pthread/malloc -- confirmed empirically, not assumed: `nm
# -gU` on notify_client.o's own compiled output shows real, external,
# non-weak `_notify_post`/`_notify_register_check`/etc. definitions.
# Real fix, same shape as build_launchd.sh's own "remove the archive
# entirely" fix for its own duplicate-pthread bug: neither libSystem.B.dylib
# NOR libsystem_notify.dylib appears anywhere on this script's own link
# line below. notifyd.macho links the same bare per-library dylib set
# build_configd.sh/build_syslogd.sh already established for a real
# dynamic executable with exactly one copy of every subsystem
# (libsystem_kernel/_pthread/_malloc/_platform/_blocks/_c, libdispatch,
# libxpc, libbsm) -- confirmed clean via this script's own bind_target_audit.sh
# gate at the end, not just a clean link.
#
# UPDATE 2026-09-06 (DAR-167 boot investigation): the paragraph above's
# conclusion ("neither libSystem.B.dylib NOR libsystem_notify.dylib
# appears... on this link line") turned out to be one step too far. Real
# dyld hard-requires it, unconditionally, for every process on the
# platform: `dyldMain.cpp`'s own `prepare()` calls
# `halt("program does not link with libSystem.B.dylib")` whenever
# `state.libSystemLoader` is still null after loading every dependency --
# and that field is set by `RuntimeState::add()` matching a loaded
# image's install name literally against `/usr/lib/libSystem.B.dylib`,
# nothing else. A real QEMU boot confirmed it: notifyd.macho aborted at
# launch with exactly that message (read out of the kernel's own
# abort_with_payload() reason string, bsd/kern/kern_exit.c, gated behind
# -user_fault_debug -- notifyd's stderr, where dyld's halt()/console()
# output really goes, is never wired to /dev/console either as a launchd
# job or a bare interactive-shell exec). The duplicate-notify-core
# concern above is still real and still the reason libsystem_notify.dylib
# specifically stays off this link line -- but it does not actually
# require excluding libSystem.B.dylib too: this project's own
# libSystem.B.dylib (build_libsystem.sh) does NOT `-Wl,-reexport_library`
# libsystem_notify.dylib at all (confirmed via `otool -l`'s own
# LC_REEXPORT_DYLIB list -- kernel/platform/blocks/malloc/pthread/c/m/
# c++/unwind/copyfile/removefile/resolv only, a real deviation from
# real Apple's own libSystem.B which does bundle it), so linking it here
# pulls in zero notify-state surface either way. And even where a dylib's
# reexport DID overlap, ld64 would not have produced a duplicate-symbol
# link error from this: an archive's own directly-linked, non-weak
# definitions shadow a same-named dylib export for every call made from
# within this image (two-level namespace resolution only ever consults
# another image's exports for a symbol THIS image doesn't itself define)
# -- the real Bug B/DAR-217 hazard was two *dylibs* each defining the
# same symbol and disagreeing about which one every OTHER image bound
# against, not an image's own local archive shadowing a dylib it merely
# links against for an unrelated reason (here, satisfying
# `libSystemLoader`). Real fix: libSystem.B.dylib added back to this
# script's link line below; libsystem_notify.dylib stays excluded.
# ============================================================
# REAL GAPS FOUND AND CLOSED (compile-one-file methodology, same as
# every prior Tier 0/Tier 1 script)
# ============================================================
# - <bsm/libbsm.h> audit_token_to_pid(): same real, already-established
#   gap+fix as build_configd.sh's own header comment (this pinned SDK's
#   copy is API_UNAVAILABLE on this platform; the real open-source BSM-13
#   tag this project pins doesn't have the symbol at all) -- reuses that
#   script's own configd_bsm_pid_compat.h verbatim (a generic, not
#   configd-specific, one-line macro), not a fresh reinvention.
# - <sys/kevent_qos_compat.h>/MAX_GROUPS/vproc.h/vproc_priv.h ordering:
#   same real gaps+fixes as build_syslogd.sh/build_configd.sh's own
#   header comments; reused via the same -include/-I recipe, not
#   rediscovered.
# - <tzfile.h> (pathwatch.c's real path-watch mtime formatting):
#   genuinely absent from this pinned iPhoneOS14.4 SDK -- real, present,
#   unmodified at third_party/Libc/stdtime/FreeBSD/tzfile.h.
# - xpc_event_publisher_*(): real Apple private XPC event-streaming API
#   (com.apple.notifyd.matching's PublishesEvents mechanism) that this
#   project has no real backing implementation for at all (no eventd,
#   no launchd-side event-matching registrar) -- PureDarwin's own real,
#   already-written, honestly-scoped compat shim
#   (pd_xpc_event_publisher.c) is used verbatim, not a fresh stub: its
#   own INITIAL_BARRIER-only real handler firing is exactly what drives
#   notifyd's two real Mach channels' `dispatch_mach_connect()` calls,
#   and PublishesEvents is deliberately left OUT of the LaunchDaemons
#   plist this script installs (see below) so real launchd is never
#   asked to honor a key this project has no matching-registrar for.
#   This is a real, narrower, already-accepted scope gap, not a new one
#   this pass introduces -- filed as its own follow-on ticket, DAR-268.
# - libsystem_m.dylib (real, silent-until-runtime gap, found via a real
#   QEMU boot, not caught by this script's own nm/bind_target_audit.sh
#   gates -- see below): libsystem_c.dylib genuinely references `_asin`
#   with no LC_LOAD_DYLIB of its own for libsystem_m.dylib (confirmed via
#   `nm -m`: "(undefined) external _asin (dynamically looked up)", a
#   true flat-namespace reference, real Apple's own actual shape here --
#   it counts on the shared cache's flat namespace or a sibling image to
#   supply it, and this project's own -fixup_chains link mode resolves
#   EVERY bind eagerly at launch, not lazily on first call, so this
#   fires unconditionally at process start regardless of whether
#   anything in the process ever really calls an asin()-family function).
#   build_configd.sh already links libsystem_m.dylib directly for this
#   exact reason (its own header comment doesn't call it out, but its
#   link line has carried it since that script was written); notifyd
#   never inherited that fix because it deliberately does not link
#   libSystem.B.dylib (whose own LC_REEXPORT_DYLIB of libsystem_m.dylib
#   is what silently protects every OTHER binary here that links
#   libSystem.B.dylib instead of the bare per-library set, e.g.
#   syslogd.macho) -- see this script's own "WHY THIS PROJECT DOES NOT
#   LINK libsystem_notify.dylib" section above for why that umbrella is
#   off the table for notifyd specifically. Real fix: link
#   libsystem_m.dylib directly, same as build_configd.sh. Found by
#   reading dyld's own real abort_with_payload() reason string out of
#   the kernel's os_reason machinery (bsd/kern/kern_exit.c's
#   abort_with_payload_internal(), gated behind the existing
#   -user_fault_debug boot-arg) -- notifyd's own stderr is never wired
#   to /dev/console (no StandardErrorPath in com.apple.notifyd.plist,
#   matching every other real MachServices daemon here), so dyld's own
#   halt()/console() output was real but genuinely invisible through any
#   userland channel, launchd job or interactive shell alike.
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi
MIG="$(which mig)"
if [ -z "$MIG" ] || [ ! -x "$MIG" ]; then
    echo "error: mig tool not found" >&2
    exit 1
fi
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

XNU="../../third_party/xnu-7195"
LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
DISPATCH="../../third_party/libdispatch"
PDNOTIFY="../../third_party/PureDarwin/src/Libraries/XPC/notify"
PD="../../third_party/PureDarwin/src/Libraries/XPC"
AVAIL_VERSIONS="../../build/xnu-latest-build/AvailabilityVersions/dst/usr/local/include"
LAUNCHD_COMPAT="launchd_compat_headers"
DCOMPAT="dispatch_compat_headers"
SCOMPAT="syslog_compat_headers"
OUT="libc_build"
MIGOUT="$OUT/notifyd_mig"
rm -rf "$OUT/notifyd_obj"
mkdir -p "$MIGOUT" "$OUT/notifyd_obj"

if [ ! -d "$PDNOTIFY" ]; then
    echo "error: $PDNOTIFY not found -- run setup_third_party.sh first" >&2
    exit 1
fi
if [ ! -f "$MIGOUT/../notify_mig/notify_ipc.h" ] && [ ! -f "libc_build/notify_mig/notify_ipc.h" ]; then
    echo "error: $OUT/notify_mig/notify_ipc.h not found -- run build_libnotify.sh first" >&2
    exit 1
fi
for a in liblaunchd_mig libmach_ipc; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run build_launchd_mig.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/chk_fail.o" ]; then
    echo "error: $OUT/chk_fail.o not found -- run build_libmalloc.sh first" >&2
    exit 1
fi
# Real gap, found 2026-09-13: several items in the loop below derive to
# either a wrong existing script (libdispatch/libxpc -> build_libdispatch.sh
# /build_libxpc.sh both exist but build unrelated static archives) or no
# script at all (libbsm/libcopyfile/libopenpam drop the "lib" prefix in
# their real producer names; "libSystem.B" contains a literal dot and
# never matches the plain-short-name-token check at all). Named
# individually here with the real, correct script names.
if [ ! -f "$OUT/system/libdispatch.dylib" ]; then
    echo "error: $OUT/system/libdispatch.dylib not found -- run build_libdispatch_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libxpc.dylib" ]; then
    echo "error: $OUT/system/libxpc.dylib not found -- run build_libxpc_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libbsm.dylib" ]; then
    echo "error: $OUT/system/libbsm.dylib not found -- run build_bsm_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libcopyfile.dylib" ]; then
    echo "error: $OUT/system/libcopyfile.dylib not found -- run build_copyfile_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libopenpam.dylib" ]; then
    echo "error: $OUT/system/libopenpam.dylib not found -- run build_openpam_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libSystem.B.dylib" ]; then
    echo "error: $OUT/system/libSystem.B.dylib not found -- run build_libsystem.sh first" >&2
    exit 1
fi
for dylib in libdispatch libxpc libsystem_kernel libsystem_pthread libsystem_malloc \
             libsystem_platform libsystem_blocks libsystem_c libsystem_m libbsm \
             libcopyfile libinfo libopenpam libSystem.B; do
    if [ ! -f "$OUT/system/$dylib.dylib" ]; then
        echo "error: $OUT/system/$dylib.dylib not found -- run its build_*.sh first" >&2
        exit 1
    fi
done

XNU_ABS="$(cd "$XNU" && pwd)"
PD_ABS="$(cd "$PD" && pwd)"

# ------------------------------------------------------------------
# Real MIG regeneration.
# ------------------------------------------------------------------
# notify_ipc.defs, SERVER side this time (build_libnotify.sh already
# regenerated the CLIENT side into $OUT/notify_mig/ -- this daemon needs
# its own server-side notify_ipcServer.c/.h, generated fresh into its own
# MIGOUT so the two scripts stay independently reproducible, same
# precedent build_configd.sh's own config_server.c regeneration
# established for build_libsystemconfiguration_client.sh's client side).
"$MIG" -arch arm64 -I"$XNU_ABS/osfmk" \
    -header "$MIGOUT/notify_ipc.h" \
    -user "$MIGOUT/notify_ipcUser.c" \
    -server "$MIGOUT/notify_ipcServer.c" -sheader "$MIGOUT/notify_ipcServer.h" \
    "$PDNOTIFY/notify_ipc.defs" >/dev/null
echo "mig: regenerated notify_ipc.h/notify_ipcUser.c/notify_ipcServer.c (server) from real notify_ipc.defs"

# osfmk/mach/notify.defs (real xnu Mach dead-name/no-senders notification
# subsystem), SERVER side ONLY -- no client side exists for this
# subsystem (see build_launchd_mig.sh's own header comment). Real,
# already-proven-to-mig-clean source: PureDarwin's own copy, same one
# build_launchd_mig.sh itself uses.
"$MIG" -arch arm64 -I"$XNU_ABS/osfmk" \
    -header /dev/null -user /dev/null \
    -server "$MIGOUT/notifyServer.c" -sheader "$MIGOUT/notifyServer.h" \
    "$PD_ABS/launchd/notify.defs" >/dev/null
echo "mig: regenerated notifyServer.c/notifyServer.h from real PureDarwin/launchd/notify.defs"

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=ios,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos\(x\)=ios,introduced=x \
    -DSPI_AVAILABLE\(...\)= -D__SPI_AVAILABLE\(...\)= \
    -include "$SCOMPAT/sys/kevent_qos_compat.h" \
    -include "sys/fileport.h" -include "$PDNOTIFY/pd_notifyd_compat.h" \
    -include "configd_bsm_pid_compat.h" \
    -include "notifyd_bsm_euid_compat.h" \
    -include "configd_sigaction_compat.h" \
    -include "notifyd_os_variant_compat.h" \
    -include "sys/param.h" -DMAX_GROUPS=NGROUPS \
    -I "$PDNOTIFY" -I "$PDNOTIFY/compat" -I "$MIGOUT" \
    -I "$SCOMPAT" -I "$LAUNCHD_COMPAT" -I "$DCOMPAT" \
    -I "$PD/launchd/System-compat" -I "$PD/libxpc/include" \
    -I "$DISPATCH" -I "$DISPATCH/private" \
    -I "$LIBC/stdtime/FreeBSD" -I "$LIBC/locale" -I "$LIBC/locale/FreeBSD" -I "$LIBC/fbsdcompat" \
    -I "$OUT/compat_include" \
    -idirafter "$AVAIL_VERSIONS" \
    -idirafter "$XNU/libsyscall/wrappers/libproc" -idirafter "$XNU/libsyscall/wrappers/spawn" \
    -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libsyscall" -idirafter "$XNU/libsyscall/mach" \
    -idirafter "$PTHREAD/include" -idirafter "$PTHREAD/private" \
    -idirafter "$PLATFORM/private" -idirafter "$PLATFORM/include" -idirafter "$PLATFORM/internal" \
    -idirafter "$XNU/BUILD.hdrs/dst/usr/local/include" -idirafter "$XNU/EXTERNAL_HEADERS" \
    -idirafter "$LIBC/darwin" -idirafter "$LIBC" -idirafter "$XNU/libkern")

ok=0
fail=0
compile_one() {
    local src="$1" base
    shift
    base=$(basename "$src")
    base=${base%.c}
    local out="$OUT/notifyd_obj/${base}.o"
    if "$CLANG" "${CFLAGS[@]}" "$@" "$src" -o "$out" 2>"$OUT/notifyd_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/notifyd_obj/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/notifyd_obj/${base}.err")"
    fi
}

# Daemon-only real sources (PureDarwin's own notifyd CMake target).
for f in notifyd notify_proc pathwatch service pd_sandbox_check; do
    compile_one "$PDNOTIFY/$f.c"
done
# Shared "notify core" real sources -- private copy, force_load-shaped
# (see this script's own header comment for why these are NOT reused
# from libsystem_notify.dylib).
for f in notify_client libnotify table pd_xpc_event_publisher; do
    compile_one "$PDNOTIFY/$f.c"
done
# Real MIG glue, compiled directly (not archived elsewhere first).
compile_one "$MIGOUT/notify_ipcUser.c"
compile_one "$MIGOUT/notify_ipcServer.c"
compile_one "$MIGOUT/notifyServer.c"
# UseSpecialReplyPort 1 support (notify_ipc.defs:38) -- the real,
# already-curated helper pair build_libnotify.sh's own header comment
# establishes. Only HALF of that pair is reused as-is: the raw trap
# object (notify_mig_special_reply_port_trap.o) is linked directly from
# build_libnotify.sh's own output further down this script. This C
# source is freshly recompiled here into a notifyd-private object
# instead (corrected 2026-09-08, code review -- the comment here used to
# claim both halves were "reused verbatim," which reads as object-level
# reuse for this one too; it is the same real, unmodified source
# recompiled, not a different implementation, but it is a real second
# clang invocation each build, and the two compiled copies could
# silently diverge if this script and build_libnotify.sh's own CFLAGS
# for it were ever edited independently).
compile_one "notify_mig_special_reply_port.c"

# Real, small, per-image-safe compat objects -- each is a self-contained
# raw syscall wrapper or FORTIFY_SOURCE helper with no process-wide
# singleton state, so (unlike notify_client.o/libnotify.o/table.o above)
# a private copy in this image is harmless even where another image also
# carries one (same precedent as chk_fail.o being linked into multiple
# binaries project-wide, per CLAUDE.md's own build_syslogd.sh citation).
#   - strlcat_chk: real, unmodified Libc/secure/strlcat_chk.c --
#     pathwatch.c's real _FORTIFY_SOURCE-checked strlcat() call sites.
#   - atoll: real, unmodified Libc/stdlib/FreeBSD/atoll.c --
#     notifyd.c's own init_config()'s real config-file integer parsing.
#     Real gap found+fixed compiling this one (DAR-159's own class,
#     xlocale_private.h's own header comment): atoll.c's real
#     _NL_LOCALE_DECLARE macro (numeric.h) reads `__locale_key` (an
#     `extern`-only reference in this TU, no definition) through
#     xlocale_private.h's `#else` branch, which marks the declaration
#     `__attribute__((visibility("hidden")))` -- correct on real macOS
#     (single-image assumption) but wrong here (the real DEFINITION is
#     in a separate image, libsystem_c.dylib): a hidden-visibility extern
#     tells clang the reference resolves via a same-linkage-unit rebase,
#     so it emits a direct adrp/add instead of a GOT-indirect load, and
#     real ld64 -fixup_chains fails hard ("target '___locale_key' does
#     not have address") since no such local rebase target exists. Real
#     fix: this ONE compile gets `-DXLOCALE_STATE_EXPORTED` too, which
#     that header's own `#ifdef` branch already reserves for exactly
#     this "genuinely cross-image, needs default visibility" case (the
#     comment's claim that referencing TUs "don't need the macro either
#     way" turned out to only hold under ld64's non-chained-fixups path;
#     -fixup_chains is this project's default everywhere, so it doesn't
#     hold here -- found empirically, not assumed).
#   - libsystem_notify_misc_compat: real creat/shm_open/usleep/
#     os_fault_with_payload/os_variant_has_internal_diagnostics --
#     build_libsystem_notify.sh's own real, already-written fix for the
#     exact same real gap class (that dylib's own copy is unreachable
#     here -- see this script's own header comment for why notifyd.macho
#     does not link libsystem_notify.dylib at all).
#   - signal: real, unmodified Libc/gen/FreeBSD/signal.c -- same file
#     build_configd.sh already compiles directly for the identical gap.
compile_one "$LIBC/secure/strlcat_chk.c"
compile_one "$LIBC/stdlib/FreeBSD/atoll.c" -DXLOCALE_STATE_EXPORTED
compile_one "libsystem_notify_misc_compat.c"
compile_one "$LIBC/gen/FreeBSD/signal.c"
compile_one "configd_sigaction_compat.c"

# Real raw-syscall trap generation, same create-syscalls.pl recipe
# build_corefoundation.sh's own header comment already established for
# this exact class of gap (a NO_SYSCALL_STUB syscalls.master entry with
# no hand-written libsyscall/custom/*.s counterpart). Two real syscalls
# neither this project's per-library dylibs nor any prior tier's static
# archive has generated yet, confirmed via exhaustive `nm -gU` across
# every built dylib before writing this step, not assumed:
#   - settid (#285, AUE_SETTID, NO_SYSCALL_STUB): real Apple's own
#     generated ___settid.s carries a real `.set _pthread_setugid_np,
#     ___settid` alias directive (confirmed by reading the generator's
#     real output directly) -- notify_proc.c/pathwatch.c's real per-
#     caller credential-drop path calls pthread_setugid_np() by that
#     public name, so generating this one file real-solves both symbols
#     at once, no hand-written alias needed.
#   - syscall (#0, the generic variadic raw-syscall trampoline, real
#     libsyscall/custom/__syscall.s, MI arm64 body): pathwatch.c's real
#     __path_stat() calls it directly for a raw fstatat-class lookup this
#     project's dylib graph has never wrapped under the plain `_syscall`
#     name before.
#   - setrlimit (#195, AUE_SETRLIMIT, NO_SYSCALL_STUB): same real gap
#     class getty_syscalls.c's own header comment already documents
#     ("real Apple's public wrapper goes through a private cancelable
#     indirection this project hasn't built") -- generated here as
#     `___setrlimit` and aliased to the plain public name at link time
#     (notifyd.c's own main() calls plain setrlimit() directly).
SYSCALL_GEN="$(mktemp -d)"
trap 'rm -rf "$SYSCALL_GEN"' EXIT
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" \
    "$XNU/libsyscall/custom" \
    "$XNU/libsyscall/Platforms" \
    iPhoneOS \
    "$SYSCALL_GEN" >/dev/null
mkdir -p "$OUT/notifyd_sys_asm_compat/mach/arm64"
cat > "$OUT/notifyd_sys_asm_compat/mach/arm64/asm.h" <<'EOF'
#include <arm64/asm.h>
EOF
for s in ___settid ___syscall ___setrlimit; do
    out="$OUT/notifyd_obj/${s#___}_trap.o"
    if "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 \
        -I "$OUT/notifyd_sys_asm_compat" -I "$XNU/libsyscall/custom" -I "$XNU/libsyscall/mach" -I "$XNU/osfmk" \
        "$SYSCALL_GEN/$s.s" -o "$out" 2>"$OUT/notifyd_obj/${s#___}_trap.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/notifyd_obj/${s#___}_trap.err"
    else
        fail=$((fail + 1))
        echo "  gap: $s -- $(cat "$OUT/notifyd_obj/${s#___}_trap.err")"
    fi
done
rm -rf "$SYSCALL_GEN"
trap - EXIT

echo "notifyd: $ok compiled, $fail gaps (see $OUT/notifyd_obj/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f "$OUT/libnotifyd.a"
ar rcs "$OUT/libnotifyd.a" "$OUT"/notifyd_obj/*.o
echo "wrote $OUT/libnotifyd.a ($(ls "$OUT"/notifyd_obj/*.o | wc -l | tr -d ' ') objects)"

# ------------------------------------------------------------------
# Real link: dynamic executable, same Csu start.s/crt.c pattern
# build_syslogd.sh/build_configd.sh already established. Deliberately
# excludes libSystem.B.dylib/libsystem_notify.dylib -- see this script's
# own header comment.
# ------------------------------------------------------------------
if [ ! -f "$OUT/notify_mig_special_reply_port_trap.o" ] && [ ! -f "$OUT/notify_mig/notify_mig_special_reply_port_trap.o" ]; then
    echo "error: notify_mig_special_reply_port_trap.o not found -- run build_libnotify.sh first" >&2
    exit 1
fi

# Link order, load-bearing (DAR-261): libsystem_kernel.dylib is listed
# BEFORE libmach_ipc.a, not after. libmach_ipc.a contains
# mach_ipc_dead_stubs.o, whose _mach_port_insert_right/
# _mach_port_extract_right/_mach_port_unguard are narrowly-scoped
# KERN_FAILURE stand-ins written only for mach_msg_destroy_port()'s
# provably-unreachable reply-validation paths (see
# mach_ipc_dead_stubs.c's own header). With the archive scanned first,
# ld resolved notify_proc.o's REAL mach_port_insert_right() call out of
# that archive -- because mach_msg_real.o, in the same archive, pulls
# the symbol in -- and baked a `mov w0, #0x5` (KERN_FAILURE) body into
# notifyd.macho, shadowing libsystem_kernel.dylib's real implementation
# (mach_port_gap_closures2.c) which was never even consulted.
#
# The real symptom that cost: __notify_generate_common_port
# (third_party/PureDarwin/src/Libraries/XPC/notify/notify_proc.c:1613)
# does mach_port_insert_right(mach_task_self(), port, port,
# MACH_MSG_TYPE_MAKE_SEND) and asserts on it, so every client's first
# notify round trip aborted the daemon's handler and hung the client
# forever. mach_ipc_dead_stubs.c's own header already warned about
# exactly this ("this project's flat-namespace symbol search would
# otherwise have bound syslogd's real dispatch/eventlink calls to those
# fake KERN_FAILURE stand-ins"); the ordering here is what makes the
# warning moot rather than prophetic.
#
# Everything else libmach_ipc.a supplies is also a real export of
# libsystem_kernel.dylib (that dylib is built by `ar x`-ing this very
# archive, minus mach_msg_real.o/mach_ipc_traps.o/mach_ipc_dead_stubs.o,
# and compiling those same real sources itself -- see
# build_libsystem_kernel.sh), so binding them to the dylib is also the
# single-process-wide-copy outcome this project wants, not a side
# effect. The archive stays on the line after it purely as a backstop.
rm -f notifyd.macho
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o notifyd.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    -Wl,-force_load,"$OUT/libnotifyd.a" \
    "$OUT/notify_mig/notify_mig_special_reply_port_trap.o" \
    "$OUT/liblaunchd_mig.a" \
    "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/libmach_ipc.a" "$OUT/chk_fail.o" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_c.dylib" \
    "$OUT/system/libsystem_m.dylib" \
    "$OUT/system/libbsm.dylib" \
    "$OUT/system/libcopyfile.dylib" "$OUT/system/libinfo.dylib" "$OUT/system/libopenpam.dylib" \
    "$OUT/system/libdispatch.dylib" "$OUT/system/libxpc.dylib" \
    "$OUT/system/libSystem.B.dylib" \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
    -Wl,-alias,'_strftime$UNIX2003',_strftime \
    -Wl,-alias,___setrlimit,_setrlimit \
    2>"$OUT/notifyd_link.err" \
    && echo "real link: 0 undefined symbols -- notifyd.macho fully closed" \
    || { n=$(grep -c 'Undefined symbol' "$OUT/notifyd_link.err" || true); echo "real link found $n undefined symbol group(s):"; grep '^  "' "$OUT/notifyd_link.err" | sed 's/,.*//' | sort -u; tail -80 "$OUT/notifyd_link.err" >&2; exit 1; }

otool -l notifyd.macho | grep -q LC_LOAD_DYLINKER || \
    echo "WARNING: no LC_LOAD_DYLINKER -- expected a real dynamic executable" >&2

if otool -L notifyd.macho | grep -q 'libsystem_notify\.dylib'; then
    echo "error: notifyd.macho unexpectedly links libsystem_notify.dylib -- see this script's own header comment for why that would duplicate the notify-core state engine" >&2
    exit 1
fi
if ! otool -L notifyd.macho | grep -q 'libSystem\.B\.dylib'; then
    echo "error: notifyd.macho does not link libSystem.B.dylib -- real dyld hard-requires this for every process (dyldMain.cpp's halt(\"program does not link with libSystem.B.dylib\")), see this script's own 2026-09-06 header update" >&2
    exit 1
fi

real_gaps=$(nm -m notifyd.macho 2>/dev/null | grep undefined | grep -v "(from " || true)
if [ -n "$real_gaps" ]; then
    echo "WARNING: real undefined symbols present (no sibling dylib resolves these):" >&2
    echo "$real_gaps" >&2
fi

./bind_target_audit.sh notifyd.macho || {
    echo "error: notifyd.macho has an unresolvable two-level bind -- it would abort at dyld load" >&2
    exit 1
}

# Hard gate for DAR-261's real regression shape (see the link-order
# comment above, and dead_stub_audit.sh's own header for the full
# mechanism). Checked against the built image rather than by inspecting
# the link line, so a future reordering anywhere cannot reintroduce it
# silently.
./dead_stub_audit.sh notifyd.macho || {
    echo "error: see the link-order comment above -- keep libsystem_kernel.dylib" >&2
    echo "       ahead of libmach_ipc.a on notifyd.macho's link line (DAR-261)" >&2
    exit 1
}
echo "wrote notifyd.macho"
