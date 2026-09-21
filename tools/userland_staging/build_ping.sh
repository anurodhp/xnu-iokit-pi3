#!/bin/bash
# Builds ping.macho: real, unmodified Apple `ping` (third_party/
# network_cmds/ping.tproj/ping.c, tag network_cmds-606.40.2, same real
# checkout build_ifconfig.sh already pulls -- see setup_third_party.sh's
# own header comment for the exact tag-matching method, no new pin
# needed here). Same Csu-static-link/curated-libc-stack pattern every
# other tier in this directory follows (see build_ifconfig.sh for the
# closest precedent -- another real network_cmds consumer -- and
# build_libresolv.sh/build_libc_regex.sh for the general "port one more
# real Apple source file into this freestanding stack" methodology).
#
# ping.c's real syscall/API needs, checked directly against the source
# (grep for every call site, not assumed):
#   socket/setsockopt/sendto/select: already existed --
#     ifconfig_extra_syscalls.c (socket) + libresolv_socket_syscalls.c
#     (setsockopt/sendto/select/bind/send), both reused here unmodified.
#   getuid/__gettimeofday/access: already existed in ps_extra_syscalls.c,
#     reused here unmodified (see that file's own header for why
#     __gettimeofday there is the real NO_SYSCALL_STUB raw syscall real
#     third_party/Libc/sys/gettimeofday.c falls back to).
#   gettimeofday (public name): real, unmodified
#     third_party/Libc/sys/gettimeofday.c (Apple source, commpage-first
#     with syscall fallback) -- same file/pattern build_ps.sh already
#     uses, needing the same two small real companions: ps_time_compat.c
#     (real UTC-only gmtime/localtime/strftime -- gettimeofday.c
#     unconditionally references `localtime` in its tzp-non-NULL branch,
#     which still needs to link even though every real call site in
#     ping.c passes NULL) and ps_commpage_gettimeofday_stub.c (honest
#     "commpage unavailable" -- forces the real __gettimeofday syscall
#     path). All three reused unmodified from the ps tier.
#   recvmsg: GENUINELY MISSING -- confirmed absent from every existing
#     tools/userland_staging/*.c file (grepped the whole directory).
#     Real syscall #27 (AUE_RECVMSG, NO_SYSCALL_STUB per
#     bsd/kern/syscalls.master line 72), added in ping_extra_syscalls.c.
#   setuid: real syscall #23 exists already in cron_extra_syscalls.c, but
#     that file also defines gettimeofday()/time()/gmtime_r() (a
#     DIFFERENT, non-commpage-aware gettimeofday that would collide with
#     the real Libc/sys/gettimeofday.c this tier needs) plus a large
#     fork/exec/wait/pwrite/initgroups surface ping has no use for --
#     duplicated as a small standalone wrapper in ping_extra_syscalls.c
#     instead, matching this project's own established "each consumer
#     gets its own small duplicated wrapper" convention (see
#     libresolv_socket_syscalls.c's header comment).
#   sigaction (PUBLIC name, real working trampoline): ping.c's real
#     recvloop() setup calls plain `sigaction()` directly for
#     SIGINT/SIGQUIT/SIGINFO/SIGALRM -- no prior tier provided the
#     public name wired to the real, working __sigtramp trampoline (cron/
#     bash only ever needed the private _sigaction/_sigaction_nobind
#     aliases; cp_mv_syscalls.c's own public sigaction() is a documented,
#     honest ABI-mismatched passthrough with NO real trampoline, not
#     suitable for ping's real signal-driven summary-on-exit behavior).
#     ping_extra_syscalls.c provides the public name plus __sigaction/
#     _sigaction/_sigaction_nobind/__sigtramp, verbatim-pattern reuse of
#     cron_extra_syscalls.c's already-verified real trampoline (see that
#     file's own header for the exact register convention, cross-checked
#     against bsd/dev/arm/unix_signal.c's sendsig_set_thread_state64()).
#     Needs third_party/libplatform/src/setjmp/generic/sigaction.c
#     (real Apple source, __platform_sigaction()) compiled in too, same
#     as build_cron.sh/build_bash.sh/build_ncurses.sh already do.
#
# SUPERSEDED 2026-09-16 (DAR-182): the "no resolver subsystem" framing in
# the paragraph below (and ping_resolver_compat.c it used to reference)
# is stale. DAR-306 (2026-09-14) built exactly that si_module/dns_module
# dispatcher into real libinfo.dylib, and real libresolv.dylib exports
# the h_errno/herror/hstrerror this file's own comment describes as
# "compiled directly into ping.macho instead" -- ping.macho no longer
# compiles third_party/Libinfo/dns.subproj/herror.c or
# ping_resolver_compat.c (deleted) at all; it links libinfo.dylib +
# libresolv.dylib for real gethostbyname2()/gethostbyaddr()/h_errno
# instead. Left the historical paragraph below for the record of what
# the gap used to be and why.
#
# Three real, honestly-scoped gaps (not fabricated stubs -- each one
# documented in its own small file, same class of finding as
# ifconfig_resolver_compat.c's gethostbyname()/getaddrinfo() stand-ins):
#   - gethostbyname2(): real implementation needs Libinfo's si_module/
#     mDNSResponder-equivalent dispatcher, the exact same subsystem
#     build_libresolv.sh's own header comment already scoped dns.c/
#     dns_async.c out of for the identical reason. ping.c's own two call
#     sites already try inet_aton() first and only reach this on a
#     genuine hostname argument -- see ping_resolver_compat.c.
#
# UPDATED 2026-08-27: sqrt() now comes from libc_build/libm.a (real
# Apple Libm, tag Libm-2026 -- see setup_third_party.sh's own header
# comment and build_libm.sh for the pin/build). Its own real
# Source/ARM/sqrt.c already takes the identical __builtin_sqrt()
# hardware-FSQRT branch ping_libm_compat.c's now-retired hand-written
# wrapper used -- same real algorithm, now from the real library instead
# of a narrow local shim. ping_libm_compat.c itself is left in the tree
# (not deleted -- nothing else references it, confirmed via grep, but no
# other consumer has been swapped over yet either) and no longer
# compiled into this tier.
#   - alarm(): real Apple alarm(3) needs setitimer(2)'s real interval-
#     timer/SIGALRM delivery subsystem, which bash_syscalls.c's own
#     header comment already documents as never wired up end-to-end in
#     this project. Same honest "no previous alarm pending" stub reused
#     here (not reinvented) -- see ping_alarm_compat.c. Real, narrow
#     effect: only the optional `-t timeout` flag (not exercised by a
#     default invocation) is affected.
#
# ipsec_set_policy()/ipsec_get_policylen()/ipsec_strerror() need NO
# stand-in at all: ping.c's only real call sites (main(), ~line 769-796)
# are inside `#ifdef IPSEC` / `#ifdef IPSEC_POLICY_IPSEC` blocks, and
# this build defines neither macro (same as real Apple's own Makefile
# when the target kernel has no IPSEC_POLICY_IPSEC configured) --
# confirmed by grep that no unconditional #define exists anywhere in the
# file, so those symbols are never referenced, not routed around.
# <netinet6/ipsec.h> itself (unconditionally #included at file scope)
# is the real, unmodified SDK header -- type/macro declarations only.
#
# inet_aton/inet_ntoa, getifaddrs/freeifaddrs, if_nametoindex,
# strlcpy/strlcat, strtod (the REAL gdtoa-based one -- already fully
# built into libc_printf.a, confirmed via nm; ifconfig's own
# ifconfig_strtod_compat.c simplified fallback predates that and is
# NOT needed here), sscanf, getopt_long, strdup/strtol, err/warn family:
# all real, already-available or already-proven-buildable Apple/FreeBSD
# source, no fabrication needed for any of them.
#
# ADDENDUM 2026-08-25 (finishing this tier -- the above was written by an
# earlier pass that was interrupted mid-link, real link errors below
# resolved by actually compiling/linking, not guessed):
#   - ifconfig_extra_syscalls.c's socket() DUPLICATES
#     libresolv_socket_syscalls.c's own socket() (both link -- real
#     "duplicate symbol '_socket'" error) -- dropped from this tier's
#     compile list; libresolv_socket_syscalls.c's copy is the one used.
#   - getopt.c (real FreeBSD source, third_party/Libc/stdlib/FreeBSD)
#     added alongside getopt_long.c: getopt_long.c's own real
#     `#if 0 / #define REPLACE_GETOPT / #endif` (disabled) means it does
#     NOT define opterr/optind/optopt/optreset/optarg or getopt() itself
#     -- only getopt.c does, confirmed by reading both files' real bodies,
#     not guessed from the macro name alone.
#   - inet_ntop.c (third_party/Libc/net, real Apple source -- NOT the
#     FreeBSD/ subdir, that one doesn't exist) added: inet_ntoa.c's own
#     real body calls it.
#   - strtoul.c (third_party/Libc/stdlib/FreeBSD, real source) added:
#     str2tos()'s real -z/TOS parsing path calls it directly.
#   - ecnprobe/gmt2local.c (real, unmodified Apple network_cmds source)
#     added: confirmed via network_cmds.xcodeproj/project.pbxproj that
#     ping's own real Xcode target's Sources list genuinely includes this
#     file (a cross-target group reference, not a guess or a
#     workaround) -- ping.c's own gmt2local(0) call (behind the optional
#     --prtime long option) needs it. Needs -I $ECNPROBE for gmt2local.h.
#   - hexdump_bcmp_compat.c (existing real bcmp()=memcmp() wrapper,
#     Block 3's own precedent) added: pr_pack()'s real ICMP-payload
#     comparison calls bcmp() directly, no prior tier defined it.
#   - ping_extra_syscalls.c gained four more real raw-syscall/thin
#     wrappers this tier's own real call sites need and no prior tier
#     provided: sendmsg() (#28 AUE_SENDMSG, -R/record-route path),
#     sigprocmask() (#48, recvloop()'s signal-mask setup -- verbatim
#     reuse of cron_extra_syscalls.c/ncurses_extra_syscalls.c's already-
#     verified body), usleep() (real __semwait_signal(2) timed wait,
#     already linked into libmalloc.a -- deliberately NOT the full real
#     Libc/gen/FreeBSD/usleep.c -> _nanosleep -> Mach-IPC-clock chain,
#     more dependency surface than pinger()'s flood-ping delay needs),
#     and time() (verbatim reuse of bash_syscalls.c's gettimeofday()-
#     based body, gmt2local()'s own t==0 case needs it).
#     __darwin_check_fd_set_overflow() (verbatim reuse of bash_syscalls.c's
#     real bounds-check body) also added there -- the SDK's FD_SET/
#     FD_ISSET macros call it unconditionally on a static link.
#   - ping_resolver_compat.c gained gethostbyaddr() (same honest "no
#     resolver subsystem" HOST_NOT_FOUND degradation as its existing
#     gethostbyname2() -- pr_addr()'s reverse-DNS path, only reached
#     without -n).
#   - Link line gained real $UNIXNNNN-suffix aliases (established pattern,
#     see the existing -Wl,-alias lines below) for bind/connect/send/
#     sendto/select/recvmsg/sendmsg/usleep/strtod/writev, plus a REVERSED
#     alias (`_mmap$UNIX2003',_mmap) so libpthread.a's own real, already-
#     built mmap$UNIX2003 wrapper (built by an earlier tier) also
#     satisfies pthread_main_thread_bootstrap.c's plain, unversioned
#     mmap() call site -- no new mmap implementation needed.
#   - Real, unrelated stale-shim bug found and fixed while chasing these
#     link errors (affects every consumer that links both libc_stdio.a
#     and real libpthread.a together, not just ping): libc_stdio_shims.c's
#     pthread_mutex_init/lock/trylock/unlock/destroy used to be single-
#     threaded no-ops, predating real pthread_create() (2026-08-24) --
#     now that libpthread.a exports real bodies for all five, linking
#     both hit a real duplicate-symbol error. Fixed at the source (see
#     that file's own header comment) and libc_stdio.a rebuilt via
#     build_libc_stdio.sh, whose own standalone smoketest link also
#     needed real libpthread.a added for the same reason (see that
#     script's own updated header comment). A second, smaller duplicate
#     (`_simple_asl_log`, pthread_compat_extras.c vs libmalloc_dispatch.c)
#     found and fixed the same way. build_libpthread.sh's OWN smoketest
#     link is a separate, NOT-yet-fixed casualty of the same stub->real
#     transition (pthread_tsd_shim.c's shim TSD functions now conflict
#     with real libpthread.a's) -- see that script's own header comment
#     for the honest, documented gap; it doesn't block this tier.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
LIBPLATFORM="../../third_party/libplatform"
XNU="../../third_party/xnu-7195"
LIBINFO="../../third_party/Libinfo"
PING="../../third_party/network_cmds/ping.tproj"
ECNPROBE="../../third_party/network_cmds/ecnprobe"
OUT="libc_build"
rm -rf "$OUT/ping_obj"
mkdir -p "$OUT/ping_obj" "$OUT/compat_include"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

for dep in libplatform.a libmalloc.a libpthread.a libclosure.a libc_gen.a libc_stdio.a libc_printf.a libc_string.a libc_locale_for_stdio.a libunwind.a libm.a; do
    if [ ! -f "$OUT/$dep" ]; then
        echo "error: $OUT/$dep not found -- run its build_*.sh first" >&2
        exit 1
    fi
done

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -DPRIVATE \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $PING \
    -I $ECNPROBE \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall \
    -I $LIBINFO/Libinfo -I $LIBINFO/gen.subproj"

ok=0
skip=0
for spec in \
    "$PING/ping.c:-Dmain=ping_cmd_real_main" \
    "$LIBC/net/FreeBSD/inet_addr.c:" \
    "$LIBC/net/FreeBSD/inet_ntoa.c:" \
    "$LIBC/net/inet_ntop.c:" \
    "$LIBINFO/gen.subproj/getifaddrs.c:" \
    "$LIBINFO/gen.subproj/if_nametoindex.c:" \
    "$LIBC/gen/FreeBSD/err.c:" \
    "$LIBC/stdlib/FreeBSD/getopt.c:" \
    "$LIBC/stdlib/FreeBSD/getopt_long.c:" \
    "$LIBC/stdlib/FreeBSD/strtoul.c:" \
    "$LIBC/gen/FreeBSD/signal.c:" \
    "$LIBC/sys/gettimeofday.c:" \
    "$LIBPLATFORM/src/setjmp/generic/sigaction.c:" \
    "$ECNPROBE/gmt2local.c:" \
    "ps_extra_syscalls.c:" \
    "ps_time_compat.c:" \
    "ps_commpage_gettimeofday_stub.c:" \
    "libresolv_socket_syscalls.c:" \
    "ping_extra_syscalls.c:" \
    "mach_ipc_clock_init.c:" \
    "libc_locale_globals_compat.c:" \
    "$LIBC/locale/FreeBSD/table.c:" \
    "$LIBC/locale/FreeBSD/none.c:" \
    "ping_alarm_compat.c:" \
    "ping_main_wrapper.c:" \
    "hexdump_bcmp_compat.c:" \
    ; do
    f="${spec%%:*}"
    extra="${spec#*:}"
    base=$(basename "$f" .c)
    out="$OUT/ping_obj/${base}.o"
    if "$CLANG" $CFLAGS $extra "$f" -o "$out" 2>"$OUT/ping_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/ping_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/ping_obj/${base}.err" >&2
    fi
done
echo "ping (real Apple network_cmds/ping.tproj): $ok compiled, $skip skipped"
if [ "$skip" -ne 0 ]; then
    echo "error: ping has real compile failures, not a clean tier" >&2
    exit 1
fi

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/ping_obj/ping_sched_yield_trap.o"
"$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OUT/ping_obj/ping_sched_yield.o"

# ___gdtoa_locks (COMMON/tentative symbol, glue.o): same real gap
# already fixed in build_cron.sh/build_removefile.sh/build_uname.sh/
# build_bash.sh/build_copyfile.sh/build_getty.sh/build_ifconfig.sh/
# build_login.sh -- ar x it directly (never -force_load).
mkdir -p "$OUT/ping_glue_extract"
(cd "$OUT/ping_glue_extract" && ar x "../libc_printf.a" glue.o)

# _mmap/_munmap: the comment above (2026-08-25 addendum) claims
# libpthread.a already provides a real `_mmap$UNIX2003` symbol and
# aliases the plain name from it -- that was true of libpthread.a's
# structure at the time, but real subsequent static-linking cleanup
# work replaced that with a direct, plain `_mmap`/`_munmap` definition
# (pthread_mmap_compat.o, bundled into libpthread.a by
# build_libpthread.sh) with no `$UNIX2003` variant at all, making this
# alias stale (same real gap independently hit and fixed in
# build_cron.sh/build_removefile.sh/build_uname.sh/build_bash.sh/
# build_copyfile.sh/build_getty.sh/build_ifconfig.sh/build_login.sh --
# `ld: warning: undefined base symbol '_mmap$UNIX2003'` in every one of
# those until fixed). Real create-syscalls.pl raw trap + alias to the
# plain name is the established fix.
XNU_FOR_MMAP="../../third_party/xnu-7195"
MACSDK_FOR_MMAP="$(xcrun --sdk macosx --show-sdk-path 2>/dev/null)"
MMAP_TMPGEN="$(mktemp -d)"
trap 'rm -rf "$MMAP_TMPGEN"' EXIT
ARCHS=arm64 perl "$XNU_FOR_MMAP/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU_FOR_MMAP/bsd/kern/syscalls.master" \
    "$XNU_FOR_MMAP/libsyscall/custom" \
    "$XNU_FOR_MMAP/libsyscall/Platforms" \
    iPhoneOS \
    "$MMAP_TMPGEN" >/dev/null
MMAP_SYSCFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -I $XNU_FOR_MMAP/libsyscall/custom -I $XNU_FOR_MMAP/libsyscall/mach -idirafter $XNU_FOR_MMAP/osfmk -idirafter $MACSDK_FOR_MMAP/usr/include"
"$CLANG" $MMAP_SYSCFLAGS "$MMAP_TMPGEN/___mmap.s" -o "$OUT/ping_mmap_raw.o"
"$CLANG" $MMAP_SYSCFLAGS "$MMAP_TMPGEN/___munmap.s" -o "$OUT/ping_munmap_raw.o"
rm -rf "$MMAP_TMPGEN"
trap - EXIT

# DYNAMIC LINK (DAR-314, 2026-09-15) -- see build_ifconfig.sh's own
# identically-structured block for the full reasoning. Short version:
# ping runs long after dyld is up, so there is no reason for it to be
# fully static; real libSystem.B.dylib + siblings replace the whole
# static libc archive stack and bake in the UNIX2003/INODE64 aliases.
# The err/warn aliases survive because libsystem_c.dylib only exports
# the `_`-prefixed FreeBSD internals, never the bare public names.
# libmach_ipc.a stays: it is a real archive with no dylib equivalent
# here, and DAR-312's usleep() fix needs semaphore_create from it.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi
# Real prerequisite check (2026-09-21) -- see build_ifconfig.sh's
# identically-structured block for why this is needed: without it, a
# from-scratch build hits a bare clang "no such file" pile instead of one
# clear message, and gen_build_order.py never learns this is a real
# dependency.
for d in libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libinfo; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/system/libdyld.dylib" ]; then
    echo "error: $OUT/system/libdyld.dylib not found -- run build_libdyld.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libSystem.B.dylib" ]; then
    echo "error: $OUT/system/libSystem.B.dylib not found -- run build_libsystem.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libresolv.dylib" ]; then
    echo "error: $OUT/system/libresolv.dylib not found -- run build_libresolv_dylib.sh first" >&2
    exit 1
fi
LINK_DYLIBS="$OUT/system/libsystem_c.dylib $OUT/system/libsystem_pthread.dylib $OUT/system/libsystem_malloc.dylib $OUT/system/libsystem_platform.dylib $OUT/system/libdyld.dylib $OUT/system/libSystem.B.dylib $OUT/system/libinfo.dylib $OUT/system/libresolv.dylib"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o ping.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/ping_obj/*.o \
    "$OUT/libmach_ipc.a" \
    $LINK_DYLIBS \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    `# Socket/writev/usleep versioned names: these alias ping's OWN` \
    `# locally-compiled syscall wrappers (ping_extra_syscalls.c,` \
    `# libresolv_socket_syscalls.c), NOT dylib exports, so unlike the` \
    `# generic UNIX2003 block they are not made redundant by` \
    `# libSystem.B.dylib's baked-in alias set and must survive the` \
    `# DAR-314 dynamic conversion.` \
    -Wl,-alias,_bind,'_bind$UNIX2003' \
    -Wl,-alias,_connect,'_connect$UNIX2003' \
    -Wl,-alias,_send,'_send$UNIX2003' \
    -Wl,-alias,_sendto,'_sendto$UNIX2003' \
    -Wl,-alias,_select,'_select$1050' \
    -Wl,-alias,_recvmsg,'_recvmsg$UNIX2003' \
    -Wl,-alias,_sendmsg,'_sendmsg$UNIX2003' \
    -Wl,-alias,_usleep,'_usleep$UNIX2003' \
    -Wl,-alias,__writev,'_writev$UNIX2003' \
    2>"$OUT/ping_link.err" && echo "wrote ping.macho" || { echo "error: ping.macho link failed:" >&2; cat "$OUT/ping_link.err" >&2; exit 1; }

# Real dynamic-executable assertions (DAR-314) -- inverted from the old
# static ones, not deleted, so the check still has teeth.
bad=0
otool -l ping.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: no LC_LOAD_DYLINKER /usr/lib/dyld -- not a real dynamic executable" >&2; bad=1; }
otool -l ping.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: no LC_LOAD_DYLIB -- not a real dynamic executable" >&2; bad=1; }
otool -l ping.macho | grep -q "cmd LC_MAIN" || {
    echo "error: no LC_MAIN (real unixproc entry)" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
