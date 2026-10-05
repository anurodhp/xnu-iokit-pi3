#!/bin/bash
# Builds and runs the host tests for the vendored VC4 validators
# (iokit/BCM2837V3D/validate) with AddressSanitizer + UBSan.
# Usage: tools/v3d_validate_test/run.sh [fuzz-iterations] [seed]
set -e
cd "$(dirname "$0")"
# alignment checks are off on purpose: the validators read/write packet fields at
# odd byte offsets (CL packets are unaligned) in cacheable staging memory,
# which is fine on arm64 Normal memory and x86.
V=../../iokit/BCM2837V3D/validate
OUT="${TMPDIR:-/tmp}/v3d_validate_test"
mkdir -p "$OUT"
cc -std=gnu11 -I ../../iokit/BCM2837V3D -I $V -g -O1 -fsanitize=address,undefined -fno-sanitize=alignment -fno-sanitize-recover=undefined \
    -Wall -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function \
    -o "$OUT/test_validate" test_validate.c \
    $V/vc4_validate.c $V/vc4_validate_shaders.c $V/vc4_render_cl.c $V/v3d_exec.c $V/v3d_kglue.c
"$OUT/test_validate" "$@"

# ABI: kext user-client structs vs Mesa's DRM uapi structs
cc -std=gnu11 -I ../../iokit/BCM2837V3D -I ../../third_party/mesa/include \
    -o "$OUT/abi_check" abi_check.c
"$OUT/abi_check"

# libdrm shim against a mock kernel (real validators behind it)
SHIM=../userland_staging/v3d_drm
cc -std=gnu11 -I ../../iokit/BCM2837V3D -I $V -I $SHIM -I ../../third_party/mesa/include \
    -g -O1 -fsanitize=address,undefined -fno-sanitize=alignment -fno-sanitize-recover=undefined \
    -Wall -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function \
    -o "$OUT/test_drm_shim" test_drm_shim.c $SHIM/v3d_drm.c \
    $V/vc4_validate.c $V/vc4_validate_shaders.c $V/vc4_render_cl.c $V/v3d_exec.c $V/v3d_kglue.c
"$OUT/test_drm_shim"

# job pipeline state machine against a fake GPU
c++ -std=gnu++14 -I ../../iokit/BCM2837V3D -g -O1 -fsanitize=address,undefined \
    -Wall -Wno-unused-function -o "$OUT/test_queue" test_queue.cpp
"$OUT/test_queue" 100000
