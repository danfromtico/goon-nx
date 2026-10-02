# goon-nx

**G**ame **O**f **O**cean **N**ets, on NX.

*Dead or Alive Xtreme Beach Volleyball* (Xbox, NTSC-U) statically recompiled
for the Nintendo Switch: the game's x86 code is translated to C with
[xboxrecomp](https://github.com/sp00nznet/xboxrecomp) and built as a homebrew
NRO, on a runtime that stands in for the Xbox kernel, GPU, audio processor
and USB.

No game data is included. You need your own copy of the disc, extracted.

## Status

Work in progress. On Linux and on Switch hardware it boots, plays the intro
movies and reaches the title screen with the island rendered. Audio is
broken (noise) and nothing past the title screen has been tested.

## Layout

| Path | What |
|---|---|
| `game/` | Everything specific to this title: `overrides.c` (hand-written replacements of translated functions), `seeds.json` (function entry points the analysis cannot see), `assets/` |
| `src/` | The program around the translated code: `main.c` (boot), `platform/switch.c` (Switch log, settings file, crash report, loading screen) |
| `scripts/` | `regen.sh` (XBE to C), `switch.sh` (Switch NRO), `linux.sh` (Linux build and headless test runs) |
| `tools/` | `xbedis.py` (disassemble the XBE by guest address), `xemu/` (find executed code the translation misses, from a running xemu), `prof-report.py` |
| `third_party/xboxrecomp/` | The toolkit and runtime, vendored with this port's changes |
| `docs/NOTES.md` | Engineering notes |
| `work/` | Not in git: the disc, translated C, build trees, staged NROs |

## Building

Needs Docker, and Python 3 with `capstone` on the host.

```sh
# 1. Put the extracted disc (default.xbe, *.afs, *.sfd, Media/) in work/game
mkdir -p work && ln -s /path/to/extracted/disc work/game

# 2. Translate the XBE to C (~5 min, into work/gen)
scripts/regen.sh

# 3. Switch NRO (in the ghcr.io/autorunhq/switch-dev container, ~15 min)
scripts/switch.sh            # Vulkan: work/sd/switch/goon-nx/goon-nx-vulkan.nro
scripts/switch.sh gl         # OpenGL: goon-nx.nro
```

An Xbox disc image has to be the game partition (an XISO); `extract-xiso`
unpacks it.

## Running on the Switch

Copy to the SD card:

```
sdmc:/switch/goon-nx/goon-nx-vulkan.nro
sdmc:/switch/goon-nx/game/           the extracted disc
sdmc:/switch/goon-nx/goon_env.txt
```

`goon_env.txt` holds `KEY=VALUE` settings, one per line. These two are
required:

```
RECOMP_GIL_EAGER=1
RECOMP_ASYNC_IO=1
```

Start it with title takeover (hold R while launching a game) so it gets the
console's full memory. The first boot copies 750 MB to
`save/Cache` (as the game does on an Xbox) and takes several minutes; later
boots do not. The log is `goon_log.txt` in the same folder.

Controls: `+` Start, `-` Back, A/B/X/Y by label (`RECOMP_PAD_LAYOUT=position`
for the Xbox positions), L/R White/Black, ZL/ZR triggers.

## Testing on Linux

```sh
scripts/linux.sh image       # once
scripts/linux.sh build
SECS=120 scripts/linux.sh run RECOMP_GL_DUMP=$PWD/work/frames/f,60
```

runs headless and writes `work/linux_run.log` and a frame every 60 presents.
`scripts/linux.sh snap` adds a backtrace of every thread.

## Credits

- [xboxrecomp](https://github.com/sp00nznet/xboxrecomp) (MIT) -- the
  recompiler and runtime.
- [nfsu2-sw](https://github.com/antoxa2584x/nfsu2-sw) -- the Switch platform
  layer, NV2A OpenGL/Vulkan renderers and the port structure this started
  from.
- [xemu](https://xemu.app) -- the reference used to find untranslated code.
