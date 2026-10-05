#!/bin/bash
# Builds htop.macho -- upstream htop 3.5.3 (htop-dev/htop; Apple never open-sourced an htop, confirmed:
# `git ls-remote apple-oss-distributions/htop` -> "Repository not found"; so per the source-priority
# rule this is the real upstream, whose own darwin/ platform is the one macOS users build).
# Dynamic only, same recipe as build_top.sh (htop's Darwin platform wants the same libproc, Mach host
# statistics, IOKit/CoreFoundation and ncurses that top-129 does): links the real libiokituser/
# libCoreFoundation/libncurses/libsystem_* dylibs. Narrow (non-wide) ncurses: no HAVE_LIBNCURSESW, this
# port's libncurses is the 8-bit build.
# config.h is hand-written below (configure is not run, like every other port here); each define is one
# htop's sources test (`grep -o 'HAVE_[A-Z0-9_]*'`), and its reason is on the line.
set -euo pipefail
cd "$(dirname "$0")"
OUT="libc_build"
T="../../third_party"
HT="$T/htop"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
S="$OUT/system"
for f in libncurses libiokituser libCoreFoundation libsystem_kernel libsystem_c libxpc libsystem_m \
         libsystem_pthread libsystem_malloc libsystem_platform libdyld libinfo libSystem.B; do
    [ -f "$S/$f.dylib" ] || { echo "error: $S/$f.dylib not found -- build it first" >&2; exit 1; }
done
[ -d "$OUT/iokitlib_headers" ] || { echo "error: run build_libiokituser_dylib.sh first" >&2; exit 1; }
[ -d "$OUT/ncurses_derived" ] || { echo "error: run build_ncurses.sh first" >&2; exit 1; }
[ -d "$HT" ] || { echo "error: $HT missing -- run setup_third_party.sh" >&2; exit 1; }
OBJ="$OUT/htop_obj"
rm -rf "$OBJ"; mkdir -p "$OBJ/inc/IOKit/ps"
# Real Apple IOPSKeys.h/IOPowerSources.h (IOKitUser ps.subproj); htop_iops_stub.c explains the no-battery linkage.
cp "$T/IOKitUser/ps.subproj/IOPSKeys.h" "$T/IOKitUser/ps.subproj/IOPowerSources.h" "$OBJ/inc/IOKit/ps/"
cat > "$OBJ/inc/config.h" <<'EOF'
#define HAVE_CONFIG_H 1
#define CONFIGDIR "/.config"
#define PACKAGE "htop"
#define PACKAGE_NAME "htop"
#define PACKAGE_VERSION "3.5.3"
#define VERSION "3.5.3"
#define COPYRIGHT "(C) 2004-2019 Hisham Muhammad. (C) 2020-2026 htop dev team."
#define SYSCONFDIR "/etc"
#define HAVE_CURSES_H 1
#define HAVE_TERM_H 1
#define HAVE_GETMOUSE 1
#define HAVE_SET_ESCDELAY 1
#define HAVE_SYS_UTSNAME_H 1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_STRNLEN 1
#define HAVE_MACH_MACH_TIME_H 1
#define HAVE_MACH_TIMEBASE_INFO 1
#define HAVE_HOST_GET_CLOCK_SERVICE 1
#define HAVE_STRUCT_VM_STATISTICS64 1
#define HAVE_STRUCT_VM_STATISTICS64_COMPRESSOR_PAGE_COUNT 1
#define HAVE_STRUCT_VM_STATISTICS64_INTERNAL_PAGE_COUNT 1
#define HAVE_THREAD_EXTENDED_INFO_DATA_T 1
#define HAVE_BUILTIN_CTZ 1
#define HAVE_ATTR_ACCESS 1
#define HAVE_ATTR_NONNULL 1
#define HAVE_ATTR_RETNONNULL 1
#define HAVE_ATTR_ALLOC_SIZE 1
#define HAVE_DECL_KIOMAINPORTDEFAULT 0
EOF
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -O1 -fno-builtin -Wno-nullability-completeness \
  -Wno-everything -DPRIVATE=1 -D__DARWIN_ONLY_VERS_1050=1 -D_FORTIFY_SOURCE=0 -DHTOP_DARWIN=1 \
  -include $OBJ/inc/config.h -I $OBJ/inc -I $HT -I $HT/darwin -I $OUT/iokitlib_headers -I $OUT/cf_headers -I $OUT/ncurses_derived \
  -I $T/ncurses/ncurses/include -I $T/libutil -idirafter $T/Libc/include \
  -I $T/xnu-7195/iokit -I $T/xnu-7195/libkern -I $T/xnu-7195/bsd -idirafter $T/xnu-7195/osfmk \
  -idirafter $T/xnu-7195/libsyscall/wrappers/libproc"
SRCS=$(cd "$HT" && sed -n '/^myhtopsources = /,/^$/p' Makefile.am | tr -d '\\' | tr -s ' \t\n' '\n' | grep '\.c$')
SRCS="$SRCS htop.c GPUMeter.c darwin/Platform.c darwin/PlatformHelpers.c darwin/DarwinMachine.c \
  darwin/DarwinProcess.c darwin/DarwinProcessTable.c generic/fdstat_sysctl.c generic/gettime.c \
  generic/hostname.c generic/openzfs_sysctl.c generic/uname.c zfs/ZfsArcMeter.c zfs/ZfsCompressedArcMeter.c"
"$CLANG" $CFLAGS htop_iops_stub.c -o "$OBJ/htop_iops_stub.o" -Wno-everything
fail=0
for f in $SRCS; do
    b=$(echo "$f" | tr / _ | sed 's/\.c$//')
    "$CLANG" $CFLAGS "$HT/$f" -o "$OBJ/$b.o" 2>"$OBJ/$b.err" || { echo "FAIL: $f"; fail=1; }
done
[ "$fail" -eq 0 ] || { echo "error: compile failures, see $OBJ/*.err" >&2; exit 1; }
# Gaps no dylib here exports (same real Libc sources and approach as build_top.sh, kept local to htop):
LC="$T/Libc"
for f in "$LC/gen/FreeBSD/sysctlnametomib.c" "$LC/gen/getloadavg.c"; do
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 -Wno-everything -D_FORTIFY_SOURCE=0 \
        -I "$LC/gen/FreeBSD" -I "$LC/gen" -idirafter "$LC/include" "$f" -o "$OBJ/libc_$(basename "$f" .c).o"
done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 -Wno-everything htop_memset_pattern16.c -o "$OBJ/htop_memset_pattern16.o"
M="htop.macho"
rm -f "$M"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$M" "$T/Csu/start.s" "$T/Csu/crt.c" "$OBJ"/*.o \
    "$S/libncurses.dylib" "$S/libiokituser.dylib" "$S/libCoreFoundation.dylib" \
    "$S/libsystem_kernel.dylib" "$S/libxpc.dylib" "$S/libsystem_c.dylib" "$S/libsystem_m.dylib" "$S/libsystem_pthread.dylib" \
    "$S/libsystem_malloc.dylib" "$S/libsystem_platform.dylib" "$S/libdyld.dylib" "$S/libinfo.dylib" "$S/libSystem.B.dylib" \
    2>"$OBJ/link.err" || { echo "error: link failed:" >&2; grep -E '^  "|duplicate' "$OBJ/link.err" | sort -u | head -40 >&2; exit 1; }
otool -l "$M" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || { echo "error: no LC_LOAD_DYLINKER" >&2; exit 1; }
otool -l "$M" | grep -q "cmd LC_LOAD_DYLIB" || { echo "error: no LC_LOAD_DYLIB" >&2; exit 1; }
otool -l "$M" | grep -q "cmd LC_MAIN" || { echo "error: no LC_MAIN" >&2; exit 1; }
./bind_target_audit.sh "$M" || { echo "error: $M has an unresolvable two-level bind" >&2; exit 1; }
echo "wrote $M"
