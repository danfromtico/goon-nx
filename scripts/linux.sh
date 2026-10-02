#!/usr/bin/env bash
# Linux build and headless runs in a container (SDL offscreen, Mesa llvmpipe)
# -- the fast way to test without a console.
#
#   scripts/linux.sh image               build the container image (once)
#   scripts/linux.sh build
#   scripts/linux.sh run  [ENV=VAL ...]  SECS=60; log in $GOON_WORK/linux_run.log
#   scripts/linux.sh snap [ENV=VAL ...]  run, then gdb backtraces of every
#                                        thread at SNAP=30 s -> $GOON_WORK/snap.txt
#
# Useful ENV: RECOMP_GL_DUMP=<dir>/f,60 (dump presented frames as BMP),
# RECOMP_PAD_SCRIPT=15000:start:600 (timed presses), RECOMP_STUB_TRACE=1,
# RECOMP_KERNEL_LOG_BUDGET=3000000.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

cmd="${1:-}"; [ $# -gt 0 ] && shift
build="$WORK/build/linux"
mounts=(); while IFS= read -r l; do mounts+=("$l"); done < <(docker_mounts)
# The settings the title needs (see README): priority-ordered guest lock and
# pending overlapped reads. A later ENV=VAL on the command line wins.
envs=(-e SDL_VIDEODRIVER=offscreen -e SDL_AUDIODRIVER=dummy
      -e GOON_GAME_DIR="$WORK/game" -e XDG_DATA_HOME="$WORK/home"
      -e RECOMP_GIL_EAGER=1 -e RECOMP_ASYNC_IO=1)
for e in "$@"; do envs+=(-e "$e"); done

case "$cmd" in
image)
    docker build -t "$LINUX_IMAGE" "$REPO/docker/linux" ;;
build)
    [ -f "$WORK/gen/recomp_funcs.h" ] || die "no lifted code in $WORK/gen (run scripts/regen.sh)"
    docker run --rm "${mounts[@]}" -w "$REPO" "$LINUX_IMAGE" bash -c "
        cmake -S . -B '$build' -G Ninja -DCMAKE_BUILD_TYPE=Release \
              -DGOON_GEN_DIR='$WORK/gen' >/dev/null &&
        ninja -C '$build' -j${JOBS:-4}" ;;
run)
    docker run --rm "${mounts[@]}" -w "$WORK" "${envs[@]}" "$LINUX_IMAGE" bash -c "
        timeout -s KILL ${SECS:-60} '$build/goon_nx' > '$WORK/linux_run.log' 2>&1; echo exit=\$?" ;;
snap)
    docker run --rm --cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
        "${mounts[@]}" -w "$WORK" "${envs[@]}" "$LINUX_IMAGE" bash -c "
        '$build/goon_nx' > '$WORK/linux_run.log' 2>&1 & pid=\$!
        sleep ${SNAP:-30}
        gdb -p \$pid -batch -ex 'set pagination off' -ex 'thread apply all bt 25' \
            > '$WORK/snap.txt' 2>&1
        kill -9 \$pid; echo snapped" ;;
*) die "usage: scripts/linux.sh image|build|run|snap [ENV=VAL ...]" ;;
esac
