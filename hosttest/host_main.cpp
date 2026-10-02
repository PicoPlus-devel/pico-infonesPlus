// Host harness: run the InfoNES core headless on Linux, dump frames as PPM.
// Usage: nes_host <rom.nes|rom.fds> <frames> <dump-every> [outdir]
//        NES_LIVE=1 nes_host <rom.nes|rom.fds> [frames] [dump-every] [outdir]
// See README.md for env-var controls (input injection, region override,
// VRAM/register dumps, BIOS path for FDS).
//
// NES_LIVE=1 (when built with SDL2) shows the frames in a window, plays the
// audio and reads the keyboard as controller 1, paced to real time
// (host_sdl.cpp). NES_PAD_REC=<file> records the pads per frame and
// NES_PAD_PLAY=<file> replays them, so a live session reproduces exactly in a
// headless run.
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
#include <climits>
#include <sys/stat.h>
#include <string>
#include <vector>

#include "InfoNES.h"
#include "InfoNES_Mapper.h"
#include "InfoNES_System.h"
#include "InfoNES_Region.h"
#include "InfoNES_FDS.h"
#include "FrensHelpers.h"
#include "state.h"
#if HOST_SDL
#include "host_sdl.h"
#endif

// The real FrensHelpers.h declares this; the device build sets it from the
// flash-loaded ROM address. We own it on host and point it at the in-memory
// ROM buffer so FDS code paths that key off it stay consistent.
uintptr_t ROM_FILE_ADDR = 0;

// ----------------------------------------------------------------------
// NES palette — 64 entries: the picoDVI "Default" palette (row 0 of
// nes_palettes.cpp, from assets/Palettes/Default DVI.pal) as RGB555 words:
// red in bits 10-14, green in 5-9, blue in 0-4. dump_ppm keeps the top four
// bits of each, which is the RGB444 the picoDVI build draws, so host frames
// match the picoDVI output. Not const: the device copies the selected palette
// into it, so InfoNES_System.h declares it writable.
// ----------------------------------------------------------------------
WORD NesPalette[64] = {
    0x39ce, 0x1071, 0x0015, 0x2013, 0x440e, 0x5402, 0x5000, 0x3c20,
    0x20a0, 0x0100, 0x0140, 0x00e2, 0x0ceb, 0x0000, 0x0000, 0x0000,
    0x5ef7, 0x01dd, 0x10fd, 0x401e, 0x5c17, 0x700b, 0x6ca0, 0x6521,
    0x45c0, 0x0240, 0x02a0, 0x0247, 0x0211, 0x0000, 0x0000, 0x0000,
    0x7fff, 0x1eff, 0x2e5f, 0x223f, 0x79ff, 0x7dd6, 0x7dcc, 0x7e67,
    0x7ae7, 0x4342, 0x2769, 0x2ff3, 0x03bb, 0x294a, 0x0000, 0x0000,
    0x7fff, 0x579f, 0x635f, 0x6b3f, 0x7f1f, 0x7f1b, 0x7ef6, 0x7f75,
    0x7f94, 0x73f4, 0x57d7, 0x5bf9, 0x4ffe, 0x5ad6, 0x0000, 0x0000,
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
    int save_state;         // frame to call Emulator_SaveState at, -1 = never
    int load_state;         // frame to call Emulator_LoadState at, -1 = never
    std::string state_path; // file the two above use
    KeyEvent keys[32];
    int keys_n;
    KeyEvent keys2[32];     // NES_PRESS_KEYS2: controller 2
    int keys2_n;
    KeyEvent fds_swaps[8];  // NES_FDS_SWAP: mask holds the side
    int fds_swaps_n;
    int live;               // NES_LIVE: window, audio and keyboard
} cfg;

static int      g_frame      = 0;
static bool     g_quit       = false;
static uint8_t  g_pad1_mask  = 0;
static uint8_t  g_pad2_mask  = 0;
static FILE    *g_audio_out  = nullptr;   // NES_AUDIO_OUT
static std::vector<int16_t> g_live_audio; // this frame's samples, for the window
#if HOST_SDL
static uint8_t  g_live_pad   = 0;         // keyboard, controller 1
static bool     g_live_quit  = false;
#endif

// ----------------------------------------------------------------------
// Frame pixels — centre 256 cols of the 320-wide framebuffer.
// ----------------------------------------------------------------------
constexpr int FRAME_W = 256, FRAME_H = 240;

// Unpack exactly like the device's CC() macro in main.cpp reads these table
// entries: red bits 11-14, green bits 6-9, blue bits 1-4, i.e. the RGB444
// the picoDVI path actually emits. Bit 15 is the emulator's backdrop marker
// (PalTable | 0x8000) and is ignored here, exactly as the display hardware
// ignores it. Returns 0x00RRGGBB; the PPM dump and the live window both use
// it, so they always show the same pixels.
static inline uint32_t frame_pixel(int x, int y)
{
    const uint16_t p = Frens::framebuffer[y * SCREENWIDTH + 32 + x];
    return (uint32_t)(((p >> 11) & 0xF) * 17) << 16 |
           (uint32_t)(((p >>  6) & 0xF) * 17) <<  8 |
           (uint32_t)(((p >>  1) & 0xF) * 17);
}

static void dump_ppm(int frame)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/frame_%05d.ppm",
             cfg.outdir.c_str(), frame);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }

    fprintf(f, "P6\n%d %d\n255\n", FRAME_W, FRAME_H);
    for (int y = 0; y < FRAME_H; y++) {
        for (int x = 0; x < FRAME_W; x++) {
            const uint32_t c = frame_pixel(x, y);
            uint8_t rgb[3] = { (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

// ----------------------------------------------------------------------
// Input record / replay (NES_PAD_REC / NES_PAD_PLAY).
// A recording is everything the machine was fed from outside: both pads per
// frame, as text, one line per change:
//     <frame> <pad1> [<pad2>]   hex masks from this frame on (pad2 default 00)
//     <frame> end               the recording stopped before this frame
// '#' starts a comment. Frames never decrease. Written by NES_PAD_REC from
// the effective pads (keyboard | scripted input); read by NES_PAD_PLAY,
// which replaces keyboard and scripted input until "end". A file without
// "end" (written by hand) never ends: its last masks persist.
// ----------------------------------------------------------------------
struct PadEvent { int frame; bool end; uint8_t pad1, pad2; };

static std::vector<PadEvent> g_play;
static size_t   g_play_i     = 0;
static bool     g_playing    = false;
static uint8_t  g_play_pad1  = 0;
static uint8_t  g_play_pad2  = 0;
static FILE    *g_rec        = nullptr;
static int      g_rec_last   = 0;       // last recorded pad1 | pad2 << 8

static bool pad_play_load(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) { perror(path); return false; }
    char line[512];
    unsigned ln = 0;
    int last = 0;
    while (fgets(line, sizeof line, fp)) {
        char *p = line, *e;
        ln++;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || !*p) continue;
        PadEvent ev = { (int)strtol(p, &e, 10), false, 0, 0 };
        if (e == p) {
            fprintf(stderr, "%s:%u: expected a frame number\n", path, ln);
            fclose(fp);
            return false;
        }
        while (*e == ' ' || *e == '\t') e++;
        if (!strncmp(e, "end", 3)) {
            ev.end = true;
        } else {
            char *e2;
            ev.pad1 = (uint8_t)strtoul(e, &e2, 16);
            if (e2 == e) {
                fprintf(stderr, "%s:%u: expected a hex pad mask or \"end\"\n", path, ln);
                fclose(fp);
                return false;
            }
            ev.pad2 = (uint8_t)strtoul(e2, nullptr, 16);
        }
        if (ev.frame < last) {
            fprintf(stderr, "%s:%u: frame %d comes after frame %d\n", path, ln,
                    ev.frame, last);
            fclose(fp);
            return false;
        }
        last = ev.frame;
        g_play.push_back(ev);
    }
    fclose(fp);
    printf("PAD_PLAY: %zu events from %s, last at frame %d\n", g_play.size(), path, last);
    return true;
}

static bool pad_rec_open(const char *path, const char *rom_path)
{
    // Settings that change the machine must match on replay; list the ones
    // that are set so the replay command can be rebuilt from the file.
    static const char *const machine_env[] = {
        "NES_REGION", "NES_NO_PSRAM", "NES_NO_SPRITE_LIMIT", "NES_FDS_DISK_SIDE",
        "NES_FDS_SWAP", "NES_FDS_SAVE", "NES_SAVE_STATE", "NES_LOAD_STATE",
        "NES_STATE_PATH", "NES_FAT_ROOT", "ASAN_OPTIONS",
    };
    g_rec = fopen(path, "w");
    if (!g_rec) { perror(path); return false; }
    fprintf(g_rec, "# pico-infonesPlus host-harness input recording\n");
    fprintf(g_rec, "# rom: %s\n", rom_path);
    for (const char *name : machine_env)
        if (getenv(name))
            fprintf(g_rec, "# env: %s=%s\n", name, getenv(name));
    fprintf(g_rec, "# <frame> <pad1> <pad2> | <frame> end\n");
    return true;
}

static void pad_rec_close(int frames_run)
{
    if (!g_rec) return;
    fprintf(g_rec, "%d end\n", frames_run);
    fclose(g_rec);
    g_rec = nullptr;
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

static void parse_frame_list(const char *env, KeyEvent *out, int max, int *n)
{
    const char *e = getenv(env);
    if (!e) return;
    char buf[1024];
    strncpy(buf, e, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    char *tok = strtok(buf, ",");
    while (tok && *n < max) {
        char *colon = strchr(tok, ':');
        if (colon) {
            *colon = 0;
            out[*n].frame = atoi(tok);
            out[*n].mask  = (uint8_t)strtoul(colon + 1, nullptr, 16);
            (*n)++;
        }
        tok = strtok(nullptr, ",");
    }
}

static void parse_keys_env()
{
    parse_frame_list("NES_PRESS_KEYS", cfg.keys, 32, &cfg.keys_n);
    parse_frame_list("NES_PRESS_KEYS2", cfg.keys2, 32, &cfg.keys2_n);
    parse_frame_list("NES_FDS_SWAP", cfg.fds_swaps, 8, &cfg.fds_swaps_n);
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

    // InfoNES_Reset() recomputes all of this; it is here only so the pre-reset
    // banner prints the right number. Keep the NES 2.0 extension in sync with
    // InfoNES.cpp.
    MapperNo      = (NesHeader.byInfo1 >> 4) | (NesHeader.byInfo2 & 0xF0);
    if ((NesHeader.byInfo2 & 0x0C) == 0x08)
        MapperNo |= (WORD)(NesHeader.byReserve[0] & 0x0F) << 8;
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

#if HOST_SDL
// Live mode: show the frame just rendered, read the keyboard for the next
// one and wait until it is due. While paused this keeps polling, so F12
// still dumps the frame on screen and N runs exactly one more.
static void live_frame()
{
    static uint32_t px[FRAME_W * FRAME_H];
    for (int y = 0; y < FRAME_H; y++)
        for (int x = 0; x < FRAME_W; x++)
            px[y * FRAME_W + x] = frame_pixel(x, y);
    hsdl_present(px);
    for (;;) {
        int ev = 0;
        g_live_pad = hsdl_poll(&ev);
        if (ev & HSDL_EV_DUMP) {
            dump_ppm(g_frame);
            printf("LIVE: frame %d dumped to %s/frame_%05d.ppm\n",
                   g_frame, cfg.outdir.c_str(), g_frame);
            fflush(stdout);
        }
        if (ev & HSDL_EV_QUIT) { g_live_quit = true; return; }
        hsdl_status(g_frame);
        if (!hsdl_paused() || (ev & HSDL_EV_STEP)) break;
        hsdl_idle();
    }
    hsdl_audio(g_live_audio.data(), (int)g_live_audio.size() / 2);
    g_live_audio.clear();
    hsdl_pace();
}
#endif

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

    // Save / load state exercise. Saving at frame A and loading at frame B
    // should make every frame from B onward match a run that never diverged.
    if (cfg.save_state == g_frame) {
        int rc = Emulator_SaveState(cfg.state_path.c_str());
        printf("SAVESTATE frame=%d rc=%d\n", g_frame, rc);
    }
    if (cfg.load_state == g_frame) {
        int rc = Emulator_LoadState(cfg.state_path.c_str());
        printf("LOADSTATE frame=%d rc=%d\n", g_frame, rc);
    }
    for (int i = 0; i < cfg.fds_swaps_n; ++i) {
        if (cfg.fds_swaps[i].frame == g_frame && IsFDS) {
            fdsRequestSwap(cfg.fds_swaps[i].mask);
            printf("FDSSWAP frame=%d side=%d\n", g_frame, cfg.fds_swaps[i].mask);
        }
    }

#if HOST_SDL
    if (cfg.live) live_frame();
#endif

    // Decide input for this frame. A replay supplies it until its "end"
    // line; after that the keyboard and the scripted input take over.
    while (g_playing && g_play_i < g_play.size() && g_play[g_play_i].frame <= g_frame) {
        const PadEvent &ev = g_play[g_play_i++];
        if (ev.end) {
            g_playing = false;
            printf("PAD_PLAY: recording ended at frame %d%s\n", g_frame,
                   cfg.live ? ", the keyboard has control" : "");
            fflush(stdout);
        } else {
            g_play_pad1 = ev.pad1;
            g_play_pad2 = ev.pad2;
        }
    }
    uint8_t mask = 0;
    uint8_t mask2 = 0;
    if (g_playing) {
        mask  = g_play_pad1;
        mask2 = g_play_pad2;
    } else {
        if (cfg.press_start >= 0 &&
            g_frame >= cfg.press_start && g_frame < cfg.press_start + 10)
            mask |= 0x08;                          // START
        if (cfg.hold_a >= 0 && g_frame >= cfg.hold_a && (g_frame & 4))
            mask |= 0x01;                          // A
        for (int i = 0; i < cfg.keys_n; i++) {
            if (g_frame >= cfg.keys[i].frame &&
                g_frame <  cfg.keys[i].frame + 10)
                mask |= cfg.keys[i].mask;
        }
        for (int i = 0; i < cfg.keys2_n; i++) {
            if (g_frame >= cfg.keys2[i].frame &&
                g_frame <  cfg.keys2[i].frame + 10)
                mask2 |= cfg.keys2[i].mask;
        }
#if HOST_SDL
        mask |= g_live_pad;
#endif
    }
    g_pad1_mask = mask;
    g_pad2_mask = mask2;
    if (g_rec && (mask | mask2 << 8) != g_rec_last) {
        fprintf(g_rec, "%d %02x %02x\n", g_frame, mask, mask2);
        g_rec_last = mask | mask2 << 8;
    }

    if (g_frame >= cfg.total_frames) g_quit = true;
#if HOST_SDL
    if (g_live_quit) g_quit = true;
#endif
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

// The APU renders its samples per scanline, as many as this returns room
// for. Rendering is part of the emulated machine: the DMC fetches its sample
// bytes and runs out while rendering, and the APU register writes of the
// scanline are applied then. So it is on only when something consumes the
// sound (live window, NES_AUDIO_OUT) and for input record and replay, which
// must run the machine the same way. A plain headless run does not render,
// as before.
static bool g_sound_on = false;

int InfoNES_GetSoundBufferSize() { return g_sound_on ? 4096 : 0; }

// Mixed like the device's InfoNES_SoundOutput in main.cpp: the same channel
// weights, DC blocker and default DVI gain (DVI_AUDIO_GAIN_Q8, 4x), as
// 44.1 kHz s16 stereo. The live window gets one video frame of it at a time
// (live_frame), so its pacing works on whole frames.
void InfoNES_SoundOutput(int samples, BYTE *w1, BYTE *w2, BYTE *w3,
                         BYTE *w4, BYTE *w5, BYTE *w6)
{
    if (!cfg.live && !g_audio_out) return;
    static int32_t dc;                      // L and R are the same mix
    static std::vector<int16_t> buf;        // a scanline has about three
    buf.resize((size_t)samples * 2);
    for (int i = 0; i < samples; i++) {
        const int raw = w1[i] * 6 + w2[i] * 3 + w3[i] * 5 + w4[i] * 51 + w5[i] * 80 +
                        (w6 ? w6[i] : 0) * 18;
        dc += (raw - dc) >> 10;
        int v = (raw - dc) * 2 * 1024 >> 8;
        v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
        buf[2 * i] = buf[2 * i + 1] = (int16_t)v;
    }
    if (g_audio_out)
        fwrite(buf.data(), sizeof(int16_t), buf.size(), g_audio_out);
    if (cfg.live)
        g_live_audio.insert(g_live_audio.end(), buf.begin(), buf.end());
}

int InfoNES_Menu() { return 0; }   // skip menu, start emulation

int InfoNES_ReadRom(const char * /*name*/) { return -1; }
void InfoNES_ReleaseRom() {}

// =====================================================================
// main
// =====================================================================
int main(int argc, char **argv)
{
    cfg.live = get_env_int("NES_LIVE", 0);
#if !HOST_SDL
    if (cfg.live) {
        fprintf(stderr, "%s: built without SDL2, so NES_LIVE=1 is not available.\n"
                "Install libsdl2-dev and rerun hosttest/build.sh.\n", argv[0]);
        return 2;
    }
#endif
    if (argc < (cfg.live ? 2 : 4)) {
        fprintf(stderr,
                "usage: %s <rom.nes|rom.fds> <frames> <dump-every> [outdir]\n"
                "       NES_LIVE=1 %s <rom.nes|rom.fds> [frames] [dump-every] [outdir]\n",
                argv[0], argv[0]);
        return 1;
    }
    // Live mode defaults: the run lasts until the window is closed, and only
    // F12 dumps a frame unless a dump-every is given.
    const char *rom_path = argv[1];
    cfg.total_frames = argc > 2 ? atoi(argv[2]) : INT_MAX;
    cfg.dump_every   = argc > 3 ? atoi(argv[3]) : 0;
    cfg.outdir       = argc > 4 ? argv[4] : "hosttest/out";
    mkdir(cfg.outdir.c_str(), 0755);

    cfg.press_start    = get_env_int("NES_PRESS_START", -1);
    cfg.hold_a         = get_env_int("NES_HOLD_A",      -1);
    cfg.dump_regs      = get_env_int("NES_DUMP_REGS",    0);
    cfg.frame_crc      = get_env_int("NES_FRAME_CRC",    0);
    cfg.dump_vram      = get_env_int("NES_DUMP_VRAM",    0);
    cfg.fds_disk_side  = get_env_int("NES_FDS_DISK_SIDE", -1);
    cfg.save_state     = get_env_int("NES_SAVE_STATE",   -1);
    cfg.load_state     = get_env_int("NES_LOAD_STATE",   -1);
    settings.flags.removeSpriteLimit = get_env_int("NES_NO_SPRITE_LIMIT", 0);
    {
        const char *p = getenv("NES_STATE_PATH");
        cfg.state_path = p ? p : (cfg.outdir + "/host.state");
    }
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

    // FDS save data, same order as the device: sidecar loaded after the
    // parse and before the reset, written back when the game exits.
    const char *fds_save = IsFDS ? getenv("NES_FDS_SAVE") : nullptr;
    if (fds_save) {
        printf("FDSSAVE load %s rc=%d\n", fds_save, (int)fdsLoadSidecar(fds_save));
    }

    if (InfoNES_Reset() < 0) {
        fprintf(stderr, "InfoNES_Reset failed\n"); return 1;
    }

    if (cfg.fds_disk_side >= 0 && IsFDS) {
        fdsRequestSwap(cfg.fds_disk_side);
    }

    if (cfg.total_frames == INT_MAX)
        printf("ROM %s loaded (%ld bytes, fds=%d, mapper=%u, region=%d, crc=%08X). Running until quit.\n",
               rom_path, size, (int)IsFDS, MapperNo, region, rom_crc);
    else
        printf("ROM %s loaded (%ld bytes, fds=%d, mapper=%u, region=%d, crc=%08X). Running %d frames.\n",
               rom_path, size, (int)IsFDS, MapperNo, region, rom_crc, cfg.total_frames);

    // NES_PAD_PLAY replays a NES_PAD_REC recording: until the recording's
    // "end" line it supplies all input, and the keyboard and scripted input
    // are ignored; after it, they take over (so a live replay hands control
    // back to you).
    if (const char *p = getenv("NES_PAD_PLAY")) {
        if (!pad_play_load(p)) return 1;
        g_playing = true;
    }
    if (const char *p = getenv("NES_PAD_REC")) {
        if (!pad_rec_open(p, rom_path)) return 1;
    }
    if (const char *p = getenv("NES_AUDIO_OUT")) {
        g_audio_out = fopen(p, "wb");
        if (!g_audio_out) perror(p);
    }
    g_sound_on = cfg.live || g_audio_out || g_playing || g_rec;

#if HOST_SDL
    if (cfg.live) {
        // Frame period for timer pacing (no audio device). With audio the
        // samples the APU produces per frame set the pace, which comes to
        // the same.
        const char *base = strrchr(rom_path, '/');
        char title[160];
        snprintf(title, sizeof title, "%s", base ? base + 1 : rom_path);
        if (!hsdl_init(title, get_env_int("NES_SCALE", 3), !get_env_int("NES_MUTE", 0),
                       InfoNES_IsPal() ? 19997 : 16639))
            return 1;
    }
#endif

    InfoNES_Cycle();           // returns when InfoNES_PadState raises PAD_SYS_QUIT

#if HOST_SDL
    if (cfg.live) hsdl_quit();
#endif
    pad_rec_close(g_frame);
    if (g_audio_out) fclose(g_audio_out);

    if (cfg.dump_vram) dump_vram_files();
    if (!cfg.live)
        dump_ppm(g_frame);     // final frame snapshot

    if (fds_save)
        printf("FDSSAVE save %s rc=%d\n", fds_save, (int)fdsSaveSidecar(fds_save));

    InfoNES_Fin();
    free(rom);
    printf("done. %d frames rendered.\n", g_frame);
    return 0;
}
