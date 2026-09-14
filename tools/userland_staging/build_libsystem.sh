#!/bin/bash
# Builds libc_build/system/libSystem.B.dylib: the second (of two) real,
# unconditional, hardcoded install_name dyldMain.cpp requires every
# launched process to link against (RuntimeState::add() in
# DyldRuntimeState.cpp matches "/usr/lib/libSystem.B.dylib" exactly).
# Phase 6.5 Phase D (docs/daemon-roadmap.md), companion to
# build_libdyld.sh.
#
# Real, deliberately simplified architecture (see build_libdyld.sh's own
# header comment for the full reasoning): real macOS's libSystem.B.dylib
# is itself a thin re-export umbrella over a half-dozen separate real
# libsystem_*.dylib/libdispatch.dylib sub-images. This project has never
# built libSystem as separate sub-dylibs -- every build_*.sh here
# produces ONE flat static archive per curated library, the same real
# objects every static binary in this project already links directly.
# This script bundles that exact same already-built, already-tested
# curated stack into ONE real dylib instead, reusing the identical
# archive list + scratch symbol-collision fixes build_libdyld.sh already
# had to solve for this same combination (libcxxabi's real
# cxa_exception.o pulling libunwind's real C++ unwinder wanting
# libunwind_dyld_shim.o's __mh_execute_header; libmalloc_dispatch.o's
# stale __platform_memset/_dyld_get_image_slide hollow stand-ins) -- not
# re-derived, reused verbatim.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
mkdir -p "$OUT/libsystem"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
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
# Real static-vs-dylib architecture fix (docs/backlog.md item d8, Stage
# B of the 2026-08-28 static-linking cleanup pass): libSystem.B.dylib
# used to statically bundle the WHOLE of libcopyfile.a/libremovefile.a/
# libresolv.a, each with its own private copy of any process-global
# state (real, mutable `_res`/`_res_9` resolver state in particular --
# same duplicate-global-state bug class already fixed once for
# mach_task_self_/g_zone). Real sibling dylibs now exist for all three
# -- reexported below (same `-Wl,-reexport_library` treatment every
# other sub-dylib here already gets) instead of bundled, so this
# umbrella's export surface stays identical for existing consumers while
# the underlying symbols now come from one real, shared image instead of
# a private static copy.
for d in libcopyfile libremovefile libresolv; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}_dylib.sh first" >&2
        exit 1
    fi
done
# Real gap, found 2026-09-13: same mismatch already documented in
# build_libdyld.sh's own header -- libcopyfile/libremovefile derive to
# nonexistent "build_libcopyfile.sh"/"build_libremovefile.sh" (real
# names drop the "lib" prefix: build_copyfile_dylib.sh/
# build_removefile_dylib.sh), and libresolv derives to the WRONG
# existing script (build_libresolv.sh builds the unrelated static
# libresolv.a, not build_libresolv_dylib.sh). Named individually here.
if [ ! -f "$OUT/system/libcopyfile.dylib" ]; then
    echo "error: $OUT/system/libcopyfile.dylib not found -- run build_copyfile_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libremovefile.dylib" ]; then
    echo "error: $OUT/system/libremovefile.dylib not found -- run build_removefile_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libresolv.dylib" ]; then
    echo "error: $OUT/system/libresolv.dylib not found -- run build_libresolv_dylib.sh first" >&2
    exit 1
fi
# libsystem_m.dylib reexport ATTEMPTED and REVERTED 2026-08-31, RETRIED
# AND CLOSED 2026-09-04 (static-linking cleanup, DAR-160/DAR-16).
# docs/historic/backlog.md item d12 proposed reexporting libsystem_m.dylib
# here (same treatment as libcopyfile/libremovefile/libresolv above) so
# build_libsystem_c.sh could drop its own curated acos/asin/fabs/fmod/
# modf/pow/scalbn/sqrt/trunc extraction. Confirmed no circular BUILD
# dependency (libsystem_m links libsystem_c, not the reverse; libsystem_m
# does not link libSystem.B.dylib back).
#
# The 2026-08-31 attempt found a real, severe blocker: a real QEMU boot
# test of the built result found reexporting libsystem_m.dylib here makes
# it a REQUIRED LOAD DEPENDENCY of every single process on this system
# (every launched process must link libSystem.B.dylib per dyldMain.cpp's
# own hardcoded check). This project's dyld only does eager chained-
# fixups binding, no lazy PLT stubs -- so EVERY bind in libsystem_m.dylib's
# own fixups must resolve at load time, including its own pre-existing,
# real, `-Wl,-U`-deferred gaps (see build_libm_dylib.sh's own OLD header:
# `___muldc3`/`__mulsc3`, real compiler-rt complex-multiply intrinsics
# this project had never ported, plus 13 unported float-precision Libm
# entry points). Real result: PID 1 itself panicked at boot -- `pid 1
# exited -- exit reason ... symbol not found in flat namespace
# '___muldc3'` -- because launchd, like every process, now transitively
# loaded libsystem_m.dylib via this reexport, and dyld eagerly tried and
# failed to resolve that flat-namespace deferral with nothing in the
# process actually providing it (see docs/static-linking-cleanup-plan.md's
# DAR-160 writeup). REVERTED at the time.
#
# That prerequisite is now genuinely gone -- closed for a different,
# independent reason (DAR-124's own dup-symbol sweep on libsystem_m.dylib
# itself, 2026-08-31/2026-09-01): `complex.o`/`scalblnf.o`, the ONLY two
# objects needing any of those 15 deferred symbols, were confirmed to
# have zero real callers anywhere in this project (a real grep sweep) and
# dropped from build_libm_dylib.sh's own link -- that dylib now asserts
# zero flat-namespace deferrals at its own build time. Reverified fresh
# before retrying this reexport (2026-09-04): `xcrun dyld_info -fixups
# libc_build/system/libsystem_m.dylib | grep -c flat-namespace` -> 0.
# With that confirmed, the reexport below no longer panics -- real QEMU
# boot test PASS (see DAR-16's own commit message for the log path).
# build_libsystem_c.sh's own curated libm.a extraction is dropped as of
# the same change; its 2 internal callers (atan() calling asin()/sqrt())
# get a `-Wl,-U` deferral instead, safe because libsystem_m.dylib is now
# unconditionally co-loaded via this same umbrella.
if [ ! -f "$OUT/system/libsystem_m.dylib" ]; then
    echo "error: $OUT/system/libsystem_m.dylib not found -- run build_libm_dylib.sh first" >&2
    exit 1
fi
# libc_gen.a/libc_string.a/libc_stdio.a/libc_printf.a/
# libcxx_locale_for_stdio_scratch.a/libcxx_tsd_shim_bootstrap_compat.o no
# longer required here (static-to-dylib migration step 4, docs/
# daemon-roadmap.md) -- real libsystem_c.dylib below instead. Same for
# libcxx.a/libcxxabi.a/libunwind.a (step 5) -- real libc++.dylib below.
if [ ! -f "$OUT/system/libsystem_c.dylib" ] || [ ! -f "$OUT/system/libc++.dylib" ]; then
    echo "error: $OUT/system/libsystem_c.dylib / libc++.dylib not found -- run build_libsystem_c.sh and build_libcxx_dylib.sh first" >&2
    exit 1
fi
# libdyld.dylib reexport added 2026-09-14: this used to live on
# libsystem_c.dylib's own link (build_libsystem_c.sh), which made that
# script and build_libdyld.sh a real cycle (libdyld.sh genuinely needs
# libsystem_c.dylib to exist first for real symbol needs of its own,
# and reexporting libdyld.dylib back from libsystem_c.dylib meant
# neither could go first). This umbrella already runs after both real
# dylibs exist, so it's the correct place for this reexport -- matches
# real Apple architecture too (libSystem.B.dylib is the umbrella that
# reexports dyld's dl*/_NSGetExecutablePath-class API, not libsystem_c
# doing it prematurely). See build_libsystem_c.sh's own comment on the
# removed reexport for the full history (docs/backlog.md item d10 is
# where the requirement itself -- ICU's libicuuc.66.dylib expecting
# _dlclose "from libSystem" -- was first found).
if [ ! -f "$OUT/system/libdyld.dylib" ]; then
    echo "error: $OUT/system/libdyld.dylib not found -- run build_libdyld.sh first" >&2
    exit 1
fi
# libunwind.dylib reexport added 2026-08-29 (docs/daemon-roadmap.md Phase
# 6.8 continued, item 1 real root-cause): a real launchd-as-PID-1 boot
# panicked with dyld's own real exit reason "Symbol not found:
# __Unwind_Resume, Referenced from: /libicuuc.66.dylib, Expected in:
# /usr/lib/libSystem.B.dylib". ICU's own upstream build links against a
# real Apple SDK and its bind info expects __Unwind_Resume to come
# directly from libSystem.B.dylib (confirmed real on macOS: libSystem.B
# reexports libunwind directly, not only transitively through libc++),
# not from libc++.dylib the way this project's own C++ code reaches it.
# libc++.dylib itself already depends on libunwind.dylib as a peer
# import (confirmed via `nm -m libc++.dylib`: "__Unwind_Resume
# (from libunwind)"), not a reexport, so reexporting libc++.dylib alone
# never surfaced __Unwind_Resume through libSystem.B for a third-party
# consumer like ICU. Real fix: reexport libunwind.dylib directly here too,
# same pattern as every other sub-dylib on this link line.
if [ ! -f "$OUT/system/libunwind.dylib" ]; then
    echo "error: $OUT/system/libunwind.dylib not found -- run build_libunwind_dylib.sh first" >&2
    exit 1
fi
# libplatform.a/libclosure.a/cerror_stub.o no longer required here
# (static-to-dylib migration batch 1, docs/daemon-roadmap.md) -- real
# libsystem_kernel.dylib/libsystem_platform.dylib/libsystem_blocks.dylib
# below instead, same real source.
if [ ! -f "$OUT/system/libsystem_platform.dylib" ] || [ ! -f "$OUT/system/libsystem_blocks.dylib" ]; then
    echo "error: $OUT/system/libsystem_platform.dylib / libsystem_blocks.dylib not found -- run build_libsystem_platform.sh and build_libsystem_blocks.sh first" >&2
    exit 1
fi
RTLIB="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/12.0.0/lib/darwin/libclang_rt.ios.a"
if [ ! -f "$RTLIB" ]; then
    echo "error: $RTLIB not found (real LLVM compiler-rt, provides ___chkstk_darwin)" >&2
    exit 1
fi

# compat_misc.o (malloc_size/tlv_get_addr stubs) REMOVED from this
# dylib's own link 2026-08-31 (static-linking cleanup, dylib-vs-dylib
# duplicate-symbol pass): confirmed via `nm -u` that neither of this
# dylib's own project-authored objects (dyld_initializer_compat.o is the
# only one) references either symbol -- only libdyld.dylib's own
# LibSystemHelpers.o (real Apple dyld source) does, and that object is
# never compiled into this dylib at all. The copy here was pure,
# unreferenced duplication of libdyld.dylib's real, structurally-needed
# one (LibSystemHelpers::malloc_size()/getTLVGetAddrFunc() -- see
# libdyld_compat_misc.c's own per-function comments), not a second
# consumer that legitimately needed its own. Real single owner:
# libdyld.dylib.

# dyld_unwind_shim_compat.c REMOVED 2026-08-30 (static-linking cleanup,
# dylib-vs-dylib duplicate-symbol pass). This comment used to (correctly,
# at the time) note the copy was "redundant rather than load-bearing"
# once libc++.dylib gained its own real `_dyld_find_unwind_sections`/
# `dladdr` stubs -- but left it in place as low-risk. Real state since
# then (Session 2026-08-30, see docs/static-linking-cleanup-plan.md):
# libc++.dylib's own copy was ALSO removed once it started genuinely
# linking libunwind.dylib as a real sibling -- libunwind.dylib is real
# Apple's actual home for these 3 symbols (build_libunwind_dylib.sh's
# own header comment). This dylib already `-Wl,-reexport_library`s
# libunwind.dylib below (and libc++.dylib, which itself depends on
# libunwind.dylib) -- confirmed via nm -m that this SCRATCH object was
# the ONLY remaining local definition of all 3 symbols in this dylib at
# the time (compat_misc.o, the other object on this link line back then,
# defined neither -- and was itself removed from this link entirely on
# 2026-08-31, see the comment above). Removing it let the real reexport
# chain actually supply them instead of a second, independently-compiled
# stub copy at a different address.

# libmalloc.a/libpthread.a and the libmalloc_dispatch.o trim-and-rebuild
# dance that used to live here are gone as of static-to-dylib migration
# step 3 (docs/daemon-roadmap.md) -- real libsystem_malloc.dylib/
# libsystem_pthread.dylib below instead. See build_libdyld.sh's own
# comment for why the trim dance isn't needed at all for real dylibs
# (only ever fixed a static-archive-flattening collision).
if [ ! -f "$OUT/system/libsystem_kernel.dylib" ] || [ ! -f "$OUT/system/libsystem_malloc.dylib" ] || [ ! -f "$OUT/system/libsystem_pthread.dylib" ]; then
    echo "error: libsystem_kernel.dylib / libsystem_malloc.dylib / libsystem_pthread.dylib not found -- run build_libsystem_kernel.sh, build_libsystem_malloc.sh, and build_libsystem_pthread.sh first" >&2
    exit 1
fi

# ___progname REMOVED 2026-08-31 (static-linking cleanup, real
# duplicate-STATE investigation): libsystem_progname_compat.c used to
# compile a second, independent `const char* __progname = 0;` into this
# dylib. Investigated with dup_symbol_audit.sh --explain plus real
# third_party/dyld/dyld/dyldMain.cpp source (unmodified Apple code): real
# modern dyld's canonical mechanism is libdyld.dylib's own `defaultVars`
# (LibdyldDyld4Section, populated from libdyldGlue.cpp's real, genuine
# `const char* __progname = NULL;` + its `state.vars->__prognamePtr`
# wiring) -- dyldMain.cpp unconditionally does
# `*state.vars->__prognamePtr = state.config.process.progname;` for
# every real launch, and separately `halt("libdyld.dylib not found")`
# if libdyld.dylib isn't loaded, i.e. libdyld.dylib's copy is BOTH the
# real-Apple-canonical one AND structurally guaranteed to always be
# co-loaded with this dylib (build_simple_cmds.sh's own LINK_DYLIBS
# already links both together, always). Confirmed via a real grep sweep
# that neither of this dylib's own compiled objects
# (dyld_initializer_compat.o/compat_misc.o) ever read or wrote
# ___progname -- the local copy here was pure dead storage, never
# written by anything, shadowing nothing observable only because
# libsystem_c.dylib's own getprogname()/setprogname() (see that file's
# own header comment) resolve ___progname via a flat/dynamic `-Wl,-U`
# lookup that happens to find the main executable's own crt.c copy
# first, not this dylib's. Real single-owner fix: delete the compat
# shim and this compile step entirely; nothing in this dylib references
# ___progname any more, so no replacement flag is needed.

# Real gap: see libsystem_dyld_initializer_compat.c's own comment. Real
# libSystem.dylib's Libc/init.c calls _dyld_initializer() as part of its
# own real constructor; this project's simplified libSystem.B.dylib has
# no such glue, so a small real constructor calling it directly is the
# minimal correct fix. As of the libsystem_kernel.dylib migration above,
# this constructor's mach_init() call also populates the ONE real, now-
# shared mach_task_self_ both this dylib and libdyld.dylib bind to (see
# that file's own updated comment) -- no more per-dylib duplicate.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libsystem_dyld_initializer_compat.c -o "$OUT/libsystem/dyld_initializer_compat.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libSystem.B.dylib"
# Real macOS's own libSystem.dylib is a thin re-export umbrella: it
# carries almost no code of its own, just LC_REEXPORT_DYLIB on each real
# libsystem_*.dylib, so a symbol like `_strlen` (really defined in
# libsystem_c.dylib) is a valid direct import of libSystem.dylib itself.
# Every sub-dylib below was plain-linked instead (a normal LC_LOAD_DYLIB
# import, pulled in only for the handful of symbols THIS file's own few
# .o's reference) until 2026-08-27, which is why a client linking only
# against libSystem.B.dylib (this project's CF port's own real dynamic
# trial link, docs/backlog.md item d7) could resolve `_getpid` (used
# below, in dyld_initializer_compat.o) but not `_pthread_atfork` (defined
# in libsystem_pthread.dylib, never referenced by this file's own
# objects, so never imported) -- confirmed via a real `nm -gU` showing
# libSystem.B.dylib exported only ~19 locally-compiled symbols and NM
# proof that swapping plain links for `-reexport_library` here resolves
# a not-locally-referenced symbol through an umbrella dylib with real
# ld64-609 (this project's pinned toolchain). `-reexport_library` (not
# plain positional linking) is the real, correct fix, not a workaround:
# it makes each sub-dylib's own real export set part of
# libSystem.B.dylib's, exactly matching real Apple's own architecture,
# not inventing a new one.
    # -not_for_dyld_shared_cache: real, newly-surfaced requirement of
    # the daily-driver Xcode's newer ld (NEWLD_BINDIR above) -- it
    # refuses "-undefined dynamic_lookup"/-U on a dylib it classifies
    # as shared-cache eligible (real /usr/lib install path) unless this
    # is set, a real check Xcode 12's ld64-609 never enforced. This
    # project builds no real dyld shared cache at all, so opting every
    # real dylib out is correct, not a workaround (docs/backlog.md item
    # d9's NEWLD_BINDIR sweep, 2026-08-28).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libSystem.B.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-U,__dyld_initializer \
    -o "$OUT/system/libSystem.B.dylib" \
    "$OUT/libsystem/dyld_initializer_compat.o" \
    -Wl,-reexport_library,"$OUT/system/libsystem_kernel.dylib" \
    -Wl,-reexport_library,"$OUT/system/libsystem_platform.dylib" \
    -Wl,-reexport_library,"$OUT/system/libsystem_blocks.dylib" \
    -Wl,-reexport_library,"$OUT/system/libsystem_malloc.dylib" \
    -Wl,-reexport_library,"$OUT/system/libsystem_pthread.dylib" \
    -Wl,-reexport_library,"$OUT/system/libsystem_c.dylib" \
    -Wl,-reexport_library,"$OUT/system/libsystem_m.dylib" \
    -Wl,-reexport_library,"$OUT/system/libc++.dylib" \
    -Wl,-reexport_library,"$OUT/system/libunwind.dylib" \
    -Wl,-reexport_library,"$OUT/system/libcopyfile.dylib" \
    -Wl,-reexport_library,"$OUT/system/libremovefile.dylib" \
    -Wl,-reexport_library,"$OUT/system/libresolv.dylib" \
    -Wl,-reexport_library,"$OUT/system/libdyld.dylib" \
    "$RTLIB"
    # ALL -Wl,-alias entries REMOVED 2026-08-28 (docs/backlog.md item
    # d10): real, systemic gap, first found via _mmap specifically --
    # every single alias in this dylib's own former list had a BASE
    # symbol only reachable via -reexport_library here, never local to
    # this dylib's own 4 compiled objects (checked directly, confirmed
    # for all ~19 entries, not just _mmap). ld64 silently accepts this
    # and still emits an export-trie REEXPORT node, but with an invalid
    # ordinal (0) -- confirmed via a real `xcrun dyld_info -exports`
    # dump showing every one of them as a suspicious
    # "[re-export] X (Y from <this-image>)" entry, and via
    # third_party/dyld's own real Loader.cpp source, which explicitly
    # rejects ordinal 0 as "out of range" at real symbol-resolution
    # time -- same real bug class already documented and fixed for
    # _fputs/_strerror in build_libsystem_c.sh (search that file for
    # "REAL BUG FOUND"), just far more widespread here, and silent until
    # a real consumer (ICU's own dylib) actually needed to resolve one
    # of these alias NAMES externally instead of via a name that never
    # needed the broken alias to begin with.
    #
    # Real fix, verified for every removed entry via a real
    # `xcrun dyld_info -exports` sweep after rebuilding: every alias
    # NAME this list used to create is already provided correctly
    # elsewhere, either as a genuinely local symbol in the sub-dylib
    # that actually owns it (open$UNIX2003/close$UNIX2003/fcntl$UNIX2003/
    # fstat$INODE64/lstat$INODE64/stat$INODE64/read$UNIX2003 -- all
    # already real, local aliases in build_libsystem_c.sh, confirmed via
    # its own $EXTRACT contents; pthread_testcancel$UNIX2003/
    # pthread_rwlock_*$UNIX2003 -- already real, local in
    # build_libsystem_pthread.sh) or fixed by moving the alias to where
    # its base genuinely is local (_mmap/_munmap -> libsystem_pthread.sh,
    # this same pass). _kdebug_trace/_write/_mprotect/_kill/_sleep$UNIX2003
    # were already redundant with real, direct (non-alias) exports in
    # libsystem_malloc.dylib. _fsync$UNIX2003/_reallocarray$DARWIN_EXTSN
    # are genuinely gone from this dylib now (their old aliases here were
    # ALSO broken the same way, not a working fallback this removal took
    # away) -- confirmed via a real sweep that neither launchd.macho nor
    # ICU's dylibs need them from libSystem specifically: launchd needs
    # plain _fsync and _reallocarray$DARWIN_EXTSN from libdyld.dylib
    # instead (a real, separate, already-valid local alias there, unlike
    # this dylib's dead copy), and ICU needs neither at all.
    # __pthread_cond_wait alias removed 2026-08-28 (see docs/backlog.md
    # item d9's NEWLD_BINDIR sweep): not local to this link either, same
    # "silently dead under ld64-609" class as build_libsystem_c.sh's own
    # matching fix -- plain _pthread_cond_wait is already a real, direct
    # export of the sibling libsystem_pthread.dylib this link already
    # includes, no alias needed.

echo "wrote $OUT/system/libSystem.B.dylib"
otool -hv "$OUT/system/libSystem.B.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
otool -l "$OUT/system/libSystem.B.dylib" | grep -A2 "LC_ID_DYLIB" | grep -q "/usr/lib/libSystem.B.dylib" || {
    echo "error: LC_ID_DYLIB is not /usr/lib/libSystem.B.dylib" >&2
    exit 1
}
if nm -u "$OUT/system/libSystem.B.dylib" | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u "$OUT/system/libSystem.B.dylib" >&2
else
    echo "libSystem.B.dylib: fully static (beyond itself), zero undefined symbols"
fi
