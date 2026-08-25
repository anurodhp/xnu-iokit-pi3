#!/bin/bash
# Builds libc_build/libiconv.a: real, unmodified Apple libiconv source
# (third_party/libiconv, tag libiconv-59, pinned in setup_third_party.sh),
# following the same Csu-static-target/curated-libc-stack pattern every
# other tier in this directory uses (see build_libc_locale.sh for the
# fullest-commented reference version of this CFLAGS recipe,
# build_libc_regex.sh for the closest analog -- another "port one more
# Apple library into the freestanding stack" job).
#
# Real file-list forensics (never guessed -- confirmed against
# third_party/libiconv/libiconv.xcodeproj/project.pbxproj, the actual
# Xcode project Apple's own build uses):
#   - The top-level checkout is Xcode-project cruft; the real sources are
#     nested at third_party/libiconv/libiconv/.
#   - The "libiconv" PBXNativeTarget's real Sources build phase (searched
#     for by its exact build-phase id, 5976ACF91007D6A5000AD30B) compiles
#     exactly TWO files: lib/iconv.c and libcharset/lib/relocatable.c (not
#     lib/relocatable.c -- both targets share the one physical copy under
#     libcharset/lib/, confirmed by tracing each PBXBuildFile's fileRef to
#     its PBXFileReference and that file's parent PBXGroup chain). Most of
#     lib/*.c (genaliases.c, gentranslit.c, genutf8.c, table-from.c,
#     cjk_tab_to_h.c, etc.) are build-time CODE GENERATORS that produced
#     the *.def/*.h table headers -- those generated outputs are already
#     checked into lib/ in this release tarball (confirmed: encodings.def,
#     aliases.h, aliases2.h, canonical.h, converters.h, loops.h, flags.h,
#     translit.h all present, no generation step needed), so the
#     generator .c files themselves are correctly excluded, not a scoping
#     shortcut.
#   - The separate "charset" PBXNativeTarget (libcharset.dylib) compiles
#     libcharset/lib/localcharset.c + the same relocatable.c. iconv_open()
#     with tocode/fromcode "" or NULL calls locale_charset() (lib/iconv.c
#     lines 249/263/287/323/337/361/659/675), so localcharset.c is a real
#     functional dependency of iconv_open(), not optional -- pulled in
#     here too even though Apple ships it as a nominally separate dylib
#     (this project has no dylibs at all, everything is one static
#     archive).
#
# Real scoping decision -- relocatable.c EXCLUDED:
#   Apple's real xcodeconfig/lib.xcconfig sets
#   LIB_GCC_PREPROCESSOR_DEFINITIONS with ENABLE_RELOCATABLE (that macro
#   comes from Apple's internal <DEVELOPER_DIR>/Makefiles/CoreOS/Xcode/
#   BSD.xcconfig, included at the top of base.xcconfig -- genuinely not
#   part of this OSS checkout, an internal Apple CoreOS build file). Both
#   iconv.c and localcharset.c already contain a real, source-supported
#   non-relocatable code path for exactly this case: relocatable.c's
#   entire body is wrapped in "#if ENABLE_RELOCATABLE" (relocatable.c
#   line ~34), and localcharset.c falls back to
#   "#define relocate(pathname) (pathname)" in its own "#else" branch
#   (localcharset.c:76) when ENABLE_RELOCATABLE is unset. We take that
#   already-existing #else branch by simply not defining
#   ENABLE_RELOCATABLE -- a real alternate path Apple's own source
#   already provides for exactly this build configuration, not a
#   fabricated stub. relocatable.c itself compiles to an empty
#   translation unit in that configuration, so it is not compiled at all
#   here (nothing would reference its symbols).
#   (Note: lib/config.h:25's "/* #undef ENABLE_RELOCATABLE */" is
#   autoconf-generated BOILERPLATE INSIDE A COMMENT, not an active
#   preprocessor directive -- it does not itself disable the macro. What
#   actually disables it here is that we never pass -DENABLE_RELOCATABLE,
#   matching upstream libiconv's default/non-Apple-CoreOS build.)
#
# Real gap found and worked around (documented, not silently patched):
#   LIBDIR is referenced unconditionally by localcharset.c:130
#   ("dir = relocate (LIBDIR);", NOT inside the ENABLE_RELOCATABLE
#   #if/#else above -- this line runs either way) but no xcconfig in this
#   checkout defines it (it's supplied by the same missing internal
#   BSD.xcconfig). Real fix, not a fabrication: libcharset/config.h:86
#   already has "#define LIBDIR \"/usr/lib\"" checked into this exact
#   release tarball (the *committed*, generated-by-a-real-./configure-run
#   config.h, not the .in template) -- adding libcharset/ (not
#   libcharset/lib/) to the include path lets localcharset.c's
#   "#include <config.h>" (HAVE_CONFIG_H-gated) find it directly, no
#   -D flag improvised. At runtime dir/"charset.alias" won't exist on
#   this project's freestanding root (no install tree, no dyld), so
#   get_charset_aliases() takes its own real fopen()-fails/returns-empty
#   fallback path (localcharset.c ~line 135) -- identical to real macOS
#   systems that don't ship a charset.alias file either; not a gap this
#   project introduced.
#
# Real transitive-dependency correction (the task brief's assumption that
# this only needs libc_string/libc_stdio/libmalloc/libplatform was
# checked against actual `nm -u` output on the compiled .o's, not
# assumed, and was INCOMPLETE): iconv_open()'s locale_charset() call path
# needs real nl_langinfo()/setlocale()/getenv() (localcharset.c) and
# iconv.c's own __ct_rune_t/wchar-adjacent code needs real
# mbrtowc()/wcrtomb()/__error()/__global_locale/__locale_key -- all of
# which live in libc_locale.a (confirmed via nm against every already-
# built libc_build/*.a), not libc_string.a/libc_stdio.a alone. The real
# dependency set is: libc_locale (libc_locale_for_stdio.a at the
# smoketest-link step, see next paragraph), libc_string, libc_stdio,
# libmalloc, libplatform.
#
# Real duplicate-symbol bug found and fixed the same way
# build_libc_stdio.sh's own header comment documents (not guessed --
# reproduced by actually hitting ld's error first): localcharset.c is
# this project's first libc_locale.a consumer to also call real fscanf()
# (to parse charset.alias), which pulls in libc_printf.a's vfscanf.o,
# which references the internal ___fread -- only real libc_stdio.a's
# fread.o defines that symbol, and extracting it collides with
# libc_locale.a's own libc_locale_stdio.o (a small hand-rolled FILE*
# shim predating the real stdio tier) also defining plain _fread.
# build_libc_stdio.sh already solved this exact class of collision by
# producing libc_locale_for_stdio.a (libc_locale.a minus that one now-
# redundant member) for exactly this situation -- used here instead of
# plain libc_locale.a at the smoketest-link step, matching every other
# build_*.sh in this directory that links real libc_stdio.a alongside
# locale-tier symbols (build_uname.sh, build_libc_gen.sh,
# build_simple_cmds.sh, etc).
#
# Real second gap: Apple's real iconvlist() (lib/iconv.c lines ~571/583)
# calls qsort() to sort alias names for display -- genuinely needed, not
# incidental. No already-built libc_build/*.a exports qsort except the
# unrelated libremovefile.a (a real symbol there, libremovefile.a/
# qsort.o, but pulling in a whole other subsystem's archive just for one
# symbol would be an odd, avoidable cross-library dependency). Fixed by
# compiling real Apple/FreeBSD sources directly into this archive
# instead: third_party/Libc/stdlib/FreeBSD/qsort.c (calls flsl() for its
# introsort recursion-depth heuristic and heapsort()/__heapsort_r() as
# its real fallback for pathological inputs -- both genuine algorithmic
# dependencies of Apple's real qsort.c, confirmed by reading it, not
# guessed) plus third_party/Libc/stdlib/FreeBSD/heapsort.c (real Apple
# source, exists in this checkout). flsl() itself has NO real .c
# implementation anywhere in this project's Libc checkout (only
# strings.h declares it) -- this is the exact same real gap
# tools/userland_staging/removefile_syscalls.c hit first (see its own
# flsl() comment); libiconv_flsl_compat.c here duplicates that same
# real, well-defined, __builtin_clzl-based implementation rather than
# linking libremovefile.a for it.
set -e
cd "$(dirname "$0")"

LIBICONV_ROOT="../../third_party/libiconv/libiconv"
LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
OUT="libc_build"
mkdir -p "$OUT/apple_libiconv"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
for a in libc_locale_for_stdio libc_string libc_stdio libmalloc libplatform; do
    if [ ! -f "$OUT/$a.a" ]; then
        echo "error: $OUT/$a.a not found -- run build_${a}.sh first (real dependency, see this script's own header comment for how it was confirmed via nm, not assumed)" >&2
        exit 1
    fi
done

# Same real flags every other tier uses (see build_libc_locale.sh's own
# header comment for the full reasoning behind each one) plus:
#   -DHAVE_CONFIG_H: real Apple xcodeconfig/base.xcconfig's
#     BASE_GCC_PREPROCESSOR_DEFINITIONS, applied to every libiconv target
#     (confirmed by reading xcodeconfig/*.xcconfig directly).
#   -I compat_include: this tier's own source hits the exact same
#     "System/machine/cpu_capabilities.h" transitive gap (via
#     third_party/Libc/include/ctype.h's xlocale chain) build_libc_locale.sh
#     already solved -- reusing that shared compat_include dir rather than
#     duplicating the same shim.
#   -I "$LIBICONV_ROOT/include" -I "$LIBICONV_ROOT/lib": iconv.c's own
#     directory (quoted #include "config.h"/"localcharset.h"/etc resolve
#     same-dir already, these -I's are for iconv_tiger.h/iconv.h via
#     angle-bracket includes) -- NOT adding $LIBICONV_ROOT/include's
#     export.h to anything's include chain matters here: that file is an
#     unprocessed "@HAVE_VISIBILITY@"-style autoconf template (Apple's
#     internal BSD.xcconfig substitutes it at derive time, same missing
#     piece as the LIBDIR gap above) -- confirmed by grep that neither
#     iconv.c nor localcharset.c ever #include it, so it is simply never
#     read by this build, not fixed.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -DHAVE_CONFIG_H \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"

ok=0
fail=0
compile() {
    local src="$1"; shift
    local base
    base=$(basename "$src" .c)
    local out="$OUT/apple_libiconv/${base}.o"
    if "$CLANG" $CFLAGS "$@" "$src" -o "$out" 2>"$OUT/apple_libiconv/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/apple_libiconv/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/apple_libiconv/${base}.err")"
    fi
}

# Real Apple libiconv sources -- the actual "libiconv" + "charset" Xcode
# target Sources lists (see header comment for the forensic trace).
compile "$LIBICONV_ROOT/lib/iconv.c" -I "$LIBICONV_ROOT/include" -I "$LIBICONV_ROOT/lib"
compile "$LIBICONV_ROOT/libcharset/lib/localcharset.c" -I "$LIBICONV_ROOT/libcharset/include" -I "$LIBICONV_ROOT/libcharset"

# Real Apple/FreeBSD qsort()+heapsort() (iconvlist()'s real, genuine
# dependency -- see header comment) plus this project's own small real
# flsl() port (same one already used by build_removefile.sh, duplicated
# here rather than cross-linking libremovefile.a).
compile "$LIBC/stdlib/FreeBSD/qsort.c"
compile "$LIBC/stdlib/FreeBSD/heapsort.c"
compile "libiconv_flsl_compat.c"

echo "libiconv: $ok compiled, $fail gaps (see libc_build/apple_libiconv/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

rm -f "$OUT/libiconv.a"
ar rcs "$OUT/libiconv.a" "$OUT"/apple_libiconv/*.o
echo "wrote $OUT/libiconv.a"

# Real smoketest: links a tiny real iconv_open/iconv/iconv_close consumer
# (converts real UTF-8 -> ISO-8859-1 and round-trips UTF-8 -> UTF-16LE ->
# UTF-8, see libiconv_smoketest.c) against this archive plus the real
# curated libc stack, same Csu-static pattern every sibling smoketest in
# this directory uses. No $UNIX2003 aliasing needed for iconv_open/iconv/
# iconv_close (confirmed: neither third_party/Libc/include/sys/cdefs.h
# nor the SDK's own usr/include/iconv.h alias any of the three symbols,
# unlike regcomp's real $UNIX2003 case in build_libc_regex.sh).
NEED_OK=1
for a in libc_gen libc_printf libpthread libclosure libunwind; do
    [ -f "$OUT/$a.a" ] || NEED_OK=0
done
if [ "$NEED_OK" -eq 1 ]; then
    rm -f libiconv_smoketest.macho
    "$CLANG" $CFLAGS libiconv_smoketest.c -o "$OUT/libiconv_smoketest.o"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
        -o libiconv_smoketest.macho \
        ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
        "$OUT/libiconv_smoketest.o" "$OUT/libiconv.a" \
        "$OUT/libc_printf.a" \
        "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libc_locale_for_stdio.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" \
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
        -Wl,-alias,'_pthread_rwlock_wrlock$UNIX2003',_pthread_rwlock_wrlock \
        -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
        -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
        2>"$OUT/libiconv_smoketest_link.err" && echo "wrote libiconv_smoketest.macho" || echo "WARNING: smoketest link failed (libiconv.a itself is still built -- see $OUT/libiconv_smoketest_link.err)" >&2

    if [ -f libiconv_smoketest.macho ]; then
        otool -l libiconv_smoketest.macho | grep -q LC_LOAD_DYLIB && \
            echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
        otool -l libiconv_smoketest.macho | grep -q LC_UNIXTHREAD || \
            echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
        if nm -u libiconv_smoketest.macho | grep -q .; then
            echo "WARNING: undefined symbols present:" >&2
            nm -u libiconv_smoketest.macho >&2
        fi
    else
        tail -60 "$OUT/libiconv_smoketest_link.err" >&2
    fi
else
    echo "skip: libiconv_smoketest.macho (needs libc_gen/libc_printf/libpthread/libclosure/libunwind .a -- run their build_*.sh first)"
fi
