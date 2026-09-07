#!/bin/bash
# Builds libc_build/libmalloc.a: a real Apple magazine-zone heap
# allocator (third_party/libmalloc, tag libmalloc-317.40.8) - continuing
# the tier structure of build_libc_string.sh/build_libplatform.sh/
# build_libc_locale.sh/build_libc_stdio.sh/build_libc_gen.sh (see
# docs/roadmap.md Phase 4 for the running tier breakdown). Runs after
# build_libplatform.sh (needs real os_unfair_lock/OSAtomic) in the
# pipeline: string -> platform -> locale -> stdio -> gen -> malloc.
#
# Scope: real magazine_tiny/small/medium/large.c + magazine_malloc.c
# (create_scalable_zone, the real malloc_zone_t dispatch table) +
# magazine_rack.c + vm.c (real mach_vm_map-backed page allocation) +
# bitarray.c + malloc_printf.c (real error/debug reporting), all
# UNMODIFIED real Apple source. Deliberately NOT built: nano_malloc.c/
# nanov2_malloc.c (real Apple's fast single-thread-local-cache
# allocator layered ON TOP of the magazine zone - a materially larger,
# separate future item: nanov2 needs real per-thread band/region
# reservation this project hasn't built; nano_zone.h (v1, legacy) is
# additionally hard-gated to x86_64-only in this exact pinned snapshot,
# see libmalloc_compat.h's own comment for the confirmed-not-guessed
# detail), malloc.c itself (the public malloc/free/... C-API dispatcher
# - genuinely can't be used as-is: its real `struct msl` holds dlopen'd
# function pointers into a stack-logging dylib, and there is no dlopen
# on this dyld-less target - libmalloc_dispatch.c is the real, minimal
# replacement for JUST malloc.c's public-API-glue, dispatching straight
# through the real magazine zone's own real malloc_zone_t function
# table - every actual allocation decision is still 100% real Apple
# magazine-allocator code), pguard_malloc.c/purgeable_malloc.c/
# frozen_malloc.c/legacy_malloc.c (real but genuinely optional debug/
# legacy features, not needed for a working heap), msl_lite_support.c
# (Malloc Stack Logging, needs the same dlopen'd dylib malloc.c does).
#
# Real gaps found and fixed while bringing this up (empirically, by
# compiling and reading the actual clang/ld errors, not guessed):
#   - Real Xcode-12/iPhoneOS14.4 SDK lacks mach/mach_vm.h (its own copy
#     literally #errors "mach_vm.h unsupported" - a real iOS-SDK
#     restriction) and mach/shared_region.h isn't on its include path
#     either. mach/mach_vm.h is MIG-generated (from osfmk/mach/
#     mach_vm.defs); this script runs the real `mig` tool (present on
#     this host, confirmed via `which mig`) against real Apple .defs
#     source to regenerate it fresh each build, exactly the same real
#     tool/source Apple's own build uses - not a hand-typed
#     approximation. mach/shared_region.h is real, unmodified source
#     already in this checkout (osfmk/mach/shared_region.h), just needed
#     on the include path.
#   - os/atomic_private.h, pthread/private.h, sys/random.h,
#     thread_stack_pcs.h: all real, unmodified source already present in
#     this project's other third_party/ checkouts (xnu-7195/libkern,
#     libpthread/private, xnu-7195/bsd, Libc/gen respectively) - just
#     needed on the include path, not missing content.
#   - os/feature_private.h and dtrace-probe-generated magmallocProvider.h
#     genuinely absent from every checkout in this project. The former
#     is only referenced by malloc.c/pguard_malloc.c (both out of
#     scope, confirmed by grep) - empty compat stand-in
#     (libc_build/compat_include/os/feature_private.h). The latter has a
#     real, intended-for-exactly-this-situation fallback already built
#     into third_party/libmalloc/src/dtrace.h itself
#     ("#ifndef DARWIN TEST ... #include magmallocProvider.h #else
#     <no-op macros> #endif") - using that real escape hatch
#     (-DDARWINTEST=1), not inventing one.
#   - CONFIG_NANOZONE and the nano_zone.h arm64 gap: see
#     libmalloc_compat.h's own comment for the full real, verified
#     detail (cross-checked against libmalloc.xcodeproj's real Sources
#     list, not guessed) - the __NANO_ZONE_H include-guard-preemption
#     technique there is the standard, honest way to suppress a
#     vendored header this project must not edit whose body isn't
#     needed and can't compile as-is for this arch in this exact pinned
#     snapshot.
#   - _dyld_get_image_slide/_NSGetMachExecuteHeader: this target has no
#     dyld, PID-1 execve()s a statically-linked fixed-address Mach-O
#     directly with no ASLR slide ever applied - see vm.c's real
#     mvm_aslr_init(), whose real "ASLR disabled" branch (zero
#     malloc_entropy) is exactly correct here, not a workaround - see
#     libmalloc_compat.h/libmalloc_dispatch.c.
#   - mach/mach_vm_internal.h and mach/vm_map_internal.h (real
#     mach_vm.c's non-trap MIG-fallback declarations): also MIG-
#     generated (from mach_vm.defs/vm_map.defs, same real `mig` tool
#     run below), genuinely absent as committed source anywhere in this
#     project. mach_vm.c's own logic only reaches these functions if the
#     kernel's fast trap path returns MACH_SEND_INVALID_DEST - this
#     project's kernel genuinely supports every trap involved (real,
#     verified trap numbers, see libmalloc_vm_traps.s), so these are
#     real but provably unreachable dead code - defined honestly
#     (KERN_FAILURE) in libmalloc_vm_mig_stubs.c, not hidden.
#   - mach_port_deallocate/mach_port_mod_refs (needed by real,
#     unmodified mig_reply_port.c's cleanup path): hand-written trap-only
#     wrappers in libmalloc_dispatch.c + libmalloc_vm_traps.s rather than
#     compiling real mach_port.c, which needs its own MIG-generated
#     header plus mach_sync_ipc.h and has many more functions/
#     dependencies than the 2 this project's build actually needs. Real
#     trap numbers -18/-19, cross-checked against syscall_sw.h.
#   - mach_init()/_pthread_set_self(0): real third_party/xnu-7195/
#     libsyscall/mach/mach_init.c itself documents "mach_init() must be
#     called explicitly in static executables" - exactly this project's
#     situation. Called lazily on first malloc()
#     (libmalloc_dispatch.c's get_zone()) rather than unconditionally
#     from Csu (keeps every pre-existing non-malloc smoketest's startup
#     path unchanged). _pthread_set_self is a documented no-op stand-in
#     (real libpthread isn't ported yet, separate future Phase 4 item -
#     this target's real TSD needs are already served by
#     tsd_bootstrap.c/pthread_tsd_shim.c, QEMU-verified).
#   - mach_reply_port(): osfmk/mach/mach_traps.h has TWO declarations
#     (real userland one at line ~88, a second KERNEL-side one further
#     down for the in-kernel syscall table) that don't coexist cleanly
#     for this one real call site (mig_reply_port.c, which itself never
#     includes mach_traps.h at all in this exact pinned tag) - declared
#     directly instead (compat_include/mach_reply_port_decl.h, copied
#     verbatim from the real line-88 signature).
#   - $UNIX2003-suffixed symbols (mprotect$UNIX2003/kill$UNIX2003/
#     write$UNIX2003/sleep$UNIX2003): real Apple public headers rename
#     these call sites via __DARWIN_ALIAS (active by default), but the
#     symbols themselves are only ever produced by a separate
#     libSystem.dylib-umbrella alias-list link step in Apple's real
#     build (see xcodeconfig/*resolver*.xcconfig naming in several of
#     this project's third_party checkouts) - not part of libsyscall's
#     own per-syscall generator (confirmed: 0 matches for "UNIX2003" in
#     that generator or its custom/ dir), and this project has no
#     libSystem umbrella link step. Real, zero-behavior-difference
#     `ld -alias` link flags below (there is no legacy-vs-UNIX03
#     distinction relevant to a from-scratch static binary) stand in for
#     that missing umbrella step - tried a same-file `.set` alias first
#     (matching how Apple's OWN generated .s files alias
#     ___mprotect -> _mprotect internally) but confirmed by testing that
#     `.set` only works within the SAME object file as its target, not
#     across separately-compiled objects - `-alias` is the real
#     cross-object mechanism.
#   - sleep(): real Apple Libc sleep() (Libc/gen/FreeBSD/sleep.c) has a
#     full EINTR-retry loop on top of the real underlying primitive,
#     more machinery than malloc_printf.c's one rare call site
#     (a debug-only "give a debugger time to attach" pause) warrants.
#     libmalloc_sleep_shim.c implements the same real underlying
#     mechanism (a timed __semwait_signal(2) call, the real syscall
#     real Apple sleep()/usleep() are actually built on) directly.
#   - abort()/__assert_rtn(): real minimal implementations in
#     libmalloc_dispatch.c - kill(getpid(), SIGABRT) (the real primitive
#     Apple's own abort() ultimately uses; this target has no
#     libpthread's pthread_kill yet) with the same real infinite-loop
#     fail-safe real abort() has after its kill() call.
#   - libc_string.a (the ORIGINAL build_libc_string.sh output archive)
#     turned out to still contain stale macOS-target (not iOS14.4-target)
#     memcpy.o/memset.o objects frozen in at ar-time, even though the
#     loose .o files on disk were later overwritten with iOS-target
#     versions by build_libc_locale.sh's own independent freebsd_string
#     recompile (confirmed via otool -l's LC_BUILD_VERSION platform
#     field: 2/IOS on disk, but archives are static snapshots of
#     whatever existed at `ar` time) - real "building for iOS, but
#     linking in object file built for macOS" link error, the same
#     class already documented in build_libc_locale.sh's own header
#     comment. Fixed by linking the on-disk (already iOS-target)
#     libc_build/freebsd_string/{memcpy,memset}.o directly instead of
#     the stale archive, plus compiling real third_party/Libc/secure/
#     {memcpy_chk,memset_chk,chk_fail}.c (real Apple _FORTIFY_SOURCE
#     wrappers magazine code calls) fresh under the iOS14.4 target.
#   - The 6 real BSD syscall stubs this tier newly needs (getpid/
#     mprotect/kill/write/madvise/kdebug_trace) plus exit and
#     __semwait_signal: build_libsyscall_smoketest.sh's own precedent
#     compiles Apple's freshly-regenerated stub .s files with the HOST's
#     default macOS toolchain/SDK (-target arm64-apple-macos11, no
#     -isysroot) rather than Xcode-12's iPhoneOS SDK, because that
#     generated assembly needs mach/arm64/asm.h and mach/arm/
#     syscall_sw.h, real headers present in the host's normal macOS SDK
#     but NOT anywhere in Xcode-12's iPhoneOS14.4 SDK or this project's
#     xnu-7195 checkout. Linking macOS-target objects directly into an
#     iOS14.4-target binary fails at link time ("building for iOS, but
#     linking in object file built for macOS") - same class of error as
#     the libc_string.a one above. Fixed by compiling these specific 6+2
#     leaf files WITH -target arm64-apple-ios14.4 (matching everything
#     else) but ALSO pulling mach/arm64/asm.h and mach/arm/syscall_sw.h
#     from the host's normal macOS SDK via -idirafter (pure assembler/
#     preprocessor plumbing, architecture-generic, not the
#     CONFIG_EMBEDDED-vs-XNU_TARGET_OS_OSX struct-layout concern
#     CLAUDE.md's kernel-build warning is about - confirmed by reading
#     what SYS.h actually pulls from these two headers before relying on
#     this).
set -e
cd "$(dirname "$0")"

MALLOC="../../third_party/libmalloc"
XNU="../../third_party/xnu-7195"
PLATFORM="../../third_party/libplatform"
PTHREAD="../../third_party/libpthread"
LIBC="../../third_party/Libc"
CSU="../../third_party/Csu"
OUT="libc_build"
mkdir -p "$OUT/apple_malloc" "$OUT/malloc_mig/mach"

# os/feature_private.h: the empty compat stand-in this script's own
# header comment above already describes ("genuinely absent from every
# checkout in this project ... only referenced by malloc.c/
# pguard_malloc.c, both out of scope"). It is WRITTEN here rather than
# merely assumed to exist -- found 2026-09-07 (DAR-261): the file had
# been created by hand at some point and nothing in the repo recreated
# it, so a `libc_build` that lost it made this script die with
# "apple_malloc: 0 compiled, 9 skipped" followed by
# `ar: libc_build/apple_malloc/*.o: No such file or directory`, i.e.
# every real magazine allocator object silently missing rather than a
# named error. That breaks this project's own "every built artifact
# needs a reproducing script" rule (CLAUDE.md), and it blocked a
# libsystem_malloc.dylib rebuild outright.
#
# compat_include/os/ is shared with build_libc_locale.sh (which writes
# its own os/assumes.h stand-in there and is the reason that directory
# exists at all); this only adds the one file that script does not own,
# and never overwrites anything else in it.
mkdir -p "$OUT/compat_include/os"
cat > "$OUT/compat_include/os/feature_private.h" <<'EOF'
/* Empty stand-in for Apple's os/feature_private.h -- see
 * build_libmalloc.sh's own header comment for the real reasoning. The
 * only symbol real libmalloc reaches for through this header is
 * os_feature_enabled_simple(), referenced solely by malloc.c and
 * pguard_malloc.c, neither of which is in this project's compiled set
 * (confirmed by grep over third_party/libmalloc/src). Written by
 * build_libmalloc.sh; do not edit here, it is regenerated on every run. */
#ifndef _OS_FEATURE_PRIVATE_H_COMPAT_
#define _OS_FEATURE_PRIVATE_H_COMPAT_
#endif
EOF

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/libplatform.a" ]; then
    echo "error: $OUT/libplatform.a not found -- run build_libplatform.sh first" >&2
    exit 1
fi
# Deliberately /usr/bin/mig (the currently-`xcode-select`ed toolchain's
# copy), NOT `xcrun --sdk macosx --find mig`'s (Xcode.app's own
# toolchain copy directly) -- tested both against the exact same real
# input .defs file: the xcrun-resolved one fails to find its own
# <mach/std_types.defs> include ("no SubSystem declaration" / "type not
# defined" cascade) while /usr/bin/mig resolves it correctly (an
# internal-search-path difference between the two copies, not a missing
# file -- std_types.defs genuinely exists in this project's own checkout
# either way, this is purely about which mig's own cpp invocation finds
# it).
MIG="$(which mig)"
if [ -z "$MIG" ] || [ ! -x "$MIG" ]; then
    echo "error: mig tool not found (needed to regenerate mach/mach_vm.h -- see this script's header comment)" >&2
    exit 1
fi
MACSDK="$(xcrun --sdk macosx --show-sdk-path 2>/dev/null)"
if [ -z "$MACSDK" ]; then
    echo "error: host macOS SDK not found (needed for mach/arm64/asm.h -- see this script's header comment)" >&2
    exit 1
fi

# Real MIG generation, from real unmodified Apple .defs source, using
# the exact same tool Apple's own build uses -- see this script's header
# comment for why the header this produces isn't just committed as a
# static file (it's real generated output, same class as the kernel's
# own BUILD/ dir, not source).
# Absolute path for the .defs input, not relative -- mig's own internal
# <mach/std_types.defs> resolution (relative to the input file's own
# directory) breaks on a "../../"-prefixed relative path (confirmed by
# testing: works from an absolute path or from the repo root, fails with
# "no SubSystem declaration" -- a real mig parser quirk, not a missing
# file -- when given "../../third_party/..." from this script's own cwd).
# Generated header lands at malloc_mig/mach/mach_vm_internal.h (mach/
# subdir, matching real mach_vm.c's own "#include <mach/
# mach_vm_internal.h>" exactly -- NOT mach/mach_vm.h, a separate real
# include real mach_vm.c and internal.h also both need, for a
# DIFFERENT, non-MIG-generated purpose: mig's raw output only declares
# the routines' real _kernelrpc_-prefixed names (that's what the .defs
# file's routines are actually named), not the clean mach_vm_allocate/
# mach_vm_map public wrapper names -- those are hand-typed in
# libc_build/compat_include/mach/mach_vm.h instead, see that file's own
# comment for why).
MACH_VM_DEFS="$(cd "$(dirname "$XNU/osfmk/mach/mach_vm.defs")" && pwd)/$(basename "$XNU/osfmk/mach/mach_vm.defs")"
"$MIG" -arch arm64 \
    -header "$OUT/malloc_mig/mach/mach_vm_internal.h" \
    -user "$OUT/malloc_mig/mach_vmUser.c" -server "$OUT/malloc_mig/mach_vmServer.c" -sheader "$OUT/malloc_mig/mach_vmServerHeader.h" \
    "$MACH_VM_DEFS" >/dev/null

# mach/vm_map_internal.h is NOT mig-generated here (unlike
# mach_vm_internal.h above): a real `mig -header` run against
# vm_map.defs produces the FULL real vm_map subsystem, which shares a
# routine (mach_make_memory_entry) with mach_vm.defs's own generated
# output -- both headers included together in the same mach_vm.c
# translation unit (its own real, unmodified #include list) causes a
# genuine duplicate-struct-definition conflict between the two
# independently-generated headers (confirmed by trying it: "conflicting
# types"/"typedef redefinition" on that one shared routine). This
# project's build only actually needs 3 of vm_map_internal.h's dozens of
# real routines (_kernelrpc_vm_map/_kernelrpc_vm_remap/
# _kernelrpc_vm_read -- the only ones real mach_vm.c's vm_map/vm_remap/
# vm_read wrapper functions call), so a small hand-typed compat header
# declaring just those (libc_build/compat_include/mach/
# vm_map_internal.h, real signatures copied verbatim from mach_vm.c's
# own real call sites) avoids the conflict entirely rather than fighting
# mig's namespacing.

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -Wno-availability -Wno-deprecated-declarations \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 -DDARWINTEST=1 -D_FORTIFY_SOURCE=0 \
    -D__API_AVAILABLE_PLATFORM_bridgeos(x)=ios,introduced=x \
    -include libmalloc_compat.h \
    -I $MALLOC/src -I $MALLOC/private -I $MALLOC \
    -I $PLATFORM/private -I $PLATFORM/internal -I $PLATFORM/include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/gen -I libc_build/compat_include \
    -I $PTHREAD/private -I $PTHREAD/include -I $XNU/bsd \
    -I $OUT/malloc_mig \
    -idirafter $XNU/osfmk -idirafter $XNU/libkern -I $XNU/libsyscall"

# Real Apple magazine allocator core -- see this script's header comment
# for the exact scope rationale.
ok=0
skip=0
for f in magazine_tiny magazine_small magazine_medium magazine_large \
         magazine_malloc magazine_rack vm bitarray malloc_printf; do
    out="$OUT/apple_malloc/${f}.o"
    if "$CLANG" $CFLAGS "$MALLOC/src/${f}.c" -o "$out" 2>"$OUT/apple_malloc/${f}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/apple_malloc/${f}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $f"
    fi
done
echo "apple_malloc: $ok compiled, $skip skipped"
# Any skip is fatal, 2026-09-07 (DAR-261 review). All 9 of these are real
# magazine-allocator core files with no optional members -- a partial
# build produces a libmalloc.a that is missing a real allocator object
# and STILL exits 0, because only the every-file-failed case trips the
# `ar: libc_build/apple_malloc/*.o: No such file or directory` glob error
# further down. That is exactly the silent-partial-success this project's
# build scripts exist to prevent (CLAUDE.md's "every built artifact needs
# a reproducing script"), and it is how the missing
# compat_include/os/feature_private.h above went unnoticed.
if [ "$skip" -ne 0 ]; then
    echo "error: $skip of the 9 real magazine allocator sources failed to compile;" >&2
    echo "       libmalloc.a would be silently incomplete. See" >&2
    echo "       $OUT/apple_malloc/*.err for the real per-file errors." >&2
    exit 1
fi

# Real mach_init.c/mach_vm.c/mig_reply_port.c/init_cpu_capabilities.c --
# real Apple libsyscall/wrappers source, unmodified, same include set as
# above minus -include libmalloc_compat.h (not needed -- these don't
# touch nano_zone.h/dyld-slide territory) plus the mach_reply_port_decl.h
# workaround for mig_reply_port.c specifically.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I $XNU/libsyscall/mach -idirafter $XNU/osfmk -I $LIBC/include -I $LIBC/darwin -I libc_build/compat_include \
    -I $OUT/malloc_mig \
    "$XNU/libsyscall/mach/mach_vm.c" -o "$OUT/mach_vm.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -include libc_build/compat_include/mach_reply_port_decl.h \
    -I $XNU/libsyscall/mach -I $XNU/libsyscall -idirafter $XNU/osfmk -I $LIBC/include -I $LIBC/darwin -I libc_build/compat_include \
    "$XNU/libsyscall/mach/mach_init.c" -o "$OUT/mach_init.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -include libc_build/compat_include/mach_reply_port_decl.h \
    -I $XNU/libsyscall/mach -I $XNU/libsyscall -I $XNU/libsyscall/os -idirafter $XNU/osfmk -I $LIBC/include -I $LIBC/darwin -I libc_build/compat_include \
    "$XNU/libsyscall/mach/mig_reply_port.c" -o "$OUT/mig_reply_port.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 -I $XNU/osfmk -I $LIBC/include -I $LIBC/darwin -I libc_build/compat_include \
    "$XNU/libsyscall/wrappers/init_cpu_capabilities.c" -o "$OUT/init_cpu_capabilities.o"

# Real libplatform "generic" (portable-C, not hand-tuned asm) string
# primitives -- _platform_strcmp/_platform_strlen/_platform_memmove/
# _platform_strchr -- the honest real answer for this bring-up target
# (no arch-optimized variant attempted), plus real src/simple/
# string_io.c (the _simple.h string-builder implementation locale/stdio
# work already depends on this header for, now also needed by
# malloc_printf.c's real error-reporting path).
PLATCFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -DPRIVATE=1 -D_LIBC_NO_FEATURE_VERIFICATION -DLONG_BIT=64 -I libc_build/compat_include"
"$CLANG" $PLATCFLAGS -I $PLATFORM/private -I $PLATFORM/internal -I $PLATFORM/include \
    "$PLATFORM/src/string/generic/strcmp.c" -o "$OUT/platform_strcmp.o"
"$CLANG" $PLATCFLAGS -I $PLATFORM/private -I $PLATFORM/internal -I $PLATFORM/include \
    "$PLATFORM/src/string/generic/strlen.c" -o "$OUT/platform_strlen.o"
"$CLANG" $PLATCFLAGS -I $PLATFORM/private -I $PLATFORM/internal -I $PLATFORM/include \
    "$PLATFORM/src/string/generic/memmove.c" -o "$OUT/platform_memmove.o"
"$CLANG" $PLATCFLAGS -I $PLATFORM/private -I $PLATFORM/internal -I $PLATFORM/include \
    "$PLATFORM/src/string/generic/strchr.c" -o "$OUT/platform_strchr.o"
"$CLANG" $PLATCFLAGS -I $PLATFORM/private -I $PLATFORM/internal -I $PLATFORM/include -idirafter $XNU/osfmk -idirafter $XNU/libkern -I $XNU/libsyscall \
    "$PLATFORM/src/simple/string_io.c" -o "$OUT/simple_string_io.o"

# Real Apple fortify-source wrappers (memcpy_chk/memset_chk/chk_fail)
# plus the two real portable memcpy/memset primitives themselves,
# correctly built under the iOS14.4 target this time (see this script's
# header comment on the stale libc_string.a archive bug this works
# around) -- refetched from the real FreeBSD source URL only if not
# already present (same real primitives build_libc_string.sh/
# build_libc_locale.sh already fetch, reused here, not re-vendored).
#
# REAL BUG FOUND VIA QEMU BOOT-TEST, FIXED HERE: -D_FORTIFY_SOURCE=0 is
# REQUIRED on these three files specifically (not just on the magazine
# tier's own CFLAGS above, which already has it) -- without it,
# __memset_chk's/__memcpy_chk's own real body ("return memset(dest, val,
# len);" / "return memcpy(...)") gets macro-rewritten by Apple's own
# <secure/_string.h> right back into a call to itself
# (__builtin___memset_chk -> a real call to __memset_chk, at -O0, since
# __builtin_object_size can't determine a heap pointer's real size) --
# genuine infinite recursion, a real stack-overflow SIGSEGV. Confirmed
# by direct QEMU boot-test bisection: crash was isolated to exactly the
# first plain memset() call inside real magazine_rack.c's rack_init()
# (writing rack->initial_regions, a completely ordinary in-bounds
# 512-byte write to already-mapped, already-writable memory -- ruled out
# entropic-range/mvm_aslr_init state, mach_vm_map's returned address,
# and struct-layout/offset issues one at a time via temporary debug
# prints in scratch copies of vm.c/magazine_malloc.c/magazine_rack.c
# before landing on this). Same root cause class build_libc_string.sh's
# own header comment already documents for stpcpy (search
# "_FORTIFY_SOURCE=0" there) -- this project simply hadn't hit it yet
# for memset/memcpy specifically until real magazine code's own memset()
# call became the first real exerciser.
# -D_FORTIFY_SOURCE=0 on these secure/ compiles is load-bearing, not
# hygiene: without it the fortify inlines in <stdio.h>/<string.h> rewrite
# each __X_chk()'s own internal fall-through call to plain X() back into
# __builtin___X_chk(), i.e. into itself, unconditionally (the rewritten
# call passes __darwin_obsz(dest) == (size_t)-1 for a pointer parameter,
# and every secure/ file's guard treats "unbounded" as "call the plain
# function"). That is infinite recursion -> stack exhaustion -> SIGSEGV.
# This was the real root cause of launchd bug #6; see
# build_launchd_core.sh's fuller write-up above its own secure/ compiles.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 -D_FORTIFY_SOURCE=0 -I libc_build/compat_include -I $LIBC/include -I $LIBC/darwin \
    -D_FORTIFY_SOURCE=0 "$LIBC/secure/memcpy_chk.c" -o "$OUT/memcpy_chk.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 -D_FORTIFY_SOURCE=0 -I libc_build/compat_include -I $LIBC/include -I $LIBC/darwin \
    -D_FORTIFY_SOURCE=0 "$LIBC/secure/memset_chk.c" -o "$OUT/memset_chk.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 -D_FORTIFY_SOURCE=0 \
    -include libc_build/compat_include/os_assumes_malloc_shim.h \
    -I libc_build/compat_include -I $LIBC/include -I $LIBC/darwin \
    -D_FORTIFY_SOURCE=0 "$LIBC/secure/chk_fail.c" -o "$OUT/chk_fail.o"
mkdir -p "$OUT/freebsd_string"
for f in memcpy memset; do
    dst="$OUT/freebsd_string/${f}.c"
    if [ ! -f "$dst" ]; then
        curl -sf -o "$dst" "https://raw.githubusercontent.com/freebsd/freebsd-src/main/lib/libc/string/${f}.c"
    fi
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
        -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 -I libc_build/compat_include -I "$OUT/freebsd_string" \
        "$dst" -o "$OUT/freebsd_string/${f}.o"
done

# Real trap-only wrappers for the handful of syscalls/Mach traps this
# tier needs that no prior tier's build script generated yet -- see this
# script's header comment for exact provenance of each.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c libmalloc_vm_traps.s -o "$OUT/libmalloc_vm_traps.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -I libc_build/compat_include -I $OUT/malloc_mig \
    libmalloc_vm_mig_stubs.c -o "$OUT/libmalloc_vm_mig_stubs.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libmalloc_sleep_shim.c -o "$OUT/libmalloc_sleep_shim.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -DPRIVATE=1 -D_LIBC_NO_FEATURE_VERIFICATION -I libc_build/compat_include \
    libmalloc_dispatch.c -o "$OUT/libmalloc_dispatch.o"

# Real TSD bootstrap (pthread_tsd_shim.c) -- REQUIRED here, not optional:
# see libmalloc_dispatch.c's own header comment on the real SIGSEGV this
# fixed (real os_unfair_lock's _os_lock_owner_get_self() dereferences
# TPIDRRO_EL0 as a TSD block pointer; build_libplatform.sh's own
# libplatform.a deliberately does NOT bundle this in, by design -- every
# real consumer, this one included, must link it and call
# pthread_tsd_shim_bootstrap() explicitly, same as
# pthread_tsd_shim_smoketest.c already does). Same real source, same
# flags as build_libplatform.sh's own build of this file.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -include libc_build_prototypes.h \
    pthread_tsd_shim.c -o "$OUT/pthread_tsd_shim.o"

# Real Apple syscall stubs regenerated fresh (same generator
# build_libsyscall_smoketest.sh already established), compiled with the
# iOS14.4 target but borrowing mach/arm64/asm.h + mach/arm/syscall_sw.h
# from the host's normal macOS SDK -- see this script's header comment
# for exactly why this specific mix is safe (pure assembler plumbing,
# not the kernel's CONFIG_EMBEDDED struct-layout concern).
TMPGEN="$(mktemp -d)"
trap 'rm -rf "$TMPGEN"' EXIT
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" \
    "$XNU/libsyscall/custom" \
    "$XNU/libsyscall/Platforms" \
    iPhoneOS \
    "$TMPGEN" >/dev/null
SYSCFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -I $XNU/libsyscall/custom -I $XNU/libsyscall/mach -idirafter $XNU/osfmk -idirafter $MACSDK/usr/include"
"$CLANG" $SYSCFLAGS "$TMPGEN/___getpid.s" -o "$OUT/sys_getpid.o"
"$CLANG" $SYSCFLAGS "$TMPGEN/___mprotect.s" -o "$OUT/sys_mprotect.o"
"$CLANG" $SYSCFLAGS "$TMPGEN/___kill.s" -o "$OUT/sys_kill.o"
"$CLANG" $SYSCFLAGS "$TMPGEN/_write.s" -o "$OUT/sys_write.o"
"$CLANG" $SYSCFLAGS "$TMPGEN/_madvise.s" -o "$OUT/sys_madvise.o"
"$CLANG" $SYSCFLAGS "$TMPGEN/___kdebug_trace.s" -o "$OUT/sys_kdebug_trace.o"
"$CLANG" $SYSCFLAGS "$TMPGEN/___exit.s" -o "$OUT/sys_exit.o"
"$CLANG" $SYSCFLAGS "$TMPGEN/___semwait_signal.s" -o "$OUT/sys_semwait_signal.o"

rm -f "$OUT/libmalloc.a"
ar rcs "$OUT/libmalloc.a" \
    "$OUT"/apple_malloc/*.o \
    "$OUT/mach_vm.o" "$OUT/mach_init.o" "$OUT/mig_reply_port.o" "$OUT/init_cpu_capabilities.o" \
    "$OUT/platform_strcmp.o" "$OUT/platform_strlen.o" "$OUT/platform_memmove.o" "$OUT/platform_strchr.o" \
    "$OUT/simple_string_io.o" \
    "$OUT/memcpy_chk.o" "$OUT/memset_chk.o" "$OUT/chk_fail.o" \
    "$OUT/freebsd_string/memcpy.o" "$OUT/freebsd_string/memset.o" \
    "$OUT/libmalloc_vm_traps.o" "$OUT/libmalloc_vm_mig_stubs.o" \
    "$OUT/libmalloc_sleep_shim.o" "$OUT/libmalloc_dispatch.o" "$OUT/pthread_tsd_shim.o" \
    "$OUT/sys_getpid.o" "$OUT/sys_mprotect.o" "$OUT/sys_kill.o" "$OUT/sys_write.o" \
    "$OUT/sys_madvise.o" "$OUT/sys_kdebug_trace.o" "$OUT/sys_exit.o" "$OUT/sys_semwait_signal.o"
echo "wrote $OUT/libmalloc.a"

# Real smoketest: malloc/write/verify, calloc zero-init, realloc-grow
# preserving contents, and a 200-round alloc/free stress loop across
# size classes (a broken free-list/coalescing bug usually corrupts
# allocator metadata that only crashes on a LATER unrelated allocation,
# not the free() call itself -- so later iterations succeeding is itself
# part of the real test).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libmalloc_smoketest.c -o "$OUT/libmalloc_smoketest.o"
# $UNIX2003-suffixed link aliases -- see this script's header comment.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o libmalloc_smoketest.macho \
    "$CSU/start.s" "$CSU/crt.c" \
    "$OUT/libmalloc_smoketest.o" "$OUT/libmalloc_dispatch.o" \
    "$OUT"/apple_malloc/*.o \
    "$OUT/mach_vm.o" "$OUT/mach_init.o" "$OUT/mig_reply_port.o" "$OUT/init_cpu_capabilities.o" \
    "$OUT/platform_strcmp.o" "$OUT/platform_strlen.o" "$OUT/platform_memmove.o" "$OUT/platform_strchr.o" \
    "$OUT/simple_string_io.o" \
    "$OUT/libmalloc_vm_traps.o" "$OUT/libmalloc_vm_mig_stubs.o" "$OUT/libmalloc_sleep_shim.o" \
    "$OUT/pthread_tsd_shim.o" \
    "$OUT/sys_getpid.o" "$OUT/sys_mprotect.o" "$OUT/sys_kill.o" "$OUT/sys_write.o" \
    "$OUT/sys_madvise.o" "$OUT/sys_kdebug_trace.o" "$OUT/sys_exit.o" "$OUT/sys_semwait_signal.o" \
    "$OUT/memcpy_chk.o" "$OUT/memset_chk.o" "$OUT/chk_fail.o" \
    "$OUT/freebsd_string/memcpy.o" "$OUT/freebsd_string/memset.o" \
    "$OUT/libplatform.a" "$OUT/cerror_stub.o" \
    "$OUT/libc_locale_for_stdio.a" "$OUT/libc_stdio.a" \
    -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
    -Wl,-alias,___kill,'_kill$UNIX2003' \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,_write,'_write$UNIX2003' \
    -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
    -Wl,-alias,___kdebug_trace,_kdebug_trace
echo "wrote libmalloc_smoketest.macho"

otool -l libmalloc_smoketest.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l libmalloc_smoketest.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u libmalloc_smoketest.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u libmalloc_smoketest.macho >&2
fi
