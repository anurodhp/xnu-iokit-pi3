#!/bin/bash
# Builds ifconfig.macho: real, unmodified Apple `ifconfig` (third_party/
# network_cmds/ifconfig.tproj/, tag network_cmds-606.40.2, matched via
# `git ls-tree macos-112 -- network_cmds` against apple-oss-distributions/
# distribution-macOS, same method as every other Phase 4 pull -- see
# setup_third_party.sh's own header comment). Same Csu-static-link
# pattern as every other tier (no dyld, see architecture.md).
#
# Two real, previously-latent bugs only this tool's real usage patterns
# exercised for the first time, both fixed at their real root cause
# (not worked around here):
#   - `__attribute__((constructor))` (af_inet/af_link/ifmedia/ifclone's
#     self-registration) never ran on this static/no-dyld target --
#     fixed in Csu itself (third_party-patches/Csu.patch, applied by
#     setup_third_party.sh) by having start.s walk
#     `__DATA,__mod_init_func` the way dyld normally would.
#   - the public, variadic `ioctl()` (real `int ioctl(int, unsigned long,
#     ...)`, real Apple's arm64 ABI passes true variadic args on the
#     stack, not in registers) was previously only ever called through
#     the private fixed-arity `_ioctl` alias (termios.c) -- ifconfig.c
#     is this project's first real caller of the PUBLIC name with a real
#     pointer argument, exposing a caller/callee ABI mismatch. Fixed in
#     tools/userland_staging/libc_gen_syscalls.c's `ioctl()` (now
#     genuinely variadic, matching real Apple's own
#     libsyscall/wrappers/ioctl.c shim shape) -- run build_libc_gen.sh
#     again after pulling that fix if libc_gen.a predates it.
#
# Scoped source list -- ifconfig.tproj ships several address-family and
# clone-interface plugins this board has no use for, each self-registering
# via a real `__constructor__` (confirmed by reading ifconfig.h/af_*.c/
# if*.c directly: af_register()/clone_setcallback() are called from
# each file's own `static __constructor void ..._ctor(void)`, never
# referenced by name from ifconfig.c itself -- so simply not compiling a
# file cleanly drops that family/clone-type with no dangling symbol):
#   - af_inet.c, af_link.c: compiled. IPv4 and link/ether/lladdr address
#     families -- this board's real config (Phase 6b landed IPv4 only).
#   - af_inet6.c: NOT compiled. INET6 domain registration is deliberately
#     disabled (see project memory/docs/roadmap.md Phase 6) -- no kernel
#     support to back it.
#   - ifmedia.c: compiled. Generic SIOCGIFMEDIA/SIOCSIFMEDIA status --
#     standard real ifconfig behavior; degrades to "media: none" if a
#     driver doesn't implement IONetworkController's media API rather
#     than erroring.
#   - ifclone.c: compiled. Generic clone-interface machinery (-C/-l,
#     SIOCIFGCLONERS) is independent of which clone *types* exist;
#     compiling it costs nothing even with zero cloners registered.
#   - ifbond.c, ifbridge.c, ifvlan.c, iffake.c, if6lowpan.c: NOT compiled.
#     Clone interface types (bond/bridge/vlan/6lowpan/loopback-fake) this
#     board has no driver support for and no use for -- real scope
#     decision, not a gap (same class of call as Phase 6's real INET6/
#     MULTIPATH decision).
#   - nexus.c: NOT compiled. Real Skywalk/nexus channel support --
#     needs kernel Skywalk infrastructure this project has never built
#     (same "not attempted" class as ps's real Mach IPC gap).
#
# ifconfig.c itself only reaches net/if_mib.h's struct ifmibdata_supplemental
# and net/network_agent.h's struct netagent_req under `#ifdef PRIVATE` --
# both real, unmodified kernel headers already vendored at $XNU/bsd/net,
# just gated the same way ps's proc_info.h needed (-DPRIVATE, confirmed by
# grep before adding, not guessed).
#
# getifaddrs()/if_nametoindex()/if_indextoname()/if_nameindex()/
# ether_ntoa()/ether_aton(): real, unmodified Apple Libinfo sources
# (third_party/Libinfo/gen.subproj/), never compiled into any prior tier
# -- Libinfo was pulled for Phase 4 step 1 but only libinfo_custom's own
# flat-file resolver (a separate, project-written file under
# tools/libinfo_custom/) has been used from it so far. getifaddrs.c's
# real NET_RT_IFLIST path (PF_ROUTE routing-socket dump via
# sysctl(CTL_NET, PF_ROUTE, 0, 0, NET_RT_IFLIST, 0)) is taken unconditionally
# here since NET_RT_IFLIST is a real, unconditionally-defined constant in
# $XNU/bsd/sys/socket.h (confirmed via grep, not the SIOCGIFCONF fallback
# path some older BSDs need).
#
# kldload(2) itself needs no stub: ifconfig.c's only call site (auto-
# attach-a-clone-kext on first use) is inside a real `#ifndef __APPLE__`
# block (confirmed by reading the file directly) -- already compiled out
# entirely when building against the Apple SDK, same as every other real
# system build of this file.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
LIBINFO="../../third_party/Libinfo"
IFCFG="../../third_party/network_cmds/ifconfig.tproj"
OUT="libc_build"
rm -rf "$OUT/ifconfig_obj"
mkdir -p "$OUT/ifconfig_obj" "$OUT/compat_include"

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
    -DPRIVATE \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $IFCFG \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall \
    -I $LIBINFO/Libinfo -I $LIBINFO/gen.subproj"

ok=0
skip=0
for spec in \
    "$IFCFG/ifconfig.c:-Dmain=ifconfig_cmd_real_main" \
    "$IFCFG/af_inet.c:" \
    "$IFCFG/af_link.c:" \
    "$IFCFG/ifmedia.c:" \
    "$IFCFG/ifclone.c:" \
    "$LIBINFO/gen.subproj/getifaddrs.c:" \
    "$LIBINFO/gen.subproj/if_nametoindex.c:" \
    "$LIBINFO/gen.subproj/if_indextoname.c:" \
    "$LIBINFO/gen.subproj/if_nameindex.c:" \
    "$LIBINFO/gen.subproj/ether_addr.c:" \
    "$LIBC/gen/FreeBSD/err.c:" \
    "$LIBC/stdlib/FreeBSD/getopt.c:" \
    "$LIBC/stdlib/FreeBSD/atoi.c:" \
    "$LIBC/stdlib/FreeBSD/strtoul.c:" \
    "$LIBC/net/FreeBSD/linkaddr.c:" \
    "$LIBC/net/FreeBSD/inet_addr.c:" \
    "$LIBC/net/FreeBSD/inet_makeaddr.c:" \
    "$LIBC/net/FreeBSD/inet_ntoa.c:" \
    "$LIBC/net/inet_ntop.c:" \
    "$LIBC/net/inet_pton.c:" \
    "ifconfig_main_wrapper.c:" \
    "ifconfig_extra_syscalls.c:" \
    "ifconfig_resolver_compat.c:" \
    "ifconfig_syslog_compat.c:" \
    "ifconfig_strtod_compat.c:" \
    ; do
    f="${spec%%:*}"
    extra="${spec#*:}"
    base=$(basename "$f" .c)
    out="$OUT/ifconfig_obj/${base}.o"
    if "$CLANG" $CFLAGS $extra "$f" -o "$out" 2>"$OUT/ifconfig_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/ifconfig_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/ifconfig_obj/${base}.err" >&2
    fi
done
echo "ifconfig (real Apple network_cmds/ifconfig.tproj): $ok compiled, $skip skipped"
if [ "$skip" -ne 0 ]; then
    echo "error: ifconfig has real compile failures, not a clean tier" >&2
    exit 1
fi

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/ifconfig_obj/ifconfig_sched_yield_trap.o"
"$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OUT/ifconfig_obj/ifconfig_sched_yield.o"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o ifconfig.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/ifconfig_obj/*.o \
    "$OUT/libc_printf.a" \
    "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libc_locale_for_stdio.a" "$OUT/libc_string.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" \
    "$OUT/pthread_tsd_shim_libc_bootstrap.o" \
    "$OUT/libpthread.a" \
    "$OUT/libclosure.a" \
    "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
    -Wl,-alias,_open,'_open$UNIX2003' \
    -Wl,-alias,_close,'_close$UNIX2003' \
    -Wl,-alias,_write,'_write$UNIX2003' \
    -Wl,-alias,___kill,'_kill$UNIX2003' \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
    -Wl,-alias,___kdebug_trace,_kdebug_trace \
    -Wl,-alias,_fcntl,'_fcntl$UNIX2003' \
    -Wl,-alias,_fstat,'_fstat$INODE64' \
    -Wl,-alias,_lstat,'_lstat$INODE64' \
    -Wl,-alias,_stat,'_stat$INODE64' \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    -Wl,-alias,_fputs,'_fputs$UNIX2003' \
    -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
    -Wl,-alias,'_getopt$UNIX2003',_getopt \
    -Wl,-alias,_strerror,'_strerror$UNIX2003' \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock
echo "wrote ifconfig.macho"

otool -l ifconfig.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l ifconfig.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u ifconfig.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u ifconfig.macho >&2
fi
