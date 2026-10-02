#!/bin/bash
# Build the host harness (hosttest/nes_host); run from anywhere. Extra
# arguments are passed to g++, e.g. hosttest/build.sh -O2 to override -O1.
# With SDL2 installed (libsdl2-dev) the harness also gets live mode
# (NES_LIVE=1, host_sdl.cpp); without it, it builds as before.
set -e
cd "$(dirname "$0")/.."   # repo root

SDL=()
if pkg-config --exists sdl2 2>/dev/null; then
    SDL=(-DHOST_SDL=1 hosttest/host_sdl.cpp $(pkg-config --cflags --libs sdl2))
    echo "SDL2 $(pkg-config --modversion sdl2) found: building with live mode (NES_LIVE=1)"
else
    echo "SDL2 not found: building without live mode (install libsdl2-dev for NES_LIVE=1)"
fi

g++ -O1 -g -fsanitize=address -std=gnu++17 \
  -DPICO_RP2350=1 -DNDEBUG -DPICO_NO_HARDWARE=1 \
  -I hosttest/shim -I infones -I pico_lib -I pico_shared -I . \
  -o hosttest/nes_host \
  hosttest/host_main.cpp hosttest/stubs.cpp state.cpp \
  infones/InfoNES.cpp infones/K6502.cpp infones/InfoNES_Mapper.cpp \
  infones/InfoNES_pAPU.cpp infones/InfoNES_pAPU_Vrc7.cpp infones/InfoNES_Region.cpp \
  infones/InfoNES_NSF.cpp infones/InfoNES_FDS.cpp \
  "${SDL[@]}" "$@"
echo "built: hosttest/nes_host"
