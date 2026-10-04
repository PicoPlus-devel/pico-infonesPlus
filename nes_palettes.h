#ifndef NES_PALETTES_H
#define NES_PALETTES_H
#include "InfoNES_Types.h"
#include "FrensHelpers.h"

// Selectable NES palettes, generated into nes_palettes.cpp by
// assets/Palettes/pal2c.py. Index 0 is the palette the board used before
// palettes became selectable. The tables stay in flash; the one in use is
// copied to NesPalette[] in SRAM.
#define NES_PALETTE_COUNT 9

extern const WORD NesPaletteTables[NES_PALETTE_COUNT][64];
extern const char *const NesPaletteNames[NES_PALETTE_COUNT];
extern const char *const NesPaletteDescriptions[NES_PALETTE_COUNT];

#endif
