#!/bin/bash
# Builds libc_build/ca_play.macho (the smoketest client), linked against libaudiohal.dylib.
source "$(dirname "$0")/build_common.sh"
need_dylibs libaudiohal libCoreFoundation libsystem_m libsystem_pthread libsystem_c libsystem_malloc libsystem_kernel libsystem_platform libdyld libSystem.B
O="$OUT/coreaudio_obj"; mkdir -p "$O"
"$CLANG" "${COMMON_CFLAGS[@]}" "$CA_DIR/test/ca_play.c" -o "$O/ca_play.o"
M="$OUT/ca_play.macho"; rm -f "$M"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$M" -DCRT_NO_PTHREAD_BOOTSTRAP "$T/Csu/start.s" "$T/Csu/crt.c" "$O/ca_play.o" \
    "$S/libaudiohal.dylib" "$S/libCoreFoundation.dylib" "$S/libsystem_m.dylib" "${EXEC_LIBS[@]}"
check_dynamic_exec "$M"
codesign -s - -f "$M" 2>&1 | grep -v "replacing existing signature" || true
echo "wrote $M"
