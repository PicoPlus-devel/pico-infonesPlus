# hosttest — Linux host harness for the InfoNES core

Runs the unmodified `infones/` emulator core headless on a Linux PC, dumping
frames as images, or in a window with sound and keyboard input
([live mode](#live-mode-nes_live1)). Useful for debugging core behavior (PPU rendering, CPU
timing, mapper state) with fast iteration and full instrumentation — no Pico
flashing, no serial console. Only hardware-specific issues (HSTX/DVI output,
SD card, PSRAM latency, audio sinks) still need the real device.

The harness builds against the **RP2350 + framebuffer** configuration:
`PICO_RP2350=1`, `FRAMEBUFFERISPOSSIBLE=1`, `isPsramEnabled()=true`
(see `NES_NO_PSRAM`). That
unlocks MMC5, VRC7 CHR-RAM, the Famicom Disk System (FDS), and the
320×240 full-frame rendering path the device uses on RP2350.

## Layout

| File | Purpose |
|---|---|
| `build.sh` | builds `hosttest/nes_host`, with live mode when SDL2 is installed |
| `host_main.cpp` | main loop, InfoNES_* callbacks, 256×240 PPM dumper, env-var input injection, input record/replay, audio mix, minimal iNES parser, inline CRC32, NES palette |
| `host_sdl.cpp`, `host_sdl.h` | live mode: window, sound output, keyboard, pacing (SDL2) |
| `stubs.cpp` | Frens helper subset (f_malloc/f_free/isPsramEnabled/...) the core links against, audio callback no-ops, host-stdio-backed FatFs (so FDS BIOS loads), `settings` instance |
| `shim/pico.h` | empty `__not_in_flash_func` / `__not_in_flash` placement macros |
| `shim/pico/time.h` | host `time_us_32` from `clock_gettime` |
| `shim/ff.h` | minimal FatFs surface — backed by stdio in `stubs.cpp` |
| `shim/FrensHelpers.h` | minimal `Frens::*` declarations the core needs; replaces the real (heavy) `pico_shared/FrensHelpers.h` |
| `shim/settings.h` | minimal `settings` struct used by `FDS_AutoInsertEnabled` and the sprite limit |
| `ppm2png.py` | PPM → PNG converter, Python stdlib only (no PIL/ImageMagick needed) |

## Build

```sh
hosttest/build.sh            # -> hosttest/nes_host
hosttest/build.sh -O2        # extra arguments go to g++
```

- With SDL2 installed (`libsdl2-dev`) the harness is built with live mode
  (see [Live mode](#live-mode-nes_live1)); without it, it builds as before.
- AddressSanitizer is intentional: it doubles as a memory-bug detector for
  the core. Remove `-fsanitize=address` from `build.sh` for faster runs.
- `-I .` (repo root, listed last so the shims keep priority) is there for
  `zapper.h`, which `K6502_rw.h` includes.
- `-DPICO_RP2350=1` enables the MMC5 / VRC7 CHR-RAM / FDS code paths.
- `-DNDEBUG` collapses `util/work_meter.h` to empty inlines.

## Run

```sh
./hosttest/nes_host <rom.nes|rom.fds> <total-frames> <dump-every-N> [outdir]
NES_LIVE=1 ./hosttest/nes_host <rom.nes|rom.fds> [total-frames] [dump-every-N] [outdir]

# examples
./hosttest/nes_host "Super Mario Bros.nes"      600 60  hosttest/out
./hosttest/nes_host "Akumajou Densetsu (J).nes" 800 100 hosttest/out   # MMC5
./hosttest/nes_host "Zelda no Densetsu (J).fds" 600 60  hosttest/out   # FDS
python3 hosttest/ppm2png.py hosttest/out/frame_00200.ppm   # -> .png next to it
```

A `.fds` extension auto-routes to `fdsParse()` instead of the iNES path.
Frames are written as `outdir/frame_NNNNN.ppm`, 256×240, RGB.

## FDS BIOS

FDS games need an 8 KB Famicom Disk System BIOS. Put it at:

```
$NES_FAT_ROOT/bios/fds-bios.rom    (default $NES_FAT_ROOT = ".")
```

i.e. `./bios/fds-bios.rom` if you run the harness from the repo root. Sidecar
save files (`*.SAV`) are written under `$NES_FAT_ROOT/saves/`.

## Environment variables

| Variable | Effect |
|---|---|
| `NES_PRESS_START=<frame>` | hold START for 10 frames starting there (gets past title screens) |
| `NES_PRESS_KEYS=<f>:<hex>[,<f>:<hex>...]` | hold the given button mask 10 frames at each frame |
| `NES_PRESS_KEYS2=<f>:<hex>[,...]` | the same for controller 2 |
| `NES_HOLD_A=<frame>` | autofire button A (4 frames on / 4 off) from that frame on |
| `NES_REGION=ntsc\|pal\|dendy` | override `InfoNES_DetectRegion` (CRC lookup still runs, but result is overridden) |
| `NES_DUMP_REGS=1` | print PPU R0..R7, scanline, PAD1 latch, mapper every 100 frames |
| `NES_FRAME_CRC=1` | print `CRC <frame> <crc32>` for every rendered frame |
| `NES_DUMP_VRAM=1` | write `ppuram.bin` (16 KB) and `sprram.bin` (256 B) to outdir at exit |
| `NES_FDS_DISK_SIDE=<N>` | (FDS only) call `fdsRequestSwap(N)` once at startup |
| `NES_FDS_SWAP=<f>:<side>[,...]` | (FDS only) call `fdsRequestSwap(side)` at each frame, like the menu's disk swap |
| `NES_FDS_SAVE=<path stem>` | (FDS only) load `<stem>.SAV` before the reset and write it back at exit, as the device does; the stem is a FatFs path, e.g. `/saves/game_fds` |
| `NES_NO_PSRAM=1` | `isPsramEnabled()` returns false, so FDS uses the copy-on-write disk image of boards without PSRAM |
| `NES_FAT_ROOT=<dir>` | root directory for FatFs paths; default `.` |
| `NES_SAVE_STATE=<frame>` | call `Emulator_SaveState` at that frame |
| `NES_LOAD_STATE=<frame>` | call `Emulator_LoadState` at that frame |
| `NES_STATE_PATH=<file>` | state file for the two above; default `<outdir>/host.state` |
| `NES_NO_SPRITE_LIMIT=1` | draw every sprite on a scanline, like the settings menu's Sprite Limit OFF; default is the hardware limit of 8 |
| `NES_LIVE=1` | window, sound and keyboard in real time (SDL2 build), see [Live mode](#live-mode-nes_live1) |
| `NES_SCALE=<n>` | live window scale, default 3 (768×720) |
| `NES_MUTE=1` | live mode without sound output |
| `NES_PAD_REC=<file>` | record both controllers per frame, see [Recording and replaying input](#recording-and-replaying-input) |
| `NES_PAD_PLAY=<file>` | replay a `NES_PAD_REC` recording |
| `NES_AUDIO_OUT=<file>` | write the mixed sound as raw 44.1 kHz s16 stereo; play it with `aplay -f S16_LE -r 44100 -c 2 <file>` |

`NES_SAVE_STATE` / `NES_LOAD_STATE` print `SAVESTATE frame=N rc=R` and
`LOADSTATE frame=N rc=R`, so `state.cpp` can be exercised without a board. Note
that FatFs paths are rewritten through `$NES_FAT_ROOT`, so pass `NES_FAT_ROOT=/`
when the state path is absolute. To check that a load truly restores rather than
merely returning 0, save at frame A in one run and load at frame B in a second,
then confirm the second run's frame B+k CRC matches the first run's A+k:

```sh
NES_FAT_ROOT=/ NES_SAVE_STATE=250 NES_FRAME_CRC=1 ./hosttest/nes_host rom.nes 400 0 out >a.txt
NES_FAT_ROOT=/ NES_LOAD_STATE=300 NES_FRAME_CRC=1 ./hosttest/nes_host rom.nes 400 0 out >b.txt
```

Button mask (per joypad, hex):

| Bit | Value | Button |
|---|---|---|
| 0 | 0x01 | A |
| 1 | 0x02 | B |
| 2 | 0x04 | SELECT |
| 3 | 0x08 | START |
| 4 | 0x10 | UP |
| 5 | 0x20 | DOWN |
| 6 | 0x40 | LEFT |
| 7 | 0x80 | RIGHT |

Example: `NES_PRESS_KEYS=120:08,200:11` taps START at frame 120, then holds
UP+A at frame 200 for 10 frames each.

## Live mode (`NES_LIVE=1`)

`NES_LIVE=1` opens a window that shows every frame, plays the sound and reads
the keyboard as controller 1, paced to real time (60 fps, 50 for PAL and
Dendy). Only the ROM is required:

```sh
NES_LIVE=1 ./hosttest/nes_host "Super Mario Bros.nes"
NES_LIVE=1 ./hosttest/nes_host game.nes 3599 600 hosttest/out   # one minute, a dump every 10 s
```

Without a frame count the run lasts until the window is closed. Frames are
dumped only with F12, unless `dump-every-N` is given. The final-frame dump
of a headless run is not written. All other environment variables still
apply; scripted input (`NES_PRESS_*`, `NES_HOLD_A`) is ORed with the keyboard.

| Key | Action |
|---|---|
| Arrow keys | D-pad |
| Z / X | A / B |
| S / A | START / SELECT |
| Space | pause or resume |
| N | while paused: run one frame (held: keeps stepping) |
| Tab (hold) | fast-forward, without sound |
| F12 | write the frame on screen to `<outdir>/frame_NNNNN.ppm`, also while paused |
| Esc | quit (closing the window or Ctrl-C does the same) |

The pad keys are those of a USB keyboard on the device
(`pico_shared/hid_app.cpp`). Keys only work while the window has focus. There
is no reset key (the device has none either) and no disk-swap key; use
`NES_FDS_SWAP` for FDS games that do not swap automatically.

The window title shows the frame number, as used by `NES_SAVE_STATE`,
`NES_PRESS_KEYS` and the dump file names, and the measured frame rate.
Pacing follows the sound output; with `NES_MUTE=1` or without a sound
device, a timer is used. Live mode shows what the core draws and plays, not
how fast the board runs it.

Under WSL2 the window and sound go through WSLg. The first `LIVE:` line
shows the drivers SDL chose; `SDL_VIDEODRIVER` and `SDL_AUDIODRIVER` override
them. The WSLg sound output sometimes stalls for a few seconds; the game
keeps running and the sound comes back by itself.

## Recording and replaying input

`NES_PAD_REC=<file>` records both controllers for every frame of a run, live
or headless. `NES_PAD_PLAY=<file>` feeds a recording back, and the run
reproduces it exactly: every frame and every sample. This turns a glitch
seen while playing into something the headless tools can examine:

```sh
NES_LIVE=1 NES_PAD_REC=/tmp/smb.pad ./hosttest/nes_host smb.nes
# pause at the glitch, step to it with N, press F12:
#   LIVE: frame 2417 dumped to hosttest/out/frame_02417.ppm
mkdir -p hosttest/out/replay
NES_PAD_PLAY=/tmp/smb.pad ./hosttest/nes_host smb.nes 2430 1 hosttest/out/replay
cmp hosttest/out/frame_02417.ppm hosttest/out/replay/frame_02417.ppm   # identical
```

The file is plain text, one line per change; `#` starts a comment:

```
# pico-infonesPlus host-harness input recording
# rom: /home/frank/roms/nes/smb.nes
# <frame> <pad1> <pad2> | <frame> end
149 08 00
152 00 00
242 81 00
846 end
```

`<frame> <pad1> <pad2>` sets both controllers from that frame on, as hex
masks in the bit order above (`<pad2>` may be left out: 00). `<frame> end`
marks where the recording stopped. Frame numbers must not decrease. A file
written by hand needs no `end` line; its last masks then last until the run
ends.

Until the `end` line the recording replaces the keyboard and the scripted
input. After it, they take over again: a live replay hands control back to
the keyboard, which is a way to return to a point deep in a game. Record the
continuation with `NES_PAD_REC` to a new file.

For an exact replay, use the same ROM and the same machine settings. The
header lists those that were set (`NES_REGION`, `NES_NO_PSRAM`,
`NES_NO_SPRITE_LIMIT`, `NES_FDS_*`, the state options, `NES_FAT_ROOT` and
`ASAN_OPTIONS`). Use a build with the same sanitizer setting, since it
decides what freshly allocated save RAM contains. After a core change, a
replay shows what the change does with the same input.

Recording and replay, like live mode and `NES_AUDIO_OUT`, render the sound;
a plain headless run does not. Rendering is part of the emulated machine
(the DMC fetches its sample bytes from the CPU bus while it renders), so a
recording replays exactly only with `NES_PAD_PLAY`. In practice this rarely
matters: 389 ROMs ran 900 frames each with identical frames either way.

## Caveats

- Host runs are fully deterministic: no PSRAM latency, no input-timing
  variation. A bug that is *intermittent* on the device usually shows up
  here as its always-broken variant.
- `NES_FRAME_CRC=1` is the cheap way to find where two builds diverge --
  diff the two CRC streams instead of dumping thousands of images. Running
  the *same* tree at `-O1` and `-O2` and diffing the streams is also a good
  undefined-behaviour probe: the core should be bit-identical either way, and
  it was not until the out-of-range `NesPalette[]` index was fixed.
- Frames are unpacked the way the picoDVI build's `CC()` macro reads the
  palette table (RGB444), so host output matches a picoDVI board. HSTX boards
  use a different palette table in `main.cpp`, so their colours differ.
- Sound is rendered and mixed (as the device does, with its default DVI
  gain) only in live mode and with `NES_AUDIO_OUT`, `NES_PAD_REC` or
  `NES_PAD_PLAY`. A plain headless run renders none, as before.
- Zapper support compiles out (`ZAPPER_D3`/`ZAPPER_D4` are undefined here, so
  `ZAPPER_SUPPORTED` is 0), and `$4017` reads behave exactly as before.
- NSF files aren't auto-detected (no `.nsf` dispatch in the harness).
- The harness does NOT load NVRAM; cartridge save RAM starts empty every run.
- `isPsramEnabled()` returns true unless `NES_NO_PSRAM` is set, so FDS
  expands every side into host RAM like a PSRAM board. The copy-on-write
  image must produce the same frame CRCs.
- Without PSRAM the device checks the free heap before each disk page; the
  host skips that check, so an out-of-memory drop never happens here.
- Region detection runs the real MesenDB CRC lookup, so games with PAL/Dendy
  entries pick the right timing automatically; `NES_REGION=…` forces it.
