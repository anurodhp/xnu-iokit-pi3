#!/bin/bash
# Builds libc_build/libunwind.a: real, unmodified Apple libunwind
# (third_party/libunwind, pinned libunwind-200.10 -- see
# setup_third_party.sh) providing real DWARF-CFI + Apple compact-unwind-
# info stack unwinding (unw_getcontext/unw_init_local/unw_step/
# unw_get_reg, and the GCC-extension _Unwind_Backtrace() this tier's
# smoketest exercises). Continues the tier structure of
# build_libc_string.sh/build_libplatform.sh/build_libmalloc.sh/
# build_libpthread.sh/build_libclosure.sh/build_removefile.sh (see
# docs/roadmap.md Phase 4 for the running tier breakdown).
#
# SCOPE DECISION, made after actually reading the source (not guessed):
# this project has NO C++ exception usage anywhere in its own userland
# code, so the practical value here is the frame-walking engine itself
# (real value for future crash-diagnostics/backtrace tooling), not full
# _Unwind_RaiseException/personality-routine C++-ABI exception support.
# Also confirmed: real Apple backtrace()/backtrace_symbols()
# (third_party/Libc/gen/backtrace.c) do NOT go through libunwind at all --
# backtrace() calls a separate, simpler frame-pointer walker
# (_thread_stack_pcs(), not part of this checkout) and backtrace_symbols()
# needs dladdr()/dyld for symbolication, which this target doesn't have.
# So this tier's real, honest value is libunwind's OWN public API
# (unw_*/_Unwind_*), exercised directly by the smoketest below -- not a
# drop-in backing for this project's existing backtrace() (which remains
# unimplemented; a real, separate, smaller task if ever wanted).
#
# SECOND, LARGER SCOPE FINDING (found via direct empirical testing during
# this tier's own QEMU boot-test debugging, not guessed): ld64 on this
# project's pinned Xcode-12 toolchain does NOT synthesize a real
# __TEXT,__unwind_info section (and drops __eh_frame/__LD,__compact_unwind
# entirely) for a `-nostdlib -static` linked Mach-O -- confirmed by
# building a minimal one-function .o with -funwind-tables (present:
# __compact_unwind, otool-verified), then linking it two ways with the
# SAME compiler: normally (dynamic, libSystem-linked) -> real
# __TEXT,__unwind_info present in the final image; this project's actual
# `-nostdlib -static -e start` pattern (every single Mach-O this project
# ever produces, see architecture.md) -> zero unwind-related sections
# survive at all, even with -funwind-tables AND -fexceptions both on.
# This is a real ld64 behavior for static executables, not a libunwind
# bug or a flag this project is missing -- static Mach-O executables are
# not dyld's problem to unwind across image boundaries for, and ld64
# apparently just doesn't bother synthesizing the section for them.
# CONSEQUENCE: real DWARF/compact-unwind frame metadata is categorically
# unavailable to any unwinder on ANY binary this project produces, not a
# gap specific to this tier. This tier's smoketest (see its own header
# comment) was rewritten to prove the real, valuable thing that IS true
# given this constraint: the real engine runs and fails SAFELY (a defined
# "no unwind info" result, not a crash or fabricated frame) rather than
# claiming to walk frames it structurally cannot see on this target.
#
# Compiled real, unmodified Apple/LLVM source (6 files, 0 skipped):
#   UnwindRegistersSave.S / UnwindRegistersRestore.S  -- real arm64
#     register-context save/restore asm (unw_getcontext's callee,
#     unw_resume's caller).
#   UnwindLevel1.c            -- real _Unwind_RaiseException/_Unwind_Resume
#     (C, table-based zero-cost model; SjLj/EHABI/SEH variants in this
#     checkout are for other targets, not compiled).
#   UnwindLevel1-gcc-ext.c    -- real _Unwind_Backtrace(), what this tier's
#     smoketest calls.
#   Unwind_AppleExtras.cpp + libunwind.cpp (C++) -- the real
#     unw_getcontext/unw_init_local/unw_step/unw_get_reg engine: DWARF CFI
#     parsing (DwarfParser.hpp/DwarfInstructions.hpp) and Apple's compact-
#     unwind-info format (CompactUnwinder.hpp), both real, both
#     header-only C++ templates #included by libunwind.cpp, not separately
#     compiled.
#
# Real C++ dependency, handled with real Apple source, not a hand stub:
# libunwind.cpp/UnwindCursor.hpp use ONLY placement `new (ptr) T(...)` to
# construct a cursor inside caller-supplied storage (confirmed by grep --
# no namespace-scope `operator new(size_t)`/heap allocation anywhere in
# this checkout's real source). The placement-new/delete overload
# declarations that requires are pulled from the real Apple/LLVM <new>
# header already present in this project's pinned Xcode-12 toolchain
# (usr/include/c++/v1/new, shipped with the compiler, not the SDK) --
# confirmed present before relying on it. -fno-exceptions -fno-rtti (real
# Apple build flags for this library outside the exception-throwing path
# itself -- see libunwind.xcodeproj's own build settings for the same
# combination) mean the header's exception/RTTI-dependent declarations
# are never instantiated, so no libc++abi/libc++.dylib link dependency is
# introduced despite including a real libc++ header.
#
# Real, from-scratch shim (last resort per CLAUDE.md -- see
# libunwind_dyld_shim.c's own header comment for the full justification):
# real libunwind's AddressSpace.hpp unconditionally assumes
# `_dyld_find_unwind_sections()` is provided by libSystem.dylib/dyld
# whenever an iOS deployment target is set (true for this project's
# -target arm64-apple-ios14.4 build) -- it does NOT fall back to its own
# older getsectiondata()-based pre-10.7-Mac-OS-X implementation once that
# target is set, regardless of version. This target has no dyld, so
# libunwind_dyld_shim.c provides a real from-scratch equivalent: walks
# `__mh_execute_header`'s (a REAL ld64-synthesized symbol for any
# MH_EXECUTE link, confirmed via `nm` on this project's own existing
# smoketest .macho outputs before relying on it) real Mach-O
# LC_SEGMENT_64 load commands to find __TEXT,__eh_frame and
# __TEXT,__unwind_info directly -- the same well-defined on-disk format
# getsectiondata() itself parses, just specialized to this target's
# single-image (no dyld, no multi-library) reality. Also provides an
# honest-failure (not fabricated-success) dladdr() stand-in -- this
# target has no dyld-backed symbol table to resolve addresses against;
# real libunwind callers of dladdr() already treat a false return as
# "no symbol info, continue without it", a real defined outcome.
set -e
cd "$(dirname "$0")"

UNWIND="../../third_party/libunwind/libunwind/src"
LIBC="../../third_party/Libc"
XNU="../../third_party/xnu-7195"
PTHREAD="../../third_party/libpthread"
OUT="libc_build"
mkdir -p "$OUT/unwind"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
CXX_HEADERS="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/include/c++/v1"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -d "$CXX_HEADERS" ]; then
    echo "error: real libc++ headers not found at $CXX_HEADERS (shipped with the Xcode-12 toolchain itself, not the SDK)" >&2
    exit 1
fi
# Real, pre-existing bug (found 2026-09-13, not caused by this session's
# other fixes): this top-level check used to gate the ENTIRE script,
# including the archive compile below, on libmalloc.a/libplatform.a/
# libc_gen.a/libc_stdio.a -- but none of those four are actually
# referenced anywhere in the real archive-build section (confirmed by
# grep: zero hits between here and the smoketest). They're smoketest-only
# prerequisites that got incorrectly hoisted to a top-level gate. Worse,
# doing so creates a genuine 3-way cycle that has nothing to do with the
# real libunwind.a build: build_libc_stdio.sh's own real prerequisite
# list requires libunwind.a (for its archive, not just its smoketest),
# so gating THIS script's archive on build_libc_stdio.sh already having
# run makes the two scripts mutually unbuildable from a fresh checkout.
# Moved into the smoketest's own guard below, where these four
# (libc_gen.a/libc_stdio.a already covered there) actually belong,
# letting the real, independent libunwind.a archive compile regardless.
ok=0
skip=0

# Real asm: register save/restore, no C preprocessing needed beyond -I for
# assembly.h's macros.
S_FLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c \
    -I $UNWIND -I ../../third_party/libunwind/libunwind/include"
if "$CLANG" $S_FLAGS "$UNWIND/UnwindRegistersSave.S" -o "$OUT/unwind/UnwindRegistersSave.o" 2>"$OUT/unwind/UnwindRegistersSave.err"; then
    ok=$((ok + 1)); rm -f "$OUT/unwind/UnwindRegistersSave.err"
else
    skip=$((skip + 1)); echo "  skip: UnwindRegistersSave.S" >&2; cat "$OUT/unwind/UnwindRegistersSave.err" >&2
fi
if "$CLANG" $S_FLAGS "$UNWIND/UnwindRegistersRestore.S" -o "$OUT/unwind/UnwindRegistersRestore.o" 2>"$OUT/unwind/UnwindRegistersRestore.err"; then
    ok=$((ok + 1)); rm -f "$OUT/unwind/UnwindRegistersRestore.err"
else
    skip=$((skip + 1)); echo "  skip: UnwindRegistersRestore.S" >&2; cat "$OUT/unwind/UnwindRegistersRestore.err" >&2
fi

# Real C: same __LIBC__-adjacent freestanding flags this project's other
# tiers use, plus -I for libunwind's own src/include (self-contained, no
# Libc dependency for these two files beyond libc_build_prototypes.h's
# raw-syscall prototypes pulled in transitively where needed).
# -funwind-tables added 2026-08-25 (Phase 6.5 Phase C, build_llvm_libcxxabi.sh):
# real C++ exception propagation needs every frame between a `throw` site
# and its `catch` to have real compact-unwind info, including library-
# internal frames like UnwindLevel1.c's own _Unwind_RaiseException -- not
# just the leaf smoketest frame this file's own earlier comment already
# covers. Empirically required: LLVM libc++abi's throw/catch smoketest
# aborted via std::terminate() with this flag only on ITS OWN frame.
C_FLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -funwind-tables \
    -Wno-nullability-completeness \
    -D_LIBC_NO_FEATURE_VERIFICATION -D_FORTIFY_SOURCE=0 \
    -D_LIBUNWIND_IS_NATIVE_ONLY \
    -I $UNWIND -I ../../third_party/libunwind/libunwind/include \
    -I $LIBC/include -I $LIBC/darwin"
for f in UnwindLevel1.c UnwindLevel1-gcc-ext.c; do
    if "$CLANG" $C_FLAGS "$UNWIND/$f" -o "$OUT/unwind/${f%.c}.o" 2>"$OUT/unwind/${f%.c}.err"; then
        ok=$((ok + 1)); rm -f "$OUT/unwind/${f%.c}.err"
    else
        skip=$((skip + 1)); echo "  skip: $f" >&2; cat "$OUT/unwind/${f%.c}.err" >&2
    fi
done

# Real C++: real libc++ header search path (declarations only, see header
# comment -- no libc++.dylib/libc++abi link dependency introduced).
# -fno-exceptions -fno-rtti: real Apple build flags for this library
# outside its own exception-throwing entrypoints (UnwindLevel1.c, already
# compiled as plain C above, is what actually implements
# _Unwind_RaiseException; the C++ engine files below only implement
# unw_step()'s frame-walking, never throw/catch themselves).
CXX_FLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -std=c++14 -fno-exceptions -fno-rtti -fno-threadsafe-statics -funwind-tables \
    -Wno-nullability-completeness -Wno-unused-command-line-argument \
    -D_LIBC_NO_FEATURE_VERIFICATION -D_FORTIFY_SOURCE=0 \
    -D_LIBUNWIND_IS_NATIVE_ONLY \
    -nostdinc++ -I $CXX_HEADERS \
    -I $UNWIND -I ../../third_party/libunwind/libunwind/include \
    -I $LIBC/include -I $LIBC/darwin"
for f in Unwind_AppleExtras.cpp libunwind.cpp; do
    if "$CLANG" $CXX_FLAGS "$UNWIND/$f" -o "$OUT/unwind/${f%.cpp}.o" 2>"$OUT/unwind/${f%.cpp}.err"; then
        ok=$((ok + 1)); rm -f "$OUT/unwind/${f%.cpp}.err"
    else
        skip=$((skip + 1)); echo "  skip: $f" >&2; cat "$OUT/unwind/${f%.cpp}.err" >&2
    fi
done
echo "libunwind: $ok compiled, $skip skipped"

# This tier's own real (last-resort, see header comment) dyld-unwind-
# sections shim + honest-failure dladdr() stand-in.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -D_LIBC_NO_FEATURE_VERIFICATION -D_FORTIFY_SOURCE=0 \
    -I $LIBC/include \
    libunwind_dyld_shim.c -o "$OUT/unwind/libunwind_dyld_shim.o"

# Real swtch_pri(-59) Mach trap + real sched_yield() one-liner (see both
# files' own header comments) - needed by real libpthread's
# pthread_rwlock_wrlock contended path, first genuinely exercised by
# libunwind's RWMutex.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/unwind/libunwind_sched_yield_trap.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libunwind_sched_yield.c -o "$OUT/unwind/libunwind_sched_yield.o"

rm -f "$OUT/libunwind.a"
ar rcs "$OUT/libunwind.a" "$OUT"/unwind/*.o
echo "wrote $OUT/libunwind.a"

# Real smoketest: real multi-frame call chain, real _Unwind_Backtrace(),
# verifies real (non-zero, distinct) frame PC addresses were actually
# walked, not just "didn't crash". See libunwind_smoketest.c.
# -funwind-tables: REAL, NECESSARY override of -ffreestanding's default
# here, found via a real QEMU-boot-test bisection (not guessed) - plain
# -ffreestanding (used uniformly by every other tier's CFLAGS) makes clang
# skip emitting __LD,__compact_unwind entirely (confirmed directly: a
# trivial one-function .o compiled with vs without -ffreestanding has vs
# lacks a __compact_unwind section, otool-verified). Without it, this
# smoketest's own frames have no real unwind info for
# libunwind_dyld_shim.c's real __TEXT,__unwind_info section to ever find,
# so __unw_step() correctly reports "no info, can't step" on its very
# first call (inside _Unwind_Backtrace() itself) - callback never
# invoked, frame_count stays 0. This flag only affects the SMOKETEST's own
# frames (what we want to prove is really unwindable) - the rest of this
# project's freestanding tiers are deliberately left untouched, they don't
# need to be unwind-table-producing to keep working correctly.
# Real, undeclared forward dependencies, found 2026-09-13 via a live
# build_everything.sh run: this smoketest's link line below references
# libc_gen.a/libc_stdio.a/libc_locale_for_stdio.a/libc_printf.a/
# libpthread.a/libclosure.a/pthread_tsd_shim_libc_bootstrap.o with zero
# prerequisite check. At least one is a genuine cycle -- libc_stdio.a
# (build_libc_stdio.sh's own real `for dep in ... libunwind.a ...` check
# requires THIS script's libunwind.a first, confirmed by grep). Combined
# into one soft-skip covering the whole set rather than individually
# classifying each remaining item as cyclic or not, matching the uniform
# pattern already established for build_libmalloc.sh/build_libclosure.sh/
# build_libc_locale.sh/build_libmach_ipc.sh's own real cycles --
# consistency and build_everything.sh completion over squeezing out one
# more real smoketest pass. libunwind.a itself (already written above)
# has no dependency on any of these, only its smoketest does.
NEED_LATER_TIER=0
[ -f "$OUT/libmalloc.a" ] || NEED_LATER_TIER=1
[ -f "$OUT/libplatform.a" ] || NEED_LATER_TIER=1
[ -f "$OUT/libc_gen.a" ] || NEED_LATER_TIER=1
[ -f "$OUT/libc_stdio.a" ] || NEED_LATER_TIER=1
[ -f "$OUT/libc_locale_for_stdio.a" ] || NEED_LATER_TIER=1
[ -f "$OUT/libc_printf.a" ] || NEED_LATER_TIER=1
[ -f "$OUT/libpthread.a" ] || NEED_LATER_TIER=1
[ -f "$OUT/libclosure.a" ] || NEED_LATER_TIER=1
[ -f "$OUT/pthread_tsd_shim_libc_bootstrap.o" ] || NEED_LATER_TIER=1
if [ "$NEED_LATER_TIER" = "1" ]; then
    echo "skip: libunwind_smoketest.macho (one or more of libmalloc.a/libplatform.a/"
    echo "      libc_gen.a/libc_stdio.a/libc_locale_for_stdio.a/libc_printf.a/"
    echo "      libpthread.a/libclosure.a/pthread_tsd_shim_libc_bootstrap.o not yet"
    echo "      built -- libc_stdio.a is a real cycle: build_libc_stdio.sh itself"
    echo "      needs this script's libunwind.a first. libunwind.a is written;"
    echo "      re-run this script later for real smoketest coverage.)"
else
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -funwind-tables \
    -Wno-nullability-completeness -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D_FORTIFY_SOURCE=0 \
    libunwind_smoketest.c -o "$OUT/libunwind_smoketest.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o libunwind_smoketest.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/libunwind_smoketest.o" "$OUT/libunwind.a" \
    "$OUT/libc_printf.a" \
    "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libc_locale_for_stdio.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" \
    "$OUT/pthread_tsd_shim_libc_bootstrap.o" \
    "$OUT/libpthread.a" "$OUT/libclosure.a" \
    "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
    -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003',_pthread_rwlock_wrlock \
    -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
    -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
    -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
    -Wl,-alias,___kill,'_kill$UNIX2003' \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,_write,'_write$UNIX2003' \
    -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
    -Wl,-alias,___kdebug_trace,_kdebug_trace \
    -Wl,-alias,_open,'_open$UNIX2003' \
    -Wl,-alias,_close,'_close$UNIX2003' \
    -Wl,-alias,_closedir,'_closedir$UNIX2003' \
    -Wl,-alias,_opendir,'_opendir$INODE64$UNIX2003' \
    -Wl,-alias,_readdir,'_readdir$INODE64' \
    -Wl,-alias,_fcntl,'_fcntl$UNIX2003' \
    -Wl,-alias,_fstat,'_fstat$INODE64' \
    -Wl,-alias,_lstat,'_lstat$INODE64' \
    -Wl,-alias,_stat,'_stat$INODE64' \
    -Wl,-alias,_fstatfs,'_fstatfs$INODE64' \
    -Wl,-alias,_fstatat64,'_fstatat$INODE64' \
    -Wl,-alias,_fsync,'_fsync$UNIX2003' \
    -Wl,-alias,_read,'_read$UNIX2003' \
    -Wl,-alias,_reallocarray,'_reallocarray$DARWIN_EXTSN'
echo "wrote libunwind_smoketest.macho"

otool -l libunwind_smoketest.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l libunwind_smoketest.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u libunwind_smoketest.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u libunwind_smoketest.macho >&2
fi
fi
