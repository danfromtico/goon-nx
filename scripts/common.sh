# Shared by the scripts: where things are.
#
#   GOON_WORK   everything that is not source (default <repo>/work, ignored
#               by git; it may be a symlink):
#                 game/            the extracted disc (default.xbe, *.afs, ...)
#                 gen/             lifted C (scripts/regen.sh)
#                 build/           build trees
#                 sd/switch/goon-nx/   staged NROs
#                 glslang-switch/  glslang for the Vulkan build
#                 home/            saves of Linux runs
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${GOON_WORK:-$REPO/work}"
TK="$REPO/third_party/xboxrecomp"
SWITCH_IMAGE="${GOON_SWITCH_IMAGE:-ghcr.io/autorunhq/switch-dev:latest}"
LINUX_IMAGE="${GOON_LINUX_IMAGE:-goon-nx-linux}"

die() { echo "error: $*" >&2; exit 1; }

# docker -v arguments that keep host paths (and symlinks into them) valid
# inside a container: the repo, the work directory and the disc.
docker_mounts() {
    local p seen=""
    for p in "$REPO" "$(cd "$WORK" && pwd -P)" "$(cd "$WORK/game" 2>/dev/null && pwd -P)"; do
        [ -n "$p" ] || continue
        case " $seen " in *" $p "*) continue ;; esac
        seen="$seen $p"
        printf -- '-v\n%s:%s\n' "$p" "$p"
    done
}
