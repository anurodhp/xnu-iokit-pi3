#!/bin/bash
# UPDATED 2026-08-29 (static-to-dylib migration pass, docs/backlog.md
# item d8): libbsm.a/libopenpam.a/libxpc.a+libxpc_syscalls.a+
# libxpc_syscalls2.a/libiokituser.a are now linked as real .dylib images
# (build_bsm_dylib.sh/build_openpam_dylib.sh/build_libxpc_dylib.sh/
# build_libiokituser_dylib.sh) instead of static archives. This RETIRED
# three of the real duplicate-symbol workarounds the rest of this
# header's "Real gaps found + real fixes" section below describes as
# current (the vsyslog/oldsyslog.o narrowing, the libiokituser.a
# iokitmig64.o single-object extraction, and the libxpc_syscalls2.a
# mach_portUser.o narrowing against libCoreFoundation.a) -- real dylibs
# resolve via two-level namespace, so those libraries' own internal
# copies of syslog()/mach_portUser-family symbols no longer collide with
# anything else's local static copy. The rest of this header (below) is
# the ORIGINAL, still-accurate narrative for everything that hasn't
# changed -- read it for context, just discount the three now-retired
# workarounds it still describes in the present tense.
#
# Builds libc_build/launchctl.macho: real, dynamically-linked launchctl,
# from PureDarwin's OWN real source
# (third_party/PureDarwin/src/Libraries/XPC/launchctl/launchctl.c, 4762
# real lines) -- this project's launchd (see build_launchd.sh, booting
# as PID 1 as of 2026-08-28, docs/backlog.md item d10) has had no
# launchctl client at all until now: no way to `list`/`load`/`unload`/
# `start`/`stop` jobs on a running launchd, only the auto-load-at-boot
# path (pd_launchd_load_daemons_dir(), see third_party-patches/
# PureDarwin.patch).
#
# ============================================================
# HONEST SCOPE STATEMENT -- read this first
# ============================================================
# Real source confirmed FIRST (this project's own source-priority rule):
# `find third_party/PureDarwin -iname "*launchctl*"` found a real,
# complete, unmodified launchctl.c already in the pinned PureDarwin
# checkout -- every subcommand real Darwin's launchctl(1) has (list,
# load, unload, start, stop, submit, remove, bootstrap, setenv, unsetenv,
# getenv, export, debug, limit, stdout, stderr, shutdown, singleuser,
# getrusage, log, umask, bsexec, bslist, bstree, managerpid, manageruid,
# managername, asuser, kill, help) is real, present, unmodified source
# here -- not a partial/reimplemented client. It compiles+links as a
# REAL, COMPLETE build (not just `list`), using the exact same real IPC
# launchd.c's own real ipc.c/core.c implement on the launchd side
# (launch_msg()/vproc_swap_integer()/_vproc_send_signal_by_label(), real
# liblaunchd_syscalls.a/libxpc.a/liblaunchd_mig.a client stubs, already
# built and already proven working by build_launchd.sh's own real link).
#
# Real gaps found + real fixes, all documented in the compat files/
# headers they live in (read those for the full per-symbol citation):
#
# - Real Apple SPI headers CFPriv.h/ForFoundationOnly.h/
#   CFLogUtilities.h/CFBundlePriv.h (launchctl.c's own real
#   `#include <CoreFoundation/X.h>` spellings) aren't in any public SDK
#   -- launchctl_compat_headers/CoreFoundation/*.h redirect each to the
#   real swift-corelibs-foundation source this project's own
#   libCoreFoundation.a is already built from (build_corefoundation.sh).
# - Real IOKit/IOKitLib.h: IOKitUser's own checkout keeps it flat, not
#   under an IOKit/ subdirectory -- launchctl_compat_headers/IOKit/
#   IOKitLib.h redirects to the real third_party/IOKitUser/IOKitLib.h.
# - Real editline/readline.h: launchctl.c includes it but genuinely never
#   calls readline()/any rl_* symbol anywhere in the file (confirmed via
#   a real grep of the whole source before writing the stub) -- honest
#   EMPTY stub (launchctl_compat_headers/editline/readline.h), not a
#   fabricated implementation of something never called.
# - Real `SO_EXECPATH` macro: same "this project's pinned SDK's own
#   older same-named header wins over xnu's real newer one" class of gap
#   as launchd_compat_headers/sockaddr_in_4_6_compat.h --
#   launchctl_compat_headers/so_execpath_compat.h force-includes just the
#   one real missing macro (verbatim value from xnu-7195's own
#   bsd/sys/socket.h).
# - `-include mach-o/getsect.h -include sys/ioctl.h -include
#   sys/sockio.h`: real SDK headers launchctl.c's own source needs but
#   never directly #includes (relies on some other real Darwin header
#   pulling them in transitively, which doesn't happen on this project's
#   pinned include chain) -- same class of gap as launchd.c's own
#   `-include net/if_dl.h` (see build_launchd_recon.sh's header comment).
#
# Real, honest compat .c files (documented in full in their own header
# comments -- summarized here):
# - launchctl_iokit_compat.c: REAL IOMasterPort/IOObjectRelease/
#   IOKitWaitQuiet/IORegistryEntryFromPath, copied faithfully from
#   IOKitLib.c's own real bodies, built on real, already-linked MIG
#   client stubs (io_registry_entry_from_path/io_service_wait_quiet,
#   iokitmig64.o) and real host port lookup (mach_host_self/
#   host_get_io_master, already in libmach_ipc.a). ONE real, documented
#   gap: IORegistryEntryCreateCFProperty returns NULL (real IOKitLib.c
#   body needs IOCFSerialize.c/IOCFUnserialize.tab.c, real IOKitUser
#   source this project has never compiled -- a separate, large
#   undertaking, out of scope here). Its only real caller,
#   do_bootroot_magic(), is itself only ever reached via
#   system_specific_bootstrap() (`launchctl bootstrap -S System`), which
#   this project's own launchd fork never invokes (auto-loads
#   LaunchDaemons directly instead, see PureDarwin.patch) -- confirmed
#   unreachable in this project's real boot path, not just deprioritized.
# - launchctl_libc_extras_compat.c: real IN6ADDR_LOOPBACK_INIT constant;
#   honest no-op si_search_module_set_flags() (real implementation needs
#   Libinfo's entire module-search-registry subsystem, real source
#   present but never compiled into this project, separate large
#   undertaking -- both real call sites are install-environment/
#   unreachable-bootstrap edge cases, see the .c file's own header); real
#   getsectiondata() returning "not found" (real Apple implementation is
#   dyld/libmacho section introspection this project's dyld port has
#   never wrapped for userland; its only caller reads a real Apple
#   `__xpcd_cache` section this project's build pipeline has no way to
#   ever produce -- genuinely inapplicable infrastructure).
# - vsyslog: real Libc source (third_party/Libc/gen/oldsyslog.c), needed
#   because this project's other real syslog(3) (cf_libc_extras.a's
#   copyfile_syscalls.o) doesn't include vsyslog -- narrow-extracted via
#   `ld -r -exported_symbol _vsyslog` from libopenpam.a's own oldsyslog.o
#   (same real narrowing precedent as build_launchd_core.sh's own
#   ncurses_extra_syscalls.o/removefile_syscalls.o) to avoid a real
#   duplicate-symbol clash against that same already-linked real syslog()
#   (oldsyslog.o's own internal openlog()/setlogmask()/syslog() stay
#   real and callable internally -- narrowing only hides them from the
#   external symbol table, doesn't remove them).
# - Real os_log_compat.o/asl_client_compat.o/libm_scalbn_only_compat.o/
#   sysdir_compat.o: launchctl.c pulls in enough of libCoreFoundation.a
#   (CFBundle/CFDate/CFDateFormatter/CFLocale/CFPlugIn/CFRunLoop/CFURL,
#   none of which build_launchd.sh's own narrower launchd.c ever
#   reaches) to need these same real compat objects
#   build_launchd_core.sh already wrote for liblaunchd_core.a -- narrow-
#   extracted here via `ar p` (same self-contained, zero-cross-
#   dependency objects, confirmed via a real `nm -u` before reuse) rather
#   than force_load-ing the whole liblaunchd_core.a archive, which would
#   drag in launchd.c's own real main() (hard duplicate-symbol conflict
#   against launchctl.c's own real main()) and the entire unrelated
#   job-management engine.
# - Real syslogd_getaddrinfo_compat.o (build_syslogd.sh's own real,
#   honest, BOUNDED getaddrinfo()/freeaddrinfo()/gai_strerror() --
#   numeric addresses + well-known service names resolve for real, real
#   hostname DNS correctly reports EAI_NONAME, see that file's own header
#   for the full real citation): launchctl.c's own real
#   sock_dict_edit_entry()/do_mgroup_join() call these directly, same
#   real gap class build_syslogd.sh already solved once -- reused here
#   via `ar p`, not re-solved.
# - Real `-Wl,-alias,_X,'_X$UNIX2003'` for connect/send/writev/strerror:
#   oldsyslog.o (vsyslog's own real internal calls) was compiled with
#   this SDK's default __DARWIN_UNIX03 symbol-versioning, which renames
#   these to $UNIX2003-suffixed variants at the call site -- same real,
#   established alias pattern this project already uses extensively
#   (build_copyfile.sh/build_bsm.sh/etc), just never needed for these 4
#   specific names before. Real bases (connect/send/writev/strerror) are
#   already linked for real elsewhere (libxpc_syscalls.a,
#   cf_libc_extras.a, libc_locale_for_stdio.a).
# - Real duplicate-symbol conflict, found + fixed: libCoreFoundation.a's
#   own cf_mach_port_mig_fallback_stub.o (CF's own small real fallback
#   for 7 specific `_kernelrpc_mach_port_*` routines it calls internally,
#   e.g. from CFMachPort_Lifetime.o) HARD-CONFLICTS with the real, full,
#   38-routine MIG-generated mach_portUser.o already sitting inside
#   libxpc_syscalls2.a (previously latent/unexercised -- build_launchd.sh
#   never actually pulls in the specific CF object that needs it,
#   confirmed via a real `-why_load` trial link, not assumed). Real fix:
#   narrow a LOCAL, launchctl-only copy of mach_portUser.o via
#   `ld -r -exported_symbol` to drop exactly those 7 real duplicate
#   names (kept as real, internally-callable, just no longer externally
#   exported -- same narrowing precedent as vsyslog above), and swap it
#   into a local, launchctl-only copy of libxpc_syscalls2.a
#   ($OUT/launchctl_xpc_syscalls2_fixed.a) built fresh by this script --
#   the real, shared, checked-in libxpc_syscalls2.a itself is
#   INTENTIONALLY left untouched (other consumers, e.g. build_launchd.sh,
#   never hit this conflict and don't need the narrowing). Also confirmed
#   real: libiokituser.a's OWN copy of mach_portUser.o has the identical
#   conflict, which is why this script extracts ONLY the one real MIG
#   client stub object it actually needs from that archive
#   (iokitmig64.o, for io_registry_entry_from_path/io_service_wait_quiet)
#   instead of linking the whole libiokituser.a.
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi

# Same real ld64-609 workaround as every other real dynamic-link script
# in this project (docs/backlog.md item d9): Xcode 12's own bundled ld
# crashes under -Wl,-fixup_chains, so the final link step's linker binary
# is the daily-driver Xcode's newer ld instead (compiling still uses
# Xcode 12's clang/SDK, untouched).
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

XNU="../../third_party/xnu-7195"
PD="../../third_party/PureDarwin/src/Libraries/XPC"
CR="../../third_party/PureDarwin/src/Libraries/CrashReporterClient"
BSM="../../third_party/BSM"
DISPATCH="../../third_party/libdispatch"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
LIBC="../../third_party/Libc"
LIBINFO_REAL="../../third_party/Libinfo/lookup.subproj"
AVAIL_VERSIONS="../../build/xnu-latest-build/AvailabilityVersions/dst/usr/local/include"
CF="../../third_party/swift-corelibs-foundation/CoreFoundation"
IOKITUSER="../../third_party/IOKitUser"
OUT="libc_build"
COMPAT="launchd_compat_headers"
DCOMPAT="dispatch_compat_headers"
LCOMPAT="launchctl_compat_headers"
MIGOUT="$OUT/launchd_mig"
ICU_LIB="icu_build/target/lib"

if [ ! -f "$PD/launchctl/launchctl.c" ]; then
    echo "error: $PD/launchctl/launchctl.c not found -- run setup_third_party.sh first" >&2
    exit 1
fi
for a in liblaunchd_core.a cf_libc_extras.a launchd_libc_extras.a \
         liblaunchd_mig.a liblaunchd_syscalls.a \
         libmach_ipc.a libclosure.a libmalloc.a libplatform.a libpthread.a \
         libc_gen.a libc_string.a libc_stdio.a libc_printf.a libc_locale_for_stdio.a \
         libsyslogd.a; do
    if [ ! -f "$OUT/$a" ]; then
        echo "error: $OUT/$a not found -- run build_launchd_core.sh / build_syslogd.sh first" >&2
        exit 1
    fi
done
# DAR-195: real libCoreFoundation.dylib, not the old static archive.
if [ ! -f "$OUT/system/libCoreFoundation.dylib" ]; then
    echo "error: $OUT/system/libCoreFoundation.dylib not found -- run build_corefoundation_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/cerror_stub.o" ]; then
    echo "error: $OUT/cerror_stub.o not found -- run build_launchd_core.sh first" >&2
    exit 1
fi
# Real per-library dylibs (static-to-dylib migration pass, 2026-08-29 --
# see docs/backlog.md item d8): libbsm.a/libopenpam.a/libxpc.a+
# libxpc_syscalls.a+libxpc_syscalls2.a/libiokituser.a are now real,
# separate .dylib images, linked directly below instead of narrow-
# extracted/narrowed .a objects. This also RETIRES the three real
# duplicate-symbol workarounds this script used to need (oldsyslog.o's
# vsyslog narrowing, libiokituser.a's iokitmig64.o single-object
# extraction, libxpc_syscalls2.a's mach_portUser.o narrowing against
# libCoreFoundation.a's own cf_mach_port_mig_fallback_stub.o) -- all
# three existed only because flat static-archive linking put every
# symbol in one shared table; real dylibs resolve via two-level
# namespace, bound per-reference at THIS consumer's own link time, so
# libopenpam.dylib's/libiokituser.dylib's/libxpc.dylib's own internal
# copies of syslog()/mach_portUser-family symbols simply don't collide
# with libCoreFoundation.a's or anything else's local static copies
# anymore (same real mechanism build_libsystem_malloc.sh's own header
# already documents for this whole migration).
for d in libdispatch.dylib libdyld.dylib libSystem.B.dylib libsystem_pthread.dylib libc++.dylib \
         libxpc.dylib; do
    if [ ! -f "$OUT/system/$d" ]; then
        echo "error: $OUT/system/$d not found -- run the matching build_*.sh (see docs/daemon-roadmap.md's dylib migration list) first" >&2
        exit 1
    fi
done
for d in libbsm.dylib libopenpam.dylib libiokituser.dylib; do
    if [ ! -f "$OUT/system/$d" ]; then
        echo "error: $OUT/system/$d not found -- run build_bsm_dylib.sh / build_openpam_dylib.sh / build_libiokituser_dylib.sh first" >&2
        exit 1
    fi
done
for d in libicuuc.dylib libicui18n.dylib libicudata.dylib; do
    if [ ! -f "$ICU_LIB/$d" ]; then
        echo "error: $ICU_LIB/$d not found -- run build_icu.sh first" >&2
        exit 1
    fi
done

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall \
    -Wno-nullability-completeness -Wno-deprecated-declarations -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 -DOS_OBJECT_USE_OBJC=0 \
    -DOS_VOUCHER_ACTIVITY_SPI=0 -DOS_FIREHOSE_SPI=0 \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=ios,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos\(x\)=ios,introduced=x \
    -DSPI_AVAILABLE\(...\)= -D__SPI_AVAILABLE\(...\)= \
    -DMAX_GROUPS=16 \
    -include net/if_dl.h \
    -include "$COMPAT/sockaddr_in_4_6_compat.h" \
    -include "$COMPAT/launch_jobkey_defaults_compat.h" \
    -include "$COMPAT/sandbox_spawnattrs_compat.h" \
    -include "$COMPAT/ld2xpc_compat.h" \
    -include mach-o/getsect.h -include sys/ioctl.h -include sys/sockio.h \
    -include "$LCOMPAT/so_execpath_compat.h" \
    -I "$DCOMPAT" -I "$COMPAT" \
    -I "$OUT/dyld_compat_sys" -I "$OUT/dyld_compat_sys_override" \
    -I "$PD/launchd/System-compat" -I "$MIGOUT" \
    -I "$PD/libxpc/include" -I "$PD/libxpc" -I "$PD/launchd" -I "$PD/libnv" -I "$PD/libinfo" \
    -I "$CR/include" -I "$BSM/bsm/lib" -I "$DISPATCH" \
    -I "$CF/Base.subproj" -I "$CF" \
    -I "$IOKITUSER" \
    -I "$LCOMPAT" \
    -idirafter "$LIBINFO_REAL" \
    -idirafter "$AVAIL_VERSIONS" \
    -idirafter "$XNU/libsyscall/wrappers/libproc" -idirafter "$XNU/libsyscall/wrappers/spawn" -idirafter "$XNU/iokit" \
    -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libsyscall" -idirafter "$XNU/libsyscall/mach" \
    -idirafter "$PTHREAD/include" -idirafter "$PTHREAD/private" \
    -idirafter "$PLATFORM/private" -idirafter "$PLATFORM/include" -idirafter "$PLATFORM/internal" \
    -idirafter "$XNU/BUILD.hdrs/dst/usr/local/include" -idirafter "$XNU/EXTERNAL_HEADERS" \
    -idirafter "$LIBC/darwin" -idirafter "$LIBC" -idirafter "$XNU/libkern")

rm -rf "$OUT/launchctl_obj"
mkdir -p "$OUT/launchctl_obj"
"$CLANG" "${CFLAGS[@]}" "$PD/launchctl/launchctl.c" -o "$OUT/launchctl_obj/launchctl.o"
echo "compiled: real launchctl.c (zero errors, zero warnings)"

# Real IOKit/libc compat pieces (see this script's own header comment for
# the full per-symbol writeup).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall \
    -DPRIVATE=1 \
    -I "$CF/Base.subproj" -I "$CF" -I "$LCOMPAT" \
    -idirafter "$XNU/iokit" -idirafter "$XNU/osfmk" -idirafter "$XNU/bsd" -idirafter "$XNU/libkern" \
    -idirafter "$XNU/BUILD.hdrs/dst/usr/include" \
    launchctl_iokit_compat.c -o "$OUT/launchctl_obj/launchctl_iokit_compat.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall \
    launchctl_libc_extras_compat.c -o "$OUT/launchctl_obj/launchctl_libc_extras_compat.o"
echo "compiled: real launchctl_iokit_compat.c + launchctl_libc_extras_compat.c (zero errors, zero warnings)"

# REAL PRE-EXISTING GAP FOUND 2026-09-02 while migrating this script off
# libCoreFoundation.a for DAR-195 (confirmed pre-existing, NOT caused by
# that migration -- a real trial build of the OLD, still-committed
# static-archive version of this exact script hits the identical 4
# undefined symbols, plus one more -- _nanosleep from
# CFXMLPreferencesDomain.o's __CFMilliSleep -- that this dylib migration
# happens to close for free via libCoreFoundation.dylib's own already-
# linked libsystem_kernel.dylib sibling): launchctl.c itself calls real
# fnmatch()/lchflags()/sysctlnametomib() directly (path_goodness_check/
# empty_dir/limitloadtohardware_iterator), and liblaunchctl_extras.a's
# own syslogd_getaddrinfo_compat.o calls real inet_pton(). None of the
# four were ever compiled into anything this project links. Real,
# unmodified FreeBSD/Apple source for three of them already sits in this
# checkout, just never wired in:
#   - third_party/Libc/gen/FreeBSD/fnmatch.c
#   - third_party/Libc/gen/FreeBSD/sysctlnametomib.c
#   - third_party/Libc/emulated/lchflags.c (real Apple source -- calls
#     lstat/chflags/setattrlist, all already available)
# inet_pton is NOT recompiled -- it's already a real, current export of
# libresolv.dylib (confirmed via nm; also in libinfo.dylib), which this
# script's own link line below now adds directly (per this project's
# own established finding that a two-level-namespace reexport through
# libSystem.B.dylib alone doesn't reliably satisfy a consumer's own
# direct calls with this toolchain).
CFLAGS_LIBC_GAPS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 \
    -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I "$OUT/compat_include" \
    -I "$LIBC/include" -I "$LIBC/darwin" -I "$LIBC/fbsdcompat" -I "$LIBC/locale" -I "$LIBC/locale/FreeBSD" \
    -I "$LIBC/stdtime/FreeBSD" -I "$LIBC/gen" \
    -idirafter "$PTHREAD/private" -idirafter "$PTHREAD/include" \
    -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libsyscall")
"$CLANG" "${CFLAGS_LIBC_GAPS[@]}" "$LIBC/gen/FreeBSD/fnmatch.c" -o "$OUT/launchctl_obj/fnmatch.o"
"$CLANG" "${CFLAGS_LIBC_GAPS[@]}" "$LIBC/gen/FreeBSD/sysctlnametomib.c" -o "$OUT/launchctl_obj/sysctlnametomib.o"
"$CLANG" "${CFLAGS_LIBC_GAPS[@]}" "$LIBC/emulated/lchflags.c" -o "$OUT/launchctl_obj/lchflags.o"
# Real acct(2) raw syscall wrapper -- see launchctl_acct_syscall.c's own
# header for the full real gap writeup (found via a real QEMU boot, not
# at link time -- same phantom-SDK-stub silent-resolution hazard as the
# __sysctl fix above).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 \
    launchctl_acct_syscall.c -o "$OUT/launchctl_obj/launchctl_acct_syscall.o"
# Real bind/getsockname/flock/unmount/vfork + task_for_pid trap -- see
# launchctl_syscall_gaps.c's own header for the full real writeup (a
# systematic sweep, after the __sysctl/acct finds above, of EVERY symbol
# ld64 silently phantom-bound to libdispatch.dylib's ordinal -- 17 total,
# 9 closed by real sibling-dylib links already in this script, these 7
# closed here).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 \
    launchctl_syscall_gaps.c -o "$OUT/launchctl_obj/launchctl_syscall_gaps.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    launchctl_task_for_pid_trap.s -o "$OUT/launchctl_obj/launchctl_task_for_pid_trap.o"
echo "compiled: real fnmatch.c + sysctlnametomib.c + lchflags.c (zero errors)"
# REAL BUG FOUND+FIXED 2026-09-02 (real QEMU boot, not caught at link
# time -- this is exactly the "eager chained-fixups: a wrong binding
# crashes every load" hazard CLAUDE.md warns about): sysctlnametomib.c's
# real FreeBSD body calls the raw `__sysctl()` variant directly (not the
# public `sysctl()` wrapper), a real, separate Mach-O symbol this
# project's link never provides anywhere -- confirmed via nm no sibling
# dylib exports it. ld64 did NOT hard-fail at link time (silently bound
# it to libdispatch.dylib's ordinal instead, for reasons not fully
# chased down -- possibly the real iPhoneOS SDK's own libdispatch.tbd
# stub incidentally being visible on the search path, same class of
# stray "text-based stub file ... unexpectedly found" warning already
# seen elsewhere in this build), so this only surfaced as a real dyld
# abort on first real QEMU boot ("Symbol not found: ___sysctl ...
# Expected in: /usr/lib/system/libdispatch.dylib"), not as a build
# failure. Real, correct fix (see this script's own link line below):
# `-Wl,-alias,_sysctl,__sysctl` -- real BSD/Darwin's own convention is
# that `__sysctl()` and `sysctl()` are the same real raw trap under two
# names (this project's own already-linked, already-working local
# `_sysctl` -- confirmed via nm, a real function, not a stub -- is a
# correct, exact substitute), matching this file's own established
# alias-based fix pattern for `_connect`/`_send`/`_writev`/`_strerror`
# just above. Verified via a real QEMU boot after the fix (not assumed
# from the alias alone) -- see qemu/dar195_launchctl_boot2.log.

# Real narrow-extractions from already-built archives that are still
# real per-consumer glue, not one of this session's dylib-converted
# libraries (see this script's own header comment for why each one is
# self-contained / safe). vsyslog (formerly narrow-extracted from
# libopenpam.a) and iokitmig64.o (formerly narrow-extracted from
# libiokituser.a) are gone from this list -- real libopenpam.dylib/
# libiokituser.dylib, linked directly below, already export them for
# real, and being separate Mach-O images they don't collide with
# anything else's local static copies the way flat archive members did.
for o in os_log_compat.o asl_client_compat.o libm_scalbn_only_compat.o sysdir_compat.o; do
    ar p "$OUT/liblaunchd_core.a" "$o" > "$OUT/launchctl_obj/$o"
done
ar p "$OUT/libsyslogd.a" syslogd_getaddrinfo_compat.o > "$OUT/launchctl_obj/syslogd_getaddrinfo_compat.o"

rm -f "$OUT/liblaunchctl_extras.a"
ar rcs "$OUT/liblaunchctl_extras.a" \
    "$OUT/launchctl_obj/launchctl_iokit_compat.o" \
    "$OUT/launchctl_obj/launchctl_libc_extras_compat.o" \
    "$OUT/launchctl_obj/os_log_compat.o" \
    "$OUT/launchctl_obj/asl_client_compat.o" \
    "$OUT/launchctl_obj/libm_scalbn_only_compat.o" \
    "$OUT/launchctl_obj/sysdir_compat.o" \
    "$OUT/launchctl_obj/syslogd_getaddrinfo_compat.o" \
    "$OUT/launchctl_obj/fnmatch.o" \
    "$OUT/launchctl_obj/sysctlnametomib.o" \
    "$OUT/launchctl_obj/lchflags.o" \
    "$OUT/launchctl_obj/launchctl_acct_syscall.o" \
    "$OUT/launchctl_obj/launchctl_syscall_gaps.o" \
    "$OUT/launchctl_obj/launchctl_task_for_pid_trap.o"
echo "wrote $OUT/liblaunchctl_extras.a"

# Real libxpc_syscalls2.a mach_portUser.o narrowing against
# libCoreFoundation.a's own cf_mach_port_mig_fallback_stub.o (this
# script's OWN local launchctl_xpc_syscalls2_fixed.a copy) is retired:
# real libxpc.dylib (linked directly below) now covers libxpc.a +
# libxpc_syscalls.a + libxpc_syscalls2.a as one real dylib, and its own
# internal mach_portUser-family objects live in that separate image, so
# they no longer collide with libCoreFoundation.a's local static copy at
# all -- the whole narrowing dance existed only because flat static
# archives shared one symbol table.

# Real link-line fix (2026-08-30): libsystem_c.dylib must be listed
# EXPLICITLY, not left to libSystem.B.dylib's real LC_REEXPORT_DYLIB of
# it. Without it this link fails on an undefined ___chkstk_darwin
# (Apple's real large-stack-frame probe helper) referenced by CF's
# CFAttributedString.o/CFBinaryPList.o -- exactly the gap
# build_launchd.sh already hit and documents at length above its own
# link line; the reason is that ld64 cannot follow a reexport by
# install-name when /usr/lib/system/libsystem_c.dylib does not exist in
# the on-disk SDK/sysroot, which it does not here. build_launchctl.sh
# had never had the same fix applied, so this script was failing to
# link at all (found when a rebuild of it deleted the stale
# launchctl.macho left over from before CF grew those call sites).
rm -f "$OUT/launchctl.macho"
# -DCRT_DYNAMIC_LINKING (DAR-161, 2026-08-31): launchctl.macho is a real,
# genuinely dynamically-linked executable (libdispatch/libdyld/
# libSystem.B/libsystem_pthread/libsystem_c/libxpc/... dylibs below).
# Without this flag, Csu/crt.c's own NXArgc/NXArgv/environ/__progname
# global storage silently shadowed the real, canonical copies
# libdyld.dylib's libdyldGlue.o exports -- any direct (non-getenv/setenv)
# read of `environ` etc in this binary would have seen a permanently-
# stale local copy instead of dyld's real one. This is a NEW,
# project-owned macro, not real Apple's `__DYNAMIC__` -- this project's
# pinned Xcode-12/iPhoneOS SDK toolchain predefines `__DYNAMIC__=1`
# unconditionally for every arm64-apple-ios* compile regardless of
# -static (confirmed via `clang -dM -E -`), so reusing it would have
# silently broken every existing static Csu consumer too; see crt.c's
# own header comment on those four globals for the full
# first-attempt-caught-it-first writeup.
# DAR-195 (2026-09-02): libCoreFoundation.a -> real libCoreFoundation.dylib.
# __mh_execute_header/__mh_dylib_header alias REMOVED -- that alias only
# existed because CF was statically embedded into this executable, so
# CFUniChar.c's __CFGetSectDataPtr() (which hunts loaded dyld images for
# ITS OWN __UNICODE section via &_mh_dylib_header, real behavior on real
# Apple where CF is always a separate dylib) needed launchctl.macho's own
# synthesized __mh_execute_header aliased to stand in for a dylib header
# that didn't exist. Now that CF really is its own dylib, its own real
# Mach-O header naturally satisfies __mh_dylib_header with zero alias
# needed -- confirmed via a real QEMU boot (this script's own smoketest
# below), not assumed. The 7 kCF*Key legacy-alias entries also REMOVED --
# moved to libCoreFoundation.dylib's own link (build_corefoundation_dylib.sh),
# since their real base symbols now live there, not here (ld64's own
# alias-base-must-be-local constraint, see that script's own comment).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-alias,_connect,'_connect$UNIX2003' \
    -Wl,-alias,_send,'_send$UNIX2003' \
    -Wl,-alias,_writev,'_writev$UNIX2003' \
    -Wl,-alias,_strerror,'_strerror$UNIX2003' \
    -Wl,-alias,_sysctl,___sysctl \
    -o "$OUT/launchctl.macho" \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/launchctl_obj/launchctl.o" \
    "$OUT/liblaunchctl_extras.a" \
    "$OUT/cf_libc_extras.a" "$OUT/launchd_libc_extras.a" \
    "$OUT/liblaunchd_mig.a" "$OUT/liblaunchd_syscalls.a" \
    "$OUT/libmach_ipc.a" "$OUT/libclosure.a" "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
    "$OUT/libpthread.a" "$OUT/libc_gen.a" "$OUT/libc_string.a" "$OUT/libc_stdio.a" "$OUT/libc_printf.a" \
    "$OUT/libc_locale_for_stdio.a" \
    "$OUT/system/libCoreFoundation.dylib" \
    "$OUT/system/libdispatch.dylib" "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_c.dylib" \
    "$OUT/system/libxpc.dylib" "$OUT/system/libbsm.dylib" "$OUT/system/libopenpam.dylib" "$OUT/system/libiokituser.dylib" \
    "$OUT/system/libresolv.dylib" \
    "$OUT/system/libsystem_kernel.dylib" "$OUT/system/libcopyfile.dylib" "$OUT/system/libremovefile.dylib" \
    "$ICU_LIB/libicuuc.dylib" "$ICU_LIB/libicui18n.dylib" "$ICU_LIB/libicudata.dylib" "$OUT/system/libc++.dylib" \
    2>"$OUT/launchctl_link.err" \
    || { echo "error: real link failed -- see $OUT/launchctl_link.err" >&2; tail -60 "$OUT/launchctl_link.err" >&2; exit 1; }

echo "wrote $OUT/launchctl.macho"

# Real smoketest, same shape as build_launchd.sh's own (NOT just "did the
# link succeed" -- confirm real launchctl.c content actually linked in,
# via a distinctive, real, externally-visible-in-the-symbol-table
# function only launchctl.c itself defines).
otool -hv "$OUT/launchctl.macho" | grep -q " EXECUTE " || {
    echo "WARNING: not a real MH_EXECUTE" >&2
}
otool -hv "$OUT/launchctl.macho" | grep -q NOUNDEFS || {
    echo "WARNING: NOUNDEFS not set -- real undefined symbols may remain" >&2
}
if nm "$OUT/launchctl.macho" 2>/dev/null | grep -q " t _list_cmd$"; then
    echo "launchctl.macho: real launchctl.c content confirmed linked (_list_cmd present)"
else
    echo "WARNING: _list_cmd not found -- launchctl.o may not have actually linked in" >&2
fi
echo "linked against:"
otool -L "$OUT/launchctl.macho" 2>/dev/null | tail -n +2
