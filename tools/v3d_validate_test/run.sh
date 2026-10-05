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
exec "$OUT/test_validate" "$@"
