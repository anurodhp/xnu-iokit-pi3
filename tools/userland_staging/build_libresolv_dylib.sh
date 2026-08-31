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
#     multiplexing): still an explicit, documented scope decision in
#     build_libresolv.sh's own header, still deferred -- res_send.c's
#     own real network I/O path is not exercised by launchd's classic
#     resolver usage, unlike the DNS-SD path above which turned out to
#     be referenced (not necessarily called) regardless.
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
#   - __darwin_check_fd_set_overflow: real `_FORTIFY_SOURCE`-family
#     compiler-inserted check, same already-documented `_chk`-family gap
#     class as docs/backlog.md item d7's `___chk_assert_no_overlap` entry
#     (no shim source located anywhere in this project yet).
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
for d in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_pthread; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done

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
"$CLANG" $CFLAGS libresolv_more_socket_syscalls.c -o "$EXTRACT/libresolv_more_socket_syscalls.o"

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
    -Wl,-alias,_bind,'_bind$UNIX2003' -Wl,-alias,_connect,'_connect$UNIX2003' \
    -Wl,-alias,_sendto,'_sendto$UNIX2003' \
    -Wl,-alias,_chmod,'_chmod$UNIX2003' \
    -Wl,-alias,_getpeername,'_getpeername$UNIX2003' -Wl,-alias,_getsockname,'_getsockname$UNIX2003' \
    -Wl,-alias,_recvmsg,'_recvmsg$UNIX2003' -Wl,-alias,_writev,'_writev$UNIX2003' \
    -Wl,-alias,_pselect,'_pselect$1050' \
    -Wl,-U,_kevent -Wl,-U,_kqueue -Wl,-U,_pipe \
    -Wl,-U,___darwin_check_fd_set_overflow \
    -o "$OUT/system/libresolv.dylib" \
    "$EXTRACT"/*.o \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_notify.dylib" \
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
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libresolv.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/|libsystem_notify/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found (not flat-namespace, not one of the 5 linked sibling dylibs):" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libresolv.dylib: every non-deferred external reference binds to a real, named sibling dylib -- no unexpected/dangling references"
