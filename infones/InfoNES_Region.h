#ifndef INFONES_PALROMS_H_INCLUDED
#define INFONES_PALROMS_H_INCLUDED

#include <stdint.h>

/* CRC32 -> PAL lookup. Returns 2 when the given ROM CRC is on the Dendy
 * list, 1 when on the PAL list, 0 (NTSC) otherwise. The tables themselves
 * live in InfoNES_Region.cpp — add new entries there. */
int InfoNES_DetectRegion(uintptr_t addr, uint32_t crc, const char *romName);

/* CRC32 of the ROM last passed to InfoNES_DetectRegion(). That runs before the
 * mapper is initialised, so a mapper can use it to recognise a board. */
extern uint32_t InfoNES_RomCrc;

#endif /* INFONES_PALROMS_H_INCLUDED */
