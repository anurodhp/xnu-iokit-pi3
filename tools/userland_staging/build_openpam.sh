#!/bin/bash
# Builds libc_build/libopenpam.a: real, unmodified Apple OpenPAM
# (third_party/OpenPAM, tag OpenPAM-28.40.1, pinned in
# setup_third_party.sh) -- specifically libpam itself (policy parsing,
# chain-walking, the pam_*/openpam_* public API surface), NOT the
# pam_deny/pam_permit sample modules or the "pam shim" legacy-API target
# (both separate Xcode targets in openpam.xcodeproj, out of scope here).
#
# Real file list confirmed two ways, not guessed: matches BOTH
# openpam/lib/Makefile.am's libpam_la_SOURCES AND (more authoritative for
# this exact Apple checkout) openpam.xcodeproj/project.pbxproj's real
# "Build Library" PBXNativeTarget (productName "pam shim", output
# libpam.2.dylib) Sources build phase -- the two lists are identical.
# That target's real GCC_PREPROCESSOR_DEFINITIONS
# (LIB_MAJ=2, OPENPAM_MODULES_DIR, _GNU_SOURCE) are reused verbatim below,
# not reinvented. Confirmed OPENPAM_STATIC_MODULES is NOT defined
# anywhere in that real build config -- so leaving it undefined here
# (see openpam_static.c's own note below) matches Apple's own real build,
# not a workaround.
#
# ============================================================================
# THE DLOPEN / MODULE-LOADING ARCHITECTURAL GAP -- read this before
# assuming anything about module loading here, and before extending this
# script to try to make dlopen "work":
#
# OpenPAM's real design loads PAM service modules (pam_unix.so,
# pam_deny.so, etc) at runtime via dlopen()/dlsym(). This project has NO
# dyld anywhere -- every binary is fully static (see CLAUDE.md,
# architecture.md) -- so dynamic module loading is structurally
# impossible here, not just unimplemented.
#
# The ENTIRE real dlopen/dlsym/dlerror/dlclose callsite surface lives in
# exactly one file, third_party/OpenPAM/openpam/lib/openpam_dynamic.c:
#   - dlopen():  lines 68, 74, 94
#   - dlsym():   line 123
#   - dlerror(): line 125
#   - dlclose(): line 132 (bail path) -- also referenced, for a
#     dynamically-loaded module's cleanup, at openpam_load.c:106
#     (openpam_release_module(), see below)
# That same file ALSO needs, just to reach those calls, two more real
# dyld/codesigning-runtime facilities this project has never had any
# reason to port and doesn't have here either:
#   - _dyld_shared_cache_contains_path() (line 67) -- dyld shared-cache
#     lookup, meaningless with no dyld.
#   - csops()/CS_OPS_CLEAR_LV/CS_REQUIRE_LV (lines 82-98) -- library-
#     validation-bypass dance, moot when dlopen() itself can never
#     succeed regardless of codesigning state.
# Rather than fabricate a fake dlopen() that pretends to succeed (which
# this project's own conventions explicitly reject -- see
# docs/roadmap.md's "Bash's own remaining honest failure stand-ins",
# which already documents dlopen/dlclose/dlsym/dlerror as permanent
# honest-failure stubs for this exact reason) or drag in dyld-shared-
# cache/GCD/codesigning support just to reach a call that would always
# fail anyway, openpam_dynamic.c is EXCLUDED from this build wholesale.
# In its place, openpam_dynamic_compat.c (new, real, ~30 lines) supplies
# the one real function openpam_load.c actually calls
# (`openpam_dynamic()`) as an honest "no dynamic module can ever be found
# here" -- see that file's own header comment for the full reasoning and
# for the real future-fix path (OPENPAM_STATIC_MODULES + a statically-
# linked pam_deny.a-equivalent, NOT a dlopen shim).
#
# openpam_static.c (real, unmodified, DOES compile below) is OpenPAM's
# OWN static-module mechanism (a linker-set of pam_module_t entries,
# populated at link time instead of via dlopen). UPDATE (DAR-339,
# 2026-09-21): this build now defines `-D__FreeBSD__` and
# `OPENPAM_STATIC_MODULES` compiles for real (openpam.h's own gate:
# `#if defined(__FreeBSD__)` leaves NO_STATIC_MODULES undefined, which
# is the ONLY thing that macro controls in this whole real library --
# confirmed via exhaustive grep, no other `__FreeBSD__`-gated behavior
# exists anywhere in OpenPAM's real source). Real FreeBSD's own
# <linker_set.h> (ELF `__start_set_X`/`__stop_set_X` boundary symbols)
# does NOT carry over to Mach-O -- confirmed via a real, standalone link
# test, not assumed -- so `su_compat_include/linker_set.h` (added to the
# -I path above) is a from-scratch, cited Mach-O-native replacement
# (getsectiondata()-based, see that header's own citation), not a port.
# `su_getsectiondata_cctools.c`/`su_linker_set_bounds.c` (compiled below,
# NOT su-specific despite the filename -- same "generic despite the
# prefix" pattern as this project's own chpass_flopen_freebsd.c) supply
# the real mechanism that header's macros call into. The actual real
# pam_unix-equivalent MODULE (su_pam_unix_static.c) is deliberately NOT
# compiled into this shared archive -- it's linked directly into su's
# own final binary instead (build_su.sh), matching real FreeBSD's own
# architecture where libpam stays policy-only and modules are separate
# translation units, not because it couldn't technically go here too.
#
# NET RESULT: real policy parsing (openpam_configure.c/openpam_load.c/
# openpam_readline.c), real chain construction and walking
# (openpam_dispatch.c), and the full real pam_*() public API all compile
# and link for real. Any attempt to actually load a service module
# (pam_authenticate() et al, once a chain has a real entry) will honestly
# fail at that one point, not silently succeed and not crash. See
# libopenpam_smoketest.c for a real, working end-to-end demonstration of
# exactly this: pam_start() genuinely parses (the total absence of) this
# image's /etc/pam.d, falls through to OpenPAM's own real
# "default to required pam_deny.so" logic (openpam_configure.c:336-359),
# genuinely fails to load pam_deny.so, and returns real PAM_SYSTEM_ERR --
# not a fabricated PAM_SUCCESS.
# ============================================================================
#
# Real syslog dependency: openpam_log.c (real, unmodified) calls
# vsyslog() unconditionally for every PAM_LOG_VERBOSE/NOTICE/ERROR
# message (and for PAM_LOG_LIBDEBUG ones too, once _openpam_debug is
# nonzero -- itself real, defined in openpam_log.c, toggled by real
# openpam_dispatch.c). Real, unmodified Libc/gen/oldsyslog.c (already
# proven to compile+link+run via build_getty.sh/build_simple_cmds.sh's
# date(1) entry -- reused verbatim here, not reintroduced) provides real
# syslog()/vsyslog()/openlog()/closelog()/setlogmask(); its own real
# socket()-based delivery to syslogd degrades to this project's already-
# existing honest ENOSYS socket()/connect() stand-ins
# (cat_socket_stub.c, likewise reused verbatim -- same file getty/cat/
# date already use, not a new stub invented for openpam) since no
# syslogd exists on this target -- oldsyslog.c's own real LOG_CONS
# fallback would write straight to /dev/console if any caller ever
# passed that flag to openlog(), but nothing in libpam does, so by
# default these messages are computed and then genuinely, harmlessly
# dropped (real, correct unconfigured-syslog(3) behavior, not a bug).
#
# vsyslog()'s own real body (Libc/gen/oldsyslog.c) unconditionally calls
# time()/localtime()/strftime() (to prefix the log line with a
# timestamp) and send()/writev() (its real syslogd-delivery/stderr-echo
# paths) regardless of whether any of that ever actually fires -- all
# real symbols the link needs regardless. date_compat.c (already
# existing, already used by build_getty.sh/build_date's own tier --
# reused verbatim, not reinvented; see its own header comment, written
# for this exact oldsyslog.c call chain already) supplies real
# send()/writev() honest-ENOSYS stand-ins (same "stub the symbol so it
# links, document why it can't fire" pattern as cat_socket_stub.c --
# both calls are only reached after a successful connect(), which never
# happens here) plus a real _NSGetArgv() (reads Csu/crt.c's already-real
# NXArgv global directly, no dyld indirection needed). The
# time()/localtime()/strftime() chain is the same real time-support
# bundle build_getty.sh's own EXTRA_FILES already proved compiles+links+
# runs (QEMU-verified there) -- reused verbatim rather than
# reintroduced: Libc/gen/FreeBSD/time.c, Libc/stdtime/FreeBSD/
# {localtime,asctime,strftime,timelocal}.c, Libc/stdtime/
# timezone_unix03.c, Libc/sys/gettimeofday.c,
# ps_commpage_gettimeofday_stub.c (honest "no commpage" fallback forcing
# the real syscall path), simple_cmds_time_compat.c (real 3-arg
# gettimeofday(2) raw syscall, the generated 2-arg stub is genuinely
# wrong per that file's own comment), ls_fenv_compat.c (real AArch64
# FPCR/FPSR fegetenv/fesetenv, needed by Libc/gen/FreeBSD/time.c's own
# Radar-3965505 floating-point-environment save/restore workaround).
# localtime.c's real tzload() (timezone-file lookup) also calls
# access(2) -- openpam_syscalls_compat.c (new, narrow, same real raw-
# syscall-asm pattern as date_compat.c/simple_cmds_time_compat.c above)
# provides it directly rather than depending on another tier's
# incidental build_simple_cmds.sh leftover .o (see that file's own
# header comment).
#
# NOT scoped/attempted here (leave for whoever wires OpenPAM into an
# actual login path):
#   - OPENPAM_STATIC_MODULES + a real statically-linked module (the real
#     fix for module loading, see openpam_dynamic_compat.c).
#   - openpam_borrow_cred.c/openpam_restore_cred.c's real
#     seteuid/setegid/setgroups/initgroups syscalls are NOT wired up
#     (this project's raw-syscall-stub generator, used by e.g.
#     build_bash.sh's SYS_STUB_NAMES, was not run for them) -- these two
#     files compile fine (the SDK's own real unistd.h/grp.h declare the
#     functions) and are archived into libopenpam.a like everything else,
#     but nothing in this smoketest calls them, so `ar`'s pull-only-what-
#     is-referenced semantics mean the link never needs those symbols to
#     exist yet. A real consumer that calls openpam_borrow_cred() would
#     need those syscall stubs generated first.
#   - Duplicate-symbol risk if this archive is ever linked alongside
#     getty.macho's or a simple_cmds consumer's own compiled-in
#     oldsyslog.c/cat_socket_stub.c/crt_externs.c objects (this script
#     bundles its own copies into libopenpam.a, per this project's
#     existing per-consumer-bundle convention -- there is no shared
#     "libc_syslog.a" tier yet) -- a real integration concern for later,
#     not addressed here (task scope: OpenPAM only, touch nothing else).
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
OPENPAM="../../third_party/OpenPAM/openpam"
OUT="libc_build"
# Clean, not incremental: an earlier draft of this script briefly
# compiled third_party/Libc/sys/crt_externs.c here before switching to
# date_compat.c's own _NSGetArgv() (see header comment); a stale
# crt_externs.o left over from that draft was silently swept into
# libopenpam.a by the `ar rcs ... *.o` glob below even after being
# dropped from the file list -- caught by `ar t`-inspecting the archive
# after the fact, not by any build failure (ld happened to never need to
# extract it, since date_compat.o's own _NSGetArgv satisfied the only
# reference first -- a real silent-no-op class of bug, see
# project_xnu_rpi3_toolchain_silent_no_ops). rm -rf up front (same
# precedent as build_getty.sh's own `rm -rf "$OUT/getty_obj"`) so the
# object directory only ever reflects this exact run's file list.
rm -rf "$OUT/openpam_obj"
mkdir -p "$OUT/openpam_obj"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
for a in libc_string libc_stdio libc_gen libc_printf libc_locale libplatform libmalloc libpthread libclosure libunwind; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run build_${a}.sh first" >&2
        exit 1
    fi
done

# Same real flags every other tier uses (see build_libc_locale.sh's own
# header comment for the full reasoning behind each one), plus this
# tier's own real GCC_PREPROCESSOR_DEFINITIONS
# (LIB_MAJ/OPENPAM_MODULES_DIR/_GNU_SOURCE, lifted verbatim from
# openpam.xcodeproj's real "Build Library" target, see header comment
# above) and -I paths for openpam's own include/ (security/openpam.h et
# al) and lib/ (openpam_impl.h, included by every .c file here).
#
# REAL GAP FOUND+FIXED: openpam_impl.h:120 (`struct pam_saved_cred`,
# `gid_t egid`/`gid_t groups[]`) needs gid_t, but neither
# security/pam_types.h (just <stddef.h>) nor security/openpam.h (just
# <stdarg.h>/openpam_attr.h/<Availability.h>) ever pull in
# <sys/types.h> themselves -- confirmed by grep, not guessed. Three
# files (openpam_configure.c/openpam_log.c/openpam_readline.c) don't
# happen to also directly include <unistd.h>/<pwd.h> the way
# openpam_borrow_cred.c does (which transitively brings sys/types.h in
# and masked the gap there), so only those three failed. Force-including
# the real, standard, unmodified <sys/types.h> (not a compat stub) below
# fixes it for all files uniformly.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h -include sys/types.h -include su_compat_include/pic_override.h \
    -DLIB_MAJ=2 -DOPENPAM_MODULES_DIR='\"/usr/lib/pam/;/usr/local/lib/pam/\"' -D_GNU_SOURCE \
    -D__FreeBSD__ \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I su_compat_include \
    -I $OPENPAM/include -I $OPENPAM/lib \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"

# Real libpam_la_SOURCES (see header comment for how this list was
# confirmed against both Makefile.am and the real Xcode target) MINUS
# openpam_dynamic.c (the real dlopen/dlsym/dlerror module loader --
# excluded, see header comment for exact line citations and the honest
# replacement in openpam_dynamic_compat.c). openpam_static.c IS included
# (real, unmodified) -- its body is conditionally empty without
# OPENPAM_STATIC_MODULES, which matches Apple's own real build.
OPENPAM_FILES="
openpam_borrow_cred.c
openpam_configure.c
openpam_dispatch.c
openpam_findenv.c
openpam_free_data.c
openpam_free_envlist.c
openpam_get_option.c
openpam_load.c
openpam_log.c
openpam_nullconv.c
openpam_readline.c
openpam_restore_cred.c
openpam_set_option.c
openpam_static.c
openpam_ttyconv.c
pam_acct_mgmt.c
pam_authenticate.c
pam_chauthtok.c
pam_close_session.c
pam_end.c
pam_error.c
pam_get_authtok.c
pam_get_data.c
pam_get_item.c
pam_get_user.c
pam_getenv.c
pam_getenvlist.c
pam_info.c
pam_open_session.c
pam_prompt.c
pam_putenv.c
pam_set_data.c
pam_set_item.c
pam_setcred.c
pam_setenv.c
pam_start.c
pam_strerror.c
pam_unsetenv.c
pam_verror.c
pam_vinfo.c
pam_vprompt.c
"

ok=0
fail=0
for f in $OPENPAM_FILES; do
    base=$(basename "$f" .c)
    out="$OUT/openpam_obj/${base}.o"
    if "$CLANG" $CFLAGS "$OPENPAM/lib/$f" -o "$out" 2>"$OUT/openpam_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/openpam_obj/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/openpam_obj/${base}.err")"
    fi
done

# This project's own honest replacement for openpam_dynamic() +
# dlclose() (see header comment + openpam_dynamic_compat.c's own header
# for the full reasoning).
if "$CLANG" $CFLAGS openpam_dynamic_compat.c -o "$OUT/openpam_obj/openpam_dynamic_compat.o" 2>"$OUT/openpam_obj/openpam_dynamic_compat.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/openpam_obj/openpam_dynamic_compat.err"
else
    fail=$((fail + 1))
    echo "  gap: openpam_dynamic_compat -- $(grep -m1 'error:' "$OUT/openpam_obj/openpam_dynamic_compat.err")"
fi

# Real Mach-O linker-set support openpam_static.c's own SET_DECLARE/
# SET_FOREACH now need (DAR-339, see header comment above).
for f in su_getsectiondata_cctools.c su_linker_set_bounds.c; do
    base=$(basename "$f" .c)
    if "$CLANG" $CFLAGS "$f" -o "$OUT/openpam_obj/${base}.o" 2>"$OUT/openpam_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/openpam_obj/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/openpam_obj/${base}.err")"
    fi
done

# Real syslog + time support openpam_log.c's vsyslog() call needs (see
# header comment) -- real, unmodified Apple source (plus this project's
# own already-existing, already-proven compat shims) reused verbatim
# from the getty/simple_cmds/date/cat tiers, not reintroduced.
for f in \
    "$LIBC/gen/oldsyslog.c" \
    "cat_socket_stub.c" \
    "date_compat.c" \
    "$LIBC/gen/FreeBSD/time.c" \
    "ls_fenv_compat.c" \
    "$LIBC/sys/gettimeofday.c" \
    "ps_commpage_gettimeofday_stub.c" \
    "simple_cmds_time_compat.c" \
    "$LIBC/stdtime/FreeBSD/localtime.c" \
    "$LIBC/stdtime/timezone_unix03.c" \
    "$LIBC/stdtime/FreeBSD/asctime.c" \
    "$LIBC/stdtime/FreeBSD/strftime.c" \
    "$LIBC/stdtime/FreeBSD/timelocal.c" \
    "openpam_syscalls_compat.c" \
    ; do
    base=$(basename "$f" .c)
    out="$OUT/openpam_obj/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/openpam_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/openpam_obj/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/openpam_obj/${base}.err")"
    fi
done

echo "libopenpam: $ok compiled, $fail gaps (see libc_build/openpam_obj/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f "$OUT/libopenpam.a"
ar rcs "$OUT/libopenpam.a" "$OUT"/openpam_obj/*.o
echo "wrote $OUT/libopenpam.a"

# Real smoketest: pam_start()/pam_end()/pam_strerror()/openpam_nullconv
# against this archive plus the same curated libc/libpthread/libplatform
# stack every other tier's smoketest links against (see
# libopenpam_smoketest.c's own header for exactly what real behavior this
# proves, including the real, honest PAM_SYSTEM_ERR outcome from
# OpenPAM's own real "default to required pam_deny.so" fallback genuinely
# failing to load a module). Degrades to a skip (not a hard failure) if a
# prerequisite archive isn't built yet, matching every other tier's
# from-scratch-checkout-ordering convention.
NEED_OK=1
for a in libc_string libc_stdio libc_gen libc_printf libc_locale libpthread libclosure libunwind libmalloc libplatform; do
    [ -f "$OUT/$a.a" ] || NEED_OK=0
done
if [ "$NEED_OK" -eq 1 ]; then
    rm -f libopenpam_smoketest.macho
    "$CLANG" $CFLAGS libopenpam_smoketest.c -o "$OUT/libopenpam_smoketest.o"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
        -o libopenpam_smoketest.macho \
        ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
        "$OUT/libopenpam_smoketest.o" "$OUT/libopenpam.a" \
        "$OUT/libc_locale.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libc_printf.a" \
        "$OUT/libc_string.a" "$OUT/libunwind.a" "$OUT/libpthread.a" "$OUT/libclosure.a" \
        "$OUT/libmalloc.a" "$OUT/libplatform.a" \
        -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
        -Wl,-alias,___kill,'_kill$UNIX2003' \
        -Wl,-alias,___kill,_kill \
        -Wl,-alias,_write,'_write$UNIX2003' \
        -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
        -Wl,-alias,___kdebug_trace,_kdebug_trace \
        -Wl,-alias,_pthread_testcancel,'_pthread_testcancel$UNIX2003' \
        -Wl,-alias,_setenv,'_setenv$UNIX2003' \
        -Wl,-alias,_fopen,'_fopen$UNIX2003' \
        -Wl,-alias,_open,'_open$UNIX2003' \
        -Wl,-alias,_close,'_close$UNIX2003' \
        -Wl,-alias,_fcntl,'_fcntl$UNIX2003' \
        -Wl,-alias,_read,'_read$UNIX2003' \
        -Wl,-alias,_connect,'_connect$UNIX2003' \
        -Wl,-alias,_send,'_send$UNIX2003' \
        -Wl,-alias,_writev,'_writev$UNIX2003' \
        -Wl,-alias,_strerror,'_strerror$UNIX2003' \
        -Wl,-alias,_fputs,'_fputs$UNIX2003' \
        -Wl,-alias,_fstat,'_fstat$INODE64' \
        -Wl,-alias,_stat,'_stat$INODE64' \
        -Wl,-alias,_lstat,'_lstat$INODE64' \
        -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003',_pthread_rwlock_wrlock \
        -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
        -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
        -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003','__pthread_rwlock_rdlock' \
        -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003','__pthread_rwlock_wrlock' \
        -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003','__pthread_rwlock_unlock' \
        2>"$OUT/libopenpam_smoketest_link.err" && echo "wrote libopenpam_smoketest.macho" || echo "WARNING: smoketest link failed (libopenpam.a itself is still built -- see $OUT/libopenpam_smoketest_link.err)" >&2

    if [ -f libopenpam_smoketest.macho ]; then
        otool -l libopenpam_smoketest.macho | grep -q LC_LOAD_DYLIB && \
            echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
        otool -l libopenpam_smoketest.macho | grep -q LC_UNIXTHREAD || \
            echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
        if nm -u libopenpam_smoketest.macho | grep -q .; then
            echo "WARNING: undefined symbols present:" >&2
            nm -u libopenpam_smoketest.macho >&2
        fi
    else
        tail -60 "$OUT/libopenpam_smoketest_link.err" >&2
    fi
else
    echo "skip: libopenpam_smoketest.macho (needs libc_string/libc_stdio/libc_gen/libc_printf/libc_locale/libpthread/libclosure/libunwind/libmalloc/libplatform .a -- run their build_*.sh first)"
fi
