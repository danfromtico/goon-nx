# Engineering notes

What was found bringing Dead or Alive Xtreme Beach Volleyball (Xbox NTSC-U,
title 0x54430007, XDK 4928, built 2003-01-10) up on the Switch. Addresses are
guest addresses in `default.xbe`.

## The title

- Entry point 0x001832A1. Sections: `.text`, `D3D`, `D3DX`, `XGRPH`, `DSOUND`,
  `PSGSFD00/_I/_B/_P` (CRI Sofdec MPEG decoder), `WMADEC`, `XPP` (USB),
  `DOLBY`, `zressect`. `.data` is 12.7 MB virtual (1.2 MB raw); the image
  ends at 0x00EFE420.
- Data: CRI archives `datadvd.afs`, `datahdd.afs`, `bgm.afs`, `voice.afs`,
  `ninja.aix`; movies are `*.sfd` (Sofdec: MPEG-1 + ADX).
- Game logic runs in XAPI **fibers** scheduled from the frame loop
  (0x000BB310 -> scheduler 0x000B5680). CRI's ADX manager runs its own
  threads (vsync 0x00189D60, file server 0x0018A180, idle 0x00189DD0, idle
  counter 0x00189D20) and schedules them with priorities and
  suspend/resume.
- First boot copies `voice.afs` (42 MB) and `datahdd.afs` (708 MB) to the
  cache partition Z: (`save/Cache`). The check at 0x000B4940..0x000B4A7E
  accepts a cached copy only if its size matches the original, its creation
  time is within 60 s of the original's and equals its own last-write time.

## Memory layout

- The runtime's own guest blocks (TLS, kernel data exports, main stack) sit
  at `XBOX_RUNTIME_BASE` + fixed offsets. The toolkit default 0x00700000 is
  inside this title's `.data`; `CMakeLists.txt` sets 0x00F00000. The loader
  refuses an image that reaches the area.
- Contiguous memory (`MmAllocateContiguousMemory*`) comes out of the heap
  allocator: physical address = heap address, VA = 0x80000000 + that. A
  separate arena made bus addresses ambiguous -- XAPI's gamepad device lives
  in the heap, its report buffer went to the OHCI model by VA, the model took
  it for a physical address and wrote every report into the contiguous
  window: no input at all.
- The title uses nearly all 64 MB (a 21 MB pool, 7 + 7 + 4 MB blocks). Main
  stack region 768 KB (the runtime's two 256 KB worker stack slices are
  carved from its low end), 128 KB per guest thread.
- Switch: guest RAM is code memory; a second view of it is refused
  (`svcMapProcessMemory` 0xD401), so the RAM mirrors and the tiled aperture
  are missing. Not yet a problem here. Autorun (wine-nx) aliases with
  `svcMapProcessCodeMemory` anchors + `svcMapProcessMemory`, on stock
  Atmosphère -- the way to do it if it becomes one.

## Recompiler (third_party/xboxrecomp/tools)

- Entries found in gaps (data tables, conditional-branch orphans) ran to the
  next known function start: the ~2,000 static initialisers at
  0x001C6F70-0x001E3550 each became 116 KB of C (186 MB, cc1 out of memory).
  `functions.py _gap_body_end` measures the body instead.
- MSVC memcpy/memmove index their tables with a bias (`LeadUpVec[eax*4-4]`,
  `TrailUpVec[ecx*4-16]`, `UnwindDownVec[ecx*4]`, ecx negative). The disasm
  resync (`engine.py`) and the lifter (`_read_biased_jump_table`) follow
  them; before, memcpy of 0-3 bytes copied nothing.
- Functions that never return (fiber tasks) have no `ret` for the
  immediate-reference pass to find: it now also accepts a 16-byte aligned
  prologue after padding (`_looks_like_function_start`).
- Unresolved-target stubs report when they run (`RECOMP_STUB_TRACE=1`).
- `game/seeds.json`: thread starts, callbacks only reached through
  pointers, the D3D ISR 0x001EC1C0, the Sofdec decoder entries
  (0x00217BE0, 0x002181C0, 0x002188A0), and functions seen executing in xemu
  (`tools/xemu/`).

## Overrides (game/overrides.c)

| Address | What |
|---|---|
| 0x0018305A / 0x00183047 | SwitchToFiber / DeleteFiber: each fiber is a host thread (`xbox_fiber_switch`) |
| 0x001EA050 | D3D BlockOnTime: hands the GPU semaphore (device 0x001F2978 +0x34) to the pushbuffer executor |
| 0x001EC3B0, 0x001EC900 | D3D vblank and PGRAPH handlers at DISPATCH (DPC 0x001ECB70, ISR 0x001EC1C0) |
| 0x00200AAE | DirectSound AC'97 channel reset: the compiler hoisted the status load out of the wait loop |
| 0x001FB065 | DirectSound DSP command post: acknowledges the mailbox word at scratch+0x810 |
| 0x00189D20 | CRI idle counter: never gave up the guest lock |
| 0x0023308F, 0x0023384E | XInputGetState / report copy: `GOON_INPUT_TRACE=1` diagnostics |

## Runtime (third_party/xboxrecomp/src)

- `NtSuspendThread` really suspends on POSIX/Switch: a suspended guest thread
  blocks when it next takes the guest lock.
- Kernel calls that cannot block keep the guest lock (object references,
  thread priorities, `NtResumeThread`, `RtlLeaveCriticalSection`): CRI makes
  ~600 of them per 32 KB of its cache copy.
- `Partition0.img` sector 4 is XAPI's cache-partition table; the runtime
  rewrote its default there at every start and XAPI formatted the cache each
  boot. Written only into a new image now.
- File times a title sets are kept in `<save>/filetimes.txt` (the Switch's
  fs service cannot set them) and reported by every query.
- `NtQueryVolumeInformationFile` classes 1/5/7 and `NtQueryInformationFile`
  class 6 (XAPI `GetFileInformationByHandle`; without them the title shows
  "There's a problem with the disc you're using").
- NV2A fog: enable, mode and parameters applied after the vertex stage
  (`gl_vsh.c nv2a_fog`); before, fixed-function batches were fully fogged
  and the island was a flat pale blue.
- Switch: the crash report prints the native call chain; the log is drained
  at exit; the loading-screen thread stops before libnx tears down.
- Linux: guest RAM is placed low so the NV2A/MCPX apertures fit in a 48-bit
  address space (Docker Desktop).
- OpenGL on the Switch: Mesa's compressed-texture upload corrupts the heap on
  the title screen (`RECOMP_GL_DXT=0` avoids it). The Vulkan build does not
  go through it.

## Required settings

`RECOMP_GIL_EAGER=1` (guest lock by guest priority -- CRI's loader depends on
it; without it the title reports a damaged disc) and `RECOMP_ASYNC_IO=1`.

## Open

- Audio is noise on the Switch.
- `+` (Start) reaching the title on hardware is untested since the input fix.
- Past the title screen is untested.
- xemu coverage: of 72,987 PC samples from an intro + menu session, 442 are
  outside translated code, at 0x001C7845, 0x001E2DFC, 0x00214750 and the
  kernel interrupt stub 0x001F550C.
