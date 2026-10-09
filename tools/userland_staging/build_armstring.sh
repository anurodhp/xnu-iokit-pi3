#!/bin/bash
# Assembles Arm's AArch64 string routines (third_party/optimized-routines, tag v26.07, MIT OR Apache-2.0 WITH LLVM-exception,
# unmodified) and the small glue that gives them this system's symbol names:
#
#   libc_build/armstring/body/arm_*.o   the Arm routines: ___memset_aarch64, ___memmove_aarch64 (also ___memcpy_aarch64, overlap-safe),
#                                       ___memcmp_aarch64, ___memchr_aarch64, ___strlen_aarch64, ___strcmp_aarch64, ___strncmp_aarch64,
#                                       ___strchr_aarch64, ___strchrnul_aarch64, ___strrchr_aarch64, ___strcpy_aarch64, ___stpcpy_aarch64,
#                                       ___strnlen_aarch64
#   libc_build/armstring/libc/*.o       one object per name FreeBSD's libc string files define (_memcpy, _memmove, _memset, _bzero, _bcopy,
#                                       _memcmp, _memchr, _strlen, _strcmp, _strncmp, _strchr, ___strchrnul, _strrchr, _rindex, _strcpy, _stpcpy, _strnlen): same one
#                                       symbol each, a branch into the routine. build_libc_string.sh archives them in place of the FreeBSD
#                                       objects (same member names, so build_libsystem_c.sh's per-name handling of memcpy.o/memset.o holds).
#   libc_build/armstring/platform/*.o   the libplatform names: __platform_memset/_bzero/_memmove/_memcmp/_strlen/_strcmp/_strchr and the
#                                       public _memset/_memcpy (exported) and _memmove/_bzero/___bzero (local, see build_libsystem_platform.sh)
#   libc_build/armstring/hidden.txt     the Arm routines' own names, for -unexported_symbols_list: each dylib that links them keeps them private,
#                                       so no two dylibs export the same symbol.
#
# Why: Apple publishes no arm64 string assembly (libplatform up to 375.120.2 has only src/string/generic), and the generic C
# fallbacks built -O0 ran memset at 24 MB/s, unaligned memcpy at 47 MB/s, memcmp at 53 MB/s (florence tests/pi/mem_bench.c).
# Arm's code is the tested set behind glibc's, Android's and FreeBSD's arm64 libc; its own test programs
# (string/test/*.c: memcpy memmove memset memcmp memchr strlen strcmp ...) pass against these exact objects. The bodies use Advanced SIMD
# loads/stores and avoid DC ZVA and system register reads (memset.S, not memset-scalar.S), which this kernel does not allow at EL0.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
ARM="../../third_party/optimized-routines/string/aarch64"
[ -f "$ARM/memcpy.S" ] || { echo "error: $ARM/memcpy.S not found -- run setup_third_party.sh (optimized-routines)" >&2; exit 1; }

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

D="$OUT/armstring"
rm -rf "$D"
mkdir -p "$D/body" "$D/libc" "$D/platform"

# WANT_GNU_PROPERTY=0: the ELF .note.gnu.property section does not exist in Mach-O. asmdefs.h has a __APPLE__ mode for the rest.
AFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -DWANT_GNU_PROPERTY=0"
for f in memset memcpy memcmp memchr strlen strcmp strncmp strchr strchrnul strrchr strcpy stpcpy strnlen; do
    "$CLANG" $AFLAGS "$ARM/$f.S" -o "$D/body/arm_$f.o"
done

# every global the routines define, one per line
nm -g "$D"/body/*.o | awk '$2 == "T" { print $3 }' | sort -u > "$D/hidden.txt"

# glue: <dir> <object name> <symbol> <body instructions>; symbol is the full Mach-O name
glue() {
    local dir="$1" obj="$2" sym="$3" body="$4"
    printf '\t.text\n\t.p2align 4\n\t.globl %s\n%s:\n%b' "$sym" "$sym" "$body" > "$D/$dir/$obj.s"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c "$D/$dir/$obj.s" -o "$D/$dir/$obj.o"
    rm -f "$D/$dir/$obj.s"
}
b() { printf '\tb\t%s\n' "$1"; }
bzero_body() { printf '\tmov\tx2, x1\n\tmov\tw1, wzr\n\tb\t___memset_aarch64\n'; }
bcopy_body() { printf '\tmov\tx3, x0\n\tmov\tx0, x1\n\tmov\tx1, x3\n\tb\t___memmove_aarch64\n'; }

# the names FreeBSD's libc string files define (one symbol per object, and the object names build_libc_string.sh/build_libsystem_c.sh use)
glue libc memcpy    _memcpy      "$(b ___memmove_aarch64)"
glue libc memmove   _memmove     "$(b ___memmove_aarch64)"
glue libc memset    _memset      "$(b ___memset_aarch64)"
glue libc bzero     _bzero       "$(bzero_body)"
glue libc bcopy     _bcopy       "$(bcopy_body)"
glue libc memcmp    _memcmp      "$(b ___memcmp_aarch64)"
glue libc memchr    _memchr      "$(b ___memchr_aarch64)"
glue libc strlen    _strlen      "$(b ___strlen_aarch64)"
glue libc strcmp    _strcmp      "$(b ___strcmp_aarch64)"
glue libc strncmp   _strncmp     "$(b ___strncmp_aarch64)"
glue libc strchr    _strchr      "$(b ___strchr_aarch64)"
glue libc strchrnul ___strchrnul "$(b ___strchrnul_aarch64)"
glue libc strcpy    _strcpy      "$(b ___strcpy_aarch64)"
glue libc stpcpy    _stpcpy      "$(b ___stpcpy_aarch64)"
glue libc strrchr   _strrchr     "$(b ___strrchr_aarch64)"
glue libc rindex    _rindex      "$(b ___strrchr_aarch64)"
glue libc strnlen   _strnlen     "$(b ___strnlen_aarch64)"

# the names libplatform's generic bzero.c/memmove.c/memcmp.c/strlen.c/strcmp.c/strchr.c define
glue platform platform_memset  __platform_memset  "$(b ___memset_aarch64)"
glue platform platform_bzero   __platform_bzero   "$(bzero_body)"
glue platform platform_memmove __platform_memmove "$(b ___memmove_aarch64)"
glue platform platform_memcmp  __platform_memcmp  "$(b ___memcmp_aarch64)"
glue platform platform_strlen  __platform_strlen  "$(b ___strlen_aarch64)"
glue platform platform_strcmp  __platform_strcmp  "$(b ___strcmp_aarch64)"
glue platform platform_strchr  __platform_strchr  "$(b ___strchr_aarch64)"
glue platform pub_memset       _memset            "$(b ___memset_aarch64)"
glue platform pub_memcpy       _memcpy            "$(b ___memmove_aarch64)"
glue platform pub_memmove      _memmove           "$(b ___memmove_aarch64)"
glue platform pub_bzero        _bzero             "$(bzero_body)"
glue platform pub___bzero      ___bzero           "$(bzero_body)"

echo "armstring: $(ls "$D"/body | wc -l | tr -d ' ') routines, $(ls "$D"/libc | wc -l | tr -d ' ') libc names, $(ls "$D"/platform | wc -l | tr -d ' ') libplatform names, $(wc -l < "$D/hidden.txt" | tr -d ' ') private symbols"
