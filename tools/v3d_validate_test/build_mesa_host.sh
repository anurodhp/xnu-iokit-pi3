#!/bin/bash
# Builds Mesa's vc4 gallium driver (and the gallium auxiliary code it needs)
# NATIVELY on the build machine, as static libraries, for
# test_mesa_vc4.c: Mesa's real shader compiler and command-list generator
# driving the libdrm shim and the mock kernel, so every shader and command
# list the driver really produces goes through the vendored validators.
#
# Output: $OUT (default qemu/mesa-host-build). Needs: meson, ninja, bison
# (Homebrew; macOS ships 2.3), flex, python3 with mako/pyyaml/packaging.
set -e
cd "$(dirname "$0")/../.."
OUT="${1:-qemu/mesa-host-build}"
export PATH=/opt/homebrew/opt/bison/bin:$PATH
SHIM="$PWD/tools/userland_staging/v3d_drm"

# Mesa only looks for libdrm on Linux-like systems; the shim's headers are
# passed as plain compiler flags instead.
cat > "$OUT.native.ini" <<EOF
[built-in options]
c_args = ['-I$SHIM/include', '-I$PWD/third_party/mesa/include', '-include', '$SHIM/include/v3d_mmap_hook.h', '-Wno-error']
cpp_args = ['-I$SHIM/include', '-I$PWD/third_party/mesa/include', '-Wno-error']
EOF

rm -rf "$OUT"
meson setup "$OUT" third_party/mesa --native-file "$OUT.native.ini" \
    --buildtype=debugoptimized \
    -Dplatforms= -Dglx=disabled -Dgallium-drivers=vc4 -Dvulkan-drivers= \
    -Dllvm=disabled -Dgallium-rusticl=false -Dopengl=true -Dgles1=disabled -Dgles2=enabled \
    -Degl=disabled -Dgbm=disabled -Dglvnd=disabled -Dlmsensors=disabled -Dlibunwind=disabled \
    -Dvalgrind=disabled -Dzstd=disabled -Dxmlconfig=disabled -Dbuild-tests=false -Dtools= \
    > "$OUT.setup.log" 2>&1 || { tail -30 "$OUT.setup.log" >&2; exit 1; }
echo "configured"
ninja -C "$OUT" src/gallium/drivers/vc4/libvc4.a src/gallium/auxiliary/libgallium.a src/util/libmesa_util.a src/c11/impl/libmesa_util_c11.a src/util/libmesa_util_simd.a src/util/blake3/libblake3.a src/compiler/nir/libnir.a src/compiler/libcompiler.a src/compiler/glsl/libglsl_util.a src/gallium/auxiliary/libgalliumvl_stub.a > "$OUT.build.log" 2>&1 || { grep -E "error|FAILED" "$OUT.build.log" | head -20 >&2; exit 1; }
echo "built libvc4.a, libgallium.a, libnir.a and the util libraries"
