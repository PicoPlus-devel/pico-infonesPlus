// Host harness: run the InfoNES core headless on Linux, dump frames as PPM.
// Usage: nes_host <rom.nes|rom.fds> <frames> <dump-every> [outdir]
// See README.md for env-var controls (input injection, region override,
// VRAM/register dumps, BIOS path for FDS).
//
// Driving model
// -------------
// InfoNES_Cycle() is an infinite for(;;) loop; the only clean way out is to
// set PAD_SYS_QUIT in InfoNES_PadState. The host injects per-frame work via
// InfoNES_LoadFrame() (called once per frame at InfoNES.cpp:956), then
// signals quit via InfoNES_PadState() right after total_frames is reached.
//
// Rendering model
// ---------------
// Matches the RP2350 framebuffer path in main.cpp:1294-1308: each scanline's
// WORD buffer is set to point directly into the 320*240 Frens::framebuffer
// at line*320 + 32. PostDrawLine is a no-op — pixels already live in the
// framebuffer. dump_ppm reads the centre 256 columns and unpacks them to
// RGB888 the same way the picoDVI build's CC() macro does.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstdarg>
#include <cctype>
#include <sys/stat.h>
#include <string>

#include "InfoNES.h"
#include "InfoNES_Mapper.h"
#include "InfoNES_System.h"
#include "InfoNES_Region.h"
#include "InfoNES_FDS.h"
#include "FrensHelpers.h"

// The real FrensHelpers.h declares this; the device build sets it from the
// flash-loaded ROM address. We own it on host and point it at the in-memory
// ROM buffer so FDS code paths that key off it stay consistent.
uintptr_t ROM_FILE_ADDR = 0;

// ----------------------------------------------------------------------
// NES palette — 64 entries, verbatim from the canonical table in
// main.cpp:161-169. These are the raw pre-CC() words the picoDVI build feeds
// to its RGB444 packing macro: red in bits 11-14, green in bits 6-9, blue in
// bits 1-4 (the remaining low bits are dropped by CC()). dump_ppm unpacks
// them the same way, so host frames match the picoDVI output.
// ----------------------------------------------------------------------
const WORD NesPalette[64] = {
    0x39ce, 0x1071, 0x0015, 0x2013, 0x440e, 0x5402, 0x5000, 0x3c20,
    0x20a0, 0x0100, 0x0140, 0x00e2, 0x0ceb, 0x0000, 0x0000, 0x0000,
    0x5ef7, 0x01dd, 0x10fd, 0x401e, 0x5c17, 0x700b, 0x6ca0, 0x6521,
    0x45c0, 0x0240, 0x02a0, 0x0247, 0x0211, 0x0000, 0x0000, 0x0000,
    0x7fff, 0x1eff, 0x2e5f, 0x223f, 0x79ff, 0x7dd6, 0x7dcc, 0x7e67,
    0x7ae7, 0x4342, 0x2769, 0x2ff3, 0x03bb, 0x0000, 0x0000, 0x0000,
    0x7fff, 0x579f, 0x635f, 0x6b3f, 0x7f1f, 0x7f1b, 0x7ef6, 0x7f75,
    0x7f94, 0x73f4, 0x57d7, 0x5bf9, 0x4ffe, 0x0000, 0x0000, 0x0000,
};

// ----------------------------------------------------------------------
// CRC32. Same polynomial (0xEDB88320) and ~crc xor-final convention as
// pico_shared/crc32.cpp:update_crc32. Lazy-build the table at first use.
// NES ROMs: pass offset=16 to skip the iNES header, matching the device
// (FrensHelpers.cpp:1082 sets crcOffset=16 for NES). FDS images: offset=0.
// ----------------------------------------------------------------------
static uint32_t crc32_buf(const void *data, size_t size, int offset)
{
    static uint32_t table[256];
    static bool inited = false;
    if (!inited) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        inited = true;
    }
    if ((int)size <= offset) return 0;
    const uint8_t *p = (const uint8_t *)data + offset;
    size_t n = size - (size_t)offset;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
        c = (c >> 8) ^ table[(c ^ p[i]) & 0xFF];
    return c ^ 0xFFFFFFFFu;
}

// ----------------------------------------------------------------------
// Runtime state — populated from argv / env at startup.
// ----------------------------------------------------------------------
struct KeyEvent { int frame; uint8_t mask; };
static struct {
    int total_frames;
    int dump_every;
    std::string outdir;
    int press_start;        // tap START 10 frames from this frame
    int hold_a;             // autofire A from this frame on
    int dump_regs;          // print PPU regs every 100 frames
    int frame_crc;          // print a CRC32 of every rendered frame
    int dump_vram;          // dump PPURAM/SPRRAM at exit
    int fds_disk_side;      // -1 = no override
    KeyEvent keys[32];
    int keys_n;
} cfg;

static int      g_frame      = 0;
static bool     g_quit       = false;
static uint8_t  g_pad1_mask  = 0;
static uint8_t  g_pad2_mask  = 0;

// ----------------------------------------------------------------------
// PPM writer — centre 256 cols of the 320-wide framebuffer.
// ----------------------------------------------------------------------
static void dump_ppm(int frame)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/frame_%05d.ppm",
             cfg.outdir.c_str(), frame);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }

    constexpr int W = 256, H = 240;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) {
        const WORD *src = &Frens::framebuffer[y * SCREENWIDTH + 32];
        for (int x = 0; x < W; x++) {
            uint16_t p = src[x];
            // Unpack exactly like the device's CC() macro in main.cpp reads
            // these table entries: red bits 11-14, green bits 6-9, blue bits
            // 1-4, i.e. the RGB444 the picoDVI path actually emits. Bit 15 is
            // the emulator's backdrop marker (PalTable | 0x8000) and is
            // ignored here, exactly as the display hardware ignores it.
            uint8_t rgb[3] = {
                (uint8_t)(((p >> 11) & 0xF) * 17),
                (uint8_t)(((p >>  6) & 0xF) * 17),
                (uint8_t)(((p >>  1) & 0xF) * 17),
            };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

// ----------------------------------------------------------------------
// Env-var parsing.
// NES_PRESS_KEYS=<frame>:<hex>[,<frame>:<hex>...] — hold mask for 10 frames
// each.
// ----------------------------------------------------------------------
static int get_env_int(const char *name, int fallback)
{
    const char *s = getenv(name);
    return s ? atoi(s) : fallback;
}

static int parse_region(const char *s, int fallback)
{
    if (!s) return fallback;
    if (!strcasecmp(s, "ntsc"))  return INFONES_REGION_NTSC;
    if (!strcasecmp(s, "pal"))   return INFONES_REGION_PAL;
    if (!strcasecmp(s, "dendy")) return INFONES_REGION_DENDY;
    return atoi(s);
}

static void parse_keys_env()
{
    const char *e = getenv("NES_PRESS_KEYS");
    if (!e) return;
    char buf[1024];
    strncpy(buf, e, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    char *tok = strtok(buf, ",");
    while (tok && cfg.keys_n < 32) {
        char *colon = strchr(tok, ':');
        if (colon) {
            *colon = 0;
            cfg.keys[cfg.keys_n].frame = atoi(tok);
            cfg.keys[cfg.keys_n].mask  = (uint8_t)strtoul(colon + 1, nullptr, 16);
            cfg.keys_n++;
        }
        tok = strtok(nullptr, ",");
    }
}

// ----------------------------------------------------------------------
// Minimal iNES parser — only the header fields the core actually reads.
// Sets ROM/VROM/MapperNo/NesHeader globals exactly like main.cpp:650-700.
// ----------------------------------------------------------------------
static bool parse_ines(uint8_t *buf, size_t size)
{
    if (size < 16 || memcmp(buf, "NES\x1A", 4) != 0) return false;
    memcpy(&NesHeader, buf, sizeof(NesHeader));
    uint8_t *p = buf + 16;
    if (NesHeader.byInfo1 & 4) p += 512;     // trainer

    ROM = p;
    p += NesHeader.byRomSize * 0x4000;
    VROM = NesHeader.byVRomSize ? p : nullptr;

    MapperNo      = (NesHeader.byInfo1 >> 4) | (NesHeader.byInfo2 & 0xF0);
    ROM_Mirroring = NesHeader.byInfo1 & 1;
    ROM_SRAM      = (NesHeader.byInfo1 & 2) ? 1 : 0;
    ROM_Trainer   = (NesHeader.byInfo1 & 4) ? 1 : 0;
    ROM_FourScr   = (NesHeader.byInfo1 & 8) ? 1 : 0;
    IsFDS = IsNSF = false;
    return true;
}

static bool ends_with_ci(const char *s, const char *suf)
{
    size_t ls = strlen(s), lf = strlen(suf);
    if (lf > ls) return false;
    return strcasecmp(s + ls - lf, suf) == 0;
}

// ----------------------------------------------------------------------
// Optional PPU register snapshot every 100 frames.
// ----------------------------------------------------------------------
static void dump_regs_snapshot()
{
    printf("F%05d  R0=%02X R1=%02X R2=%02X R3=%02X R7=%02X  "
           "Scan=%u  PAD1=%08X  Mapper=%u\n",
           g_frame, PPU_R0, PPU_R1, PPU_R2, PPU_R3, PPU_R7,
           (unsigned)PPU_Scanline, (unsigned)PAD1_Latch, MapperNo);
}

// ----------------------------------------------------------------------
// Optional VRAM/SPRRAM dump at exit.
// ----------------------------------------------------------------------
static void dump_vram_files()
{
    char path[512];
    snprintf(path, sizeof(path), "%s/ppuram.bin", cfg.outdir.c_str());
    if (FILE *f = fopen(path, "wb")) { fwrite(PPURAM, 1, 0x4000, f); fclose(f); }
    snprintf(path, sizeof(path), "%s/sprram.bin", cfg.outdir.c_str());
    if (FILE *f = fopen(path, "wb")) { fwrite(SPRRAM, 1, 256,    f); fclose(f); }
}

// =====================================================================
// InfoNES_* callbacks (declared in InfoNES_System.h).
// =====================================================================

void InfoNES_PreDrawLine(int line)
{
    // Match main.cpp:1296-1297 (RP2350 framebuffer path): hand the core a
    // pointer 32 pixels into row `line` of the full framebuffer, with a
    // width budget that fits the rest of the 320-wide row.
    InfoNES_SetLineBuffer(&Frens::framebuffer[line * SCREENWIDTH] + 32,
                          (WORD)(SCREENWIDTH - 32));
}

void InfoNES_PostDrawLine(int /*line*/)
{
    // Nothing to do — line already lives in Frens::framebuffer.
}

int InfoNES_LoadFrame()
{
    // Called once per frame at InfoNES.cpp:956, right before InfoNES_PadState
    // at line 980. The framebuffer at this point holds the just-rendered
    // frame, so dumps and instrumentation here are safe.
    if (cfg.dump_every > 0 && g_frame % cfg.dump_every == 0)
        dump_ppm(g_frame);
    if (cfg.dump_regs && g_frame > 0 && g_frame % 100 == 0)
        dump_regs_snapshot();
    if (cfg.frame_crc) {
        // One line per frame, so two runs can be diffed to find the exact
        // frame where behaviour diverges without dumping every image.
        printf("CRC %5d %08X\n", g_frame,
               crc32_buf(Frens::framebuffer,
                         SCREENWIDTH * 240 * sizeof(WORD), 0));
    }

    // Decide input for this frame.
    uint8_t mask = 0;
    if (cfg.press_start >= 0 &&
        g_frame >= cfg.press_start && g_frame < cfg.press_start + 10)
        mask |= 0x08;                              // START
    if (cfg.hold_a >= 0 && g_frame >= cfg.hold_a && (g_frame & 4))
        mask |= 0x01;                              // A
    for (int i = 0; i < cfg.keys_n; i++) {
        if (g_frame >= cfg.keys[i].frame &&
            g_frame <  cfg.keys[i].frame + 10)
            mask |= cfg.keys[i].mask;
    }
    g_pad1_mask = mask;
    g_pad2_mask = 0;

    if (g_frame >= cfg.total_frames) g_quit = true;
    g_frame++;
    return 0;
}

void InfoNES_PadState(DWORD *pad1, DWORD *pad2, DWORD *sys)
{
    *pad1 = g_pad1_mask;
    *pad2 = g_pad2_mask;
    *sys  = g_quit ? PAD_SYS_QUIT : 0;
    if (getenv("NES_TRACE_PAD") && g_pad1_mask)
        printf("F%05d pad1=%02X PAD1_Bit=%u\n",
               g_frame, g_pad1_mask, (unsigned)PAD1_Bit);
}

int InfoNES_Menu() { return 0; }   // skip menu, start emulation

int InfoNES_ReadRom(const char * /*name*/) { return -1; }
void InfoNES_ReleaseRom() {}

// =====================================================================
// main
// =====================================================================
int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s <rom.nes|rom.fds> <frames> <dump-every> [outdir]\n",
                argv[0]);
        return 1;
    }
    const char *rom_path = argv[1];
    cfg.total_frames = atoi(argv[2]);
    cfg.dump_every   = atoi(argv[3]);
    cfg.outdir       = argc > 4 ? argv[4] : "hosttest/out";
    mkdir(cfg.outdir.c_str(), 0755);

    cfg.press_start    = get_env_int("NES_PRESS_START", -1);
    cfg.hold_a         = get_env_int("NES_HOLD_A",      -1);
    cfg.dump_regs      = get_env_int("NES_DUMP_REGS",    0);
    cfg.frame_crc      = get_env_int("NES_FRAME_CRC",    0);
    cfg.dump_vram      = get_env_int("NES_DUMP_VRAM",    0);
    cfg.fds_disk_side  = get_env_int("NES_FDS_DISK_SIDE", -1);
    parse_keys_env();

    // Load ROM into memory.
    FILE *f = fopen(rom_path, "rb");
    if (!f) { perror(rom_path); return 1; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *rom = (uint8_t *)malloc(size);
    if (!rom) { fprintf(stderr, "malloc(%ld) failed\n", size); return 1; }
    if (fread(rom, 1, size, f) != (size_t)size) {
        fprintf(stderr, "short read\n"); return 1;
    }
    fclose(f);
    ROM_FILE_ADDR = (uintptr_t)rom;

    // NES ROMs: skip the 16-byte iNES header for CRC (matches the device's
    // crcOffset=16 in pico_shared/FrensHelpers.cpp:1082). FDS: no offset.
    bool is_fds = ends_with_ci(rom_path, ".fds");
    int  crc_offset = is_fds ? 0 : 16;
    uint32_t rom_crc = crc32_buf(rom, (size_t)size, crc_offset);

    int region = parse_region(getenv("NES_REGION"),
                              InfoNES_DetectRegion((uintptr_t)rom, rom_crc, rom_path));
    InfoNES_SetRegion(region);
    InfoNES_Init();

    bool ok;
    if (is_fds) {
        ok = fdsParse(rom, (size_t)size);
    } else {
        ok = parse_ines(rom, (size_t)size);
    }
    if (!ok) { fprintf(stderr, "ROM parse failed for %s\n", rom_path); return 1; }

    if (InfoNES_Reset() < 0) {
        fprintf(stderr, "InfoNES_Reset failed\n"); return 1;
    }

    if (cfg.fds_disk_side >= 0 && IsFDS) {
        fdsRequestSwap(cfg.fds_disk_side);
    }

    printf("ROM %s loaded (%ld bytes, fds=%d, mapper=%u, region=%d, crc=%08X). Running %d frames.\n",
           rom_path, size, (int)IsFDS, MapperNo, region, rom_crc, cfg.total_frames);

    InfoNES_Cycle();           // returns when InfoNES_PadState raises PAD_SYS_QUIT

    if (cfg.dump_vram) dump_vram_files();
    dump_ppm(g_frame);         // final frame snapshot

    InfoNES_Fin();
    free(rom);
    printf("done. %d frames rendered.\n", g_frame);
    return 0;
}
