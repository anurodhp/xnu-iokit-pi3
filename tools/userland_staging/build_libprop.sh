#!/bin/bash
# Builds /usr/lib/libprop.dylib: NetBSD's property-list library (proplib,
# common/lib/libprop) and the red-black tree it uses (common/lib/libc/gen/
# rb.c), from our fork anurodhp/netbsd-src (third_party/netbsd-src).
# NetBSD's bthcid keeps its link keys with it, btkey reads them
# (docs/bluetooth-stack-plan.md S4).
#
# Built in proplib's portable user-space mode (prop_object_impl.h: not
# __NetBSD__, not HAVE_NBTOOL_CONFIG_H -> "Use pthread mutexes everywhere
# else"). Headers for consumers go to libc_build/libprop_include/ (prop/*.h
# and NetBSD's sys/rbtree.h).
set -euo pipefail
cd "$(dirname "$0")"

OUT="libc_build"
T="../../third_party"
NB="$T/netbsd-src"
COMPAT="../../compat/netbt-user"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
S="$OUT/system"

[ -d "$NB/common/lib/libprop" ] && [ -f "$NB/common/lib/libc/gen/rb.c" ] || {
    echo "error: $NB/common/lib/libprop or rb.c not found -- run ./setup_third_party.sh" >&2; exit 1; }
for f in libsystem_kernel libsystem_c libsystem_malloc libsystem_platform libsystem_pthread libdyld libSystem.B; do
    [ -f "$S/$f.dylib" ] || { echo "error: $S/$f.dylib not found -- build it first" >&2; exit 1; }
done

INC="$OUT/libprop_include"
rm -rf "$INC"
mkdir -p "$INC/prop" "$INC/sys"
cp "$NB"/common/include/prop/*.h "$INC/prop/"
cp "$NB"/sys/sys/rbtree.h "$INC/sys/"
cp "$COMPAT"/sys/endian.h "$INC/sys/"

OBJ="$OUT/libprop_obj"
rm -rf "$OBJ"
mkdir -p "$OBJ"
CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -O1 -Wno-nullability-completeness
    -D_FORTIFY_SOURCE=0 -Werror=implicit-function-declaration -Werror=int-conversion
    -include "$COMPAT/netbt_user.h" -I "$INC" -I "$NB/common/lib/libprop")
# common/lib/libprop/Makefile.inc SRCS, plus rb.c.
for s in prop_array prop_array_util prop_bool prop_data prop_dictionary prop_dictionary_util \
         prop_extern prop_ingest prop_intern prop_kern prop_number prop_object prop_stack prop_string; do
    "$CLANG" "${CFLAGS[@]}" "$NB/common/lib/libprop/$s.c" -o "$OBJ/$s.o"
done
"$CLANG" "${CFLAGS[@]}" "$NB/common/lib/libc/gen/rb.c" -o "$OBJ/rb.o"

D="$S/libprop.dylib"
rm -f "$D"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libprop.dylib \
    -Wl,-not_for_dyld_shared_cache -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$D" "$OBJ"/*.o \
    "$S/libsystem_c.dylib" "$S/libsystem_malloc.dylib" "$S/libsystem_kernel.dylib" \
    "$S/libsystem_platform.dylib" "$S/libsystem_pthread.dylib" "$S/libdyld.dylib" "$S/libSystem.B.dylib"
otool -hv "$D" | grep -q " DYLIB " || { echo "error: $D is not a dylib" >&2; exit 1; }
./bind_target_audit.sh "$D" || { echo "error: $D has an unresolvable two-level bind" >&2; exit 1; }
codesign -s - -f "$D" 2>&1 | grep -v "replacing existing signature" || true
echo "wrote $D"
