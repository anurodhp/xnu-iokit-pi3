#!/bin/bash
# Builds libc_build/audiod.macho, the audio daemon (installed as /usr/sbin/audiod).
source "$(dirname "$0")/build_common.sh"
need_dylibs libiokituser libCoreFoundation libxpc libsystem_m libsystem_pthread libsystem_c libsystem_malloc libsystem_kernel libsystem_platform libdyld libSystem.B
O="$OUT/coreaudio_obj"; mkdir -p "$O"
for f in daemon/audiod daemon/mixer; do
    "$CLANG" "${COMMON_CFLAGS[@]}" "$CA_DIR/$f.c" -o "$O/$(basename $f).o"
done
M="$OUT/audiod.macho"; rm -f "$M"
# libxpc is listed before libsystem_* so bootstrap_check_in binds to its real owner (DAR-217 rule).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$M" -DCRT_NO_PTHREAD_BOOTSTRAP "$T/Csu/start.s" "$T/Csu/crt.c" "$O/audiod.o" "$O/mixer.o" \
    "$S/libiokituser.dylib" "$S/libxpc.dylib" "$S/libCoreFoundation.dylib" "$S/libsystem_m.dylib" "${EXEC_LIBS[@]}"
check_dynamic_exec "$M"
# IOAudioEngineUserClient demands this entitlement (see tools/userland_staging/audio_play.c).
codesign -s - -f --entitlements "$CA_DIR/audiod.entitlements" "$M" 2>&1 | grep -v "replacing existing signature" || true
codesign -d --entitlements - "$M" 2>/dev/null | grep -q "com.apple.private.audio.driver-host" || { echo "error: $M lacks the audio entitlement" >&2; exit 1; }
echo "wrote $M"
