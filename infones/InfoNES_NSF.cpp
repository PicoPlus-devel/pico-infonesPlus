/*===================================================================*/
/*                                                                   */
/*  InfoNES_NSF.cpp : Nintendo Sound Format (.nsf) playback          */
/*                                                                   */
/*  Reference: Mesen2 NsfMapper.cpp (SourMesen/Mesen2)              */
/*                                                                   */
/*===================================================================*/

#include "InfoNES_NSF.h"
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_Mapper.h"
#include "InfoNES_pAPU.h"
#include "K6502.h"
#include "FrensHelpers.h"
#include <cstring>
#include <cstdio>
#include <pico.h>   /* PICO_RP2350 - the RP2040 bank window below depends on it */

#ifndef __not_in_flash_func
#define __not_in_flash_func(name) name
#endif

/*-------------------------------------------------------------------*/
/*  NSF global state                                                 */
/*-------------------------------------------------------------------*/

bool IsNSF = false;

struct NsfHeader_tag NsfHeader;

BYTE NsfCurrentTrack = 0;

/* VU meter peak levels per channel */
BYTE NsfVuLevels[5] = {};

/* Playback state */
bool NsfIsPlaying = false;
int NsfFrameCounter = 0;
static int NsfSilenceCounter = 0;

/* Pointer to the raw NSF ROM data (after 128-byte header) */
static const BYTE *NsfRomData = nullptr;
static size_t NsfRomSize = 0;

/* Current bank register values ($5FF8-$5FFF) */
static BYTE NsfBankRegs[8];

/* Whether this NSF uses bank switching */
static bool NsfHasBanking = false;

/* Number of 4KB pages in the NSF ROM data */
static int NsfPageCount = 0;

/* Load-address offset within the first 4KB page. NSF data is conceptually
   prefixed with this many zero bytes so that page 0 lands at LoadAddress
   rounded down to a 4KB boundary, with the actual data starting at
   (LoadAddress & 0xFFF). Required for NSFs whose LoadAddress isn't 4KB
   aligned (e.g. 1942jp.nsf with LoadAddress=$9F80). */
static int NsfLoadOffset = 0;

/* IRQ counter for PLAY routine timing (in CPU cycles) */
static int NsfIrqCounter = 0;
static int NsfIrqReloadValue = 0;

/* Sunsoft 5B latched register index ($C000 selects, $E000 writes) */
static BYTE NsfS5bReg = 0;

/* 4KB-granular bank pointers used by K6502_Read in NSF mode. Switching
   a bank is just a pointer write — for "clean" pages (fully covered by
   NsfRomData with no zero padding) the pointer goes directly into the
   flash-resident NSF data, eliminating the per-bank-write 4KB memcpy
   that the original ChrBuf-shadow scheme paid (flash → PSRAM is slow,
   ~10 MB/s, which dominated the per-frame budget on heavy bankswitchers
   like Castlevania III). */
BYTE *NsfBank4K[8] = { nullptr, nullptr, nullptr, nullptr,
                       nullptr, nullptr, nullptr, nullptr };

/* Shadow buffers for partial pages — built once per NSF load.
     - NsfShadowZero: page index >= NsfPageCount (out of range).
     - NsfShadowLead: physical page 0 when NsfLoadOffset > 0
       (zero-padded prefix, then ROM bytes).
     - NsfShadowTail: last physical page when NsfRomSize doesn't
       end on a 4KB boundary (ROM bytes, then zero-padded suffix).
   These are allocated in regular SRAM (plain malloc) rather than
   PSRAM since they're hit by CPU-emulation reads. */
static BYTE *NsfShadowZero = nullptr;
static BYTE *NsfShadowLead = nullptr;
static BYTE *NsfShadowTail = nullptr;
static int NsfShadowLeadPage = -1;
static int NsfShadowTailPage = -1;

#if !PICO_RP2350
/* RP2040 only: a contiguous 32KB SRAM image of $8000-$FFFF that
   ROMBANK0..3 point at, so K6502_Read needs no `if (IsNSF)` test on its
   hot ROM path (see the long comment in K6502_rw.h - that test was the
   v0.41 RP2040 frame-rate regression). Bank writes copy the 4KB page in
   rather than just repointing; NSF mode skips the whole PPU pixel
   pipeline, so it has the budget the NES path does not. NsfWindowPage
   suppresses the copy when a register is rewritten with the value it
   already holds, which is the common case. */
static BYTE *NsfWindow = nullptr;
static int NsfWindowPage[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };

/* $FFFC-$FFFF live in bank register 7; rewrite them after every copy
   that lands there. On RP2350 K6502_Read serves these directly. */
static void NsfPatchVectors()
{
    if (!NsfWindow) return;
    NsfWindow[0x7FFC] = 0x00;
    NsfWindow[0x7FFD] = 0x41;  /* reset -> $4100 */
    NsfWindow[0x7FFE] = 0x10;
    NsfWindow[0x7FFF] = 0x41;  /* IRQ   -> $4110 */
}
#endif

/* Build a 4KB shadow page that includes any required zero padding. */
static void NsfBuildShadowPage(BYTE *dst, int page)
{
    int src = page * 0x1000 - NsfLoadOffset;
    int dstOff = 0;
    int len = 0x1000;
    if (src < 0)
    {
        int pad = -src;
        if (pad > len) pad = len;
        memset(dst, 0, pad);
        dstOff += pad;
        len -= pad;
        src = 0;
    }
    int avail = (int)NsfRomSize - src;
    if (avail < 0) avail = 0;
    int copyLen = (len < avail) ? len : avail;
    if (copyLen > 0) memcpy(dst + dstOff, NsfRomData + src, copyLen);
    if (copyLen < len) memset(dst + dstOff + copyLen, 0, len - copyLen);
}

/*-------------------------------------------------------------------*/
/*  NSF BIOS (32 bytes, mapped at $4100-$411F)                       */
/*                                                                   */
/*  From Mesen2 NsfMapper.h:                                         */
/*                                                                   */
/*  .org $4100                                                       */
/*  reset:                                                           */
/*    CLI                    ; $4100: 58                              */
/*    JSR [INIT]             ; $4101: 20 xx xx (patched)             */
/*    STA $4100              ; $4104: 8D 00 41 (clear IRQ)           */
/*    CLI                    ; $4107: 58                              */
/*  loop:                                                            */
/*    JMP loop               ; $4108: 4C 08 41                       */
/*    (padding)              ; $410B-$410F: 00                       */
/*                                                                   */
/*  .org $4110                                                       */
/*  irq:                                                             */
/*    STA $4100              ; $4110: 8D 00 41 (clear IRQ)           */
/*    JSR [PLAY]             ; $4113: 20 xx xx (patched)             */
/*    CLI                    ; $4116: 58                              */
/*    RTI                    ; $4117: 40                              */
/*-------------------------------------------------------------------*/

static BYTE NsfBios[0x20] = {
    0x58, 0x20, 0x00, 0x00, 0x8D, 0x00, 0x41, 0x58,
    0x4C, 0x08, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x8D, 0x00, 0x41, 0x20, 0x00, 0x00, 0x58, 0x40,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*-------------------------------------------------------------------*/
/*  Forward declarations for mapper callbacks                        */
/*-------------------------------------------------------------------*/

static void MapNsf_Write(WORD wAddr, BYTE byData);
static void MapNsf_Sram(WORD wAddr, BYTE byData);
static void MapNsf_Apu(WORD wAddr, BYTE byData);
static BYTE MapNsf_ReadApu(WORD wAddr);
static void MapNsf_VSync();
static void MapNsf_HSync();
static void MapNsf_PPU(WORD wAddr);
static void MapNsf_RenderScreen(BYTE byMode);

/*-------------------------------------------------------------------*/
/*  Helper: apply a single 4KB bank register                         */
/*                                                                   */
/*  K6502_Read goes through NsfBank4K[] (4KB-granular) when IsNSF    */
/*  is true. Switching a bank is just a pointer assignment — direct  */
/*  into the flash-resident NSF data for clean pages, or into one of */
/*  the prebuilt SRAM shadow pages for partial/out-of-range pages.   */
/*-------------------------------------------------------------------*/

static void __not_in_flash_func(NsfApplyBank)(int reg)
{
    int page = NsfBankRegs[reg];
#if !PICO_RP2350
    if (!NsfWindow) return;
    if (NsfWindowPage[reg] == page) return;
    NsfWindowPage[reg] = page;
    BYTE *dst = NsfWindow + reg * 0x1000;
    if (page >= NsfPageCount)
        memset(dst, 0, 0x1000);
    else
        NsfBuildShadowPage(dst, page);
    if (reg == 7)
        NsfPatchVectors();
    return;
#else
    if (page >= NsfPageCount)
    {
        NsfBank4K[reg] = NsfShadowZero;
    }
    else if (page == NsfShadowLeadPage)
    {
        NsfBank4K[reg] = NsfShadowLead;
    }
    else if (page == NsfShadowTailPage)
    {
        NsfBank4K[reg] = NsfShadowTail;
    }
    else
    {
        /* Clean page: fully covered by NsfRomData with no zero padding.
           Read directly from flash, no copy. */
        NsfBank4K[reg] = (BYTE *)(NsfRomData + page * 0x1000 - NsfLoadOffset);
    }
#endif
}

/* Allocate (once) and rebuild the partial-page shadow buffers. Called
   each time an NSF is loaded so a different LoadAddress / file size
   from a previous NSF in the same boot doesn't carry over. */
static void NsfBuildShadows()
{
#if !PICO_RP2350
    /* RP2040 uses the contiguous 32KB window instead of per-page shadow
       buffers; force every register to re-copy for the new NSF. */
    if (!NsfWindow) NsfWindow = (BYTE *)Frens::f_malloc(0x8000);
    for (int i = 0; i < 8; i++)
        NsfWindowPage[i] = -1;
    return;
#else
    if (!NsfShadowZero) NsfShadowZero = (BYTE *)Frens::f_malloc(0x1000);
    if (!NsfShadowLead) NsfShadowLead = (BYTE *)Frens::f_malloc(0x1000);
    if (!NsfShadowTail) NsfShadowTail = (BYTE *)Frens::f_malloc(0x1000);
    if (!NsfShadowZero || !NsfShadowLead || !NsfShadowTail)
        return; /* malloc failure — out-of-range reads will crash, but
                   we have no clean fallback so let the caller fail noisily. */

    memset(NsfShadowZero, 0, 0x1000);

    NsfShadowLeadPage = -1;
    NsfShadowTailPage = -1;

    /* Leading partial page: only when LoadAddress isn't 4KB-aligned. */
    if (NsfLoadOffset > 0)
    {
        NsfShadowLeadPage = 0;
        NsfBuildShadowPage(NsfShadowLead, 0);
    }

    /* Trailing partial page: when (LoadOffset + NsfRomSize) isn't a
       multiple of 4KB, the last physical page has zero padding at the
       end. Skip if it coincides with the leading page (tiny NSFs). */
    int lastPage = NsfPageCount - 1;
    if (lastPage > NsfShadowLeadPage)
    {
        int lastPageEnd = lastPage * 0x1000 - NsfLoadOffset + 0x1000;
        if (lastPageEnd > (int)NsfRomSize)
        {
            NsfShadowTailPage = lastPage;
            NsfBuildShadowPage(NsfShadowTail, lastPage);
        }
    }
#endif
}

/* Initial bank apply for all 8 registers. K6502 vectors at $FFFC-$FFFF
   are no longer patched into a shadow buffer — they're served directly
   by K6502_Read's NSF special case. */
static void NsfApplyAllBanks()
{
    NsfBuildShadows();
    for (int i = 0; i < 8; i++)
        NsfApplyBank(i);

#if !PICO_RP2350
    /* RP2040: ROMBANK[] *is* the NSF read path - K6502_Read has no NSF
       special case there. Fall back to ChrBuf if the 32KB window could
       not be allocated, so stray reads still land in mapped memory. */
    BYTE *base = NsfWindow ? NsfWindow : ChrBuf;
    ROMBANK0 = base + 0x0000;
    ROMBANK1 = base + 0x2000;
    ROMBANK2 = base + 0x4000;
    ROMBANK3 = base + 0x6000;
    NsfPatchVectors();
#else
    /* ROMBANK[] is unused in NSF mode (K6502_Read goes via NsfBank4K),
       but point it at ChrBuf anyway so any stray access from sprite-DMA
       paths or leftover mapper code lands in addressable memory rather
       than nullptr. */
    ROMBANK0 = ChrBuf + 0x0000;
    ROMBANK1 = ChrBuf + 0x2000;
    ROMBANK2 = ChrBuf + 0x4000;
    ROMBANK3 = ChrBuf + 0x6000;
#endif
}

/*===================================================================*/
/*                                                                   */
/*  nsfParse() : Parse an NSF file                                   */
/*                                                                   */
/*===================================================================*/

bool nsfParse(const BYTE *data, size_t size)
{
    if (size < 128)
    {
        printf("[NSF] File too small (%zu bytes)\n", size);
        return false;
    }

    /* Validate magic */
    if (memcmp(data, "NESM\x1a", 5) != 0)
    {
        printf("[NSF] Invalid magic\n");
        return false;
    }

    /* Parse header (128 bytes, little-endian) */
    memcpy(&NsfHeader, data, sizeof(NsfHeader_tag));

    /* Ensure strings are null-terminated */
    NsfHeader.szSongName[31] = '\0';
    NsfHeader.szArtistName[31] = '\0';
    NsfHeader.szCopyright[31] = '\0';

    printf("[NSF] Song: %s\n", NsfHeader.szSongName);
    printf("[NSF] Artist: %s\n", NsfHeader.szArtistName);
    printf("[NSF] Copyright: %s\n", NsfHeader.szCopyright);
    printf("[NSF] Total songs: %d, starting: %d\n", NsfHeader.byTotalSongs, NsfHeader.byStartingSong);
    printf("[NSF] Load: $%04X, Init: $%04X, Play: $%04X\n",
           NsfHeader.wLoadAddress, NsfHeader.wInitAddress, NsfHeader.wPlayAddress);
    printf("[NSF] Play speed NTSC: %d us, PAL: %d us\n",
           NsfHeader.wPlaySpeedNtsc, NsfHeader.wPlaySpeedPal);
    printf("[NSF] Sound chips: $%02X\n", NsfHeader.bySoundChips);

    /* Adjust common timing values per Mesen2 */
    if (NsfHeader.wPlaySpeedNtsc == 16666 || NsfHeader.wPlaySpeedNtsc == 16667)
        NsfHeader.wPlaySpeedNtsc = 16639;
    if (NsfHeader.wPlaySpeedPal == 20000)
        NsfHeader.wPlaySpeedPal = 19997;

    /* Store ROM data pointer (past 128-byte header) */
    NsfRomData = data + 128;
    NsfRomSize = size - 128;

    /* Compute padding for load address alignment to 4KB boundary */
    int loadOffset = NsfHeader.wLoadAddress % 0x1000;
    NsfLoadOffset = loadOffset;

    /* Total ROM size with padding, rounded up to 4KB */
    size_t paddedSize = loadOffset + NsfRomSize;
    if (paddedSize % 0x1000 != 0)
        paddedSize += 0x1000 - (paddedSize % 0x1000);

    NsfPageCount = paddedSize / 0x1000;
    printf("[NSF] Page count: %d (%zuKB padded)\n", NsfPageCount, paddedSize / 1024);

    /* Check bank switching */
    NsfHasBanking = false;
    for (int i = 0; i < 8; i++)
    {
        if (NsfHeader.byBankSwitch[i] != 0)
        {
            NsfHasBanking = true;
            break;
        }
    }

    /* Set initial track */
    NsfCurrentTrack = (NsfHeader.byStartingSong > 0) ? NsfHeader.byStartingSong - 1 : 0;

    /* Patch INIT and PLAY addresses into the BIOS */
    NsfBios[0x02] = NsfHeader.wInitAddress & 0xFF;
    NsfBios[0x03] = (NsfHeader.wInitAddress >> 8) & 0xFF;
    NsfBios[0x14] = NsfHeader.wPlayAddress & 0xFF;
    NsfBios[0x15] = (NsfHeader.wPlayAddress >> 8) & 0xFF;

    IsNSF = true;
    return true;
}

/*===================================================================*/
/*                                                                   */
/*  nsfRelease() : Release NSF resources                             */
/*                                                                   */
/*===================================================================*/

void nsfRelease()
{
    IsNSF = false;
    NsfRomData = nullptr;
    NsfRomSize = 0;
    NsfPageCount = 0;
    NsfLoadOffset = 0;
    NsfHasBanking = false;

    /* Free the partial-page shadow buffers allocated by NsfBuildShadows
       so we don't leak ~12KB of SRAM when the user exits NSF playback. */
    if (NsfShadowZero) { Frens::f_free(NsfShadowZero); NsfShadowZero = nullptr; }
    if (NsfShadowLead) { Frens::f_free(NsfShadowLead); NsfShadowLead = nullptr; }
    if (NsfShadowTail) { Frens::f_free(NsfShadowTail); NsfShadowTail = nullptr; }
    NsfShadowLeadPage = -1;
    NsfShadowTailPage = -1;

#if !PICO_RP2350
    if (NsfWindow) { Frens::f_free(NsfWindow); NsfWindow = nullptr; }
    for (int i = 0; i < 8; i++)
        NsfWindowPage[i] = -1;
#endif

    /* Drop the bank pointers — NsfBank4K entries point into freed
       buffers or into NsfRomData (which is becoming invalid). */
    for (int i = 0; i < 8; i++)
        NsfBank4K[i] = nullptr;
}

/*===================================================================*/
/*                                                                   */
/*  nsfSelectTrack() / nsfNextTrack() / nsfPrevTrack()               */
/*                                                                   */
/*===================================================================*/

void nsfSelectTrack(BYTE track)
{
    if (track < NsfHeader.byTotalSongs)
        NsfCurrentTrack = track;
}

void nsfNextTrack()
{
    if (NsfCurrentTrack + 1 < NsfHeader.byTotalSongs)
        NsfCurrentTrack++;
    else
        NsfCurrentTrack = 0;
}

void nsfPrevTrack()
{
    if (NsfCurrentTrack > 0)
        NsfCurrentTrack--;
    else
        NsfCurrentTrack = NsfHeader.byTotalSongs - 1;
}

/*===================================================================*/
/*                                                                   */
/*  nsfSetupCpuState() : Set CPU state for NSF playback              */
/*                       Called after K6502_Reset()                   */
/*                                                                   */
/*===================================================================*/

extern BYTE SP, A, X, Y, F;
extern WORD PC;
extern BYTE ApuCtrl;
extern BYTE ApuCtrlNew;

void nsfSetupCpuState()
{
    /* Set up bank switching */
    if (NsfHasBanking)
    {
        memcpy(NsfBankRegs, NsfHeader.byBankSwitch, 8);
    }
    else
    {
        /* Auto-configure banks based on load address (per Mesen2) */
        memset(NsfBankRegs, 0, 8);
        int startBank = NsfHeader.wLoadAddress / 0x1000;
        for (int i = 0; i < NsfPageCount; i++)
        {
            if (startBank + i - 8 >= 0 && startBank + i - 8 < 8)
                NsfBankRegs[startBank + i - 8] = i;
        }
    }

    NsfApplyAllBanks();

    /* SRAMBANK points to SRAM (zero-filled by InfoNES_Reset) */
    SRAMBANK = SRAM;

    /* Set CPU registers per NSF spec */
    A = NsfCurrentTrack;
    X = (NsfHeader.byFlags & 0x01) ? 1 : 0;  /* 1=PAL, 0=NTSC */
    Y = 0;
    SP = 0xFD;
    /* K6502_Reset() read PC from the reset vector before banks were
       applied, so it has a stale value. Set PC explicitly to the
       NSF BIOS entry point. */
    PC = 0x4100;

    /* Compute IRQ reload value: PlaySpeed_us × CPUClock / 1000000
       NTSC CPU clock = 1789773 Hz */
    WORD playSpeed = NsfHeader.wPlaySpeedNtsc;
    if (playSpeed == 0)
        playSpeed = 16639; /* Default ~60 Hz */
    NsfIrqReloadValue = (int)(playSpeed * (1789773.0 / 1000000.0));
    NsfIrqCounter = NsfIrqReloadValue;

    /* NSF spec hardware-init sequence before INIT runs:
         $4015 = $0F  enable pulse/triangle/noise (DMC stays off)
         $4017 = $40  disable frame IRQ (4-step, IRQ inhibit)
       Some NSFs (e.g. 1942Us) have INIT routines that don't touch $4015
       themselves and rely on the player having pre-enabled the channels.
       Without this, those tracks play silently after the APU is zeroed by
       InfoNES_pAPUInit on each track-change reset.
       Set ApuCtrl/ApuCtrlNew directly rather than going through the event
       queue: at this point K6502_Reset has just zeroed the clock counter,
       so an event queued here would carry a wrapped (large) timestamp from
       the stale `entertime` left by pAPUInit and be silently dropped by
       the first hsync. */
    ApuCtrl = ApuCtrlNew = 0x0F;
    APU_Reg[0x15] = 0x0F;
    FrameStep = 0;
    FrameIRQ_Enable = 0;
    APU_Reg[0x17] = 0x40;

    /* Enable expansion audio based on sound chip flags, and allocate the
       per-chip wave buffers that the pAPU mixer expects. Mappers 5/24/69
       normally allocate these in their init paths, but those don't run
       in NSF mode (we use synthetic mapper 31), so do it here. */
#if PICO_RP2350
    ApuMmc5Enable = (NsfHeader.bySoundChips & NSF_CHIP_MMC5) ? 1 : 0;
    if (ApuMmc5Enable && !mmc5_wave_buffers)
    {
        mmc5_wave_buffers = (BYTE (*)[APU_MAX_SAMPLES_PER_SYNC])
            Frens::f_malloc(3 * APU_MAX_SAMPLES_PER_SYNC);
        if (mmc5_wave_buffers)
            memset((void *)mmc5_wave_buffers, 0, 3 * APU_MAX_SAMPLES_PER_SYNC);
        else
            ApuMmc5Enable = 0;
    }
#else
    ApuMmc5Enable = 0;
#endif

    ApuVrc6Enable = (NsfHeader.bySoundChips & NSF_CHIP_VRC6) ? 1 : 0;
    if (ApuVrc6Enable && !vrc6_wave_buffers)
    {
        vrc6_wave_buffers = (BYTE (*)[APU_MAX_SAMPLES_PER_SYNC])
            Frens::f_malloc(3 * APU_MAX_SAMPLES_PER_SYNC);
        if (vrc6_wave_buffers)
            memset((void *)vrc6_wave_buffers, 0, 3 * APU_MAX_SAMPLES_PER_SYNC);
        else
            ApuVrc6Enable = 0;
    }

    ApuSunsoft5BEnable = (NsfHeader.bySoundChips & NSF_CHIP_SUNSOFT) ? 1 : 0;
    if (ApuSunsoft5BEnable && !s5b_wave_buffers)
    {
        s5b_wave_buffers = (BYTE (*)[APU_MAX_SAMPLES_PER_SYNC])
            Frens::f_malloc(3 * APU_MAX_SAMPLES_PER_SYNC);
        if (s5b_wave_buffers)
            memset((void *)s5b_wave_buffers, 0, 3 * APU_MAX_SAMPLES_PER_SYNC);
        else
            ApuSunsoft5BEnable = 0;
    }
    NsfS5bReg = 0;

    printf("[NSF] Playing track %d/%d, IRQ reload=%d cycles\n",
           NsfCurrentTrack + 1, NsfHeader.byTotalSongs, NsfIrqReloadValue);

    nsfResetPlayback();
}

/*===================================================================*/
/*                                                                   */
/*  nsfUpdateVuLevels() : Compute VU peak levels from wave_buffers   */
/*                                                                   */
/*===================================================================*/

extern BYTE (*wave_buffers)[APU_MAX_SAMPLES_PER_SYNC];

void nsfUpdateVuLevels()
{
    if (!wave_buffers)
        return;

    /* Compute peak amplitude per channel for this frame.
       wave_buffers values are 0-based (0 = silence):
         ch 0,1 (Pulse):    0-15
         ch 2   (Triangle): 0-255
         ch 3   (Noise):    0-15
         ch 4   (DPCM):     0-127  */

    for (int ch = 0; ch < 5; ch++)
    {
        int peak = 0;
        int mn = 255, mx = 0;
        for (int i = 0; i < APU_MAX_SAMPLES_PER_SYNC; i += 4)
        {
            int val = wave_buffers[ch][i];
            if (val > peak)
                peak = val;
            if (val < mn) mn = val;
            if (val > mx) mx = val;
        }

        /* Scale to 0-255 based on channel max value */
        int scaled;
        switch (ch)
        {
        case 0: /* Pulse 1: max 15 */
        case 1: /* Pulse 2: max 15 */
        case 3: /* Noise:   max 15 */
            scaled = peak * 255 / 15;
            break;
        case 2: /* Triangle: max 255 */
            scaled = peak;
            break;
        case 4:
            /* DPCM DAC holds its last value even when DMA is disabled, so a
               stuck non-zero level would block silence-based auto-advance.
               Measure AC range instead — DC offset reads as silence. */
            scaled = (mx - mn) * 2;
            break;
        default:
            scaled = peak;
            break;
        }
        if (scaled > 255)
            scaled = 255;

        /* Set level directly — no peak-hold, so bars track the music
           in real time. Smooth with the previous level to avoid flicker. */
        NsfVuLevels[ch] = (BYTE)((NsfVuLevels[ch] + scaled) / 2);
    }
}

/*===================================================================*/
/*                                                                   */
/*  Playback control and auto-advance                                */
/*                                                                   */
/*===================================================================*/

void nsfStartPlayback()
{
    if (!NsfIsPlaying)
    {
        NsfIsPlaying = true;
        NsfSilenceCounter = 0;
    }
}

void nsfResetPlayback()
{
    NsfFrameCounter = 0;
    NsfSilenceCounter = 0;
    NsfIsPlaying = true;
}

void nsfStopPlayback()
{
    NsfIsPlaying = false;
    memset(NsfVuLevels, 0, sizeof(NsfVuLevels));
}

bool nsfUpdatePlayback()
{
    if (!NsfIsPlaying)
        return false;

    NsfFrameCounter++;

    /* Silence detection: check the four melodic channels only.
       DPCM is excluded because many NSFs leave a percussion sample
       looping after the melody ends — it would never fall silent and
       would block auto-advance indefinitely. */
    bool silent = true;
    for (int ch = 0; ch < 4; ch++)
    {
        if (NsfVuLevels[ch] > 2)
        {
            silent = false;
            break;
        }
    }

    if (silent)
        NsfSilenceCounter++;
    else
        NsfSilenceCounter = 0;

    /* Auto-advance: silence for too long, or max duration reached */
    if (NsfSilenceCounter >= NSF_SILENCE_FRAMES ||
        NsfFrameCounter >= NSF_MAX_TRACK_FRAMES)
    {
        nsfNextTrack();
        NsfFrameCounter = 0;
        NsfSilenceCounter = 0;
        return true; /* Caller should set resetGame */
    }

    return false;
}

BYTE nsfGetProgress()
{
    if (NsfFrameCounter >= NSF_MAX_TRACK_FRAMES)
        return 255;
    return (BYTE)((NsfFrameCounter * 255) / NSF_MAX_TRACK_FRAMES);
}

/*===================================================================*/
/*                                                                   */
/*  MapNsf_Init() : Mapper 31 (NSF) initialization                  */
/*                                                                   */
/*===================================================================*/

void MapNsf_Init()
{
    MapperInit = MapNsf_Init;
    MapperWrite = MapNsf_Write;
    MapperSram = MapNsf_Sram;
    MapperApu = MapNsf_Apu;
    MapperReadApu = MapNsf_ReadApu;
    MapperVSync = MapNsf_VSync;
    MapperHSync = MapNsf_HSync;
    MapperPPU = MapNsf_PPU;
    MapperRenderScreen = MapNsf_RenderScreen;

    /* Set up initial banks and CPU vectors */
    SRAMBANK = SRAM;
    /* Banks will be fully configured by nsfSetupCpuState after K6502_Reset */
}

/*===================================================================*/
/*                                                                   */
/*  Mapper callback implementations                                  */
/*                                                                   */
/*===================================================================*/

static void __not_in_flash_func(MapNsf_Write)(WORD wAddr, BYTE byData)
{
    /* ROM-area ($8000-$FFFF) writes: in NSF mode used only to drive
       expansion audio chips that decode addresses in this range. */

    /* VRC6 audio: decodes only A0, A1, A12-A15 → use (wAddr & 0xF003). */
    if (NsfHeader.bySoundChips & NSF_CHIP_VRC6)
    {
        switch (wAddr & 0xF003)
        {
            case 0x9000: ApuWriteVrc6P1a(wAddr, byData); return;
            case 0x9001: ApuWriteVrc6P1b(wAddr, byData); return;
            case 0x9002: ApuWriteVrc6P1c(wAddr, byData); return;
            case 0x9003: ApuWriteVrc6Freq(wAddr, byData); return;
            case 0xA000: ApuWriteVrc6P2a(wAddr, byData); return;
            case 0xA001: ApuWriteVrc6P2b(wAddr, byData); return;
            case 0xA002: ApuWriteVrc6P2c(wAddr, byData); return;
            case 0xB000: ApuWriteVrc6SawA(wAddr, byData); return;
            case 0xB001: ApuWriteVrc6SawB(wAddr, byData); return;
            case 0xB002: ApuWriteVrc6SawC(wAddr, byData); return;
            default: break;
        }
    }

    /* Sunsoft 5B: $C000 latches register index, $E000 writes data. */
    if (NsfHeader.bySoundChips & NSF_CHIP_SUNSOFT)
    {
        switch (wAddr & 0xE000)
        {
            case 0xC000:
                NsfS5bReg = byData & 0x0F;
                return;
            case 0xE000:
                ApuWriteSunsoft5B(NsfS5bReg, byData);
                return;
            default: break;
        }
    }
}

static void MapNsf_Sram(WORD wAddr, BYTE byData)
{
    /* SRAM writes handled by default K6502_Write */
    (void)wAddr;
    (void)byData;
}

static void __not_in_flash_func(MapNsf_Apu)(WORD wAddr, BYTE byData)
{
    /* $4100: Clear IRQ (written by BIOS after JSR INIT/PLAY) */
    if (wAddr == 0x4100)
    {
        IRQ_State = IRQ_Wiring; /* Clear pending IRQ */
        return;
    }

    /* $5FF8-$5FFF: Bank switching registers — incremental update only. */
    if (wAddr >= 0x5FF8 && wAddr <= 0x5FFF)
    {
        int reg = wAddr - 0x5FF8;
        if (NsfBankRegs[reg] != byData)
        {
            NsfBankRegs[reg] = byData;
            NsfApplyBank(reg);
        }
        return;
    }

#if PICO_RP2350
    /* MMC5 expansion audio ($5000-$5015) — mirror Map5_Apu dispatch. */
    if ((NsfHeader.bySoundChips & NSF_CHIP_MMC5) &&
        wAddr >= 0x5000 && wAddr <= 0x5015)
    {
        switch (wAddr)
        {
            case 0x5000: ApuWriteMmc5P1a(wAddr, byData); return;
            case 0x5002: ApuWriteMmc5P1c(wAddr, byData); return;
            case 0x5003: ApuWriteMmc5P1d(wAddr, byData); return;
            case 0x5004: ApuWriteMmc5P2a(wAddr, byData); return;
            case 0x5006: ApuWriteMmc5P2c(wAddr, byData); return;
            case 0x5007: ApuWriteMmc5P2d(wAddr, byData); return;
            case 0x5011:
                if (byData != 0)
                    ApuMmc5PcmValue = byData;
                return;
            case 0x5015: ApuWriteMmc5Ctrl(wAddr, byData); return;
            default: return;
        }
    }
#endif
}

static BYTE MapNsf_ReadApu(WORD wAddr)
{
    /* $4100-$411F: NSF BIOS code */
    if (wAddr >= 0x4100 && wAddr <= 0x411F)
    {
        return NsfBios[wAddr - 0x4100];
    }

    /* Default: open bus */
    return (BYTE)(wAddr >> 8);
}

static void MapNsf_VSync()
{
    /* Nothing special — APU vsync handled by InfoNES_pAPUVsync */
}

static void __not_in_flash_func(MapNsf_HSync)()
{
    /* When stopped, don't fire IRQs — PLAY routine won't be called */
    if (!NsfIsPlaying)
        return;

    /* Decrement IRQ counter by CPU cycles per scanline.
       When it reaches zero, fire IRQ and reload. */
    NsfIrqCounter -= STEP_PER_SCANLINE;
    if (NsfIrqCounter <= 0)
    {
        NsfIrqCounter += NsfIrqReloadValue;
        IRQ_REQ;  /* Trigger IRQ → CPU jumps to $4110 → JSR PLAY */
    }
}

static void MapNsf_PPU(WORD wAddr)
{
    (void)wAddr;
}

static void MapNsf_RenderScreen(BYTE byMode)
{
    (void)byMode;
}
