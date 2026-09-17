#!/bin/bash
# Builds ifconfig.macho: real, unmodified Apple `ifconfig` (third_party/
# network_cmds/ifconfig.tproj/, tag network_cmds-606.40.2, matched via
# `git ls-tree macos-112 -- network_cmds` against apple-oss-distributions/
# distribution-macOS, same method as every other Phase 4 pull -- see
# setup_third_party.sh's own header comment). Same Csu-static-link
# pattern as every other tier (no dyld, see architecture.md).
#
# SUPERSEDED 2026-09-16 (DAR-182): ifconfig_resolver_compat.c (deleted)
# used to stand in for getaddrinfo/getnameinfo/freeaddrinfo/gai_strerror/
# gethostbyname/getnetbyname with an honest "no resolver subsystem"
# degradation. DAR-306 (2026-09-14) built exactly that subsystem for
# real (libinfo.dylib's si_module/dns_module dispatcher) - ifconfig.macho
# now links libinfo.dylib + libresolv.dylib and gets all of those for
# real instead.
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
# DAR-246: libpthread.a's pthread_main_thread_bootstrap.o needs real
# `mmap`/`munmap` (ifconfig_main_wrapper.c's pthread_main_thread_bootstrap()
# call, same TSD-bootstrap gap every *_main_wrapper.c in this tier
# documents -- see build_getty.sh/build_ps.sh's own identical comment).
# Both are real, already-built, already-proven components: the raw
# __mmap/__munmap syscall stubs from build_simple_cmds.sh, wrapped by
# simple_cmds_mmap_compat.c's real mmap()/munmap() (real Apple
# libsyscall/wrappers/unix03/{mmap,munmap}.c semantics, see that file's
# own header comment) -- reused verbatim, not reinvented.
for dep in sys____mmap.o sys____munmap.o; do
    if [ ! -f "$OUT/simple_obj/$dep" ]; then
        echo "error: $OUT/simple_obj/$dep not found -- run build_simple_cmds.sh first (shared raw syscall stub)" >&2
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
    "ifconfig_syslog_compat.c:" \
    "ifconfig_strtod_compat.c:" \
    "simple_cmds_mmap_compat.c:" \
    "libc_locale_globals_compat.c:" \
    "$LIBC/locale/FreeBSD/table.c:" \
    "$LIBC/locale/FreeBSD/none.c:" \
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

# ___gdtoa_locks (COMMON/tentative symbol, glue.o): same real gap
# already fixed in build_cron.sh/build_removefile.sh/build_uname.sh/
# build_bash.sh/build_copyfile.sh/build_getty.sh -- ar x it directly
# (never -force_load).
mkdir -p "$OUT/ifconfig_glue_extract"
(cd "$OUT/ifconfig_glue_extract" && ar x "../libc_printf.a" glue.o)

# DYNAMIC LINK (DAR-314, 2026-09-15). This used to link fully static
# (-nostdlib -static -e start) against libc_printf.a/libc_gen.a/
# libc_stdio.a/libc_locale_for_stdio.a/libc_string.a/libpthread.a/
# libclosure.a/libmalloc.a/libplatform.a/cerror_stub.o plus a ~20-entry
# -Wl,-alias block hand-synthesizing the UNIX2003/INODE64 symbol-
# versioning gaps static linking forced this project to fake. All of
# that is obsolete now: real dyld-940 boots (Phase 6.5), ifconfig runs
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
LINK_DYLIBS="$OUT/system/libsystem_c.dylib $OUT/system/libsystem_pthread.dylib $OUT/system/libsystem_malloc.dylib $OUT/system/libsystem_platform.dylib $OUT/system/libdyld.dylib $OUT/system/libSystem.B.dylib $OUT/system/libinfo.dylib $OUT/system/libresolv.dylib"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o ifconfig.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/ifconfig_obj/*.o \
    $LINK_DYLIBS \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn
echo "wrote ifconfig.macho"

# Real dynamic-executable assertions (DAR-314). These are the INVERSE of
# the old static ones -- inverted, not deleted, so the check still has
# teeth. Same three real load commands build_simple_cmds.sh verifies.
bad=0
otool -l ifconfig.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: no LC_LOAD_DYLINKER /usr/lib/dyld -- not a real dynamic executable" >&2; bad=1; }
otool -l ifconfig.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: no LC_LOAD_DYLIB -- not a real dynamic executable" >&2; bad=1; }
otool -l ifconfig.macho | grep -q "cmd LC_MAIN" || {
    echo "error: no LC_MAIN (real unixproc entry)" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
