#!/bin/bash
# Builds libc_build/libbsm.a: real, unmodified-except-one-narrow-patch
# apple-oss-distributions/BSM (tag bsm-13, third_party/BSM) userland
# audit-record library -- the real bsm/libbsm.h/bsm/auditd_lib.h client
# API docs/daemon-roadmap.md's Phase 6.8 PureDarwin launchd/libxpc survey
# already flagged as a real, named, not-yet-built dependency
# (`bsm/auditd_lib.h`/`bsm/libbsm.h` -- BSM audit, not yet ported here"),
# pinned in setup_third_party.sh well before this pass but never built.
# Ported proactively, same "build it before something else needs it"
# spirit as build_libm.sh (2026-08-27) -- not wired into any consumer yet
# (no real auditd/notifyd exists on this target either), just made real
# and ready.
#
# Real per-file compile loop (build_libc_regex.sh's own template): all 11
# real files in third_party/BSM/bsm/lib/*.c (bsm_audit, bsm_class,
# bsm_control, bsm_event, bsm_flags, bsm_io, bsm_mask, bsm_notify,
# bsm_token, bsm_user, lib_wrappers -- BSM's own real Makefile's exact
# file list, matched against it directly, not guessed) compile clean
# against this project's pinned xnu-7195 + already-built libnotify_client.a
# stack. Zero files excluded -- every real gap found below was closed with
# either a real, narrowly-scoped compat header or (for bsm_token.c) a
# real, minimal patch to BSM's own source, not papered over.
#
# Real manifest ceiling (same honest-gap shape as Libm/IOUSBFamily, see
# docs/apple-oss-catalog.md): checked distribution-macOS's macos-112
# manifest directly (git ls-tree macos-112, full 232-entry tree) -- no
# entry named BSM or audit exists at all. bsm-13 is confirmed the newest
# real tag that exists (git ls-remote --tags apple-oss-distributions/BSM:
# bsm-13 > bsm-8 > bsm-2.11 > bsm-2.10), same "pin the newest real
# available source, document the ceiling honestly" call as Libm/
# IOUSBFamily -- not a fixable pin, a real absence in the manifest.
#
# Real, narrow compat gaps found (all in bsm_compat_headers/
# bsm_internal_compat.h, force-included -- read its own header comment for
# the full per-symbol writeup, not re-derived here):
#   - bsm_audit.c needs bsm/audit_internal.h (struct au_token/au_record,
#     real, present in this project's pinned xnu-7195) but never includes
#     it directly -- force-included.
#   - libbsm.h's own au_groups_t typedef uses a bare MAX_GROUPS that is
#     never defined ANYWHERE in this BSM-13 checkout (grepped) -- a real,
#     confirmed omission in Apple's own published header, not a project
#     gap. Aliased to the real, semantically-equivalent AUDIT_MAX_GROUPS
#     from xnu-7195's own bsd/bsm/audit_record.h.
#   - Real, confirmed naming drift between BSM-13's userland lib source
#     (HEADER_SIZE/TRAILER_SIZE, AU_*_TOKEN/AU_NULL/AUR_LONG naming) and
#     this project's paired xnu-7195 bsd/bsm/audit_internal.h+
#     audit_record.h (AUDIT_HEADER_SIZE/AUDIT_TRAILER_SIZE, AUT_*/AUR_INT32
#     naming) -- BSM-13's own lib source predates a later xnu-side rename
#     it was never resynced against. Full 1:1 real value mapping in the
#     compat header, each one cross-checked against audit_record.h's own
#     numeric value and call-site semantics, not guessed.
#   - be16enc/be32enc/be64enc (real, standard BSD <sys/endian.h>
#     big-endian-encode API, called by xnu-7195's own real
#     ADD_U_INT16/32/64 macros): not present anywhere in this project's
#     vendored Libc. A real, standard, unambiguous algorithm (no platform
#     variation), transcribed directly rather than pinning a whole extra
#     repo for 3 one-line functions.
#   - bsm_token.c needs sys/socket.h/netinet/in.h/netinet/ip.h (struct
#     in_addr/in6_addr/sockaddr_in/sockaddr_in6/ip, AF_INET6) without
#     including them itself -- same "relies on a transitive real-SDK
#     include order this project doesn't replicate" pattern as
#     audit_internal.h above.
#
# Real, narrow patch to BSM's OWN source (third_party-patches/BSM.patch,
# registered in setup_third_party.sh): xnu-7195's bsd/bsm/audit_record.h
# already declares a full, real "deprecated userland BSM API" prototype
# block (au_to_arg/au_to_data/au_to_groups/au_to_opaque/au_to_file/
# au_to_path/au_to_seq/au_to_exec_args/au_to_exec_env) that BSM-13's own
# bsm_token.c defines with slightly different (but real, compatible)
# signatures -- mostly a dropped/added `const` qualifier, `int *` vs
# `gid_t *` for au_to_groups (xnu's own header is arguably the buggy side
# here: the very next declaration, au_to_newgroups, correctly uses
# `gid_t *`), and one genuine behavioral difference (au_to_file gained an
# extra, real `struct timeval` parameter in the newer header that BSM-13's
# implementation predates -- the function already computes its own real
# timestamp internally via gettimeofday(), so the added parameter is
# accepted-and-ignored, not a lost feature). This is a real, confirmed
# cross-repo API-vintage skew between two real Apple sources (BSM's
# userland lib tag vs. xnu's published kernel-paired header), not
# something this project introduced -- fixed by aligning BSM-13's
# definitions to match xnu-7195's real, currently-declared contract
# (the header is the public interface; the .c file's definition must
# conform), same "real, confirmed bug in a pristine Apple snapshot,
# patched with a documented git-am patch" precedent as dyld.patch's
# ImagePair fix / ICU.patch's aaplbfct.cpp omission.
#
# Real new syscall wrappers (bsm_syscalls.c): audit(2)/auditon(2)/
# getaudit(2) (#350/#351/#355, cross-checked against xnu-7195's own
# bsd/kern/syscalls.master), none wrapped anywhere in this project before.
# Real, confirmed, NOT a project-specific gap: syscalls.master lists #355
# as `{ int nosys(void); } { old getaudit }` -- this exact xnu-7195 kernel
# has genuinely retired legacy getaudit(2) in favor of getaudit_addr(2)
# (#357, not wrapped -- no real BSM-13 lib source calls it), matching
# real, current Darwin's own actual behavior, not a simplification.
#
# Real dependency ordering: needs libnotify_client.a (bsm_notify.c's real
# notify_register_check/notify_check/notify_cancel calls) and libpthread.a
# already built.
#
# Built as a static archive (libbsm.a), not a .dylib, matching this
# project's still-dominant recent precedent for a curated support library
# linked INTO Csu-static consumers rather than loaded by dyld at runtime
# (build_libm.sh, build_libnotify_client.sh, build_libresolv.sh,
# build_openpam.sh are all .a, built after the project's real per-library-
# dylib convention was announced -- see CLAUDE.md's "Static-vs-dylib
# architecture debt" note) -- real Apple's own bsm/lib/Makefile actually
# targets a dylib (`libbsm.dylib`), so revisiting this as a real dylib is
# a reasonable, low-risk follow-up once a real dyld-based consumer
# (OpenSSH, launchd/libxpc) actually needs it, not done here since no
# such consumer exists yet.
set -e
cd "$(dirname "$0")"

BSM="../../third_party/BSM"
XNU="../../third_party/xnu-7195"
NOTIFY="../../third_party/PureDarwin/src/Libraries/XPC/notify"
PTHREAD="../../third_party/libpthread"
OUT="libc_build"
mkdir -p "$OUT/bsm_obj"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -d "$BSM/bsm/lib" ]; then
    echo "error: $BSM not found -- run setup_third_party.sh first (pins BSM bsm-13)" >&2
    exit 1
fi
for a in libnotify_client libpthread; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run build_${a}.sh first" >&2
        exit 1
    fi
done

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include bsm_compat_headers/bsm_internal_compat.h \
    -I $BSM/bsm/lib -I $NOTIFY \
    -idirafter $XNU/bsd -idirafter $XNU/osfmk -idirafter $XNU/libsyscall \
    -idirafter $PTHREAD/include -idirafter $PTHREAD/private"

ok=0
fail=0
for base in bsm_audit bsm_class bsm_control bsm_event bsm_flags bsm_io \
            bsm_mask bsm_notify bsm_token bsm_user lib_wrappers; do
    f="$BSM/bsm/lib/${base}.c"
    out="$OUT/bsm_obj/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/bsm_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/bsm_obj/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/bsm_obj/${base}.err")"
    fi
done

# This project's own new compat: real audit(2)/auditon(2)/getaudit(2)
# raw syscall wrappers (see this script's own header comment).
if "$CLANG" $CFLAGS bsm_syscalls.c -o "$OUT/bsm_obj/bsm_syscalls.o" 2>"$OUT/bsm_obj/bsm_syscalls.err"; then
    ok=$((ok + 1))
    rm -f "$OUT/bsm_obj/bsm_syscalls.err"
else
    fail=$((fail + 1))
    echo "  gap: bsm_syscalls -- $(grep -m1 'error:' "$OUT/bsm_obj/bsm_syscalls.err")"
fi

echo "libbsm: $ok compiled, $fail gaps (see libc_build/bsm_obj/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    echo "error: libbsm has real, unexpected compile failures (not one of the documented ones above) -- not a clean tier" >&2
    exit 1
fi

rm -f "$OUT/libbsm.a"
ar rcs "$OUT/libbsm.a" "$OUT"/bsm_obj/*.o
echo "wrote $OUT/libbsm.a"
nm -g "$OUT/libbsm.a" | awk '/:$/{next} NF==3 && $2=="T"{print $3}' | sort -u > "$OUT/libbsm_exports.txt"
echo "libbsm.a exports $(wc -l < "$OUT/libbsm_exports.txt" | tr -d ' ') real global functions (see libc_build/libbsm_exports.txt)"

# Real smoketest: real au_open/au_to_subject32/au_to_return32/au_to_text/
# au_write/au_close cycle, linked against Csu's real start.s/crt.c plus
# this project's existing minimal curated stack (same set
# build_libc_regex.sh's own smoketest uses) -- checked via `nm -u` for
# undefined symbols. Deliberately does NOT link libnotify_client.a/
# libxpc*/libdispatch_real.a/libmach_ipc.a (see bsm_smoketest.c's own
# header comment for why: this smoketest never calls bsm_notify.c's
# au_notify_initialize()/au_notify_terminate(), the only libbsm.a entry
# points that need them, so `ar`-based static linking never pulls that
# object in here). Degrades to a skip if a prerequisite archive isn't
# built yet.
NEED_OK=1
for a in libc_string libc_stdio libc_gen libc_printf libc_locale_for_stdio libpthread libclosure libunwind libmalloc libplatform libremovefile; do
    [ -f "$OUT/$a.a" ] || NEED_OK=0
done
if [ "$NEED_OK" -eq 1 ]; then
    rm -f bsm_smoketest.macho
    "$CLANG" $CFLAGS -I "$NOTIFY" bsm_smoketest.c -o "$OUT/bsm_obj/bsm_smoketest.o"
    # Smoketest-only compat (see bsm_smoketest_extra_syscalls.c's own
    # header comment): real au_to_me() -- bundled in the same object file
    # (bsm_token.o) as the au_to_subject32/au_to_return32/au_to_text
    # functions this smoketest actually calls -- needs geteuid/getuid/
    # gettimeofday (real, already provided by libremovefile.a, linked
    # below) and getgid (missing from every archive on this minimal link
    # line, provided by this smoketest-only file instead).
    "$CLANG" $CFLAGS bsm_smoketest_extra_syscalls.c -o "$OUT/bsm_obj/bsm_smoketest_extra_syscalls.o"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
        -o bsm_smoketest.macho \
        ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
        "$OUT/bsm_obj/bsm_smoketest.o" "$OUT/bsm_obj/bsm_smoketest_extra_syscalls.o" "$OUT/libbsm.a" \
        "$OUT/libremovefile.a" \
        "$OUT/libunwind.a" "$OUT/libpthread.a" "$OUT/libclosure.a" \
        "$OUT/libmalloc.a" "$OUT/libplatform.a" \
        "$OUT/libc_gen.a" "$OUT/libc_string.a" "$OUT/libc_stdio.a" "$OUT/libc_printf.a" \
        "$OUT/libc_locale_for_stdio.a" \
        -Wl,-alias,___mmap,_mmap \
        -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
        -Wl,-alias,___kill,'_kill$UNIX2003' \
        -Wl,-alias,___kill,_kill \
        -Wl,-alias,_write,'_write$UNIX2003' \
        -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
        -Wl,-alias,___kdebug_trace,_kdebug_trace \
        -Wl,-alias,_open,'_open$UNIX2003' \
        -Wl,-alias,_close,'_close$UNIX2003' \
        -Wl,-alias,_fcntl,'_fcntl$UNIX2003' \
        -Wl,-alias,_read,'_read$UNIX2003' \
        -Wl,-alias,_fstat,'_fstat$INODE64' \
        -Wl,-alias,_stat,'_stat$INODE64' \
        -Wl,-alias,_lstat,'_lstat$INODE64' \
        -Wl,-alias,_fopen,'_fopen$UNIX2003' \
        -Wl,-alias,_setenv,'_setenv$UNIX2003' \
        -Wl,-alias,_pthread_testcancel,'_pthread_testcancel$UNIX2003' \
        -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003',_pthread_rwlock_wrlock \
        -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
        -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
        2>"$OUT/bsm_smoketest_link.err" && echo "wrote bsm_smoketest.macho" || { echo "WARNING: smoketest link failed (libbsm.a itself is still built -- see $OUT/bsm_smoketest_link.err)" >&2; }

    if [ -f bsm_smoketest.macho ]; then
        otool -l bsm_smoketest.macho | grep -q LC_LOAD_DYLIB && \
            echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
        otool -l bsm_smoketest.macho | grep -q LC_UNIXTHREAD || \
            echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
        if nm -u bsm_smoketest.macho | grep -q .; then
            echo "WARNING: undefined symbols present:" >&2
            nm -u bsm_smoketest.macho >&2
        else
            echo "bsm_smoketest.macho: fully static, zero undefined symbols"
        fi
    else
        tail -60 "$OUT/bsm_smoketest_link.err" >&2
    fi
else
    echo "skip: bsm_smoketest.macho (needs libc_string/libc_stdio/libc_gen/libc_printf/libc_locale_for_stdio/libpthread/libclosure/libunwind/libmalloc/libplatform/libremovefile .a -- run their build_*.sh first)"
fi
