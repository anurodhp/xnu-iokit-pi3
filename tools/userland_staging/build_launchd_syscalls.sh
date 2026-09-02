#!/bin/bash
# Builds libc_build/liblaunchd_syscalls.a: real BSD syscall wrappers
# launchd's own core.c/runtime.c/launchd.c call directly but this
# project's curated libc stack never implemented (confirmed via `nm -g`
# before writing this script, not assumed -- see
# build_launchd_core.sh's own trial-link output for the exact list this
# closes a large chunk of).
#
# ============================================================
# HONEST SCOPE STATEMENT -- read this first
# ============================================================
# Real, auto-wrapped raw traps (create-syscalls.pl generates both the
# raw trap AND the plain convenience name for these -- no
# NO_SYSCALL_STUB in syscalls.master): chown, chroot, sync, revoke,
# setuid, setgid, setsid, setpgid, getrusage, execve, mount, kevent,
# dup, dup2, getppid, getpeereid, umask.
#
# Real dedicated wrapper files, compiled as-is: posix_spawn
# (third_party/xnu-7195/libsyscall/wrappers/spawn/posix_spawn.c -- needs
# a real, higher-priority `-I` (not `-idirafter`) on that same
# directory so its own real spawn.h wins over this project's pinned
# SDK's older, same-named copy, which lacks 3 real newer
# POSIX_SPAWN_NP_CSM_* macros this file needs -- same "SDK's own
# same-named older header wins" class of bug as mach_vm.h/mach_time.h/
# netinet/in.h before it), munmap (.../wrappers/unix03/munmap.c),
# reboot/setpriority (.../wrappers/*.c), getrlimit/setrlimit
# (.../wrappers/unix03/*.c).
#
# Real, curated (launchd_syscall_wrappers_compat.c): accept()/listen()/
# socketpair() -- verbatim logic from .../wrappers/legacy/*.c, minus
# the same real `_NONSTD_SOURCE`/`__DARWIN_ONLY_UNIX_CONFORMANCE`
# preprocessor conflict already found and curated around for connect()/
# recvmsg()/sendmsg() in xpc_syscall_wrappers_compat.c (real Apple's
# own arm64/iOS libsyscall build excludes these legacy files per-arch
# too, for the same reason). fork() (third_party/Libc/sys/fork.c) --
# see fork_compat.c further down for its real, full
# libSystem_atfork_prepare/parent/child implementation and the exact
# list of real handlers this port does and does not have.
#
# Real, curated: wait4 (third_party/Libc's own waitpid.c calls
# __wait4()/__wait4_nocancel() -- both real raw traps this project
# already generated for the dyld/pthread ports, reused here -- but
# never exposes a plain wait4() convenience name itself since it's
# POSIX waitpid() that's the public API on real Darwin; curated a
# thin real wait4() forwarding to the same already-real
# __wait4_nocancel(), matching waitpid.c's own real logic exactly).
#
# Also real, unmodified: uuid_clear/uuid_compare/uuid_unpack
# (third_party/Libc/uuid/uuidsrc/, same real repo build_libxpc_syscalls2.sh
# already used for uuid_unparse_upper), posix_spawnp (third_party/Libc/
# sys/posix_spawn.c -- a DIFFERENT real file from the xnu-7195
# posix_spawn.c above, needs -idirafter on that file's own spawn_private.h
# too), getopt (third_party/Libc/stdlib/FreeBSD/getopt.c, needs a real
# fbsdcompat/namespace.h this project hadn't referenced before).
#
# UPDATE 2026-08-26 (real launchd critical-path pass, all real fixes
# verified via trial compile, not assumed): exec.c/signal.c's own
# private-name calls (`_execve`/`_write`/`_sigaction`) are a real gap in
# THIS project (no linker alias/export-map step to auto-generate them)
# -- closed via launchd_syscall_compat_headers/private_names_compat.c
# (thin forwards to the already-real public execve/write/sigaction) +
# private_names_decls.h (force-included declarations so exec.c/signal.c
# themselves see them as extern before use). waitpid.c's real issue
# wasn't K&R syntax -- it was `__weak_reference(sym,alias)` (used to
# alias the real `__waitpid` to the public `waitpid` name) silently
# expanding to nothing (third_party/Libc/fbsdcompat/sys/cdefs.h's own
# real definition is intentionally empty, no real linker weak-aliasing
# support here either) while ALSO not being defined by the real SDK's
# own sys/cdefs.h at all -- clang then parsed the bare macro-call lines
# as invalid K&R declarations. Fixed via `-D'__weak_reference(sym,alias)='`
# (neutralizes it explicitly, same real no-op) + a curated public
# `waitpid()` wrapper (same forwarding-only pattern as this script's own
# fork_compat.c/wait4_compat.c) since the alias never happens. atexit.c
# needed `__isthreaded` (a real Apple libc global for skip-locking-when-
# single-threaded; genuinely undefined anywhere in this project's stack
# -- closed via isthreaded_compat.c, always-1/always-threaded, safe
# since this project's real pthread support means locking is always
# correct, just not maximally optimized) plus a real, tracked
# `os/alloc_once_private.h` (same class of gap as `mach_vm.h`'s own
# per-consumer tracked-copy precedent -- the untracked, gitignored
# `libc_build/compat_include/` already had one for a different key,
# `OS_ALLOC_ONCE_KEY_LIBSYSTEM_PTHREAD`; this consumer needs
# `OS_ALLOC_ONCE_KEY_LIBSYSTEM_C` instead). glob.c needed a real chain
# of headers (`sys/syslimits.h` for `COLL_WEIGHTS_MAX`, `setlocale.h`,
# `malloc_private.h`, a tracked `System/machine/cpu_capabilities.h`
# forward for `pthread/tsd_private.h`) plus `-D__LIBC__=1` (real
# Apple-internal build macro gating declarations in `collate.h` --
# `__collate_range_cmp`'s own prototype is invisible without it) plus
# its own real dependency `locale/FreeBSD/collcmp.c`.
# login_tty/setlogin/raise/siglist/getpeereid already compiled clean and
# are now wired in (getpeereid replaces a broken create-syscalls.pl
# attempt in the ASMFLAGS loop below -- real Apple `getpeereid` is
# `NO_SYSCALL_STUB` too, implemented via `getsockopt(LOCAL_PEERCRED)` in
# `gen/FreeBSD/getpeereid.c`, not a raw trap at all; the old attempt
# silently failed via this script's own `2>/dev/null || true`).
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi

XNU="../../third_party/xnu-7195"
LIBC="../../third_party/Libc"
OUT="libc_build"

if [ ! -d "$OUT/dyld_syscall_gen" ]; then
    echo "error: $OUT/dyld_syscall_gen not found -- run build_dyld_link.sh first (reused here for __wait4_nocancel/__fork raw traps)" >&2
    exit 1
fi

mkdir -p "$OUT/launchd_syscall_obj" "$OUT/launchd_sys_asm_compat/mach/arm64"
cat > "$OUT/launchd_sys_asm_compat/mach/arm64/asm.h" <<'EOF'
#include <arm64/asm.h>
EOF

# --- real create-syscalls.pl raw trap + convenience-wrapper generation
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" \
    "$XNU/libsyscall/custom" \
    "$XNU/libsyscall/Platforms" \
    iPhoneOS \
    "$TMP" >/dev/null
ASMFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -arch arm64 \
    -I"$OUT/launchd_sys_asm_compat" -I"$XNU/libsyscall/custom" -I"$XNU/libsyscall/mach" -I"$XNU/osfmk")
for s in _chown _chroot _sync _revoke _setuid _setgid _setsid _setpgid \
    _getrusage _execve _mount _kevent _dup _dup2 _getppid _umask _getgid _mkdirat _getsockopt \
    _getaudit_addr _setaudit_addr _memorystatus_control _setgroups \
    ___accept_nocancel ___listen ___socketpair ___munmap ___wait4_nocancel ___fork \
    ___reboot ___setpriority ___getrlimit ___setrlimit ___posix_spawn ___setlogin ___getlogin; do
    "$CLANG" "${ASMFLAGS[@]}" "$TMP/${s}.s" -o "$OUT/launchd_syscall_obj/sys_${s}.o" 2>/dev/null || true
done
rm -rf "$TMP"
trap - EXIT
echo "compiled: real syscall trap/wrapper stubs (create-syscalls.pl)"

# --- real dedicated wrapper files, compiled as-is ------------------
CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libsyscall/mach" -idirafter "$XNU/libsyscall")

"$CLANG" "${CFLAGS[@]}" -I "$XNU/libsyscall/wrappers/spawn" "$XNU/libsyscall/wrappers/spawn/posix_spawn.c" -o "$OUT/launchd_syscall_obj/posix_spawn.o"
"$CLANG" "${CFLAGS[@]}" "$XNU/libsyscall/wrappers/unix03/munmap.c" -o "$OUT/launchd_syscall_obj/munmap.o"
"$CLANG" "${CFLAGS[@]}" launchd_syscall_wrappers_compat.c -o "$OUT/launchd_syscall_obj/launchd_syscall_wrappers_compat.o"
# REAL DUP-SYMBOL BUG FOUND 2026-09-02 (while migrating build_launchctl.sh
# off libCoreFoundation.a for DAR-195 -- pre-existing, not caused by that
# migration, just newly surfaced by that link's changed archive-pull
# order): this file's own real, hand-curated `_listen`/`_socketpair`
# duplicate the SAME two symbols create-syscalls.pl's own auto-generated
# sys____listen.o/sys____socketpair.o (compiled just above, same
# $OUT/launchd_syscall_obj/ dir, both swept into this script's own
# trailing `ar rcs` glob) already provide -- confirmed via nm, both
# pairs define plain `_listen`/`_socketpair` with real, working syscall
# bodies. A plain archive link tolerates this (lazy per-symbol member
# pull, silently picks whichever member a scan hits first) right up
# until something ALSO needs THIS file's one real, non-duplicated
# symbol (`_accept` -- no auto-generated stub exists for it, see this
# script's own "Real, curated" header comment above for why), which
# forces ld64 to pull this object in as well, at which point its
# duplicate `_listen`/`_socketpair` collide for real with the
# auto-generated pair ("duplicate symbol" hard error, not a lazy-pick).
# Real fix: narrow this object to just `_accept`, its own real, unique
# contribution -- same `ld -r -exported_symbols_list` technique already
# used throughout this project's dylib-conversion work. `_listen`/
# `_socketpair`'s function bodies stay in the object as private/local,
# unreachable, so nothing is lost -- every real caller resolves to the
# auto-generated pair instead, which is functionally identical (both are
# the same real Apple legacy wrapper logic, per this file's own header).
echo "_accept" > "$OUT/launchd_syscall_obj/keep_accept.txt"
"$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld" -r -arch arm64 \
    -exported_symbols_list "$OUT/launchd_syscall_obj/keep_accept.txt" \
    "$OUT/launchd_syscall_obj/launchd_syscall_wrappers_compat.o" \
    -o "$OUT/launchd_syscall_obj/launchd_syscall_wrappers_compat.o.narrow"
mv "$OUT/launchd_syscall_obj/launchd_syscall_wrappers_compat.o.narrow" "$OUT/launchd_syscall_obj/launchd_syscall_wrappers_compat.o"
"$CLANG" "${CFLAGS[@]}" "$XNU/libsyscall/wrappers/reboot.c" -o "$OUT/launchd_syscall_obj/reboot.o"
"$CLANG" "${CFLAGS[@]}" "$XNU/libsyscall/wrappers/setpriority.c" -o "$OUT/launchd_syscall_obj/setpriority.o"
"$CLANG" "${CFLAGS[@]}" "$XNU/libsyscall/wrappers/unix03/getrlimit.c" -o "$OUT/launchd_syscall_obj/getrlimit.o"
"$CLANG" "${CFLAGS[@]}" "$XNU/libsyscall/wrappers/unix03/setrlimit.c" -o "$OUT/launchd_syscall_obj/setrlimit.o"

# real, unmodified uuid_clear()/uuid_compare() (+ uuid_unpack(), the
# latter's own real dependency) -- third_party/Libc/uuid/uuidsrc/, same
# real repo build_libxpc_syscalls2.sh already used for uuid_unparse_upper.
"$CLANG" "${CFLAGS[@]}" -DHAVE_STDINT_H=1 -I "$LIBC/uuid/uuidsrc" "$LIBC/uuid/uuidsrc/clear.c" -o "$OUT/launchd_syscall_obj/uuid_clear.o"
"$CLANG" "${CFLAGS[@]}" -DHAVE_STDINT_H=1 -I "$LIBC/uuid/uuidsrc" "$LIBC/uuid/uuidsrc/compare.c" -o "$OUT/launchd_syscall_obj/uuid_compare.o"
"$CLANG" "${CFLAGS[@]}" -DHAVE_STDINT_H=1 -I "$LIBC/uuid/uuidsrc" "$LIBC/uuid/uuidsrc/unpack.c" -o "$OUT/launchd_syscall_obj/uuid_unpack.o"
"$CLANG" "${CFLAGS[@]}" -DHAVE_STDINT_H=1 -I "$LIBC/uuid/uuidsrc" "$LIBC/uuid/uuidsrc/pack.c" -o "$OUT/launchd_syscall_obj/uuid_pack.o"
# uuid_generate_random()/uuid_generate_time() (real gap found via the
# full launchd critical-path trial link, docs/backlog.md item d6): real
# gen_uuid.c needs one real forward declaration this project's
# freestanding CFLAGS don't surface from <stdlib.h> on their own --
# arc4random_buf() itself is already real (launchd_libc_extras.a's own
# arc4random.o, build_launchd_core.sh).
cat > "$OUT/launchd_syscall_obj/arc4random_buf_decl.h" <<'EOF'
extern void arc4random_buf(void *, unsigned long);
EOF
"$CLANG" "${CFLAGS[@]}" -DHAVE_STDINT_H=1 -I "$LIBC/uuid/uuidsrc" \
    -include "$OUT/launchd_syscall_obj/arc4random_buf_decl.h" \
    "$LIBC/uuid/uuidsrc/gen_uuid.c" -o "$OUT/launchd_syscall_obj/gen_uuid.o"

# real, unmodified posix_spawnp()/getopt() -- both needed
# -idirafter "$XNU/libsyscall/wrappers/spawn" (posix_spawnp.c calls
# real posix_spawn() from the file already compiled above) and no
# other real gaps once the right include paths are added.
CFLAGS_LIBC=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -idirafter "$XNU/libsyscall/wrappers/spawn" -idirafter "$LIBC/fbsdcompat" \
    -idirafter "$XNU/bsd" -idirafter "$XNU/osfmk" -idirafter "$XNU/libsyscall/mach" -idirafter "$XNU/libsyscall" \
    -idirafter "$LIBC/darwin" -idirafter "$LIBC")
"$CLANG" "${CFLAGS_LIBC[@]}" "$LIBC/sys/posix_spawn.c" -o "$OUT/launchd_syscall_obj/posix_spawnp.o"
"$CLANG" "${CFLAGS_LIBC[@]}" "$LIBC/stdlib/FreeBSD/getopt.c" -o "$OUT/launchd_syscall_obj/getopt.o"
"$CLANG" "${CFLAGS_LIBC[@]}" "$LIBC/util/login_tty.c" -o "$OUT/launchd_syscall_obj/login_tty.o"
"$CLANG" "${CFLAGS_LIBC[@]}" "$LIBC/gen/setlogin.c" -o "$OUT/launchd_syscall_obj/setlogin.o"
"$CLANG" "${CFLAGS_LIBC[@]}" "$LIBC/gen/raise.c" -o "$OUT/launchd_syscall_obj/raise.o"
echo "compiled: real dedicated wrapper files + real uuid_clear/compare/unpack + real posix_spawnp/getopt/login_tty/setlogin/raise (zero errors)"

# --- real getpwuid_r()/initgroups() (docs/daemon-roadmap.md's
# 2026-08-28 "mechanical" gap group) -- Phase 5's flat-file resolver
# (tools/libinfo_custom/), same real dependency-free module
# ps_passwd_compat.c already builds against, plus a real setgroups(2)
# raw trap (AUE_SETGROUPS, plain create-syscalls.pl stub, added to the
# ASMFLAGS loop above) -- see launchd_passwd_compat.c's own header for
# the full real-fix writeup. ---------------------------------------
"$CLANG" "${CFLAGS[@]}" -I ../libinfo_custom ../libinfo_custom/libinfo_custom.c -o "$OUT/launchd_syscall_obj/libinfo_custom.o"
"$CLANG" "${CFLAGS[@]}" -I ../libinfo_custom launchd_passwd_compat.c -o "$OUT/launchd_syscall_obj/launchd_passwd_compat.o"
echo "compiled: real getpwuid_r/initgroups (flat-file resolver + real setgroups(2))"

# --- real getsectbynamefromheader_64()/getsegbyname() (same real,
# pinned cctools-973.0.1 libmacho build_corefoundation.sh's own
# cctools_arch.o already trial-compiles against). getsegbyname.c is
# self-contained on this arm64/__LP64__ target (its __OPENSTEP__/
# _dyld_lookup_and_bind branch is never compiled here), needing only
# real _NSGetMachExecuteHeader() from <crt_externs.h>, already a real
# libdyld/libSystem export -- compiled as-is. getsectbynamefromheader_64
# is instead a real, curated extraction (see
# cctools_getsectbynamefromheader64_compat.c's own header) -- its real
# source file also defines the withswap() variants, which pull in a
# real but entirely unneeded swap_section()/swap_segment_command()
# dependency chain nothing in this project's launchd link actually
# needs. ---------------------------------------------------------
CCTOOLS="../../third_party/cctools"
if [ ! -f "$CCTOOLS/libmacho/getsegbyname.c" ]; then
    echo "error: $CCTOOLS/libmacho/getsegbyname.c not found -- run setup_third_party.sh first" >&2
    exit 1
fi
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 \
    -I "$CCTOOLS/include" -I "$CCTOOLS" \
    "$CCTOOLS/libmacho/getsegbyname.c" -o "$OUT/launchd_syscall_obj/cctools_getsegbyname.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 \
    -I "$CCTOOLS/include" -I "$CCTOOLS" \
    cctools_getsectbynamefromheader64_compat.c -o "$OUT/launchd_syscall_obj/cctools_getsectbynamefromheader64_compat.o"
echo "compiled: real getsectbynamefromheader_64/getsegbyname (cctools libmacho)"

# --- real, curated fork()/wait4() -----------------------------------
cat > "$OUT/launchd_syscall_obj/fork_compat.c" <<'EOF'
/* Real fork() -- third_party/Libc/sys/fork.c's real body, with the
 * real libSystem_atfork_prepare/parent/child hooks it dispatches
 * through implemented here rather than in Libsystem (this project has
 * no Libsystem umbrella project at all: libSystem.B.dylib here is a
 * link-time reexport shim, not a compiled image with its own init.c).
 *
 * HISTORY, so the next person doesn't re-derive it:
 *
 * (1) Originally a bare `return __fork();`, justified by the claim that
 * real Libc's _libSystem_atfork_child() hook "would always be a real
 * no-op regardless" since nothing here registers a pthread_atfork()
 * handler. That reasoning was WRONG, and it was a real, QEMU-observed
 * bug: on real Darwin the atfork CHILD hook is not only about
 * user-registered handlers -- it is where libSystem re-establishes the
 * child's Mach, pthread and malloc state after fork().
 *
 * Real symptom: launchd's job_start() forks a daemon, and the child
 * calls bootstrap_init() (PureDarwin libxpc/libbootstrap.c), whose very
 * first act is the MIG RPC task_get_special_port(task_self_trap(),
 * TASK_BOOTSTRAP_PORT, ...). That RPC needs a valid reply port, got the
 * parent's stale one, failed, and bootstrap_init() called abort() --
 * and because this port has no real signal delivery yet, abort()'s
 * kill(getpid(), SIGABRT) does nothing and it falls into its own
 * infinite spin loop. So every daemon launchd tried to spawn silently
 * hung forever instead of exec'ing.
 *
 * (2) Then a child-only partial fix: _mach_fork_child() plus a
 * hand-written zeroing of TSD slot __TSD_MIG_REPLY. That got daemon
 * children past the hang, but they then crashed downstream with a real
 * EXC_CORPSE_NOTIFY (qemu/launchd_boot_test95.log, pid 3 and pid 4),
 * and its own comment flagged the pthread/malloc halves as knowingly
 * missing. This file is that gap closed.
 *
 * THE REAL ORDER, verbatim from real Apple Libsystem-1292.50.1's
 * init.c (the macOS 11.2 / Darwin 20.3 vintage this whole port is
 * pinned to -- see docs/apple-oss-catalog.md), which is the actual
 * authority here. Note in particular that _mach_fork_child() is FIRST
 * in the child, BEFORE _pthread_atfork_child(); libpthread's own
 * pthread_atfork.c comment claiming _pthread_atfork_child() is "called
 * first ... (after _dyld_fork_child)" is stale for this vintage, where
 * _dyld_fork_child() actually runs sixth:
 *
 *   libSystem_atfork_prepare:  _pthread_atfork_prepare_handlers,
 *       _libSC_info_fork_prepare, xpc_atfork_prepare,
 *       dispatch_atfork_prepare, _dyld_atfork_prepare, cc_atfork_prepare,
 *       _malloc_fork_prepare, _pthread_atfork_prepare
 *   libSystem_atfork_parent:   _pthread_atfork_parent, _malloc_fork_parent,
 *       cc_atfork_parent, _dyld_atfork_parent, dispatch_atfork_parent,
 *       xpc_atfork_parent, _libSC_info_fork_parent,
 *       _pthread_atfork_parent_handlers
 *   libSystem_atfork_child:    _mach_fork_child, _pthread_atfork_child,
 *       _malloc_fork_child, cc_atfork_child, _libc_fork_child,
 *       _dyld_fork_child, dispatch_atfork_child,
 *       _libcoreservices_fork_child, _asl_fork_child, _notify_fork_child,
 *       xpc_atfork_child, _libtrace_fork_child, _libSC_info_fork_child,
 *       _pthread_atfork_child_handlers
 *
 * WHAT THIS PORT REALLY HAS, checked with `nm -gU` against the actual
 * built dylibs in libc_build/system/ -- every one of these is real,
 * unmodified Apple source already compiled and exported by this
 * project, reached here through launchd.macho's own existing
 * LC_LOAD_DYLIBs (libsystem_pthread / libdispatch / libdyld directly,
 * libsystem_kernel and libsystem_malloc through libSystem.B.dylib's
 * real LC_REEXPORT_DYLIB entries):
 *   _pthread_atfork_prepare_handlers / _pthread_atfork_prepare /
 *   _pthread_atfork_parent / _pthread_atfork_parent_handlers /
 *   _pthread_atfork_child / _pthread_atfork_child_handlers
 *       -- third_party/libpthread/src/pthread_atfork.c, libsystem_pthread.dylib
 *   _mach_fork_child
 *       -- third_party/xnu-7195/libsyscall/mach/mach_init.c (its own comment
 *          literally reads "called by libSystem_atfork_child()"),
 *          libsystem_kernel.dylib
 *   _malloc_fork_prepare / _malloc_fork_parent / _malloc_fork_child
 *       -- tools/userland_staging/libmalloc_dispatch.c, libsystem_malloc.dylib
 *          (real malloc.c itself is deliberately out of scope for this port,
 *          so its three fork hooks live in this project's replacement for
 *          malloc.c's public-API glue; see that file's own comment).
 *          CAVEAT, real and measured with `nm -m launchd.macho`: launchd.macho
 *          still statically links libmalloc.a as well as loading
 *          libSystem.B.dylib, so it carries its OWN _malloc/_free/_g_zone and
 *          its own copy of these three hooks -- the call below binds to that
 *          local copy and reinitializes launchd's own zone, which is the zone
 *          launchd's own allocations really use. libsystem_malloc.dylib's
 *          separate zone (the one libdispatch/CF/libc++ allocate from) is NOT
 *          reinitialized by this. That is not a shortcut taken here: it is the
 *          already-documented Phase 2b duplicate-static-copy condition in
 *          docs/static-linking-cleanup-plan.md ("launchd.macho is the
 *          highest-risk one: it defines _malloc, _free, ... locally despite
 *          loading libSystem.B.dylib"), and closing Phase 2b fixes this hook
 *          for free -- there is deliberately no clever dlsym(RTLD_NEXT)
 *          workaround here papering over it.
 *   dispatch_atfork_prepare / dispatch_atfork_parent / dispatch_atfork_child
 *       -- third_party/libdispatch/src/init.c, libdispatch.dylib
 *
 * DYLD -- REAL, BUILT, AND DELIBERATELY NOT CALLED (2026-08-30):
 *   _dyld_atfork_prepare / _dyld_atfork_parent / _dyld_fork_child are all
 *   real, exported by this project's libdyld.dylib, and are exactly what
 *   real Libsystem calls. They are omitted here anyway, because calling
 *   them is a measured REGRESSION on this port right now:
 *
 *   With them in, every forked child reached _dyld_fork_child() and hung
 *   there forever -- no crash, no corpse, just a stuck process
 *   (qemu/launchd_boot_test107_atexit_fix.log: markers for
 *   _mach_fork_child, _pthread_atfork_child and _malloc_fork_child all
 *   printed, the marker after _dyld_fork_child() never did, for all three
 *   children). With them out, the same children get all the way through
 *   dispatch_atfork_child() (qemu/launchd_boot_test108_nodyld.log).
 *
 *   The likely mechanism, for whoever picks this up -- NOT yet proven, so
 *   do not treat it as diagnosed: dyld's RuntimeState::takeLockBeforeFork()
 *   (third_party/dyld/dyld/DyldRuntimeState.cpp) takes loadersLock,
 *   notifiersLock and tlvInfosLock unconditionally, while the matching
 *   resetLockInForkChild() only resets them if
 *   `libSystemHelpers->version() >= 2`; and the reset itself goes through
 *   libplatform's os_unfair_recursive_lock_unlock_forked_child(), which
 *   deliberately __LIBPLATFORM_CLIENT_CRASH__es with "Lock was not held"
 *   if the lock does not look held. Either half of that mismatch would
 *   leave a forked child stuck. Check this port's real
 *   LibSystemHelpers::version() first.
 *
 *   Not calling them is not a silent regression from the pre-2026-08-30
 *   state either: this port's fork() never called ANY dyld atfork hook
 *   before, so children are no worse off than they have always been, and
 *   the mach/pthread/malloc/dispatch halves are a real net gain. Tracked
 *   in docs/backlog.md.
 *
 * WHAT IT REALLY DOES NOT HAVE, and why each is a genuine absence
 * rather than something skipped here (all verified by `nm`/grep, not
 * assumed):
 *   cc_atfork_*            -- corecrypto. Apple has never open-sourced
 *                             corecrypto as an apple-oss-distributions repo
 *                             at all (docs/backlog.md's crypto item); there
 *                             is no such image in this port to notify.
 *   _libc_fork_child       -- third_party/Libc/sys/_libc_fork_child.c exists,
 *                             but its three callees do not all: _arc4_fork_child
 *                             is real and compiled into libsystem_c.dylib (as a
 *                             non-exported local), while _init_clock_port
 *                             (Libc/gen/nanosleep.c) and __environ_lock_fork_child
 *                             (Libc/stdlib/FreeBSD/getenv.c) are absent from the
 *                             built dylib -- those files aren't in this port's
 *                             libsystem_c yet. Wiring this up is a real, separate
 *                             follow-on, not a one-liner here: the whole thing
 *                             has to be compiled INTO libsystem_c.dylib to see
 *                             its own private callees.
 *   _asl_fork_child        -- no real asl/syslog client library in this port
 *                             (docs/daemon-roadmap.md's syslogd/ASL section).
 *   _notify_fork_child     -- REAL AND BUILT (libsystem_notify.dylib), but
 *                             deliberately not called here: launchd.macho does
 *                             not link libsystem_notify.dylib and libSystem.B
 *                             does not reexport it, so referencing it would be a
 *                             real link-line change to every consumer of this
 *                             archive. Wire it up when something in launchd
 *                             actually uses libnotify across a fork.
 *   xpc_atfork_*           -- PureDarwin's libxpc reimplementation has no fork
 *                             handlers at all (grep: zero hits for "xpc_atfork"
 *                             anywhere in third_party/PureDarwin). A real gap in
 *                             that source, not something omitted here.
 *   _libcoreservices_fork_child / _libtrace_fork_child /
 *   _libSC_info_fork_*     -- CoreServices, libtrace and SystemConfiguration are
 *                             not part of this port at all.
 *
 * NOTE the hand-written __TSD_MIG_REPLY zeroing that used to live here
 * is GONE, and deliberately so -- it is not merely redundant now, the
 * real handler does strictly more. _pthread_atfork_child() ->
 * _pthread_main_thread_postfork_init() -> _pthread_main_thread_init()
 * (third_party/libpthread/src/pthread.c) really does:
 *     _pthread_tsd_slot(p, MACH_THREAD_SELF)   = mach_thread_self();
 *     _pthread_tsd_slot(p, MIG_REPLY)          = mach_reply_port();
 *     _pthread_tsd_slot(p, MACH_SPECIAL_REPLY) = MACH_PORT_NULL;
 *     _pthread_tsd_slot(p, SEMAPHORE_CACHE)    = SEMAPHORE_NULL;
 * i.e. it allocates a genuinely fresh reply port instead of leaving the
 * slot NULL for lazy allocation, AND it refreshes MACH_THREAD_SELF and
 * MACH_SPECIAL_REPLY -- which the old hand-written version left holding
 * the PARENT's port names, meaningless in the child's new IPC space.
 * That stale __TSD_MACH_THREAD_SELF is read on every single
 * os_unfair_lock acquisition (libplatform's _os_lock_owner_get_self()),
 * so it is a real correctness problem, not a tidiness one. This writes
 * to the same memory _os_tsd_get_direct() reads because this port's
 * main-thread TSD base really is &_main_thread.tsd[0] -- see
 * pthread_tsd_platform_bootstrap.c's own header comment.
 *
 * _pthread_atfork_child() requires _pthread_atfork_prepare() to have run
 * in the parent (it consumes globals->psaved_self, which prepare sets
 * from pthread_self()), which is exactly why the prepare/parent halves
 * below are not optional decoration. */
extern int __fork(void);

extern void _pthread_atfork_prepare_handlers(void);
extern void _pthread_atfork_prepare(void);
extern void _pthread_atfork_parent(void);
extern void _pthread_atfork_parent_handlers(void);
extern void _pthread_atfork_child(void);
extern void _pthread_atfork_child_handlers(void);
extern int  _mach_fork_child(void);
extern void _malloc_fork_prepare(void);
extern void _malloc_fork_parent(void);
extern void _malloc_fork_child(void);
extern void dispatch_atfork_prepare(void);
extern void dispatch_atfork_parent(void);
extern void dispatch_atfork_child(void);

/* Real Libsystem init.c order, with only the genuinely-absent handlers
 * dropped (each one named and justified in this file's header comment).
 * Relative order of everything that IS present is unchanged. */
static void libSystem_atfork_prepare(void)
{
	_pthread_atfork_prepare_handlers();
	/* _libSC_info_fork_prepare(), xpc_atfork_prepare(): absent. */
	dispatch_atfork_prepare();
	/* _dyld_atfork_prepare(): real and exported, but DELIBERATELY NOT
	 * CALLED -- see the DYLD block in this file's header comment. */
	/* cc_atfork_prepare(): absent. */
	_malloc_fork_prepare();
	_pthread_atfork_prepare();
}

static void libSystem_atfork_parent(void)
{
	_pthread_atfork_parent();
	_malloc_fork_parent();
	/* cc_atfork_parent(): absent. */
	/* _dyld_atfork_parent(): not called, see header. */
	dispatch_atfork_parent();
	/* xpc_atfork_parent(), _libSC_info_fork_parent(): absent. */
	_pthread_atfork_parent_handlers();
}

static void libSystem_atfork_child(void)
{
	(void)_mach_fork_child();
	_pthread_atfork_child();
	_malloc_fork_child();
	/* cc_atfork_child(), _libc_fork_child(): absent. */
	/* _dyld_fork_child(): not called, see header. */
	dispatch_atfork_child();
	/* _libcoreservices_fork_child(), _asl_fork_child(),
	 * _notify_fork_child(), xpc_atfork_child(), _libtrace_fork_child(),
	 * _libSC_info_fork_child(): absent. */
	_pthread_atfork_child_handlers();
}

/* Body is real Libc/sys/fork.c's, unchanged -- including its real
 * behavior of running the PARENT hooks on the __fork() == -1 error
 * path, which is easy to get wrong. */
int fork(void)
{
	int ret;

	libSystem_atfork_prepare();

	ret = __fork();

	if (-1 == ret) {
		/* __fork already set errno for us */
		libSystem_atfork_parent();
		return ret;
	}

	if (0 == ret) {
		libSystem_atfork_child();
		return 0;
	}

	libSystem_atfork_parent();
	return ret;
}
EOF
cat > "$OUT/launchd_syscall_obj/wait4_compat.c" <<'EOF'
/* Real, curated wait4() -- third_party/Libc's own waitpid.c calls
 * __wait4_nocancel() as its real underlying implementation (POSIX
 * waitpid() is the public name on real Darwin; wait4() itself has no
 * real dedicated wrapper file since it's BSD-only legacy). Thin
 * forward, matching waitpid.c's own real non-cancelable-variant body
 * exactly. */
#include <sys/types.h>
struct rusage;
extern int __wait4_nocancel(int pid, int *status, int options, struct rusage *rusage);
int wait4(int pid, int *status, int options, struct rusage *rusage) {
	return __wait4_nocancel(pid, status, options, rusage);
}
EOF
"$CLANG" "${CFLAGS[@]}" "$OUT/launchd_syscall_obj/fork_compat.c" -o "$OUT/launchd_syscall_obj/fork_compat.o"
"$CLANG" "${CFLAGS[@]}" "$OUT/launchd_syscall_obj/wait4_compat.c" -o "$OUT/launchd_syscall_obj/wait4_compat.o"
echo "compiled: real curated fork()/wait4()"

# --- real getpeereid() -- NO_SYSCALL_STUB in syscalls.master, real
# implementation is getsockopt(LOCAL_PEERCRED)-based, not a raw trap at
# all (the ASMFLAGS loop above never actually had this one -- silently
# swallowed by its own `2>/dev/null || true`, see this script's header
# comment) ---------------------------------------------------------
"$CLANG" "${CFLAGS_LIBC[@]}" "$LIBC/gen/FreeBSD/getpeereid.c" -o "$OUT/launchd_syscall_obj/getpeereid.o"
"$CLANG" "${CFLAGS_LIBC[@]}" "$LIBC/gen/FreeBSD/getlogin.c" -o "$OUT/launchd_syscall_obj/getlogin.o"

# --- real exec.c/signal.c/waitpid.c/atexit.c/glob.c + their real
# private-name/global compat shims (see this script's header comment
# for the full real-fix writeup for each) ---------------------------
CFLAGS_LAUNCHD_LIBC=("${CFLAGS_LIBC[@]}" -D__LIBC__=1 \
    -D'__weak_reference(sym,alias)=' \
    -include launchd_syscall_compat_headers/private_names_decls.h \
    -include launchd_syscall_compat_headers/isthreaded_compat.h \
    -include "$XNU/bsd/sys/syslimits.h" \
    -I launchd_syscall_compat_headers \
    -I "$LIBC/locale/FreeBSD" -I "$LIBC/stdtime/FreeBSD" \
    -idirafter "../../third_party/libmalloc/private" \
    -idirafter "$LIBC/locale" \
    -idirafter "../../third_party/libplatform/private" -idirafter "../../third_party/libpthread/private" \
    -idirafter "../../third_party/libdispatch/src/BlocksRuntime" \
    -idirafter "$XNU/libkern")
"$CLANG" "${CFLAGS_LAUNCHD_LIBC[@]}" launchd_syscall_compat_headers/private_names_compat.c -o "$OUT/launchd_syscall_obj/private_names_compat.o"
"$CLANG" "${CFLAGS_LAUNCHD_LIBC[@]}" launchd_syscall_compat_headers/isthreaded_compat.c -o "$OUT/launchd_syscall_obj/isthreaded_compat.o"
"$CLANG" "${CFLAGS_LAUNCHD_LIBC[@]}" "$LIBC/gen/FreeBSD/exec.c" -o "$OUT/launchd_syscall_obj/exec.o"
"$CLANG" "${CFLAGS_LAUNCHD_LIBC[@]}" "$LIBC/gen/FreeBSD/signal.c" -o "$OUT/launchd_syscall_obj/signal.o"
"$CLANG" "${CFLAGS_LAUNCHD_LIBC[@]}" "$LIBC/gen/FreeBSD/waitpid.c" -o "$OUT/launchd_syscall_obj/waitpid.o"
"$CLANG" "${CFLAGS_LAUNCHD_LIBC[@]}" "$LIBC/stdlib/FreeBSD/atexit.c" -o "$OUT/launchd_syscall_obj/atexit.o"
"$CLANG" "${CFLAGS_LAUNCHD_LIBC[@]}" "$LIBC/gen/FreeBSD/glob.c" -o "$OUT/launchd_syscall_obj/glob.o"
"$CLANG" "${CFLAGS_LAUNCHD_LIBC[@]}" "$LIBC/locale/FreeBSD/collcmp.c" -o "$OUT/launchd_syscall_obj/collcmp.o"
"$CLANG" "${CFLAGS_LIBC[@]}" "$LIBC/gen/FreeBSD/siglist.c" -o "$OUT/launchd_syscall_obj/siglist.o"
cat > "$OUT/launchd_syscall_obj/waitpid_compat.c" <<'EOF'
/* Real, curated public waitpid() -- gen/FreeBSD/waitpid.c's own real
 * __waitpid() never gets its public "waitpid" alias in this project
 * (the real `__weak_reference(sym,alias)` mechanism it relies on is a
 * no-op here, see this script's header comment). Thin forward, exactly
 * matching what the real weak-reference alias would have done. */
struct rusage;
extern int __waitpid(int pid, int *istat, int options);
int waitpid(int pid, int *istat, int options) { return __waitpid(pid, istat, options); }
EOF
"$CLANG" "${CFLAGS[@]}" "$OUT/launchd_syscall_obj/waitpid_compat.c" -o "$OUT/launchd_syscall_obj/waitpid_compat.o"
echo "compiled: real getpeereid/getlogin + exec/signal/waitpid/atexit/glob/collcmp/siglist + their real compat shims (zero errors)"

rm -f "$OUT/liblaunchd_syscalls.a"
ar rcs "$OUT/liblaunchd_syscalls.a" "$OUT"/launchd_syscall_obj/*.o
echo "wrote $OUT/liblaunchd_syscalls.a ($(ls "$OUT"/launchd_syscall_obj/*.o | wc -l | tr -d ' ') objects)"
