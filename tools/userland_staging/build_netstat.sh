#!/bin/bash
# Builds netstat.macho: real, unmodified Apple `netstat` (third_party/
# network_cmds/netstat.tproj/, tag network_cmds-606.40.2 -- the same
# macos-112-pinned repo ifconfig and ping come from; see setup_third_party.sh
# and build_ifconfig.sh's header). Installed at /usr/sbin/netstat, Apple's
# INSTALL_PATH for it.
#
# Source list and defines are Apple's own netstat target
# (network_cmds.xcodeproj): all 15 netstat.tproj/*.c, with
# GCC_PREPROCESSOR_DEFINITIONS = INET6 IPSEC, kept to match Apple's build
# (the project's parity rule). CAUTION: this kernel does not attach the
# PF_INET6 or MULTIPATH domains, yet their sysctl OIDs are registered
# statically, and some of their handlers take locks that were never
# initialized. `netstat -an` panicked the kernel that way through
# net.inet.mptcp.pcblist (guarded in the xnu fork's mptcp_subr.c). The
# IPv6 multicast/ND OIDs have the same shape: see DAR-376 before relying on
# `netstat -g` or other INET6 views.
#
# Everything else - toolchain, CFLAGS recipe, the TSD-bootstrap
# main wrapper, the dynamic link and its assertions, the __err/__warn
# aliases - follows build_ifconfig.sh; read that file's comments for the
# reasons. Differences: the socket calls (connect/send/recv) and their
# $UNIX2003 names bind to libsystem_kernel.dylib, which exports them, so
# there are no local copies or aliases for them; only netstat_extra_syscalls.c's
# sigsuspend/__sysctl/_sigaction are local, because no dylib here exports them. Added for netstat (2026-09-24, DAR-374 needed an interface
# input-error counter and the guest had no netstat).
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
LIBINFO="../../third_party/Libinfo"
NS="../../third_party/network_cmds/netstat.tproj"
OUT="libc_build"
rm -rf "$OUT/netstat_obj"
mkdir -p "$OUT/netstat_obj" "$OUT/compat_include"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

for dep in libplatform.a libmalloc.a libpthread.a libclosure.a libc_gen.a libc_stdio.a libc_printf.a libc_string.a; do
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
    -DPRIVATE -DINET6 -DIPSEC \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $NS \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall \
    -I $LIBINFO/Libinfo -I $LIBINFO/gen.subproj"

ok=0
skip=0
for spec in \
    "$NS/main.c:-Dmain=netstat_cmd_real_main" \
    "$NS/data.c:" \
    "$NS/if.c:" \
    "$NS/inet.c:" \
    "$NS/inet6.c:" \
    "$NS/ipsec.c:" \
    "$NS/mbuf.c:" \
    "$NS/mcast.c:" \
    "$NS/misc.c:" \
    "$NS/mptcp.c:" \
    "$NS/route.c:" \
    "$NS/systm.c:" \
    "$NS/tp_astring.c:" \
    "$NS/unix.c:" \
    "$NS/vsock.c:" \
    "$LIBINFO/gen.subproj/if_nametoindex.c:" \
    "$LIBINFO/gen.subproj/if_indextoname.c:" \
    "$LIBC/gen/FreeBSD/err.c:" \
    "$LIBC/stdlib/FreeBSD/getopt.c:" \
    "$LIBC/net/FreeBSD/inet_ntoa.c:" \
    "$LIBC/net/inet_ntop.c:" \
    "$LIBC/net/inet_pton.c:" \
    "$LIBINFO/gen.subproj/getifmaddrs.c:" \
    "$LIBC/net/FreeBSD/inet_lnaof.c:" \
    "$LIBC/net/FreeBSD/inet_netof.c:" \
    "$LIBC/net/FreeBSD/linkaddr.c:" \
    "$LIBC/gen/FreeBSD/signal.c:" \
    "$LIBC/gen/FreeBSD/sysctlnametomib.c:" \
    "netstat_extra_syscalls.c:" \
    "hexdump_bcmp_compat.c:" \
    "netstat_main_wrapper.c:" \
    "simple_cmds_mmap_compat.c:" \
    "libc_locale_globals_compat.c:" \
    "$LIBC/locale/FreeBSD/table.c:" \
    "$LIBC/locale/FreeBSD/none.c:" \
    ; do
    f="${spec%%:*}"
    extra="${spec#*:}"
    base=$(basename "$f" .c)
    out="$OUT/netstat_obj/${base}.o"
    if "$CLANG" $CFLAGS $extra "$f" -o "$out" 2>"$OUT/netstat_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/netstat_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/netstat_obj/${base}.err" >&2
    fi
done
echo "netstat (real Apple network_cmds/netstat.tproj): $ok compiled, $skip skipped"
if [ "$skip" -ne 0 ]; then
    echo "error: netstat has real compile failures, not a clean tier" >&2
    exit 1
fi

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/netstat_obj/netstat_sched_yield_trap.o"
"$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OUT/netstat_obj/netstat_sched_yield.o"

# DYNAMIC LINK (DAR-314, 2026-09-15). This used to link fully static
# (-nostdlib -static -e start) against libc_printf.a/libc_gen.a/
# libc_stdio.a/libc_locale_for_stdio.a/libc_string.a/libpthread.a/
# libclosure.a/libmalloc.a/libplatform.a/cerror_stub.o plus a ~20-entry
# -Wl,-alias block hand-synthesizing the UNIX2003/INODE64 symbol-
# versioning gaps static linking forced this project to fake. All of
# that is obsolete now: real dyld-940 boots (Phase 6.5), netstat runs
# long after dyld is up, and real libSystem.B.dylib already reexports
# libsystem_c/libsystem_pthread/libsystem_malloc/libsystem_platform/
# libsystem_blocks AND bakes in that exact UNIX2003/INODE64 alias set
# itself (build_libsystem.sh's own tail), so none of those are needed.
# Same real conversion build_simple_cmds.sh did on 2026-08-28 for its
# 76 commands -- see its LINK_COMMON comment for the full reasoning.
#
# The err/warn aliases below are the ONE part of the old alias block
# that is NOT a static-linking artifact and must survive the conversion:
# this project's libsystem_c.dylib exports only the `_`-prefixed FreeBSD
# internals `__err`/`__warn`, never the bare public `err()`/`warn()`
# (confirmed via nm -gU), so the public names still have to be
# synthesized by hand. build_simple_cmds.sh keeps these two for exactly
# the same reason even after its own full dynamic conversion (its
# link_aliases, and the COMMON_ERR_OBJ comment explaining why __err/
# __warn are deliberately excluded from its unexported-symbol list so
# these aliases still have a base).
#
# The dylibs are listed individually rather than relying on
# libSystem.B.dylib's LC_REEXPORT_DYLIB: ld64 resolves an absolute
# /usr/lib/... install name against the HOST SDK's stub instead of this
# project's local libc_build/system/ copy, a real hazard already
# documented in CLAUDE.md and hit for real by DAR-13/DAR-196.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi
for d in system/libsystem_c.dylib system/libsystem_pthread.dylib \
         system/libsystem_malloc.dylib system/libsystem_platform.dylib \
         system/libdyld.dylib system/libSystem.B.dylib; do
    if [ ! -f "$OUT/$d" ]; then
        echo "error: $OUT/$d not found -- build the dylib tier first" >&2
        exit 1
    fi
done
# Real prerequisite check (2026-09-21), added here and copied to every
# script whose comment points back at this block (build_bash.sh/
# build_getty.sh/build_login.sh/build_ping.sh/build_ps.sh/build_cron.sh)
# -- without it, a from-scratch build hits a bare clang "no such file"
# pile instead of one clear message, and gen_build_order.py never learns
# this is a real dependency (only a `-f "$OUT/..." not found -- run
# build_X.sh first` idiom feeds its dependency graph).
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
LINK_DYLIBS="$OUT/system/libsystem_kernel.dylib $OUT/system/libsystem_c.dylib $OUT/system/libsystem_pthread.dylib $OUT/system/libsystem_malloc.dylib $OUT/system/libsystem_platform.dylib $OUT/system/libdyld.dylib $OUT/system/libSystem.B.dylib $OUT/system/libinfo.dylib $OUT/system/libresolv.dylib"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o netstat.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/netstat_obj/*.o \
    $LINK_DYLIBS \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    `# sigsuspend$UNIX2003: no dylib exports it; aliased to this link's` \
    `# OWN local sigsuspend() (netstat_extra_syscalls.c), same pattern and` \
    `# reason as build_ping.sh's socket aliases.` \
    -Wl,-alias,_sigsuspend,'_sigsuspend$UNIX2003'
echo "wrote netstat.macho"

# Real dynamic-executable assertions (DAR-314). These are the INVERSE of
# the old static ones -- inverted, not deleted, so the check still has
# teeth. Same three real load commands build_simple_cmds.sh verifies.
bad=0
otool -l netstat.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: no LC_LOAD_DYLINKER /usr/lib/dyld -- not a real dynamic executable" >&2; bad=1; }
otool -l netstat.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: no LC_LOAD_DYLIB -- not a real dynamic executable" >&2; bad=1; }
otool -l netstat.macho | grep -q "cmd LC_MAIN" || {
    echo "error: no LC_MAIN (real unixproc entry)" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1

# Hard gate (CLAUDE.md): a clean link does not prove every two-level bind
# resolves at dyld load. This one really caught ___sysctl (see
# netstat_extra_syscalls.c).
./bind_target_audit.sh netstat.macho
