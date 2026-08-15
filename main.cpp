#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/divider.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "util/work_meter.h"
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_pAPU.h"
#include "InfoNES_Region.h"
#include "ff.h"
#include "tusb.h"
#include "gamepad.h"
#include "rom_selector.h"
#include "menu.h"
#include "nespad.h"
#include "wiipad.h"
#include "FrensHelpers.h"
#include "settings.h"
#include "FrensFonts.h"
#include "vumeter.h"
#include "menu_settings.h"
#include "state.h"
#include "soundrecorder.h"
#include "pico/bootrom.h"
#include "InfoNES_FDS.h"
#include "InfoNES_NSF.h"
#include "zapper.h"
#if EMBEDDED_NES_ROM
extern "C" const unsigned char embedded_nes_rom[];
extern "C" const unsigned int embedded_nes_rom_len;
#endif
bool isFatalError = false;
//static bool pendingLoadState = false;
char *romName;
bool showSettings = false;
bool loadSaveStateMenu = false;
SaveStateTypes quickSaveAction = SaveStateTypes::NONE;
static uint32_t start_tick_us = 0;
static uint32_t fps = 0;
static uint8_t framesbeforeAutoStateIsLoaded = 0;
#define EMULATOR_CLOCKFREQ_KHZ 252000 //  Overclock frequency in kHz when using Emulator

// Note: When using framebuffer, AUDIOBUFFERSIZE must be increased to 1024
#if PICO_RP2350
#define AUDIOBUFFERSIZE 1024
#else
#define AUDIOBUFFERSIZE 256
#endif

// DVI Gain (Q8). 256 = 1.0x, 384 = 1.5x, 512 = 2.0x
#ifndef DVI_AUDIO_GAIN_Q8
#define DVI_AUDIO_GAIN_Q8 1024
#endif
// Current gain setting (DVI audio)
static int g_dvi_audio_gain_q8 = DVI_AUDIO_GAIN_Q8;

// Recording gain (Q8). 256 = 1.0x, 512 = 2.0x
#ifndef RECORD_GAIN_Q8
#define RECORD_GAIN_Q8 2048
#endif
static int g_record_gain_q8 = RECORD_GAIN_Q8;

static uint32_t CPUFreqKHz = EMULATOR_CLOCKFREQ_KHZ;
// Visibility configuration for options menu (NES specific)
// 1 = show option line, 0 = hide.
// Order must match enum in menu_options.h
#if PICO_RP2350
static const MenuFdsHooks fdsMenuHooks = {
    fdsCurrentSwapValue,
    fdsGetNumSides,
    fdsRequestSwap,
    fdsRequestEject
};
#endif

// Non-const so the FDS disk-swap entry can be flipped on per-ROM after
// fdsParse() detects we're loading a .fds. The pointer in
// `g_settings_visibility` is `const int8_t *`, but it can point at
// non-const storage just fine.
int8_t g_settings_visibility_nes[MOPT_COUNT] = {
    0,                               // Exit Game, or back to menu. Always visible when in-game.
    0,                               // Reset Game
    BOOTLOADER_BUILD,                // Return to emuLoader picker (only when built for the loader)
    0,                               // Save / Restore State
    1,                               // Screen Mode
    0,                               // Scanlines toggle (superseded by Screen Mode)
    HSTX,                            // Scanline Type (HSTX only)
    1,                               // FPS Overlay
    0,                               // Audio Enable
    0,                               // Frame Skip
    HSTX && ENABLEDVI,                            // Display Mode (HDMI or DVI, only when HSTX is enabled, because non-HSTX builds always use HDMI)
    (EXT_AUDIO_IS_ENABLED ), // External Audio
    1,                               // Font Color
    1,                               // Font Back Color
    ENABLE_VU_METER,                 // VU Meter
    //(HW_CONFIG == 8),                // Fruit Jam Internal Speaker
    (HW_CONFIG == 8),                // Fruit Jam Volume Control
    0,                               // DMG Palette (NES emulator does not use GameBoy palettes)
    0,                               // Border Mode (Super Gameboy style borders not applicable for NES)
    1,                               // Rapid Fire on A
    1,                               // Rapid Fire on B
    0,                               // Auto Insert Disk A, enabled at runtime on RP2350
    0,                               // Auto Swap FDS, enabled at runtime on RP2350
    0,                               // FDS Disk Swap (toggled on after fdsParse succeeds)
    0,                               // Overclock (CPU high clock toggle set at runtime, depends on HSTX and PSRAM available)
    0,                               // YM2413 FM (SMS only, RP2350-only with HSTX)
    1,                               // Enter bootsel mode
    1,                               // Controller test
};
// #if defined(__riscv)
// const uint8_t g_available_screen_modes[] = {
//     0, // SCANLINE_8_7,      
//     0, // NOSCANLINE_8_7,
//     1, // SCANLINE_1_1,
//     1  // NOSCANLINE_1_1
// };
// #else
const uint8_t g_available_screen_modes_nes[] = {
    1, // SCANLINE_8_7,
    1, // NOSCANLINE_8_7,
    1, // SCANLINE_1_1,
    1  // NOSCANLINE_1_1
    };
//#endif

namespace
{
    ROMSelector romSelector_;
}

#if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
// Cached Wii pad state updated once per frame in ProcessAfterFrameIsRendered()
static uint16_t wiipad_raw_cached = 0;
#endif
#if 0
#if !HSTX
// convert RGB565 to RGB444
#define CC(x) (((x >> 1) & 15) | (((x >> 6) & 15) << 4) | (((x >> 11) & 15) << 8))
#else 
// convert RGB565 to RGB555
#define CC(x) ((((x) >> 11) & 0x1F) << 10 | (((x) >> 6) & 0x1F) << 5 | ((x) & 0x1F))
#endif
const WORD __not_in_flash_func(NesPalette)[64] = {
    CC(0x39ce), CC(0x1071), CC(0x0015), CC(0x2013), CC(0x440e), CC(0x5402), CC(0x5000), CC(0x3c20),
    CC(0x20a0), CC(0x0100), CC(0x0140), CC(0x00e2), CC(0x0ceb), CC(0x0000), CC(0x0000), CC(0x0000),
    CC(0x5ef7), CC(0x01dd), CC(0x10fd), CC(0x401e), CC(0x5c17), CC(0x700b), CC(0x6ca0), CC(0x6521),
    CC(0x45c0), CC(0x0240), CC(0x02a0), CC(0x0247), CC(0x0211), CC(0x0000), CC(0x0000), CC(0x0000),
    CC(0x7fff), CC(0x1eff), CC(0x2e5f), CC(0x223f), CC(0x79ff), CC(0x7dd6), CC(0x7dcc), CC(0x7e67),
    CC(0x7ae7), CC(0x4342), CC(0x2769), CC(0x2ff3), CC(0x03bb), CC(0x0000), CC(0x0000), CC(0x0000),
    CC(0x7fff), CC(0x579f), CC(0x635f), CC(0x6b3f), CC(0x7f1f), CC(0x7f1b), CC(0x7ef6), CC(0x7f75),
    CC(0x7f94), CC(0x73f4), CC(0x57d7), CC(0x5bf9), CC(0x4ffe), CC(0x0000), CC(0x0000), CC(0x0000)};
#endif
#if 1
#if !HSTX
// RGB565 to RGB444
#define CC(x) (((x >> 1) & 15) | (((x >> 6) & 15) << 4) | (((x >> 11) & 15) << 8))
const WORD __not_in_flash_func(NesPalette)[64] = {
    CC(0x39ce), CC(0x1071), CC(0x0015), CC(0x2013), CC(0x440e), CC(0x5402), CC(0x5000), CC(0x3c20),
    CC(0x20a0), CC(0x0100), CC(0x0140), CC(0x00e2), CC(0x0ceb), CC(0x0000), CC(0x0000), CC(0x0000),
    CC(0x5ef7), CC(0x01dd), CC(0x10fd), CC(0x401e), CC(0x5c17), CC(0x700b), CC(0x6ca0), CC(0x6521),
    CC(0x45c0), CC(0x0240), CC(0x02a0), CC(0x0247), CC(0x0211), CC(0x0000), CC(0x0000), CC(0x0000),
    CC(0x7fff), CC(0x1eff), CC(0x2e5f), CC(0x223f), CC(0x79ff), CC(0x7dd6), CC(0x7dcc), CC(0x7e67),
    CC(0x7ae7), CC(0x4342), CC(0x2769), CC(0x2ff3), CC(0x03bb), CC(0x0000), CC(0x0000), CC(0x0000),
    CC(0x7fff), CC(0x579f), CC(0x635f), CC(0x6b3f), CC(0x7f1f), CC(0x7f1b), CC(0x7ef6), CC(0x7f75),
    CC(0x7f94), CC(0x73f4), CC(0x57d7), CC(0x5bf9), CC(0x4ffe), CC(0x0000), CC(0x0000), CC(0x0000)};
#else
// RGB888 to RGB555
#define CC(c) (((c & 0xf8) >> 3) | ((c & 0xf800) >> 6) | ((c & 0xf80000) >> 9))
const WORD __not_in_flash_func(NesPalette)[64] = {
    CC(0x626262), CC(0x001C95), CC(0x1904AC), CC(0x42009D),
    CC(0x61006B), CC(0x6E0025), CC(0x650500), CC(0x491E00),
    CC(0x223700), CC(0x004900), CC(0x004F00), CC(0x004816),
    CC(0x00355E), CC(0x000000), CC(0x000000), CC(0x000000),

    CC(0xABABAB), CC(0x0C4EDB), CC(0x3D2EFF), CC(0x7115F3),
    CC(0x9B0BB9), CC(0xB01262), CC(0xA92704), CC(0x894600),
    CC(0x576600), CC(0x237F00), CC(0x008900), CC(0x008332),
    CC(0x006D90), CC(0x000000), CC(0x000000), CC(0x000000),

    CC(0xFFFFFF), CC(0x57A5FF), CC(0x8287FF), CC(0xB46DFF),
    CC(0xDF60FF), CC(0xF863C6), CC(0xF8746D), CC(0xDE9020),
    CC(0xB3AE00), CC(0x81C800), CC(0x56D522), CC(0x3DD36F),
    CC(0x3EC1C8), CC(0x4E4E4E), CC(0x000000), CC(0x000000),

    CC(0xFFFFFF), CC(0xBEE0FF), CC(0xCDD4FF), CC(0xE0CAFF),
    CC(0xF1C4FF), CC(0xFCC4EF), CC(0xFDCACE), CC(0xF5D4AF),
    CC(0xE6DF9C), CC(0xD3E99A), CC(0xC2EFA8), CC(0xB7EFC4),
    CC(0xB6EAE5), CC(0xB8B8B8), CC(0x000000), CC(0x000000)};
#endif
#endif
uint32_t getCurrentNVRAMAddr()
{

    if (!romSelector_.getCurrentROM())
    {
        return {};
    }
    int slot = romSelector_.getCurrentNVRAMSlot();
    if (slot < 0)
    {
        return {};
    }
    printf("SRAM slot %d\n", slot);
    return ROM_FILE_ADDR - SRAM_SIZE * (slot + 1);
}

void saveNVRAM()
{
    char pad[FF_MAX_LFN];
    char fileName[FF_MAX_LFN];
    strcpy(fileName, Frens::GetfileNameFromFullPath(romName));
    Frens::stripextensionfromfilename(fileName);
#if PICO_RP2350
    if (IsFDS)
    {
        snprintf(pad, FF_MAX_LFN, "%s/%s_fds", GAMESAVEDIR, fileName);
        fdsSaveSidecar(pad);
        return;
    }
#endif
    if (!SRAMwritten)
    {
        printf("SRAM not updated.\n");
        return;
    }
    snprintf(pad, FF_MAX_LFN, "%s/%s.SAV", GAMESAVEDIR, fileName);
    printf("Save SRAM to %s\n", pad);
    FIL fil;
    FRESULT fr;
    fr = f_open(&fil, pad, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot open save file: %d", fr);
        printf("%s\n", ErrorMessage);
        return;
    }
    size_t bytesWritten;
    fr = f_write(&fil, SRAM, SRAM_SIZE, &bytesWritten);
    if (bytesWritten < SRAM_SIZE)
    {
        snprintf(ErrorMessage, ERRORMESSAGESIZE, "Error writing save: %d %d/%d written", fr, bytesWritten, SRAM_SIZE);
        printf("%s\n", ErrorMessage);
    }
    f_close(&fil);
    printf("done\n");
    SRAMwritten = false;
}

bool loadNVRAM()
{
    char pad[FF_MAX_LFN];
    FILINFO fno;
    bool ok = false;
    char fileName[FF_MAX_LFN];
    strcpy(fileName, Frens::GetfileNameFromFullPath(romName));
    Frens::stripextensionfromfilename(fileName);

#if PICO_RP2350
    if (IsFDS)
    {
        snprintf(pad, FF_MAX_LFN, "%s/%s_fds", GAMESAVEDIR, fileName);
        fdsSetSaveBasePath(pad);
        return fdsLoadSidecar(pad);
    }
#endif

    snprintf(pad, FF_MAX_LFN, "%s/%s.SAV", GAMESAVEDIR, fileName);

    FIL fil;
    FRESULT fr;

    size_t bytesRead;
    if (auto addr = getCurrentNVRAMAddr())
    {
        printf("Load SRAM from %s\n", pad);
        fr = f_stat(pad, &fno);
        if (fr == FR_NO_FILE)
        {
            printf("Save file not found, load SRAM from flash %x\n", addr);
            memcpy(SRAM, reinterpret_cast<void *>(addr), SRAM_SIZE);
            ok = true;
        }
        else
        {
            if (fr == FR_OK)
            {
                printf("Loading save file %s\n", pad);
                fr = f_open(&fil, pad, FA_READ);
                if (fr == FR_OK)
                {
                    fr = f_read(&fil, SRAM, SRAM_SIZE, &bytesRead);
                    if (fr == FR_OK)
                    {
                        printf("Savefile read from disk\n");
                        ok = true;
                    }
                    else
                    {
                        snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot read save file: %d %d/%d read", fr, bytesRead, SRAM_SIZE);
                        printf("%s\n", ErrorMessage);
                    }
                }
                else
                {
                    snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot open save file: %d", fr);
                    printf("%s\n", ErrorMessage);
                }
                f_close(&fil);
            }
            else
            {
                snprintf(ErrorMessage, ERRORMESSAGESIZE, "f_stat() failed on save file: %d", fr);
                printf("%s\n", ErrorMessage);
            }
        }
    }
    else
    {
        ok = true;
    }
    SRAMwritten = false;
    return ok;
}

static DWORD prevButtons[2]{};
static int rapidFireMask[2]{};
static int rapidFireCounter = 0;
static bool reset = false;
static bool resetGame = false;
void InfoNES_PadState(DWORD *pdwPad1, DWORD *pdwPad2, DWORD *pdwSystem)
{
    static constexpr int LEFT = 1 << 6;
    static constexpr int RIGHT = 1 << 7;
    static constexpr int UP = 1 << 4;
    static constexpr int DOWN = 1 << 5;
    static constexpr int SELECT = 1 << 2;
    static constexpr int START = 1 << 3;
    static constexpr int A = 1 << 0;
    static constexpr int B = 1 << 1;

    // moved variables outside function body because prevButtons gets initialized to 0 everytime the function is called.
    // This is strange because a static variable inside a function is only initialsed once and retains it's value
    // throughout different function calls.
    // Am i missing something?
    // static DWORD prevButtons[2]{};
    // static int rapidFireMask[2]{};
    // static int rapidFireCounter = 0;

    ++rapidFireCounter;

#if NES_PIN_CLK != -1
    // Buttons of a GPIO pad in NES order. A NES pad shifts them out that way
    // already; a SNES pad puts its B and Y in the A and B slots, so its face
    // buttons are named instead of taken positionally: physical A drives NES A
    // and physical B drives NES B, the same as on USB and Wii Classic pads, and
    // the same buttons the menu uses to choose and go back. X, Y, L and R have
    // no NES equivalent and are ignored there too.
    auto nespadGameBits = [](int padnum) -> int
    {
        if (nespad_padtype[padnum] != NESPAD_TYPE_SNES)
        {
            return nespad_states[padnum];
        }
        const uint16_t ext = nespad_states_ext[padnum];
        int v = ext & (SELECT | START | UP | DOWN | LEFT | RIGHT); // same bits on both pads
        if (ext & (1u << 8)) v |= A;
        if (ext & (1u << 0)) v |= B;
        return v;
    };
#endif

    bool usbConnected = false;
    for (int i = 0; i < 2; ++i)
    {
        auto &dst = i == 0 ? *pdwPad1 : *pdwPad2;
        auto &gp = io::getCurrentGamePadState(i);
        if (i == 0)
        {
            usbConnected = gp.isConnected();
        }
        int v = (gp.buttons & io::GamePadState::Button::LEFT ? LEFT : 0) |
                (gp.buttons & io::GamePadState::Button::RIGHT ? RIGHT : 0) |
                (gp.buttons & io::GamePadState::Button::UP ? UP : 0) |
                (gp.buttons & io::GamePadState::Button::DOWN ? DOWN : 0) |
                (gp.buttons & io::GamePadState::Button::A ? A : 0) |
                (gp.buttons & io::GamePadState::Button::B ? B : 0) |
                (gp.buttons & io::GamePadState::Button::SELECT ? SELECT : 0) |
                (gp.buttons & io::GamePadState::Button::START ? START : 0) |
                0;
#if NES_PIN_CLK != -1
        // When USB controller is connected both NES ports act as controller 2
        if (usbConnected)
        {
            if (i == 1)
            {
                v = v | nespadGameBits(1) | nespadGameBits(0);
            }
        }
        else
        {
            v |= nespadGameBits(i);
        }
#endif

// When USB controller is connected  wiipad acts as controller 2
#if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
        if (usbConnected)
        {
            if (i == 1)
            {
                v |= wiipad_raw_cached;
            }
        }
        else // if no USB controller is connected, wiipad acts as controller 1
        {
            if (i == 0)
            {
                v |= wiipad_raw_cached;
            }
        }
#endif

        int rv = v;
        rapidFireMask[i] = (settings.flags.rapidFireOnA ? A : 0) |
                           (settings.flags.rapidFireOnB ? B : 0);
        if (rapidFireCounter & 2)
        {
            // 15 fire/sec
            rv &= ~rapidFireMask[i];
        }

        dst = rv;

        // Reboot to BOOTSEL mode for flashing (player 1 only)
        if (i == 0 && (v & (SELECT | START | UP | A)) == (SELECT | START | UP | A)) {
             reset_usb_boot(0, 0);
        }

        auto p1 = v;

        auto pushed = v & ~prevButtons[i];

        if (p1 & START)
        {
            if (pushed & A) // Toggle frame rate
            {
                settings.flags.displayFrameRate = !settings.flags.displayFrameRate;
                // FrensSettings::savesettings();
            } else if (pushed & B)
            {
#if PICO_RP2350
               if (Frens::isPsramEnabled() && !SoundRecorder::isRecording()) {
                     SoundRecorder::startRecording();
               } 
#endif
            } else if (pushed & UP) {
                loadSaveStateMenu = true;
                quickSaveAction = SaveStateTypes::LOAD;
            } else if (pushed & DOWN) {
                loadSaveStateMenu = true;
                quickSaveAction = SaveStateTypes::SAVE;
            } else if (pushed & LEFT) {
#if HW_CONFIG == 8
               settings.fruitjamVolumeLevel = std::max(-63, settings.fruitjamVolumeLevel - 1);
               EXT_AUDIO_SETVOLUME(settings.fruitjamVolumeLevel);
#endif
            } else if (pushed & RIGHT) {
#if HW_CONFIG == 8
               settings.fruitjamVolumeLevel = std::min(23, settings.fruitjamVolumeLevel + 1);
               EXT_AUDIO_SETVOLUME(settings.fruitjamVolumeLevel);
#endif
            }
        }
        // if (p1 & UP) {
        //     if (pushed & SELECT) {
        //         loadSaveStateMenu = true;
        //         quickSaveAction = SaveStateTypes::LOAD;
        //     }
        //     if (pushed & START) {
        //         loadSaveStateMenu = true;
        //         quickSaveAction = SaveStateTypes::SAVE;
        //     }
        // }
        if (p1 & SELECT && !IsNSF)
        {
            if (pushed & START)
            {
                // saveNVRAM();
                // reset = true;
                FrensSettings::savesettings();
                showSettings = true;
            }
            if (pushed & A)
            {            
               rapidFireMask[i] ^= io::GamePadState::Button::A;
               //g_dvi_audio_gain_q8 = g_dvi_audio_gain_q8 == DVI_AUDIO_GAIN_Q8 ? 256 : DVI_AUDIO_GAIN_Q8;
            }
            if (pushed & B)
            {
                
                rapidFireMask[i] ^= io::GamePadState::Button::B;
               
            }
            if (pushed & UP)
            {
                scaleMode8_7_ = Frens::screenMode(-1);
            } else if (pushed & DOWN)
            {
                scaleMode8_7_ = Frens::screenMode(+1);
            } else if (pushed & LEFT)
            {
                // Toggle audio output, ignore if HSTX is enabled, because HSTX must use external audio
#if EXT_AUDIO_IS_ENABLED && !HSTX
                settings.flags.useExtAudio = !settings.flags.useExtAudio;
                if (settings.flags.useExtAudio)
                {
                    printf("Using I2S Audio\n");
                }
                else
                {
                    printf("Using DVIAudio\n");
                }

#else
                settings.flags.useExtAudio = 0;
#endif
                //FrensSettings::savesettings();
            }
#if ENABLE_VU_METER
            else if (pushed & RIGHT)
            {
                settings.flags.enableVUMeter = !settings.flags.enableVUMeter;
                //FrensSettings::savesettings();
                // printf("VU Meter %s\n", settings.flags.enableVUMeter ? "enabled" : "disabled");
                turnOffAllLeds();
            }
#endif
        }

        prevButtons[i] = v;
    }

    /* NSF track controls (player 1 only, checked after button state update) */
    if (IsNSF)
    {
        static constexpr int LEFT = 1 << 6;
        static constexpr int RIGHT = 1 << 7;
        static constexpr int START = 1 << 3;
        static constexpr int SELECT = 1 << 2;
        static constexpr int A_BTN = 1 << 0;
        static constexpr int B_BTN = 1 << 1;

        /* Recalculate pushed for player 1: edges since last frame. */
        static DWORD nsfPrevPad = 0;
        DWORD nsfPushed = prevButtons[0] & ~nsfPrevPad;
        nsfPrevPad = prevButtons[0];

        if (!(prevButtons[0] & START) && !(prevButtons[0] & SELECT))
        {
            if (nsfPushed & RIGHT)
            {
                nsfNextTrack();
                /* Trigger a re-init by resetting the CPU state */
                resetGame = true;
            }
            else if (nsfPushed & LEFT)
            {
                nsfPrevTrack();
                resetGame = true;
            }
        }

        /* A = play, B = stop */
        if (nsfPushed & A_BTN)
            nsfStartPlayback();
        if (nsfPushed & B_BTN)
            nsfStopPlayback();

        if ((prevButtons[0] & SELECT) && (nsfPushed & START))
        {
            /* Select+Start: exit NSF playback, return to menu */
            reset = true;
        }
    }

    if (reset && !IsNSF)
    {
        saveNVRAM();
    }
    *pdwSystem = (reset || resetGame) ? PAD_SYS_QUIT : 0;
}

void InfoNES_MessageBox(const char *pszMsg, ...)
{
    printf("[MSG]");
    va_list args;
    va_start(args, pszMsg);
    vprintf(pszMsg, args);
    va_end(args);
    printf("\n");
}

void InfoNES_Error(const char *pszMsg, ...)
{
    printf("[Error]");
    va_list args;
    va_start(args, pszMsg);
    vsnprintf(ErrorMessage, ERRORMESSAGESIZE, pszMsg, args);
    printf("%s", ErrorMessage);
    va_end(args);
    printf("\n");
}
bool parseROM(const uint8_t *nesFile)
{
#if PICO_RP2350
    // Famicom Disk System dispatch. The disk image was loaded into memory
    // (PSRAM or flash); look up its size from the file on SD so we can
    // determine side count and strip any fwNES header.
    if (fdsIsFdsFilename(romName))
    {
        FILINFO fno;
        if (f_stat(romName, &fno) != FR_OK)
        {
            snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot stat FDS file %s", romName);
            printf("%s\n", ErrorMessage);
            return false;
        }
        if (!fdsParse((BYTE *)nesFile, (size_t)fno.fsize))
        {
            // fdsParse already populated ErrorMessage via InfoNES_Error.
            return false;
        }
        // Disk image lives in memory at nesFile; PRG/CHR-RAM live in dedicated
        // FDS_* buffers. ROM/VROM are wired up by Mapper 20 init (phase 3).
        ROM = nullptr;
        VROM = nullptr;
        // Phase 5: expose FDS options in the in-game settings menu.
        g_settings_visibility_nes[MOPT_FDS_DISK_SWAP] = 1;
        g_settings_visibility_nes[MOPT_AUTO_SWAP_FDS_DISK] = 1;
        g_settings_visibility_nes[MOPT_AUTO_INSERT_FDS_DISK_A] = 1;
        menuSetFdsHooks(&fdsMenuHooks);
        return true;
    }
#endif

    // NSF (Nintendo Sound Format) detection — check magic or file extension.
    if (checkNSFMagic(nesFile))
    {
        // If already in NSF mode (e.g. track change via resetGame), skip re-parse
        // so that NsfCurrentTrack is preserved.
        if (IsNSF)
            return true;

        // Determine file size via f_stat (works for both SD and flash-based files).
        size_t fileSize = 0;
        FILINFO fno;
        if (f_stat(romName, &fno) == FR_OK)
            fileSize = (size_t)fno.fsize;
        if (fileSize == 0)
        {
            snprintf(ErrorMessage, ERRORMESSAGESIZE, "Cannot determine NSF file size");
            return false;
        }
        if (!nsfParse(nesFile, fileSize))
        {
            snprintf(ErrorMessage, ERRORMESSAGESIZE, "NSF parse error");
            return false;
        }
        ROM = nullptr;
        VROM = nullptr;
        return true;
    }

    memcpy(&NesHeader, nesFile, sizeof(NesHeader));
    if (!checkNESMagic(NesHeader.byID))
    {
        return false;
    }

    nesFile += sizeof(NesHeader);

    memset(SRAM, 0, SRAM_SIZE);

    if (NesHeader.byInfo1 & 4)
    {
        memcpy(&SRAM[0x1000], nesFile, 512);
        nesFile += 512;
    }

    auto romSize = NesHeader.byRomSize * 0x4000;
    auto vromSize = NesHeader.byVRomSize * 0x2000;

    // Detect ROMs where the header overstates PRG size and CHR data is
    // embedded inside the declared PRG area (e.g. Galaxian (J) has 8KB PRG +
    // 8KB CHR packed as 16KB with a header claiming 16KB PRG + 8KB CHR).
    // The reset vector at the end of the declared PRG will be invalid while
    // the vector at (romSize - vromSize) is valid.
    if (romSize > 0 && vromSize > 0 && romSize > vromSize)
    {
        uint16_t resetVec = nesFile[romSize - 4] | ((uint16_t)nesFile[romSize - 3] << 8);
        if (resetVec < 0x8000)
        {
            auto actualPrgSize = romSize - vromSize;
            uint16_t fixedResetVec = nesFile[actualPrgSize - 4] |
                                     ((uint16_t)nesFile[actualPrgSize - 3] << 8);
            if (fixedResetVec >= 0x8000)
            {
                printf("ROM header fix: PRG %dK -> %dK, CHR found at PRG offset %d\n",
                       romSize / 1024, actualPrgSize / 1024, actualPrgSize);
                ROM = (BYTE *)nesFile;
                VROM = (BYTE *)(nesFile + actualPrgSize);
                NesHeader.byRomSize = actualPrgSize / 0x4000;
                return true;
            }
        }
    }

    ROM = (BYTE *)nesFile;
    nesFile += romSize;

    if (NesHeader.byVRomSize > 0)
    {
        VROM = (BYTE *)nesFile;
        nesFile += vromSize;
    }

    return true;
}

void InfoNES_ReleaseRom()
{
    ROM = nullptr;
    VROM = nullptr;
    if (IsNSF)
    {
        /* On a track-change reset (resetGame), keep NSF state alive so
           the current track number is preserved across the re-init. */
        if (!resetGame)
            nsfRelease();
        return;
    }
#if PICO_RP2350
    if (IsFDS)
    {
        fdsRelease();
        IsFDS = false;
        g_settings_visibility_nes[MOPT_FDS_DISK_SWAP] = 0;
        menuSetFdsHooks(nullptr);
    }
#endif
}

void InfoNES_SoundInit()
{
}

int InfoNES_SoundOpen(int samples_per_sync, int sample_rate)
{
    printf("InfoNES_SoundOpen: samples_per_sync=%d, sample_rate=%d\n", samples_per_sync, sample_rate);
    return 0;
}

void InfoNES_SoundClose()
{
}

int __not_in_flash_func(InfoNES_GetSoundBufferSize)()
{
    // Prefer early return to avoid duplicated branches.
#if EXT_AUDIO_IS_ENABLED
    if (settings.flags.useExtAudio)
    {
        return audio_i2s_get_freebuffer_size();
    }
#endif

#if HSTX
    // Compute free HDMI Data Island audio packet capacity and convert to samples.
    int level = hstx_di_queue_get_level();
    int free_packets = HSTX_AUDIO_DI_HIGH_WATERMARK - level;
    if (free_packets <= 0)
        return 0;
    // Each DI packet carries 4 audio samples; use shift for fast multiply.
    return free_packets << 2;
#else
    // Non-HSTX path: return available ring buffer capacity directly.
    return dvi_->getAudioRingBuffer().getFullWritableSize();
#endif
}



static inline int16_t apply_dvi_gain_i32(int x)
{
    int64_t v = (int64_t)x * (int64_t)g_dvi_audio_gain_q8; // Q8 scale
    v >>= 8;
    if (v > 32767) v = 32767;
    else if (v < -32768) v = -32768;
    return (int16_t)v;
}

static inline int16_t apply_record_gain_i32(int x)
{
    int64_t v = (int64_t)x * (int64_t)g_record_gain_q8; // Q8 scale
    v >>= 8;
    if (v > 32767) v = 32767;
    else if (v < -32768) v = -32768;
    return (int16_t)v;
}

static inline void set_dviaudio_gain_q8(int q8)
{
    if (q8 < 0) q8 = 0;
    if (q8 > 1024) q8 = 1024; // up to 4.0x
    g_dvi_audio_gain_q8 = q8;
}
static inline void recordSampleToSoundRecorder(int l, int r)
{
#if PICO_RP2350
    if (SoundRecorder::isRecording())
    {
        // int16_t cl = (l > 32767 ? 32767 : (l < -32768 ? -32768 : l));
        // int16_t cr = (r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
        int16_t cl = apply_record_gain_i32(l);
        int16_t cr = apply_record_gain_i32(r);
        int16_t stereo[2] = {cl, cr};
        SoundRecorder::recordFrame(stereo, 2);
    }
#endif
}

void __not_in_flash_func(InfoNES_SoundOutput)(int samples, BYTE *wave1, BYTE *wave2, BYTE *wave3, BYTE *wave4, BYTE *wave5, BYTE *wave6)
{
    // DC blocker state. Larger shift = slower response / more low-frequency
    // preserved; smaller shift = stronger DC removal. 10 ≈ 20 Hz corner.
    static int32_t dc_l = 0;
    static int32_t dc_r = 0;
    constexpr int DC_FILTER_SHIFT = 10;

#if !HSTX
#if EXT_AUDIO_IS_ENABLED
    if (settings.flags.useExtAudio)
    {
        for (int i = 0; i < samples; ++i)
        {
            int w1 = wave1[i];
            int w2 = wave2[i];
            int w3 = wave3[i];
            int w4 = wave4[i];
            int w5 = wave5[i];
            int w6 = wave6 ? wave6[i] : 0;

            int raw_l = w1 * 6 + w2 * 3 + w3 * 5 + w4 * 51 + w5 * 80 + w6 * 18;
            int raw_r = w1 * 6 + w2 * 3 + w3 * 5 + w4 * 51 + w5 * 80 + w6 * 18;

            dc_l += (raw_l - dc_l) >> DC_FILTER_SHIFT;
            dc_r += (raw_r - dc_r) >> DC_FILTER_SHIFT;

            int out_l = (raw_l - dc_l) * 2;
            int out_r = (raw_r - dc_r) * 2;
#if PICO_RP2350
            recordSampleToSoundRecorder(out_l, out_r);
#endif
            EXT_AUDIO_ENQUEUE_SAMPLE((int16_t)out_l, (int16_t)out_r);
#if ENABLE_VU_METER
            if (settings.flags.enableVUMeter)
            {
                addSampleToVUMeter(out_l);
            }
#endif
        }
        return;
    }
#endif
    while (samples)
    {
        auto &ring = dvi_->getAudioRingBuffer();
        auto n = std::min<int>(samples, ring.getWritableSize());
        if (!n)
        {
            return;
        }

        auto p = ring.getWritePointer();

        int ct = n;
        while (ct--)
        {
            int w1 = *wave1++;
            int w2 = *wave2++;
            int w3 = *wave3++;
            int w4 = *wave4++;
            int w5 = *wave5++;
            int w6 = wave6 ? *wave6++ : 0;

            int raw_l = w1 * 6 + w2 * 3 + w3 * 5 + w4 * 51 + w5 * 80 + w6 * 18;
            int raw_r = w1 * 6 + w2 * 3 + w3 * 5 + w4 * 51 + w5 * 80 + w6 * 18;

            dc_l += (raw_l - dc_l) >> DC_FILTER_SHIFT;
            dc_r += (raw_r - dc_r) >> DC_FILTER_SHIFT;

            int out_l = (raw_l - dc_l) * 2;
            int out_r = (raw_r - dc_r) * 2;
#if PICO_RP2350
            recordSampleToSoundRecorder(out_l, out_r);
#endif
            int l = apply_dvi_gain_i32(out_l);
            int r = apply_dvi_gain_i32(out_r);
            *p++ = {static_cast<short>(l), static_cast<short>(r)};
#if ENABLE_VU_METER
            if (settings.flags.enableVUMeter)
            {
                addSampleToVUMeter(out_l);
            }
#endif
        }

        ring.advanceWritePointer(n);
        samples -= n;
    }
#else
#if EXT_AUDIO_IS_ENABLED
    bool audioJackConnected = Frens::isHeadPhoneJackConnected();
#endif
    for (int i = 0; i < samples; ++i)
    {
        int w1 = wave1[i];
        int w2 = wave2[i];
        int w3 = wave3[i];
        int w4 = wave4[i];
        int w5 = wave5[i];
          /* w6: expansion audio
                - VRC6 (Konami Mapper 24)
                - Famicom Disk System (Mapper 20)
                - Sunsoft 5B (Mapper 69)
                - null when no expansion cart is loaded. */
        int w6 = wave6 ? wave6[i] : 0;

        int raw_l = w1 * 6 + w2 * 3 + w3 * 5 + w4 * 51 + w5 * 80 + w6 * 18;
        int raw_r = w1 * 6 + w2 * 3 + w3 * 5 + w4 * 51 + w5 * 80 + w6 * 18;

        dc_l += (raw_l - dc_l) >> DC_FILTER_SHIFT;
        dc_r += (raw_r - dc_r) >> DC_FILTER_SHIFT;

        int out_l = (raw_l - dc_l) * 2;
        int out_r = (raw_r - dc_r) * 2;

    #if PICO_RP2350
        recordSampleToSoundRecorder(out_l, out_r);
    #endif
    #if ENABLE_VU_METER
        if (settings.flags.enableVUMeter)
        {
            addSampleToVUMeter(out_l);
        }
    #endif

    #if EXT_AUDIO_IS_ENABLED
        if (settings.flags.useExtAudio || audioJackConnected)
        {
            EXT_AUDIO_ENQUEUE_SAMPLE((int16_t)out_l, (int16_t)out_r);
            continue;
        }
    #endif

        int gl = apply_dvi_gain_i32(out_l);
        int gr = apply_dvi_gain_i32(out_r);
        hstx_push_audio_sample(gl, gr);
    }
#endif
}

extern WORD PC;

// Region-aware frame pacing.
//   NTSC: forwards to Frens::PaceFrames60fps (preserves the existing HSTX
//         vsync-wait and non-HSTX vsync/line-buffer behavior verbatim).
//   PAL : 50 Hz pacing via sleep_until. The HDMI/DVI panel still scans at
//         60 Hz, so 1 in every 6 displayed frames will be a duplicate; the
//         emulator itself runs at correct PAL speed. Works on HSTX (replaces
//         hstx_paceFrame's 60 Hz vsync wait) and on non-HSTX framebuffer
//         mode (replaces the vsync busy-wait). PAL is rejected upstream
//         when no framebuffer is available — see fallback at the call site.
static void paceFrame(bool init)
{
    if (!InfoNES_IsPal())
    {
        Frens::PaceFrames60fps(init);
        return;
    }

    static absolute_time_t next_frame_time;
    static bool initialized = false;
    if (init || !initialized)
    {
        next_frame_time = make_timeout_time_us(0);
        initialized = true;
    }

    // Slack-aware: if we overran the target by more than one frame (e.g.
    // returning from the menu after an idle pause), snap forward instead of
    // bursting through the backlog. Mirrors hstx_paceFrame's resync logic.
    absolute_time_t now = get_absolute_time();
    if (absolute_time_diff_us(now, next_frame_time) <= -20000)
    {
        next_frame_time = now;
    }

    sleep_until(next_frame_time);
    next_frame_time = delayed_by_us(next_frame_time, 20000); // 1/50s = 20000us
#if DOUBLEFRAMEBUFFER
    Frens::swapFrameBuffers();
#endif
}

int InfoNES_LoadFrame()
{
//      if (pendingLoadState) {         // perform at frame start
//         pendingLoadState = false;
//         printf("Loading state...\n");
//         if (Emulator_LoadState("/slot0.state") == 0) {
//             printf("State loaded.\n");
// #if FRAMEBUFFERISPOSSIBLE
//             if (Frens::isFrameBufferUsed()) {
//                 memset(Frens::framebuffer, 0, sizeof(Frens::framebuffer));
//             }
// #endif
//         } else {
//             printf("State load failed.\n");
//         }
//     }
    paceFrame(false);
    //Frens::waitForVSync();
    Frens::pollHeadPhoneJack();
    EXT_AUDIO_POLL_HEADPHONE();

    /* NSF: update VU meter levels once per frame */
    if (IsNSF)
    {
        nsfUpdateVuLevels();
        /* Check for auto-advance (silence detection / max duration) */
        if (nsfUpdatePlayback())
            resetGame = true;
    }
#if NES_PIN_CLK != -1
    nespad_read_start();
#endif
    auto count =
#if !HSTX
        dvi_->getFrameCounter();
#else
        hstx_getframecounter();
#endif
    long onOff = hw_divider_s32_quotient_inlined(count, 60) & 1;
    Frens::blinkLed(onOff);
#if NES_PIN_CLK != -1
    nespad_read_finish(); // Sets global nespad_state var
#endif
    zapperPoll();    // Latch "Zapper present" on port 2 (+ optional debug trace)
    zapperMeasureFrame(); // Display-lag measurement, compiled out by default
    tuh_task();
    // Frame rate calculation
    if (settings.flags.displayFrameRate)
    {
        // calculate fps and round to nearest value (instead of truncating/floor)
        uint32_t tick_us = Frens::time_us() - start_tick_us;
        fps = (1000000 - 1) / tick_us + 1;
        start_tick_us = Frens::time_us();
    }

#if !HSTX
#else
    // hstx_waitForVSync();
#endif
#if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
    // Poll Wii pad once per frame (function called once per rendered frame)
    wiipad_raw_cached = wiipad_read();
#endif
#if ENABLE_VU_METER
    if (isVUMeterToggleButtonPressed())
    {
        settings.flags.enableVUMeter = !settings.flags.enableVUMeter;
        FrensSettings::savesettings();
        // printf("VU Meter %s\n", settings.flags.enableVUMeter ? "enabled" : "disabled");
        turnOffAllLeds();
    }
#endif
   
    if (showSettings && !IsNSF)
    {
        showSettings = false;
        int rval = showSettingsMenu(true);
        if (rval == 3)
        {
            reset = true;
            if (isAutoSaveStateConfigured() ){
                loadSaveStateMenu = true;
                quickSaveAction = SaveStateTypes::SAVE_AND_EXIT;
            }
        }
        if ( rval == 4) {
            loadSaveStateMenu = true;
            quickSaveAction = SaveStateTypes::NONE;
           
        }
        if (rval == 5) {
           resetGame = true;
        }
    }
    if (loadSaveStateMenu && !IsNSF) {
        if (quickSaveAction == SaveStateTypes::LOAD_AND_START) {
            if (framesbeforeAutoStateIsLoaded > 0) {
                --framesbeforeAutoStateIsLoaded;  // let the emulator run for a few frames before loading state
            }   
        }  else {
            framesbeforeAutoStateIsLoaded = 0;
        } 
        if (framesbeforeAutoStateIsLoaded == 0) {
           
            char msg[24];
            snprintf(msg, sizeof(msg), "Mapper %03d CRC %08X", MapperNo, Frens::getCrcOfLoadedRom());
            if ( showSaveStateMenu(Emulator_SaveState, Emulator_LoadState, msg, quickSaveAction) == false ) {
                reset = true;
            };
            loadSaveStateMenu = false;
        }
    }

    return count;
}

namespace
{
#if !HSTX
    dvi::DVI::LineBuffer *currentLineBuffer_{};
    WORD *currentLineBuf{nullptr};
#else
    WORD *currentLineBuffer_{nullptr};
#endif
}
#if !HSTX
void __not_in_flash_func(drawWorkMeterUnit)(int timing,
                                            [[maybe_unused]] int span,
                                            uint32_t tag)
{
    if (timing >= 0 && timing < 640)
    {
        auto p = currentLineBuffer_->data();
        p[timing] = tag; // tag = color
    }
}

void __not_in_flash_func(drawWorkMeter)(int line)
{
    if (!currentLineBuffer_)
    {
        return;
    }

    memset(currentLineBuffer_->data(), 0, 64);
    memset(&currentLineBuffer_->data()[320 - 32], 0, 64);
    (*currentLineBuffer_)[160] = 0;
    if (line == 4)
    {
        for (int i = 1; i < 10; ++i)
        {
            (*currentLineBuffer_)[16 * i] = 31;
        }
    }

    constexpr uint32_t clocksPerLine = 800 * 10;
    constexpr uint32_t meterScale = 160 * 65536 / (clocksPerLine * 2);
    util::WorkMeterEnum(meterScale, 1, drawWorkMeterUnit);
    //    util::WorkMeterEnum(160, clocksPerLine * 2, drawWorkMeterUnit);
}
#endif

/*-------------------------------------------------------------------*/
/*  NSF display helper: draw a text string on a scanline             */
/*-------------------------------------------------------------------*/
static void nsfDrawText(WORD *buf, int x, int line, const char *text, int textRow, WORD fgc, WORD bgc)
{
    x+=2; // 2 pixel padding on the left
    for (int i = 0; text[i] != '\0'; i++)
    {
        char fontSlice = getcharslicefrom8x8font(text[i], textRow);
        for (int bit = 0; bit < 8; bit++)
        {
            buf[x + i * 8 + bit] = (fontSlice & 1) ? fgc : bgc;
            fontSlice >>= 1;
        }
    }
}

/*-------------------------------------------------------------------*/
/*  NSF display: render VU meter UI on a scanline                    */
/*                                                                   */
/*  Layout (320 pixels wide, lines 4-235):                           */
/*   Lines  20-27:  Song name                                        */
/*   Lines  36-43:  Artist name                                      */
/*   Lines  52-59:  Copyright                                        */
/*   Lines  72-79:  Track N / Total                                  */
/*   Lines  96-103: "Pulse 1" bar label                              */
/*   Lines 104-111: Pulse 1 VU bar                                   */
/*   Lines 116-123: "Pulse 2" bar label                              */
/*   Lines 124-131: Pulse 2 VU bar                                   */
/*   Lines 136-143: "Triangle" bar label                             */
/*   Lines 144-151: Triangle VU bar                                  */
/*   Lines 156-163: "Noise" bar label                                */
/*   Lines 164-171: Noise VU bar                                     */
/*   Lines 176-183: "DPCM" bar label                                 */
/*   Lines 184-191: DPCM VU bar                                      */
/*-------------------------------------------------------------------*/
static void nsfRenderLine(WORD *buf, int line)
{
    /* NES palette indices for colors */
    WORD bgc = NesPalette[0x0F];   /* Black */
    WORD fgc = NesPalette[0x30];   /* White */

    /* Fill background */
    for (int i = 0; i < 320; i++)
        buf[i] = bgc;

    /* Channel bar colors (NES palette) */
    static const BYTE barColors[5] = { 0x16, 0x12, 0x1A, 0x14, 0x17 };
    static const char *chanNames[5] = { "Pulse 1", "Pulse 2", "Triangle", "Noise", "DPCM" };

    /* Song name (lines 20-27) */
    if (line >= 20 && line < 28)
    {
        int textRow = line - 20;
        nsfDrawText(buf, 32, line, NsfHeader.szSongName, textRow, fgc, bgc);
    }
    /* Artist (lines 36-43) */
    else if (line >= 36 && line < 44)
    {
        int textRow = line - 36;
        nsfDrawText(buf, 32, line, NsfHeader.szArtistName, textRow, NesPalette[0x21], bgc);
    }
    /* Copyright (lines 52-59) */
    else if (line >= 52 && line < 60)
    {
        int textRow = line - 52;
        nsfDrawText(buf, 32, line, NsfHeader.szCopyright, textRow, NesPalette[0x21], bgc);
    }
    /* Track info (lines 72-79) */
    else if (line >= 72 && line < 80)
    {
        char trackStr[40];
        /* Show elapsed time MM:SS and play/stop status */
        int totalSec = NsfFrameCounter / 60;
        int mm = totalSec / 60;
        int ss = totalSec % 60;
        snprintf(trackStr, sizeof(trackStr), "Track %d / %d  %d:%02d %s",
                 NsfCurrentTrack + 1, NsfHeader.byTotalSongs,
                 mm, ss, NsfIsPlaying ? ">" : "||");
        int textRow = line - 72;
        nsfDrawText(buf, 32, line, trackStr, textRow, NesPalette[0x30], bgc);
    }
    /* VU bars: 5 channels, each occupies 20 lines (8 label + 8 bar + 4 gap) */
    else if (line >= 96 && line < 196)
    {
        for (int ch = 0; ch < 5; ch++)
        {
            int labelStart = 96 + ch * 20;
            int barStart = labelStart + 8;

            /* Channel label */
            if (line >= labelStart && line < labelStart + 8)
            {
                int textRow = line - labelStart;
                nsfDrawText(buf, 32, line, chanNames[ch], textRow, NesPalette[barColors[ch]], bgc);
            }
            /* VU bar */
            else if (line >= barStart && line < barStart + 8)
            {
                /* Bar width proportional to VU level (0-255 → 0-240 pixels) */
                int barWidth = (NsfVuLevels[ch] * 240) / 255;
                WORD barColor = NesPalette[barColors[ch]];
                for (int x = 40; x < 40 + barWidth && x < 280; x++)
                    buf[x] = barColor;
            }
        }
    }
    /* Progress bar (lines 210-215) */
    else if (line >= 210 && line < 216)
    {
        BYTE progress = nsfGetProgress();
        int barWidth = (progress * 240) / 255;
        WORD borderColor = NesPalette[0x10]; /* Dark grey */
        WORD fillColor = NesPalette[0x30];   /* White */

        /* Draw border on top/bottom lines, fill on interior */
        if (line == 210 || line == 215)
        {
            for (int x = 39; x <= 280; x++)
                buf[x] = borderColor;
        }
        else
        {
            buf[39] = borderColor;
            buf[280] = borderColor;
            for (int x = 40; x < 40 + barWidth && x < 280; x++)
                buf[x] = fillColor;
        }
    }
}

void __not_in_flash_func(InfoNES_PreDrawLine)(int line)
{
#if !HSTX

    WORD *buff;
// b.size --> 640
// printf("Pre Draw%d\n", b->size());
// WORD = 2 bytes
// b->size = 640
// printf("%d\n", b->size());
#if FRAMEBUFFERISPOSSIBLE
    if (Frens::isFrameBufferUsed())
    {
        currentLineBuf = &Frens::framebuffer[line * 320];
        InfoNES_SetLineBuffer(currentLineBuf + 32, 320);
    }
    else
    {
#endif
        util::WorkMeterMark(0xaaaa);
        auto b = dvi_->getLineBuffer();
        util::WorkMeterMark(0x5555);
        InfoNES_SetLineBuffer(b->data() + 32, b->size());
        currentLineBuffer_ = b;
#if FRAMEBUFFERISPOSSIBLE
    }
#endif
    //    (*b)[319] = line + dvi_->getFrameCounter();

#else
    currentLineBuffer_ = hstx_getlineFromFramebuffer(line + 4); // Top Margin of 4 lines
    InfoNES_SetLineBuffer(currentLineBuffer_ + 32, 640);

#endif
}

void __not_in_flash_func(InfoNES_PostDrawLine)(int line)
{
#if !HSTX
#if !defined(NDEBUG)
    util::WorkMeterMark(0xffff);
    drawWorkMeter(line);
#endif
#endif

    /* NSF mode: overwrite scanline with VU meter UI */
    if (IsNSF)
    {
        WORD *buf =
#if !HSTX
            currentLineBuf == nullptr ? currentLineBuffer_->data() : currentLineBuf;
#else
            currentLineBuffer_;
#endif
        nsfRenderLine(buf, line);
    }

    // Display frame rate
    if (settings.flags.displayFrameRate && line >= 8 && line < 16)
    {
        char fpsString[2];
        WORD *fpsBuffer =
#if !HSTX
            currentLineBuf == nullptr ? currentLineBuffer_->data() + 40 : currentLineBuf + 40;
#else
            currentLineBuffer_ + 40;
#endif
        WORD fgc = NesPalette[48];
        WORD bgc = NesPalette[15];
        fpsString[0] = '0' + (fps / 10);
        fpsString[1] = '0' + (fps % 10);

        int rowInChar = line % 8;
        for (auto i = 0; i < 2; i++)
        {
            char firstFpsDigit = fpsString[i];
            char fontSlice = getcharslicefrom8x8font(firstFpsDigit, rowInChar);
            for (auto bit = 0; bit < 8; bit++)
            {
                if (fontSlice & 1)
                {
                    *fpsBuffer++ = fgc;
                }
                else
                {
                    *fpsBuffer++ = bgc;
                }
                fontSlice >>= 1;
            }
        }
    }

#if ZAPPER_SUPPORTED && ZAPPER_DEBUG_OVERLAY
    // Zapper status readout, drawn on the character row below the frame rate
    // counter. 32 characters at 8 pixels each is exactly the 256-pixel NES
    // picture, which starts at offset 32 in the 320-wide line buffer.
    //
    // Note this puts bright pixels on every frame, which the gun's sensor sees -
    // including the all-black and single-white-box frames the games use to work
    // out where the gun is pointed. Diagnostics only, never while playing.
    if (line >= 16 && line < 24)
    {
        WORD *zapBuffer =
#if !HSTX
            currentLineBuf == nullptr ? currentLineBuffer_->data() + 32 : currentLineBuf + 32;
#else
            currentLineBuffer_ + 32;
#endif
        WORD fgc = NesPalette[48];
        WORD bgc = NesPalette[15];
        int rowInChar = line % 8;
        bool pastEnd = false;
        for (int i = 0; i < ZAPPER_DBG_TEXT_LEN; i++)
        {
            if (zapperDbgText[i] == '\0')
                pastEnd = true; // blank the tail rather than render stale bytes
            char fontSlice = getcharslicefrom8x8font(pastEnd ? ' ' : zapperDbgText[i], rowInChar);
            for (auto bit = 0; bit < 8; bit++)
            {
                *zapBuffer++ = (fontSlice & 1) ? fgc : bgc;
                fontSlice >>= 1;
            }
        }
    }
#endif

#if ZAPPER_SUPPORTED && ZAPPER_MEASURE
    // Display-lag measurement takes over the picture entirely: flat black or a
    // single flat white frame, so the only thing the gun can see is the flash.
    {
        WORD *mbuf =
#if !HSTX
            currentLineBuf == nullptr ? currentLineBuffer_->data() : currentLineBuf;
#else
            currentLineBuffer_;
#endif
        WORD c = zapperMeasureIsWhite() ? NesPalette[48] : NesPalette[15];
        for (int x = 32; x < 32 + 256; x++)
            mbuf[x] = c;
        zapperMeasureLine(line);
    }
#endif

#if !HSTX
#if FRAMEBUFFERISPOSSIBLE
    if (!Frens::isFrameBufferUsed())
    {
#endif
        assert(currentLineBuffer_);
        dvi_->setLineBuffer(line, currentLineBuffer_);
        currentLineBuffer_ = nullptr;
#if FRAMEBUFFERISPOSSIBLE
    }
#endif
#endif
}

bool loadAndReset()
{
    auto rom = romSelector_.getCurrentROM();
    if (!rom)
    {
        printf("ROM does not exists.\n");
        return false;
    }

    if (!parseROM(rom))
    {
        printf("NES file parse error.\n");
        return false;
    }
    if (loadNVRAM() == false)
    {
        return false;
    }

    if (InfoNES_Reset() < 0)
    {
        printf("NES reset error.\n");
        return false;
    }
    return true;
}

int InfoNES_Menu()
{
    // InfoNES_Main() のループで最初に呼ばれる
    return loadAndReset() ? 0 : -1;
    // return 0;
}

int main()
{
    char selectedRom[FF_MAX_LFN];

    romName = selectedRom;
    ErrorMessage[0] = selectedRom[0] = 0;

    vreg_voltage voltage = VREG_VOLTAGE_1_20;
#if HSTX
    Frens::FlashParams *flashParams;
    // assign flashParams to point to flash location
    bool freqOverruled = false;
    flashParams = (Frens::FlashParams *)FLASHPARAM_ADDRESS;
    if ( Frens::validateFlashParams(*flashParams) ) {
        CPUFreqKHz = flashParams->cpuFreqKHz;
        voltage = flashParams->voltage;
        freqOverruled = true;
    }
#endif
    Frens::setClocksAndStartStdio(CPUFreqKHz, voltage);

    printf("==========================================================================================\n");
    printf("Pico-InfoNES+ %s\n", SWVERSION);
    printf("Build date: %s\n", __DATE__);
    printf("Build time: %s\n", __TIME__);
    printf("CPU freq: %d kHz\n", clock_get_hz(clk_sys) / 1000);
#if HSTX
    printf("HSTX freq: %d\n", clock_get_hz(clk_hstx) / 1000);
#endif
    printf("Stack size: %d bytes\n", PICO_STACK_SIZE);
    printf("==========================================================================================\n");
    printf("Starting up...\n");
#if PICO_RP2350
    printf("Mapper 5 and Mapper 85 are enabled\n");
#else
    printf("Mapper 5 and Mapper 85 are disabled\n");
#endif
    FrensSettings::initSettings(FrensSettings::emulators::NES);
    // Note:
    //     - When using framebuffer, AUDIOBUFFERSIZE must be increased to 1024
    //     - Top and bottom margins are reset to zero
    isFatalError = !Frens::initAll(selectedRom, CPUFreqKHz, 4, 4, AUDIOBUFFERSIZE, false, true);

    scaleMode8_7_ = Frens::applyScreenMode(settings.screenMode);
    initzapper();       // Claims GPIO27/28 as pulled-up inputs (custom PCB only)
    zapperMeasureInit(); // Display-lag measurement, compiled out by default
    bool showSplash = true;
#if PICO_RP2350
    g_settings_visibility_nes[MOPT_AUTO_SWAP_FDS_DISK] = 1;
    g_settings_visibility_nes[MOPT_AUTO_INSERT_FDS_DISK_A] = 1;
#else
    g_settings_visibility_nes[MOPT_AUTO_SWAP_FDS_DISK] =   0;
    g_settings_visibility_nes[MOPT_AUTO_INSERT_FDS_DISK_A] = 0;
#endif
#if HSTX
    if ( Frens::isPsramEnabled() ) {
        g_settings_visibility_nes[MOPT_OVERCLOCK] = 1;
    } else {
        g_settings_visibility_nes[MOPT_OVERCLOCK] = 0;
    }
#else
    g_settings_visibility_nes[MOPT_OVERCLOCK] = 0;
#endif
    g_settings_visibility = g_settings_visibility_nes;
    g_available_screen_modes = g_available_screen_modes_nes;
    while (true)
    {
#if EMBEDDED_NES_ROM
        ROM_FILE_ADDR = (uintptr_t)embedded_nes_rom;
        strcpy(selectedRom, "Embedded");
        isFatalError = false; // SD card failure is not fatal when ROM is embedded
        *ErrorMessage = 0;
        Frens::PaceFrames60fps(true);
#else
        if (strlen(selectedRom) == 0)
        {
#if PICO_RP2350
            const char *romExtensions = ".nes .fds .nsf";
#else
            const char *romExtensions = ".nes .nsf";
#endif
            menu("Pico-InfoNES+", ErrorMessage, isFatalError, showSplash, romExtensions, selectedRom); // With no psram this never returns, but reboots upon selecting a game
            printf("Playing selected ROM from menu: %s\n", selectedRom);
          
        }
        // Lagrange Point
        //  0x743387FF = Lagrange Point (Japan) (Rev A)
        //  0x00F49381 = English translation of Lagrange Point (Japan) (Rev A)
        if ( (Frens::getCrcOfLoadedRom() == 0x743387FF || Frens::getCrcOfLoadedRom() == 0x00F49381) ) 
        { 
#if HSTX
            if ( !Frens::isPsramEnabled() ) 
            {
                // Lagrange Point  needs PSRAM for its memory requirements.
                strcpy(ErrorMessage, "Lagrange Point needs PSRAM to run.");
                selectedRom[0] = 0;
                continue;
            }
            if ( !settings.flags.overclock ) 
            {
                // Lagrange Point  needs overclocking to run.
                strcpy(ErrorMessage, "Set Overclock ON in settings menu.");
                selectedRom[0] = 0;
                continue;
            }
#else
            strcpy(ErrorMessage, "Cannot run this game with this config.");
            selectedRom[0] = 0;
            continue;
#endif
           
        }
#endif
        reset = resetGame = loadSaveStateMenu = false;
        //EXT_AUDIO_MUTE_INTERNAL_SPEAKER(settings.flags.fruitJamEnableInternalSpeaker == 0);
        EXT_AUDIO_SETVOLUME(settings.fruitjamVolumeLevel);
        *ErrorMessage = 0;
        if (!Frens::isPsramEnabled())
        {
            printf("Now playing: %s\n", selectedRom);
        }
       
        if (isAutoSaveStateConfigured() && !IsNSF)
        {
            char tmpPath[40];
            getAutoSaveStatePath(tmpPath, sizeof(tmpPath));
            printf("Auto-save is configured found for this ROM (%s)\n", tmpPath);
            if (Frens::fileExists(tmpPath) ) {
                printf("Auto-save state found for this ROM (%s)\n", tmpPath);
                printf("Loading auto-save state...\n");
                loadSaveStateMenu = true;
                quickSaveAction = SaveStateTypes::LOAD_AND_START;
                framesbeforeAutoStateIsLoaded = 120; // wait 2 seconds before loading auto state
            } else {
                printf("No auto-save state found for this ROM.\n");
            }
        } else {
            printf("No auto-save configured for this ROM.\n");
        }
        do {
            resetGame = false;
#if PICO_RP2350
            if (fdsIsFdsFilename(romName))
            {
                romSelector_.initRaw(ROM_FILE_ADDR);
            }
            else
#endif
            if (checkNSFMagic(reinterpret_cast<const uint8_t *>(ROM_FILE_ADDR)))
            {
                romSelector_.initRaw(ROM_FILE_ADDR);
            }
            else if (!romSelector_.init(ROM_FILE_ADDR) ) {
                strcpy(ErrorMessage, "Not a NES ROM file.");
                break;
            }

            // isRomPal: 0 = NTSC, 1 = PAL, 2 = Dendy.
            int region = InfoNES_DetectRegion(ROM_FILE_ADDR, Frens::getCrcOfLoadedRom(), selectedRom);
            static const char *regionNames[] = { "NTSC", "PAL", "Dendy" };
            const char *regionName = regionNames[region & 3];

            // PAL/Dendy require a framebuffer-based video pipeline. In non-HSTX
            // line-streaming mode (RP2040, no framebuffer) the line-buffer
            // queue is hardware-locked to the DVI 60 Hz scanline rate, so
            // pacing the emulator at 50 Hz starves the queue (flicker) and
            // the audio ring buffer (silence). Force NTSC there.
#if !HSTX
            if (region != INFONES_REGION_NTSC && !Frens::isFrameBufferUsed())
            {
                printf("%s not supported in line-streaming mode (no framebuffer); running as NTSC.\n", regionName);
                region = INFONES_REGION_NTSC;
                regionName = regionNames[0];
            }
#endif
            printf("Region: %s\n", regionName);
            // After a non-PSRAM reboot the monitor needs time to sync with the
            // fresh HDMI signal.  Without a delay the FDS BIOS intro animation
            // plays while the display is still dark.  Only needed on the very
            // first launch (showSplash is true); resets keep the link up.
            // This also benefits RP2040/RP2350: .nsf files don't clip sound at the start, roms that 
            // start with sound also don't clip sound.
            if (showSplash && !Frens::isPsramEnabled())
            {
                showSplash = false;
                printf("Feeding blank frames for display sync...\n");
                menuPumpBlankFrames(180);
            }
            paceFrame(true); // reset pacing to avoid burst of frames if resetGame is true
            InfoNES_Main(region);

        } while (resetGame);
#if !EMBEDDED_NES_ROM
        selectedRom[0] = 0;
#endif
        showSplash = false;
    }

    return 0;
}
