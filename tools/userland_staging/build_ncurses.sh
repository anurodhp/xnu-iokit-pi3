#!/bin/bash
# Builds libc_build/libncurses.a: real, unmodified Apple ncurses source
# (third_party/ncurses, tag ncurses-57, pinned in setup_third_party.sh),
# following the same Csu-static-target/curated-libc-stack pattern every
# other tier in this directory uses (see build_libc_locale.sh for the
# fullest-commented reference version of this CFLAGS recipe,
# build_libc_regex.sh for the closest analog, build_libiconv.sh for a
# sibling "port one more Apple library" job done the same session).
#
# Real file-list forensics (never guessed -- confirmed against
# third_party/ncurses/ncurses.xcodeproj/project.pbxproj, the actual Xcode
# project Apple's own build uses):
#   - The top-level checkout is Xcode-project cruft; the real sources are
#     nested at third_party/ncurses/ncurses/ncurses/{base,tinfo,tty,trace,
#     widechar}/.
#   - The "libncurses" PBXNativeTarget's real Sources build phase (id
#     4C29FC2C10225768001D8279) lists exactly 164 files. 156 of those are
#     real, physical .c files under ncurses/ncurses/ (resolved by exact
#     basename search against this checkout, one hit each, zero
#     ambiguity). The other 8 (codes.c, comp_captab.c, expanded.c,
#     fallback.c, lib_gen.c, lib_keyname.c, names.c, unctrl.c) are NOT
#     checked-in source -- they are build-time GENERATED sources.
#
# Real generated-source pipeline (this is the one genuinely new piece of
# machinery this tier needed -- every earlier tier's "generated" files
# were already checked into the tarball, e.g. libiconv's *.def/*.h; this
# is the first tier where Apple's own real build generates C source at
# build time). Confirmed real, not improvised: the SAME xcodeproj has a
# real "Derived Sources" aggregate target (id FCD5D43214E5C99D00752D20)
# whose Run Script phase (id FCD5D43714E5CA1F00752D20) declares those
# exact 8 filenames (plus termsort.c, used by tic/infocmp, NOT by
# libncurses -- confirmed absent from libncurses's own Sources list, so
# not generated here) as its outputPaths, running
# xcodescripts/derived_headers.sh + xcodescripts/derived_sources.sh. That
# target in turn depends on three more real aggregate targets:
# "Bootstrap Sources" (xcodescripts/bootstrap_headers.sh +
# bootstrap_sources.sh -- produces just enough of curses.h/names.c to
# build the next two), "native_make_hash", and "native_make_keys" (each
# just xcodescripts/native_execs.sh building a tiny native host tool from
# real Apple source, tinfo/comp_hash.c and tinfo/make_keys.c
# respectively). Stage 1 below reproduces this exact real pipeline with
# host tools (host cc/awk, since these are host-side codegen steps that
# produce portable C text, not target object code -- Apple's own
# native_execs.sh does the same thing via `cc -sdk macosx.internal`,
# i.e. the HOST sdk, not the iPhoneOS target sdk this script's Stage 2
# cross-compiles everything else with). ncurses_cfg.h itself is a real,
# checked-in, already-generated-by-a-real-./configure-run file (its own
# header comment says so) -- no autoconf/./configure invocation needed
# anywhere in this script.
#
# Real, non-fabricated architectural gap (docs/backlog.md P2 item 11):
# this project has NO real termcap/terminfo DATABASE, only compiled
# library CODE. fallback.c is generated here with an empty FALLBACK_LIST
# (matching Apple's own real derived_sources.sh invocation, which also
# passes none), producing a real, correct, empty _nc_fallback() that
# always returns NULL (MKfallback.sh's own real "the fallback list is
# empty" branch -- not a stub, the exact same code shape Apple's own
# default build produces). USE_DATABASE=1 in the real, checked-in
# ncurses_cfg.h means the library still tries a real on-disk
# /usr/share/terminfo tree at runtime via read_entry.c/db_iterator.c
# (real code, genuinely compiled below) -- there is just no database
# provisioned on this project's freestanding root yet, a separate
# deliverable from "does the C code compile", exactly like real macOS
# systems without a terminfo tree installed.
#
# Real extra gaps found (documented, not silently patched) by actually
# compiling the full 164-file real Sources list and diffing `nm -u` on
# the resulting archive against every already-built libc_build/*.a:
#   - _XOPEN_SOURCE_EXTENDED (needed for USE_WIDEC_SUPPORT's real
#     ncurses/include/curses.wide section -- ncurses_cfg.h already sets
#     USE_WIDEC_SUPPORT=1, i.e. this checkout's real target IS the wide
#     build) must be passed on the command line, matching Apple's own
#     native_execs.sh MACROS. curses.priv.h:246-253's own
#     "#if USE_WIDEC_SUPPORT ... #else #undef _XOPEN_SOURCE_EXTENDED"
#     confirms the wide path is intentional, not incidental.
#   - link()/chdir()/mkdir()/access()/kill()/gettimeofday()/strstr()/
#     __darwin_check_fd_set_overflow()/sigprocmask()/__sigaction()+
#     __sigtramp/fpathconf()/getpgrp()/public read()+select() forwarders:
#     see ncurses_extra_syscalls.c's own header comment -- every body is
#     either a genuinely new raw syscall (cross-checked against
#     syscalls.master) or a verbatim reuse of an already-verified real
#     implementation from bash_syscalls.c/cron_extra_syscalls.c.
#   - Public sigaction(): real Apple wrapper chain sigaction()
#     [third_party/Libc/sys/sigaction.c] -> __platform_sigaction()
#     [third_party/libplatform/src/setjmp/generic/sigaction.c, real
#     unmodified source] -> __sigaction() [ncurses_extra_syscalls.c's raw
#     syscall] -- same real chain build_bash.sh already established
#     (including that script's own found-and-fixed real bug: both real
#     checkouts ship a file literally named sigaction.c, so this script
#     uses the same full-path-based unique object naming build_bash.sh's
#     own header comment documents, not a basename scheme). The
#     libplatform copy needs -D_DARWIN_C_SOURCE (NOT part of this tier's
#     main CFLAGS, which deliberately sets _XOPEN_SOURCE=600 for
#     ncurses's own wide-char code -- that combination hides
#     <sys/signal.h>'s NSIG under strict-XOPEN visibility rules on this
#     SDK, confirmed by reading the header's own
#     "!defined(_POSIX_C_SOURCE) || defined(_DARWIN_C_SOURCE)" guard, not
#     guessed): applied only to this one extra file via EXTRA_CFLAGS, not
#     the main ncurses source list, so real ncurses's own strict-XOPEN
#     compilation is undisturbed.
#   - abs()/atoi(): real Apple/FreeBSD source
#     (third_party/Libc/stdlib/FreeBSD/{abs,atoi}.c), same tier this
#     project's other ports already pull FreeBSD-fallback stdlib pieces
#     from (see build_libiconv.sh's qsort.c/heapsort.c).
#   - nanosleep(): lib_napms.c's real napms() genuinely calls it (not
#     incidental). Real, already-QEMU-verified Mach-semaphore-backed
#     implementation already exists at
#     tools/userland_staging/simple_cmds_nanosleep_pub_compat.c (Phase
#     4.6 item 5, see that file's own header comment) -- compiled a
#     second time here (own object, own tier) rather than cross-linking
#     build_simple_cmds.sh's output archive, same "duplicate compile,
#     don't cross-link a whole other tier's .a for one symbol" precedent
#     build_libiconv.sh's own header comment sets for flsl().
#   - Plain (non-$UNIX2003) sleep(): lib_print.c references the plain
#     symbol directly (confirmed via nm on the compiled .o, not the
#     $UNIX2003-suffixed alias every other tier's smoketest link step
#     already produces from libmalloc.a's real __mal_semwait_sleep).
#     Tried a second `-Wl,-alias` pointing the same base symbol at the
#     unversioned name too (two aliases, one base) -- that combination
#     genuinely crashes ld64 itself (a real
#     "Assertion failed: (target != NULL), function
#     generateLinkEditInfo" internal linker bug, reproduced directly, not
#     guessed). Real fix: ncurses_extra_syscalls.c defines plain sleep()
#     as a one-line real forwarder to libmalloc_sleep_shim.c's already-
#     real _mal_semwait_sleep(), the same "thin forwarder over an
#     already-real private implementation" shape that file's own read()/
#     select() forwarders already use -- no linker aliasing needed for
#     this one at all.
#   - pthread_testcancel(): libc_gen.a's real termios.o (tcdrain())
#     genuinely calls it. Every sibling tier's smoketest wires this up
#     via `-Wl,-alias,_pthread_testcancel,'_pthread_testcancel$UNIX2003'`
#     -- harmless there, but combined with THIS tier's three real,
#     reverse-direction pthread_rwlock_{wrlock,rdlock,unlock}$UNIX2003
#     aliases (libc_printf.a's xprintf.o genuinely needs all three), an
#     alias whose base symbol is undefined (real Apple's own
#     pthread_testcancel is genuinely absent from this project's
#     pthread port) crashes ld64 itself the same way the sleep() case
#     above does -- reproduced by bisecting this tier's alias list one
#     entry at a time (see git history / this script's own dev notes),
#     not guessed. Real fix, not a workaround: ncurses_extra_syscalls.c
#     defines pthread_testcancel() as a real no-op, verbatim reuse of
#     bash_syscalls.c's own identical body (that file's own comment: this
#     project genuinely has no pthread cancellation infrastructure, so a
#     no-op is the correct behavior, not a stub standing in for missing
#     logic) -- no alias needed for this one at all.
#   - setenv$UNIX2003/fopen$UNIX2003 aliases (present in sibling tiers'
#     smoketest boilerplate) are dropped entirely here: neither symbol is
#     actually referenced anywhere in this tier's real link (confirmed by
#     `nm -u` showing zero $-suffixed symbols anywhere in libncurses.a
#     itself, and a full from-scratch link without either alias resolving
#     clean otherwise) -- keeping unreferenced aliases whose base symbols
#     are also undefined is exactly the "undefined-base alias + reverse
#     rwlock alias" combination that crashes ld64 above, so they are
#     real, provable dead weight here, not just tidiness.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
NC="../../third_party/ncurses/ncurses"
OUT="libc_build"
DERIVED="$OUT/ncurses_derived"
mkdir -p "$OUT/apple_ncurses" "$DERIVED"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
for a in libc_locale libc_string libc_stdio libc_gen libc_printf libmalloc libplatform libpthread libunwind libclosure libmach_ipc; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run build_${a}.sh first (real dependency, confirmed via nm on the compiled ncurses objects, not assumed)" >&2
        exit 1
    fi
done
if [ ! -d "$NC/ncurses" ]; then
    echo "error: $NC not found -- run setup_third_party.sh first" >&2
    exit 1
fi

################################################################################
# Stage 1: real host-side codegen, reproducing Apple's own real Xcode build
# phases (bootstrap_headers.sh/bootstrap_sources.sh, native_execs.sh
# -target make_hash/make_keys, derived_headers.sh/derived_sources.sh) with
# host cc/awk. This is genuinely a HOST build step (produces portable C
# text or a native host tool, matching Apple's own native_execs.sh using
# `cc -sdk macosx.internal`, the HOST sdk) -- Stage 2 below is the only
# part that uses the Xcode-12/iPhoneOS cross toolchain.
################################################################################
CAPS="$NC/include/Caps"
AWK=awk

echo "ncurses: Stage 1 -- host-side derived headers/sources"

# curses.h (real header assembled from real fragments + a real generator
# script, per bootstrap_headers.sh/derived_headers.sh)
cat "$NC/include/curses.head" > "$DERIVED/curses.h"
sh "$NC/include/MKkey_defs.sh" "$CAPS" >> "$DERIVED/curses.h"
cat "$NC/include/curses.wide" >> "$DERIVED/curses.h"
cat "$NC/include/curses.tail" >> "$DERIVED/curses.h"

# hashsize.h / ncurses_def.h / parametrized.h / term.h / transform.h --
# real generator scripts + the real, checked-in ncurses_cfg.h.
sh "$NC/include/MKhashsize.sh" "$CAPS" > "$DERIVED/hashsize.h"
sh "$NC/include/MKncurses_def.sh" "$NC/include/ncurses_defs" > "$DERIVED/ncurses_def.h"
sh "$NC/include/MKparametrized.sh" "$CAPS" > "$DERIVED/parametrized.h"
"$AWK" -f "$NC/include/MKterm.h.awk" "$CAPS" > "$DERIVED/term.h"
cp "$NC/include/ncurses_cfg.h" "$DERIVED/ncurses_cfg.h"
sh "$NC/include/edit_cfg.sh" "$DERIVED/ncurses_cfg.h" "$DERIVED/term.h"
{
    echo '#define PROG_CAPTOINFO "captoinfo"'
    echo '#define PROG_INFOTOCAP "infotocap"'
    echo '#define PROG_RESET     "reset"'
    echo '#define PROG_INIT      "init"'
} > "$DERIVED/transform.h"

# names.c / keys.list (real generator scripts against the real Caps db)
"$AWK" -f "$NC/ncurses/tinfo/MKnames.awk" bigstrings=1 < "$CAPS" > "$DERIVED/names.c"
sh "$NC/ncurses/tinfo/MKkeys_list.sh" "$CAPS" | sort > "$DERIVED/keys.list"

# make_keys / make_hash: real native host tools (Apple's own
# native_execs.sh builds these via xcodebuild against the macosx.internal
# SDK; this checkout has no such SDK, so this script invokes the plain
# host cc directly on the exact same two real source files instead --
# same real recipe Makefile.in's own make_keys$(BUILD_EXEEXT)/
# make_hash$(BUILD_EXEEXT) rules use, $(BUILD_CC) being the host
# compiler). NCURSES_INTERNALS + the same MACROS as
# xcodescripts/native_execs.sh (needed for USE_WIDEC_SUPPORT's real wide
# section of curses.h -- see this script's own header comment).
NATIVE_MACROS="-DHAVE_CONFIG_H -U_XOPEN_SOURCE -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED -DNDEBUG -DSIGWINCH=28"
cc $NATIVE_MACROS -DNCURSES_INTERNALS \
    -I "$DERIVED" -I "$NC/ncurses" -I "$NC/ncurses/tinfo" -I "$NC/include" \
    -o "$DERIVED/make_keys" "$NC/ncurses/tinfo/make_keys.c"
"$DERIVED/make_keys" "$DERIVED/keys.list" > "$DERIVED/init_keytry.h"
cc $NATIVE_MACROS -DNCURSES_INTERNALS -DMAIN_PROGRAM \
    -I "$DERIVED" -I "$NC/ncurses" -I "$NC/ncurses/tinfo" -I "$NC/include" \
    -o "$DERIVED/make_hash" "$NC/ncurses/tinfo/comp_hash.c"

# codes.c / comp_captab.c / expanded.c / fallback.c / lib_gen.c /
# lib_keyname.c / unctrl.c: the remaining real derived_sources.sh steps.
# comp_captab.c's real MKcaptab.sh invokes "./make_hash" by relative
# path (same as Apple's own derived_sources.sh, which pushd's into
# $BUILT_PRODUCTS_DIR first) -- run from inside $DERIVED to match.
"$AWK" -f "$NC/ncurses/tinfo/MKcodes.awk" bigstrings=1 "$CAPS" > "$DERIVED/codes.c"
( cd "$DERIVED" && sh "../../$NC/ncurses/tinfo/MKcaptab.sh" "$AWK" 1 \
    "../../$NC/ncurses/tinfo/MKcaptab.awk" "../../$CAPS" > comp_captab.c )
INCDIR="-I$(pwd)/$DERIVED -I$(pwd)/$NC/ncurses -I$(pwd)/$NC/include"
sh "$NC/ncurses/tty/MKexpanded.sh" "cc -E" $NATIVE_MACROS $INCDIR > "$DERIVED/expanded.c"
sh "$NC/ncurses/tinfo/MKfallback.sh" /usr/share/terminfo "$NC/misc/terminfo.src" > "$DERIVED/fallback.c"
sh "$NC/ncurses/base/MKlib_gen.sh" "cc -E -DHAVE_CONFIG $NATIVE_MACROS $INCDIR" "$AWK" generated \
    < "$DERIVED/curses.h" > "$DERIVED/lib_gen.c"
"$AWK" -f "$NC/ncurses/base/MKkeyname.awk" bigstrings=1 "$DERIVED/keys.list" > "$DERIVED/lib_keyname.c"
echo | "$AWK" -f "$NC/ncurses/base/MKunctrl.awk" bigstrings=1 > "$DERIVED/unctrl.c"

echo "ncurses: Stage 1 done ($(ls "$DERIVED"/*.c | wc -l | tr -d ' ') derived .c files, $(ls "$DERIVED"/*.h | wc -l | tr -d ' ') derived .h files)"

################################################################################
# Stage 2: real cross-compile, Xcode-12/iPhoneOS toolchain, same
# Csu-static-target pattern every other tier uses.
################################################################################
# Same real flags every other tier uses (see build_libc_locale.sh's own
# header comment for the full reasoning behind each one) plus this
# tier's own real requirements: HAVE_CONFIG_H/NCURSES_INTERNALS (real
# Apple xcconfig-equivalent macros, see xcodescripts/native_execs.sh) and
# the _XOPEN_SOURCE(_EXTENDED)/NDEBUG/SIGWINCH macros that same real
# script passes, needed for USE_WIDEC_SUPPORT's wide-char section (this
# script's own header comment).
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h \
    -DHAVE_CONFIG_H -DNCURSES_INTERNALS -U_XOPEN_SOURCE -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED -DNDEBUG -DSIGWINCH=28 \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $DERIVED -I $NC/ncurses -I $NC/ncurses/tinfo -I $NC/include \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"

echo "ncurses: Stage 2 -- cross-compiling real Sources list"
ok=0
fail=0

# Real file-list resolution: the exact 164-name real Sources build phase
# (see this script's own header comment), 156 of them real physical files
# under $NC/ncurses (resolved once via `find`, not hand-typed -- avoids
# silently typo'ing a path across 156 entries), the other 8 the Stage 1
# derived .c files above.
GENERATED_NAMES="codes.c comp_captab.c expanded.c fallback.c lib_gen.c lib_keyname.c names.c unctrl.c"
SOURCES_LIST="$OUT/apple_ncurses/.sources_list"
python3 - "$NC/ncurses" "../../ncurses.xcodeproj/project.pbxproj" > "$SOURCES_LIST" <<'PYEOF'
import re, subprocess, sys, os
ncurses_dir, pbxproj_rel = sys.argv[1], sys.argv[2]
pbxproj = os.path.join(ncurses_dir, pbxproj_rel)
data = open(pbxproj).read()
m = re.search(r'4C29FC2C10225768001D8279 /\* Sources \*/ = \{(.*?)\n\t\t\};', data, re.S)
names = re.findall(r'[0-9A-F]{24} /\* (.*?) in Sources \*/,', m.group(1))
generated = {"codes.c","comp_captab.c","expanded.c","fallback.c","lib_gen.c","lib_keyname.c","names.c","unctrl.c"}
for n in names:
    if n in generated:
        continue
    found = subprocess.run(["find", ncurses_dir, "-name", n], capture_output=True, text=True).stdout.strip().split("\n")
    found = [f for f in found if f]
    if len(found) != 1:
        print("AMBIGUOUS_OR_MISSING:" + n, file=sys.stderr)
        sys.exit(1)
    print(found[0])
PYEOF

while IFS= read -r f; do
    base=$(basename "$f" .c)
    out="$OUT/apple_ncurses/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/apple_ncurses/${base}.err"; then
        ok=$((ok + 1)); rm -f "$OUT/apple_ncurses/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/apple_ncurses/${base}.err")"
    fi
done < "$SOURCES_LIST"
rm -f "$SOURCES_LIST"

for base in $GENERATED_NAMES; do
    f="$DERIVED/${base}"
    out="$OUT/apple_ncurses/$(basename "$base" .c).o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/apple_ncurses/$(basename "$base" .c).err"; then
        ok=$((ok + 1)); rm -f "$OUT/apple_ncurses/$(basename "$base" .c).err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/apple_ncurses/$(basename "$base" .c).err")"
    fi
done

# Real extra compat glue -- see this script's own header comment for the
# full real-gap-by-real-gap story.
if "$CLANG" $CFLAGS ncurses_extra_syscalls.c -o "$OUT/apple_ncurses/ncurses_extra_syscalls.o" 2>"$OUT/apple_ncurses/ncurses_extra_syscalls.err"; then
    ok=$((ok + 1)); rm -f "$OUT/apple_ncurses/ncurses_extra_syscalls.err"
else
    fail=$((fail + 1)); echo "  gap: ncurses_extra_syscalls -- $(grep -m1 'error:' "$OUT/apple_ncurses/ncurses_extra_syscalls.err")"
fi
if "$CLANG" $CFLAGS "$LIBC/sys/sigaction.c" -o "$OUT/apple_ncurses/Libc_sys_sigaction.o" 2>"$OUT/apple_ncurses/Libc_sys_sigaction.err"; then
    ok=$((ok + 1)); rm -f "$OUT/apple_ncurses/Libc_sys_sigaction.err"
else
    fail=$((fail + 1)); echo "  gap: Libc/sys/sigaction.c -- $(grep -m1 'error:' "$OUT/apple_ncurses/Libc_sys_sigaction.err")"
fi
# -D_DARWIN_C_SOURCE only for this one file -- see header comment.
if "$CLANG" $CFLAGS -D_DARWIN_C_SOURCE "../../third_party/libplatform/src/setjmp/generic/sigaction.c" -o "$OUT/apple_ncurses/libplatform_sigaction.o" 2>"$OUT/apple_ncurses/libplatform_sigaction.err"; then
    ok=$((ok + 1)); rm -f "$OUT/apple_ncurses/libplatform_sigaction.err"
else
    fail=$((fail + 1)); echo "  gap: libplatform/sigaction.c -- $(grep -m1 'error:' "$OUT/apple_ncurses/libplatform_sigaction.err")"
fi
for f in "$LIBC/stdlib/FreeBSD/abs.c" "$LIBC/stdlib/FreeBSD/atoi.c" simple_cmds_nanosleep_pub_compat.c; do
    base=$(echo "$f" | sed 's/[\/.]/_/g')
    if "$CLANG" $CFLAGS "$f" -o "$OUT/apple_ncurses/${base}.o" 2>"$OUT/apple_ncurses/${base}.err"; then
        ok=$((ok + 1)); rm -f "$OUT/apple_ncurses/${base}.err"
    else
        fail=$((fail + 1)); echo "  gap: $f -- $(grep -m1 'error:' "$OUT/apple_ncurses/${base}.err")"
    fi
done

echo "ncurses: $ok compiled, $fail gaps (see libc_build/apple_ncurses/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f "$OUT/libncurses.a"
ar rcs "$OUT/libncurses.a" "$OUT"/apple_ncurses/*.o
echo "wrote $OUT/libncurses.a"

################################################################################
# Real smoketest: links a tiny real initscr/printw/refresh/endwin consumer
# against this archive plus the real curated libc stack, same Csu-static
# pattern every sibling smoketest in this directory uses. Degrades to a
# skip (not a hard failure) if any prerequisite archive isn't built yet,
# matching every other tier's from-scratch-checkout-ordering convention.
#
# Deliberately NO "-Wl,-alias,___kill,_kill" below (unlike sibling
# smoketests' alias list): ncurses_extra_syscalls.c's own kill() already
# defines real plain _kill directly (lib_mouse.c/lib_tstp.c call it), so
# that alias would collide with a real duplicate symbol -- found by
# actually hitting ld's "duplicate symbol '_kill'" error the first time
# through, not guessed up front.
################################################################################
NEED_OK=1
for a in libc_string libc_stdio libc_gen libc_printf libc_locale_for_stdio libpthread libclosure libunwind libmalloc libplatform libmach_ipc; do
    [ -f "$OUT/$a.a" ] || NEED_OK=0
done
if [ "$NEED_OK" -eq 1 ] && [ -f ncurses_smoketest.c ]; then
    rm -f ncurses_smoketest.macho
    "$CLANG" $CFLAGS ncurses_smoketest.c -o "$OUT/ncurses_smoketest.o"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
        -o ncurses_smoketest.macho \
        ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
        "$OUT/ncurses_smoketest.o" "$OUT/libncurses.a" \
        "$OUT/libc_locale_for_stdio.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libc_printf.a" \
        "$OUT/libc_locale_for_stdio.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" \
        "$OUT/libmach_ipc.a" \
        "$OUT/libc_string.a" "$OUT/libunwind.a" "$OUT/libpthread.a" "$OUT/libclosure.a" \
        "$OUT/libmalloc.a" "$OUT/libplatform.a" \
        -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
        -Wl,-alias,___kill,'_kill$UNIX2003' \
        -Wl,-alias,_write,'_write$UNIX2003' \
        -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
        -Wl,-alias,___kdebug_trace,_kdebug_trace \
        -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003',_pthread_rwlock_wrlock \
        -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
        -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
        2>"$OUT/ncurses_smoketest_link.err" && echo "wrote ncurses_smoketest.macho" || echo "WARNING: smoketest link failed (libncurses.a itself is still built -- see $OUT/ncurses_smoketest_link.err)" >&2

    if [ -f ncurses_smoketest.macho ]; then
        otool -l ncurses_smoketest.macho | grep -q LC_LOAD_DYLIB && \
            echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
        otool -l ncurses_smoketest.macho | grep -q LC_UNIXTHREAD || \
            echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
        if nm -u ncurses_smoketest.macho | grep -q .; then
            echo "WARNING: undefined symbols present:" >&2
            nm -u ncurses_smoketest.macho >&2
        fi
    else
        tail -60 "$OUT/ncurses_smoketest_link.err" >&2
    fi
else
    echo "skip: ncurses_smoketest.macho (needs libc_gen/libc_printf/libpthread/libclosure/libunwind/libmach_ipc .a and ncurses_smoketest.c -- run their build_*.sh first)"
fi
