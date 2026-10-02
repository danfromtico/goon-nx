#!/usr/bin/env bash
# Lift the XBE to C: disasm -> func_id -> abi -> recomp, into $GOON_WORK/gen.
#
#   scripts/regen.sh              full run (~5 min); needed after seed or
#                                 toolkit analysis changes
#   LIFT_ONLY=1 scripts/regen.sh  only re-lift (after game/overrides.c edits)
#
# Needs python3 with capstone. game/seeds.json lists entry points the static
# pass cannot see; grow it from a run log with
#   python3 -m tools.seed_from_log run.log default.xbe \
#       --functions tools/disasm/output/functions.json --seeds game/seeds.json
# (from third_party/xboxrecomp) and review every addition: a seed inside a
# function splits it.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/common.sh"

XBE="${GOON_XBE:-$WORK/game/default.xbe}"
GEN="$WORK/gen"
[ -f "$XBE" ] || die "no $XBE (put the extracted disc in $WORK/game)"

# The toolkit looks for <xbe stem>_analysis.json beside the XBE; keep it out
# of the disc folder.
mkdir -p "$WORK/xbe" "$GEN"
ln -sf "$(cd "$(dirname "$XBE")" && pwd -P)/$(basename "$XBE")" "$WORK/xbe/default.xbe"
XBE="$WORK/xbe/default.xbe"

cd "$TK"
if [ "${LIFT_ONLY:-0}" != "1" ]; then
    python3 -m tools.xbe_parser "$XBE" --json "$WORK/xbe/default_analysis.json" >/dev/null
    python3 -m tools.disasm "$XBE" --seed-functions "$REPO/game/seeds.json"
    python3 -m tools.func_id "$XBE"
    python3 -m tools.abi_analysis "$XBE"
fi
# recomp_types.h is written only when absent, so a stale copy would hide
# register-model changes in the toolkit.
rm -f "$GEN/recomp_types.h"
python3 -m tools.recomp "$XBE" --game-name "Dead or Alive Xtreme Beach Volleyball" \
    --all --split 250 --gen-dir "$GEN" --exclude-manual "$REPO/game/overrides.c" \
    --mmio-sections DSOUND,XPP
