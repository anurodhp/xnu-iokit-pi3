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
# for mach_port_deallocate/mach_port_mod_refs, abort/__assert_rtn again
# from a SECOND source, mach_port_insert_right/extract_right/unguard,
# vm_region_64: the OTHER, older/less-complete copies (libmach_ipc.a's
# explicitly-documented "honest dead stub" mach_ipc_dead_stubs.o,
# libc_printf.a's libc_printf_mach_stub.o) are hidden, in favor of
# dyld_core.a's own freshly real, complete MIG-generated mach_portUser.o.
#
# REAL BUG FOUND+FIXED 2026-08-25 (this session's third real dyld crash,
# after the TSD-bootstrap and rebase/fixup-chain fixes): an EARLIER pass
# of this exact script wrongly hid libmalloc.a's real mach_vm.o
# (vm_allocate/vm_deallocate/vm_map/vm_protect/vm_purgable_control/
# vm_read/vm_remap) in favor of dyld_core.a's own vm_mapUser.o copies of
# the SAME 7 names -- backwards. vm_mapUser.o's versions are generated
# straight from osfmk/mach/vm_map.defs's legacy MIG routines
# (KERNEL_SERVER_SUFFIX(vm_allocate) etc, msgh_id base 3800), and real,
# unmodified xnu-7195 NEVER registers that subsystem in its kobject
# dispatch table -- osfmk/kern/ipc_kobject.c only registers
# `mach_vm_subsystem` (from mach_vm.defs), never `vm_map_subsystem`.
# Confirmed by a temporary kernel diagnostic in the generated
# vm_map_server.c: any real vm_allocate() built from vm_mapUser.o gets a
# real MIG_BAD_ARGUMENTS (-304) bounce with the server's own per-routine
# check function never even reached (msgh_id 3801 has no registered
# handler at all) -- this is real, general Apple xnu behavior (the
# legacy vm_map.defs interface is vestigial on 64-bit-only modern
# Darwin), not a project-specific gap. libmalloc.a's mach_vm.o, compiled
# from the REAL libsyscall/mach/mach_vm.c (build_libmalloc.sh), is
# exactly Apple's own real fix for this: its `vm_allocate`/etc symbols
# are hand-written 32-bit-compat wrappers that call the fast
# `_kernelrpc_vm_allocate_trap`-style direct Mach trap first (real,
# always-registered kernel trap, no MIG round trip at all), falling back
# to MIG only via its own internal `mach_vm_allocate` path. Fixed by
# hiding vm_mapUser.o's 7 overlapping legacy names instead (dyld_core
# still needs vm_mapUser.o's OTHER real routines this session's earlier
# work relies on -- vm_copy, vm_region, vm_map_64, etc, none of which
# have a mach_vm.c fast-trap equivalent -- so the whole object stays,
# only these 7 conflicting names are unexported) and leaving libmalloc.a
# untouched.
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
(cd "$SCRATCH/extracted" && ar x "../../libmalloc.a" libmalloc_dispatch.o)
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
cat > "$SCRATCH/unexp_vm_mapuser.txt" << 'EOF'
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
"$LD" -r -arch arm64 -unexported_symbols_list "$SCRATCH/unexp_vm_mapuser.txt" \
    "$SCRATCH/extracted/vm_mapUser.o" -o "$SCRATCH/vm_mapUser_trimmed.o"

# Assemble the scratch archives: same real content as the shared,
# committed archives, with only the above members replaced/removed.
cp "$OUT/dyld_core.a" "$SCRATCH/dyld_core_scratch.a"
ar d "$SCRATCH/dyld_core_scratch.a" glue.o vm_mapUser.o
ar r "$SCRATCH/dyld_core_scratch.a" "$SCRATCH/glue_trimmed.o" "$SCRATCH/vm_mapUser_trimmed.o"

cp "$OUT/libmalloc.a" "$SCRATCH/libmalloc_scratch.a"
ar d "$SCRATCH/libmalloc_scratch.a" libmalloc_dispatch.o
ar r "$SCRATCH/libmalloc_scratch.a" "$SCRATCH/libmalloc_dispatch_trimmed.o"

# _os_crash: real gap, same as build_libsystem_malloc.sh's own (that
# script's os_crash_impl.c already exists on disk from its own real
# build -- see its header comment for the full why, __builtin_trap()
# body matching this project's fail-closed semantics). This archive's
# own chk_fail.o (a real member of libmalloc.a, carried over into this
# scratch copy) calls it with no provider. Recompile the same source
# directly here rather than depend on build_libsystem_malloc.sh having
# run first (same "recompile in the consumer" pattern already used
# elsewhere in this tree).
if [ ! -f "$OUT/libsystem_malloc/os_crash_impl.c" ]; then
    echo "error: $OUT/libsystem_malloc/os_crash_impl.c not found -- run build_libsystem_malloc.sh first (reused here)" >&2
    exit 1
fi
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    "$OUT/libsystem_malloc/os_crash_impl.c" -o "$SCRATCH/os_crash_impl.o"
ar r "$SCRATCH/libmalloc_scratch.a" "$SCRATCH/os_crash_impl.o"

cp "$OUT/libc_printf.a" "$SCRATCH/libc_printf_scratch.a"
ar d "$SCRATCH/libc_printf_scratch.a" libc_printf_mach_stub.o
ar r "$SCRATCH/libc_printf_scratch.a" "$SCRATCH/libc_printf_mach_stub_trimmed.o"

cp "$OUT/libmach_ipc.a" "$SCRATCH/libmach_ipc_scratch.a"
ar d "$SCRATCH/libmach_ipc_scratch.a" mach_ipc_dead_stubs.o

# dyld_core.a itself needs zero unwind symbols once compiled with real
# -fno-exceptions (confirmed via nm) -- but libcxx.a/libcxxabi.a
# themselves are real, general-purpose builds (shared with other
# consumers, compiled WITH exceptions enabled) whose own real code
# (vector::at()/string bounds checks, __cxa_throw, cxa_personality's
# real phase1/phase2 unwind search) still calls into libunwind's real
# C++ implementation (libunwind.o), which in turn needs the *_dyld_*
# callback APIs (__dyld_find_unwind_sections/
# __dyld_register_func_for_remove_image/dladdr) real Apple's
# libunwind_dyld_shim.o provides for a NORMAL process asking the REAL
# dyld where ITS OWN loaded images' unwind sections live. dyld asking
# itself this is circular and structurally wrong, and the real shim
# needs `__mh_execute_header`, a symbol ld64 only synthesizes for
# MH_EXECUTE outputs, never MH_DYLINKER (confirmed after fixing the
# MH_DYLINKER filetype bug above) -- so the real shim object is excluded
# (`ar d` on a scratch copy only) and replaced with real, honestly-
# scoped safe fallbacks (dyld_unwind_shim_compat.c below): dyld's own
# real code never throws (-fno-exceptions, confirmed via nm), so these
# three functions are structurally unreachable in practice on this
# target -- their real, documented "nothing found"/no-op behaviors are
# the correct real answer, not a stand-in for missing logic.
cp "$OUT/libunwind.a" "$SCRATCH/libunwind_scratch.a"
ar d "$SCRATCH/libunwind_scratch.a" libunwind_dyld_shim.o

cat > "$SCRATCH/dyld_unwind_shim_compat.c" << 'EOF'
#include <stddef.h>
int _dyld_find_unwind_sections(void *addr, void *info) {
    (void)addr; (void)info;
    return 0; /* real "no unwind info found" outcome, same as a real
               * process querying an address with none -- libunwind's
               * own phase1 search already handles this as "can't
               * unwind further", not corruption. */
}
void _dyld_register_func_for_remove_image(void (*func)(const void *, size_t)) {
    (void)func; /* real no-op: dyld itself never unloads images in a
                 * way that needs this real notification callback on
                 * this exceptions-disabled build. */
}
void *dladdr(const void *addr, void *info) {
    (void)addr; (void)info;
    return 0; /* real documented "not found" return value */
}
EOF
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c "$SCRATCH/dyld_unwind_shim_compat.c" -o "$SCRATCH/dyld_unwind_shim_compat.o"

# Real bug found+fixed 2026-08-25 (see dyld_tsd_entry_trampoline.s's own
# header comment for the full root-cause trace): dyld's own real entry
# (__dyld_start) never bootstraps TPIDRRO_EL0 itself -- on real Apple
# systems libSystem.dylib's initializer always already did that by the
# time dyld needs it. This project has no real libSystem.dylib yet, so
# dyld's first MIG-based Mach call (_mig_get_reply_port) dereferenced
# TPIDRRO_EL0+0x10 while it was still NULL -- real, deterministic
# EXC_BAD_ACCESS/KERN_INVALID_ADDRESS, subcode 0x10, confirmed via a
# temporary kernel diagnostic. Fixed with a new entry symbol
# (`-e __dyld_start_tsd_bootstrap` below) that does this project's
# existing, QEMU-verified platform_syscall TSD bootstrap
# (pthread_tsd_shim.c's mechanism) then branches into dyld's own real,
# untouched __dyld_start with SP undisturbed.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c "dyld_tsd_entry_trampoline.s" -o "$SCRATCH/dyld_tsd_entry_trampoline.o"

# Real, complete, fully-static link. Same real UNIX2003/INODE64/
# DARWIN_EXTSN alias set every other smoketest in this project already
# needs (real Apple header-driven symbol decoration, not guessed).
rm -f dyld.macho

# Real bug found+fixed 2026-08-25 (second real crash after the TSD fix
# above): dyld.macho linked with `-static` carries NO rebase/fixup load
# command at all (confirmed via otool -l -- no LC_DYLD_INFO_ONLY, no
# LC_DYLD_CHAINED_FIXUPS, no PIE flag in the mach_header). `-static` in
# ld64 means "assume this binary always runs at its link-time address,
# omit all rebase info" -- correct for this project's other, genuinely
# fixed-address-oblivious static binaries, but WRONG for dyld: the kernel
# still ASLR-slides dyld's mapped segments (mach_loader.c's
# aslr_dyld_page_offset, real Apple behavior, confirmed via a temporary
# diagnostic showing dyld's real slid load range), and with zero rebase
# info the kernel has nothing to walk to fix up dyld's own internal
# absolute-address globals (e.g. dyld4::halt()'s static error-info
# pointer) to match -- they're left holding their raw link-time values,
# causing a real, deterministic bad-pointer dereference the moment dyld
# touches one (confirmed: fault address matched a plain sub-1MB
# link-time address with zero slide applied, not a garbage/random value).
# Fix: drop `-static` (dyld has zero LC_LOAD_DYLIB dependents regardless,
# so this does not reintroduce any real dynamic-linking requirement) and
# add `-Wl,-fixup_chains`, matching real dyld-940's own actual build
# config (configs/dyld.xcconfig's OTHER_LDFLAGS) -- both together make
# ld64 emit real LC_DYLD_CHAINED_FIXUPS + a PIE mach_header flag so the
# kernel's existing, unmodified rebase-processing in parse_machfile()
# actually has real fixup chains to walk for this binary too.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -e __dyld_start_tsd_bootstrap \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-dylinker -Wl,-dylinker_install_name,/usr/lib/dyld \
    -o dyld.macho \
    "$SCRATCH/dyld_core_scratch.a" \
    "$SCRATCH/strstr.o" "$SCRATCH/strncmp.o" "$SCRATCH/sigprocmask.o" "$SCRATCH/voucher_compat.o" \
    "$SCRATCH/dyld_unwind_shim_compat.o" "$SCRATCH/dyld_tsd_entry_trampoline.o" \
    "$OUT/libcxx.a" "$OUT/libcxxabi.a" "$SCRATCH/libunwind_scratch.a" \
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
    -Wl,-alias,_close,'_close$UNIX2003' \
    -Wl,-alias,_fcntl,'_fcntl$UNIX2003' \
    -Wl,-alias,_fstat,'_fstat$INODE64' \
    -Wl,-alias,_lstat,'_lstat$INODE64' \
    -Wl,-alias,_stat,'_stat$INODE64' \
    -Wl,-alias,_fsync,'_fsync$UNIX2003' \
    -Wl,-alias,_reallocarray,'_reallocarray$DARWIN_EXTSN' \
    -Wl,-alias,'_mmap$UNIX2003',_mmap \
    -Wl,-alias,'_munmap$UNIX2003',_munmap \
    -Wl,-alias,__pthread_cond_wait,_pthread_cond_wait \
    -Wl,-alias,_pthread_testcancel,'_pthread_testcancel$UNIX2003'
    # _open/_read aliases removed 2026-08-28 (docs/backlog.md item d9's
    # NEWLD_BINDIR sweep): this link's own libresolv.a already defines
    # real _open$UNIX2003/_read$UNIX2003 directly (libresolv_arc4random_
    # deps_compat.o) -- the alias directive tried to create the exact
    # same symbol names from a different base, a real duplicate-symbol
    # conflict Xcode 12's ld64-609 silently tolerated (picking one
    # definition without complaint) but the daily-driver Xcode's newer
    # ld correctly rejects. __pthread_cond_wait KEPT here (unlike the
    # matching removal in build_libsystem_c.sh/build_libdyld.sh/
    # build_libsystem.sh) -- this script links libpthread.a directly
    # (fully static dyld.macho, no libsystem_pthread.dylib sibling at
    # all), so the raw __pthread_cond_wait base genuinely is local here;
    # removing it broke a real libcxxabi.a reference
    # (std::__1::__libcpp_condvar_wait) that only this script's own
    # object set can satisfy.

echo "wrote dyld.macho"
otool -hv dyld.macho | grep -q " DYLINKER " || \
    echo "WARNING: filetype is not MH_DYLINKER -- mach_loader.c's parse_machfile()/load_dylinker() branch on this real filetype in several places (e.g. note_all_image_info_section() is skipped entirely for non-dylinker files), a real, confirmed root cause of a 2026-08-25 boot hang/kill -- see docs/daemon-roadmap.md" >&2
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
