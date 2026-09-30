#!/bin/bash
# Builds top.macho -- real, unmodified Apple top-129 (apple-oss-distributions/top,
# tag top-129 == macos-112 manifest commit 5ce037b8; see setup_third_party.sh).
# DAR-412 needs a way to see physical memory on the real Pi. Dynamic only:
# links the real libiokituser/libCoreFoundation/libncurses/libsystem_* dylibs
# (same recipe as build_iokitlib_smoketest.sh). libproc calls (proc_listpids,
# proc_pidinfo, ...) resolve through libsystem_kernel/libxpc dylib exports.
set -euo pipefail
cd "$(dirname "$0")"
OUT="libc_build"
T="../../third_party"
TOP="$T/top"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
S="$OUT/system"
for f in libncurses libpanel libiokituser libCoreFoundation libsystem_kernel libsystem_c libxpc \
         libsystem_pthread libsystem_malloc libsystem_platform libdyld libSystem.B; do
    [ -f "$S/$f.dylib" ] || { echo "error: $S/$f.dylib not found -- build it first" >&2; exit 1; }
done
[ -d "$OUT/iokitlib_headers" ] || { echo "error: run build_libiokituser_dylib.sh first" >&2; exit 1; }
[ -d "$OUT/ncurses_derived" ] || { echo "error: run build_ncurses.sh first" >&2; exit 1; }
[ -d "$TOP" ] || { echo "error: $TOP missing -- run setup_third_party.sh" >&2; exit 1; }
OBJ="$OUT/top_obj"
rm -rf "$OBJ"; mkdir -p "$OBJ"
# iPhoneOS SDK's <mach/mach_vm.h> is "#error unsupported"; use the real
# MIG-generated header (xnu mach_vm.defs) built by build_libsystem_kernel.sh.
mkdir -p "$OBJ/inc/mach"
# mach_make_memory_entry is declared by both this header and the SDK's vm_map.h
# with different types; top never calls it, so rename it in this private copy.
sed 's/mach_make_memory_entry/mach_make_memory_entry_mv/g' "$OUT/libsystem_kernel/vm_mig/mach_vm.h" > "$OBJ/inc/mach/mach_vm.h"
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -O0 -Wno-nullability-completeness \
  -Wno-everything -DPRIVATE=1 -D__DARWIN_ONLY_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
  -I $TOP -I $OUT/iokitlib_headers -I $OUT/cf_headers -I $OUT/ncurses_derived \
  -I $T/ncurses/ncurses/include -I $T/ncurses/ncurses/panel \
  -I $OBJ/inc -I $T/libutil -idirafter $T/Libc/include \
  -I $T/xnu-7195/iokit -I $T/xnu-7195/libkern -I $T/xnu-7195/bsd -I $T/xnu-7195/osfmk \
  -idirafter $T/xnu-7195/libsyscall/wrappers/libproc"
fail=0
for f in "$TOP"/*.c; do
    b=$(basename "$f" .c)
    "$CLANG" $CFLAGS "$f" -o "$OBJ/$b.o" 2>"$OBJ/$b.err" || { echo "FAIL: $b"; fail=1; }
done
# real Apple libutil humanize_number (no libutil dylib exists here yet)
"$CLANG" $CFLAGS "$T/libutil/humanize_number.c" -o "$OBJ/humanize_number.o" 2>"$OBJ/humanize_number.err" || { echo "FAIL: humanize_number"; fail=1; }
[ "$fail" -eq 0 ] || { echo "error: compile failures, see $OBJ/*.err" >&2; exit 1; }
# --- gaps libsystem_kernel/libsystem_c dylibs do not export (adding them there
# would force a rebuild+audit of every image; local to top, real sources only).
# 1. MIG client stubs, narrowed to exactly the missing routines.
XNU="$T/xnu-7195"; MIG="$(which mig)"
CLANGMIG="$CLANG -isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wno-everything -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1"
mkdir -p "$OBJ/mig"
"$MIG" -arch arm64 -I"$XNU/osfmk" -header "$OBJ/mig/processor_set.h" -user "$OBJ/mig/processor_setUser.c" \
    -server /dev/null -sheader /dev/null "$XNU/osfmk/mach/processor_set.defs"
$CLANGMIG -I "$XNU/libsyscall/mach" -idirafter "$XNU/osfmk" -I "$OBJ/mig" "$OBJ/mig/processor_setUser.c" -o "$OBJ/mig/processor_setUser.o"
LD12="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
"$LD12" -r -arch arm64 -exported_symbol _processor_set_tasks "$OBJ/mig/processor_setUser.o" -o "$OBJ/mig_processor_set_tasks.o"
"$LD12" -r -arch arm64 -exported_symbol __kernelrpc_mach_port_space_basic_info "$OUT/libsystem_kernel/mach_portUser.o" -o "$OBJ/mig_space_basic_info.o"
"$LD12" -r -arch arm64 -exported_symbol _mach_vm_region_recurse "$OUT/libsystem_kernel/vm_mig/mach_vmUser.o" -o "$OBJ/mig_vm_region_recurse.o"
$CLANGMIG -idirafter "$XNU/osfmk" top_mach_port_space_basic_info.c -o "$OBJ/top_mach_port_space_basic_info.o"
# 2. pid_for_task Mach trap (osfmk/mach/syscall_sw.h:124)
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c top_pid_for_task.s -o "$OBJ/top_pid_for_task.o"
# 3. real Apple Libc sysctlnametomib + getloadavg
LC="$T/Libc"
for f in "$LC/gen/FreeBSD/sysctlnametomib.c" "$LC/gen/getloadavg.c"; do
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 -Wno-everything -D_FORTIFY_SOURCE=0 \
        -I "$LC/gen/FreeBSD" -I "$LC/gen" -idirafter "$LC/include" "$f" -o "$OBJ/libc_$(basename "$f" .c).o"
done
M="top.macho"
rm -f "$M"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$M" "$T/Csu/start.s" "$T/Csu/crt.c" "$OBJ"/*.o \
    "$S/libpanel.dylib" "$S/libncurses.dylib" "$S/libiokituser.dylib" "$S/libCoreFoundation.dylib" \
    "$S/libsystem_kernel.dylib" "$S/libxpc.dylib" "$S/libsystem_c.dylib" "$S/libsystem_pthread.dylib" \
    "$S/libsystem_malloc.dylib" "$S/libsystem_platform.dylib" "$S/libdyld.dylib" "$S/libinfo.dylib" "$S/libSystem.B.dylib"
otool -l "$M" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || { echo "error: no LC_LOAD_DYLINKER" >&2; exit 1; }
otool -l "$M" | grep -q "cmd LC_LOAD_DYLIB" || { echo "error: no LC_LOAD_DYLIB" >&2; exit 1; }
otool -l "$M" | grep -q "cmd LC_MAIN" || { echo "error: no LC_MAIN" >&2; exit 1; }
./bind_target_audit.sh "$M" || { echo "error: $M has an unresolvable two-level bind" >&2; exit 1; }
echo "wrote $M"
