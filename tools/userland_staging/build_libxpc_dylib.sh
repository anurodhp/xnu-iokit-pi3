#!/bin/bash
# Builds libc_build/system/libxpc.dylib: static-to-dylib migration,
# docs/backlog.md item d8 -- LAST of this migration pass's Stage A
# units. Bundles libxpc.a + libxpc_syscalls.a + libxpc_syscalls2.a as
# ONE real dylib (real Apple ships a single libxpc.dylib too, and no
# other consumer in this project links any of the three independently --
# confirmed via `grep libxpc_syscalls tools/userland_staging/build_*.sh`,
# every hit also links libxpc.a in the same command).
#
# Real install_name: /usr/lib/system/libxpc.dylib (real Apple ships XPC
# as a libsystem_*-family umbrella member, same class as libdispatch).
#
# Real, unmodified source: reuses every already-built member object out
# of the three archives (ar x, not recompiled -- see build_libxpc.sh/
# build_libxpc_syscalls.sh/build_libxpc_syscalls2.sh's own headers for
# the full scope writeups). The real cross-symbol gap closures below are
# NOT reinvented here -- they're the exact same real fix set
# build_launchd.sh's own static link already proved correct (read that
# script's own comments around its `libmach_ipc_no_dead_stubs.a`/
# `mach_port_gap_closures2.o` construction before touching this file;
# this script reproduces that construction independently rather than
# depending on build_launchd.sh having been run first, so this script
# stays reproducible standalone):
#   - liblaunchd_mig.a: real, already-regenerated MIG client stubs for
#     the whole `vproc_mig_*` family (job control IPC to launchd, ~33
#     real symbols) -- a per-daemon curated MIG bundle, stays `.a` by
#     design (docs/backlog.md item d8's own "not real conversion
#     candidates" list), linked here as-is, same as build_launchd.sh
#     already does.
#   - mach_port_gap_closures2.o/.s: real, narrowed (`ld -r
#     -exported_symbol`) replacements for mach_port_insert_right/
#     extract_right/unguard/kernel_object/thread_destruct_special_reply_port
#     -- see mach_port_gap_closures2.c's own header for the full
#     citation (verbatim libsyscall/mach/mach_port.c bodies). Compiled
#     fresh here from the same real source build_launchd.sh already
#     uses, not copied as a build artifact.
#   - libmach_ipc.a with mach_ipc_dead_stubs.o/mach_msg_real.o stripped
#     (`ar d`, real gap already found+fixed once during launchd's own
#     session-2 debugging -- see this script's mach_port_gap_closures2.o
#     comment above and docs/backlog.md/memory for the full trail): the
#     dead-stub object's own honest `voucher_mach_msg_set` (`return
#     FALSE`, "no voucher subsystem", weak_import per every real MIG
#     client stub -- same scope decision as INET/INET6) is re-narrowed
#     back in separately so IT'S the one that wins, not
#     libdispatch.dylib's own considerably more complex real
#     implementation (confirmed wrong for this project's boot-time
#     process state during launchd's own debugging).
#
# Real sibling dylibs beyond the usual 5: libdispatch.dylib (real Apple
# XPC is built on GCD -- dispatch_async/_sync/_once/_queue_create/
# _semaphore_*/_source_* etc, ~20 symbols, all real, direct exports) and
# libsystem_blocks.dylib (Block_copy/_release/_object_assign/_dispose,
# NSConcreteGlobalBlock/StackBlock -- real Blocks runtime, same sibling
# dependency class).
#
# Real, honestly-classified `-Wl,-U` deferrals (found via the same real
# nm-based intra-archive resolution pass build_libresolv_dylib.sh's
# header documents):
#   - csops: CORRECTED 2026-08-29 -- was wrongly deferred via `-Wl,-U`
#     on the assumption an unreached symbol is harmless; it is not (real
#     dyld flat-namespace lookup fails at real process load time
#     regardless of whether the call path executes, confirmed via a real
#     QEMU boot failure). Real, narrow raw-syscall wrapper provided
#     instead (libxpc_csops_syscall.c), not deferred anymore.
#   - mach_errors, environ: real, confirmed genuinely absent from every
#     archive in this checkout (`nm -g` swept across every .a in
#     libc_build/) -- project-wide gaps, not xpc-specific. (dup/geteuid/
#     getuid ARE closable via already-built sys__dup.o/geteuid.o/getuid.o,
#     syslogd_libc_extras.a -- same precedent build_bsm_dylib.sh already
#     established -- linked directly instead of deferred.)
#   - kevent, kqueue: same already-documented res_send.c-class gap
#     build_libresolv_dylib.sh's header already covers -- XPC's own real
#     kqueue-based event source support (dispatch_source_create's
#     mach-recv path) isn't exercised without these either way.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
EXTRACT="$OUT/libxpc_dylib/extracted"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
for a in libxpc libxpc_syscalls libxpc_syscalls2 liblaunchd_mig libmach_ipc syslogd_libc_extras; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run build_${a}.sh first" >&2
        exit 1
    fi
done
for d in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_pthread libdispatch libsystem_blocks; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/mach_ipc_dead_stubs.o" ]; then
    echo "error: $OUT/mach_ipc_dead_stubs.o not found -- run build_libmach_ipc.sh first" >&2
    exit 1
fi

# Real, verified fix (docs/backlog.md item d9): Xcode 12's own bundled
# ld64-609 has a real internal crash under -Wl,-fixup_chains that a much
# newer ld does not. Compiling still uses Xcode 12's clang/SDK above;
# only the final link step's linker binary changes, via clang's -B
# override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

(cd "$EXTRACT" && ar x "../../libxpc.a")
(cd "$EXTRACT" && ar x "../../libxpc_syscalls.a")
(cd "$EXTRACT" && ar x "../../libxpc_syscalls2.a")
(cd "$EXTRACT" && ar x "../../syslogd_libc_extras.a" sys__dup.o)

# 5 members REMOVED 2026-08-31 (static-linking cleanup, dylib-vs-dylib
# duplicate-symbol pass): libxpc_syscalls.a's own private copies of
# `___thread_selfid` (dyldgen____thread_selfid.o), `_sched_yield`/
# `_swtch_pri` (simple_sched_yield.o/simple_sched_yield_trap.o), and
# `_kill`/`___kill` (kill_wrap.o/sys____kill.o) all duplicate real
# exports of siblings this dylib already links: `___thread_selfid`/
# `_sched_yield`/`_swtch_pri` are real Apple's own libsystem_pthread.
# dylib API (see build_libsystem_pthread.sh's own header -- this
# project moved them there specifically as "real Apple exports every
# one of these from libsystem_pthread.dylib -- this is where they
# belong"), and `___kill` is libsystem_malloc.dylib's real canonical
# copy (build_libsystem_malloc.sh's own `-Wl,-alias` base). Confirmed
# via `nm -m` across every other object in this extract dir that the
# ONLY real consumer is `libproc.o`'s one undefined reference to
# `___thread_selfid` -- it now resolves externally to the real
# libsystem_pthread.dylib sibling below instead. Plain `_kill` (from
# kill_wrap.o) has no other consumer at all in this dylib; removing it
# also drops this dylib's contribution to a separate, still-open 4-way
# `_kill` duplicate (libdyld.dylib/libncurses.dylib/libsystem_c.dylib).
rm -f "$EXTRACT/dyldgen____thread_selfid.o" \
      "$EXTRACT/simple_sched_yield.o" "$EXTRACT/simple_sched_yield_trap.o" \
      "$EXTRACT/kill_wrap.o" "$EXTRACT/sys____kill.o"

# 4 more members REMOVED same pass: libxpc_syscalls2.a's own private
# `_chk`-tier copies (memmove_chk.o/stpncpy_chk.o/strcpy_chk.o/
# strncpy_chk.o) duplicate libsystem_c.dylib's real `_FORTIFY_SOURCE`
# `_chk` tier (established as libsystem_c.dylib's canonical home in the
# 2026-08-30 session) -- this dylib already links libsystem_c.dylib as
# a sibling. Confirmed via `nm -m` that the only in-dylib consumers
# (liblaunch.o/libproc.o/libvproc.o) merely reference these as
# undefined, so they now resolve externally instead.
rm -f "$EXTRACT/memmove_chk.o" "$EXTRACT/stpncpy_chk.o" \
      "$EXTRACT/strcpy_chk.o" "$EXTRACT/strncpy_chk.o"

# darwin_check_fd_set_overflow_compat.o REMOVED 2026-08-31 (static-
# linking cleanup, "needs a new home" batch): a leftover from the same
# libxpc_syscalls2.a _chk-family removal just above that got missed at
# the time -- real Apple's `__darwin_check_fd_set_overflow()` is the
# same `_FORTIFY_SOURCE` hardening family as memmove_chk/strcpy_chk/
# strncpy_chk, and now has the same real canonical home,
# libsystem_c.dylib (build_libsystem_c.sh's own new extraction of this
# exact object, same batch) -- this dylib already links libsystem_c.
# dylib as a sibling below. Confirmed via nm -m the only in-dylib
# consumer (liblaunch.o's own FD_SET/FD_ISSET use) merely references it
# as undefined, so it now resolves externally instead.
rm -f "$EXTRACT/darwin_check_fd_set_overflow_compat.o"

# time.o REMOVED: its own `_time` duplicates libsystem_c.dylib's real
# BSD `time()` syscall wrapper (already a linked sibling below);
# confirmed via nm -m no other object in this extract dir references
# `_time` locally.
rm -f "$EXTRACT/time.o"

# uuid_unparse.o REMOVED 2026-08-31 (static-linking cleanup,
# libcopyfile-vs-libxpc ownership investigation): this dylib's own
# freshly-compiled copy of third_party/Libc/uuid/uuidsrc/unparse.c
# (uuid_unparse/uuid_unparse_lower/uuid_unparse_upper) duplicated
# libcopyfile.dylib's own extracted copy of the SAME real Apple source --
# genuinely ambiguous ownership (neither dylib is uuid's real Apple
# home), resolved by making libsystem_c.dylib the single canonical
# owner (see build_libsystem_c.sh's own comment: real Apple ships this
# as part of Libc). Confirmed via nm -u that this extract dir's real
# consumers (nvlist.o, xpc_misc.o) only ever reference `_uuid_unparse*`
# as undefined -- they now resolve externally to the libsystem_c.dylib
# sibling this dylib already links. Not from libxpc_syscalls2.a itself
# (that archive stays intact for its own static consumers, e.g.
# build_libnotify_client.sh's smoketest).
rm -f "$EXTRACT/uuid_unparse.o"

# sys__fileport_makefd.o/sys__fileport_makeport.o REMOVED 2026-08-31:
# `_fileport_makefd`/`_fileport_makeport` are real BSD syscalls
# (#430/#431) whose real home is libsystem_kernel.dylib, which now
# generates them for real (see build_libsystem_kernel.sh's own
# comment, same create-syscalls.pl recipe as geteuid/getegid/etc) --
# this dylib already links libsystem_kernel.dylib as a sibling below.
# `libvproc.o`/`xpc_type.o`'s real, undefined references (confirmed via
# nm -m) now resolve there instead of this dylib's own private trap.
rm -f "$EXTRACT/sys__fileport_makefd.o" "$EXTRACT/sys__fileport_makeport.o"

# select_wrap.o/xpc_syscall_wrappers_compat.o/sys__socket.o REMOVED
# 2026-08-31 (static-linking cleanup, dylib-vs-dylib duplicate-symbol
# pass, BSD socket family): `_connect`/`_recvmsg`/`_select`/`_sendmsg`/
# `_socket` now all have a real, canonical home in
# libsystem_kernel.dylib -- build_libsystem_kernel.sh directly reuses
# THIS project's own xpc_select_compat.c/xpc_syscall_wrappers_compat.c
# (the exact same real, already-proven, already-QEMU-verified curated
# source build_libxpc_syscalls.sh's own header comment documents in
# full) verbatim, so this is a real move to the correct owner, not a
# behavior change. This dylib already links libsystem_kernel.dylib as a
# sibling below. `liblaunch.o`'s real, undefined references to all five
# (confirmed via nm -m -- the only consumer in this dylib's own archive
# set) now resolve there instead.
rm -f "$EXTRACT/select_wrap.o" "$EXTRACT/xpc_syscall_wrappers_compat.o" "$EXTRACT/sys__socket.o"

# sys____connect_nocancel.o/sys____recvmsg_nocancel.o/
# sys____select_nocancel.o/sys____sendmsg_nocancel.o REMOVED too: these
# are the raw `_nocancel` trap objects the just-removed
# xpc_syscall_wrappers_compat.o/select_wrap.o used to call -- with those
# wrappers gone, nothing in this dylib's own archive set references
# them locally any more (confirmed via nm -m), and libsystem_kernel.
# dylib's own build now generates the exact same raw traps (same
# create-syscalls.pl recipe, confirmed identical filenames) for its own
# real xpc_syscall_wrappers_compat.c/xpc_select_compat.c compiles.
# Leaving these as pure dead weight here would just be a second,
# unused, fully-external copy of the exact same duplicate-symbol shape
# this whole pass exists to close.
rm -f "$EXTRACT/sys____connect_nocancel.o" "$EXTRACT/sys____recvmsg_nocancel.o" \
      "$EXTRACT/sys____select_nocancel.o" "$EXTRACT/sys____sendmsg_nocancel.o"

# Members dropped 2026-08-29 (static-linking cleanup, Phase 1a). Every
# one of these was a second copy of something a sibling dylib already on
# this link line now owns, so every process loading libxpc.dylib next to
# libdispatch.dylib or libSystem.B.dylib had two definitions of each:
#
#   -> libsystem_kernel.dylib: the Mach semaphore traps and their
#      wrappers, mach_error_string, and the mach_port_allocate family.
#      geteuid.o/getuid.o go the same way (raw BSD syscall stubs).
#   -> libsystem_c.dylib: the environment-variable subsystem
#      (setenv/environ_lock/findenv/getenv) -- this one mattered most,
#      because two setenv() implementations were mutating one real
#      `environ` array under two separate locks -- the _FORTIFY_SOURCE
#      _chk family, and os_assumes.
#
# chk_fail_compat.o is deliberately NOT dropped either, for a different
# and better reason: every symbol it defines (___chk_fail_overflow,
# ___chk_overlap, ___chk_assert_no_overlap, ___chk_fail_overlap) is
# `.private external` in real Apple's own source -- confirmed via
# `nm -m`, not assumed -- so it is image-local by design and invisible
# across a dylib boundary. There is no symbol here for another image to
# bind to the wrong copy of, and the remaining _chk members this dylib
# still carries (memmove_chk.o, stpcpy_chk.o, strcpy_chk.o, ...) each
# need one in their own image. Same situation as
# ___gettimeofday_with_mach in build_libdispatch_dylib.sh.
#
# mach_portUser.o IS now dropped too (static-linking cleanup, Phase 1d,
# 2026-08-30). It used to stay because libsystem_kernel.dylib deferred
# the whole MIG-generated __kernelrpc_mach_port_* set via -Wl,-U, making
# libxpc.dylib their de-facto provider. That deferral is gone:
# build_libsystem_kernel.sh now runs `mig` on xnu-7195's own
# osfmk/mach/mach_port.defs itself and owns all 51 stubs outright, which
# is where real Apple puts them. Keeping this copy would leave
# libxpc.dylib a second owner of 40 symbols libiokituser.dylib was
# ALSO duplicating -- the largest single remaining group in the audit.
(cd "$EXTRACT" && rm -f \
    semaphore_wrap.o mach_semaphore_traps.o mach_error_string.o \
    mach_port_allocate_compat.o mach_port_allocate_trap.o \
    mach_portUser.o \
    setenv.o environ_lock_compat.o findenv_locked_compat.o getenv_compat.o \
    snprintf_chk.o vsnprintf_chk.o strlcpy_chk.o \
    os_assumes_compat.o)

# mach_port_gap_closures2.o/.s no longer built here (static-linking
# cleanup, Phase 1a, 2026-08-29): _mach_port_insert_right/_extract_right/
# _unguard/_kernel_object and _thread_destruct_special_reply_port are now
# real exports of libsystem_kernel.dylib, a sibling on this link line.
# libdispatch.dylib was compiling its own third copy of the same file.

# libmach_ipc.a is no longer bundled here (static-linking cleanup, Phase
# 1a, 2026-08-29): its whole content -- the MIG client stubs for
# clock.defs/mach_host.defs/task.defs, mig_strncpy, mach_host_self and
# the g_mach_ipc_clock_port/_sem cache -- now lives in
# libsystem_kernel.dylib. The libmach_ipc_no_dead_stubs_for_xpc.a copy
# and the _voucher_mach_msg_set narrowing that used to be needed here go
# with it; _voucher_mach_msg_set is deferred at the libsystem_kernel end
# now (real body: libdispatch.dylib, same as real Apple).

# REAL BUG FOUND+FIXED 2026-08-29: csops was deferred via `-Wl,-U` below
# on the (wrong) assumption that leaving it undefined was harmless if
# unreached. It is not -- real dyld's flat-namespace lookup at real
# process load time hard-fails the instant ANY code in the process tries
# to bind it, regardless of whether that specific call path ever
# executes, and this project has nothing anywhere exporting csops
# (confirmed via nm -g across every .a). Found via a real QEMU boot of
# launchd (the first real consumer to load libxpc.dylib): "symbol not
# found in flat namespace '_csops'", pid 1 exited before main() ran at
# all. Real, narrow syscall wrapper instead (see libxpc_csops_syscall.c
# for the full citation) -- same real fix class as
# build_copyfile_dylib.sh's chmod$UNIX2003 family from earlier this pass
# (a symbol wrongly deferred instead of actually provided).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libxpc_csops_syscall.c -o "$EXTRACT/libxpc_csops_syscall.o"

# REAL BUG FOUND+FIXED 2026-08-29, same real class as csops above:
# __mach_errors (a data table mach_error_string.o -- already extracted
# from libxpc_syscalls.a above -- references) and kevent were ALSO
# wrongly deferred via `-Wl,-U`. Real fixes, see each source's own
# header: mach_error_string_minimal_compat.c (a real, honestly-empty
# `_mach_errors` table -- the real generated per-subsystem data doesn't
# exist in this checkout, but the struct layout and fallback behavior
# are real, not fabricated) and libc_build/simple_obj/sys__kevent.o (a
# real, already-generated create-syscalls.pl raw syscall stub, same one
# kqueue_smoketest.c already uses -- reused, not recompiled).
if [ ! -f "$OUT/simple_obj/sys__kevent.o" ]; then
    echo "error: $OUT/simple_obj/sys__kevent.o not found -- run build_simple_cmds.sh first" >&2
    exit 1
fi
# mach_error_string_minimal_compat.o moved to libsystem_kernel.dylib
# alongside mach_error_string.o itself (static-linking cleanup, Phase 1a).
cp "$OUT/simple_obj/sys__kevent.o" "$EXTRACT/sys__kevent.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libxpc.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libxpc.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-U,_environ -Wl,-U,_kqueue \
    -o "$OUT/system/libxpc.dylib" \
    "$EXTRACT"/*.o \
    -Wl,-force_load,"$OUT/liblaunchd_mig.a" \
    "$OUT/system/libdispatch.dylib" "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    2>"$OUT/libxpc_dylib_link.err" \
    && echo "wrote $OUT/system/libxpc.dylib" \
    || { echo "libxpc.dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libxpc_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }

otool -hv "$OUT/system/libxpc.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
echo "declared -Wl,-U deferrals (expect: <flat-namespace> bind target):"
xcrun dyld_info -fixups "$OUT/system/libxpc.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print "  " $NF}'
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libxpc.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/|libdispatch/|libsystem_blocks/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found:" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libxpc.dylib: every non-deferred external reference binds to a real, named sibling dylib -- no unexpected/dangling references"
