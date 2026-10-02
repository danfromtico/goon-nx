# goon-nx — notes for Claude

Static recompilation of Dead or Alive Xtreme Beach Volleyball (Xbox) for the
Nintendo Switch. `README.md` has the layout and the commands;
`docs/NOTES.md` has every finding so far -- read it before debugging.

- Never commit game data, lifted C or build output: all of it lives in
  `work/` (ignored). Ask before committing or pushing.
- `third_party/xboxrecomp` is vendored and modified here; changes to it are
  part of this repo.
- Test on Linux first (`scripts/linux.sh`), then build for the Switch
  (`scripts/switch.sh`); a full Switch build takes ~15 min, so batch changes.
- After editing `game/seeds.json` or the toolkit's analysis: full
  `scripts/regen.sh`. After editing `game/overrides.c` (adding or removing an
  override): `LIFT_ONLY=1 scripts/regen.sh`.
- Docker has ~8 GB here: `JOBS=3` for Switch builds, `JOBS=4` for Linux.
