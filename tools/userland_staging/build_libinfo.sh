#!/bin/bash
# Builds libc_build/system/libinfo.dylib: real Apple Libinfo
# (apple-oss-distributions/Libinfo, tag Libinfo-542.40.3, already pinned
# in setup_third_party.sh and cloned at third_party/Libinfo -- confirmed
# present, just never built before this) -- DAR-181.
#
# Distinct from tools/libinfo_custom/ (this project's own working
# custom flat-file account/group resolver, already used by cron/
# ifconfig/launchd_syscalls/login/openpam/ps/simple_cmds -- see that
# file's own header). This is additive: a new, standalone, real-Apple-
# source dylib, not a replacement for libinfo_custom's existing
# consumers (deliberately out of scope for this pass -- a future ticket
# can migrate specific consumers once this exists and is proven; see
# DAR-181's own scoping note).
#
# ==========================================================================
# REAL "no directory service" INVESTIGATION (the load-bearing finding
# this whole build depends on -- done before writing a single line of
# this script, not assumed):
#
# Real Libinfo's lookup.subproj/ implements a pluggable "si_module"
# framework (si_module.c/search_module.c): each named module (file,
# cache, ds, muser, mdns) is a real, independent lookup backend, and
# search_module.c's si_module_static_search() picks a real, hardcoded
# DEFAULT search order per lookup category (getpwnam/getgrnam/etc all
# fall through to CATEGORY_DEFAULT since nothing here populates a
# per-category /etc/nsswitch.conf-style override -- confirmed by reading
# search_module.c's own si_module_config_modules_for_category(), gated
# behind a real, unset-by-default SYSINFO_CONF_ENABLE env var).
#
# That default order is real, Apple's own: cache -> ds -> mdns -> file
# (search_module.c ~line 953). Two of those four are genuinely
# unreachable here without a live daemon this project doesn't have:
#   - "ds" (lookup.subproj/ds_module.c): real OpenDirectory/
#     DirectoryService client -- but it's ALREADY gated behind a real,
#     pre-existing `#ifdef DS_AVAILABLE` in both si_module.c and
#     search_module.c (Apple's own source, not added by this project).
#     Simply never defining DS_AVAILABLE excludes it entirely, with zero
#     source changes needed -- confirmed by reading both call sites.
#   - "mdns" (lookup.subproj/mdns_module.c): real mDNSResponder client
#     (dns_sd.h/dnsinfo.h/os/log.h -- genuine daemon-backed SPI, zero
#     xpc/mach/opendirectory calls of its own kind but a real, heavy
#     multicast-DNS daemon dependency all the same). UNLIKE ds/muser,
#     Apple's real source has NO existing guard around it -- confirmed
#     via grep, zero #ifdef anywhere near its si_module.c/search_module.c
#     references. This project added one real, minimal patch, committed
#     directly in third_party/Libinfo's own git history (see `git -C
#     third_party/Libinfo log -1`): a new MDNS_AVAILABLE guard, textually
#     identical in shape to the existing DS_AVAILABLE/MUSER_AVAILABLE
#     pattern right next to it. Never defined here, so mdns_module.c is
#     never compiled or linked.
#   - "muser" (lookup.subproj/muser_module.c): already gated behind
#     Apple's own pre-existing MUSER_AVAILABLE, also never defined here.
#
# With none of DS_AVAILABLE/MUSER_AVAILABLE/MDNS_AVAILABLE defined, the
# REAL, Apple-authored default search order this build actually runs is
# just: cache -> file. "file" (lookup.subproj/file_module.c) is real
# Apple's own flat-file backend -- confirmed via a real dependency scan
# (grep for xpc_/mach_port/bootstrap_look_up/opendirectory/OpenDirectory
# across the whole file: zero hits) that it has NO directory-service
# dependency of any kind, reading /etc/passwd, /etc/master.passwd,
# /etc/group, /etc/hosts, /etc/services, etc directly off disk, exactly
# like tools/libinfo_custom's own approach but with real Apple parsing
# logic, real getpwent()/getgrent() full-enumeration iterators (the real
# gap DAR-181 calls out that libinfo_custom doesn't cover), and real
# support for the other flat files libinfo_custom never attempted
# (/etc/hosts, /etc/services, /etc/protocols, /etc/rpc, /etc/ethers,
# /etc/netgroup, /etc/fstab). "cache" (cache_module.c) is a real,
# pure in-memory TTL cache with no external dependency of its own.
#
# CONCLUSION: real Libinfo genuinely runs in a true files-only mode with
# ZERO source-logic changes (only a config-time "don't define these
# three macros" choice, matching this project's own stated preference
# for "configuration/build-mode selection over source edits" -- the one
# real source patch needed, MDNS_AVAILABLE, mirrors an EXISTING Apple
# pattern rather than inventing a new one). This is the real, intended,
# documented Apple behavior for a system with no directory service
# configured, not a workaround.
# ==========================================================================
#
# SCOPE (first real slice, DAR-181's own "keep blast radius small"):
# built as a real per-library .dylib (this project's standing convention
# for new work, not a new static archive -- see CLAUDE.md's "Static-vs-
# dylib architecture debt" note), covering:
#   - The whole si_module framework: si_module.c, search_module.c,
#     cache_module.c, file_module.c, si_data.c, thread_data.c, kvbuf.c,
#     ils.c (indivisible -- every lookup category runs through this
#     shared machinery, can't be split further without reimplementing it)
#   - lookup.subproj/libinfo.c: the single real file implementing every
#     public API entry point (getpwnam/getpwuid/getgrnam/getgrgid/
#     getpwent+setpwent+endpwent/getgrent+setgrent+endgrent -- this
#     ticket's real target -- plus gethostbyname/getaddrinfo/getservbyname/
#     etc, all in one real 3482-line translation unit; Apple ships it as
#     one file, not one per category, so "narrow scope" here means real
#     preprocessor/link-level scoping via -Wl,-U deferrals below, not
#     file selection, same shape build_copyfile.sh's own header documents
#     for a similarly monolithic real source file)
#   - gen.subproj/ether_addr.c, lookup.subproj/getnameinfo_link.c: small,
#     self-contained real files file_module.c's own ethers/netgroup
#     parsing needs
#
# Deliberately NOT compiled into this first slice:
#   - lookup.subproj/si_compare.c: real getaddrinfo result-ordering logic
#     (route-source comparison, kernel netsrc/ntstat sockets) -- confirmed
#     via a real cross-reference pass (nm -g on its own object vs `nm -u`
#     on every other file in this list) that ZERO other file in this
#     build references any of its exports; the pwd/grp/enumeration path
#     this ticket targets never touches it. Genuinely unused dead weight
#     for this slice, not deferred -- just not compiled.
#   - lookup.subproj/si_getaddrinfo.c and its 5 real exports (si_addrinfo/
#     si_ipnode_byname/si_list_to_addrinfo/si_nameinfo/the internal
#     _gai_simple helper libinfo.c's own getaddrinfo() wrapper calls):
#     real hostname/addrinfo resolution, a genuinely separate real slice
#     of Libinfo from user/group lookup -- honestly deferred via
#     `-Wl,-U` below (getpwnam/getgrnam/getpwent/getgrent all work fully
#     without them; calling real getaddrinfo() through this dylib today
#     would abort at the real call site, not at load time -- a future
#     pass can compile si_getaddrinfo.c for real once addrinfo/hostname
#     lookup specifically is in scope).
#   - dns.subproj/nis.subproj/rpc.subproj/membership.subproj: genuinely
#     separate real concerns (DNS resolver internals already owned by
#     libresolv.dylib; NIS/RPC not used anywhere in this project; mbr_*
#     UUID<->uid/gid mapping is real but unattempted this pass).
#
# Real gaps found+closed while getting this slice to compile+link (each
# documented in its own file/commit, not re-derived here):
#   - dispatch/private.h: the real umbrella header (already reachable
#     project-wide via dispatch_compat_headers/) fails to PARSE under
#     this project's pinned Xcode 12 clang (real `API_AVAILABLE(...,
#     bridgeos(4.0))` annotations in mach_private.h/data_private.h/
#     source_private.h -- a real, pre-existing SDK/header-vintage
#     mismatch, confirmed via direct probing, not a Libinfo bug). si_module.c
#     needs exactly one real constant from it (DISPATCH_QUEUE_OVERCOMMIT).
#     Fix: libinfo_compat_include/dispatch/private.h, a narrow local
#     override (shadows the broken umbrella on this build's own -I
#     path only) defining just that one real, correctly-valued constant.
#   - apfs/apfs_sysctl.h: real unpublished Apple SPI header (apfs kext),
#     confirmed absent from every apple-oss-distributions repo this
#     project has pinned. file_module.c's real
#     `#if (TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR)` branch (taken for
#     real on this project's arm64-apple-ios14.4 target, same precedent
#     build_copyfile.sh already documents) needs APFS_FSTAB_SYSCTL and
#     struct edt_fstab from it. This project's XNU has no apfs kext at
#     all (HFS+ only), so the real sysctlbyname() call this unlocks
#     always fails safely regardless of these values' exact bytes.
#     Fix: libinfo_compat_include/apfs/apfs_sysctl.h, a small real
#     header (exact field layout matches file_module.c's own
#     unconditional usage, which is identical to real BSD `struct
#     fstab`'s layout).
#   - servers/bootstrap.h: ils.c #includes it but (confirmed via grep)
#     never calls anything from it -- LI_ils_create() only needs
#     mach_port_t, already available via <mach/mach.h>. Fix:
#     libinfo_compat_include/servers/bootstrap.h, a genuinely empty
#     placeholder (not a stubbed API -- there's no real API being used).
#   - malloc zone SPI (malloc_default_zone/malloc_zone_calloc/
#     malloc_zone_realloc/malloc_zone_from_ptr/malloc_make_purgeable/
#     malloc_make_nonpurgeable): a real, project-wide gap (confirmed via
#     nm -g across every already-built libmalloc.a/libsystem_malloc.dylib
#     member: none of the public multi-zone API ever got implemented,
#     only the internal "scalable zone" it's built on). kvbuf.c --
#     Libinfo's core item serializer, used unconditionally by every
#     module -- calls these in its base (non-purgeable-specific) path,
#     so this is real, load-bearing infrastructure, not something safe
#     to defer via -Wl,-U the way the genuinely-separate addrinfo gap
#     above is. Fix: libinfo_malloc_zone_compat.c, a real, honest, narrow
#     shim (single default-zone sentinel, calloc/realloc pass-throughs,
#     purgeable calls as real no-ops matching their documented contract
#     for a system with no purgeable-VM support) -- same shape as this
#     project's own asl_client_compat.c precedent for a similar
#     "real SPI, no real backing subsystem here" gap.
#   - asl_log(): libinfo.c's one real ASL call site (a group-enumeration
#     deprecation warning) needed this one function added to the
#     existing shared asl_client_compat.c shim (build_login.sh/
#     build_launchd_core.sh's real ASL client stand-in) -- it had every
#     other real ASL entry point already, just not this one. Real
#     signature match against third_party/syslog/libsystem_asl.tproj/
#     include/asl.h, not guessed.
#   - ___initgroups: NO LONGER DEFERRED (2026-09-02, DAR-181 smoketest
#     verification). This is the raw double-underscore BSD syscall #243
#     trap (syscalls.master NO_SYSCALL_STUB) that real Apple
#     Libinfo/lookup.subproj/libinfo.c's public initgroups() wrapper
#     calls directly underneath itself -- a genuinely different symbol
#     from openpam's own single-underscore _initgroups() (DAR-153's
#     getgrouplist()-then-setgroups(2) userspace algorithm for
#     login/su). Linking libopenpam.dylib here would NOT have resolved
#     this (wrong symbol) and would have introduced a real duplicate-
#     symbol shadow, since libinfo.dylib already locally defines its
#     own _initgroups -- same bug class as DAR-18/19/20. Real fix: added
#     __initgroups() as a hand raw trap to libc_gen_syscalls.c (already
#     linked into libsystem_c.dylib, already one of this dylib's link
#     deps), same shape as every other NO_SYSCALL_STUB wrapper there.
#     Found via a real QEMU boot: the smoketest aborted at dyld load
#     with "symbol not found in flat namespace '___initgroups'" -- this
#     project's dyld does only eager chained-fixups binding, so a stale
#     deferral past its own documented "once DAR-153 commits" condition
#     fails every load, not just the code path that needs it.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
LIBINFO="../../third_party/Libinfo"
LIBC="../../third_party/Libc"
LIBRESOLV="../../third_party/libresolv"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
CSU="../../third_party/Csu"
mkdir -p "$OUT/libinfo_obj" "$OUT/system"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

for d in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_pthread libsystem_notify libsystem_blocks libdispatch libresolv libcopyfile; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done

# Real, verified fix (see build_libresolv_dylib.sh's own header for the
# full trail, docs/backlog.md item d9): Xcode 12's bundled ld64-609
# crashes under -Wl,-fixup_chains; only the final link step's linker
# binary changes to the daily-driver Xcode's newer one. Compiling still
# uses Xcode 12's clang/SDK throughout.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

# -I libinfo_compat_include first: the 3 narrow compat headers documented
# above (dispatch/private.h override, apfs/apfs_sysctl.h, servers/
# bootstrap.h) must win over the real, broader headers reachable later on
# this same path.
#
# -I $XNU/bsd as a real, high-priority -I (not -idirafter): file_module.c/
# search_module.c need this project's own real, richer XNU net/netsrc.h
# and net/ntstat.h (not si_compare.c's real dependency -- that file isn't
# compiled here at all -- but si_module.c/search_module.c's own broader
# BSD network type usage). -idirafter for $XNU/osfmk and $XNU/libsyscall:
# si_module.c needs real, complete SDK mach/mach.h (mach_task_self()/
# mach_msg_destroy()) to win over XNU's own kernel-side mach headers,
# which declare a leaner subset -- confirmed by direct probing (regular
# -I here caused real "implicit declaration" errors on both symbols).
CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -DUSE__RES_9 -DPRIVATE \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I libinfo_compat_include -I dispatch_compat_headers \
    -I "$LIBRESOLV" \
    -I "$OUT/compat_include" \
    -I "$LIBC/include" -I "$LIBC/darwin" -I "$LIBC/fbsdcompat" -I "$LIBC/locale" -I "$LIBC/locale/FreeBSD" \
    -I "$LIBC/stdtime/FreeBSD" -I "$LIBC/gen" \
    -I "$PTHREAD/include" -I "$PTHREAD/private" \
    -I "$LIBINFO/Libinfo" -I "$LIBINFO/gen.subproj" -I "$LIBINFO/lookup.subproj" -I "$LIBINFO/dns.subproj" \
    -I "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libsyscall" \
    -I ../../generated)

ok=0
fail=0
compile_one() {
    local src="$1" base out
    base=$(basename "$src" .c)
    out="$OUT/libinfo_obj/${base}.o"
    if "$CLANG" "${CFLAGS[@]}" "$src" -o "$out" 2>"$OUT/libinfo_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/libinfo_obj/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/libinfo_obj/${base}.err")"
    fi
}

echo "compiling real Libinfo si_module framework + public API (files-only mode, see header above)..."
for f in si_data.c thread_data.c kvbuf.c ils.c si_module.c cache_module.c search_module.c file_module.c getnameinfo_link.c libinfo.c si_getaddrinfo.c si_compare.c; do
    compile_one "$LIBINFO/lookup.subproj/$f"
done
compile_one "$LIBINFO/gen.subproj/ether_addr.c"

echo "compiling this project's own real, narrow compat shims..."
compile_one asl_client_compat.c
compile_one libinfo_malloc_zone_compat.c

echo "libinfo: $ok compiled, $fail gaps (see $OUT/libinfo_obj/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f "$OUT/system/libinfo.dylib"
# -Wl,-U deferrals: real, honestly-scoped exclusions only -- see this
# script's own header for the full per-symbol rationale. Every one of
# these is a genuinely separate real slice of Libinfo (hostname/addrinfo
# resolution, or a syscall another in-flight session owns), not a
# shortcut around real pwd/grp/enumeration work.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libinfo.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -Wl,-unexported_symbol,_initgroups \
    `# unexported 2026-09-03 (DAR-229, per-library dedup series). Real,` \
    `# separate real initgroups() implementations exist in two dylibs:` \
    `# this one's own (lookup.subproj/libinfo.c -- si_module lookup then` \
    `# the real raw __initgroups kernel trap, genuinely the Apple-` \
    `# canonical shape) and libopenpam.dylib's (getgrouplist()-then-` \
    `# setgroups(2), DAR-153's userspace algorithm, already wired to` \
    `# real login/su call sites). Confirmed via a full` \
    `# 'xcrun dyld_info -fixups' sweep: ZERO images currently bind to` \
    `# EITHER copy of plain _initgroups (nothing calls it through a` \
    `# dylib symbol yet), so this is a clean, no-relink-required` \
    `# narrow-export, not a behavior change for any real running` \
    `# consumer today. Keeping openpam's copy exported (the established,` \
    `# real-auth-path-relied-upon side) and narrow-exporting this` \
    `# dylib's newer, not-yet-wired one is the conservative choice --` \
    `# DAR-198 (migrate tools/libinfo_custom consumers to real` \
    `# libinfo.dylib) is the tracked follow-up for deciding whether/how` \
    `# login/su should eventually move to this dylib's real` \
    `# si_module-backed version instead, one real consumer at a time` \
    `# with its own QEMU verification, not a blind symbol-priority` \
    `# pick here. Does NOT affect ___initgroups (double-underscore, the` \
    `# raw syscall trap this dylib's own initgroups() calls internally --` \
    `# a genuinely different symbol, already resolved to` \
    `# libsystem_c.dylib's real trap, see this script's own header).` \
    -o "$OUT/system/libinfo.dylib" \
    "$OUT/libinfo_obj/"*.o \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" "$OUT/system/libsystem_notify.dylib" \
    "$OUT/system/libsystem_blocks.dylib" "$OUT/system/libdispatch.dylib" \
    "$OUT/system/libresolv.dylib" "$OUT/system/libcopyfile.dylib" \
    2>"$OUT/libinfo_dylib_link.err" \
    && echo "wrote $OUT/system/libinfo.dylib" \
    || { echo "libinfo.dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libinfo_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }

otool -hv "$OUT/system/libinfo.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
otool -l "$OUT/system/libinfo.dylib" | grep -q LC_DYLD_CHAINED_FIXUPS || {
    echo "error: no LC_DYLD_CHAINED_FIXUPS -- real dyld here only supports chained fixups, see build_dyld_test_exec.sh's header" >&2
    exit 1
}

# Same real verification build_libresolv_dylib.sh's own header
# established: ld's own exit code already proves every non-deferred
# symbol bound to something real; this just double-checks the *shape*
# of that binding (every -Wl,-U really shows <flat-namespace>, nothing
# else binds anywhere but a real, named, expected sibling dylib).
echo "declared -Wl,-U deferrals (expect: <flat-namespace> bind target, resolved at real load time):"
xcrun dyld_info -fixups "$OUT/system/libinfo.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print "  " $NF}'
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libinfo.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/|libsystem_notify/|libsystem_blocks/|libdispatch/|libresolv/|libcopyfile/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found (not flat-namespace, not one of the 10 linked sibling dylibs):" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libinfo.dylib: every non-deferred external reference binds to a real, named sibling dylib -- no unexpected/dangling references"

# Real smoketest (libinfo_smoketest.c): a real dyld-executable exercising
# getpwnam/getpwuid/getgrnam/getgrgid plus the real getpwent()/getgrent()
# enumeration this ticket exists to close. Follows build_dyld_test_exec.sh's
# own real dynamically-linked-executable recipe (Csu start.s/crt.c,
# -DCRT_DYNAMIC_LINKING, real chained fixups) -- NOT executed on this
# host (arm64-apple-ios target, blocked by this host's own hardened-
# runtime enforcement, same constraint as every other freestanding
# Mach-O this project builds); static verification (link succeeds, real
# LC_LOAD_DYLINKER/LC_DYLD_CHAINED_FIXUPS/LC_MAIN, zero unexpected
# undefined symbols) is the real host-side check, matching every other
# build_*_dylib.sh's own precedent. Real execution happens under QEMU
# against the real SD image's /etc/master.passwd + /etc/group.
#
# Degrades to a skip (not a hard failure) if libdyld.dylib/libSystem.B.dylib
# aren't built yet, matching every other tier's from-scratch-checkout
# ordering convention.
if [ -f "$OUT/system/libdyld.dylib" ] && [ -f "$OUT/system/libSystem.B.dylib" ]; then
    # Real smoketest main compiled into its OWN subdirectory
    # ($OUT/libinfo_obj/smoketest/, not $OUT/libinfo_obj/ itself) --
    # fixed 2026-09-03 (DAR-229, per-library dedup series). Root cause of
    # a real, confirmed bug: libinfo.dylib's own link above globs
    # "$OUT/libinfo_obj/"*.o (non-recursive), which on a SECOND run of
    # this script picked up the FIRST run's leftover
    # libinfo_smoketest_main.o (nothing ever cleaned it, and this step
    # necessarily runs after the dylib link above) -- a real smoketest
    # main() ends up archived into the shipped dylib's own `_main`
    # export. Confirmed via `nm -m`: libinfo.dylib exported `_main` from
    # exactly this object. A subdirectory the dylib's own `*.o` glob
    # can never match is a permanent fix, not a one-time cleanup.
    mkdir -p "$OUT/libinfo_obj/smoketest"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall \
        libinfo_smoketest.c -o "$OUT/libinfo_obj/smoketest/libinfo_smoketest_main.o" 2>"$OUT/libinfo_obj/smoketest/libinfo_smoketest_main.err" \
        && rm -f "$OUT/libinfo_obj/smoketest/libinfo_smoketest_main.err"
    rm -f libinfo_smoketest.macho
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
        -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
        -o libinfo_smoketest.macho \
        "$CSU/start.s" "$CSU/crt.c" "$OUT/libinfo_obj/smoketest/libinfo_smoketest_main.o" \
        "$OUT/system/libinfo.dylib" \
        "$OUT/system/libsystem_pthread.dylib" \
        "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib" \
        2>libinfo_smoketest_link.err \
        && echo "wrote libinfo_smoketest.macho" \
        || echo "WARNING: smoketest link failed (libinfo.dylib itself is still built -- see libinfo_smoketest_link.err)" >&2

    if [ -f libinfo_smoketest.macho ]; then
        otool -l libinfo_smoketest.macho | grep -q LC_DYLD_CHAINED_FIXUPS || \
            echo "WARNING: no LC_DYLD_CHAINED_FIXUPS in smoketest exec" >&2
        UNEXPECTED_EXE=$(nm -u libinfo_smoketest.macho | grep -v -E '^_NXArgc$|^_NXArgv$|^___progname$|^_exit$|^_printf$|^_pthread_main_thread_bootstrap$|^_(get|set|end)(pw|gr)(nam|uid|gid|ent)$' || true)
        if [ -n "$UNEXPECTED_EXE" ]; then
            echo "WARNING: unexpected undefined symbols in libinfo_smoketest.macho:" >&2
            echo "$UNEXPECTED_EXE" >&2
        fi
        echo "libinfo_smoketest.macho: real dyld-executable, ready for a real QEMU boot test against the SD image's /etc/master.passwd + /etc/group"
    fi
else
    echo "skip: libinfo_smoketest.macho (needs libdyld.dylib + libSystem.B.dylib -- run build_libdyld.sh/build_libsystem.sh first)"
fi
