#!/bin/bash
# Builds libc_build/system/libCoreFoundation.dylib: static-to-dylib
# migration (DAR-195), same pass/precedent as build_libresolv_dylib.sh /
# build_openpam_dylib.sh -- read those two scripts' own headers first,
# this one follows the identical shape (ar x the already-built,
# already-verified static archive's member objects rather than
# recompiling, drop/narrow the handful that now duplicate a real sibling
# dylib's own exports, link as a real MH_DYLIB against those siblings).
#
# WHY NOW (not "not urgent" anymore): docs/historic/backlog.md's item
# d12 carry-forward flagged this as deliberately deferred ("own future
# session -- real load-time global state/constructors"). DAR-170's real
# configd/DHCP port is about to become a heavy new CF consumer; the user
# explicitly decided (2026-09-01) to close this once, now, rather than
# let a 7th static consumer (bootp/configd) get added and have to
# migrate it twice. See Linear DAR-195 for the full writeup.
#
# REAL CONSTRUCTOR/INIT-ORDER INVESTIGATION (the deferred-for-a-reason
# part) -- done BEFORE writing this script, not assumed:
#   `third_party/swift-corelibs-foundation/CoreFoundation/Base.subproj/
#   CFRuntime.c:1121` declares
#   `static void __CFInitialize(void) __attribute__ ((constructor));` --
#   a REAL, ordinary Mach-O `__DATA,__mod_init_func` constructor, not a
#   lazy dispatch_once/pthread_once gate on first CF call. Its own body
#   (lines 1128-1280) is self-guarded (`__CFInitialized`/
#   `__CFInitializing` flags) so it is safe to run exactly once per
#   process, whenever dyld actually invokes it. Confirmed via `nm -m` on
#   libCoreFoundation.a that NO OTHER file in this archive registers a
#   second `__mod_init_func` constructor -- this is CF's only one.
#
#   The real open question this ticket flagged was whether THIS
#   project's own dyld-940 port (third_party/dyld, real Apple source,
#   patched via third_party-patches/dyld.patch) actually runs a loaded
#   dylib's own mod_init_func section at load time at all, vs. only ever
#   running the MAIN EXECUTABLE's own initializers (which Csu/crt.c's
#   real startup code always ran anyway, even in the old fully-static
#   world -- so a constructor baked into the main executable already
#   "worked" whether or not dyld's own runInitializers() path was real).
#   Checked directly: `third_party/dyld/dyld/JustInTimeLoader.cpp`
#   defines a real, unmodified `JustInTimeLoader::runInitializers()`
#   (line 511) that dyld's own real load path calls for every loaded
#   image, dylibs included -- not stubbed out, not main-executable-only.
#   This project's dyld already exercises this exact machinery for other
#   real per-library dylibs today (libdispatch.dylib, libsystem_notify.dylib
#   -- both had real, since-fixed init-ORDERING bugs the same week this
#   ticket was filed, i.e. their own constructors DO run, just needed
#   fixing for what they did once running). So the real risk here was
#   never "will a dylib's constructor run at all" (already proven yes,
#   generically, by every other real dylib in this stack) -- it's
#   "does __CFInitialize's own body have any real ordering dependency on
#   something not yet ready at the point dyld runs it". Read its body:
#   pthread_self()/getenv()/_NSGetArgv()/_NSGetArgc()/CFArrayCreate() --
#   all either libSystem-provided (dyld runs libSystem's own initializer
#   before any of libSystem's own dependents' per real dyld init-order
#   rules, unchanged by this project's real dyld source) or CF's own
#   internal calls (fine, this IS __CFInitialize, CF initializing
#   itself). No dependency on anything CF's own sibling-dylib link list
#   below doesn't already guarantee is initialized first. Real,
#   QEMU-verified confirmation that this actually holds (not just "reads
#   safe on paper") is the harness build_corefoundation_dylib_test.sh
#   ADDS as a companion script -- see its own header for what it proves.
#
# REAL DUPLICATE-SYMBOL PRE-AUDIT (done before writing the extract/link
# steps below, same method dup_symbol_audit.sh/gap_audit.sh formalize):
# `nm -gU` on every one of libCoreFoundation.a's 105 member objects,
# checked against the union of every sibling dylib this new dylib will
# actually link against. 38 real duplicate-symbol hits found, all
# individually accounted for below (either the whole object is dropped
# because a sibling already owns 100% of what it defines, or the object
# is narrowed via a real `ld -r -exported_symbols_list` partial link --
# same technique build_openpam_dylib.sh's own `_initgroups` narrowing
# already established -- to keep only the real, non-duplicated symbols
# it alone still provides).
set -e
cd "$(dirname "$0")"

OUT="libc_build"
EXTRACT="$OUT/libcorefoundation_dylib/extracted"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"
CF="../../third_party/swift-corelibs-foundation/CoreFoundation"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
LD="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi
if [ ! -f "$OUT/libCoreFoundation.a" ]; then
    echo "error: $OUT/libCoreFoundation.a not found -- run build_corefoundation.sh first (this script reuses its already-built member objects)" >&2
    exit 1
fi
# DAR-253: real Unicode-decomposition bitmap/property/mapping data
# files (checked into this fork already, see build_corefoundation.sh's
# own comment above the now-dead CFCharacterSetData.S/
# CFUniCharPropertyDatabase.S compiles it still does). CFUniChar.c
# compiles with TARGET_OS_MAC=1, which sets USE_MACHO_SEGMENT=1
# unconditionally (CFUniChar.c line ~44) -- on that path,
# __CFUniCharLoadFile() NEVER reads the raw .incbin symbols those two
# .S files provide; it calls __CFGetSectDataPtr("__UNICODE", ...),
# which searches already-loaded Mach-O images for real segment/section
# names (__UNICODE,__csbitmaps / __UNICODE,__properties /
# __UNICODE,__data) via getsectbynamefromheader_64(). This project's
# CF dylib link never created that segment, so every one of those
# lookups silently returned a garbage/adjacent address instead of
# NULL -- __CFUniCharLoadBitmapData() then read a bogus header off
# that address and dereferenced a wild pointer following it. Real,
# symbolicated crash (Linear DAR-253, two independent investigations):
# CFStringGetFileSystemRepresentation -> CFUniCharDecomposeWithErrorLocation
# -> __CFUniCharLoadDecompositionTable -> CFUniCharGetBitmapPtrForPlane,
# and CFBundleCreateBundlesFromDirectory's own addBundle()/sortBundles()
# path into the same loader. This crashed BOTH configd (bundle
# discovery, DAR-170) and notifyd (CFURLCreateWithFileSystemPath,
# DAR-167) -- any real CF path/string call that needs Unicode
# decomposition data hit it.
#
# Real fix (real Apple/swift source, not custom): swift-corelibs-
# foundation's OWN CMakeLists.txt (CoreFoundation/CMakeLists.txt,
# the real Darwin platform link-options target) creates this exact
# segment via `-sectcreate __UNICODE <section> <file>` linker flags
# pointing directly at the same 3 real data files already checked into
# this fork's CharacterSets/ dir -- no assembly needed at all. This
# project's dylib link below never carried those flags (added as part
# of the DAR-195 static-to-dylib conversion, which didn't carry this
# forward from the old, never-actually-executed static-archive path).
# Replicating that real recipe here, verbatim in spirit: little-endian
# (__CF_BIG_ENDIAN__ is not defined on arm64) so __data uses
# CFUnicodeData-L.mapping, matching MAPPING_TABLE_FILE's own
# __CF_BIG_ENDIAN__-gated choice in CFUniChar.c.
CF_UNICHAR_BITMAP_FILE_PATH="$(cd "$CF" && pwd)/CharacterSets/CFCharacterSetBitmaps.bitmap"
CF_UNICHAR_PROPDB_FILE_PATH="$(cd "$CF" && pwd)/CharacterSets/CFUniCharPropertyDatabase.data"
CF_UNICHAR_MAPPING_FILE_PATH="$(cd "$CF" && pwd)/CharacterSets/CFUnicodeData-L.mapping"
for f in "$CF_UNICHAR_BITMAP_FILE_PATH" "$CF_UNICHAR_PROPDB_FILE_PATH" "$CF_UNICHAR_MAPPING_FILE_PATH"; do
    if [ ! -f "$f" ]; then
        echo "error: $f not found -- real Unicode data file missing from this swift-corelibs-foundation checkout" >&2
        exit 1
    fi
done
SIBLING_DYLIBS=(libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_pthread \
    libsystem_notify libsystem_m libdispatch libc++ libxml2 libresolv libcopyfile libremovefile \
    libopenpam libdyld libxpc libinfo)
# Real gap, found 2026-09-14: this combined `||` check with one shared
# generic message was invisible to gen_build_order.py's parser (its
# idiom-1 regex needs a literal "run build_X.sh first" per script), which
# is how build_launchd_core.sh ended up scheduled after this script
# despite this real requirement. Rephrased into individual checks.
# DAR-121: real syslog-385 ASL client for __CFLogCStringLegacy()'s
# asl_open/asl_new/asl_set/asl_send/asl_free/asl_close (CFUtilities.c).
# These used to bind to a no-op shim inside libinfo.dylib.
if [ ! -f "$OUT/system/libsystem_asl.dylib" ]; then
    echo "error: $OUT/system/libsystem_asl.dylib not found -- run build_libsystem_asl_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/liblaunchd_core.a" ]; then
    echo "error: $OUT/liblaunchd_core.a not found -- run build_launchd_core.sh first (this dylib pulls a handful of real, narrow, non-duplicated objects out of it -- see the extraction step below)" >&2
    exit 1
fi
if [ ! -f "$OUT/liblaunchd_syscalls.a" ]; then
    echo "error: $OUT/liblaunchd_syscalls.a not found -- run build_launchd_syscalls.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/libmalloc.a" ]; then
    echo "error: $OUT/libmalloc.a not found -- run build_libmalloc.sh first" >&2
    exit 1
fi
for d in "${SIBLING_DYLIBS[@]}"; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done
ICU_LIB="icu_build/target/lib"
for l in libicuuc libicui18n libicudata; do
    if [ ! -f "$ICU_LIB/$l.dylib" ]; then
        echo "error: $ICU_LIB/$l.dylib not found -- run build_icu_host.sh then build_icu_target.sh first" >&2
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

(cd "$EXTRACT" && ar x "../../libCoreFoundation.a")

# REAL BUG FOUND (2026-09-02, part of this ticket's own mandated
# constructor/init-order verification): CFRuntime.c's __CFInitialize is
# NOT actually a real constructor on this project's TARGET_OS_MAC build
# (confirmed via a real preprocessor probe -- see
# cf_compat_headers/cf_init_constructor.c's own header for the full
# writeup) and has no other real caller anywhere in this checkout on
# this target. Pre-existing bug, not introduced by this dylib conversion
# -- the static build never called it either. Real fix: a small,
# separately-compiled, project-owned real constructor that just calls
# CF's own real (plain, external-linkage) __CFInitialize() -- not a
# patch to upstream CFRuntime.c (see that file's own header for why a
# source patch was considered and rejected in favor of this, matching
# the CFLocaleKeys.c precedent already in this checkout's own git
# history, commit 9381a2e).
CFLAGS_INIT=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0)
"$CLANG" "${CFLAGS_INIT[@]}" cf_compat_headers/cf_init_constructor.c -o "$EXTRACT/cf_init_constructor.o"

# DAR-253: real _NSGetMachExecuteHeader() for this dylib specifically --
# see cf_compat_headers/nsgetmachheader_compat.c's own header for the
# full root-cause writeup (real NULL+0x10 crash inside cctools'
# getsegbyname(), reached from CFBundleCreate()'s main-bundle
# initialization the first time any bundle is created in a process).
"$CLANG" "${CFLAGS_INIT[@]}" cf_compat_headers/nsgetmachheader_compat.c -o "$EXTRACT/nsgetmachheader_compat.o"

# --- DROP: whole object fully superseded by a real sibling dylib -----
#
# cf_mach_port_syscalls.o / mach_port_traps.o (real __kernelrpc_mach_port_*
# client wrappers + their raw traps): ALL 7 symbols each now real,
# defined exports of libsystem_kernel.dylib (confirmed via nm -gU) --
# this dylib already links libsystem_kernel.dylib as a sibling below, so
# CF's own undefined references resolve there instead of a second,
# private copy.
#
# cf_mach_port_mig_fallback_stub.o: this file's OWN header already
# documents it as dead code on a real working boot ("the trap entry
# points these fall back FROM are all confirmed backed by a real
# kernel-side implementation... this path is dead code"). With the traps
# above gone from this dylib's own extract (superseded by
# libsystem_kernel.dylib's real ones), this fallback has zero remaining
# real callers here either.
#
# libm_compat.o: STALE -- confirmed via `grep` that build_corefoundation.sh's
# own FILES[]/explicit-compile list does NOT compile cf_compat_headers/
# libm_compat.c at all as of the 2026-08-27 real-libm.a switch (that
# script's own header documents fmod/modf/pow moving to real Apple Libm).
# This .o is a leftover object from before that switch, still sitting in
# cf_obj/ and getting swept into libCoreFoundation.a by that script's
# trailing `ar rcs ... cf_obj/*.o` glob -- a real latent bug in the
# static archive itself (tracked separately, not fixed here since this
# script only consumes the archive, doesn't rebuild it). Its 3 symbols
# (_fmod/_modf/_pow) are real, current exports of libsystem_m.dylib (a
# sibling below) -- dropping this stale object and linking the real
# dylib instead is a strict correctness improvement, not just a dedup.
#
# memmove_chk.o (___memmove_chk) / strncpy_chk.o (___strncpy_chk): each
# is a single-symbol object, and both symbols are now real, current
# exports of libsystem_c.dylib (a sibling below) -- same real "needs a
# new home, nobody came back to relink" class already documented
# elsewhere in this project's dup-symbol sweep. sprintf_chk.o/
# strlcat_chk.o are NOT dropped -- confirmed via nm that libsystem_c.dylib
# exports neither ___sprintf_chk/___sprintf_object_size_chk nor
# ___strlcat_chk, so these two stay as CF's own real, non-duplicated
# local definitions.
#
# cf_libproc_syscalls.o (_proc_pidinfo) / proc_info_trap.o (its own real
# __proc_info raw trap, create-syscalls.pl-generated, and proc_pidinfo()'s
# only real caller of that trap in this archive): _proc_pidinfo is now a
# real, current export of libsystem_kernel.dylib -- dropping both
# together is correct (the trap object has no other real consumer left
# in this archive once its one caller is gone, confirmed via nm -u
# across the remaining extract set after removal, see the verification
# step below).
rm -f "$EXTRACT/cf_mach_port_syscalls.o" "$EXTRACT/mach_port_traps.o" \
      "$EXTRACT/cf_mach_port_mig_fallback_stub.o" \
      "$EXTRACT/libm_compat.o" \
      "$EXTRACT/memmove_chk.o" "$EXTRACT/strncpy_chk.o" \
      "$EXTRACT/cf_libproc_syscalls.o" "$EXTRACT/proc_info_trap.o"

# --- NARROW: object keeps real, non-duplicated symbols, drops the rest
# via a real `ld -r -exported_symbols_list` partial link (same technique
# build_openpam_dylib.sh's own `_initgroups` narrowing already
# established) -- the dropped symbols' function BODIES stay in the
# object as private/local, unreachable from outside and therefore no
# longer a real duplicate-PUBLIC-symbol hazard, without needing to
# re-edit or recompile the original hand-written .c source.
#
# cf_pthread_attr_syscalls.o: real pthread_attr_init/_destroy/
# _setdetachstate are now real, current exports of libsystem_pthread.dylib
# (a sibling below) -- keep only pthread_attr_setscope, this dylib's
# real, only remaining local owner of that one symbol (not part of
# libsystem_pthread.dylib's own export list, confirmed via nm).
cat > "$OUT/libcorefoundation_dylib/keep_pthread_attr.txt" <<'EOF'
_pthread_attr_setscope
EOF
"$LD" -r -arch arm64 -exported_symbols_list "$OUT/libcorefoundation_dylib/keep_pthread_attr.txt" \
    "$EXTRACT/cf_pthread_attr_syscalls.o" -o "$EXTRACT/cf_pthread_attr_syscalls.o.narrow"
mv "$EXTRACT/cf_pthread_attr_syscalls.o.narrow" "$EXTRACT/cf_pthread_attr_syscalls.o"

# cf_pthread_setname_syscalls.o: this file's own local _platform_bzero
# helper duplicates libsystem_platform.dylib's real __platform_bzero
# (confirmed via nm); its sibling local helper, _platform_strlcpy, is
# NOT duplicated anywhere but is also, confirmed by reading the file,
# dead code with zero real callers (pthread_setname_np's own body below
# it calls plain strlcpy()/bzero(), not these two wrappers -- both are
# vestigial leftovers from the verbatim pthread.c extraction, per this
# file's own header). Real plain `_bzero`/`_strlcpy` (what
# pthread_setname_np's body actually calls) are themselves real, current
# exports of libsystem_c.dylib (a sibling below), confirmed via nm --
# so narrowing this object down to just `_pthread_setname_np` is safe:
# nothing outside this file needs the other two, and this file's own
# real remaining symbol has everything it actually calls satisfied by a
# sibling.
cat > "$OUT/libcorefoundation_dylib/keep_pthread_setname.txt" <<'EOF'
_pthread_setname_np
EOF
"$LD" -r -arch arm64 -exported_symbols_list "$OUT/libcorefoundation_dylib/keep_pthread_setname.txt" \
    "$EXTRACT/cf_pthread_setname_syscalls.o" -o "$EXTRACT/cf_pthread_setname_syscalls.o.narrow"
mv "$EXTRACT/cf_pthread_setname_syscalls.o.narrow" "$EXTRACT/cf_pthread_setname_syscalls.o"

# cf_socket_syscalls.o: 7 of its 11 real raw-syscall wrappers
# (bind/getsockname/recv/recvfrom/sendto/setsockopt/socket) are now real,
# current exports of libsystem_kernel.dylib (a sibling below, confirmed
# via nm) -- keep only the 4 libsystem_kernel.dylib does NOT yet export
# (accept/getsockopt/listen/socketpair), this dylib's real, only
# remaining local owner of those four.
cat > "$OUT/libcorefoundation_dylib/keep_socket.txt" <<'EOF'
_accept
_getsockopt
_listen
_socketpair
EOF
"$LD" -r -arch arm64 -exported_symbols_list "$OUT/libcorefoundation_dylib/keep_socket.txt" \
    "$EXTRACT/cf_socket_syscalls.o" -o "$EXTRACT/cf_socket_syscalls.o.narrow"
mv "$EXTRACT/cf_socket_syscalls.o.narrow" "$EXTRACT/cf_socket_syscalls.o"

# malloc_zone_compat.o: REAL dup_symbol_audit.sh catch (2026-09-02, run
# after this dylib's first successful link, per this project's own
# standing "run it after any fix that touches a shared dylib" rule) --
# libinfo.dylib (a real sibling below, added earlier for asl_*/
# getpwuid_r) turns out to carry its OWN, separately-motivated compat
# stub with a real, partial overlap: malloc_default_zone/
# malloc_zone_calloc/malloc_zone_realloc are defined in BOTH. Not a
# symmetric duplicate, though (confirmed via nm) -- libinfo.dylib's copy
# has neither malloc_zone_malloc/_free/_memalign nor malloc_good_size,
# which this dylib's own CFAllocator.c call sites still need. Narrow to
# keep only the 4 libinfo.dylib does NOT provide; the 3 it does resolve
# to that real sibling instead of a second, private copy.
cat > "$OUT/libcorefoundation_dylib/keep_malloc_zone.txt" <<'EOF'
_malloc_zone_malloc
_malloc_zone_free
_malloc_zone_memalign
_malloc_good_size
EOF
"$LD" -r -arch arm64 -exported_symbols_list "$OUT/libcorefoundation_dylib/keep_malloc_zone.txt" \
    "$EXTRACT/malloc_zone_compat.o" -o "$EXTRACT/malloc_zone_compat.o.narrow"
mv "$EXTRACT/malloc_zone_compat.o.narrow" "$EXTRACT/malloc_zone_compat.o"

# --- ADD: real, narrow objects for symbols CF's own code needs that
# have zero real implementation anywhere in this dylib's sibling set --
# same "check for a real implementation before deferring" discipline
# DAR-181 already established for libinfo.dylib (5 symbols that turned
# out to have real source sitting unused). A real, systematic search
# (every already-built .a/.dylib under libc_build/, same method
# gap_audit.sh formalizes) found a real implementation of every one of
# these 12 remaining undefined symbols already sitting in
# liblaunchd_core.a/liblaunchd_syscalls.a/libmalloc.a -- none needed a
# genuine `-Wl,-U` deferral. These are all generic, CF-relevant helpers
# (UUID generation for CFUUID.c, rlimit for CFPlatform.c's stack-size
# probing, gethostuuid for CFUUID/CFPlatform, getsectbynamefromheader_64/
# getsegbyname for CFBundle_Binary.c's own dyld-image introspection,
# vsprintf_chk/chk_fail_overflow/chk_overlap for CF's own fortified
# string calls, sysdir_* for CFSystemDirectories.c) that only happened to
# live inside launchd-specific helper archives because those archives'
# own build scripts each compiled their own generic-helper copy, not
# because the functions are launchd-specific. CF becomes their one real,
# shared, canonical home going forward -- matching this project's own
# established "needs a new home" pattern (see git log's static-linking-
# cleanup rounds 9/12).
#
# REAL FOLLOW-UP THIS CREATES, tracked for the consumer-migration step
# below (NOT fixed here): liblaunchd_core.a/liblaunchd_syscalls.a/
# libmalloc.a are still linked directly into launchd.macho/launchctl.macho
# today, so once those two migrate to load this dylib instead of statically
# embedding libCoreFoundation.a, they will carry a real duplicate copy of
# these same 12 symbols (their own static one, from the exact same source
# archives, plus this dylib's) until their own build scripts stop pulling
# these specific objects in. dup_symbol_audit.sh run after each consumer
# migration is what catches and forces a fix for this -- not silently
# ignored, see this script's own final verification step and DAR-195's
# Linear tracking.
rm -rf "$OUT/libcorefoundation_dylib/extra"
mkdir -p "$OUT/libcorefoundation_dylib/extra"
EXTRA="$OUT/libcorefoundation_dylib/extra"
(cd "$EXTRA" && ar x "../../liblaunchd_core.a" launchd_audit_syscalls_compat.o vsprintf_chk.o sysdir_compat.o)
(cd "$EXTRA" && ar x "../../liblaunchd_syscalls.a" getrlimit.o sys____getrlimit.o setrlimit.o sys____setrlimit.o \
    cctools_getsectbynamefromheader64_compat.o cctools_getsegbyname.o gen_uuid.o uuid_unpack.o)
(cd "$EXTRA" && ar x "../../libmalloc.a" chk_fail.o)
# uuid_pack.o deliberately NOT extracted (real dup_symbol_audit.sh catch,
# not found by this script's own earlier per-object nm sweep because that
# sweep only checked gen_uuid.o's own two undefined refs -- _uuid_pack/
# _uuid_unpack -- against "does this exact archive already have it",
# never against the sibling-dylib union already established for CF's own
# archive objects): libsystem_c.dylib (already a linked sibling below)
# real, current exports include _uuid_pack (confirmed via nm) --
# gen_uuid.o's own undefined reference to it resolves there instead of a
# second, private copy. _uuid_unpack has NO such sibling owner (confirmed
# absent from libsystem_c.dylib's own export list), so uuid_unpack.o
# stays.

# launchd_audit_syscalls_compat.o ALSO defines audit_session_join/_port/
# _self, getpeername, nanosleep, readlink -- real functions, but CF
# doesn't reference any of them (confirmed via this dylib's own trial
# link before this fix, only `_gethostuuid` was ever undefined) and
# 3 of the 6 (the audit_session_* trio) have no sibling-dylib owner at
# all, which would make THEM a new real gap if exported here unused.
# Narrow to just `_gethostuuid`, same real `ld -r -exported_symbols_list`
# technique used above -- the other 6 function bodies stay in the object
# as private/local, unreachable, no new gap and no new dup.
echo "_gethostuuid" > "$OUT/libcorefoundation_dylib/keep_gethostuuid.txt"
"$LD" -r -arch arm64 -exported_symbols_list "$OUT/libcorefoundation_dylib/keep_gethostuuid.txt" \
    "$EXTRA/launchd_audit_syscalls_compat.o" -o "$EXTRA/launchd_audit_syscalls_compat.o.narrow"
mv "$EXTRA/launchd_audit_syscalls_compat.o.narrow" "$EXTRA/launchd_audit_syscalls_compat.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libCoreFoundation.dylib"

# Real `-Wl,-alias` entries for CF's own real/legacy constant-name pairs
# -- MOVED here from build_launchd.sh/build_launchctl.sh (both used to
# apply these at their OWN final executable link, back when CF was
# statically embedded into each of them separately). Per this project's
# own documented ld64 constraint (feedback_ld64_alias_nonlocal_base_bug:
# "alias BASE must be locally compiled in same link or ld64 silently
# emits a malformed ordinal-0 reexport"), an alias's base symbol
# (_kCFDateFormatterCalendar etc) must be LOCAL to the link that declares
# the alias. Now that CF is its own dylib, that base symbol lives HERE,
# not in launchd.macho/launchctl.macho -- so the alias has to move here
# too; leaving it at the old call sites would hit exactly that
# documented bug (malformed ordinal-0 reexport, verified via
# dyld_info -exports, not just nm, per that same feedback note).
# launchd.macho/launchctl.macho's own build scripts are updated in the
# same commit as this dylib's consumer migration to drop their now-stale
# copies of these same 7 -Wl,-alias flags.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libCoreFoundation.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-sectcreate,__UNICODE,__csbitmaps,"$CF_UNICHAR_BITMAP_FILE_PATH" \
    -Wl,-sectcreate,__UNICODE,__properties,"$CF_UNICHAR_PROPDB_FILE_PATH" \
    -Wl,-sectcreate,__UNICODE,__data,"$CF_UNICHAR_MAPPING_FILE_PATH" \
    -Wl,-segprot,__UNICODE,r,r \
    -Wl,-alias,_kCFDateFormatterCalendarKey,_kCFDateFormatterCalendar \
    -Wl,-alias,_kCFDateFormatterTimeZoneKey,_kCFDateFormatterTimeZone \
    -Wl,-alias,_kCFCalendarIdentifierGregorian,_kCFGregorianCalendar \
    -Wl,-alias,_kCFLocaleCalendarKey,_kCFLocaleCalendar \
    -Wl,-alias,_kCFLocaleCountryCodeKey,_kCFLocaleCountryCode \
    -Wl,-alias,_kCFLocaleLanguageCodeKey,_kCFLocaleLanguageCode \
    -Wl,-alias,_kCFLocaleScriptCodeKey,_kCFLocaleScriptCode \
    -o "$OUT/system/libCoreFoundation.dylib" \
    "$EXTRACT"/*.o \
    "$EXTRA"/*.o \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" "$OUT/system/libsystem_notify.dylib" \
    "$OUT/system/libsystem_m.dylib" \
    "$OUT/system/libdispatch.dylib" \
    "$ICU_LIB/libicuuc.dylib" "$ICU_LIB/libicui18n.dylib" "$ICU_LIB/libicudata.dylib" \
    "$OUT/system/libc++.dylib" "$OUT/system/libxml2.dylib" \
    "$OUT/system/libresolv.dylib" "$OUT/system/libcopyfile.dylib" "$OUT/system/libremovefile.dylib" \
    "$OUT/system/libopenpam.dylib" "$OUT/system/libdyld.dylib" \
    "$OUT/system/libxpc.dylib" \
    `# libsystem_asl after libopenpam/libcopyfile on purpose: those two` \
    `# also export syslog(3), and moving CF's syslog binds is not this` \
    `# change's job. Only the asl_* binds (no other exporter) move here.` \
    "$OUT/system/libsystem_asl.dylib" \
    "$OUT/system/libinfo.dylib" \
    2>"$OUT/libcorefoundation_dylib_link.err" \
    && echo "wrote $OUT/system/libCoreFoundation.dylib" \
    || { echo "libCoreFoundation.dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libcorefoundation_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }

otool -hv "$OUT/system/libCoreFoundation.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}

echo "declared -Wl,-U deferrals (expect: <flat-namespace> bind target, resolved at real load time):"
xcrun dyld_info -fixups "$OUT/system/libCoreFoundation.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print "  " $NF}'
# NOTE: ICU's own dyld_info label includes its real internal version
# number in the bind-target string (e.g. "libicui18n.66/_ucal_open", not
# "libicui18n/_ucal_open") -- confirmed by actually inspecting the real
# output, not assumed, so the icu entries below allow an optional
# "\.[0-9]+" between the lib basename and the slash.
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libCoreFoundation.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/|libsystem_notify/|libsystem_m/|libdispatch/|libicuuc(\.[0-9]+)?/|libicui18n(\.[0-9]+)?/|libicudata(\.[0-9]+)?/|libc\+\+/|libxml2/|libresolv/|libcopyfile/|libremovefile/|libopenpam/|libdyld/|libxpc/|libsystem_asl/|libinfo/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found:" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libCoreFoundation.dylib: every non-deferred external reference binds to a real, named sibling dylib -- no unexpected/dangling references"

# Real, verified constructor check: confirm the real constructor this
# dylib now carries (cf_init_constructor.o's __iokit_CFInitialize_trigger,
# which calls CF's own real __CFInitialize()) survived linking. Checked
# BOTH real Mach-O initializer encodings, not just one -- the classic
# __DATA,__mod_init_func absolute-pointer-array section (what a single
# .o file's own otool -l shows, confirmed above at the object level
# before this link), and this project's -Wl,-fixup_chains-era linker's
# real, modern replacement, __TEXT,__init_offsets (a relative-offset
# table introduced for chained-fixups binaries -- confirmed via a real
# read of third_party/dyld's own MachOAnalyzer::forEachInitializer(),
# which this project's real, unmodified dyld already normalizes both
# encodings through, and Loader::findAndRunAllInitializers()/
# runInitializersBottomUp(), which already runs a loaded dylib's own
# initializers in real, correct bottom-up dependency order -- so
# whichever encoding ld64 chose here, this project's dyld already knows
# how to run it). Confirmed empirically too: this exact dylib link
# produces __TEXT,__init_offsets, not the classic section -- expected
# for a -Wl,-fixup_chains link, not a sign anything was lost. The actual
# "does dyld run it and does __CFInitialize's own body behave correctly"
# question is answered by build_corefoundation_dylib_test.sh's own real
# QEMU boot, not by this link-time-only check.
if otool -l "$OUT/system/libCoreFoundation.dylib" | grep -q "sectname __mod_init_func"; then
    echo "libCoreFoundation.dylib: real __DATA,__mod_init_func section present (__CFInitialize trigger constructor survived linking, classic encoding)"
elif otool -l "$OUT/system/libCoreFoundation.dylib" | grep -q "sectname __init_offsets"; then
    echo "libCoreFoundation.dylib: real __TEXT,__init_offsets section present (__CFInitialize trigger constructor survived linking, chained-fixups encoding)"
else
    echo "error: no real Mach-O initializer section (__DATA,__mod_init_func or __TEXT,__init_offsets) found -- the real constructor did not survive linking" >&2
    exit 1
fi
