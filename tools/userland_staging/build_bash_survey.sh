#!/bin/bash
# Real-source survey/spike for Phase 4 step 3 (docs/roadmap.md): does NOT
# produce a working bash.macho. Attempts to compile real, unmodified GNU
# bash 3.2 (third_party/bash/bash-3.2, Apple's real fork, tag
# bash-123.40.1) core .c files against this project's curated
# libc_build/*.a stack (string/platform/locale/stdio/gen/printf/malloc/
# pthread/closure/removefile/copyfile), using the SAME Csu-static pattern
# every other tier in this directory uses -- not against dyld at all,
# since this project's entire architecture is static (see architecture.md
# and every prior tier's zero-LC_LOAD_DYLIB verification).
#
# WHY THIS IS A SURVEY, NOT A BUILD SCRIPT: a real bash build normally
# goes through Apple's own Xcode project or upstream's Autoconf
# `configure`, which generates config.h (already present here, real,
# Apple-generated, copied in below) AND several other real generated
# files this project's non-Xcode/non-configure build path doesn't
# reproduce automatically (get_compat.h, version.h's CONF_MACHTYPE-style
# defines). This script surveys real compile-time gaps against the
# curated libc stack; closing every one of them into a real linked binary
# is a separate, larger follow-up (see docs/roadmap.md's writeup for the
# concrete remaining list).
#
# Real result of this survey (see docs/roadmap.md for narrative):
# 21/33 real bash-3.2 core .c files compile cleanly against the curated
# libc stack with ONLY real, existing Apple-shipped headers added to the
# include path (third_party/bash's top-level pathnames.h/version.h/
# libintl.h -- Apple's own real generated companions to this exact bash
# checkout, plus real third_party/Libc locale/stdtime FreeBSD subdirs
# already used by other tiers). The 12 real remaining gaps are ALL
# concrete and enumerated, not vague:
#   - lib/tilde, lib/readline, lib/glob (bash's own bundled real source
#     subdirectories -- just missing -I paths, not missing source)
#   - get_compat.h -- a real Autoconf-generated file (single
#     shell_compatibility_level default define), never generated here
#     since this project doesn't run bash's own `configure`/Makefile
#   - CONF_MACHTYPE/CONF_HOSTTYPE/CONF_OSTYPE/CONF_VENDOR/PACKAGE --
#     real string-literal -D flags Makefile.in's SYSTEM_FLAGS/LOCALE_DEFS
#     derive from `configure`'s target-triple substitution (see that
#     file's own SYSTEM_FLAGS line) -- trivial, real values for this
#     target (e.g. -DCONF_MACHTYPE='"arm64-apple-darwin20"'), not
#     fabricated logic, just not yet threaded through by this script.
set -e
cd "$(dirname "$0")"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
BASHSRC="../../third_party/bash/bash-3.2"
BASHTOP="../../third_party/bash"
OUT="libc_build"
SURVEY_OUT="bash_survey_out"
mkdir -p "$SURVEY_OUT"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

# Real Apple-generated config.h (third_party/bash/config.h, Apple's own
# real macOS/arm64 build output for this exact bash-123.40.1 checkout) --
# copied into bash-3.2/ the same way Apple's own build places it, not
# hand-written. Real bison (host tool, not a target compile) regenerates
# y.tab.c from the real parse.y grammar, same as Apple's own build does.
cp "$BASHTOP/config.h" "$BASHSRC/config.h"
if [ ! -f "$BASHSRC/y.tab.c" ]; then
    if ! command -v bison >/dev/null 2>&1; then
        echo "error: bison not found (needed to generate y.tab.c from parse.y, host-side only)" >&2
        exit 1
    fi
    ( cd "$BASHSRC" && bison -y -d parse.y -o y.tab.c )
fi

# Same real flags every other tier uses (see build_libc_printf.sh) plus
# bash's own -I paths and its real SHELL/PROGRAM defines.
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -include libc_build_prototypes.h \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -DHAVE_CONFIG_H -DSHELL=1 -DPROGRAM='\"bash\"' \
    -I $BASHSRC -I $BASHSRC/include -I $BASHSRC/builtins -I $BASHTOP \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD -I $LIBC/stdtime/FreeBSD \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/osfmk -I $XNU/libsyscall"

# Real core interpreter/state files (not the full ~80-file source tree --
# builtins/, lib/readline, lib/glob, lib/tilde, lib/sh are each a
# separate real subsystem, out of scope for this survey pass; see
# docs/roadmap.md for the exact next-step list).
FILES="general.c variables.c shell.c subst.c eval.c execute_cmd.c \
    copy_cmd.c dispose_cmd.c make_cmd.c print_cmd.c error.c expr.c \
    flags.c jobs.c mailcheck.c trap.c input.c unwind_prot.c pathexp.c \
    sig.c test.c version.c alias.c array.c arrayfunc.c braces.c \
    bashhist.c list.c stringlib.c locale.c findcmd.c redir.c xmalloc.c"

ok=0
fail=0
for f in $FILES; do
    errfile="$SURVEY_OUT/${f}.err"
    if $CLANG $CFLAGS "$BASHSRC/$f" -o "$SURVEY_OUT/${f}.o" >"$errfile" 2>&1; then
        ok=$((ok + 1))
        rm -f "$errfile"
    else
        fail=$((fail + 1))
        echo "  gap: $f -- $(grep -m1 'error:' "$errfile")"
    fi
done
echo "bash-3.2 core-file survey: $ok compiled clean, $fail real gaps (see bash_survey_out/*.err for full detail)"
