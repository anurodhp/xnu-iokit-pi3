#!/bin/bash
# Builds libc_build/system/libopenpam.dylib: static-to-dylib migration,
# docs/backlog.md item d8 -- BASE LIBRARY ONLY. Real, honestly-scoped
# exclusion, not attempted here: real per-module PAM dylibs (each PAM
# module is independently dlopen'd on real macOS -- this project has no
# dlopen at all, same documented architectural gap class as OpenPAM's
# own module loader elsewhere in this project's history). This script
# only converts the base `libopenpam.a` (openpam_*.c -- the
# configuration/chain/policy engine itself) to a real dylib; the
# module-loading question is a separate, deeper architectural gap this
# pass does not close.
#
# Real install_name: /usr/lib/libopenpam.dylib -- no real per-version
# infix confirmed from this checkout's own upstream build config (unlike
# libbsm/libncurses, no explicit `-install_name`/version macro found in
# third_party's own OpenPAM build files); using the plain, unversioned
# real macOS path this library actually ships at.
#
# Real, unmodified source: reuses every already-built member object out
# of libc_build/libopenpam.a (ar x, not recompiled -- see build_openpam.sh's
# own header for the full scope), plus real, cheap, already-compiled
# objects a real dylib link surfaces as genuine external references this
# archive's own narrower smoketest never needed (found via the same real
# nm-based intra-archive resolution pass build_libresolv_dylib.sh's
# header documents -- not guessed):
#   - geteuid.o (syslogd_libc_extras.a), pthread_getegid.o (libpthread.a):
#     real, cheap, already-verified-safe (only dep is _cerror_nocancel,
#     resolved via the libsystem_kernel.dylib sibling link below) -- same
#     precedent build_bsm_dylib.sh already established for geteuid.o.
#   - sys__setgroups.o (liblaunchd_syscalls.a): real setgroups(2)
#     wrapper, only dep _cerror_nocancel.
#   - __global_locale/__locale_key: SAME real gap + SAME real fix as
#     build_libresolv_dylib.sh's own pilot (libc_locale_globals_compat.c
#     + table.o/none.o, real local definitions -- `-Wl,-U` cannot defer
#     these, real arm64 adrp/lo12 direct data-address fixup).
#
# Real, honestly-classified `-Wl,-U` deferrals:
#   - alarm, NXArgv, setegid, seteuid, getgroups: real, confirmed
#     genuinely absent from every archive in this checkout (`nm -g` swept
#     across every .a in libc_build/ -- zero hits for any of these five)
#     -- not a resolv/bsm-specific gap, project-wide.
#   - __ldpart_free_extra/__part_load_locale: real LC_TIME loader
#     (locale/FreeBSD/ldpart.c) -- tried compiling it in for real first
#     (docs/backlog.md item d9 reports it compiling clean elsewhere), but
#     its own __part_load_locale calls a real, deeper helper
#     (__open_path_locale) that's genuinely absent from this checkout's
#     entire third_party/Libc (confirmed via `find`, not guessed) -- the
#     same deeper LC_TIME/locale-path-search wall d9's own writeup
#     already flags for a different command set. Reverted the ldpart.c
#     addition rather than chase that wall here; deferred instead.
#   - sigaction: real source for a raw wrapper exists
#     (Libc_sys_sigaction.o, syslogd_libc_extras.a) but it calls a real
#     `__platform_sigaction` (third_party/libplatform/src/setjmp/generic/
#     sigaction.c) not yet compiled into libsystem_platform.dylib --
#     closing this for real means editing that SHARED dylib's own build
#     script, which belongs to this migration's later Stage B, not this
#     single-library conversion. Deferred here instead of a partial,
#     broken pull-in.
#   - initgroups: NO LONGER DEFERRED (2026-09-01, DAR-153) -- real
#     source (launchd_passwd_compat.o's initgroups(), same real flat-file
#     passwd/group database loader this project wrote for launchd) is
#     now compiled fresh into this dylib, narrow-exported to just
#     `_initgroups` (getpwuid_r() from that same file stays private,
#     see the compile step's own comment below). Found as a real,
#     genuine next abort site on a real QEMU boot once alarm/setegid/
#     seteuid/getgroups (earlier in this dylib's own fixup chain) were
#     closed -- not spun up speculatively.
#   - connect$UNIX2003/send$UNIX2003/writev$UNIX2003: were a real,
#     project-wide BSD socket syscall gap when this `-Wl,-U` deferral was
#     first written -- NO LONGER a flat-namespace gap as of 2026-08-31
#     (commit 6ba2b5e, "BSD socket family consolidated into
#     libsystem_kernel.dylib"): real `$UNIX2003`-suffixed wrappers now
#     live in libsystem_kernel.dylib, already a linked sibling below, and
#     `xcrun dyld_info -fixups` on this dylib's own built output confirms
#     `_connect$UNIX2003` binds there for real (not flat-namespace).
#     `-Wl,-U` is kept here only because it is harmless once a real
#     provider exists (ld64 still prefers the real bind over the
#     deferral) -- not because the gap is still open. See the `_socket`
#     comment further down for why this dylib nonetheless keeps its OWN
#     separate, private `connect`/`socket`/`send`/`writev` stubs rather
#     than using libsystem_kernel.dylib's now-real implementations.
#
# `_dlclose` DUPLICATE-SYMBOL FIX (2026-08-31, static-linking cleanup,
# DAR-8): this dylib now also links libdyld.dylib as a sibling (added to
# both the dependency check and the final link line below) purely to
# obtain the real `_dlclose` real dyld's DyldAPIs.cpp implements, in
# place of openpam_dynamic_compat.c's own honest-but-now-redundant stub
# (that file's own dlclose() body is genuinely unreachable dead code --
# see its header -- so this is a pure single-owner cleanup, not a
# behavior change). No link cycle: libdyld.dylib does not depend on
# libopenpam.dylib, and this dylib's one real consumer (syslogd.macho)
# already loads libdyld.dylib directly regardless, so no new dylib
# enters any process's load set. See openpam_dynamic_compat.c's own
# "DYLIB BUILD" comment and this script's OPENPAM_DYNAMIC_COMPAT_DYLIB_VARIANT
# recompile step below for the mechanism.
#
# `_syslog` DUPLICATE-SYMBOL INVESTIGATION (2026-08-31, static-linking
# cleanup, DAR-7): `libopenpam.dylib` and `libcopyfile.dylib` both export
# `_syslog` -- investigated, CONSCIOUSLY NOT consolidated, same accepted-
# exception category as this dylib's own `_connect`/`_socket`/`_send`/
# `_writev` ENOSYS stubs above. This dylib's copy is the real, unmodified
# `Libc/gen/oldsyslog.c` (openpam_log.c's vsyslog() call needs its real
# LOG_CONS console fallback, which depends on those same local socket
# stubs failing on purpose -- see build_openpam.sh's own header).
# `libcopyfile.dylib`'s copy (copyfile_syscalls.c) is a deliberate,
# separately-documented no-op stand-in for a much more widely-loaded
# library (cp/mv/tar/ls/find/du/gzip/less/ed/chmod/mktemp/sed, plus
# libSystem.B.dylib/libdyld.dylib itself, vs. this dylib's one real
# consumer, syslogd.macho). Confirmed unsafe to naively point
# libcopyfile.dylib at this dylib's real implementation instead: (1)
# architecturally backwards -- would force every copyfile.dylib consumer
# to also load a PAM-specific dylib for an unrelated feature; (2) not
# even a behavior improvement -- copyfile.c's own `syslog(LOG_WARNING/
# LOG_DEBUG, ...)` call sites never call `openlog()`, so oldsyslog.c's
# default `_sl_LogStat == 0` means no LOG_CONS/LOG_PERROR bit is ever
# set and the real implementation would still print nothing, just after
# a real wasted `socket()`+`connect()` syscall pair against a
# `/var/run/syslog` this target never runs (no syslogd is auto-spawned,
# DAR-153) on every non-fatal diagnostic call site across most of the
# 76 simple commands -- a real regression, not a cleanup. Confirmed no
# live dyld ambiguity either: swept every built `.dylib`/`.macho` for an
# undefined external reference to `_syslog` (`nm -m`) -- zero hits
# anywhere; both copies are purely self-contained (copyfile.c's macros
# resolve to copyfile_syscalls.c's local copy, openpam_log.c's vsyslog()
# resolves to oldsyslog.c's local copy), so even though syslogd.macho
# ends up loading both dylibs at once (this one directly, libcopyfile.dylib
# transitively via libSystem.B.dylib), two-level-namespace chained fixups
# mean neither side's bind can ever be shadowed by the other. Left as a
# permanent, documented exception -- do not attempt to merge these two.
#
# Re-verified 2026-09-17 (DAR-7 continuation) with a stronger reason to
# leave this exported, found by checking real built artifacts rather than
# re-reasoning from source alone: `xcrun dyld_info -fixups` on the
# already-built `libSystemConfiguration.dylib` shows its own `_syslog`
# reference binds to `libopenpam/_syslog`, a REAL live ordinal-specific
# dependency (`build_libsystemconfiguration_client.sh` lists
# libopenpam.dylib ahead of libcopyfile.dylib on its own link line).
# Hiding `_syslog` from this dylib's export trie the way `_connect`/
# `_socket` were hidden above would break that bind outright (the exact
# DAR-217 hazard: clean link, clean nm, dyld abort at load). `_syslog`
# stays exported -- do not add it to the `-unexported_symbol` list below
# without first fixing `build_libsystemconfiguration_client.sh`'s own
# link order (out of this ticket's scope, same precedent as `_connect`'s
# formerly-blocking `launchctl.macho` dependency above).
set -e
cd "$(dirname "$0")"

OUT="libc_build"
EXTRACT="$OUT/libopenpam_dylib/extracted"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
OPENPAM="../../third_party/OpenPAM/openpam"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
for a in libopenpam libc_locale liblaunchd_syscalls libpthread; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run build_${a}.sh first" >&2
        exit 1
    fi
done
for d in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_pthread libdyld; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done

# Real, verified fix (docs/backlog.md item d9): Xcode 12's own bundled
# ld64-609 has a real internal crash under -Wl,-fixup_chains that a much
# newer ld does not. Compiling still uses Xcode 12's clang/SDK above;
# only the final link step's linker binary changes, via clang's -B
# override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

(cd "$EXTRACT" && ar x "../../libopenpam.a")
# Real duplicate-symbol fix (static-linking cleanup, 2026-08-31): the
# wholesale `ar x` above pulls in 5 members that `build_libsystem_c.sh`
# ALREADY separately extracts from this exact same `libopenpam.a` (its
# own "Real gap: ICU also references time..." comment) -- `time.o`
# (`_time`), `gettimeofday.o` (`_gettimeofday`), `ls_fenv_compat.o`
# (`_fegetenv`/`_fesetenv`), `ps_commpage_gettimeofday_stub.o`
# (`___commpage_gettimeofday`), `simple_cmds_time_compat.o`. This dylib
# already links `libsystem_c.dylib` as a sibling below, so deleting these
# 5 re-extracted objects here (rather than trying to select every OTHER
# member by name) makes this dylib's own undefined references resolve
# there instead of to a second, private copy -- confirmed via a real
# trial link (0 undefined symbols) before treating this as done, not
# assumed.
#
# `localtime.o`/`timezone_unix03.o`/`strftime.o` REMOVED too 2026-08-31
# (static-linking cleanup, locale/timezone-string consolidation batch).
# Round 8 (continuation 6) tried removing just `localtime.o`/
# `timezone_unix03.o` and found it structurally blocked: this dylib's
# own (also wholesale-extracted) `strftime.o` directly references
# `__st_get_timezone` (timezone_unix03.o) and `___darwin_altzone`
# (localtime.o) for `%z`/`%Z` formatting, and both are real Apple
# `.private_extern` symbols (confirmed via `nm -m`, "was a private
# external") -- private externs cannot cross a dylib boundary, so as
# long as `strftime.o` stayed resident here, its two private-extern
# deps had to stay right beside it. This session's fix removes the
# WHOLE coupled trio instead of leaving `strftime.o` behind:
# `build_libsystem_c.sh` now compiles its own `strftime.o` (same real
# FreeBSD source, same `__DARWIN_UNIX03=1` recipe, so the exported
# symbol names match byte-for-byte: `_strftime$UNIX2003`/
# `_strftime_l$UNIX2003`/`_strftime_l`) and is a real, complete owner --
# its own `localtime.o`/`timezone_unix03.o` (already resident there for
# ICU's `mktime` gap) satisfy `strftime.o`'s private-extern refs
# locally, plus a new `-Wl,-alias,_mktime,'_mktime$UNIX2003'` there
# bridges the one real symbol-name difference between the two
# `__DARWIN_UNIX03` configurations (`libsystem_c.dylib`'s own
# `localtime.o` comes from `liblaunchd_core.a`, deliberately built
# WITHOUT UNIX03 for ICU's plain-`_mktime` expectation -- see that
# script's own comment). This dylib already links `libsystem_c.dylib` as
# a sibling below, so all of `tzset`/`localtime`/`gmtime`/`ctime`/
# `mktime`/`timegm`/`tzname`/`daylight`/`strftime`/`strftime_l` -- real
# duplicate STATE as well as code (`_tzname`/`_daylight`/`_lcl_rwlock`
# were each independently instantiated before this fix, the same
# duplicate-state shape as the already-fixed `mach_task_self_`/`g_zone`/
# `errno` cases) -- now resolve to the one real owner. Verified via a
# real trial link (0 undefined symbols) before treating this as done.
#
# `asctime.o` ALSO REMOVED 2026-08-31 (same session, closing DAR-134):
# the previous exception here ("libsystem_c.dylib's own `_asctime`/
# `_asctime_r` is a project-authored, deliberately temporary stand-in,
# consolidating would silently downgrade to the inferior stub") no
# longer holds -- DAR-134's real blocker (`strftime_l()`, which
# `asctime_r()` calls to format the 4-digit year) got ported this same
# session as part of the strftime.o move above, so `build_libsystem_c.sh`
# now compiles the SAME real, unmodified FreeBSD `asctime.c` too.
# Consolidating is a plain, ordinary dedup now, not a regression.
# Confirmed via `nm -u` that `asctime.c` needs neither `localtime.o`/
# `timezone_unix03.o` (it hardcodes its own day/month name tables) nor
# anything else just removed, so removing it doesn't disturb anything
# else in this extract dir.
rm -f "$EXTRACT"/time.o "$EXTRACT"/gettimeofday.o \
      "$EXTRACT"/ls_fenv_compat.o "$EXTRACT"/ps_commpage_gettimeofday_stub.o \
      "$EXTRACT"/simple_cmds_time_compat.o \
      "$EXTRACT"/localtime.o "$EXTRACT"/timezone_unix03.o "$EXTRACT"/strftime.o \
      "$EXTRACT"/asctime.o

# openpam_syscalls_compat.o REMOVED too 2026-08-31 (static-linking
# cleanup, dylib-vs-dylib duplicate-symbol pass, continuation): its own
# header explains it exists ONLY for libopenpam.a's own static
# smoketest link (real, unmodified localtime.c's tzload() calls
# access() to probe for /usr/share/zoneinfo/<TZ>, and the static
# archive has no sibling dylib to resolve it from) -- it defines ONLY
# `_access` (confirmed via nm, no other symbol), a full real duplicate
# of libsystem_c.dylib's own real copy. This dylib already links
# libsystem_c.dylib as a sibling below, so localtime.o's real reference
# resolves there instead. Not removed from libopenpam.a itself -- the
# static archive (dyld.macho's build, and libopenpam.a's own smoketest)
# still needs its own local copy.
rm -f "$EXTRACT"/openpam_syscalls_compat.o

# su_linker_set_bounds.o REMOVED (2026-09-21, real link failure found via
# a from-scratch build_everything.sh run): this object's own
# &_mh_execute_header reference is real and correct for su.macho (an
# MH_EXECUTE image, where the linker always populates that symbol - see
# su_linker_set_bounds.c's own header) but real Apple <mach-o/ldsyms.h>
# is explicit that _mh_execute_header "does not appear in any file type
# other than a MH_EXECUTE file type" - undefined at link time for this
# MH_DYLIB output ("libopenpam.dylib link found real undefined symbols:
# __mh_execute_header"). openpam_static.o (also wholesale-extracted
# above) genuinely needs __ls_begin()/__ls_end() too (confirmed via
# nm -u), so this is not a case of dropping dead weight - see
# openpam_dylib_linker_set_bounds.c (compiled below, once CFLAGS exists)
# for this dylib's own real replacement, using &_mh_dylib_header (the
# exact MH_DYLIB counterpart, same header, same linker-synthesized
# mechanism) instead.
rm -f "$EXTRACT"/su_linker_set_bounds.o

# `_shutdown`/`_send`/`_writev`/`_netsettime`/`_settimeofday`
# UNREFERENCED-DUPLICATE FIX (2026-09-03, DAR-7 continuation). cat_socket_stub.o
# and date_compat.o above (pulled in wholesale by the `ar x` because
# oldsyslog.o genuinely needs their `_socket`/`__NSGetArgv`) also carry
# `_shutdown` and `_send`/`_writev`/`_netsettime`/`_settimeofday`/
# `_pututxline` respectively -- confirmed via a real `nm -gu` sweep across
# EVERY object in this dylib's own extract dir that none of these symbols
# is referenced by anything here (oldsyslog.o only ever calls the
# $UNIX2003-suffixed `connect`/`send`/`writev`, the real, project-wide
# flat-namespace gap this file's own -Wl,-U lines already defer, and per
# this file's OWN header, genuinely unreachable dead code -- openlog()'s
# real logic bails out before ever reaching send/writev). Narrow each
# object to just the symbol(s) actually needed (same `ld -r
# -exported_symbol` technique this file already uses below for
# launchd_passwd_compat_narrow.o) rather than editing cat_socket_stub.c/
# date_compat.c themselves -- both are also compiled directly into
# cat.macho/date.macho (build_simple_cmds.sh), out of this ticket's scope.
# Real, measured result: 2 of the 5 duplicates this ticket's re-audit found
# (`_send`/`_writev` vs libsystem_kernel.dylib) are now gone.
#
# `_connect` and `_socket` (cat_socket_stub.o) HIDDEN FROM THE EXPORT
# TRIE (2026-09-17, DAR-7 continuation), not removed. Both symbols stay
# real, local definitions inside this dylib -- oldsyslog.o's own internal
# `socket()`/`connect$UNIX2003()` call sequence still resolves to them at
# THIS dylib's own static-link time, same as before, because same-image
# references never go through the export trie/dyld bind mechanism at all.
# `-Wl,-unexported_symbol` only stops OTHER images from being able to bind
# to this dylib's private copies -- it does not change this dylib's own
# internal behavior.
#
# History: as of 2026-09-03/04, `_connect` was a REAL LIVE DEPENDENCY,
# found the expensive way (a real QEMU boot): `launchctl.macho` linked
# libopenpam.dylib ahead of libsystem_kernel.dylib on ITS OWN link line,
# so ld64 baked an ordinal-specific two-level bind of `_connect` ->
# libopenpam.dylib straight into launchctl.macho at ITS link time. That
# blocked hiding the export outright (see DAR-7's Linear history for the
# full `dyld[12]: Symbol not found: _connect` trace).
#
# Re-verified 2026-09-17: DAR-261 (2026-09-07, unrelated -- a
# `_task_for_pid` duplicate-symbol fix) moved `libsystem_kernel.dylib`
# earlier on `build_launchctl.sh`'s own link line as a side effect, ahead
# of libopenpam.dylib too. Confirmed via `xcrun dyld_info -fixups` on the
# actual built `launchctl.macho` (not assumed): `_connect` now binds to
# `libsystem_kernel/_connect`, not libopenpam. Swept every other
# currently-built image that links libopenpam.dylib
# (`libCoreFoundation.dylib`, `libSystemConfiguration.dylib`) the same
# way -- `_connect`/`_socket` both already bind to `libsystem_kernel`
# everywhere; `libopenpam/_connect` and `libopenpam/_socket` are bound by
# zero currently-built images project-wide. The 4 not-currently-built
# consumers (`chronyd.macho`/`configd.macho`/`notifyd.macho`/
# `syslogd.macho`) all list `libsystem_kernel.dylib` earlier than
# `libopenpam.dylib` on their own link lines too (checked each
# build_*.sh's own link-line order directly), so a fresh build of any of
# them would resolve the same way. Safe to hide both exports now --
# nothing project-wide has an ordinal-specific bind to libopenpam's
# copies of `_connect`/`_socket` to break. If a future consumer's link
# order ever changes to list libopenpam.dylib first, `bind_target_audit.sh`
# / `dup_symbol_audit.sh --binaries` will catch a resulting shadow before
# it becomes a live UNRESOLVABLE bind -- re-check with `dyld_info -fixups`
# before touching this again.
#
# `_socket`'s own internal-use reasoning, CORRECTED 2026-09-17: an
# earlier round of this ticket (2026-09-03/04 comments) claimed
# `connect$UNIX2003` "has no real provider anywhere in this project" as
# the reason `_socket` must stay a private stub. That claim was already
# stale the day it was written -- commit 6ba2b5e (2026-08-31, THREE DAYS
# EARLIER) had already added real `_connect$UNIX2003`/`_send$UNIX2003`/
# `_writev$UNIX2003` wrappers to libsystem_kernel.dylib, confirmed today
# via `nm -gU` on the real built dylib and `dyld_info -fixups` on this
# dylib's own rebuilt output (binds to `libsystem_kernel/_connect$UNIX2003`
# for real, not flat-namespace). Per DAR-313 (comment citations must be
# verified against real source, not reused transitively): the REAL reason
# to keep this dylib's own private `socket`/`connect`/`send`/`writev`
# stubs is the one commit 6ba2b5e's own message states directly --
# oldsyslog.o's `vsyslog()` deliberately needs these calls to FAIL so it
# takes its real, designed `LOG_CONS` console fallback path; pointing it
# at libsystem_kernel.dylib's now-real, now-working implementations would
# make `socket()`+`connect$UNIX2003()` actually succeed against
# `/var/run/syslog` (this project's launchd DOES bind that real AF_UNIX
# SOCK_DGRAM socket via com.apple.syslogd.plist's own Sockets key /
# socketgroup_setup_one(), confirmed via inject_into_sd_image.sh's own
# comment) -- a real, deliberate BEHAVIOR CHANGE (syslog output routing
# to a real socket instead of the console), not a symbol-hygiene fix, and
# out of this ticket's scope. Hiding the EXPORT (this pass's own change)
# does not touch this internal resolution at all -- see the top of this
# comment -- so this correction changes no code, only the stated reason.
#
# `_pututxline` (date_compat.o) ALSO stays exported -- same real-live-
# dependency discovery as `_connect` above, this time from
# `syslogd.macho` (build_syslogd.sh, also out of this ticket's scope):
# bind_target_audit.sh flagged `libopenpam/_pututxline (not exported...)`
# as UNRESOLVABLE the moment this pass's first narrowing pass hid it.
# Not a real duplicate-symbol count fix either way (`_pututxline` was
# never in the `--summary` dylib-vs-dylib duplicate list -- no other
# shipped dylib defines it), just an accidental single-owner export
# syslogd.macho already depends on; safest to leave it exactly as
# syslogd.macho's own link already expects.
NARROW_LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
"$NARROW_LD" -r -arch arm64 -exported_symbol _socket -exported_symbol _connect \
    "$EXTRACT/cat_socket_stub.o" -o "$EXTRACT/cat_socket_stub_narrow.o"
mv -f "$EXTRACT/cat_socket_stub_narrow.o" "$EXTRACT/cat_socket_stub.o"
"$NARROW_LD" -r -arch arm64 -exported_symbol __NSGetArgv -exported_symbol _pututxline \
    "$EXTRACT/date_compat.o" -o "$EXTRACT/date_compat_narrow.o"
mv -f "$EXTRACT/date_compat_narrow.o" "$EXTRACT/date_compat.o"

(cd "$EXTRACT" && ar x "../../libc_locale.a" table.o none.o)
# See narrow_rune_locale.sh's own header (static-linking cleanup, Phase
# 1d, 2026-08-30): demotes the 4 public rune-locale globals table.o/
# none.o carry so they bind to libsystem_c.dylib (a sibling below)
# instead of this dylib defining a second, independently-mutable copy.
./narrow_rune_locale.sh "$EXTRACT"
(cd "$EXTRACT" && ar x "../../liblaunchd_syscalls.a" sys__setgroups.o)
# geteuid.o (syslogd_libc_extras.a) / pthread_getegid.o (libpthread.a)
# REMOVED 2026-08-30 (static-linking cleanup, dylib-vs-dylib duplicate-
# symbol pass), same fix + same real reasoning as build_bsm_dylib.sh's
# own geteuid.o/getuid.o removal: _geteuid/_getegid are both real
# libsystem_kernel.dylib exports as of 2026-08-29 (see that script's own
# "geteuid/getegid" comment), and this dylib already links
# libsystem_kernel.dylib as a sibling below -- libopenpam.a's own
# undefined references now resolve there instead of a second, private
# copy of each raw syscall trap.

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"
"$CLANG" $CFLAGS libc_locale_globals_compat.c -o "$EXTRACT/libc_locale_globals_compat.o"

# openpam_dynamic_compat.o REPLACED 2026-08-31 (static-linking cleanup,
# dylib-vs-dylib duplicate-symbol pass, DAR-8): the object `ar x`
# extracted above still carries the honest-stub `dlclose()` this file's
# own header documents as permanently-dead code (openpam never returns a
# module with a non-NULL dlh) -- but real dyld now exists, and
# libdyld.dylib linked below is the real, canonical owner of `_dlclose`
# (see that file's own DYLIB BUILD comment). Recompile the same source
# fresh with OPENPAM_DYNAMIC_COMPAT_DYLIB_VARIANT defined, which omits
# just the dlclose() definition (openpam_dynamic() itself, still
# genuinely needed, is untouched) so this dylib's own undefined
# `_dlclose` reference resolves externally to libdyld.dylib's real
# implementation instead of a second, private copy. The static archive
# build (build_openpam.sh, consumed by dyld.macho -- which runs before
# libdyld.dylib exists and cannot link one back) keeps the original,
# unguarded compile untouched.
CFLAGS_OPENPAM="$CFLAGS -I $OPENPAM/include -I $OPENPAM/lib -DLIB_MAJ=2 -D_GNU_SOURCE -DOPENPAM_DYNAMIC_COMPAT_DYLIB_VARIANT"
rm -f "$EXTRACT/openpam_dynamic_compat.o"
"$CLANG" $CFLAGS_OPENPAM openpam_dynamic_compat.c -o "$EXTRACT/openpam_dynamic_compat.o"

# _initgroups real fix (2026-09-01, DAR-153 syslogd boot investigation):
# was `-Wl,-U` deferred below with NO real provider anywhere in the
# process (confirmed via a real dyld abort on a real QEMU boot --
# "symbol not found in flat namespace '_initgroups'" -- once the
# alarm/setegid/seteuid/getgroups gap ahead of it in this dylib's own
# fixup chain was closed, see build_libsystem_kernel.sh's/
# build_libc_gen.sh's own updated comments for that earlier gap). Real
# source already exists in this project: launchd_passwd_compat.c's
# initgroups() (real BSD getgrouplist()-then-setgroups(2) algorithm,
# written for third_party/PureDarwin's launchd core.c, see that file's
# own header) -- reused here, not reinvented. Recompiled fresh (not
# reusing build_launchd_syscalls.sh's already-built .o) so this dylib's
# own CFLAGS/include path apply, matching every other file in this
# script. Narrowed to export ONLY `_initgroups` via `ld -r
# -exported_symbol` (same real technique build_libsystem_kernel.sh's
# own mach_port_set_attributes.o already uses) rather than also
# exporting this file's own getpwuid_r() -- that symbol isn't needed by
# this dylib's one real consumer (syslogd.macho) and real Apple doesn't
# put initgroups()/getpwuid_r() in the same library either, so keeping
# getpwuid_r private-extern here avoids this dylib becoming an
# unintended second public owner of it later.
"$CLANG" $CFLAGS -I ../libinfo_custom \
    launchd_passwd_compat.c -o "$OUT/libopenpam_dylib/launchd_passwd_compat_raw.o"
"$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld" -r -arch arm64 \
    -exported_symbol _initgroups \
    "$OUT/libopenpam_dylib/launchd_passwd_compat_raw.o" \
    -o "$EXTRACT/launchd_passwd_compat_narrow.o"
rm -f "$OUT/libopenpam_dylib/launchd_passwd_compat_raw.o"
# libinfo_custom.c: initgroups()'s own real li_pw_load()/li_gr_load()/
# li_getpwuid()/li_gr_entry_count()/li_gr_at() dependency (flat-file
# /etc/master.passwd,/etc/group reader, see that file's own header).
# Compiled fresh here too, same "narrowest real fix" discipline -- no
# other currently-built binary that also links libopenpam.dylib
# privately compiles this file into itself (confirmed: syslogd.macho,
# this dylib's one real consumer, is a plain dyld-linked executable with
# no static compat objects of its own), so there is no duplicate-
# static-copy risk from adding a real (non-narrowed) copy here.
"$CLANG" $CFLAGS -I ../libinfo_custom \
    ../libinfo_custom/libinfo_custom.c -o "$EXTRACT/libinfo_custom.o"

# openpam_dylib_linker_set_bounds.c: this dylib's own real __ls_begin()/
# __ls_end() (see the su_linker_set_bounds.o removal above for why the
# su(1)-specific one can't be reused here).
"$CLANG" $CFLAGS openpam_dylib_linker_set_bounds.c \
    -o "$EXTRACT/openpam_dylib_linker_set_bounds.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libopenpam.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libopenpam.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-U,_alarm -Wl,-U,_NXArgv -Wl,-U,_setegid -Wl,-U,_seteuid -Wl,-U,_getgroups \
    -Wl,-U,_sigaction \
    -Wl,-U,___ldpart_free_extra -Wl,-U,___part_load_locale \
    -Wl,-U,'_connect$UNIX2003' -Wl,-U,'_send$UNIX2003' -Wl,-U,'_writev$UNIX2003' \
    `# _connect/_socket HIDDEN from the export trie (DAR-7, 2026-09-17) --` \
    `# see the "HIDDEN FROM THE EXPORT TRIE" comment near the top of this` \
    `# file. Both stay real local definitions (cat_socket_stub.o, still` \
    `# ar-extracted above); oldsyslog.o's own internal socket()/connect()` \
    `# calls are unaffected -- only external dyld binds to these two` \
    `# ordinals are removed, and nothing currently built has one.` \
    -Wl,-unexported_symbol,_connect -Wl,-unexported_symbol,_socket \
    -o "$OUT/system/libopenpam.dylib" \
    "$EXTRACT"/*.o \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" "$OUT/system/libdyld.dylib" \
    2>"$OUT/libopenpam_dylib_link.err" \
    && echo "wrote $OUT/system/libopenpam.dylib" \
    || { echo "libopenpam.dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libopenpam_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }

otool -hv "$OUT/system/libopenpam.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
echo "declared -Wl,-U deferrals (expect: <flat-namespace> bind target):"
xcrun dyld_info -fixups "$OUT/system/libopenpam.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print "  " $NF}'
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libopenpam.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/|libdyld/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found:" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libopenpam.dylib: every non-deferred external reference binds to a real, named sibling dylib -- no unexpected/dangling references"
