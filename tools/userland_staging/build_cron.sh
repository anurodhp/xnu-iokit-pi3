#!/bin/bash
# Builds cron.macho and crontab.macho: real, unmodified Apple `cron`/
# `crontab` (third_party/cron, tag cron-40, matched via
# `git ls-tree macos-112 -- cron` against apple-oss-distributions/
# distribution-macOS, same method as every other Phase 4/5.5 pull -- see
# setup_third_party.sh's own header comment). Same Csu-static-link pattern
# as every other tier (no dyld, see architecture.md).
#
# One real source patch (third_party-patches/cron.patch, applied by
# setup_third_party.sh): do_command.c's IOKit/CoreFoundation battery-check
# and per-user-launchd-bootstrap-switch blocks needed headers
# (IOKit/*, CoreFoundation/*, vproc.h, vproc_priv.h, bootstrap_priv.h) that
# don't exist anywhere in this project (no CoreFoundation, no launchd,
# no battery on this board) -- real scope decisions, not gaps, see the
# patch's own commit message. Everything else in do_command.c, and every
# other file in this tree, is untouched real Apple source.
#
# Real gaps this pull needed that prior tiers already solved or newly
# solve here:
#   - get_compat.c (third_party/Libc/gen/get_compat.{h,c}) -- real,
#     already used by build_uname.sh, same reuse here (crontab.c's
#     COMPAT_MODE()).
#   - syslog(3)/openlog/closelog -- real Apple syslog(3) has no source
#     anywhere in this project's Libc pull (see
#     ifconfig_syslog_compat.c's own header comment); reusing that exact
#     file here too (its openlog/closelog/syslog/vsyslog are fully
#     generic, not ifconfig-specific) rather than duplicating it.
#     cron/config.h defines SYSLOG unconditionally (real Apple default,
#     confirmed by reading it -- the Makefile's own lack of -DSYSLOG is
#     not the real story), so this project's already-real syslog(3)
#     fallback makes every cron job start/end genuinely loggable instead
#     of the silent no-op it would otherwise be built with LOG_FILE also
#     unset (also real Apple's default, per pathnames.h).
#   - CC_MD5() (crontab.c's MD5File(), the legacy one-shot CommonDigest.h
#     wrapper, distinct from md5.c's CCDigest*() SPI usage) -- newly added
#     to md5_commoncrypto_shim.c, see that file's own header comment.
#   - libutil/pidfile.c (third_party/libutil, tag libutil-58.40.2) -- real
#     Apple source, newly pinned this pass (do_command.c/cron.c's
#     pidfile_open/_write/_close/_remove); only pidfile.c itself is
#     compiled, not the rest of libutil (ExtentManager/wipefs/tzlink/
#     realhostname/getmntopts are all unrelated to cron).
#   - daemon(3) (third_party/Libc/gen/FreeBSD/daemon.c) -- real source,
#     first real caller in this project. In practice cron.c's own call
#     site is a no-op here regardless (`getppid() != 1 && daemon(1,0)`
#     short-circuits since this project's custom PID-1 is always cron's
#     real parent, unlike real launchd-supervised macOS) but is still
#     compiled for real rather than stubbed, since it costs nothing and
#     keeps the source path faithful.
#
# LOGIN_CAP is NOT defined (matches real Apple's own Makefile, which
# leaves -DLOGIN_CAP commented out) -- no login.conf/login_cap subsystem
# exists in this project either way.
#
# crontab is installed setuid-root in real macOS (BINMODE=4555) so a
# non-root user can install their own crontab; this project has no
# setuid-helper story yet (docs/roadmap.md Phase 5's "setuid-root helper"
# item, still open) so crontab.macho ships without the setuid bit for
# now -- usable by root only until that lands, tracked there rather than
# invented here.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
LIBPLATFORM="../../third_party/libplatform"
XNU="../../third_party/xnu-7195"
CRON="../../third_party/cron"
LIBUTIL="../../third_party/libutil"
COMMONCRYPTO="../../third_party/CommonCrypto"
OUT="libc_build"
rm -rf "$OUT/cron_obj" "$OUT/crontab_obj"
mkdir -p "$OUT/cron_obj" "$OUT/crontab_obj" "$OUT/commoncrypto_glue/CommonCrypto"

cp "$COMMONCRYPTO/include/CommonDigest.h" "$OUT/commoncrypto_glue/CommonCrypto/CommonDigest.h"
cp "$COMMONCRYPTO/include/Private/CommonDigestSPI.h" "$OUT/commoncrypto_glue/CommonCrypto/CommonDigestSPI.h"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

for dep in libplatform.a libmalloc.a libpthread.a libclosure.a libc_gen.a libc_stdio.a libc_printf.a libc_string.a; do
    if [ ! -f "$OUT/$dep" ]; then
        echo "error: $OUT/$dep not found -- run its build_*.sh first" >&2
        exit 1
    fi
done

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -DPRIVATE \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $CRON/cron \
    -I $LIBUTIL \
    -I $OUT/commoncrypto_glue \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall \
    -idirafter $XNU/BUILD/obj/EXPORT_HDRS/osfmk"

ok=0
skip=0
for spec in \
    "$CRON/cron/cron.c:-Dmain=cron_cmd_real_main" \
    "$CRON/cron/database.c:" \
    "$CRON/cron/do_command.c:" \
    "$CRON/cron/job.c:" \
    "$CRON/cron/popen.c:" \
    "$CRON/cron/user.c:" \
    "$CRON/lib/compat.c:" \
    "$CRON/lib/entry.c:" \
    "$CRON/lib/env.c:" \
    "$CRON/lib/misc.c:" \
    "$LIBUTIL/pidfile.c:" \
    "cron_daemon_compat.c:" \
    "$LIBC/gen/get_compat.c:" \
    "$LIBC/gen/FreeBSD/err.c:" \
    "$LIBC/stdlib/FreeBSD/getopt.c:" \
    "$LIBC/stdlib/FreeBSD/atoi.c:" \
    "$LIBC/stdlib/FreeBSD/strtoul.c:" \
    "ifconfig_syslog_compat.c:" \
    "md5_commoncrypto_shim.c:" \
    "cron_extra_syscalls.c:" \
    "ps_passwd_compat.c:" \
    "../libinfo_custom/libinfo_custom.c:" \
    "ps_time_compat.c:" \
    "simple_cmds_getenv_compat.c:" \
    "md5_rfc1321_core.c:" \
    "$LIBPLATFORM/src/setjmp/generic/sigaction.c:" \
    "$LIBC/gen/FreeBSD/signal.c:" \
    "$LIBC/gen/FreeBSD/gethostname.c:" \
    "cron_main_wrapper.c:" \
    ; do
    f="${spec%%:*}"
    extra="${spec#*:}"
    base=$(basename "$f" .c)
    out="$OUT/cron_obj/${base}.o"
    if "$CLANG" $CFLAGS $extra "$f" -o "$out" 2>"$OUT/cron_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/cron_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/cron_obj/${base}.err" >&2
    fi
done
echo "cron (real Apple cron/*.c + lib/*.c): $ok compiled, $skip skipped"
if [ "$skip" -ne 0 ]; then
    echo "error: cron has real compile failures, not a clean tier" >&2
    exit 1
fi

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/cron_obj/cron_sched_yield_trap.o"
"$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OUT/cron_obj/cron_sched_yield.o"
# DAR-314: __locale_key/__global_locale and _DefaultRuneXLocale are LOCAL
# (lowercase `s` in nm) inside libsystem_c.dylib, not exports, so the
# dynamic link must provide them. Uses this project's already-proven
# shared compat file, not a narrower hand-written one (see DAR-169).
"$CLANG" $CFLAGS libc_locale_globals_compat.c -o "$OUT/cron_obj/libc_locale_globals_compat.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/table.c" -o "$OUT/cron_obj/table.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/none.c" -o "$OUT/cron_obj/none.o"

# Real gap, found 2026-09-13 via a live build_everything.sh run (first
# time cron.macho's real link with real libc_printf.a/libpthread.a was
# reached): two real issues.
#
# 1. ___vfprintf/___gdtoa_locks/_vfprintf_l undefined even though the
# providing objects (xprintf.o, gdtoa-misc.o, fprintf.o, printf.o) are
# all real members of libc_printf.a already on this link line -- real
# ld64 single-pass left-to-right archive resolution can't satisfy a
# member's own need for ANOTHER member of the SAME archive once other
# archives (libc_gen.a/libc_stdio.a) have already been scanned past it.
# Same real fix build_libc_printf.sh's own smoketest already established
# (libc_printf.a listed twice): list it again after libc_gen.a/
# libc_stdio.a.
#
# 2. _mmap undefined (libpthread.a's real pthread_create() needs it) --
# same NO_SYSCALL_STUB gap as build_libc_stdio.sh/build_libc_gen.sh/
# build_pthread_once_deep_repro.sh (mmap/munmap have no static provider
# since libSystem.B.dylib covers them dynamically only).
MACSDK="$(xcrun --sdk macosx --show-sdk-path 2>/dev/null)"
MMAP_TMPGEN="$(mktemp -d)"
trap 'rm -rf "$MMAP_TMPGEN"' EXIT
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" \
    "$XNU/libsyscall/custom" \
    "$XNU/libsyscall/Platforms" \
    iPhoneOS \
    "$MMAP_TMPGEN" >/dev/null
MMAP_SYSCFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -I $XNU/libsyscall/custom -I $XNU/libsyscall/mach -idirafter $XNU/osfmk -idirafter $MACSDK/usr/include"
"$CLANG" $MMAP_SYSCFLAGS "$MMAP_TMPGEN/___mmap.s" -o "$OUT/sys_mmap_for_cron.o"
"$CLANG" $MMAP_SYSCFLAGS "$MMAP_TMPGEN/___munmap.s" -o "$OUT/sys_munmap_for_cron.o"
rm -rf "$MMAP_TMPGEN"
trap - EXIT
# Real gap, found 2026-09-14: even listed twice, libc_printf.a's real
# ___gdtoa_locks (a genuine COMMON/tentative-definition symbol, defined
# in glue.o, confirmed via nm) still didn't resolve for gdtoa-misc.o's
# own real reference to it -- common symbols don't force archive-member
# pull-in the same way a strong "T"/"D" definition does, so a second
# textual mention of the same archive path doesn't guarantee ld64
# revisits glue.o. -Wl,-force_load,libc_printf.a was tried and reverted:
# it pulls in EVERY real object in the archive unconditionally, which
# drags in genuinely unwanted files this real Apple/FreeBSD tier
# deliberately never needs for cron (vfwscanf.o's wide-char scanf,
# xprintf_vis.o's vis(3) encoding, and machdep_ldisQ.o -- a real
# per-arch quad-precision long-double variant this script's own header
# comment already documents as excluded for arm64 via
# FreeBSD_INCLUDED_SOURCE_FILE_NAMES_arm64, confirmed real duplicate/
# incompatible with machdep_ldisd.o's own strtold), each surfacing its
# own new never-needed undefined symbols once force-loaded. Narrower,
# correct fix: extract just glue.o (genuinely small, self-contained,
# confirmed via nm to define only ___gdtoa_locks-class real gdtoa
# globals) and link it directly, forcing only the one real object this
# link actually needs.
mkdir -p "$OUT/cron_glue_extract"
(cd "$OUT/cron_glue_extract" && ar x "../libc_printf.a" glue.o)
# DYNAMIC LINK (DAR-314, 2026-09-15) -- see build_ifconfig.sh's own
# identically-structured block for the full reasoning. cron/crontab run
# long after dyld is up, so neither needs to be fully static.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi
# Real prerequisite check (2026-09-21) -- this DAR-314 dynamic-link block
# never checked its own LINK_DYLIBS existed before this, so a from-scratch
# build_everything.sh run hit a bare `clang: error: no such file or
# directory` pile for all six instead of one clear message, AND
# gen_build_order.py never learned this was a real dependency (only a
# `-f "$OUT/..." ... not found -- run build_X.sh first` idiom feeds it),
# so it silently ordered this script before the ones that produce these
# dylibs. See build_ifconfig.sh's identically-structured block for the
# same fix.
for d in libsystem_c libsystem_kernel libsystem_pthread libsystem_malloc libsystem_platform; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/system/libdyld.dylib" ]; then
    echo "error: $OUT/system/libdyld.dylib not found -- run build_libdyld.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libSystem.B.dylib" ]; then
    echo "error: $OUT/system/libSystem.B.dylib not found -- run build_libsystem.sh first" >&2
    exit 1
fi
# libsystem_kernel.dylib: owns mmap/munmap (and their $UNIX2003 names)
# since DAR-416 step 1b; before that libsystem_pthread.dylib exported them.
LINK_DYLIBS="$OUT/system/libsystem_c.dylib $OUT/system/libsystem_kernel.dylib $OUT/system/libsystem_pthread.dylib $OUT/system/libsystem_malloc.dylib $OUT/system/libsystem_platform.dylib $OUT/system/libdyld.dylib $OUT/system/libSystem.B.dylib"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o cron.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/cron_obj/*.o \
    $LINK_DYLIBS \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    `# These three alias cron's OWN locally-compiled wrappers, not dylib` \
    `# exports, so libSystem.B.dylib's baked-in UNIX2003 alias set does` \
    `# not cover them and they survive the DAR-314 conversion.` \
    -Wl,-alias,_pwrite,'_pwrite$UNIX2003' \
    -Wl,-alias,_wait,'_wait$UNIX2003'
echo "wrote cron.macho"

# Real dynamic-executable assertions (DAR-314) -- inverted, not deleted.
bad=0
otool -l cron.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: cron: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l cron.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: cron: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l cron.macho | grep -q "cmd LC_MAIN" || {
    echo "error: cron: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1

# --- crontab ---------------------------------------------------------

ok=0
skip=0
for spec in \
    "$CRON/crontab/crontab.c:-Dmain=crontab_cmd_real_main" \
    "$CRON/lib/compat.c:" \
    "$CRON/lib/entry.c:" \
    "$CRON/lib/env.c:" \
    "$CRON/lib/misc.c:" \
    "$LIBUTIL/pidfile.c:" \
    "$LIBC/gen/get_compat.c:" \
    "$LIBC/gen/FreeBSD/err.c:" \
    "$LIBC/stdlib/FreeBSD/getopt.c:" \
    "$LIBC/stdlib/FreeBSD/atoi.c:" \
    "$LIBC/stdlib/FreeBSD/strtoul.c:" \
    "ifconfig_syslog_compat.c:" \
    "md5_commoncrypto_shim.c:" \
    "cron_extra_syscalls.c:" \
    "crontab_extra_syscalls.c:" \
    "ps_passwd_compat.c:" \
    "../libinfo_custom/libinfo_custom.c:" \
    "ps_time_compat.c:" \
    "simple_cmds_getenv_compat.c:" \
    "md5_rfc1321_core.c:" \
    "$LIBC/gen/FreeBSD/signal.c:" \
    "$LIBPLATFORM/src/setjmp/generic/sigaction.c:" \
    "crontab_main_wrapper.c:" \
    ; do
    f="${spec%%:*}"
    extra="${spec#*:}"
    base=$(basename "$f" .c)
    out="$OUT/crontab_obj/${base}.o"
    if "$CLANG" $CFLAGS $extra "$f" -o "$out" 2>"$OUT/crontab_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/crontab_obj/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
        cat "$OUT/crontab_obj/${base}.err" >&2
    fi
done
echo "crontab (real Apple crontab/crontab.c + lib/*.c): $ok compiled, $skip skipped"
if [ "$skip" -ne 0 ]; then
    echo "error: crontab has real compile failures, not a clean tier" >&2
    exit 1
fi

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    libunwind_sched_yield.s -o "$OUT/crontab_obj/crontab_sched_yield_trap.o"
"$CLANG" $CFLAGS libunwind_sched_yield.c -o "$OUT/crontab_obj/crontab_sched_yield.o"
# DAR-314: __locale_key/__global_locale and _DefaultRuneXLocale are LOCAL
# (lowercase `s` in nm) inside libsystem_c.dylib, not exports, so the
# dynamic link must provide them. Uses this project's already-proven
# shared compat file, not a narrower hand-written one (see DAR-169).
"$CLANG" $CFLAGS libc_locale_globals_compat.c -o "$OUT/crontab_obj/libc_locale_globals_compat.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/table.c" -o "$OUT/crontab_obj/table.o"
"$CLANG" $CFLAGS "$LIBC/locale/FreeBSD/none.c" -o "$OUT/crontab_obj/none.o"

# Same real gaps as cron.macho's own link above (this file's own header
# comment has the full citation): libc_printf.a listed twice for its own
# internal cross-references. Real mmap()/munmap() are NOT needed here
# the way cron.macho's own link above needs them -- crontab_obj/*.o
# already includes crontab_extra_syscalls.o, which has its own real
# hand-written mmap()/munmap() (plain C, real syscall numbers #197/#73,
# both genuinely NO_SYSCALL_STUB in syscalls.master) predating this
# session's own work. Confirmed via a real duplicate-symbol link error
# when this session first tried adding the same raw-trap fix here too
# ("duplicate symbol '_mmap' in crontab_extra_syscalls.o and
# sys_mmap_for_crontab.o") -- crontab.macho was already correctly
# self-sufficient for this real gap; only cron.macho needed the fix.
# Same real glue.o fix as cron.macho's own link above (this file's own
# header comment has the full citation): ___gdtoa_locks is a real COMMON
# symbol in libc_printf.a(glue.o) that a second archive mention doesn't
# reliably re-pull-in.
mkdir -p "$OUT/cron_glue_extract"
(cd "$OUT/cron_glue_extract" && ar x "../libc_printf.a" glue.o)
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o crontab.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT"/crontab_obj/*.o \
    $LINK_DYLIBS \
    -Wl,-alias,__err,_err \
    -Wl,-alias,__warn,_warn \
    `# These three alias cron's OWN locally-compiled wrappers, not dylib` \
    `# exports, so libSystem.B.dylib's baked-in UNIX2003 alias set does` \
    `# not cover them and they survive the DAR-314 conversion.` \
    -Wl,-alias,_pwrite,'_pwrite$UNIX2003' \
    -Wl,-alias,_wait,'_wait$UNIX2003' \
    -Wl,-alias,_fchmod,'_fchmod$UNIX2003' \
    `# real libsystem_c.dylib exports _strftime$UNIX2003 but never the` \
    `# bare _strftime (confirmed nm -gU), so alias the versioned export` \
    `# to the plain name crontab's real Apple source calls -- same` \
    `# direction as the _getopt$UNIX2003 -> _getopt alias elsewhere.` \
    -Wl,-alias,'_strftime$UNIX2003',_strftime
echo "wrote crontab.macho"

# Real dynamic-executable assertions (DAR-314) -- inverted, not deleted.
bad=0
otool -l crontab.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: crontab: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l crontab.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: crontab: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l crontab.macho | grep -q "cmd LC_MAIN" || {
    echo "error: crontab: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
