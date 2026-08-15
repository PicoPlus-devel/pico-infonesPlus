# hosttest — Linux host harness for the InfoNES core

Runs the unmodified `infones/` emulator core headless on a Linux PC, dumping
frames as images. Useful for debugging core behavior (PPU rendering, CPU
timing, mapper state) with fast iteration and full instrumentation — no Pico
flashing, no serial console. Only hardware-specific issues (HSTX/DVI output,
SD card, PSRAM latency, audio sinks) still need the real device.

The harness builds against the **RP2350 + framebuffer** configuration:
`PICO_RP2350=1`, `FRAMEBUFFERISPOSSIBLE=1`, `isPsramEnabled()=true`. That
unlocks MMC5, VRC7 CHR-RAM, the Famicom Disk System (FDS), and the
320×240 full-frame rendering path the device uses on RP2350.

## Layout

| File | Purpose |
|---|---|
| `host_main.cpp` | main loop, InfoNES_* callbacks, 256×240 PPM dumper, env-var input injection, minimal iNES parser, inline CRC32, NES palette |
| `stubs.cpp` | Frens helper subset (f_malloc/f_free/isPsramEnabled/...) the core links against, audio callback no-ops, host-stdio-backed FatFs (so FDS BIOS loads), `settings` instance |
| `shim/pico.h` | empty `__not_in_flash_func` / `__not_in_flash` placement macros |
| `shim/pico/time.h` | host `time_us_32` from `clock_gettime` |
| `shim/ff.h` | minimal FatFs surface — backed by stdio in `stubs.cpp` |
| `shim/FrensHelpers.h` | minimal `Frens::*` declarations the core needs; replaces the real (heavy) `pico_shared/FrensHelpers.h` |
| `shim/settings.h` | minimal `settings` struct used by `FDS_AutoInsertEnabled` |
| `ppm2png.py` | PPM → PNG converter, Python stdlib only (no PIL/ImageMagick needed) |

## Build

From the repo root:

```sh
g++ -O1 -g -fsanitize=address -std=gnu++17 \
  -DPICO_RP2350=1 -DNDEBUG -DPICO_NO_HARDWARE=1 \
  -I hosttest/shim -I infones -I pico_lib -I pico_shared -I . \
  -o hosttest/nes_host \
  hosttest/host_main.cpp hosttest/stubs.cpp \
  infones/InfoNES.cpp infones/K6502.cpp infones/InfoNES_Mapper.cpp \
  infones/InfoNES_pAPU.cpp infones/InfoNES_pAPU_Vrc7.cpp infones/InfoNES_Region.cpp \
  infones/InfoNES_NSF.cpp infones/InfoNES_FDS.cpp
```

- AddressSanitizer is intentional: it doubles as a memory-bug detector for
  the core. Drop `-fsanitize=address` for faster runs.
- `-I .` (repo root, listed last so the shims keep priority) is there for
  `zapper.h`, which `K6502_rw.h` includes.
- `-DPICO_RP2350=1` enables the MMC5 / VRC7 CHR-RAM / FDS code paths.
- `-DNDEBUG` collapses `util/work_meter.h` to empty inlines.

## Run

```sh
./hosttest/nes_host <rom.nes|rom.fds> <total-frames> <dump-every-N> [outdir]

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
| `NES_HOLD_A=<frame>` | autofire button A (4 frames on / 4 off) from that frame on |
| `NES_REGION=ntsc\|pal\|dendy` | override `InfoNES_DetectRegion` (CRC lookup still runs, but result is overridden) |
| `NES_DUMP_REGS=1` | print PPU R0..R7, scanline, PAD1 latch, mapper every 100 frames |
| `NES_DUMP_VRAM=1` | write `ppuram.bin` (16 KB) and `sprram.bin` (256 B) to outdir at exit |
| `NES_FDS_DISK_SIDE=<N>` | (FDS only) call `fdsRequestSwap(N)` once at startup |
| `NES_FAT_ROOT=<dir>` | root directory for FatFs paths; default `.` |

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

## Caveats

- Host runs are fully deterministic: no PSRAM latency, no input-timing
  variation. A bug that is *intermittent* on the device usually shows up
  here as its always-broken variant.
- Audio is stubbed entirely (`InfoNES_SoundOutput` is a sink).
- Zapper support compiles out (`ZAPPER_D3`/`ZAPPER_D4` are undefined here, so
  `ZAPPER_SUPPORTED` is 0), and `$4017` reads behave exactly as before.
- NSF files aren't auto-detected (no `.nsf` dispatch in the harness).
- The harness does NOT load NVRAM; cartridge save RAM starts empty every run.
- `isPsramEnabled()` always returns true, so FDS multi-side games keep all
  sides in host RAM (matches the device's PSRAM build behavior).
- Region detection runs the real MesenDB CRC lookup, so games with PAL/Dendy
  entries pick the right timing automatically; `NES_REGION=…` forces it.
