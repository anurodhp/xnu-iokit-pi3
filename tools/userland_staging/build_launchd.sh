#!/bin/bash
# Builds libc_build/launchd.macho: the real, complete, dynamically-linked
# launchd executable -- promotes build_launchd_core.sh's own informational
# trial link (which reached 0 undefined/0 duplicate symbols for real,
# 2026-08-28, see docs/daemon-roadmap.md's "Milestone 2 CLOSED" writeup)
# into a real, first-class, checked-in build product, per this project's
# own "every built artifact needs a reproducing script" convention.
#
# `-force_load` on liblaunchd_core.a is NOT just informational here --
# it's the real, required build flag. Real launchd.c's own main() reaches
# core.o/ipc.o/log.o only indirectly (dispatch source handlers, XPC
# connection callbacks stored as function pointers), which a plain
# (non-force_load) static archive link's pull-in logic can't see as a
# "reference" -- confirmed by build_launchd_core.sh's own header comment
# (its first trial-link attempt, without -force_load, silently produced a
# "0 undefined symbols" link that was missing core.o/ipc.o/log.o
# entirely). Real DYNAMIC link (not `-static`): CoreFoundation depends on
# real libdispatch.dylib, which a static link can't resolve at all (real
# ld64 limitation). Every `-Wl,-alias` pair and prerequisite archive here
# is real, load-bearing, and copied exactly from build_launchd_core.sh's
# own real trial-link command (see that script + docs/daemon-roadmap.md
# for the full per-symbol writeup of why each one is needed).
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi

# Same real ld64-609 workaround as every other real dynamic-link script
# in this project (docs/backlog.md item d9): Xcode 12's own bundled ld
# crashes under -Wl,-fixup_chains, so the final link step's linker binary
# is the daily-driver Xcode's newer ld instead (compiling still uses
# Xcode 12's clang/SDK, untouched).
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

OUT="libc_build"
ICU_LIB="icu_build/target/lib"

for a in liblaunchd_core.a libCoreFoundation.a cf_libc_extras.a launchd_libc_extras.a \
         libxpc.a liblaunchd_mig.a libxpc_syscalls.a libxpc_syscalls2.a liblaunchd_syscalls.a \
         libmach_ipc.a libclosure.a libmalloc.a libplatform.a libpthread.a \
         libc_gen.a libc_string.a libc_stdio.a libc_printf.a libc_locale_for_stdio.a libbsm.a; do
    if [ ! -f "$OUT/$a" ]; then
        echo "error: $OUT/$a not found -- run build_launchd_core.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/cerror_stub.o" ]; then
    echo "error: $OUT/cerror_stub.o not found -- run build_launchd_core.sh first" >&2
    exit 1
fi
for d in libdispatch.dylib libdyld.dylib libSystem.B.dylib libsystem_pthread.dylib libc++.dylib; do
    if [ ! -f "$OUT/system/$d" ]; then
        echo "error: $OUT/system/$d not found -- run the matching build_*.sh (see docs/daemon-roadmap.md's dylib migration list) first" >&2
        exit 1
    fi
done
for d in libicuuc.dylib libicui18n.dylib libicudata.dylib; do
    if [ ! -f "$ICU_LIB/$d" ]; then
        echo "error: $ICU_LIB/$d not found -- run build_icu.sh first" >&2
        exit 1
    fi
done

rm -f "$OUT/launchd.macho"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-alias,__mh_execute_header,__mh_dylib_header \
    -Wl,-alias,_kCFDateFormatterCalendarKey,_kCFDateFormatterCalendar \
    -Wl,-alias,_kCFDateFormatterTimeZoneKey,_kCFDateFormatterTimeZone \
    -Wl,-alias,_kCFCalendarIdentifierGregorian,_kCFGregorianCalendar \
    -Wl,-alias,_kCFLocaleCalendarKey,_kCFLocaleCalendar \
    -Wl,-alias,_kCFLocaleCountryCodeKey,_kCFLocaleCountryCode \
    -Wl,-alias,_kCFLocaleLanguageCodeKey,_kCFLocaleLanguageCode \
    -Wl,-alias,_kCFLocaleScriptCodeKey,_kCFLocaleScriptCode \
    -o "$OUT/launchd.macho" \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    -Wl,-force_load,"$OUT/liblaunchd_core.a" \
    "$OUT/libCoreFoundation.a" "$OUT/cf_libc_extras.a" "$OUT/launchd_libc_extras.a" \
    "$OUT/libxpc.a" "$OUT/liblaunchd_mig.a" \
    "$OUT/libxpc_syscalls.a" "$OUT/libxpc_syscalls2.a" "$OUT/liblaunchd_syscalls.a" \
    "$OUT/libmach_ipc.a" "$OUT/libclosure.a" "$OUT/libmalloc.a" "$OUT/cerror_stub.o" "$OUT/libplatform.a" \
    "$OUT/libpthread.a" "$OUT/libc_gen.a" "$OUT/libc_string.a" "$OUT/libc_stdio.a" "$OUT/libc_printf.a" \
    "$OUT/libc_locale_for_stdio.a" "$OUT/libbsm.a" \
    "$OUT/system/libdispatch.dylib" "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    "$ICU_LIB/libicuuc.dylib" "$ICU_LIB/libicui18n.dylib" "$ICU_LIB/libicudata.dylib" "$OUT/system/libc++.dylib" \
    2>"$OUT/launchd_link.err" \
    || { echo "error: real link failed -- see $OUT/launchd_link.err" >&2; tail -60 "$OUT/launchd_link.err" >&2; exit 1; }

echo "wrote $OUT/launchd.macho"

# Real smoketest: not just "did the link succeed" (a plain, non-force_load
# link can trivially succeed with 0 undefined symbols while silently
# missing core.o/ipc.o/log.o entirely, per this script's own header
# comment) -- confirm real daemon content actually linked in, via a
# distinctive, real, externally-visible core.c-only symbol
# (job_dispatch, the job-state-machine's own real entry point).
otool -hv "$OUT/launchd.macho" | grep -q " EXECUTE " || {
    echo "WARNING: not a real MH_EXECUTE" >&2
}
otool -hv "$OUT/launchd.macho" | grep -q NOUNDEFS || {
    echo "WARNING: NOUNDEFS not set -- real undefined symbols may remain" >&2
}
if nm "$OUT/launchd.macho" 2>/dev/null | grep -q "T _job_dispatch$"; then
    echo "launchd.macho: real core.c content confirmed linked (_job_dispatch present)"
else
    echo "WARNING: _job_dispatch not found -- core.o may not have actually linked in (see this script's own -force_load comment)" >&2
fi
echo "linked against:"
otool -L "$OUT/launchd.macho" 2>/dev/null | tail -n +2
