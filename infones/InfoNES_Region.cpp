/*
 * Region detection for NES ROMs.
 *
 * The NES emulator core itself is region-agnostic; the calling shell (e.g.
 * main.cpp) decides PAL vs NTSC vs Dendy and passes the result into
 * InfoNES_Main(). This file provides InfoNES_DetectRegion(), which uses
 * three layered heuristics — NES 2.0 header, CRC32 database, filename tag —
 * to pick the right timing for a given cartridge image.
 */

#include "InfoNES_Region.h"
#include "stdio.h"
#include "string.h"
#include "ctype.h"
#include <algorithm>
#include <iterator>

// CRC32 -> region table generated from the Mesen game database.
// Provides `mesenNesDB[]` and the MESEN_* enum used in the lookup below.
#include "MesenNesDB.h"

// Set by InfoNES_DetectRegion(); see InfoNES_Region.h.
uint32_t InfoNES_RomCrc = 0;

// 16-byte iNES / NES 2.0 file header, laid out to match the on-disk format
// so it can be cast directly over the start of a ROM image.
typedef struct {
    uint8_t magic[4];   // "NES\x1A"
    uint8_t prg_banks;
    uint8_t chr_banks;
    uint8_t flags6;
    uint8_t flags7;
    uint8_t flags8;     // NES 2.0: high bits of PRG/CHR size
    uint8_t flags9;     // NES 2.0: PRG/CHR size MSB
    uint8_t flags10;    // iNES: TV system (unreliable); NES 2.0: PRG RAM
    uint8_t flags11;    // NES 2.0: CHR RAM
    uint8_t flags12;    // NES 2.0: timing/region
    uint8_t flags13;
    uint8_t flags14;
    uint8_t flags15;
} NESHeader;

// Region encoding used by the NES 2.0 flags12 field (bits 1:0).
typedef enum {
    REGION_NTSC  = 0,
    REGION_PAL   = 1,
    REGION_MULTI = 2,   // supports both
    REGION_DENDY = 3,   // Russian clone
} NESRegion;

// True when the header advertises NES 2.0 format. The spec encodes this in
// flags7 bits 3:2 as the pattern 0b10 — distinguishing it from plain iNES
// (which leaves these bits clear) and from corrupted/archaic dumps.
static bool is_nes2(const NESHeader *h) {
    return (h->flags7 & 0x0C) == 0x08;
}

// Heuristic region guess based on No-Intro style filename tags such as
// "(Europe)" or "(USA)". Used only as a last resort when the header and
// CRC database both come up empty. Defaults to NTSC on no match.
static NESRegion region_from_filename(const char *filename) {
    // No-Intro tags: (Europe), (Australia), (Sweden), etc. → PAL
    // (USA), (Japan) → NTSC
    static const char *pal_tags[]  = {"(Europe)", "(Australia)", "(Sweden)",
                                       "(Germany)", "(France)", "(Spain)",
                                       "(Italy)", "(Netherlands)", "(E)", NULL};
    static const char *ntsc_tags[] = {"(USA)", "(Japan)", "(U)", "(J)", NULL};
    printf("Checking filename for region tags: %s\n", filename);
    for (int i = 0; pal_tags[i]; i++) {
        if (strstr(filename, pal_tags[i])) {
            printf("Found PAL tag in filename: %s\n", pal_tags[i]);
            return REGION_PAL;
        }
    }
   
    for (int i = 0; ntsc_tags[i]; i++) {
        if (strstr(filename, ntsc_tags[i])) {
            printf("Found NTSC tag in filename: %s\n", ntsc_tags[i]);
            return REGION_NTSC;
        }
    }
    printf("No region tag found in filename, assuming NTSC.\n");
    return REGION_NTSC; // default assumption
}

// Decide which timing the emulator core should use for the ROM at `addr`.
//
// Parameters:
//   addr    - pointer (as an integer) to the first byte of the iNES image,
//             i.e. the start of the 16-byte header.
//   crc     - CRC32 of the PRG+CHR payload (header excluded), used for the
//             Mesen database lookup step.
//   romName - filename of the ROM, consulted only as a final fallback.
//
// Returns:
//   0 = NTSC (default), 1 = PAL, 2 = Dendy / Famiclone (50 Hz, non-PAL timing).
//
// Detection priority, highest to lowest:
//   1. NES 2.0 header flags12 region bits — authoritative when present.
//   2. CRC32 lookup in the Mesen database (mesenNesDB).
//   3. No-Intro style filename tag.
// An invalid header short-circuits to NTSC to avoid acting on garbage.
int InfoNES_DetectRegion(uintptr_t addr, uint32_t crc, const char* romName)
{
    auto *p = reinterpret_cast<const uint8_t *>(addr);
    NESHeader *h = (NESHeader *)p;
    printf("Detecting region for ROM CRC %08X\n", crc);
    InfoNES_RomCrc = crc;
    // Guard against garbage input — without this, random bytes could hit
    // the NES 2.0 path below and yield a bogus region from flags12.
    if (h->magic[0] != 'N' || h->magic[1] != 'E' || h->magic[2] != 'S' || h->magic[3] != 0x1A) {
        printf("Invalid NES header magic: %02X %02X %02X %02X\n", h->magic[0], h->magic[1], h->magic[2], h->magic[3]);
        return 0; // default to NTSC if not a valid NES header
    }
    // 1. NES 2.0 header is authoritative
    if (is_nes2(h)) {
        printf("NES 2.0 header detected.\n");
        int region = (h->flags12 ) & 0x03;
        // print the region for debugging
        switch (region) {
            case REGION_NTSC:  printf("Region: NTSC\n"); break; 
                case REGION_PAL:   printf("Region: PAL\n"); break; 
                case REGION_MULTI: printf("Region: MULTI\n"); break; 
                case REGION_DENDY: printf("Region: DENDY\n"); break;
        }
        if (region == REGION_PAL)
            return 1;
        if (region == REGION_DENDY)
            return 2;
        return 0;
    } else {
        printf("Not an NES 2.0 header (flags7=%02X); falling back to CRC lookup.\n", h->flags7);
    }
    // 2. Database lookup by CRC32 against the Mesen game DB.
    //    Mesen tags many systems (Vs., PlayChoice-10, VT0x, …) but for
    //    timing purposes we only care about PAL vs. Dendy/Famiclone vs.
    //    NTSC — anything not explicitly PAL/Dendy is treated as NTSC.
    // Binary search: mesenNesDB is sorted ascending by `crc`.
    auto begin = std::begin(mesenNesDB);
    auto end   = std::end(mesenNesDB);
    auto it = std::lower_bound(begin, end, crc,
        [](const GameEntry &e, uint32_t c) { return e.crc < c; });
    if (it != end && it->crc == crc) {
        printf("CRC found in MesenDB: system=%d\n", it->system);
        switch (it->system) {
            case MESEN_PAL:
                return 1;
            case MESEN_DENDY:
            case MESEN_FamicloneDecimal:
                return 2;
            default:
                return 0;
        }
    }
    printf("CRC not found in MesenDB; falling back to filename-based detection.\n");
    // 3. Fallback to filename-based detection
    NESRegion region = region_from_filename(romName);
    if (region == REGION_PAL)
        return 1;
    if (region == REGION_DENDY)
        return 2;

    
    return 0;
}
