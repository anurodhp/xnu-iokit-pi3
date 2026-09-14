#!/bin/bash
# Builds libc_build/system/libresolv.dylib: static-to-dylib migration,
# docs/backlog.md item d8's "real, standalone libraries still static,
# genuine conversion candidates" list -- pilot library for this pass
# (docs/daemon-roadmap.md's "Static-vs-dylib architecture debt" section).
#
# REAL CORRECTNESS MOTIVATION, same bug class as mach_task_self_/g_zone
# before it (see build_libsystem_malloc.sh's own header): libresolv.a's
# res_data.c carries a real, mutable process-global `_res`/`_res_9`
# resolver-state struct (confirmed -DUSE__RES_9, see build_libresolv.sh's
# own header). Today that archive is statically bundled into THREE
# separate places at once (`grep libresolv.a build_libdyld.sh
# build_libsystem.sh build_corefoundation.sh`) -- each with its own
# private, independently-uninitialized copy. A real, single
# libresolv.dylib (matching real Apple's own libresolv.9.dylib) closes
# this the same way libsystem_malloc.dylib closed g_zone -- Stage B of
# this pass (see the static-vs-dylib plan) removes the static bundling
# from build_libdyld.sh/build_libsystem.sh/build_corefoundation.sh and
# links this dylib as a real sibling instead.
#
# Real, unmodified Apple/FreeBSD source: reuses every already-built,
# already-verified member object out of libc_build/libresolv.a (ar x,
# not recompiled -- see build_libresolv.sh's own header for the full
# 24-of-26-file scope writeup, not re-derived here), plus three
# additional real, cheap, already-in-checkout files that archive never
# needed for its narrow smoketest but a real dylib link surfaces as
# genuine external references (confirmed via a real nm-based
# intra-archive resolution pass, not guessed):
#   - Libinfo/gen.subproj/if_indextoname.c (real sibling of
#     if_nametoindex.c, already compiled by build_libresolv.sh)
#   - Libinfo/dns.subproj/herror.c (real `int h_errno` + herror()/
#     hstrerror() -- res_data.c's __h_errno_set() sets this global)
#   - Libc/net/FreeBSD/nsap_addr.c (real inet_nsap_addr()/
#     inet_nsap_ntoa(), self-contained, only needs headers already on
#     this script's include path)
#
# Real, honestly-classified `-Wl,-U` deferrals (38 symbols found via a
# real nm-based pass -- extract every libresolv.a member, union their
# defined symbols, subtract from the union of their undefined refs, then
# subtract every symbol the 5 sibling dylibs below actually export; not
# guessed, and NOT the same as build_libresolv.sh's own archive-level
# smoketest gaps, which never linked res_send.c's real send_dg()/
# send_vc() network path at all):
#   - DNSServiceQueryRecord/_ProcessResult/_RefDeallocate/_RefSockFD:
#     CLOSED 2026-08-29, no longer deferred -- real, honest "no
#     mDNSResponder daemon" stand-ins instead (libresolv_dnssd_compat.c),
#     found blocking a real QEMU boot of launchd (flat-namespace
#     eager-bind failure). The underlying scope decision (this project's
#     own build_libresolv.sh header: no mDNSResponder daemon exists
#     here) is unchanged -- only the deferral mechanism was wrong.
#   - kevent/kqueue/pipe (res_send.c's real kqueue-based I/O
#     multiplexing): CLOSED 2026-08-31 (DAR-124 sweep) -- real Apple
#     canonical home for all three is libsystem_kernel.dylib (already a
#     linked sibling below), which now really owns them (see that
#     script's own updated comment for the full "why this mattered"
#     writeup: this dylib is unconditionally reexported into every
#     process via libSystem.B.dylib, so a flat-namespace deferral here
#     was never actually scoped to "launchd's classic resolver usage" as
#     this comment used to claim -- it was a real, confirmed gap for
#     ANY process lacking a local copy, found via a direct DAR-124
#     audit, not by exercising res_send.c's I/O path at all).
#   - getprotobynumber/getprotoent/setprotoent/endprotoent/getservbyport/
#     getservent/setservent/endservent/getnameinfo: CLOSED 2026-08-29,
#     no longer deferred -- real, honest "no protocol/service database"
#     stand-ins instead (libresolv_netdb_compat.c), found blocking a
#     real QEMU boot of launchd. Real Libinfo netdb .c sources are still
#     confirmed genuinely absent from this checkout (`find
#     third_party/Libinfo -iname '*getproto*.c' -iname '*getserv*.c'` --
#     zero hits) and getnameinfo's real source still needs the same
#     si_module wall build_libresolv.sh's header documents for
#     dns_async.c -- only the deferral mechanism was wrong, not the
#     underlying scope reasoning.
#   - notify_cancel/notify_get_state/notify_register_plain: CLOSED
#     2026-08-29 -- libsystem_notify.dylib now exists (this same
#     migration pass) and is linked below as a real sibling, not
#     deferred.
#   - chmod$UNIX2003/getpeername$UNIX2003/getsockname$UNIX2003/
#     recvmsg$UNIX2003/writev$UNIX2003/pselect$1050: CLOSED 2026-08-29 --
#     real raw syscall wrappers (libresolv_more_socket_syscalls.c) +
#     real `-Wl,-alias` entries below, not deferred. Was the same
#     project-wide "BSD socket syscall wrappers missing" gap
#     docs/backlog.md item d7 already flagged.
#   - Real dns.c-internal helpers (_check_cache/_pdns_search/
#     _sdns_search/dns_search_list_count): CLOSED 2026-08-29, no longer
#     deferred -- real, honest dns.c-internal stand-ins instead
#     (libresolv_dns_internal_compat.c), found blocking a real QEMU boot
#     of launchd. Same dns.c exclusion build_libresolv.sh's own header
#     already made (some already-compiled file has a residual forward
#     reference into it) -- only the deferral mechanism was wrong.
#   - CORRECTED 2026-08-29: bind$UNIX2003/connect$UNIX2003/sendto$UNIX2003
#     are NOT actually part of this gap -- their real bases (bind/
#     connect/sendto) are genuinely LOCAL to this exact archive (real
#     libresolv_socket_syscalls.c raw wrappers, confirmed via `nm -g`),
#     so these are real, working `-Wl,-alias` entries below instead of
#     deferrals (same bug class already found+fixed for
#     libremovefile.dylib's fsync$UNIX2003/reallocarray$DARWIN_EXTSN and
#     libcopyfile.dylib's chmod$UNIX2003-family -- a real launchd boot
#     failure surfaced the original mistake here too). What remains a
#     real, project-wide BSD socket syscall gap, not resolv-specific
#     (confirmed via `nm -gU` against every sibling dylib below: none
#     export these in any form, and this archive's own objects don't
#     define them either): chmod$UNIX2003, getpeername$UNIX2003,
#     getsockname$UNIX2003, recvmsg$UNIX2003, writev$UNIX2003,
#     pselect$1050 -- same missing-prerequisite docs/backlog.md item d7
#     already flagged for CFSocket.c/CFURLSessionInterface.c ("BSD
#     socket syscall wrappers in userland libc" as a shared, not-yet-
#     attempted need).
#   - __darwin_check_fd_set_overflow: CLOSED 2026-08-31 (DAR-124 sweep)
#     -- libsystem_c.dylib (already a linked sibling below) gained real
#     ownership of this exact symbol in the 2026-08-31 "needs a new
#     home" round; nobody came back to relink this dylib against it
#     until this DAR-124 audit found the stale `-Wl,-U` deferral still
#     in place (a real chained-fixups staleness bug, same shape as the
#     libbsm.dylib/_atoi stale-ordinal find documented elsewhere in this
#     sweep -- this one just never got exercised by a boot test because
#     nothing calls into the FD_SET-checked code path on the current
#     boot path).
#   - __global_locale/__locale_key: real, already-known project-wide gap
#     (docs/backlog.md item d9's `libc_locale_globals_compat.c` --
#     deliberately NOT pulled into every individual new dylib one at a
#     time; belongs at libsystem_c.dylib itself so every consumer gets it
#     once, see this pass's plan Stage D note).
#   - notify_cancel/notify_get_state/notify_register_plain: real
#     functions, confirmed already defined in libc_build/libnotify.a
#     (`nm -g`) -- genuinely resolvable, just not yet as a sibling
#     .dylib (libnotify.dylib is later in this same migration pass).
#     Deferred via `-Wl,-U` now, to be verified resolving for real once
#     libnotify.dylib exists and is linked by whatever real consumer
#     loads both (matches this project's own `_dyld_initializer`-style
#     iterative -U-then-resolve-later precedent).
#
# ============================================================
# DAR-285 (2026-09-11): REAL dns_configuration_copy(), not a stand-in
# ============================================================
# This dylib used to ship `libresolv_dnsinfo_compat.o` (extracted from
# libresolv.a), whose dns_configuration_copy() unconditionally returned
# NULL. That file was honest and correct for the world it was written in
# -- its own header says "this project has no XPC, no launchd, no
# configd" -- and Apple's own dns_configuration_copy() really does take
# that same "DNS configuration server not available" branch when the
# dnsinfo XPC service is unreachable. Every one of those premises is now
# false: libxpc.dylib is real and a real XPC round trip really completes
# (DAR-284), launchd is PID 1, configd is resident, and the real dnsinfo
# XPC *server* half (dnsinfo_server.c + dnsinfo_create.c +
# dnsinfo_flatfile.c + libSystemConfiguration_server.c) is really linked
# into configd.macho by build_configd_plugins.sh.
#
# Why it mattered, not just tidiness: res_init.c's __res_vinit() is a
# real caller --
#     sc_dns = dns_configuration_copy();
#     if (sc_dns == NULL) return res_vinit_from_file(statp, preinit, _PATH_RESCONF);
# -- so every resolver init in every process linking this dylib fell
# through to parsing /etc/resolv.conf, a file that legitimately does not
# exist on this TARGET_OS_IPHONE target (ip_plugin.c's flat-file write is
# `#if !TARGET_OS_IPHONE`; real Apple publishes DNS only to the dynamic
# store here -- DAR-283 proved State:/Network/Global/DNS really carries
# the lease's ServerAddresses). The published configuration was real and
# libresolv could not see any of it.
#
# The two files compiled in below are the real, unmodified Apple client
# half, the exact same pair build_scutil.sh already links successfully:
#   configd/dnsinfo/dnsinfo_copy.c
#   configd/libSystemConfiguration/libSystemConfiguration_client.c
# DAR-283 had deliberately compiled them into scutil.macho itself rather
# than rebuild a dylib sitting underneath libSystem.B.dylib mid-ticket;
# that private duplicate is removed by this same ticket, so there is now
# exactly one real implementation in the system, here.
#
# THREE NEW SIBLING DYLIBS (libsystem_blocks/libdispatch/libxpc) are
# linked below as a result. That adds NO new image to any process: this
# dylib already links libsystem_notify.dylib, which already links all
# three (confirmed via otool -L, not assumed), so the transitive image
# set of every process that loads libSystem.B.dylib is unchanged.
#
# SEQUENCING, and why this could not have been done before DAR-284:
# libSystemConfiguration_client.c's libSC_send_message_with_reply_sync()
# retries forever on XPC_ERROR_CONNECTION_INTERRUPTED. Before DAR-284 no
# XPC message had ever completed a round trip on this port at all, so
# swapping the stub for the real client would have turned a wrong-but-
# fast NULL into a hang inside res_init() for every caller -- strictly
# worse. DAR-284 made an unreachable service fail fast with
# XPC_ERROR_CONNECTION_INVALID (which this loop breaks out of), which is
# the actual precondition this change needed.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
EXTRACT="$OUT/libresolv_dylib/extracted"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"

LIBINFO="../../third_party/Libinfo"
LIBC="../../third_party/Libc"
LIBRESOLV="../../third_party/libresolv"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
# DAR-285: the real dnsinfo XPC client half (see this script's header).
CONFIGD="../../third_party/configd"
DISPATCH="../../third_party/libdispatch"
PD="../../third_party/PureDarwin/src/Libraries/XPC"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/libresolv.a" ]; then
    echo "error: $OUT/libresolv.a not found -- run build_libresolv.sh first (this script reuses its already-built member objects)" >&2
    exit 1
fi
for d in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_pthread \
         libsystem_blocks; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done
# DAR-285's real dnsinfo client needs these two as real siblings. Their
# own build scripts are named build_libdispatch_dylib.sh /
# build_libxpc_dylib.sh, so they can't go in the short-name loop above.
if [ ! -f "$OUT/system/libdispatch.dylib" ]; then
    echo "error: $OUT/system/libdispatch.dylib not found -- run build_libdispatch_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libxpc.dylib" ]; then
    echo "error: $OUT/system/libxpc.dylib not found -- run build_libxpc_dylib.sh first" >&2
    exit 1
fi
# Real gap, found 2026-09-14: the final link (further below) also
# needs libsystem_notify.dylib, with no prerequisite check at all --
# only surfaced by actually running this script this far in the real
# order (every prior attempt died earlier).
if [ ! -f "$OUT/system/libsystem_notify.dylib" ]; then
    echo "error: $OUT/system/libsystem_notify.dylib not found -- run build_libsystem_notify.sh first" >&2
    exit 1
fi
for f in "$CONFIGD/dnsinfo/dnsinfo_copy.c" \
         "$CONFIGD/libSystemConfiguration/libSystemConfiguration_client.c"; do
    if [ ! -f "$f" ]; then
        echo "error: $f not found -- run setup_third_party.sh first (configd pinned 2026-09-01)" >&2
        exit 1
    fi
done
if [ ! -f "$PD/libxpc/include/xpc/xpc.h" ]; then
    echo "error: $PD/libxpc/include/xpc/xpc.h not found -- run setup_third_party.sh first" >&2
    exit 1
fi
if [ ! -d "$DISPATCH/private" ]; then
    echo "error: $DISPATCH/private not found -- run setup_third_party.sh first" >&2
    exit 1
fi

# Real, verified fix (2026-08-28, see docs/backlog.md item d9 for the
# full trail): Xcode 12's own bundled ld64-609 has a real internal crash
# under -Wl,-fixup_chains that a much newer ld does not. Compiling still
# uses Xcode 12's clang/SDK above; only the final link step's linker
# binary changes, via clang's -B override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

(cd "$EXTRACT" && ar x "../../libresolv.a")
# arc4random.o/libresolv_getentropy_syscall.o REMOVED from THIS dylib's
# own extract dir 2026-08-31 (static-linking cleanup, dylib-vs-dylib
# duplicate-symbol pass) -- NOT from libresolv.a itself, which stays
# intact (dyld.macho's own real static link in build_dyld_link.sh still
# needs it, and dyld runs before any dylib exists).
#
# Real duplicate-STATE bug, the highest-priority category this pass
# tracks: arc4random.o is a complete, self-contained RC4-based PRNG
# with real internal state (`_rs`/`_rdat`/`_rs_stired`/
# `_rs_data_available`/`_arc4_count`/`_arc4_lock`, confirmed via nm -m
# -- __DATA,__bss/__DATA,__data, not stateless code). libsystem_c.dylib
# already has its own complete, real, independently-seeded copy of the
# exact same algorithm -- two live PRNG states in one process is the
# same bug class as mach_task_self_/g_zone (arguably worse: a caller
# reseeding via `arc4random_stir()`/`arc4random_addrandom()` through one
# copy has zero effect on the other's output stream). This dylib already
# links libsystem_c.dylib as a sibling; the only real in-archive
# consumer, `res_init.o`'s one undefined reference to `_arc4random`
# (randomizing DNS query IDs), now resolves externally to the single,
# real, canonical copy.
#
# libresolv_getentropy_syscall.o's own `_getentropy` is a plain,
# stateless raw syscall trap duplicating libsystem_c.dylib's own real
# copy of the exact same wrapper -- confirmed via nm -m no other member
# of this archive defines it (arc4random.o's own reference to it was
# already `undefined`, satisfied the same way).
rm -f "$EXTRACT/arc4random.o" "$EXTRACT/libresolv_getentropy_syscall.o"

# libresolv_arc4random_deps_compat.o REMOVED too: its own header
# explains it exists purely to give arc4random.c's real fallback path
# (_my_getentropy() opening/reading /dev/urandom directly) local
# `_open$UNIX2003`/`_read$UNIX2003` forwarders, reasoning at the time
# that `-Wl,-alias` needs a local base and "nothing else in this build
# calls plain read()/open() directly." That reasoning no longer holds:
# confirmed via nm -m that `_close$UNIX2003` -- needed by the exact same
# res_init.o/res_query.o/res_send.o consumers, from the exact same
# libsystem_c.dylib sibling -- was ALREADY resolving externally via
# ordinary two-level-namespace binding with zero local copy, proving
# `_open$UNIX2003`/`_read$UNIX2003` don't need one either. With
# arc4random.o gone (above), `_open$UNIX2003` has no consumer left in
# this archive at all; `_read$UNIX2003`'s one real remaining consumer
# (res_send.o) now resolves the same way `_close$UNIX2003` always did.
rm -f "$EXTRACT/libresolv_arc4random_deps_compat.o"

# atoi.o REMOVED 2026-08-31 (static-linking cleanup, dylib-vs-dylib
# duplicate-symbol pass, continuation): real, unmodified Libc/stdlib/
# FreeBSD/atoi.c, defining `_atoi`/`_atoi_l` -- full real duplicate of
# libsystem_c.dylib's own real copy of the same source (already a
# linked sibling below). Its ONE real in-archive consumer,
# res_init.o's undefined reference to `_atoi`, now resolves there
# instead. NOT the same object as the `__locale_key`/`__global_locale`
# direct-address gotcha this script's own header documents above --
# that fix (libc_locale_globals_compat.c + table.o/none.o) is
# independently required by other members regardless of atoi.o's
# presence, confirmed via a real trial link after removing atoi.o alone
# (0 undefined symbols, no new fixup_chains error).
rm -f "$EXTRACT/atoi.o"

# libresolv_dnsinfo_compat.o REMOVED 2026-09-11 (DAR-285) -- replaced,
# further down this script, by the real Apple dnsinfo XPC client
# (configd/dnsinfo/dnsinfo_copy.c + libSystemConfiguration_client.c).
# See this script's header for the full writeup.
#
# NOT removed from libresolv.a itself, and libresolv_dnsinfo_compat.c is
# NOT deleted: build_dyld_link.sh statically links libresolv.a into
# dyld.macho, and build_libresolv.sh's own smoketest links the archive
# standalone. dyld runs before any dylib exists and cannot have an XPC
# connection, a dispatch queue or a bootstrap port -- for THAT consumer
# Apple's own "DNS configuration server not available" branch is still
# the only reachable one, and the stand-in is still the honest answer.
# (Confirmed today it is also simply unreached there: `nm dyld.macho`
# shows no _dns_configuration_copy at all, because nothing in dyld pulls
# res_init.o out of the archive.)
rm -f "$EXTRACT/libresolv_dnsinfo_compat.o"

# Same real CFLAGS recipe build_libresolv.sh's own header already
# documents (USE__RES_9/PRIVATE and the dnsinfo.h compat_include it
# generates), reused verbatim so these three new files see the exact
# same real headers/macros the rest of the archive was compiled with.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -DUSE__RES_9 -DPRIVATE \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $LIBRESOLV \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall \
    -I $LIBINFO/Libinfo -I $LIBINFO/gen.subproj \
    -I ../../generated"

if [ ! -f "$OUT/compat_include/dnsinfo.h" ]; then
    echo "error: $OUT/compat_include/dnsinfo.h not found -- run build_libresolv.sh first (it generates this real, vendored header)" >&2
    exit 1
fi

echo "compiling 3 real additional files (if_indextoname.c, herror.c, nsap_addr.c)..."
"$CLANG" $CFLAGS "$LIBINFO/gen.subproj/if_indextoname.c" -o "$EXTRACT/if_indextoname.o"
"$CLANG" $CFLAGS "$LIBINFO/dns.subproj/herror.c" -o "$EXTRACT/herror.o"
"$CLANG" $CFLAGS "$LIBC/net/FreeBSD/nsap_addr.c" -o "$EXTRACT/nsap_addr.o"

# REAL BUG FOUND+FIXED 2026-08-29: DNSServiceQueryRecord/_ProcessResult/
# _RefDeallocate/_RefSockFD were deferred via `-Wl,-U` on the assumption
# an unreached symbol is harmless; it is not (real dyld flat-namespace
# lookup fails at real process load time regardless of whether the call
# path executes, confirmed via a real QEMU boot failure). Real, honest
# "no mDNSResponder daemon" stand-in instead -- see
# libresolv_dnssd_compat.c's own header. Uses the real Apple dns_sd.h
# already on this SDK's include path (no -I needed beyond $CFLAGS's
# existing -isysroot), not hand-declared types.
"$CLANG" $CFLAGS libresolv_dnssd_compat.c -o "$EXTRACT/libresolv_dnssd_compat.o"

# REAL BUG FOUND+FIXED 2026-08-29, same real class as the DNS-SD fix
# above: __check_cache/__pdns_search/__sdns_search/_dns_search_list_count
# were ALSO wrongly deferred. Real, honest dns.c-internal stand-ins
# instead -- see libresolv_dns_internal_compat.c's own header.
"$CLANG" $CFLAGS libresolv_dns_internal_compat.c -o "$EXTRACT/libresolv_dns_internal_compat.o"

# REAL BUG FOUND+FIXED 2026-08-29, same real class as above:
# chmod$UNIX2003/getpeername$UNIX2003/getsockname$UNIX2003/
# recvmsg$UNIX2003/writev$UNIX2003/pselect$1050 were also wrongly
# deferred -- real, project-wide missing raw syscalls, not resolv-
# specific (see libresolv_more_socket_syscalls.c's own header). Real
# raw wrappers compiled in, real $UNIX2003/$1050 aliases added to this
# dylib's own link below (base symbols now genuinely local here).
#
# -DLIBRESOLV_SOCKET_SYSCALLS_DYLIB_VARIANT=1 added 2026-08-31
# (static-linking cleanup, dylib-vs-dylib duplicate-symbol pass): guards
# out chmod()/recvmsg()/writev() for this dylib build specifically --
# see libresolv_more_socket_syscalls.c's own comment. getpeername()/
# getsockname()/pselect() are unaffected (not guarded in that file).
"$CLANG" $CFLAGS -DLIBRESOLV_SOCKET_SYSCALLS_DYLIB_VARIANT=1 libresolv_more_socket_syscalls.c -o "$EXTRACT/libresolv_more_socket_syscalls.o"

# libresolv_socket_syscalls.o recompiled fresh here (NOT `ar x`-extracted
# from libresolv.a like most of this dylib's other members) 2026-08-31,
# same reason and pattern as above: guards out socket()/connect()/
# send()/select() (real canonical home now libsystem_kernel.dylib, see
# that script's own "BSD socket family" comment) while keeping bind()/
# setsockopt()/sendto() (this dylib's own real, only copies -- sendto()
# specifically must stay local here for this dylib's own
# `-Wl,-alias,_sendto,'_sendto$UNIX2003'` below, see that file's own
# comment on the ld64 nonlocal-base constraint).
"$CLANG" $CFLAGS -DLIBRESOLV_SOCKET_SYSCALLS_DYLIB_VARIANT=1 libresolv_socket_syscalls.c -o "$EXTRACT/libresolv_socket_syscalls.o"

# REAL BUG FOUND+FIXED 2026-08-29, same real class as above:
# getprotobynumber/getprotoent/setprotoent/endprotoent/getservbyport/
# getservent/setservent/endservent/getnameinfo were also wrongly
# deferred. Real, honest "no protocol/service database" stand-ins
# instead -- see libresolv_netdb_compat.c's own header.
"$CLANG" $CFLAGS libresolv_netdb_compat.c -o "$EXTRACT/libresolv_netdb_compat.o"

# REAL FIX, found while first attempting to defer ___global_locale/
# ___locale_key via plain -Wl,-U (see this script's header -- that
# attempt is what this comment replaces): ld64's real chained-fixups
# encoding for atoi.o's own direct `adrp/lo12` address computation of
# __locale_key (an extern DATA symbol, not a called function) requires a
# real, locally-resolvable address at link time -- `-Wl,-U` only works
# for lazily-bound calls through a stub, not this kind of direct
# data-address fixup ("ld: fixup error (kind=arm64_adrp_lo12)... target
# '___locale_key' does not have address", confirmed by actually trying
# it). Real fix, matching build_libdispatch_dylib.sh's own already-
# proven precedent for the exact same two globals: compile
# libc_locale_globals_compat.c (this project's own real, narrow
# reproduction of xlocale.c's two globals -- see that file's own header)
# plus table.o/none.o (real locale/FreeBSD/table.c's _DefaultRuneXLocale
# + its own self-contained charset-conversion dependency, extracted from
# the already-built libc_locale.a) directly into THIS dylib, so they're
# real local definitions, not deferred externs.
if [ ! -f "$OUT/libc_locale.a" ]; then
    echo "error: $OUT/libc_locale.a not found -- run build_libc_locale.sh first" >&2
    exit 1
fi
(cd "$EXTRACT" && ar x "../../libc_locale.a" table.o none.o)
# See narrow_rune_locale.sh's own header (static-linking cleanup, Phase
# 1d, 2026-08-30): demotes the 4 public rune-locale globals table.o/
# none.o carry so they bind to libsystem_c.dylib (a sibling below)
# instead of this dylib defining a second, independently-mutable copy.
./narrow_rune_locale.sh "$EXTRACT"
"$CLANG" $CFLAGS libc_locale_globals_compat.c -o "$EXTRACT/libc_locale_globals_compat.o"

# ------------------------------------------------------------------
# DAR-285: the real Apple dnsinfo XPC client, compiled in here.
# ------------------------------------------------------------------
# These two files get their OWN flag set, not $CFLAGS above: $CFLAGS is
# libresolv's real freestanding libc recipe (-DUSE__RES_9/-DPRIVATE plus
# ~15 Libc/xnu -I entries), which neither file needs, while both need the
# real xpc/dispatch-private headers libresolv has never had on its path.
# Same "one file needs a different, reduced flag set" precedent
# build_scutil.sh already uses for xnu's libsyscall sources.
#
# Real header staging, one symlink: <dispatch/private.h> really lives at
# $DISPATCH/private/private.h, i.e. NOT under a dispatch/ subdirectory of
# its own. Staging a `dispatch` symlink that points at private/ makes
# <dispatch/private.h> resolve, while <dispatch/dispatch.h> (which
# private/ does not contain) still falls through to the real
# $DISPATCH/dispatch/dispatch.h listed right after it. This is a private
# copy of the exact same one-line staging step
# build_libsystemconfiguration_client.sh does into $OUT/sc_dispatch_compat
# -- deliberately NOT a dependency on that script's output, because
# libSystemConfiguration.dylib links THIS dylib (otool -L), so depending
# on it here would be a real, circular build-order dependency.
rm -rf "$OUT/libresolv_dispatch_compat"
mkdir -p "$OUT/libresolv_dispatch_compat"
ln -sf "$(pwd)/$DISPATCH/private" "$OUT/libresolv_dispatch_compat/dispatch"

# The include set here is deliberately tiny (4 -I entries). It is small
# only because of the matching one-line guard this ticket added to our
# configd fork's dnsinfo/dnsinfo_internal.h: that header #included
# <SystemConfiguration/SCPrivate.h> unconditionally "for SC_log", even
# though SC_log is used ONLY by its own `#ifndef my_log` fallback -- and
# dnsinfo_copy.c defines my_log to os_log() before including it, so the
# macro was never referenced. Without the guard this compile needed the
# entire staged SystemConfiguration + CoreFoundation + IOKit header world
# (confirmed: 67 generated headers, via clang -MD), every one of which is
# produced by scripts that link this dylib. See that header's own comment.
DNSINFO_CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin \
    -fno-stack-protector -O0 -Wno-nullability-completeness \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 \
    -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN \
    -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__va_list=__builtin_va_list -DPRIVATE \
    `# configd's real headers use bridgeos availability attributes the` \
    `# pinned Xcode-12 iPhoneOS SDK does not know; same two -D remaps` \
    `# build_scutil.sh/build_configd_plugins.sh already use.` \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=bridgeos,unavailable \
    "-D__API_AVAILABLE_PLATFORM_bridgeos(x)=ios,introduced=x" \
    -I "$PD/libxpc/include" \
    -I "$OUT/libresolv_dispatch_compat" -I "$DISPATCH" \
    -I "$CONFIGD/dnsinfo" -I "$CONFIGD/libSystemConfiguration")

echo "compiling the real Apple dnsinfo XPC client (dnsinfo_copy.c, libSystemConfiguration_client.c)..."
"$CLANG" "${DNSINFO_CFLAGS[@]}" "$CONFIGD/dnsinfo/dnsinfo_copy.c" \
    -o "$EXTRACT/dnsinfo_copy.o" 2>"$OUT/libresolv_dnsinfo_copy.err" || {
    echo "error: dnsinfo_copy.c failed to compile:" >&2
    grep -m5 'error:' "$OUT/libresolv_dnsinfo_copy.err" >&2
    exit 1
}
"$CLANG" "${DNSINFO_CFLAGS[@]}" "$CONFIGD/libSystemConfiguration/libSystemConfiguration_client.c" \
    -o "$EXTRACT/libSystemConfiguration_client.o" 2>"$OUT/libresolv_libSC_client.err" || {
    echo "error: libSystemConfiguration_client.c failed to compile:" >&2
    grep -m5 'error:' "$OUT/libresolv_libSC_client.err" >&2
    exit 1
}

# Real gate, not cosmetic: prove the object that just got compiled really
# is the XPC client and not something that optimised down to a constant
# NULL. `_xpc_connection_send_message_with_reply_sync` is the one call
# that actually performs the round trip to configd.
nm -m "$EXTRACT/libSystemConfiguration_client.o" | \
    grep -q "(undefined) external _xpc_connection_send_message_with_reply_sync" || {
    echo "error: libSystemConfiguration_client.o has no xpc_connection_send_message_with_reply_sync" >&2
    echo "       reference -- this is not the real XPC client." >&2
    exit 1
}
nm -m "$EXTRACT/dnsinfo_copy.o" | grep -q "(undefined) external _libSC_send_message_with_reply_sync" || {
    echo "error: dnsinfo_copy.o does not call libSC_send_message_with_reply_sync --" >&2
    echo "       this is not the real dnsinfo client." >&2
    exit 1
}

mkdir -p "$OUT/system"
rm -f "$OUT/system/libresolv.dylib"
# -not_for_dyld_shared_cache: see build_libsystem_malloc.sh's own header
# for why every real dylib in this project opts out (no real dyld shared
# cache built here at all).
#
# -Wl,-U deferrals: see this script's own header comment above for the
# full, individually-cited classification of each one.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libresolv.9.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-alias,_bind,'_bind$UNIX2003' \
    -Wl,-alias,_sendto,'_sendto$UNIX2003' \
    -Wl,-alias,_getpeername,'_getpeername$UNIX2003' -Wl,-alias,_getsockname,'_getsockname$UNIX2003' \
    -Wl,-alias,_pselect,'_pselect$1050' \
    `# chmod/connect/recvmsg/writev's $UNIX2003 aliases REMOVED 2026-08-31` \
    `# (static-linking cleanup, dylib-vs-dylib duplicate-symbol pass) --` \
    `# their real bases moved to libsystem_kernel.dylib (see that` \
    `# script's own comment); this dylib no longer defines the plain` \
    `# names locally (libresolv_socket_syscalls.c/libresolv_more_socket_` \
    `# syscalls.c's own LIBRESOLV_SOCKET_SYSCALLS_DYLIB_VARIANT guards),` \
    `# so re-creating these aliases here would hit the documented ld64` \
    `# "-alias nonlocal base" bug (malformed ordinal-0 re-export).` \
    `# _kevent/_kqueue/_pipe/___darwin_check_fd_set_overflow -Wl,-U` \
    `# deferrals REMOVED 2026-08-31 (DAR-124 sweep): these were a real,` \
    `# confirmed-serious gap, not a theoretical one -- this dylib is` \
    `# unconditionally reexported by libSystem.B.dylib (LC_REEXPORT_` \
    `# DYLIB), i.e. loaded into literally every process this project` \
    `# ships (confirmed via otool -L libSystem.B.dylib), and this` \
    `# project's dyld does ONLY eager chained-fixups binding (round 13's` \
    `# real __muldc3 PID-1 panic already proved this). ___darwin_check_` \
    `# fd_set_overflow already had a real sibling owner (libsystem_c.` \
    `# dylib, since the 2026-08-31 "needs a new home" round) that nobody` \
    `# came back to relink against -- a real stale-declaration bug, same` \
    `# shape as the libbsm.dylib/_atoi stale-chained-fixups-ordinal find` \
    `# documented elsewhere in this sweep. _kevent/_kqueue/_pipe had NO` \
    `# real sibling owner at all until this round gave libsystem_kernel.` \
    `# dylib (already linked below) real ownership of all three (see` \
    `# that script's own updated comment) -- confirmed via a real` \
    `# df.macho-class audit that most of this project's own build_` \
    `# simple_cmds.sh binaries only survived this by accident (each` \
    `# carries a private LINK_COMMON copy of _kevent/_kqueue, but NOT of` \
    `# _pipe, which only ed/diff/less/tar happen to define` \
    `# locally) -- any OTHER real or future consumer of libSystem.B.` \
    `# dylib with none of those local copies would have hit "symbol not` \
    `# found in flat namespace '_pipe'" the instant it started, whether` \
    `# or not it ever called pipe() itself.` \
    -Wl,-unexported_symbol,_endprotoent -Wl,-unexported_symbol,_endservent \
    -Wl,-unexported_symbol,_getnameinfo -Wl,-unexported_symbol,_getprotobynumber \
    -Wl,-unexported_symbol,_getprotoent -Wl,-unexported_symbol,_getservbyport \
    -Wl,-unexported_symbol,_getservent -Wl,-unexported_symbol,_setprotoent \
    -Wl,-unexported_symbol,_setservent \
    `# unexported 2026-09-03 (DAR-229, per-library dedup series):` \
    `# libresolv_netdb_compat.c's own copies (this script's header,` \
    `# "CLOSED 2026-08-29" note above) are real, honest "no protocol/` \
    `# service database" stand-ins -- getprotobynumber/getprotoent/` \
    `# getservbyport/getservent always return NULL, setprotoent/` \
    `# endprotoent/setservent/endservent are no-ops, getnameinfo always` \
    `# returns EAI_FAIL. Real Apple libinfo.dylib (DAR-181) now exists` \
    `# and genuinely implements all 9 of these for real via the same` \
    `# si_module flat-file dispatch getpwnam/getgrnam use (confirmed:` \
    `# lookup.subproj/libinfo.c defines real getservbyport/getservent/` \
    `# setservent/endservent/getprotobynumber/getprotoent/setprotoent/` \
    `# endprotoent/getnameinfo). Confirmed via a full` \
    `# 'xcrun dyld_info -fixups' sweep of every shipped dylib/binary:` \
    `# ZERO images currently bind to libresolv's copies of any of these` \
    `# 9 symbols, so unexporting is a clean, no-relink-required removal` \
    `# (nothing today depends on the honest-stub behavior) -- any future` \
    `# real consumer that links both this dylib and libinfo.dylib now` \
    `# gets the real implementation instead of an always-fails stand-in;` \
    `# one that links only this dylib gets a real link-time undefined-` \
    `# symbol error instead of a silent wrong-answer, matching this` \
    `# project's fail-loud-at-build-time preference. Local definitions` \
    `# stay (nothing else in this archive calls any of the 9), just` \
    `# hidden from the export trie.` \
    -Wl,-unexported_symbol,__libSC_info_fork_prepare \
    -Wl,-unexported_symbol,__libSC_info_fork_parent \
    -Wl,-unexported_symbol,__libSC_info_fork_child \
    `# unexported 2026-09-11 (DAR-285): libSystemConfiguration_client.c` \
    `# marks all four of its real entry points __private_extern__ EXCEPT` \
    `# these three pthread_atfork handlers, which real Apple ships as` \
    `# exports of the single libsystem_configuration.dylib. This project's` \
    `# libSystemConfiguration.dylib already exports them (nm -gU) and is` \
    `# the one image that should; nothing in this project registers them` \
    `# with libSystem at all yet (see build_launchd_syscalls.sh's atfork` \
    `# writeup, which lists all three as "absent"), so exporting a second` \
    `# copy from here would create a real duplicate-definition ambiguity` \
    `# for zero benefit. The local definitions stay -- this dylib's own` \
    `# copy of libSC_info_available() reads the same file's _available` \
    `# static -- they are just hidden from the export trie, exactly as the` \
    `# 9 libresolv_netdb_compat.c symbols above are.` \
    -o "$OUT/system/libresolv.dylib" \
    "$EXTRACT"/*.o \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_notify.dylib" \
    `# DAR-285: real siblings for the real dnsinfo XPC client compiled in` \
    `# above. libsystem_blocks owns _NSConcreteGlobalBlock/StackBlock` \
    `# (both files use blocks), libdispatch owns dispatch_sync/` \
    `# dispatch_once/dispatch_queue_create/_dispatch_is_fork_of_` \
    `# multithreaded_parent, libxpc owns the whole xpc_* surface -- each` \
    `# confirmed by a real nm -gU sweep of every dylib in $OUT/system,` \
    `# not assumed. All three are ALREADY in every process that loads` \
    `# this dylib (libsystem_notify.dylib, linked right above, links all` \
    `# three), so no process gains an image.` \
    "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/system/libdispatch.dylib" "$OUT/system/libxpc.dylib" \
    2>"$OUT/libresolv_dylib_link.err" \
    && echo "wrote $OUT/system/libresolv.dylib" \
    || { echo "libresolv.dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libresolv_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }

otool -hv "$OUT/system/libresolv.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
# REAL FINDING while writing this script's verification, worth carrying
# into every later library in this migration: `nm -u` on a real dylib
# that links against sibling dylibs is NOT a self-containedness check --
# it lists every symbol this image's own code references but doesn't
# locally define, REGARDLESS of whether that reference correctly binds
# to a linked sibling dylib (real, working, two-level-namespace bind) or
# is a genuinely dangling `-Wl,-U` deferral. A successful `ld` exit code
# already proves every non-deferred symbol bound to something real (`ld`
# hard-fails otherwise) -- confirmed by re-checking an already-existing,
# already-shipped dylib in this project (libsystem_malloc.dylib) that
# was ALSO real, links against libsystem_kernel.dylib/
# libsystem_platform.dylib, and ALSO shows a real, nonzero `nm -u` list
# today despite its own build script's self-check message claiming
# "zero undefined symbols" -- that message's pass/fail framing was never
# actually load-bearing (this project's real verification has always
# been ld's own exit code), just misleadingly worded. The precise real
# check: `dyld_info -fixups` shows each declared `-Wl,-U` deferral bound
# to `<flat-namespace>` (resolved at real load time) and everything else
# bound to a real, named sibling dylib.
echo "declared -Wl,-U deferrals (expect: <flat-namespace> bind target, resolved at real load time):"
xcrun dyld_info -fixups "$OUT/system/libresolv.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print "  " $NF}'
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libresolv.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/|libsystem_notify/|libsystem_blocks/|libdispatch/|libxpc/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found (not flat-namespace, not one of the 8 linked sibling dylibs):" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libresolv.dylib: every non-deferred external reference binds to a real, named sibling dylib -- no unexpected/dangling references"

# ------------------------------------------------------------------
# DAR-285 gates: prove the shipped dylib really carries the real dnsinfo
# XPC client, not the always-NULL stand-in it used to.
# ------------------------------------------------------------------
# A clean link proves nothing here: the stand-in linked just as cleanly
# and exported the same three symbol names. These check behaviour-bearing
# facts instead -- that the dylib really reaches libxpc's send-with-reply
# call, and that the three dns_configuration_* entry points are still the
# real exported surface.
for s in _dns_configuration_copy _dns_configuration_free _dns_configuration_notify_key; do
    nm -gU "$OUT/system/libresolv.dylib" | grep -q " T $s\$" || {
        echo "error: libresolv.dylib no longer exports $s" >&2
        exit 1
    }
done
xcrun dyld_info -fixups "$OUT/system/libresolv.dylib" 2>/dev/null | \
    grep -q 'libxpc/_xpc_connection_send_message_with_reply_sync' || {
    echo "error: libresolv.dylib does not bind libxpc's" >&2
    echo "       _xpc_connection_send_message_with_reply_sync -- dns_configuration_copy()" >&2
    echo "       would still be a stand-in that never talks to configd (DAR-285)." >&2
    exit 1
}
# The three libSC_info_fork_* handlers must stay OUT of the export trie
# (see the -Wl,-unexported_symbol block above): libSystemConfiguration.dylib
# is this system's single real exporter of them.
if nm -gU "$OUT/system/libresolv.dylib" | grep -q "__libSC_info_fork_"; then
    echo "error: libresolv.dylib exports __libSC_info_fork_* -- those belong to" >&2
    echo "       libSystemConfiguration.dylib alone (DAR-285)." >&2
    exit 1
fi
echo "libresolv.dylib: real dnsinfo XPC client present (binds libxpc send-with-reply), 3 dns_configuration_* exports intact"
