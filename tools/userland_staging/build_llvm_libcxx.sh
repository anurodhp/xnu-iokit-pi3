#!/bin/bash
# Builds libc_build/libcxx.a: real, unmodified LLVM libc++
# (third_party/llvm-project/libcxx, pinned llvmorg-11.1.0 -- see
# setup_third_party.sh and build_llvm_libcxxabi.sh, which pulled in the
# same sparse checkout for libc++abi first). Phase 6.5 Phase C
# (docs/daemon-roadmap.md): real dyld C++ source needs a working
# libc++/libc++abi to compile at all; libc++abi alone (cxa_* / RTTI /
# demangler) isn't the STL itself.
#
# All 36 real top-level libcxx/src/*.cpp files this CMakeLists.txt builds
# on a non-Windows/non-Solaris target compile clean with this toolchain
# (confirmed by direct per-file compilation before writing this script,
# not assumed) -- no gaps, no stubs, no skips.
#
# Explicitly OUT OF SCOPE, matching this project's existing dyld3-shared-
# cache-style real scope decisions (a real gap documented, not papered
# over):
#   - filesystem/{operations,directory_iterator,int128_builtins}.cpp:
#     std::filesystem. dyld2 (this port's real target, see
#     docs/daemon-roadmap.md's dyld2-vs-dyld3 finding) doesn't use it;
#     revisit only if a later real dyld/STL consumer needs it.
#   - experimental/memory_resource.cpp: Library Fundamentals TS, not
#     needed by dyld2 either.
#   - support/win32/*.cpp, support/solaris/*: other-platform source,
#     CMakeLists.txt itself only builds these under _WIN32/__sun__.
#
# Flags, found empirically (see this file's compile loop):
#   - `-nostdinc++ -isystem $LIBCXX/include`: same reasoning as
#     build_llvm_libcxxabi.sh -- point at this project's own sparse
#     llvm-project checkout, not the SDK (no libc++ headers at all) or
#     the Xcode toolchain's own bundled c++/v1 (would silently pull in
#     Apple's real shipped libc++ headers instead of building against
#     OUR sources' own headers -- confirmed via `-v` search-path dump
#     that -nostdinc++ correctly drops the toolchain's c++/v1 from the
#     search list, leaving only $LIBCXX/include and the SDK's plain C
#     headers for the `#include_next <string.h>`-style forwarding those
#     headers do).
#   - `-I $LIBCXXABI/include`: libc++'s own headers/src (<exception>,
#     typeinfo.cpp, etc.) include real libc++abi headers (<cxxabi.h>,
#     __cxxabi_config.h) directly.
#   - `-D_LIBCPP_BUILDING_LIBRARY`: real libc++ own CMake build always
#     defines this when compiling the library itself (not just consuming
#     it) -- gates some internal-linkage-only declarations.
#   - `-std=c++14`, RTTI on, `-funwind-tables`: same real reasons as
#     build_llvm_libcxxabi.sh (C++17 drops `std::unexpected_handler`
#     that libc++abi's headers still reference; RTTI/typeinfo.cpp IS
#     part of the real implementation; unwind tables needed on every
#     frame in the throw path, not just the leaf -- see
#     build_libunwind.sh's original finding).
#
# NOTE: this script's own shell loop MUST run under bash (not the
# interactive zsh this repo's shell defaults to) -- zsh does not word-
# split an unquoted "$CXX_FLAGS" the way bash does, which silently
# collapses the whole flag string into a single -isysroot argument value
# (confirmed the hard way while probing these flags interactively; see
# feedback_qemu_test... no -- see this project's zsh-word-splitting
# lesson). The `#!/bin/bash` shebang here is load-bearing, not cosmetic.
set -e
cd "$(dirname "$0")"

LIBCXX="../../third_party/llvm-project/libcxx"
LIBCXXABI="../../third_party/llvm-project/libcxxabi"
LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
OUT="libc_build"
mkdir -p "$OUT/libcxx"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANGXX="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANGXX" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -d "$LIBCXX" ] || [ ! -d "$LIBCXXABI" ]; then
    echo "error: $LIBCXX / $LIBCXXABI not found -- run ./setup_third_party.sh first" >&2
    exit 1
fi

CXX_FLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -std=c++14 -funwind-tables \
    -nostdinc++ -isystem $LIBCXX/include \
    -I $LIBCXXABI/include \
    -D_LIBCPP_BUILDING_LIBRARY -DLIBCXX_BUILDING_LIBCXXABI \
    -Wno-macro-redefined"

# Only the 36 real, non-Windows/non-Solaris top-level src/*.cpp files
# CMakeLists.txt actually builds for this platform class -- explicitly
# NOT a glob over src/*.cpp (that would also sweep in filesystem/ and
# experimental/, out of scope per this file's header comment).
LIBCXX_SOURCES="algorithm any atomic barrier bind charconv chrono \
    condition_variable condition_variable_destructor debug exception \
    functional future hash ios iostream locale memory mutex \
    mutex_destructor new optional random random_shuffle regex \
    shared_mutex stdexcept string strstream system_error thread \
    typeinfo utility valarray variant vector"

ok=0
fail=0
for base in $LIBCXX_SOURCES; do
    f="$LIBCXX/src/${base}.cpp"
    out="$OUT/libcxx/${base}.o"
    if "$CLANGXX" $CXX_FLAGS "$f" -o "$out" 2>"$OUT/libcxx/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/libcxx/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/libcxx/${base}.err")"
    fi
done
echo "libcxx: $ok compiled, $fail gaps (see libc_build/libcxx/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

# Real gaps found linking the very first libcxx.a consumer that touches
# std::string (string.o's internal std::sto*/wide-string helpers) --
# bundled directly into this archive so every future consumer gets them,
# same shape as build_libpthread.sh bundling simple_cmds_mmap_compat.c
# or build_libiconv.sh bundling qsort.c/heapsort.c into its own archive:
#   - strtoll/strtoul/strtoull: real FreeBSD source (third_party/Libc/
#     stdlib/FreeBSD), needs the SAME xlocale-adjacent include set
#     build_libc_locale.sh already worked out for strtol.c (that file's
#     own strtol.o already ships in libc_locale.a -- not duplicated
#     here, only its 3 siblings that tier never attempted).
#   - wcstod/wcstof/wcstold: real Apple/FreeBSD source for these three
#     needs `_simple.h`, missing from this checkout entirely (same real,
#     already-documented gap build_libc_locale.sh's own skip list
#     records) -- libcxx_wcstof_compat.c is a real, narrowly-scoped
#     wrapper around the already-real gdtoa strtod/strtof/strtold
#     instead (see that file's header comment for why this is a correct
#     implementation, not a stand-in).
#   - dlopen/dlsym: libcxx_dlfcn_stub.c -- see that file's header
#     comment (real "not available" answer, this target has no dyld).
STRTOL_CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"
for f in strtoll strtoul strtoull; do
    "$CLANG" $STRTOL_CFLAGS "$LIBC/stdlib/FreeBSD/${f}.c" -o "$OUT/libcxx/${f}.o"
done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libcxx_wcstof_compat.c -o "$OUT/libcxx/wcstof_compat.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libcxx_dlfcn_stub.c -o "$OUT/libcxx/dlfcn_stub.o"

rm -f "$OUT/libcxx.a"
ar rcs "$OUT/libcxx.a" "$OUT"/libcxx/*.o
echo "wrote $OUT/libcxx.a"

# Real smoketest: exercise real STL containers/algorithms (std::vector,
# std::string, std::sort) plus a real std::shared_ptr (memory.cpp) --
# none of this needs live stack unwinding, consistent with this
# project's llvm_libcxxabi_smoketest.cpp precedent of keeping the hard
# pass/fail gate on functionality that doesn't depend on the still-open
# throw/catch gap (see that file's header comment / this project's
# Phase 6.5 memory).
NEED_OK=1
for a in libcxxabi libunwind libc_string libc_stdio libc_locale_for_stdio libc_gen libc_printf libpthread libclosure libmalloc libplatform; do
    [ -f "$OUT/$a.a" ] || NEED_OK=0
done
if [ "$NEED_OK" -eq 1 ]; then
    rm -f libcxx_smoketest.macho
    "$CLANGXX" $CXX_FLAGS -fexceptions -frtti -funwind-tables llvm_libcxx_smoketest.cpp -o "$OUT/libcxx_smoketest.o"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
        libcxx_tsd_shim_bootstrap_compat.c -o "$OUT/libcxx_tsd_shim_bootstrap_compat.o"
    # Real libpthread.a's own pthread_tsd.o is pulled into THIS
    # smoketest (unlike llvm_libcxxabi_smoketest.cpp, which never
    # touches pthread_key_create) because libc++abi's
    # cxa_exception_storage.o needs real pthread_key_create/
    # setspecific/getspecific the moment anything needs __cxa_get_globals
    # (RTTI/dynamic_cast machinery does) -- that collides with
    # libc_locale_for_stdio.a's own now-superseded pthread_tsd_shim.o
    # (same real, already-documented class of duplicate
    # pthread_tsd_platform_bootstrap.c's header comment describes,
    # already fixed once for libmalloc.a via `ar d`). Same fix, applied
    # to a LOCAL SCRATCH COPY only -- the shared, committed
    # libc_locale_for_stdio.a is never touched, since other already-
    # built consumers still need that member's pthread_key_init_np until
    # the broader tier reconciliation that header comment flags as
    # separate, larger follow-up work actually happens.
    # libcxx_tsd_shim_bootstrap_compat.o supplies back the one other
    # real symbol that member provided (see its own header comment).
    cp "$OUT/libc_locale_for_stdio.a" "$OUT/libcxx_locale_for_stdio_scratch.a"
    ar d "$OUT/libcxx_locale_for_stdio_scratch.a" pthread_tsd_shim.o
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
        -o libcxx_smoketest.macho \
        ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
        "$OUT/libcxx_smoketest.o" "$OUT/libcxx.a" "$OUT/libcxxabi.a" "$OUT/libunwind.a" \
        "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libcxx_locale_for_stdio_scratch.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" \
        "$OUT/libc_printf.a" "$OUT/libc_string.a" \
        "$OUT/libcxx_tsd_shim_bootstrap_compat.o" \
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
        -Wl,-alias,'_mmap$UNIX2003',_mmap \
        -Wl,-alias,'_munmap$UNIX2003',_munmap \
        -Wl,-alias,__pthread_cond_wait,_pthread_cond_wait \
        -Wl,-alias,_pthread_testcancel,'_pthread_testcancel$UNIX2003' \
        2>"$OUT/libcxx_smoketest_link.err" && echo "wrote libcxx_smoketest.macho" || echo "WARNING: smoketest link failed (libcxx.a itself is still built -- see $OUT/libcxx_smoketest_link.err)" >&2

    if [ -f libcxx_smoketest.macho ]; then
        otool -l libcxx_smoketest.macho | grep -q LC_LOAD_DYLIB && \
            echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
        if nm -u libcxx_smoketest.macho | grep -q .; then
            echo "WARNING: undefined symbols present:" >&2
            nm -u libcxx_smoketest.macho >&2
        fi
    else
        tail -60 "$OUT/libcxx_smoketest_link.err" >&2
    fi
else
    echo "skip: libcxx_smoketest.macho (needs libcxxabi/libunwind/libc_string/libc_stdio/libc_gen/libc_printf/libpthread/libclosure/libmalloc/libplatform .a -- run their build_*.sh first)"
fi
