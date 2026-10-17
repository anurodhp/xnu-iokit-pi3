#!/bin/bash
# Builds libc_build/sample_prof.dylib (/usr/local/lib/sample_prof.dylib): a
# sampling CPU profiler injected with DYLD_INSERT_LIBRARIES (sample_prof.c).
# Not linked by anything; installed by inject_into_sd_image.sh for
# profiling on the Pi, where there is no other profiler.
set -euo pipefail
cd "$(dirname "$0")"
. ./media_common.sh
O="$OUT/sample_prof.dylib"
rm -f "$O"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c "${MEDIA_CFLAGS[@]}" -Wall \
    -Werror=implicit-function-declaration sample_prof.c -o "$MEDIA_GEN/sample_prof.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib "${MEDIA_LDFLAGS[@]}" \
    -install_name /usr/local/lib/sample_prof.dylib -o "$O" "$MEDIA_GEN/sample_prof.o" "${MEDIA_SYSLIBS[@]}"
otool -hv "$O" | grep -q " DYLIB " || { echo "error: $O is not MH_DYLIB" >&2; exit 1; }
./bind_target_audit.sh "$O"
echo "wrote $O"
