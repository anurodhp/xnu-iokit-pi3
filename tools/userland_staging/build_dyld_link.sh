#!/bin/bash
# Links libc_build/dyld_core.a (build_dyld_core.sh) against this
# project's own curated libc++/libc++abi/libc/libpthread/libmalloc/
# libplatform stack into a real, complete, fully-static `dyld.macho` --
# the actual real dyld-940 binary, not a stub or smoketest. Phase 6.5
# Phase C (docs/daemon-roadmap.md).
#
# Real architectural finding this script embodies: real Apple dyld is
# deliberately self-sufficient (it bootstraps BEFORE any dylib including
# libSystem is mapped, so it can't depend on one) -- dyld_core.a's own
# real source already proves this directly: it references zero malloc/
# free/calloc/realloc symbols (Allocator.cpp is dyld's own real
# allocator) and glue.c provides dyld's own abort()/exit()/stdout/stderr/
# std::__throw_bad_alloc()/Block_object_assign()/dispose(). Linking
# dyld_core.a INTO THE SAME final binary as this project's general-
# purpose curated libc/libmalloc/libclosure stack (needed for real,
# unrelated reasons -- dyld genuinely does call real printf/pread/
# pwrite/statfs/etc that this project's curated stack already provides)
# therefore creates real, genuine duplicate-symbol collisions between
# dyld's own intentional self-sufficient copies and this project's
# shared ones -- not a bug in either, just two real, correct
# implementations of the same real routine both being visible in one
# link. Real fix, applied via `ld -r -unexported_symbols_list` on
# SCRATCH COPIES only (never the shared, committed archives -- other
# consumers still need the hidden symbols to stay globally visible):
# for the "generic runtime infra" class (abort/exit/stdio globals/
# Block_object_*/__throw_bad_alloc), dyld's own glue.o copies are hidden
# in favor of this project's already-established, shared curated stack;
# for the "real new Mach IPC dyld itself now needs" class (vm_allocate/
# vm_deallocate/vm_map/vm_protect/vm_remap/vm_purgable_control/vm_read,
# mach_port_deallocate/mach_port_mod_refs, abort/__assert_rtn again from
# a SECOND source, mach_port_insert_right/extract_right/unguard,
# vm_region_64), the OTHER, older/less-complete copies (libmalloc.a's
# hand-curated mach_vm.o, libmach_ipc.a's explicitly-documented "honest
# dead stub" mach_ipc_dead_stubs.o, libc_printf.a's libc_printf_mach_stub.o)
# are hidden instead, in favor of dyld_core.a's own freshly real,
# complete MIG-generated vm_mapUser.o/mach_portUser.o.
#
# Real small gaps closed here (found via real trial-link undefined-
# symbol tracking, same methodology as build_dyld_core.sh):
#   - real plain strstr()/strncmp() (third_party/libplatform/src/string/
#     generic/{strstr,strncmp}.c, real Apple source, compiled with
#     -DVARIANT_STATIC=1 -- that's the real Apple build flag gating the
#     public un-prefixed wrapper around _platform_strstr()/
#     _platform_strncmp(), confirmed by reading the file, not guessed).
#   - real sigprocmask (syscall #48, standard, non-NO_SYSCALL_STUB --
#     the real create-syscalls.pl generator emits the complete wrapper).
#   - voucher_mach_msg_set(): real Apple source
#     (libsyscall/wrappers/_libc_funcptr.c) delegates through a real
#     runtime-registered function-pointer table
#     (_libkernel_voucher_functions) that's NULL until libdispatch
#     registers real voucher callbacks -- this target has no libdispatch
#     at all, so the real fallback behavior (this exact function, when
#     the pointer is NULL) is already "return 0" -- replicated directly
#     rather than compiling that whole file (which pulls in a much
#     larger real libkernel-function-table bootstrap mechanism dyld
#     itself doesn't need, confirmed by attempting the real compile
#     first).
set -e
cd "$(dirname "$0")"

XNU="../../third_party/xnu-7195"
PLATFORM="../../third_party/libplatform"
OUT="libc_build"
SCRATCH="$OUT/dyld_link_scratch"
rm -rf "$SCRATCH"
mkdir -p "$SCRATCH"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/dyld_core.a" ]; then
    echo "error: $OUT/dyld_core.a not found -- run build_dyld_core.sh first" >&2
    exit 1
fi
for a in libcxx libcxxabi libunwind libc_gen libc_stdio libc_printf libc_string \
         libpthread libclosure libmalloc libplatform libcopyfile libremovefile \
         libopenpam libmach_ipc libresolv; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run its build_$a.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/libcxx_locale_for_stdio_scratch.a" ] || [ ! -f "$OUT/libcxx_tsd_shim_bootstrap_compat.o" ]; then
    echo "error: $OUT/libcxx_locale_for_stdio_scratch.a / libcxx_tsd_shim_bootstrap_compat.o not found -- run build_llvm_libcxx.sh first (same real pthread_tsd_shim.o collision fix reused here)" >&2
    exit 1
fi

RTLIB="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/12.0.0/lib/darwin/libclang_rt.ios.a"
if [ ! -f "$RTLIB" ]; then
    echo "error: $RTLIB not found (real LLVM compiler-rt, provides ___chkstk_darwin for large-stack-frame functions)" >&2
    exit 1
fi

# Real plain strstr()/strncmp() -- see this script's header comment.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -DPRIVATE=1 -DVARIANT_STATIC=1 \
    -I "$PLATFORM/private" -I "$PLATFORM/include" -I "$PLATFORM/internal" \
    "$PLATFORM/src/string/generic/strstr.c" -o "$SCRATCH/strstr.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -DPRIVATE=1 -DVARIANT_STATIC=1 \
    -I "$PLATFORM/private" -I "$PLATFORM/include" -I "$PLATFORM/internal" \
    "$PLATFORM/src/string/generic/strncmp.c" -o "$SCRATCH/strncmp.o"

# Real sigprocmask (syscall #48) -- generated the same way every other
# real syscall stub in this project is (create-syscalls.pl).
SYSGEN="$SCRATCH/syscall_gen"
rm -rf "$SYSGEN"; mkdir -p "$SYSGEN"
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" "$XNU/libsyscall/custom" "$XNU/libsyscall/Platforms" \
    iPhoneOS "$SYSGEN" >/dev/null
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    -I "$XNU/BUILD.hdrs/dst/usr/include" -I "$SYSGEN" \
    "$SYSGEN/_sigprocmask.s" -o "$SCRATCH/sigprocmask.o"

# Real voucher_mach_msg_set() fallback -- see this script's header comment.
cat > "$SCRATCH/voucher_compat.c" << 'EOF'
int voucher_mach_msg_set(void *msg) {
    (void)msg;
    return 0;
}
EOF
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c "$SCRATCH/voucher_compat.c" -o "$SCRATCH/voucher_compat.o"

# Real `ld -r -unexported_symbols_list` trims on SCRATCH COPIES of
# individual archive members (never the shared, committed archives) --
# see this script's header comment for which direction each collision
# is resolved and why.
mkdir -p "$SCRATCH/extracted"
(cd "$SCRATCH/extracted" && ar x "../../dyld_core.a" glue.o mach_portUser.o vm_mapUser.o)
(cd "$SCRATCH/extracted" && ar x "../../libmalloc.a" libmalloc_dispatch.o mach_vm.o)
(cd "$SCRATCH/extracted" && ar x "../../libc_printf.a" libc_printf_mach_stub.o)
(cd "$SCRATCH/extracted" && ar x "../../libmach_ipc.a" mach_ipc_dead_stubs.o)

cat > "$SCRATCH/unexp_glue.txt" << 'EOF'
__Block_object_assign
__Block_object_dispose
__ZSt17__throw_bad_allocv
___stderrp
___stdoutp
_exit
EOF
cat > "$SCRATCH/unexp_malloc_dispatch.txt" << 'EOF'
_abort
___assert_rtn
_mach_port_deallocate
_mach_port_mod_refs
EOF
cat > "$SCRATCH/unexp_printf_mach_stub.txt" << 'EOF'
_abort_report_np
_vm_region_64
EOF
cat > "$SCRATCH/unexp_mach_vm.txt" << 'EOF'
_vm_allocate
_vm_deallocate
_vm_map
_vm_protect
_vm_purgable_control
_vm_read
_vm_remap
EOF

"$LD" -r -arch arm64 -unexported_symbols_list "$SCRATCH/unexp_glue.txt" \
    "$SCRATCH/extracted/glue.o" -o "$SCRATCH/glue_trimmed.o"
"$LD" -r -arch arm64 -unexported_symbols_list "$SCRATCH/unexp_malloc_dispatch.txt" \
    "$SCRATCH/extracted/libmalloc_dispatch.o" -o "$SCRATCH/libmalloc_dispatch_trimmed.o"
"$LD" -r -arch arm64 -unexported_symbols_list "$SCRATCH/unexp_printf_mach_stub.txt" \
    "$SCRATCH/extracted/libc_printf_mach_stub.o" -o "$SCRATCH/libc_printf_mach_stub_trimmed.o"
"$LD" -r -arch arm64 -unexported_symbols_list "$SCRATCH/unexp_mach_vm.txt" \
    "$SCRATCH/extracted/mach_vm.o" -o "$SCRATCH/mach_vm_trimmed.o"

# Assemble the scratch archives: same real content as the shared,
# committed archives, with only the above members replaced/removed.
cp "$OUT/dyld_core.a" "$SCRATCH/dyld_core_scratch.a"
ar d "$SCRATCH/dyld_core_scratch.a" glue.o
ar r "$SCRATCH/dyld_core_scratch.a" "$SCRATCH/glue_trimmed.o"

cp "$OUT/libmalloc.a" "$SCRATCH/libmalloc_scratch.a"
ar d "$SCRATCH/libmalloc_scratch.a" libmalloc_dispatch.o mach_vm.o
ar r "$SCRATCH/libmalloc_scratch.a" "$SCRATCH/libmalloc_dispatch_trimmed.o" "$SCRATCH/mach_vm_trimmed.o"

cp "$OUT/libc_printf.a" "$SCRATCH/libc_printf_scratch.a"
ar d "$SCRATCH/libc_printf_scratch.a" libc_printf_mach_stub.o
ar r "$SCRATCH/libc_printf_scratch.a" "$SCRATCH/libc_printf_mach_stub_trimmed.o"

cp "$OUT/libmach_ipc.a" "$SCRATCH/libmach_ipc_scratch.a"
ar d "$SCRATCH/libmach_ipc_scratch.a" mach_ipc_dead_stubs.o

# Real, complete, fully-static link. Same real UNIX2003/INODE64/
# DARWIN_EXTSN alias set every other smoketest in this project already
# needs (real Apple header-driven symbol decoration, not guessed).
rm -f dyld.macho
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e __dyld_start \
    -o dyld.macho \
    "$SCRATCH/dyld_core_scratch.a" \
    "$SCRATCH/strstr.o" "$SCRATCH/strncmp.o" "$SCRATCH/sigprocmask.o" "$SCRATCH/voucher_compat.o" \
    "$OUT/libcxx.a" "$OUT/libcxxabi.a" "$OUT/libunwind.a" \
    "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libcxx_locale_for_stdio_scratch.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" \
    "$SCRATCH/libc_printf_scratch.a" "$OUT/libc_string.a" \
    "$OUT/libcxx_tsd_shim_bootstrap_compat.o" \
    "$OUT/libpthread.a" "$OUT/libclosure.a" "$SCRATCH/libmalloc_scratch.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
    "$OUT/libcopyfile.a" "$OUT/libremovefile.a" "$OUT/libopenpam.a" "$SCRATCH/libmach_ipc_scratch.a" "$OUT/libresolv.a" \
    "$RTLIB" \
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
    -Wl,-alias,_pthread_testcancel,'_pthread_testcancel$UNIX2003'

echo "wrote dyld.macho"
otool -l dyld.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l dyld.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u dyld.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u dyld.macho >&2
else
    echo "dyld.macho: fully static, zero undefined symbols"
fi
