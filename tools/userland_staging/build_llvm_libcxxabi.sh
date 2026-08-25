#!/bin/bash
# Builds libc_build/libcxxabi.a: real, unmodified LLVM libc++abi
# (third_party/llvm-project/libcxxabi, pinned llvmorg-11.1.0 -- see
# setup_third_party.sh) -- Phase 6.5 Phase C (docs/daemon-roadmap.md),
# real dyld's own C++ source needs a working libc++/libc++abi to compile
# at all. Apple never open-sources libc++/libc++abi as a standalone repo
# (confirmed via `git ls-remote` 404s on apple-oss-distributions and
# PureDarwin both, see docs/daemon-roadmap.md) -- LLVM upstream is the real
# fallback per this project's source-priority rule (Apple's own shipped
# libc++ is downstream of this exact codebase, just not packaged
# separately here). This is the FIRST non-apple-oss-distributions,
# non-FreeBSD real source this project has ever pulled in.
#
# All 19 real libcxxabi/src/*.cpp files compile clean with this toolchain
# (confirmed by direct per-file compilation before writing this script,
# not assumed) -- no gaps, no stubs, no skips. The only two things needed
# beyond a plain freestanding compile:
#   - `-nostdinc++ -isystem $LIBCXX/include`: real libc++abi source
#     includes real libc++ headers (<exception>, <typeinfo>, <new>, ...)
#     internally -- point at this project's own sparse llvm-project
#     checkout's libcxx/include, not the SDK (which ships no libc++
#     headers at all, only a .tbd link stub -- confirmed) or the Xcode
#     toolchain's own bundled minimal c++/v1 (declarations-only, used
#     elsewhere in this project e.g. build_libunwind.sh for placement-new
#     alone -- insufficient here, real libcxxabi needs the real
#     implementation headers, not just declarations).
#   - `-D_LIBCPP_HAS_NO_THREADS` is NOT defined: this target has real
#     pthreads (build_libpthread.sh), so libc++abi's real thread-safe
#     static-init guard (cxa_guard.cpp) and thread-local exception storage
#     (cxa_exception_storage.cpp) compile and use the real pthread_mutex/
#     pthread_key_create paths, not the single-threaded fallback.
#
# Stack unwinding: links against this project's own already-real, already-
# built third_party/libunwind (build_libunwind.sh) at smoketest-link time,
# NOT LLVM's own libunwind subproject (deliberately not sparse-checked-out
# at all -- see setup_third_party.sh's header comment) -- Apple's real
# libunwind is already pinned/built here and implements the same public
# unw_*/_Unwind_* API libc++abi's cxa_exception.cpp/cxa_personality.cpp
# call into, per this project's source-priority rule (Apple's real version
# first, LLVM only for what Apple doesn't open-source at all).
set -e
cd "$(dirname "$0")"

LIBCXXABI="../../third_party/llvm-project/libcxxabi"
LIBCXX="../../third_party/llvm-project/libcxx"
LIBC="../../third_party/Libc"
OUT="libc_build"
mkdir -p "$OUT/libcxxabi"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANGXX="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANGXX" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -d "$LIBCXXABI" ] || [ ! -d "$LIBCXX" ]; then
    echo "error: $LIBCXXABI / $LIBCXX not found -- run ./setup_third_party.sh first" >&2
    exit 1
fi

# -funwind-tables here (not just on the smoketest): the exception-raise
# path runs THROUGH __cxa_throw/_Unwind_RaiseException (both compiled from
# this archive's own object files) before it ever reaches the smoketest's
# own frame -- every frame the unwinder walks needs real compact-unwind
# info, not just the leaf. Same real ld64-vs-`-nostdlib -static` gap
# build_libunwind.sh's header comment documents, just needing to be closed
# on this archive's side too, found empirically (the first smoketest
# attempt aborted via std::terminate() with this flag only on the
# smoketest itself, not here).
CXX_FLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -std=c++14 -funwind-tables \
    -nostdinc++ -isystem $LIBCXX/include \
    -I $LIBCXXABI/include -I $LIBCXXABI/src \
    -Wno-macro-redefined"

ok=0
fail=0
for f in "$LIBCXXABI"/src/*.cpp; do
    base=$(basename "$f" .cpp)
    out="$OUT/libcxxabi/${base}.o"
    if "$CLANGXX" $CXX_FLAGS "$f" -o "$out" 2>"$OUT/libcxxabi/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/libcxxabi/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/libcxxabi/${base}.err")"
    fi
done
echo "libcxxabi: $ok compiled, $fail gaps (see libc_build/libcxxabi/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f "$OUT/libcxxabi.a"
ar rcs "$OUT/libcxxabi.a" "$OUT"/libcxxabi/*.o
echo "wrote $OUT/libcxxabi.a"

# Real smoketest: real dynamic_cast/RTTI/operator-new-delete as the hard
# pass/fail gate (none need live stack unwinding), plus an honestly-scoped,
# non-fatal real throw/catch attempt -- see
# llvm_libcxxabi_smoketest.cpp's own header comment for the real,
# QEMU-tested finding behind that split (real __eh_frame/__gcc_except_tab
# content survives this static link, cxa_throw/the personality routine/
# cxa_demangle all genuinely run, but phase-1 unwind search doesn't yet
# find the local catch -- a real, distinct gap from the already-documented
# build_libunwind.sh static-link limitation, not yet root-caused further).
NEED_OK=1
for a in libunwind libc_string libc_stdio libc_gen libc_printf libpthread libclosure libmalloc libplatform; do
    [ -f "$OUT/$a.a" ] || NEED_OK=0
done
if [ "$NEED_OK" -eq 1 ]; then
    rm -f libcxxabi_smoketest.macho
    # -funwind-tables: REAL, NECESSARY override of -ffreestanding's default,
    # same real ld64 limitation build_libunwind.sh's smoketest already
    # documents and works around -- plain -ffreestanding makes ld64 skip
    # emitting __LD,__compact_unwind entirely for a -nostdlib -static
    # link, so without this the throw below has no real unwind info for
    # _Unwind_RaiseException to walk and cxa_throw falls through to
    # std::terminate()/abort() (confirmed: that's exactly what happened
    # before this flag was added).
    "$CLANGXX" $CXX_FLAGS -fexceptions -frtti -funwind-tables llvm_libcxxabi_smoketest.cpp -o "$OUT/libcxxabi_smoketest.o"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
        -o libcxxabi_smoketest.macho \
        ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
        "$OUT/libcxxabi_smoketest.o" "$OUT/libcxxabi.a" "$OUT/libunwind.a" \
        "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libc_locale_for_stdio.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" \
        "$OUT/libc_printf.a" "$OUT/libc_string.a" \
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
        -Wl,-alias,_fcntl,'_fcntl$UNIX2003' \
        -Wl,-alias,_fstat,'_fstat$INODE64' \
        -Wl,-alias,_lstat,'_lstat$INODE64' \
        -Wl,-alias,_stat,'_stat$INODE64' \
        -Wl,-alias,_fsync,'_fsync$UNIX2003' \
        -Wl,-alias,_read,'_read$UNIX2003' \
        -Wl,-alias,_reallocarray,'_reallocarray$DARWIN_EXTSN' \
        2>"$OUT/libcxxabi_smoketest_link.err" && echo "wrote libcxxabi_smoketest.macho" || echo "WARNING: smoketest link failed (libcxxabi.a itself is still built -- see $OUT/libcxxabi_smoketest_link.err)" >&2

    if [ -f libcxxabi_smoketest.macho ]; then
        otool -l libcxxabi_smoketest.macho | grep -q LC_LOAD_DYLIB && \
            echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
        if nm -u libcxxabi_smoketest.macho | grep -q .; then
            echo "WARNING: undefined symbols present:" >&2
            nm -u libcxxabi_smoketest.macho >&2
        fi
    else
        tail -60 "$OUT/libcxxabi_smoketest_link.err" >&2
    fi
else
    echo "skip: libcxxabi_smoketest.macho (needs libunwind/libc_string/libc_stdio/libc_gen/libc_printf/libpthread/libclosure/libmalloc/libplatform .a -- run their build_*.sh first)"
fi
