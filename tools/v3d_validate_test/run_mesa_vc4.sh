#!/bin/bash
# Builds and runs test_mesa_vc4.c: Mesa's real vc4 driver (built natively by
# build_mesa_host.sh) against the libdrm shim, the mock kernel and the
# vendored validators. See test_mesa_vc4.c.
# Usage: tools/v3d_validate_test/run_mesa_vc4.sh [mesa-build-dir]
set -e
cd "$(dirname "$0")/../.."
MB="${1:-qemu/mesa-host-build}"
[ -f "$MB/src/gallium/drivers/vc4/libvc4.a" ] || { echo "error: run tools/v3d_validate_test/build_mesa_host.sh first" >&2; exit 1; }
OUT="${TMPDIR:-/tmp}/v3d_validate_test"
mkdir -p "$OUT"
SHIM=tools/userland_staging/v3d_drm
V=iokit/BCM2837V3D/validate

# Compiler flags exactly as meson used for a vc4 object (include paths,
# generated headers, defines), from compile_commands.json with relative
# paths made absolute.
FLAGS=()
while IFS= read -r -d '' x; do FLAGS+=("$x"); done < <(python3 - "$MB" <<'PY'
import json, os, shlex, sys
mb = sys.argv[1]
cc = json.load(open(os.path.join(mb, "compile_commands.json")))
ent = next(e for e in cc if e["file"].endswith("drivers/vc4/vc4_screen.c"))
d = ent["directory"]
args = shlex.split(ent["command"]) if "command" in ent else ent["arguments"]
out = []
i = 1
def ab(p):
    return p if os.path.isabs(p) else os.path.normpath(os.path.join(d, p))
while i < len(args):
    a = args[i]
    if a in ("-I", "-isystem", "-include"):
        v = args[i + 1]
        if "v3d_mmap_hook" not in v:
            out += [a, ab(v)]
        i += 2
        continue
    if a.startswith("-I"):
        out.append("-I" + ab(a[2:]))
    elif a.startswith("-isystem"):
        out.append("-isystem" + ab(a[8:]))
    elif a.startswith("-D"):
        out.append(a)
    i += 1
sys.stdout.write("\0".join(out) + "\0")
PY
)
FLAGS+=(-include "$PWD/$SHIM/include/v3d_mmap_hook.h")

# Everything static meson produced that the driver may need; ld64 resolves
# archives without ordering constraints.
LIBS=$(ls "$MB"/src/gallium/drivers/vc4/libvc4.a "$MB"/src/gallium/auxiliary/libgallium.a \
    "$MB"/src/gallium/auxiliary/libgalliumvl_stub.a "$MB"/src/compiler/nir/libnir.a \
    "$MB"/src/compiler/libcompiler.a "$MB"/src/compiler/glsl/libglsl_util.a \
    "$MB"/src/util/libmesa_util.a "$MB"/src/c11/impl/libmesa_util_c11.a \
    "$MB"/src/util/libmesa_util_simd.a "$MB"/src/util/blake3/libblake3.a 2>/dev/null)

cc -std=gnu11 -g -O1 -fsanitize=address,undefined -fno-sanitize=alignment -fno-sanitize-recover=undefined "${FLAGS[@]}" \
    -I third_party/mesa/src/gallium/auxiliary -I third_party/mesa/src/gallium/include -I third_party/mesa/src/gallium/drivers -I third_party/mesa/src \
    -I tools/v3d_validate_test -I $SHIM -I $SHIM/include -I iokit/BCM2837V3D -I $V -I third_party/mesa/include \
    -Wno-everything \
    -o "$OUT/test_mesa_vc4" ${V3D_TEST_SRC:-tools/v3d_validate_test/test_mesa_vc4.c} tools/v3d_validate_test/mesa_stubs.c \
    $SHIM/v3d_drm.c $V/vc4_validate.c $V/vc4_validate_shaders.c $V/vc4_render_cl.c $V/v3d_exec.c $V/v3d_kglue.c \
    $LIBS -lm -lz -lpthread -lc++ 2>&1 | head -60
exec "$OUT/test_mesa_vc4"
