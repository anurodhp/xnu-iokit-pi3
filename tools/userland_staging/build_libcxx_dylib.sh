#!/bin/bash
# Builds libc_build/system/libc++.dylib: static-to-dylib migration
# step 5 (docs/daemon-roadmap.md's "Static-vs-dylib architecture debt"
# section).
#
# Combines libcxx.a + libcxxabi.a into ONE real dylib, matching real
# Apple's own libc++abi.dylib+libc++.dylib pairing being tightly
# coupled (this project bundles both together rather than as two
# separate images specifically because of a real RTTI correctness risk,
# not just convenience -- see below).
#
# THIS ONE IS ALSO A REAL CORRECTNESS CONCERN, related to but distinct
# from the mach_task_self_/errno/g_zone class: C++ `typeid` equality
# (`std::type_info::operator==`) is commonly implemented by real libc++abi
# as a POINTER comparison of the two operands' `type_info` objects
# (private_typeinfo.o's `is_equal()`), not a string/name comparison in
# the common case. If libcxx.a and libcxxabi.a's RTTI data
# (`__ZTI*`/`__ZTS*`/`__ZTV*` symbols, confirmed present via nm) were
# ever split so that two DIFFERENT dylibs each held their own private
# copy of the "same" type's type_info, a `dynamic_cast`/`catch` clause
# comparing types across that dylib boundary could silently fail even
# for logically identical types. Bundling both into one image sidesteps
# the question entirely rather than needing to prove real Apple's own
# two-level-namespace symbol coalescing handles it correctly for every
# case this project might hit.
#
# libunwind.a is still linked as a static archive here (real EH/RTTI
# personality-routine dependency) -- NOT yet migrated to its own real
# dylib (that's step 6, alongside the smaller remaining archives). Same
# real libunwind_dyld_shim.o exclusion build_dyld_link.sh/
# build_libdyld.sh already established (needs __mh_execute_header, never
# synthesized for a dylib) applies here too.
#
# Real, unmodified LLVM source: same file sets build_llvm_libcxx.sh/
# build_llvm_libcxxabi.sh already established for their own static
# archives (each script's own header comment has the full scope/gap
# writeup, not re-derived here) -- reused by extracting already-built
# member objects (ar x), same approach every dylib since
# libsystem_malloc.dylib.
#
# Real, honestly-scoped exclusions (not silently worked around),
# confirmed real via nm -u tracing, not guessed:
#   - thread.o: needs real pthread_join()/pthread_detach(), neither of
#     which exists anywhere in this project yet (build_libpthread.sh's
#     own header comment already scopes real pthread.c -- 2587 lines --
#     out entirely; this file is effectively real pthread.c's own
#     <thread> surface by another name). std::thread is UNUSABLE from
#     C++ code in this project until real pthread_join/pthread_detach
#     exist -- a real, substantial follow-up (this project's own
#     pthread memory already flags bsdthread_terminate as still
#     ENOTSUP), not a permanent ceiling.
#   - condition_variable.o: needs real pthread_cond_timedwait() (only
#     the untimed pthread_cond_wait() exists so far, and only
#     pthread_cond_timedwait_relative_np() -- not the POSIX absolute-
#     deadline entry point -- exists in real, unmodified
#     third_party/libpthread/src/pthread_cond.c; a real, non-trivial
#     addition given real timeout math on top of the already-working
#     ulock-based wait, not attempted here). std::condition_variable is
#     UNUSABLE until this lands. Cascades to future.o (std::future/
#     std::promise) and shared_mutex.o (std::shared_mutex), both real
#     condition_variable consumers -- excluded for the same reason, not
#     independent gaps (confirmed via nm -u tracing: nothing else in
#     this dylib needs either). std::mutex itself does NOT need any of
#     this -- see build_libsystem_pthread.sh's own real $UNIX2003 alias for the
#     one real, small, separate gap mutex.o actually has.
#   - cxa_thread_atexit.o: needs __tlv_bootstrap, real arm64 Thread-Local
#     Variable support this project has never had (see
#     libdyld_compat_misc.c's own tlv_get_addr() stub comment -- this
#     project has zero __thread consumers so far).
#   - wcsftime.o (from libc_locale.a, added below): needs strftime_l,
#     same already-documented wide-char/locale gap
#     build_libsystem_c.sh's own header comment already excludes this
#     exact file for.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
XNU="../../third_party/xnu-7195"
OUT="libc_build"
EXTRACT="$OUT/libcxx_dylib/extracted"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
CLANGXX="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANGXX" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
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
for a in libcxx libcxxabi libunwind libc_locale libmach_ipc; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run build_$a.sh first (build_llvm_libcxx.sh/build_llvm_libcxxabi.sh/build_libunwind.sh/build_libc_locale.sh)" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/system/libsystem_kernel.dylib" ] || [ ! -f "$OUT/system/libsystem_platform.dylib" ] || \
   [ ! -f "$OUT/system/libsystem_malloc.dylib" ] || [ ! -f "$OUT/system/libsystem_pthread.dylib" ] || \
   [ ! -f "$OUT/system/libsystem_blocks.dylib" ] || [ ! -f "$OUT/system/libsystem_c.dylib" ]; then
    echo "error: libsystem_kernel/platform/malloc/pthread/blocks/c.dylib not found -- run those build scripts first" >&2
    exit 1
fi

rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"
(cd "$EXTRACT" && ar x "../../libcxx.a")
(cd "$EXTRACT" && ar x "../../libcxxabi.a")
# libc_locale.a is no longer bundled wholesale here (static-linking
# cleanup, Phase 1a, 2026-08-29). It was contributing ~244 duplicate
# exports against libsystem_c.dylib -- the entire rune/ctype/collate
# surface plus a full second copy of the string primitives.
#
# xlocale.o/ldpart.o/setlocale.o/setrunelocale.o/collate.o/lmessages.o/
# lmonetary.o/lnumeric.o/table.o/ascii.o/big5.o/euc.o/gb18030.o/gb2312.o/
# gbk.o/mskanji.o/none.o/utf2.o/utf8.o/mbsnrtowcs.o/wcsnrtombs.o (the
# WHOLE real Apple locale-loading subsystem xlocale.o cascades into) were
# ALSO extracted here until 2026-08-31 (static-linking cleanup, DAR-159)
# for a real reason, not convenience: xlocale.c's `__global_locale`/
# `__locale_key` -- the current-locale state itself -- were real Apple
# `.private_extern` (confirmed via `nm -m` reporting libsystem_c.dylib's
# own copy "non-external (was a private external)"), so libsystem_c.
# dylib could not export them and this dylib's own locale.o/strftime.o/
# collate.o/etc couldn't bind to a single shared copy across the dylib
# boundary -- each image that instantiated any part of this cascade had
# to carry the whole thing, landing this dylib 26 duplicate exports
# (checked via `comm -12` on `dyld_info -exports`, not the stale ~21+18
# split an earlier session's table recorded) against libsystem_c.dylib,
# nearly half of which (the rune-locale globals, `__CurrentRuneLocale`/
# `__DefaultRuneLocale`/`___mb_cur_max`/`___mb_sb_limit`) are real, live,
# MUTABLE current-locale state -- the same duplicate-state bug class as
# mach_task_self_/g_zone, not just duplicate code.
#
# DAR-159's real fix (2026-08-31): widened `__global_locale`/
# `__locale_key` to default visibility at the real Apple source
# (third_party/Libc/locale/xlocale_private.h, local commit + regenerated
# third_party-patches/Libc.patch -- see that commit's own message and
# this project's docs/static-linking-cleanup-plan.md DAR-159 entry for
# the full investigation) so libsystem_c.dylib -- which already carries
# a complete, real copy of this entire cascade via libc_locale_for_
# stdio.a -- can export the one real current-locale state for real.
# Confirmed via `nm -u` on every OTHER object in this dylib (locale.o,
# cxa_demangle.o, string.o -- LLVM's own real <locale>/<string> code)
# that none of them reference `___global_locale`/`___locale_key`
# directly, only the public API (`_newlocale`/`_setlocale`/`_strtoll_l`/
# etc, already ordinary external symbols, no visibility change needed)
# -- so the entire family above is now genuinely redundant here, not
# merely narrowable, and is dropped outright rather than re-extracted.
# strtoll.o/strtoul.o (below, from libcxx.a itself, not this archive)
# are the one other place this dylib independently defined part of the
# same family; dropped for the identical reason.
rm -f "$EXTRACT/__.SYMDEF"* \
    "$EXTRACT/thread.o" "$EXTRACT/mutex.o" "$EXTRACT/condition_variable.o" \
    "$EXTRACT/future.o" "$EXTRACT/shared_mutex.o" \
    "$EXTRACT/cxa_thread_atexit.o" "$EXTRACT/wcsftime.o" "$EXTRACT/debug.o" \
    "$EXTRACT/cxa_noexception.o" \
    "$EXTRACT/strtoll.o" "$EXTRACT/strtoul.o"
# strtoll.o/strtoul.o dropped here (DAR-159, 2026-08-31): real
# duplicates of libsystem_c.dylib's own build_libc_gen.sh-compiled
# _strtoll/_strtoll_l/_strtoul/_strtoul_l (confirmed via nm -m against
# the shipped libsystem_c.dylib) -- build_llvm_libcxx.sh's own header
# comment claiming these were "never attempted" by that tier was stale,
# not re-verified since libc_gen.a grew real strtoll.c/strtoul.c
# support. strtoull.o/strtoull_l is a genuine remaining gap (no other
# owner anywhere in this project) and stays -- its own undefined
# `___global_locale`/`___locale_key` refs now resolve cross-image to
# libsystem_c.dylib's newly-widened real copy (see the xlocale note
# above) instead of needing a local xlocale.o here.
# mutex.o: first pass tried keeping it (std::mutex/std::recursive_mutex
# themselves need nothing beyond what libsystem_pthread.dylib already
# has -- see the real, working $UNIX2003 alias in
# build_libsystem_pthread.sh for the one small gap that closed). Turned
# out to still need excluding: the SAME object file also defines
# std::timed_mutex/std::recursive_timed_mutex, which internally use a
# real std::condition_variable for the timed-wait path (confirmed via
# nm -u after the alias fix still showed condition_variable methods
# undefined) -- since condition_variable.o is excluded for the real
# pthread_cond_timedwait() gap above, and object-file granularity means
# the two mutex classes can't be split from each other, all of
# std::mutex/std::timed_mutex/std::recursive_mutex/
# std::recursive_timed_mutex go together. A real, disappointing, but
# honest outcome of this specific gap, not a new one. BUT mutex.o's
# __call_once() (std::call_once/std::once_flag -- needed pervasively,
# including by real locale.o's own lazy facet init) genuinely only needs
# the untimed pthread_cond_wait(), already real and working -- see
# libcxx_call_once_compat.cpp's own comment for why it's compiled
# separately below instead of being lost along with the rest of this
# file.
#
# cxa_noexception.o: real libcxxabi ships this as a mutually-exclusive
# alternate to cxa_exception.o (same real symbol set -- ___cxa_throw/
# __cxa_current_primary_exception/etc -- for a -fno-exceptions-wide
# build config this project doesn't use). Confirmed via a real
# duplicate-symbol link error when both were included -- the static
# archive never hit this because selective member scanning only ever
# pulled one of the two. cxa_exception.o (the real, exceptions-enabled
# implementation) is what stays.
#
# debug.o: real libc++ "debug mode" container/iterator checking
# (_LIBCPP_DEBUG) -- this project's build (same real CXX_FLAGS
# build_llvm_libcxx.sh already established) never defines that macro,
# so debug.o's own std::mutex-touching functions are genuinely dead
# code for this build, confirmed the same way every other exclusion in
# this script was (nothing else needs debug.o's own exports).

# Real, verbatim __call_once() (see libcxx_call_once_compat.cpp's own
# header comment) -- same real CXX_FLAGS build_llvm_libcxx.sh already
# established for every other libcxx source file.
LIBCXX="../../third_party/llvm-project/libcxx"
LIBCXXABI="../../third_party/llvm-project/libcxxabi"
"$CLANGXX" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -std=c++14 -funwind-tables \
    -nostdinc++ -isystem "$LIBCXX/include" \
    -I "$LIBCXXABI/include" -I "$LIBCXX/src" \
    -D_LIBCPP_BUILDING_LIBRARY -DLIBCXX_BUILDING_LIBCXXABI \
    -Wno-macro-redefined \
    libcxx_call_once_compat.cpp -o "$EXTRACT/call_once_compat.o"

# Real gap found booting launchd as PID 1 (docs/backlog.md item d10):
# ICU's own umutex.cpp/unifiedcache.cpp need real
# condition_variable::notify_all()/wait() -- see
# condition_variable_trimmed.cpp's own header for the full real writeup
# (real, unmodified upstream source minus __do_timed_wait/
# notify_all_at_thread_exit, which need the still-missing
# pthread_cond_timedwait()).
"$CLANGXX" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -std=c++14 -funwind-tables \
    -nostdinc++ -isystem "$LIBCXX/include" \
    -I "$LIBCXXABI/include" -I "$LIBCXX/src" \
    -D_LIBCPP_BUILDING_LIBRARY -DLIBCXX_BUILDING_LIBCXXABI \
    -Wno-macro-redefined \
    condition_variable_trimmed.cpp -o "$EXTRACT/condition_variable.o"

# Real gap: ICU's own umutex.cpp needs real std::mutex::lock()/unlock()
# too -- see mutex_trimmed.cpp's own header for the full real writeup
# (real, unmodified upstream mutex.cpp, only __call_once() omitted since
# libcxx_call_once_compat.cpp already provides it separately).
"$CLANGXX" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -std=c++14 -funwind-tables \
    -nostdinc++ -isystem "$LIBCXX/include" \
    -I "$LIBCXXABI/include" -I "$LIBCXX/src" \
    -D_LIBCPP_BUILDING_LIBRARY -DLIBCXX_BUILDING_LIBCXXABI \
    -Wno-macro-redefined \
    mutex_trimmed.cpp -o "$EXTRACT/mutex.o"

# libcxx_dylib_compat.o is NOT compiled here any more (static-linking
# cleanup, Phase 1d, 2026-08-30): every one of its 7 functions
# (catopen/catgets/catclose, clock_gettime, _dyld_find_unwind_sections/
# _dyld_register_func_for_remove_image/dladdr) is ALSO compiled, from
# the exact same source file, into libunwind.dylib's own extract dir --
# and this dylib genuinely links libunwind.dylib as a real sibling below
# (see this script's own header: "the last piece libc++.dylib was still
# linking as a static archive", now fixed). There was never a real need
# for a second copy; it just hadn't been removed when the sibling link
# was wired up. Real Apple's own architecture has libunwind provide all
# of these anyway.
#
# Real, working nanosleep(2) -- real Apple algorithm (host_get_clock_service
# + semaphore_create + clock_get_time + __semwait_signal, real Mach IPC,
# already QEMU-verified, see that file's own header comment for the full
# story), reused verbatim rather than re-deriving a second nanosleep --
# needed by real atomic.o/chrono.o (std::atomic's wait/notify timeout
# path). clock_gettime itself has no real source anywhere in this
# project's pinned checkouts to reuse (it's not a raw BSD syscall at all
# -- confirmed absent from syscalls.master -- and real Apple's own
# gen/clock_gettime.c is commpage-accelerated, more machinery than this
# narrow need justifies porting), so libcxx_dylib_compat.c's own version
# combines two already-real, already-working primitives
# (gettimeofday/mach_absolute_time) behind the standard POSIX name
# instead.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -I "$XNU/osfmk" -I ../../generated \
    simple_cmds_nanosleep_pub_compat.c -o "$EXTRACT/nanosleep_compat.o"

# libmach_ipc.a is no longer bundled here (static-linking cleanup, Phase
# 1a, 2026-08-29). nanosleep_compat.o still needs the same real Mach-IPC
# clock plumbing (host_get_clock_service/semaphore_create/clock_get_time
# and their traps), but all of it is now a real export of
# libsystem_kernel.dylib -- already a sibling on this link line -- rather
# than a private copy in each of libc++.dylib, libdispatch.dylib,
# libxpc.dylib and libncurses.dylib. Two of the symbols involved
# (_g_mach_ipc_clock_port/_sem) are real cached Mach port rights, so
# those copies were duplicate state, not just duplicate code.

# Real timezone/strftime family (real Apple/FreeBSD source, same real
# files+order build_simple_cmds.sh's own many entries already establish
# for real commands like date/ls/tar) -- real libc++'s <locale>
# std::time_put/std::time_get facets (locale.o) call strftime_l()
# directly, not just through the already-excluded wide-char wcsftime.o.
#
# localtime.o/timezone_unix03.o/strftime.o REMOVED from this loop
# 2026-08-31 (static-linking cleanup, locale/timezone-string
# consolidation batch): round 8 (continuation 6) found removing just
# localtime.o/timezone_unix03.o structurally blocked, because this
# dylib's own strftime.o (kept resident) directly calls
# __st_get_timezone/___darwin_altzone, both real Apple `.private_extern`
# symbols that cannot cross a dylib boundary -- so those two objects had
# to stay wherever strftime.o stayed. This session moves the WHOLE
# coupled trio to libsystem_c.dylib instead of leaving strftime.o behind
# (see that script's own comment for the full real-owner writeup) --
# libsystem_c.dylib is a sibling below and now exports
# _strftime$UNIX2003/_strftime_l$UNIX2003/_strftime_l/_mktime$UNIX2003
# with the exact same symbol names this dylib's own consumers (this
# file's own locale.o, calling plain _strftime_l) already expected, so
# no other relink was needed just to keep resolving.
#
# timelocal.o REMOVED 2026-08-31 (same session, DAR-159): it stayed
# resident only because xlocale.o's own ___time_load_locale reference
# needed it in the same image -- confirmed via nm that setlocale.o/
# xlocale.o were the ONLY consumers of _timelocal/___time_load_locale in
# this whole dylib. Now that the xlocale family itself is gone (see the
# extraction comment above), this goes with it.
#
# asctime.o ALSO REMOVED 2026-08-31 (same session, closing DAR-134): the
# pre-existing documented exception ("real FreeBSD source here vs
# libsystem_c.dylib's own deliberately temporary hand-rolled stand-in")
# no longer holds -- libsystem_c.dylib's own asctime.o is now the SAME
# real, unmodified FreeBSD source too (DAR-134's real blocker,
# strftime_l(), just got ported as part of this same batch, see
# build_libsystem_c.sh's own asctime.c comment). Confirmed via `nm -u`
# asctime.c needs neither localtime.o/timezone_unix03.o (it hardcodes
# its own day/month name tables) nor timelocal.o, so removing it doesn't
# disturb anything else in this loop.
# LOCALE_CFLAGS/the timelocal.o compile loop that used to live here were
# removed 2026-08-31 along with the rest of the xlocale family (DAR-159)
# -- see the extraction comment near the top of this file.

mkdir -p "$OUT/system"
rm -f "$OUT/system/libc++.dylib"
# ___progname: same real cross-image lookup build_libsystem_c.sh's own
# comment already documents (real per-process global living in the main
# executable's own crt.c). _fputs$UNIX2003: real UNIX2003 alias this
# dylib's own locale.o/ios_base.o etc need -- ld64 resolves the alias's
# base (_fputs) externally to libsystem_c.dylib (a sibling below) and
# correctly emits a real `[re-export]` entry, confirmed via `dyld_info
# -exports` (not just `nm`, per this project's own ld64 `-alias`
# nonlocal-base landmine lesson).
#
# The previous `-Wl,-alias,'_strftime_l$UNIX2003',_strftime_l` entry was
# REMOVED 2026-08-31 (static-linking cleanup, locale/timezone-string
# consolidation batch): it existed only because this dylib used to
# compile its own strftime.o (see the LOCALE_CFLAGS loop above, now
# trimmed to just asctime/timelocal). Now that libsystem_c.dylib is the
# sole owner and itself exports both `_strftime_l` and
# `_strftime_l$UNIX2003` directly (see that script's own alias list),
# this dylib's locale.o (which calls plain `_strftime_l`, confirmed via
# `nm -u`) resolves straight to that sibling -- no alias needed here at
# all any more.
    # -not_for_dyld_shared_cache: real, newly-surfaced requirement of
    # the daily-driver Xcode's newer ld (NEWLD_BINDIR above) -- it
    # refuses "-undefined dynamic_lookup"/-U on a dylib it classifies
    # as shared-cache eligible (real /usr/lib install path) unless this
    # is set, a real check Xcode 12's ld64-609 never enforced. This
    # project builds no real dyld shared cache at all, so opting every
    # real dylib out is correct, not a workaround (docs/backlog.md item
    # d9's NEWLD_BINDIR sweep, 2026-08-28).
"$CLANGXX" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libc++.1.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-U,___progname \
    -Wl,-alias,_fputs,'_fputs$UNIX2003' \
    -o "$OUT/system/libc++.dylib" \
    "$EXTRACT"/*.o \
    "$OUT/system/libunwind.dylib" \
    "$OUT/system/libsystem_kernel.dylib" "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_blocks.dylib" "$OUT/system/libsystem_c.dylib"

echo "wrote $OUT/system/libc++.dylib"
otool -hv "$OUT/system/libc++.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
if nm -u "$OUT/system/libc++.dylib" | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u "$OUT/system/libc++.dylib" >&2
else
    echo "libc++.dylib: fully self-contained (beyond libsystem_*.dylib), zero undefined symbols"
fi
