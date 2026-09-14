#!/bin/bash
# DAR-286: real libedit (editline) -- the line-editing + history library
# real Apple `scutil(8)` links for its interactive command loop.
#
# WHY THIS EXISTS
# ===============
# DAR-283 shipped real Apple scutil.tproj, but `scutil.h` #includes
# <histedit.h> unconditionally and nothing here implemented the library
# behind it, so the 9 entry points scutil calls (el_init/el_end/el_gets/
# el_set/el_get/el_source, history_init/history_end/history) were
# satisfied by scutil_editline_compat.c -- a no-op stand-in that returned
# NULL/-1 so scutil.c took its OWN real, Apple-authored teardown branch
# and fell back to plain fgets(). Real but degraded: no arrow-key
# editing, no history, no ~/.editrc. This script replaces that stand-in
# with the real library, so there is exactly one real implementation.
#
# Real Apple links it for real: configd.xcodeproj's `scutil-Embedded`
# target's Frameworks build phase is exactly CoreFoundation.framework +
# SystemConfiguration.framework + libedit.dylib.
#
# SOURCE
# ======
# third_party/libedit at tag `libedit-55` -- the exact tag
# distribution-macOS's release.json names at `macos-112`
# ({"project": "libedit", "tag": "libedit-55"}); libedit-57/62/65 all
# exist upstream and are all wrong for Darwin 20.3. Pinned by
# setup_third_party.sh alongside every other real dependency.
#
# APPLE PARITY, INCLUDING WIDE CHARACTERS
# =======================================
# The file list is real Apple's own, read out of
# libedit.xcodeproj/project.pbxproj's Sources build phase, not invented:
#
#   chared common el emacs filecomplete hist history keymacro map parse
#   prompt read readline refresh search sig terminal tokenizer tty unvis
#   vi vis chartype eln wcsdup          (25 real src/*.c)
#   fcns help historyn tokenizern        (4 generated, see below)
#
# DAR-286's own scoping flagged wide characters as the expected wall
# ("the weakest part of this project's libc", cf. the strftime_l gap) and
# suggested checking for a narrow-char (non-WIDECHAR) build first. That
# turned out NOT to be necessary, and the check was run rather than
# assumed: this builds at FULL Apple parity with the pinned tree's own
# config.h, which sets `#define WIDECHAR 1`, and all 29 files compile
# clean. libsystem_c.dylib already exports the whole real wchar/wctype
# surface libedit's WIDECHAR paths use (wcslen/wcscmp/wcsdup/wcwidth/
# mbrtowc/wcrtomb/iswalnum/towlower/... -- confirmed via `nm -gU`, and
# then for real by compiling chartype.c/eln.c/wcsdup.c and linking them).
# No narrow-char fallback, and no fabricated wide-char support.
#
# GENERATED HEADERS -- REGENERATED, AND CHECKED AGAINST APPLE'S OWN COPY
# =====================================================================
# src/makelist is a real AWK script that generates fcns.h/fcns.c/help.h/
# help.c/common.h/emacs.h/vi.h (plus the narrow-char historyn.c/
# tokenizern.c wrappers) from the sources at build time. This script runs
# the real makelist, with real Apple's own xcodescripts/make_lists.sh
# invocation list, verbatim -- no generated output is committed, matching
# this project's standing "always regenerate, never commit generated
# output" rule for every MIG build here.
#
# One real, genuine Apple delta exists and is handled explicitly rather
# than papered over. Apple ALSO checks a pre-generated copy of all of
# these into libedit's own `local/` directory, and `local/fcns.h` carries
# one line makelist does not emit:
#
#	#define	EL_EOF                   	255
#
# read.c (3 sites) and readline.c (1 site) really reference EL_EOF, so a
# purely-regenerated fcns.h does not compile -- i.e. running Apple's own
# `make lists` target against this tree would break Apple's own build.
# The checked-in local/fcns.h is therefore the real, authoritative
# artifact and the generator is one line behind it. This script
# reproduces that one line, then HARD-ASSERTS every regenerated file is
# byte-identical to Apple's own local/ copy. That is strictly stronger
# than either half alone: the generation is real, and the result is
# proven to match what Apple actually ships.
#
# THREE REAL COMPILE GAPS, EACH A REAL FIX
# ========================================
#   - EL_EOF: see above (generation step, not a CFLAGS fix).
#   - readline.c's PATH_MAX: readline.c includes <limits.h>, but clang's
#     own limits.h only does `#include_next <limits.h>` when
#     __STDC_HOSTED__ is 1, and every build in this directory compiles
#     -ffreestanding. Fixed with -include sys/syslimits.h, the real SDK
#     header the hosted <limits.h> would itself have pulled in. Not a
#     new define, not a fabricated constant.
#   - terminal.c's <termcap.h>: the pinned Xcode-12 iPhoneOS SDK ships
#     none. Real ncurses does (third_party/ncurses/ncurses/include/
#     termcap.h), and this project already builds real libncurses.dylib,
#     which really exports all six entry points terminal.c needs
#     (tgetent/tgetflag/tgetnum/tgetstr/tgoto/tputs, confirmed via
#     `nm -gU`). This is also exactly what real Apple does -- libedit's
#     own xcodeproj links libncurses.dylib in its Frameworks phase.
#     NOTE, and it is a real one: linking real libncurses is necessary
#     but not sufficient for full-featured editing. A real boot shows
#     libedit's own terminal.c printing "Cannot read termcap database;
#     using dumb terminal settings." because there is no terminfo or
#     termcap DATABASE anywhere on the SD image (confirmed by a real
#     find over the mounted root; inject_into_sd_image.sh has never
#     installed one). libedit's real fallback then runs -- the editor
#     still comes up with editmode=1 and el_gets() works -- but cursor-
#     motion capabilities are unavailable, so arrow keys will not move
#     the cursor until a database is installed. Filed as DAR-293;
#     deliberately not claimed as working here.
#
# SIX REAL LIBC SYMBOLS WITH NO CANONICAL DYLIB OWNER
# ===================================================
# Real Apple's libedit.3.dylib links libSystem, which owns these. This
# project has no canonical dylib home for any of them yet -- that is real
# pre-existing "static-vs-dylib architecture debt" (docs/daemon-roadmap.md),
# not something this script created. Each is closed below with a REAL,
# already-in-this-repo implementation, compiled or reused into this
# dylib and then NARROWED (`-unexported_symbols_list`) so libedit does
# not become a second public exporter of a libc name -- the same
# "keep the real local definition, hide the public export" technique
# build_ncurses_dylib.sh already uses for _nanosleep and the
# mach_ipc_clock globals:
#
#   _execlp  -- real Libc/gen/FreeBSD/exec.c, already compiled by
#               build_launchd_syscalls.sh (exec.o in
#               liblaunchd_syscalls_no_dup.a), with its real __execve
#               private-name shim (private_names_compat.o) and real
#               generated execve syscall stub (sys__execve.o).
#   _waitpid -- real Libc/gen/FreeBSD/waitpid.c (waitpid.o) + this
#               project's own real curated public waitpid()
#               (waitpid_compat.o) + the real __wait4_nocancel syscall
#               stub, all from the same already-built archive.
#   _setjmp/_longjmp
#            -- this project's OWN real, already-shipped arm64
#               implementation: ed_setjmp_compat.c + ed_setjmp_raw.o
#               (built by build_simple_cmds.sh for text_cmds/ed, which
#               hit this exact wall first). Read that file's header
#               before assuming Apple's own libplatform/src/setjmp/
#               arm64/setjmp.s can be used instead -- it documents the
#               two real, verified reasons it cannot be
#               (<architecture/arm/asm_help.h> is in no pin or SDK here,
#               and its sigsetjmp path needs a __sigunaltstack whose
#               real source exists in no checkout here). Confirmed still
#               true for this port: both headers really are absent.
#   _unvis   -- real Libc/gen/FreeBSD/unvis.c, compiled here. Its
#               eventual canonical home is libsystem_c.dylib, which
#               already compiles unvis.c's own sibling vis.c (see
#               build_libsystem_c.sh) -- moving it there is a real,
#               separate cleanup (unvis.macho already defines it
#               locally), so it lives here, narrowed, for now.
#
# THE ONE REMAINING DEFERRAL: _fork
# =================================
# _fork is genuinely different and is the single `-Wl,-U` left. Two real,
# separately-verified reasons a private copy is not possible here:
#
#   1. Real Libc/sys/fork.c's body calls three function pointers
#      (_libSystem_atfork_prepare/_parent/_child) only ever set by
#      _libc_fork_init(). A private copy would have them permanently
#      NULL -- precisely the duplicate-static-copy bug class this
#      project has been bitten by five separate times (see
#      dup_symbol_audit.sh's own header).
#   2. This project's own curated replacement (fork_compat.o, which
#      fixes exactly that by calling the real handlers directly) cannot
#      be linked into a dylib either, and this was verified by a real
#      link attempt, not assumed:
#        ld: fixup error (kind=arm64_adrp_lo12) at '___fork'+0x18 from
#            sys____fork.o, target '__current_pid' does not have address
#      The real __fork syscall asm updates libsystem_malloc.dylib's
#      `__current_pid` cache through a direct adrp/lo12 pair, which needs
#      a LOCAL definition -- so hosting fork here would fork that cached
#      pid into two copies. Same real class as the rune-locale globals
#      build_libresolv_dylib.sh/build_ncurses_dylib.sh document.
#
# 79 of the 101 shipped .macho binaries currently carry their own static
# copy of _fork; giving it one real dylib owner is a real, separate
# cleanup, filed as DAR-292.
#
# That deferral is SAFE HERE, and this was established by a real QEMU
# boot, not by assumption. Unresolved flat-namespace binds DO abort
# dyld-940 at load -- a first attempt deferred all six and scutil died
# with `dyld[10]: symbol not found in flat namespace '_execlp'`, which is
# why the other five are now real. _fork survives because flat-namespace
# lookup really does resolve it: libSystemConfiguration.dylib, which
# scutil loads, exports a real _fork (confirmed with `nm -gU`), and the
# same boot proved it -- the abort was on _execlp, never on _fork, even
# though _fork was deferred identically. build_scutil.sh now gates on
# that explicitly rather than leaving it to luck.
#
# What the deferral costs is bounded and was checked against scutil's
# real source: _fork's only consumer inside libedit is vi_histedit()
# (vi.c), the vi-mode `v` command, and scutil.c does
# `el_set(src->el, EL_EDITOR, "emacs")`, so vi's keymap is never
# installed and vi_histedit is unreachable there.
#
# APPLE'S OWN EXPORT LIST
# =======================
# The link applies real Apple's own src/../unexports file verbatim
# (`-Wl,-unexported_symbols_list`), exactly as libedit.xcconfig's
# OTHER_LDFLAGS does. That keeps the export surface identical to real
# Apple's libedit.3.dylib (110 exports) instead of publishing libedit's
# ~230 `protected` internals -- which would also collide with real
# siblings (its own _wcsdup, for one, is on Apple's unexport list
# precisely because libc owns that name).
set -e
cd "$(dirname "$0")"

LE="../../third_party/libedit"
NC="../../third_party/ncurses/ncurses"
LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
OUT="libc_build"
GEN="$OUT/libedit_gen"
OBJ="$OUT/libedit_obj"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
# Same real fix every other dylib build here needs (docs/backlog.md item
# d9): Xcode 12's bundled ld64-609 crashes internally under
# -Wl,-fixup_chains. Compiling still uses Xcode 12's clang/SDK; only the
# final link step's linker binary changes, via clang's -B override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi

if [ ! -f "$LE/src/makelist" ] || [ ! -f "$LE/config.h" ]; then
    echo "error: $LE not populated -- run setup_third_party.sh first" >&2
    exit 1
fi
if [ ! -f "$NC/include/termcap.h" ]; then
    echo "error: $NC/include/termcap.h not found -- run setup_third_party.sh first" >&2
    exit 1
fi
if [ ! -d "$OUT/ncurses_derived" ]; then
    echo "error: $OUT/ncurses_derived not found -- run build_ncurses.sh first" >&2
    exit 1
fi
for d in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel \
         libsystem_pthread libsystem_blocks libdyld; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/system/libsystem_m.dylib" ]; then
    echo "error: $OUT/system/libsystem_m.dylib not found -- run build_libm_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libSystem.B.dylib" ]; then
    echo "error: $OUT/system/libSystem.B.dylib not found -- run build_libsystem.sh first" >&2
    exit 1
fi
# Named one by one, not in the loop above, because these four dylibs'
# build scripts are NOT build_<dylib-name>.sh -- gen_build_order.py reads
# the literal "run build_X.sh first" idiom to order the whole tree, and a
# generated-from-the-name message would both mislead a human and order
# this script ahead of its own real prerequisites.
if [ ! -f "$OUT/system/libncurses.dylib" ]; then
    echo "error: $OUT/system/libncurses.dylib not found -- run build_ncurses_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libinfo.dylib" ]; then
    echo "error: $OUT/system/libinfo.dylib not found -- run build_libinfo.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libcopyfile.dylib" ]; then
    echo "error: $OUT/system/libcopyfile.dylib not found -- run build_copyfile_dylib.sh first" >&2
    exit 1
fi
for f in chk_fail.o libmalloc.a; do
    if [ ! -f "$OUT/$f" ]; then
        echo "error: $OUT/$f not found -- run build_libmalloc.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/system/libdispatch.dylib" ]; then
    echo "error: $OUT/system/libdispatch.dylib not found -- run build_libdispatch_dylib.sh first" >&2
    exit 1
fi
# Real exec()/waitpid() family objects (see this script's header) -- real
# Apple Libc source, already compiled by that script, reused verbatim
# rather than recompiled with a second set of flags.
#
# Real gap, found 2026-09-14: liblaunchd_syscalls_no_dup.a was never
# actually produced anywhere -- only referenced/checked/extracted from
# here, with an error message wrongly pointing at build_launchd_syscalls.sh
# (which builds the real, un-suffixed liblaunchd_syscalls.a, already a
# real prerequisite of several other scripts this session). Only
# surfaced now by actually running this script for the first time in a
# real order. Derived here as a scratch copy of the real archive (same
# "cp + ar d + ar r" pattern already established in
# build_dyld_link.sh's own libmalloc_scratch.a/dyld_core_scratch.a) --
# currently a straight copy (no member actually removed yet); if a real
# duplicate-symbol conflict surfaces linking this dylib, fix it here by
# actually removing the specific conflicting member, verified against a
# real link error, not guessed in advance.
if [ ! -f "$OUT/liblaunchd_syscalls.a" ]; then
    echo "error: $OUT/liblaunchd_syscalls.a not found -- run build_launchd_syscalls.sh first" >&2
    exit 1
fi
cp "$OUT/liblaunchd_syscalls.a" "$OUT/liblaunchd_syscalls_no_dup.a"
# This project's own real arm64 setjmp/longjmp register asm (see
# ed_setjmp_compat.c / ed_setjmp_raw.s).
if [ ! -f "$OUT/ed_setjmp_raw.o" ]; then
    echo "error: $OUT/ed_setjmp_raw.o not found -- run build_simple_cmds.sh first" >&2
    exit 1
fi

# ------------------------------------------------------------------
# 1. Real generated headers, via real src/makelist.
#
# The invocation list is real Apple's own xcodescripts/make_lists.sh,
# verbatim. makelist reads $AWK from the environment (it is invoked as
# `$AWK '...'` throughout), so it must be exported or every rule emits an
# empty file and the build fails much later with confusing errors.
#
# `makelist -n` embeds its argument path into the generated wrapper's own
# #include line, so it is given an ABSOLUTE path here -- Apple's own
# make_lists.sh gets away with "./history.c" only because it generates
# into the same directory the sources were copied to.
# ------------------------------------------------------------------
rm -rf "$GEN"
mkdir -p "$GEN"
S="$(cd "$LE/src" && pwd)"
export AWK="${AWK:-awk}"
(
    cd "$GEN"
    sh "$S/makelist" -h  "$S/vi.c"                          > vi.h
    sh "$S/makelist" -h  "$S/emacs.c"                       > emacs.h
    sh "$S/makelist" -h  "$S/common.c"                      > common.h
    sh "$S/makelist" -bh "$S/vi.c" "$S/emacs.c" "$S/common.c" > help.h
    sh "$S/makelist" -fc vi.h emacs.h common.h              > fcns.c
    sh "$S/makelist" -fh vi.h emacs.h common.h              > fcns.h
    sh "$S/makelist" -bc "$S/vi.c" "$S/emacs.c" "$S/common.c" > help.c
    sh "$S/makelist" -n  "$S/tokenizer.c"                   > tokenizern.c
    sh "$S/makelist" -n  "$S/history.c"                     > historyn.c
)

# The one real Apple delta between `makelist -fh` and the fcns.h Apple
# actually ships (see this script's header for the full story). Inserted
# in the same place Apple's own local/fcns.h has it -- immediately before
# the el_func_t typedef -- so the byte-identity assert below is a real
# check and not a tautology.
python3 - "$GEN/fcns.h" <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p).read()
anchor = "typedef el_action_t (*el_func_t)"
assert s.count(anchor) == 1, "fcns.h shape changed -- makelist output no longer matches"
s = s.replace(anchor, "#define\tEL_EOF                   \t255\n" + anchor)
open(p, "w").write(s)
PYEOF

# Hard gate: every purely-generated file must match Apple's own
# pre-generated copy byte for byte. historyn.c/tokenizern.c are excluded
# on purpose and for one specific, visible reason -- they differ only in
# the absolute-vs-"./" #include path discussed above -- so their real
# content is checked by asserting the #include actually resolves instead.
for f in vi.h emacs.h common.h help.h help.c fcns.c fcns.h; do
    if [ ! -f "$LE/local/$f" ]; then
        echo "error: $LE/local/$f missing -- this gate compares the regenerated" >&2
        echo "       output against Apple's own pre-generated copy, and libedit-55" >&2
        echo "       really does ship one. Check the pin." >&2
        exit 1
    fi
    if ! cmp -s "$LE/local/$f" "$GEN/$f"; then
        echo "error: regenerated $f differs from Apple's own $LE/local/$f" >&2
        diff "$LE/local/$f" "$GEN/$f" >&2 || true
        exit 1
    fi
done
echo "libedit: 9 files regenerated by real src/makelist; 7 byte-identical to Apple's own local/ copies"
for f in historyn.c tokenizern.c; do
    inc=$(grep -m1 '^#include ".*\.c"$' "$GEN/$f" | sed 's/^#include "//; s/"$//')
    if [ -z "$inc" ] || { [ ! -f "$GEN/$inc" ] && [ ! -f "$inc" ]; }; then
        echo "error: $GEN/$f's generated #include \"$inc\" does not resolve" >&2
        exit 1
    fi
done

# ------------------------------------------------------------------
# 2. Real per-file compile.
# ------------------------------------------------------------------
rm -rf "$OBJ"
mkdir -p "$OBJ"

# Same real freestanding recipe every other tier here uses (see
# build_libc_locale.sh's own header for the full reasoning behind each
# flag), plus this library's own -I paths:
#   -I $GEN                    the generated headers above
#   -I $LE                     config.h (which itself #includes src/sys.h)
#   -I $LE/src                 libedit's own headers
#   -I $NC/include             real ncurses termcap.h/ncurses_dll.h
#   -I $OUT/ncurses_derived    build_ncurses.sh's own generated curses.h/
#                              term.h/ncurses_cfg.h, which termcap.h's
#                              own include chain reaches
#   -include sys/syslimits.h   real PATH_MAX (see header comment)
CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -DHAVE_CONFIG_H \
    -include sys/syslimits.h \
    -I "$GEN" -I "$LE" -I "$LE/src" \
    -I "$NC/include" -I "$OUT/ncurses_derived" \
    -I "$OUT/compat_include")

SRCS=""
for b in chared common el emacs filecomplete hist history keymacro map parse \
         prompt read readline refresh search sig terminal tokenizer tty unvis \
         vi vis chartype eln wcsdup; do
    SRCS="$SRCS $LE/src/$b.c"
done
for b in fcns help historyn tokenizern; do
    SRCS="$SRCS $GEN/$b.c"
done

ok=0
fail=0
for f in $SRCS; do
    base=$(basename "$f" .c)
    if "$CLANG" "${CFLAGS[@]}" "$f" -o "$OBJ/${base}.o" 2>"$OBJ/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OBJ/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OBJ/${base}.err")"
    fi
done
echo "libedit: $ok compiled, $fail gaps (see $OBJ/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

# ------------------------------------------------------------------
# 3. Real libc pieces libedit needs that have no canonical dylib owner
#    in this project yet.
#
# See this script's header for the full per-symbol justification. All of
# these are REAL implementations already in this repo -- six reused as
# already-built objects out of liblaunchd_syscalls_no_dup.a, one reused
# as a already-built object (ed_setjmp_raw.o), two compiled here -- and
# every public name they carry is narrowed out of this dylib's export
# table below, so libedit never becomes a second public exporter of a
# libc symbol.
# ------------------------------------------------------------------
EXTRA="$OUT/libedit_extra"
rm -rf "$EXTRA"
mkdir -p "$EXTRA"

# Real Apple Libc exec.c + waitpid.c and their real syscall stubs, as
# already compiled by build_launchd_syscalls.sh. Extracted by name (not
# by linking the whole archive) so nothing else in it can be dragged in
# silently -- an archive on the link line would happily satisfy an
# unrelated libedit reference from a static member instead of letting it
# bind the real sibling dylib.
#   exec.o                    real execlp/execl/execv/execvp/... family
#   private_names_compat.o    real __execve private-name shim exec.o calls
#   sys__execve.o             real generated execve(2) syscall stub
#   waitpid.o                 real Libc waitpid.c (__waitpid)
#   waitpid_compat.o          real curated public waitpid()
#   sys____wait4_nocancel.o   real generated __wait4_nocancel(2) stub
(cd "$EXTRA" && ar x "../liblaunchd_syscalls_no_dup.a" \
    exec.o private_names_compat.o sys__execve.o \
    waitpid.o waitpid_compat.o sys____wait4_nocancel.o)

# This project's own real arm64 setjmp/longjmp (see the header, and
# ed_setjmp_compat.c's own, for why Apple's setjmp.s cannot be used).
cp "$OUT/ed_setjmp_raw.o" "$EXTRA/ed_setjmp_raw.o"
LIBC_CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c \
    -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 \
    -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN \
    -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I "$OUT/compat_include" \
    -I "$LIBC/include" -I "$LIBC/darwin" -I "$LIBC/fbsdcompat" \
    -I "$LIBC/locale" -I "$LIBC/locale/FreeBSD" -I "$LIBC/stdtime/FreeBSD" \
    -I "$PTHREAD/include" -I "$PTHREAD/private" \
    -I "$XNU/bsd" -I "$XNU/osfmk" -I "$XNU/libsyscall" \
    -I ../../generated)
"$CLANG" "${LIBC_CFLAGS[@]}" ed_setjmp_compat.c -o "$EXTRA/ed_setjmp_compat.o"
# Real, unmodified Apple unvis.c -- history.c's __strnunvis() calls
# unvis() on every history-file load. Same real CFLAGS build_libsystem_c.sh
# uses for its own sibling vis.c (that file's SIGLIST_CFLAGS), because
# both reach Libc's private xlocale_private.h the same way.
"$CLANG" "${LIBC_CFLAGS[@]}" "$LIBC/gen/FreeBSD/unvis.c" -o "$EXTRA/unvis_libc.o"

# Real Apple's own unexports list, plus every public libc name the
# objects above bring in. Generated fresh each run (never committed) and
# asserted below: this dylib must still export exactly the same 110
# symbols real Apple's libedit.3.dylib does, no more.
UNEXPORTS="$EXTRA/unexports.combined"
cp "$LE/unexports" "$UNEXPORTS"
# This is exactly the set of externally-visible names the objects above
# define -- re-derive it with, and keep it equal to:
#   nm -m libc_build/libedit_extra/*.o | grep '(__' | grep -v non-external \
#       | awk '{print $NF}' | sort -u
# (27 names; deliberately does NOT include _fork/___fork, because no fork
# object is linked into this dylib at all -- see the header.)
cat >> "$UNEXPORTS" <<'EOF'
___ed_longjmp_raw
___ed_setjmp_raw
___wait4_nocancel
___waitpid
__execve
__execvpe
__longjmp
__setjmp
__sigaction
_execl
_execle
_execlp
_execv
_execve
_execvp
_execvP
_longjmp
_setjmp
_siglongjmp
_sigsetjmp
_strnunvis
_strnunvisx
_strunvis
_strunvisx
_unvis
_wait4
_waitpid
EOF

# ------------------------------------------------------------------
# 4. Real dylib link.
#
# install_name /usr/lib/libedit.3.dylib is real Apple's own: libedit.
# xcconfig sets PRODUCT_NAME = edit.3 / EXECUTABLE_PREFIX = lib /
# INSTALL_PATH = /usr/lib, and xcodescripts/install_misc.sh symlinks
# libedit.dylib -> libedit.3.dylib there.
# ------------------------------------------------------------------
mkdir -p "$OUT/system"
rm -f "$OUT/system/libedit.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libedit.3.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-unexported_symbols_list,"$UNEXPORTS" \
    `# the ONE remaining deferral -- see this script's header for the two` \
    `# real, separately-verified reasons _fork cannot live in a dylib` \
    `# here, and for the real QEMU evidence that this one resolves.` \
    -Wl,-U,_fork \
    -o "$OUT/system/libedit.dylib" \
    "$OBJ"/*.o \
    "$EXTRA"/*.o \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libncurses.dylib" \
    `# libinfo owns the real getpwnam_r/getpwuid_r/getpwuid/getpwent/` \
    `# setpwent/endpwent family (fn_tilde_expand + readline.c)` \
    "$OUT/system/libinfo.dylib" \
    `# libcopyfile is this project's real current canonical owner of` \
    `# _fchmod (history_save) -- build_copyfile_dylib.sh's own comment` \
    `# says so explicitly ("libsystem_kernel.dylib doesn't have real` \
    `# copies of those (yet), so this dylib's own real implementations` \
    `# stay the correct, only copies")` \
    "$OUT/system/libcopyfile.dylib" \
    2>"$OUT/libedit_dylib_link.err" \
    && echo "wrote $OUT/system/libedit.dylib" \
    || { echo "libedit.dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libedit_dylib_link.err" | sed 's/,.*//' | sort -u >&2; tail -20 "$OUT/libedit_dylib_link.err" >&2; exit 1; }

otool -hv "$OUT/system/libedit.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}

# Real gate: the 9 entry points scutil actually calls must be real,
# exported definitions -- the whole point of this ticket. A regression
# that silently dropped one (Apple's unexports list is applied above, and
# it really does hide ~230 internals) would otherwise only show up as a
# scutil link failure much later.
for s in _el_init _el_end _el_gets _el_set _el_get _el_source \
         _history_init _history_end _history; do
    if ! nm -gU "$OUT/system/libedit.dylib" | grep -qE "^[0-9a-f]+ T $s\$"; then
        echo "error: libedit.dylib does not export $s -- scutil needs all 9" >&2
        exit 1
    fi
done
NEXPORTS=$(nm -gU "$OUT/system/libedit.dylib" | grep -c ' T ')
echo "libedit.dylib: all 9 scutil entry points exported ($NEXPORTS total exports)"

# Real gate on the OTHER direction: section 3 links real libc objects
# into this dylib, and every public name they carry is supposed to be
# narrowed away. If one escapes, libedit silently becomes a second public
# exporter of a libc symbol -- exactly the duplicate-owner situation
# dup_symbol_audit.sh exists to catch, but caught here at build time
# instead. Real Apple's libedit.3.dylib exports 110 symbols and so must
# this one.
if [ "$NEXPORTS" -ne 110 ]; then
    echo "error: libedit.dylib exports $NEXPORTS symbols, expected real Apple's 110." >&2
    echo "       Something section 3 links in is escaping the unexports list:" >&2
    nm -gU "$OUT/system/libedit.dylib" | grep ' T ' | awk '{print $3}' \
        | grep -E '^_(fork|wait|exec|setjmp|longjmp|sigsetjmp|siglongjmp|unvis|strunvis|strnunvis)' >&2 || true
    exit 1
fi

echo "declared -Wl,-U deferrals (expect exactly _fork -- see the header):"
xcrun dyld_info -fixups "$OUT/system/libedit.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print "  " $NF}' | sort -u
DEFERRED=$(xcrun dyld_info -fixups "$OUT/system/libedit.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print $NF}' | sed 's|.*/||' | sort -u | tr '\n' ' ')
EXPECTED="_fork "
if [ "$DEFERRED" != "$EXPECTED" ]; then
    echo "error: flat-namespace deferral set changed" >&2
    echo "  expected: $EXPECTED" >&2
    echo "  actual:   $DEFERRED" >&2
    echo "  NOTE: an unresolved flat-namespace bind really does abort dyld-940" >&2
    echo "  at load (proved by a real QEMU boot, see this script's header) --" >&2
    echo "  a new entry here is a real runtime failure, not a warning." >&2
    exit 1
fi

UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libedit.dylib" 2>/dev/null \
    | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' \
    | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/|libncurses(\.5)?/|libinfo/|libcopyfile/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found:" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libedit.dylib: every non-deferred external reference binds to a real, named sibling dylib"

# ------------------------------------------------------------------
# 5. Real smoketest.
#
# libedit is a terminal line editor, and this project's QEMU harness
# cannot synthesise keypresses, so "interactive editing verified" is not
# a claim this script can honestly make. What it CAN prove -- and does,
# for real, on the target -- is the exact contract scutil.c depends on
# and that the old scutil_editline_compat.c deliberately failed:
# history_init() and el_init() return non-NULL, history(H_SETSIZE)
# succeeds, el_set(EL_EDITOR,"emacs")/el_set(EL_PROMPT) succeed,
# el_source(NULL) runs, el_get(EL_EDITMODE) returns 0 with a non-zero
# editmode (which is what makes scutil KEEP the editor instead of tearing
# it down), and el_gets() reads a real line back. See
# libedit_smoketest.c's own header.
# ------------------------------------------------------------------
rm -f libedit_smoketest.macho
"$CLANG" "${CFLAGS[@]}" libedit_smoketest.c -o "$OBJ/../libedit_smoketest.o"
# libedit.dylib's one flat-namespace deferral, _fork, has to resolve in
# THIS process too -- scutil gets it from libSystemConfiguration.dylib,
# which a smoketest has no business loading. So this executable carries
# the real objects itself, exactly as the other 79 shipped .macho
# binaries in this project already do:
#   fork_compat.o   this project's own real curated fork() (calls the
#                   real atfork handlers directly rather than through
#                   Libc's never-initialised function pointers)
#   sys____fork.o   the real generated __fork(2) asm stub
#   sys_getpid.o    from libmalloc.a -- owns the `__current_pid` cache
#                   sys____fork.o updates through a direct adrp/lo12
#                   pair, so the two MUST be in the same image (real
#                   Apple marks it .private_extern in
#                   libsyscall/custom/__getpid.s for exactly that
#                   reason). This pairing is also precisely why fork
#                   cannot live inside libedit.dylib -- see the header.
FORKOBJ="$EXTRA/fork_for_smoketest"
rm -rf "$FORKOBJ"
mkdir -p "$FORKOBJ"
(cd "$FORKOBJ" && ar x "../../liblaunchd_syscalls_no_dup.a" fork_compat.o sys____fork.o \
    && ar x "../../libmalloc.a" sys_getpid.o)
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o libedit_smoketest.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/libedit_smoketest.o" \
    "$OUT/chk_fail.o" \
    "$FORKOBJ"/*.o \
    "$OUT/system/libsystem_kernel.dylib" "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libsystem_blocks.dylib" "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_c.dylib" \
    `# libsystem_c.dylib carries a real, pre-existing flat-namespace` \
    `# deferral for _asin (and the rest of the libm family); without` \
    `# libsystem_m.dylib in the closure this binary aborts at dyld load` \
    `# with "symbol not found in flat namespace '_asin'". Found by a real` \
    `# QEMU boot, not predicted -- scutil already links libsystem_m, so` \
    `# only this new binary ever exposed it.` \
    "$OUT/system/libsystem_m.dylib" \
    "$OUT/system/libedit.dylib" "$OUT/system/libncurses.dylib" \
    "$OUT/system/libinfo.dylib" "$OUT/system/libcopyfile.dylib" \
    "$OUT/system/libdispatch.dylib" \
    "$OUT/system/libdyld.dylib" \
    `# REQUIRED, and not for any symbol: dyld-940 halts with "program` \
    `# does not link with libSystem.B.dylib" (dyld/dyldMain.cpp:685)` \
    `# unless some image in the process's closure has install name` \
    `# /usr/lib/libSystem.B.dylib (DyldRuntimeState.cpp:243 is the only` \
    `# place libSystemLoader is ever set). scutil satisfies this` \
    `# transitively; a small smoketest does not, so it links it directly,` \
    `# LAST, exactly as res_init_dnsinfo_smoketest.macho already does.` \
    `# Last on the line matters: libSystem.B.dylib here is still the` \
    `# flattened static stack (CLAUDE.md's "Static-vs-dylib architecture` \
    `# debt"), so listing it after the real per-library dylibs keeps every` \
    `# two-level bind pointed at the real owner. Found by a real QEMU` \
    `# boot -- it hides behind any earlier bind failure, because dyld` \
    `# resolves binds before it reaches this check.` \
    "$OUT/system/libSystem.B.dylib" \
    2>"$OUT/libedit_smoketest_link.err" \
    && echo "wrote libedit_smoketest.macho" \
    || { echo "libedit_smoketest link failed:" >&2; grep '^  "' "$OUT/libedit_smoketest_link.err" | sed 's/,.*//' | sort -u >&2; tail -20 "$OUT/libedit_smoketest_link.err" >&2; exit 1; }

./bind_target_audit.sh libedit_smoketest.macho || {
    echo "error: libedit_smoketest.macho has an unresolvable two-level bind" >&2
    exit 1
}
# See the libSystem.B.dylib comment on the link line above -- dyld halts
# without it, and the failure hides behind any earlier bind error, so it
# is asserted here rather than rediscovered by another QEMU boot.
if ! otool -L libedit_smoketest.macho | grep -q "/usr/lib/libSystem.B.dylib"; then
    echo "error: libedit_smoketest.macho does not link /usr/lib/libSystem.B.dylib --" >&2
    echo "       dyld-940 would halt with 'program does not link with libSystem.B.dylib'" >&2
    exit 1
fi
echo "  install: tools/init_binary/inject_into_sd_image.sh puts libedit.dylib at"
echo "           /usr/lib/libedit.3.dylib and the smoketest at /usr/bin/libedit_smoketest"
