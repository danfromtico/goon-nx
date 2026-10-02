#!/usr/bin/env bash
# Build the Switch NRO in the switch-dev container.
#
#   scripts/switch.sh [vulkan|gl]      default: vulkan
#
# Output: $GOON_WORK/sd/switch/goon-nx/goon-nx-vulkan.nro (or goon-nx.nro).
# Copy it to sdmc:/switch/goon-nx/ next to game/ (the extracted disc) and
# start it with title takeover (hold R on a game) for full memory.
#
#   JOBS    parallel compile jobs (default 3: the lifted C needs ~2 GB each)
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

kind="${1:-vulkan}"
[ -f "$WORK/gen/recomp_funcs.h" ] || die "no lifted code in $WORK/gen (run scripts/regen.sh)"
mounts=(); while IFS= read -r l; do mounts+=("$l"); done < <(docker_mounts)
run() { docker run --rm "${mounts[@]}" -w "$REPO" "$SWITCH_IMAGE" bash -c "$1"; }

case "$kind" in
vulkan)
    build="$WORK/build/switch-vulkan"; nro=goon-nx-vulkan.nro
    gl="$WORK/glslang-switch"
    if [ ! -f "$gl/lib/libglslang.a" ]; then
        echo "building glslang for the Switch (once)..."
        run "set -e
            [ -d '$WORK/build/glslang-src' ] || git clone -q --depth 1 --branch 15.1.0 \
                https://github.com/KhronosGroup/glslang.git '$WORK/build/glslang-src'
            cmake -S '$WORK/build/glslang-src' -B '$WORK/build/glslang' -G Ninja \
                -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
                -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX='$gl' \
                -DENABLE_OPT=OFF -DENABLE_GLSLANG_BINARIES=OFF -DGLSLANG_TESTS=OFF \
                -DENABLE_HLSL=OFF -DBUILD_SHARED_LIBS=OFF -DENABLE_SPVREMAPPER=OFF \
                -DBUILD_EXTERNAL=OFF >/dev/null
            ninja -C '$WORK/build/glslang' install >/dev/null"
    fi
    args="-DGOON_VULKAN=ON -DNVK_SDK=/opt/devkitpro/portlibs/switch -Dglslang_DIR=$gl/lib/cmake/glslang"
    ;;
gl)
    build="$WORK/build/switch-gl"; nro=goon-nx.nro
    args="-DGOON_VULKAN=OFF"
    ;;
*) die "usage: scripts/switch.sh [vulkan|gl]" ;;
esac

run "set -e
    cmake -S . -B '$build' -G Ninja -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
        -DCMAKE_BUILD_TYPE=Release -DGOON_GEN_DIR='$WORK/gen' $args >/dev/null
    ninja -C '$build' -j${JOBS:-3}"

dest="$WORK/sd/switch/goon-nx"
mkdir -p "$dest"
cp "$build/goon_nx.nro" "$dest/$nro"
echo "staged $dest/$nro ($(wc -c < "$dest/$nro" | tr -d ' ') bytes)"
