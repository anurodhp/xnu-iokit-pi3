#!/bin/bash
# Builds libc_build/system/libsystem_malloc.dylib: static-to-dylib
# migration batch 2a (docs/daemon-roadmap.md's "Static-vs-dylib
# architecture debt" section).
#
# THIS ONE IS A REAL CORRECTNESS FIX, NOT JUST ARCHITECTURE CLEANUP.
# Audited before writing this script (same duplicate-global-state check
# already caught mach_task_self_ and errno): libmalloc_dispatch.c's
# `static malloc_zone_t *g_zone` is the live heap zone pointer every
# malloc/free/realloc call in this project's port goes through
# (create_scalable_zone() is only ever called once, lazily, the first
# time get_zone() sees g_zone == NULL). Until this script, libdyld.dylib
# and libSystem.B.dylib EACH bundled their own private static copy of
# the entire magazine allocator, including their own separate g_zone --
# meaning any pointer malloc'd via code executing in one dylib and
# free'd via code executing in the other would be operating on a
# completely different heap's metadata. That is real, silent heap
# corruption territory (not merely "wrong value" the way a duplicated
# errno is) -- the most serious instance of this bug class found so far
# in this migration. This single, real, shared libsystem_malloc.dylib
# (real Apple's own architecture) closes it for good: one process, one
# zone, matching every other real macOS process.
#
# Real, unmodified Apple source: same file set build_libmalloc.sh
# already established for static libmalloc.a (its own header comment has
# the full scope/gap writeup, not re-derived here) -- reused by
# extracting the already-built, already-verified-correct member objects
# out of libc_build/libmalloc.a directly (ar x) rather than recompiling
# from scratch a second time, EXCLUDING the 10 objects that now live in
# separate, real dylibs instead (6 in libsystem_kernel.dylib as of
# migration step 1, 4 generic string primitives in
# libsystem_platform.dylib as of this step -- see that script's own
# comment). The remaining ~25 objects (the actual magazine allocator:
# bitarray/magazine_*/vm/malloc_printf, plus this project's own
# libmalloc_dispatch.c/libmalloc_sleep_shim.c, plus real Apple
# fortify-source memcpy_chk/memset_chk/chk_fail + the real FreeBSD
# memcpy/memset primitives, plus pthread_tsd_shim.o and a handful of
# real BSD syscall stubs build_libmalloc.sh generated for its own
# private use) have no comparable process-global state of their own
# (per-zone metadata all hangs off g_zone itself, which every one of
# these objects reaches through the same single copy once linked into
# one dylib) -- carried over unchanged.
#
# Unlike the earlier libmalloc_dispatch.o exclusion dance
# build_libdyld.sh/build_libsystem.sh used to need (hiding its stale
# __platform_memset/_dyld_get_image_slide stand-ins behind a real
# implementation linked into the SAME flat static archive): that trick
# doesn't apply here. Real dylibs resolve symbols via two-level
# namespace, bound per-reference at the CONSUMER's link time, not a
# flattened single symbol table the way `ar`-combined static archives
# work -- so libmalloc_dispatch.o's two stand-ins living inside this
# dylib don't collide with libsystem_platform.dylib's real
# __platform_memset or libdyld.dylib's own real _dyld_get_image_slide;
# nothing outside this dylib ever binds to this dylib's copies of either
# symbol.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
mkdir -p "$OUT/libsystem_malloc/extracted"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
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
if [ ! -f "$OUT/libmalloc.a" ]; then
    echo "error: $OUT/libmalloc.a not found -- run build_libmalloc.sh first (this script reuses its already-built member objects)" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libsystem_kernel.dylib" ] || [ ! -f "$OUT/system/libsystem_platform.dylib" ]; then
    echo "error: libsystem_kernel.dylib / libsystem_platform.dylib not found -- run build_libsystem_kernel.sh and build_libsystem_platform.sh first" >&2
    exit 1
fi

# memcpy.o/memset.o REMOVED from MEMBERS 2026-09-04 (DAR-13, real-Apple-
# parity fix): real Apple never defines plain _memcpy/_memset in
# libsystem_malloc.dylib -- they're owned by exactly one dylib,
# libsystem_platform.dylib (see that script's own updated header comment
# for the full real-source trail). This dylib's own magazine allocator
# source (vm.c's mvm_aslr_init, magazine_rack.c's rack_init, etc) still
# calls plain memcpy()/memset() internally -- those now resolve
# externally to the libsystem_platform.dylib sibling already linked
# below (real, direct link, not just transitive), same real "one owner
# per symbol" fix class as g_zone/mach_task_self_/errno. memcpy_chk.o/
# memset_chk.o (the _FORTIFY_SOURCE __memcpy_chk/__memset_chk wrappers --
# DIFFERENT symbols, out of this ticket's scope) are untouched.
MEMBERS="bitarray.o magazine_large.o magazine_malloc.o magazine_medium.o magazine_rack.o \
    magazine_small.o magazine_tiny.o malloc_printf.o vm.o simple_string_io.o \
    memcpy_chk.o memset_chk.o chk_fail.o \
    libmalloc_sleep_shim.o \
    sys_mprotect.o sys_kill.o sys_write.o sys_madvise.o sys_kdebug_trace.o \
    sys_exit.o sys_semwait_signal.o"
rm -rf "$OUT/libsystem_malloc/extracted"
mkdir -p "$OUT/libsystem_malloc/extracted"
(cd "$OUT/libsystem_malloc/extracted" && ar x "../../libmalloc.a" $MEMBERS)

# pthread_tsd_shim.o REMOVED from the MEMBERS ar-x list above 2026-09-14
# (real gap, found via a live build_everything.sh run): build_libpthread.sh
# now retires this exact member from the SHARED libmalloc.a (real,
# deliberate, documented -- avoids a genuine duplicate-symbol conflict
# against libpthread.a's own real pthread_tsd.c for STATIC consumers
# that link both archives together, e.g. build_libc_stdio.sh's own
# smoketest). But THIS script has an unrelated, real, independent need
# for the exact same object (see this file's own header comment just
# above: libsystem_malloc.dylib can't link the real
# libsystem_pthread.dylib without a genuine circular dylib dependency,
# which is why pthread_tsd_shim.c's own from-scratch TSD implementation
# exists at all) -- the two real requirements share one source file but
# are otherwise unrelated, and libpthread.sh's retirement (correct for
# its own real reason) silently broke this script's own `ar x`, which
# assumed the member would always still be there. Fix: compile
# pthread_tsd_shim.c directly here instead of depending on libmalloc.a
# still containing it -- same real source, same flags as
# build_libmalloc.sh's own original build of this file, writing to the
# same extracted/ path so the rest of this script's narrowing step below
# is unaffected.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O2 \
    -Wno-nullability-completeness -include libc_build_prototypes.h \
    pthread_tsd_shim.c -o "$OUT/libsystem_malloc/extracted/pthread_tsd_shim.o"
# Not `ar x`-extracted above (see comment there) -- added back to
# MEMBERS here, after the freshly-compiled object already exists on disk
# under the same filename, purely so the final link's
# `for m in $MEMBERS` loop below picks it up (same real precedent as
# libmalloc_dispatch.o just below).
MEMBERS="$MEMBERS pthread_tsd_shim.o"

# os_crash(): real gap, found 2026-09-14 via a live build_everything.sh
# run. chk_fail.o (extracted above from libmalloc.a) calls real
# os_crash() -- build_libmalloc.sh's own compat_include/os/assumes.h
# (see that script's own header comment) only ever wrote the real
# DECLARATION (`void os_crash(const char *format, ...)
# __attribute__((noreturn, format(printf, 1, 2)))`), never a body,
# because chk_fail.o has always just been compiled there, not fully
# linked -- build_libmalloc.a's own smoketest already tolerates
# undefined symbols as a WARNING, not a hard failure, so this real gap
# was silently latent until now, the first time chk_fail.o is linked
# into something (a real dylib) that requires every symbol resolved.
# Real semantics needed (matching Apple's own os_crash: an unconditional
# process-terminating call for a detected fatal corruption, per this
# project's own established "fail-closed" precedent, e.g.
# libc_printf_mach_stub.c's real %n-write mitigation) -- __builtin_trap()
# is a real compiler intrinsic (illegal instruction trap), needing zero
# external symbols, so this real implementation can't itself introduce
# a new link dependency.
cat > "$OUT/libsystem_malloc/os_crash_impl.c" <<'EOF'
#include <stdarg.h>
__attribute__((noreturn))
void os_crash(const char *format, ...) {
	(void)format;
	__builtin_trap();
}
EOF
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O2 \
    "$OUT/libsystem_malloc/os_crash_impl.c" -o "$OUT/libsystem_malloc/extracted/os_crash_impl.o"
MEMBERS="$MEMBERS os_crash_impl.o"

# libmalloc_dispatch.o recompiled fresh here (NOT `ar x`-extracted from
# libmalloc.a like the rest of MEMBERS) 2026-08-30 (static-linking
# cleanup, dylib-vs-dylib duplicate-symbol pass): the pre-built object
# in libmalloc.a carries real, working `_pthread_set_self`/
# `mach_port_deallocate`/`mach_port_mod_refs` for its OTHER, static-only
# consumers (dyld.macho via build_dyld_link.sh, which must stay fully
# static and cannot link any dylib) -- but those 3 duplicate real
# exports libsystem_kernel.dylib already has (see that script's own
# comment: "copied verbatim from ... libmalloc_dispatch.c"), and nothing
# else in THIS dylib references them locally (confirmed via nm -m on
# every other MEMBERS object). LIBMALLOC_DISPATCH_DYLIB_VARIANT (see
# libmalloc_dispatch.c's own header comment on this exact guard) compiles
# a variant without those 3, so this dylib falls through to the real
# libsystem_kernel.dylib sibling it already links below instead.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O2 \
    -DPRIVATE=1 -D_LIBC_NO_FEATURE_VERIFICATION -DLIBMALLOC_DISPATCH_DYLIB_VARIANT=1 -I libc_build/compat_include \
    libmalloc_dispatch.c -o "$OUT/libsystem_malloc/extracted/libmalloc_dispatch_dylib.o"

# __dyld_get_image_slide/__platform_memset narrowed to private-extern:
# this dylib is BELOW libdyld.dylib AND libsystem_platform.dylib in the
# real dependency order for these two specific symbols (malloc cannot
# link dyld back -- circular, dyld itself needs malloc to allocate
# memory; and while malloc DOES link libsystem_platform.dylib as a
# sibling below, this file's own `_platform_memset` predates that real
# sibling existing at all -- see this file's own header comment: "there
# is NO real _platform_memset source anywhere in this [checkout]", so
# it's a genuine hand-written stand-in, not merely stale), so real,
# unmodified vm.c's mvm_aslr_init() and the magazine allocator's own
# memset(-via-macro) calls genuinely need local definitions here (same
# bottom-of-stack necessity as pthread_tsd_shim.o above) -- but nothing
# OUTSIDE this dylib should ever bind to malloc's own copies instead of
# libdyld.dylib's/libsystem_platform.dylib's real ones. Same
# `ld -r -keep_private_externs -unexported_symbol` narrowing this
# project already uses in narrow_rune_locale.sh/build_libdispatch_
# dylib.sh's ___gettimeofday_with_mach/this script's own pthread_tsd_
# shim.o block below.
XCODE12_LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
"$XCODE12_LD" -r -arch arm64 -keep_private_externs \
    -unexported_symbol __dyld_get_image_slide -unexported_symbol __platform_memset \
    "$OUT/libsystem_malloc/extracted/libmalloc_dispatch_dylib.o" \
    -o "$OUT/libsystem_malloc/extracted/libmalloc_dispatch.o"
rm -f "$OUT/libsystem_malloc/extracted/libmalloc_dispatch_dylib.o"
# Not `ar x`-extracted above (see comment there) -- added back to
# MEMBERS here, after the freshly-compiled+narrowed object already
# exists on disk under the same filename, purely so the final link's
# `for m in $MEMBERS` loop below picks it up.
MEMBERS="$MEMBERS libmalloc_dispatch.o"

# pthread_tsd_shim.o narrowed 2026-08-30 (static-linking cleanup, Phase
# 1d). Real constraint, not an oversight: libsystem_pthread.dylib itself
# links libsystem_malloc.dylib as a sibling (build_libsystem_pthread.sh's
# own link line), so malloc cannot link the real pthread dylib back
# without a real dependency cycle -- this is why pthread_tsd_shim.c
# exists at all (see its own header: an independent, from-scratch TSD
# block built on the same real TPIDRRO_EL0/platform_syscall mechanism,
# not delegating to the real pthread library malloc sits below).
#
# build_libsystem_c.sh already dropped its OWN copy of this exact object
# in favor of the real libsystem_pthread.dylib sibling (libsystem_c has
# no such cycle) -- that fix isn't available here, but the audit-visible
# problem is narrower than "malloc needs its own TSD": it is that this
# object EXPORTS the same public names
# (_pthread_getspecific/_pthread_setspecific/_pthread_key_create/
# _pthread_once/_pthread_key_init_np) the real libsystem_pthread.dylib
# does, so any OTHER image's genuine pthread_getspecific() call could,
# depending on two-level-namespace search order, bind to malloc's
# private shim instead of the real implementation -- a real symbol-
# collision hazard, not merely a cosmetic duplicate. The shim's own
# internal callers (magazine_*.o etc.) still resolve to it locally
# regardless of external visibility, so hiding it changes nothing about
# malloc's own behavior -- only which OTHER images can see the name.
#
# Not touched: the shim's actual TSD mechanism (QEMU-verified, per its
# own header) -- only the 5 symbols' external visibility, via the same
# `ld -r -keep_private_externs -unexported_symbol` narrowing this
# project already uses in narrow_rune_locale.sh and
# build_libdispatch_dylib.sh's ___gettimeofday_with_mach.
XCODE12_LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
"$XCODE12_LD" -r -arch arm64 -keep_private_externs \
    -unexported_symbol _pthread_getspecific -unexported_symbol _pthread_setspecific \
    -unexported_symbol _pthread_key_create -unexported_symbol _pthread_once \
    -unexported_symbol _pthread_key_init_np \
    "$OUT/libsystem_malloc/extracted/pthread_tsd_shim.o" \
    -o "$OUT/libsystem_malloc/extracted/pthread_tsd_shim.o.narrowed"
mv -f "$OUT/libsystem_malloc/extracted/pthread_tsd_shim.o.narrowed" "$OUT/libsystem_malloc/extracted/pthread_tsd_shim.o"

# getpid/__current_pid (sys_getpid.o) and the __fork raw trap used to live
# here (DAR-292): __fork.s updates __current_pid through a local
# adrp/lo12 pair, so both had to be in one image, and moving the pair to
# libsystem_kernel meant relinking every getpid() caller. DAR-423 did that
# move and relink: both are libsystem_kernel's now, as on Apple
# (build_libsystem_kernel.sh), and this dylib binds getpid from there.

mkdir -p "$OUT/system"
rm -f "$OUT/system/libsystem_malloc.dylib"
# Real UNIX2003/cancelable-variant symbol aliases the magazine
# allocator's own real source calls by these exact names -- same real
# aliases build_libdyld.sh/build_libsystem.sh's own link lines already
# apply at the outer dylib (needed there too, for everything else that
# calls these), now also needed HERE since these particular raw syscall
# stubs (sys_kill.o/sys_write.o/sys_mprotect.o/sys_kdebug_trace.o/
# libmalloc_sleep_shim.o, all in MEMBERS above) live inside this dylib,
# not the outer one, and real dylib symbol resolution doesn't flatten
# across image boundaries the way static-archive linking did.
    # -not_for_dyld_shared_cache: real, newly-surfaced requirement of
    # the daily-driver Xcode's newer ld (NEWLD_BINDIR above) -- it
    # refuses "-undefined dynamic_lookup"/-U on a dylib it classifies
    # as shared-cache eligible (real /usr/lib install path) unless this
    # is set, a real check Xcode 12's ld64-609 never enforced. This
    # project builds no real dyld shared cache at all, so opting every
    # real dylib out is correct, not a workaround (docs/backlog.md item
    # d9's NEWLD_BINDIR sweep, 2026-08-28).
# The 6 aliases below exist ONLY because this dylib's own magazine
# allocator source calls these exact names internally (see the comment
# above this link command) -- they were never meant to be PUBLIC
# exports, and every one of them duplicates a real export already owned
# elsewhere (libdyld.dylib/libsystem_c.dylib for the $UNIX2003/plain
# forms) -- hidden via `-Wl,-unexported_symbol` right below each alias,
# 2026-08-30 (static-linking cleanup, dylib-vs-dylib duplicate-symbol
# pass). Real base symbols (___kill, ___kdebug_trace, _mprotect,
# __mal_semwait_sleep, _write) are left exported -- they are NOT
# themselves flagged duplicates today (see docs/static-linking-cleanup-
# plan.md's session notes: ___kill vs libxpc's own separate copy is a
# distinct, not-yet-fixed fact, deferred to a libxpc-focused pass rather
# than folded in here).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libsystem_malloc.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-U,_getenv -Wl,-U,_getprogname -Wl,-U,_isatty \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,_reallocarray,'_reallocarray$DARWIN_EXTSN' \
    -Wl,-alias,_reallocarrayf,'_reallocarrayf$DARWIN_EXTSN' \
    -Wl,-alias,___kill,'_kill$UNIX2003' \
    -Wl,-alias,___kdebug_trace,_kdebug_trace \
    -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
    -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
    -Wl,-alias,_write,'_write$UNIX2003' \
    -Wl,-unexported_symbol,_kill -Wl,-unexported_symbol,'_kill$UNIX2003' \
    -Wl,-unexported_symbol,_kdebug_trace -Wl,-unexported_symbol,'_mprotect$UNIX2003' \
    -Wl,-unexported_symbol,'_sleep$UNIX2003' -Wl,-unexported_symbol,'_write$UNIX2003' \
    -o "$OUT/system/libsystem_malloc.dylib" \
    $(for m in $MEMBERS; do echo "$OUT/libsystem_malloc/extracted/$m"; done) \
    "$OUT/system/libsystem_kernel.dylib" "$OUT/system/libsystem_platform.dylib"

echo "wrote $OUT/system/libsystem_malloc.dylib"
otool -hv "$OUT/system/libsystem_malloc.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
if nm -u "$OUT/system/libsystem_malloc.dylib" | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u "$OUT/system/libsystem_malloc.dylib" >&2
else
    echo "libsystem_malloc.dylib: fully self-contained (beyond libsystem_kernel.dylib/libsystem_platform.dylib), zero undefined symbols"
fi
