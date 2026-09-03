#!/bin/bash
# Builds libc_build/libsyslogd.a and syslogd.macho: the real, legacy-scope
# syslogd port (docs/daemon-roadmap.md Phase 6.7) -- everything
# build_syslogd_recon.sh already proved compiles clean under
# -fsyntax-only (10/10 files), now compiled to real .o's, archived, and
# linked into a real, runnable Mach-O. Same CFLAGS/include-path recipe as
# that recon script (reused verbatim, not re-derived) -- see its own
# header comment for the full file-list/compat-header rationale, not
# duplicated here.
#
# Real DYNAMIC link (not `-static`): daemon.c's own real event loop
# (dispatch_source_create() for SIGHUP + a mark timer, dispatch_main() at
# the tail of syslogd.c's main()) pulls in libdispatch_real.a's queue.o
# wholesale, which itself needs ~19 real pthread QOS-class extension
# symbols (pthread_attr_set_qos_class_np, qos_class_main,
# pthread_set_qos_class_self_np, etc) this project's curated static
# libpthread.a has never implemented -- confirmed by a real trial link
# attempt against libdispatch_real.a, not assumed. Same real, already-
# precedented fix build_launchd_core.sh's own header comment documents
# for the identical wall: link dynamically against the real
# libdispatch.dylib/libsystem_pthread.dylib/libSystem.B.dylib/
# libdyld.dylib instead (all real, already built, Phase 6.6/6.5) --
# real, unconditional requirement per this project's 2026-08-25 dylib-
# first directive anyway (prefer per-library dylibs over new static
# work), not just a workaround. The kernel's exec path genuinely
# supports LC_LOAD_DYLINKER for a normal execve() by a forked child
# (confirmed real, QEMU-verified via dyld_test_exec.macho, invoked from
# this project's own real interactive shell -- docs/daemon-roadmap.md's
# Phase 6.5 Phase C -- the exact same execve() mechanism spawn_cron()-
# style PID-1 spawning uses), so this is not a speculative choice.
#
# Real dependency: libc_build/libnotify_client.a (build_libnotify_client.sh,
# new -- the real public notify_post()/notify_register_dispatch() API
# daemon.c/syslogd.c/asl_action.c/remote.c/bsd_out.c all call
# unconditionally). Real, expected runtime behavior once booted: every
# notify_* call will fail cleanly (NOTIFY_STATUS_SERVER_NOT_FOUND-class),
# not hang or crash -- this project has no bootstrap-namespace registrar
# yet (docs/daemon-roadmap.md Phase 6.7's own still-open item), confirmed
# by reading _notify_lib_init_locked()'s real synchronous error path.
#
# Out of scope, unchanged from build_syslogd_recon.sh: dbserver.c (needs
# a real asl_ipc.h MIG regen, not attempted), aslmanager.tproj/,
# util.tproj/.
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi

# Real, verified fix (2026-08-28, see docs/backlog.md item d9 for the
# full trail and tools/userland_staging/build_simple_cmds.sh's own
# NEWLD_BINDIR header comment for the isolated repro this is based on):
# Xcode 12's own bundled ld64-609 has a real internal crash under
# -Wl,-fixup_chains that a much newer ld does not. Compiling still uses
# Xcode 12's clang/SDK above (untouched -- that vintage match is a
# separate, still-load-bearing invariant); only the final link step's
# linker binary changes, via clang's -B override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

XNU="../../third_party/xnu-7195"
SYSLOG="../../third_party/syslog"
DISPATCH="../../third_party/libdispatch"
PTHREAD="../../third_party/libpthread"
PLATFORM="../../third_party/libplatform"
LIBC="../../third_party/Libc"
NOTIFY="../../third_party/Libnotify"
PD="../../third_party/PureDarwin/src/Libraries/XPC"
AVAIL_VERSIONS="../../build/xnu-latest-build/AvailabilityVersions/dst/usr/local/include"
LAUNCHD_COMPAT="launchd_compat_headers"
DCOMPAT="dispatch_compat_headers"
SCOMPAT="syslog_compat_headers"
OUT="libc_build"

if [ ! -d "$SYSLOG/syslogd.tproj" ]; then
    echo "error: $SYSLOG/syslogd.tproj not found -- run setup_third_party.sh first" >&2
    exit 1
fi
for a in liblaunchd_mig liblaunchd_syscalls libmach_ipc libplatform libc_gen libc_string libc_stdio libc_printf libc_locale_for_stdio; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run build_${a}.sh first" >&2
        exit 1
    fi
done
# libmalloc.a and libpthread.a are NOT in the list above any more
# (DAR-217, 2026-09-02) -- see the link step's own "libmalloc.a REMOVED
# FROM THIS LINK" and "libpthread.a AND cerror_stub.o REMOVED FROM THIS
# LINK" comments for the real, QEMU-diagnosed bugs those two removals
# fix. build_libmalloc.sh is still a real prerequisite of this script
# though, for the one loose object still linked out of its output.
for o in chk_fail.o; do
    if [ ! -f "$OUT/$o" ]; then
        echo "error: $OUT/$o not found -- run build_libmalloc.sh first" >&2
        exit 1
    fi
done
# libnotify.a/libnotify_client.a/libxpc.a/libxpc_syscalls.a/libxpc_syscalls2.a/
# libopenpam.a/libncurses.a's real per-consumer curated objects are gone
# from this build -- static-linking cleanup pass (2026-08-29): real,
# separately-built dylibs replace all of these now (libsystem_notify.dylib
# replaces libnotify.a+libnotify_client.a, real Apple ships them as one
# product; libxpc.dylib replaces libxpc.a+libxpc_syscalls.a+
# libxpc_syscalls2.a). libresolv/libcopyfile/libremovefile's curated
# extractions below are also gone -- all three are already real
# `-Wl,-reexport_library` members of libSystem.B.dylib (build_libsystem.sh),
# already linked below, so syslogd reaches them through the umbrella with
# no direct link needed.
# Every dylib below is listed EXPLICITLY on the link line (see the link
# step's own comments for why reaching them through libSystem.B.dylib's
# LC_REEXPORT_DYLIB is not enough for ld64 in this project's sysroot), so
# every one of them is a real, fail-fast prerequisite.
for d in libdispatch libdyld libSystem.B libsystem_kernel libsystem_pthread libsystem_malloc \
         libsystem_c libcopyfile libresolv libsystem_notify libxpc libopenpam libncurses; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run its build_*.sh first" >&2
        exit 1
    fi
done

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -D__API_UNAVAILABLE_PLATFORM_bridgeos=ios,unavailable \
    -D__API_AVAILABLE_PLATFORM_bridgeos\(x\)=ios,introduced=x \
    -DSPI_AVAILABLE\(...\)= -D__SPI_AVAILABLE\(...\)= \
    -include "$SCOMPAT/sys/kevent_qos_compat.h" \
    -I "$SCOMPAT" -I "$LAUNCHD_COMPAT" -I "$DCOMPAT" \
    -I "$SYSLOG/syslogd.tproj" -I "$SYSLOG/aslcommon" -I "$SYSLOG/libsystem_asl.tproj/include" \
    -I "$NOTIFY" -I "$PD/launchd/System-compat" -I "$PD/libxpc/include" \
    -I "$DISPATCH" -I "$DISPATCH/private" \
    -idirafter "$AVAIL_VERSIONS" \
    -idirafter "$XNU/libsyscall/wrappers/libproc" -idirafter "$XNU/libsyscall/wrappers/spawn" \
    -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libsyscall" -idirafter "$XNU/libsyscall/mach" \
    -idirafter "$PTHREAD/include" -idirafter "$PTHREAD/private" \
    -idirafter "$PLATFORM/private" -idirafter "$PLATFORM/include" -idirafter "$PLATFORM/internal" \
    -idirafter "$XNU/BUILD.hdrs/dst/usr/local/include" -idirafter "$XNU/EXTERNAL_HEADERS" \
    -idirafter "$LIBC/darwin" -idirafter "$LIBC" -idirafter "$XNU/libkern")

mkdir -p "$OUT/syslogd_obj"
ok=0
fail=0
for f in klog_in bsd_in bsd_out udp_in daemon syslogd asl_action remote; do
    out="$OUT/syslogd_obj/${f}.o"
    if "$CLANG" "${CFLAGS[@]}" "$SYSLOG/syslogd.tproj/$f.c" -o "$out" 2>"$OUT/syslogd_obj/${f}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/syslogd_obj/${f}.err"
    else
        fail=$((fail + 1))
        echo "  gap: syslogd.tproj/$f -- $(grep -m1 'error:' "$OUT/syslogd_obj/${f}.err")"
    fi
done
for f in asl_common asl_memory; do
    out="$OUT/syslogd_obj/${f}.o"
    if "$CLANG" "${CFLAGS[@]}" "$SYSLOG/aslcommon/$f.c" -o "$out" 2>"$OUT/syslogd_obj/${f}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/syslogd_obj/${f}.err"
    else
        fail=$((fail + 1))
        echo "  gap: aslcommon/$f -- $(grep -m1 'error:' "$OUT/syslogd_obj/${f}.err")"
    fi
done
# Real, self-contained ASL client-library files (Mach/XPC-free,
# confirmed by a real trial compile before adding here) that daemon.o/
# asl_action.o/asl_common.o/asl_memory.o/bsd_out.o/syslogd.o directly
# reference: asl_msg.c (the core asl_msg_t message ADT -- genuinely
# required by nearly every file in this build, not optional), asl_core.c
# (real hashing/time/encode utilities the message and store layers both
# need), asl_file.c/asl_store.c (the real on-disk ASL binary log store --
# lets db_save_message() below persist to a real file, not just memory),
# asl_string.c (real string-builder ADT), asl_util.c (real facility
# name<->number tables + asl_trigger_aslmanager()). None of this was part
# of build_syslogd_recon.sh's original 10-file scope -- a real gap this
# pass's own trial link found (see this script's own header comment).
for f in asl_msg asl_msg_list asl_legacy1 asl_object asl_core asl_file asl_store asl_string asl_util; do
    out="$OUT/syslogd_obj/${f}.o"
    if "$CLANG" "${CFLAGS[@]}" "$SYSLOG/libsystem_asl.tproj/src/$f.c" -o "$out" 2>"$OUT/syslogd_obj/${f}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/syslogd_obj/${f}.err"
    else
        fail=$((fail + 1))
        echo "  gap: libsystem_asl.tproj/src/$f -- $(grep -m1 'error:' "$OUT/syslogd_obj/${f}.err")"
    fi
done
# This project's own new real compat (see each file's own header comment
# for the exact gap it closes): dbserver.c's non-Mach persistence/no-op
# subset, a bounded numeric-address-only getaddrinfo, honest os_log/
# os_variant no-ops, and remote.c's own real "module unavailable"
# contract applied to remote_init() without needing remote.c's own
# pthread_attr_*/db_query internals.
for f in syslogd_dbserver_compat syslogd_getaddrinfo_compat syslogd_oslog_compat syslogd_remote_stub syslogd_alloc_once_compat syslogd_misc_libc_compat syslogd_mach_port_compat; do
    out="$OUT/syslogd_obj/${f}.o"
    if "$CLANG" "${CFLAGS[@]}" "${f}.c" -o "$out" 2>"$OUT/syslogd_obj/${f}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/syslogd_obj/${f}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $f -- $(grep -m1 'error:' "$OUT/syslogd_obj/${f}.err")"
    fi
done
if "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c syslogd_mach_port_extra_trap.s -o "$OUT/syslogd_obj/syslogd_mach_port_extra_trap.o" 2>"$OUT/syslogd_obj/syslogd_mach_port_extra_trap.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/syslogd_obj/syslogd_mach_port_extra_trap.err"
else
    fail=$((fail + 1))
    echo "  gap: syslogd_mach_port_extra_trap -- $(grep -m1 'error:' "$OUT/syslogd_obj/syslogd_mach_port_extra_trap.err")"
fi
# Real gap found during this pass's own QEMU boot test (not the trial
# link, which "succeeded" in ld64's own sense -- a real, dynamically-
# linked executable doesn't require every symbol to resolve into a
# directly-linked dylib at static-link time, only at real dyld load time,
# see this script's own header comment): libxpc_syscalls.a's real
# mach_error_string.o expects a real `_mach_errors` error-description
# table to come from a dylib at runtime ("Expected in:
# /usr/lib/libSystem.B.dylib", confirmed via the real QEMU crash log),
# but neither libSystem.B.dylib nor libsystem_kernel.dylib in this
# project's build actually exports it (confirmed by nm -gU on both,
# zero hits) -- a real, narrow export-parity gap, same class already
# documented elsewhere (docs/daemon-roadmap.md's item d7). Real fix:
# compile the actual table (third_party/xnu-7195/libsyscall/mach/
# error_codes.c, real, unmodified Apple source -- the "int" field of
# each per-subsystem error_system struct being initialized from a NULL
# void* is a real, harmless, pre-existing Apple source quirk, warns but
# doesn't error) directly into this static link, so the symbol resolves
# locally and dyld never needs to find it in a dylib at all.
if "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I "$XNU/libsyscall/mach" -idirafter "$XNU/iokit" -idirafter "$XNU/BUILD.hdrs/dst/usr/include" -idirafter "$XNU/osfmk" \
    "$XNU/libsyscall/mach/error_codes.c" -o "$OUT/syslogd_obj/error_codes.o" 2>"$OUT/syslogd_obj/error_codes.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/syslogd_obj/error_codes.err"
else
    fail=$((fail + 1))
    echo "  gap: error_codes -- $(grep -m1 'error:' "$OUT/syslogd_obj/error_codes.err")"
fi
echo "syslogd: $ok compiled, $fail gaps (see $OUT/syslogd_obj/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f "$OUT/libsyslogd.a"
ar rcs "$OUT/libsyslogd.a" "$OUT"/syslogd_obj/*.o
echo "wrote $OUT/libsyslogd.a ($(ls "$OUT"/syslogd_obj/*.o | wc -l | tr -d ' ') objects -- includes real remote.o/dbserver-facing symbols for archival completeness per docs/daemon-roadmap.md's own 'compile the 10 clean files to real .o's and archive them' next step, even though the real remote.o is deliberately NOT part of the executable link below)"

# Real, curated extraction of a handful of already-built-elsewhere real
# functions this trial link needs -- same selective `ar x` pattern
# build_launchd_core.sh already established (not whole-archive linking,
# same C++/xattr pollution risk documented in docs/backlog.md item d7).
# Static-linking cleanup pass (2026-08-29): the ls_fenv_compat.o/mkdir.o/
# fchown.o/symlink.o/parse.o/realpath.o/pack.o/statfs64.o/atoi.o/
# gethostname.o/inet_ntop.o/inet_pton.o/if_nametoindex.o/getifaddrs.o/
# Libc_sys_sigaction.o/libplatform_sigaction.o/
# simple_cmds_nanosleep_pub_compat_c.o members formerly extracted here
# from libopenpam.a/libremovefile.a/libcopyfile.a/libresolv.a/libncurses.a
# are gone -- those five libraries are all real dylibs now, linked
# directly (libopenpam.dylib/libncurses.dylib below) or reached through
# libSystem.B.dylib's own real reexport (libresolv/libcopyfile/
# libremovefile, already linked). Real gaps found during this pass's own
# QEMU boot test that are NOT part of this migration's scope (this
# project's own internal glue archives, not independent libraries, stay
# static by design -- docs/backlog.md item d8): getpwnam
# (cf_libc_extras.a's copyfile_syscalls.o), chown/dup/geteuid/getuid/
# kevent/mkdir/umask/uuid_clear/signal (liblaunchd_syscalls.a -- NOT
# linked as a whole archive because its own private_names_compat.o real
# __write alias collides with libc_locale_for_stdio.a's own real
# __write, a genuine duplicate-symbol conflict found by this pass's own
# trial link; every other real member is independent and safe),
# mach_port_destruct/mach_port_move_member (dyld_core.a's real MIG
# client stub, mach_portUser.o).
rm -rf "$OUT/syslogd_libc_extras_obj"
mkdir -p "$OUT/syslogd_libc_extras_obj"
( cd "$OUT/syslogd_libc_extras_obj" && \
  ar x "../cf_libc_extras.a" copyfile_syscalls.o geteuid.o getuid.o && \
  ar x "../liblaunchd_syscalls.a" sys__chown.o sys__dup.o sys__kevent.o sys__umask.o uuid_clear.o signal.o && \
  ar x "../launchd_libc_extras.a" qsort.o heapsort.o )
rm -f "$OUT/syslogd_libc_extras.a"
ar rcs "$OUT/syslogd_libc_extras.a" "$OUT"/syslogd_libc_extras_obj/*.o
echo "wrote $OUT/syslogd_libc_extras.a"

# Real trial link. Deliberately links the individual real objects this
# pass's own scope covers -- NOT -force_load on the whole libsyslogd.a
# archive -- because that archive also contains the real remote.o (kept
# archived for completeness, see above) whose own internals need real
# pthread_attr_*/dbserver.c support this pass doesn't build (see
# syslogd_remote_stub.c's own header comment for the exact real, already-
# established "module unavailable" contract this uses instead).
# libnotify_client.a IS force_load'd: its own real entry points
# (notify_post/notify_register_*) are referenced by name directly from
# daemon.c/syslogd.c, so force_load isn't strictly required there either,
# but keeps this consistent with build_launchd_core.sh's own "prove the
# whole archive's real content links clean standalone" precedent for a
# freshly-built archive.
#
# libmalloc.a REMOVED FROM THIS LINK ENTIRELY 2026-09-02 (DAR-217), same
# "remove, don't reconcile" fix (and same reasoning) build_launchd.sh
# already applied to its own link on 2026-08-31 -- read that script's own
# "libmalloc.a REMOVED FROM THIS LINK ENTIRELY" comment first; this one
# only records what was different here. syslogd.macho is a real DYNAMIC
# executable that already loads the real libsystem_malloc.dylib
# transitively (via libSystem.B.dylib's own LC_REEXPORT_DYLIB) while ALSO
# statically linking libmalloc.a -- the exact "two heaps + duplicate mach
# state" bug class. Two real, concrete failures this removal fixes, both
# found by `nm -m`/`otool -tV` on the pre-fix binary while root-causing
# DAR-217, not speculatively:
#
#   1. libmalloc.a's mach_init.o (a verbatim copy of xnu-7195's
#      libsyscall/mach/mach_init.c, built into that archive back when
#      there was no libsystem_kernel.dylib) gave syslogd.macho its OWN
#      __DATA,__common _mach_task_self_ / _bootstrap_port / _NDR_record /
#      _vm_page_* / _vm_kernel_page_*, shadowing libsystem_kernel.dylib's.
#      Nothing ever initialized them: the local mach_init() was in the
#      binary but unreferenced (syslogd's only real constructor is
#      libc_locale's ___xlocale_init_constructor -- confirmed by dumping
#      __TEXT,__init_offsets), and the mach_init() that DOES run, from
#      libSystem.B.dylib's own real constructor, writes the
#      libsystem_kernel.dylib copy. Real symptom: launch_config()
#      (third_party/syslog/syslogd.tproj/syslogd.c) loads
#      _mach_task_self_ via a direct `adrp/add` of the LOCAL copy
#      (verified in the disassembly), so its
#      mach_port_allocate(mach_task_self(), ...) ran with task == 0 and
#      returned MACH_SEND_INVALID_DEST -> exit(1) -> launchd respawn loop.
#   2. `_malloc`/`_free`/`_calloc`/`_realloc` (plus the whole
#      malloc-zone/_simple_* family, ~55 symbols) were duplicated between
#      this binary and libsystem_malloc.dylib -- and syslogd genuinely
#      frees cross-heap: launch_config() calls launch_data_free() on a
#      launch_data_t that libxpc.dylib allocated from libsystem_malloc's
#      zone. Same DAR-131 class as launchd's own fixed copy.
#
# Fix, exactly as in build_launchd.sh: drop the archive, link the real
# libsystem_malloc.dylib directly as a sibling (ld64 cannot resolve a
# reexport-only symbol by install-name alone in this project's sysroot --
# see build_launchd.sh for why), and relink the one genuinely
# per-image member back in from its already-built standalone object:
# $OUT/chk_fail.o (___chk_fail_overflow/___chk_overlap/
# ___chk_assert_no_overlap/___chk_fail_overlap -- `.private_extern`-by-
# construction helpers that per docs/static-linking-cleanup-plan.md
# legitimately belong in every image carrying `_chk` members). Unlike
# launchd, this link needed NO sys_getpid.o/__current_pid rescue: syslogd
# links no raw __fork trap, so nothing here depends on __current_pid
# being same-image-addressable, and plain _getpid resolves through
# libsystem_malloc.dylib. Verified after the relink with the same `nm -m`
# duplicate audit: _bootstrap_port/_mach_task_self_/_NDR_record/
# _vm_page_*/_malloc/_free are all gone from syslogd.macho's own defined
# set, and `dyld_info -fixups` now shows _mach_task_self_ as a real
# cross-image bind (through libdispatch.dylib's own real
# LC_REEXPORT_DYLIB of libsystem_kernel.dylib, i.e. the single
# process-wide copy). Known remaining, PRE-EXISTING duplicates left
# alone here because they are a different, non-blocking scope: _errno
# (cerror_stub.o), __os_alloc_once_table (syslogd_alloc_once_compat.o),
# __mach_errors (also duplicated in launchd.macho today).
#
# ORDER MATTERS on the dylib list below, and getting it wrong is silent
# at link time: libsystem_malloc.dylib is listed BEFORE libdispatch.dylib
# on purpose. ld64 resolves an undefined symbol against the transitive
# LC_LOAD_DYLIB closure of each listed dylib in order and then records
# the bind against the dylib it was LISTED under -- but real dyld only
# follows LC_REEXPORT_DYLIB when resolving that bind. libdispatch.dylib
# plain-links (does NOT reexport) libsystem_malloc.dylib, so with
# libdispatch first, `___assert_rtn` -- a real export of
# libsystem_malloc.dylib and of nothing else in this project -- was
# recorded as `bind libdispatch/___assert_rtn`, linked clean, and then
# died at real dyld load time:
#   dyld[7]: Symbol not found: ___assert_rtn
#     Referenced from: /usr/sbin/syslogd
#     Expected in: /usr/lib/system/libdispatch.dylib
# (real QEMU boot, 2026-09-02, running /usr/sbin/syslogd by hand from the
# root shell -- as a launchd job the same abort was invisible, showing up
# only as an EXC_CORPSE_NOTIFY line on the console). This became reachable
# only once libmalloc.a stopped supplying ___assert_rtn statically. The
# bind-target audit at the bottom of this script now catches this class
# at build time instead.

# libpthread.a AND cerror_stub.o REMOVED FROM THIS LINK, and
# libc_locale_for_stdio.a replaced by a syslogd-local copy with its two
# pthread-TSD-shim members dropped -- 2026-09-02 (DAR-217), the exact
# same three removals build_launchd.sh already made on 2026-08-30 for the
# exact same reason. Read docs/launchd-pid1-postmortem.md's "Bug A"
# (TSD-base clobber) and "Bug B" (dual pthread_create) plus that script's
# own comments before touching this; only what is different for syslogd
# is recorded here.
#
# Pre-fix `nm -m` on syslogd.macho showed it defining SIXTY-FOUR symbols
# that libsystem_pthread.dylib also exports -- not helpers, the entire
# threading core: _pthread_create, _pthread_self, _pthread_getspecific/
# _setspecific, _pthread_key_create, _pthread_once, the whole
# _pthread_mutex_*/_pthread_rwlock_* family, the raw ___bsdthread_create/
# ___bsdthread_register/___bsdthread_terminate traps, every ___psynch_*
# trap, and the shared state they hang off (__main_thread,
# ___is_threaded). launchd.macho, by contrast, duplicates 30 symbols with
# that dylib and NONE of them are threading-core (only MIG host_* stubs
# and pthread_attr_* helpers) -- syslogd was simply never swept.
#
# That is two complete, independently-initialized pthread runtimes in one
# process: separate TSD key tables, separate ___bsdthread_register()
# registrations racing to be the one the kernel trusts, separate
# __main_thread. It stayed invisible for as long as syslogd died in
# launch_config() before ever becoming multi-threaded. Once DAR-217's
# bootstrap-port fix let the real launchd check-in succeed, syslogd
# reached init_modules() -- which creates real dispatch sources and real
# libdispatch worker threads (klog_in/bsd_in/udp_in) -- and started dying
# with a bare "EXC_CORPSE_NOTIFY ... for pid N" at a DIFFERENT point on
# every boot (twice mid-notify_register_dispatch, once between two of
# them), the signature of an asynchronous crash on a worker thread rather
# than a deterministic bug on the main one. Confirmed by a real
# instrumented boot that ruled out libnotify's own error paths first:
# every notify_* call really does fail cleanly
# (bootstrap_look_up2 -> 0x44e BOOTSTRAP_UNKNOWN_SERVICE, there being no
# notifyd, then NOTIFY_STATUS_SERVER_NOT_FOUND) and returns normally.
#
# Fix is the same "remove, don't reconcile" as launchd's: let
# libsystem_pthread.dylib be the one pthread runtime in the process.
if [ ! -f "$OUT/libc_locale_for_stdio.a" ]; then
    echo "error: $OUT/libc_locale_for_stdio.a not found -- run build_libc_stdio.sh first" >&2
    exit 1
fi
# Derived syslogd-local copy: the shared archive itself stays untouched
# because other, still-fully-static consumers (build_simple_cmds.sh,
# pd_console_login.macho, the smoketests) genuinely still need their own
# private TSD shim. Own filename so this script and build_launchd.sh
# never race over one output file.
rm -f "$OUT/libc_locale_for_stdio_syslogd.a"
cp "$OUT/libc_locale_for_stdio.a" "$OUT/libc_locale_for_stdio_syslogd.a"
ar d "$OUT/libc_locale_for_stdio_syslogd.a" pthread_tsd_shim.o
ar d "$OUT/libc_locale_for_stdio_syslogd.a" pthread_tsd_shim_libc_bootstrap.o
# ...and this archive's own private copy of cerror_stub.o, for the same
# reason build_launchd.sh drops it: dropping only the loose
# $OUT/cerror_stub.o below would still leave ___error/_errno defined
# locally out of this archive, i.e. syslogd's static half and every
# sibling dylib would keep writing two different errno slots.
ar d "$OUT/libc_locale_for_stdio_syslogd.a" cerror_stub.o

rm -f syslogd.macho
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o syslogd.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/syslogd_obj/syslogd.o" "$OUT/syslogd_obj/daemon.o" \
    "$OUT/syslogd_obj/bsd_in.o" "$OUT/syslogd_obj/bsd_out.o" \
    "$OUT/syslogd_obj/udp_in.o" "$OUT/syslogd_obj/klog_in.o" \
    "$OUT/syslogd_obj/asl_action.o" "$OUT/syslogd_obj/asl_common.o" "$OUT/syslogd_obj/asl_memory.o" \
    "$OUT/syslogd_obj/asl_msg.o" "$OUT/syslogd_obj/asl_msg_list.o" \
    "$OUT/syslogd_obj/asl_legacy1.o" "$OUT/syslogd_obj/asl_object.o" "$OUT/syslogd_obj/asl_core.o" \
    "$OUT/syslogd_obj/asl_file.o" "$OUT/syslogd_obj/asl_store.o" \
    "$OUT/syslogd_obj/asl_string.o" "$OUT/syslogd_obj/asl_util.o" \
    "$OUT/syslogd_obj/syslogd_dbserver_compat.o" "$OUT/syslogd_obj/syslogd_getaddrinfo_compat.o" \
    "$OUT/syslogd_obj/syslogd_oslog_compat.o" "$OUT/syslogd_obj/syslogd_remote_stub.o" \
    "$OUT/syslogd_obj/syslogd_alloc_once_compat.o" "$OUT/syslogd_obj/syslogd_misc_libc_compat.o" \
    "$OUT/syslogd_obj/syslogd_mach_port_compat.o" "$OUT/syslogd_obj/syslogd_mach_port_extra_trap.o" \
    "$OUT/syslogd_obj/error_codes.o" \
    "$OUT/syslogd_libc_extras.a" \
    "$OUT/libc_regex.a" \
    "$OUT/liblaunchd_mig.a" \
    "$OUT/libmach_ipc.a" "$OUT/libclosure.a" "$OUT/chk_fail.o" "$OUT/libplatform.a" \
    "$OUT/libc_gen.a" "$OUT/libc_string.a" "$OUT/libc_stdio.a" "$OUT/libc_printf.a" \
    "$OUT/libc_locale_for_stdio_syslogd.a" \
    "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" \
    `# libsystem_c.dylib/libcopyfile.dylib listed EXPLICITLY (not left to` \
    `# libSystem.B.dylib's LC_REEXPORT_DYLIB of them) for the same reason` \
    `# build_launchd.sh already lists libsystem_c.dylib/libremovefile.dylib` \
    `# explicitly -- see its own ___chkstk_darwin comment: ld64 cannot` \
    `# follow a reexport by install-name alone here, because` \
    `# /usr/lib/system/libsystem_c.dylib does not exist in this project's` \
    `# on-disk sysroot, only at this project's own build output path. Real` \
    `# symptom without them, once libpthread.a/cerror_stub.o stopped` \
    `# supplying them transitively: _read/_mkdir (libsystem_c) and` \
    `# _fchown/_symlink (libcopyfile) and _gethostname/_inet_ntop/_inet_pton` \
    `# (libresolv) undefined at link.` \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libcopyfile.dylib" "$OUT/system/libresolv.dylib" \
    "$OUT/system/libdispatch.dylib" "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib" \
    "$OUT/system/libsystem_notify.dylib" "$OUT/system/libxpc.dylib" \
    "$OUT/system/libopenpam.dylib" "$OUT/system/libncurses.dylib" \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
    -Wl,-alias,_getattrlist,'_getattrlist$UNIX2003' \
    2>"$OUT/syslogd_link.err" \
    && echo "real link: 0 undefined symbols -- syslogd.macho fully closed" \
    || { n=$(grep -c 'Undefined symbol' "$OUT/syslogd_link.err" || true); echo "real link found $n undefined symbol group(s):"; grep '^  "' "$OUT/syslogd_link.err" | sed 's/,.*//' | sort -u; tail -80 "$OUT/syslogd_link.err" >&2; exit 1; }

otool -l syslogd.macho | grep -q LC_LOAD_DYLINKER || \
    echo "WARNING: no LC_LOAD_DYLINKER -- expected a real dynamic executable" >&2
# plain nm -u is misleading for a real dyld-loaded executable: every
# entry it lists includes symbols already bound to a specific sibling
# dylib at link time (nm -m annotates these "(from libdispatch)" etc) --
# those resolve at load time exactly like any other real Apple two-level-
# namespace binding, not a gap. Only an entry nm -m marks "(dynamically
# looked up)" is a real unresolved symbol (see build_libsystem_pthread.sh/
# build_libdispatch_dylib.sh's own identical fix for the same
# nm -u-vs-nm -m distinction).
real_gaps=$(nm -m syslogd.macho 2>/dev/null | grep undefined | grep -v "(from " || true)
if [ -n "$real_gaps" ]; then
    echo "WARNING: real undefined symbols present (no sibling dylib resolves these):" >&2
    echo "$real_gaps" >&2
fi

# ...and `nm -m`'s "(from libdispatch)" annotation is NOT sufficient
# either: it repeats ld64's own recorded bind target without checking
# that real dyld can actually find the symbol there. bind_target_audit.sh
# (DAR-217, written after exactly that false "clean" on this binary --
# see the link step's own "ORDER MATTERS" comment) re-checks every
# two-level bind against the target dylib's real LC_REEXPORT_DYLIB
# closure, which is the only thing dyld searches.
./bind_target_audit.sh syslogd.macho || {
    echo "error: syslogd.macho has an unresolvable two-level bind -- it would abort at dyld load" >&2
    exit 1
}
echo "wrote syslogd.macho"
