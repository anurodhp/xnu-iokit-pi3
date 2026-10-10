#!/bin/bash
# Builds libc_build/system/libaudiohal.dylib (/usr/lib/libaudiohal.dylib), the CoreAudio HAL subset.
source "$(dirname "$0")/build_common.sh"
need_dylibs libxpc libCoreFoundation libsystem_pthread libsystem_c libsystem_malloc libsystem_kernel libsystem_platform libdyld libSystem.B
O="$OUT/coreaudio_obj"; mkdir -p "$O"
"$CLANG" "${COMMON_CFLAGS[@]}" -fvisibility=hidden "$CA_DIR/lib/libaudiohal.c" -o "$O/libaudiohal.o"
rm -f "$S/libaudiohal.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libaudiohal.dylib -Wl,-not_for_dyld_shared_cache -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$S/libaudiohal.dylib" "$O/libaudiohal.o" \
    "$S/libxpc.dylib" "$S/libCoreFoundation.dylib" "${EXEC_LIBS[@]}"
otool -hv "$S/libaudiohal.dylib" | grep -q " DYLIB " || { echo "error: not a DYLIB" >&2; exit 1; }
# Exactly the documented API must be exported, nothing more.
want="_AudioDeviceCreateIOProcID _AudioDeviceDestroyIOProcID _AudioDeviceStart _AudioDeviceStop _AudioObjectGetPropertyData _AudioObjectGetPropertyDataSize _AudioObjectHasProperty _AudioObjectIsPropertySettable _AudioObjectSetPropertyData"
got=$(nm -gU "$S/libaudiohal.dylib" | awk '{print $3}' | sort | tr '\n' ' ')
[ "$got" = "$(echo $want | tr ' ' '\n' | sort | tr '\n' ' ')" ] || { echo "error: unexpected exports: $got" >&2; exit 1; }
"$STAGING/bind_target_audit.sh" "$S/libaudiohal.dylib" || { echo "error: libaudiohal has an unresolvable bind" >&2; exit 1; }
echo "wrote $S/libaudiohal.dylib"
