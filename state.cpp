/*
 * Save/Load state implementation for InfoNES on Pico.
 *
 * Design goals:
 *  - Deterministic restoration of CPU, PPU, APU, controller, timing and mapper state.
 *  - Correct handling of both CHR ROM and CHR RAM cartridges.
 *  - Minimal coupling: only serializes globals actually required to resume execution.
 *  - FatFs (ff.h) used instead of stdio.
 *
 * Important points:
 *  - PPURAM is always saved, even for CHR ROM (only name tables / palette are meaningful then).
 *  - Pattern table selection (BG / Sprite) is recomputed from PPU_R0 after load.
 *  - Bank indices are stored instead of raw pointers (portable across address relocation).
 *  - Mapper and APU offer optional variable‑size blobs via callback hooks.
 *  - Flags: bit0 in header = CHR RAM present (NesHeader.byVRomSize == 0).
 */

#include "state.h"
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_Mapper.h"
#include "InfoNES_pAPU.h"
#include "K6502.h"
#include "ff.h"
#include <cstring>
#include <cstdio>
#include <memory>
#include "FrensHelpers.h"

/* -------- Extern declarations (core emulator globals) -------- */

// 6502 registers and timing
extern WORD PC;
extern BYTE SP;
extern BYTE F; // Processor status flags
extern BYTE A;
extern BYTE X;
extern BYTE Y;
extern BYTE IRQ_State;  // Current asserted level (internal logic)
extern BYTE IRQ_Wiring; // Config / enable mask
extern BYTE NMI_State;
extern BYTE NMI_Wiring;
extern int g_wPassedClocks;  // Total cycles elapsed
extern int g_wCurrentClocks; // Cycles consumed in current step/frame

// Memory regions
extern BYTE *RAM;         // 2KB internal RAM
extern BYTE *SRAM;        // Battery-backed (mapper dependent)
extern BYTE *PPURAM;      // PPU address space for CHR RAM + name tables + palette
extern BYTE *SPRRAM;      // Sprite (OAM) memory
extern BYTE *ChrBuf;      // Decoded pattern cache (implementation‑specific)
extern BYTE *PPUBANK[16]; // 16 x 1KB PPU bank pointers
extern BYTE *ROM;         // PRG ROM base
extern BYTE *ROMBANK[4];  // 4 x 8KB PRG bank pointers (typically 16KB/32KB mapped)
extern BYTE *VROM;        // CHR ROM base (NULL if CHR RAM)
extern struct NesHeader_tag NesHeader;
extern BYTE MapperNo;
extern BYTE ROM_Mirroring; // Current mirroring type

// PPU register/state
extern BYTE PPU_R0, PPU_R1, PPU_R2, PPU_R3, PPU_R7;
extern BYTE PPU_Scr_H_Byte;    // Horizontal coarse scroll byte
extern BYTE PPU_Scr_H_Bit;     // Horizontal fine scroll bits
extern WORD PPU_Addr;          // VRAM address latch
extern WORD PPU_Temp;          // Temp VRAM address (loopy register scratch)
extern WORD PPU_Increment;     // VRAM increment (1 or 32)
extern WORD PPU_Scanline;      // Current scanline
extern BYTE PPU_NameTableBank; // Base nametable bank index
extern BYTE *PPU_BG_Base;      // Pointer to background pattern table (decoded)
extern BYTE *PPU_SP_Base;      // Pointer to sprite pattern table (decoded)
extern WORD PPU_SP_Height;     // Sprite height (8 or 16)
extern int SpriteJustHit;      // Sprite zero hit latch
extern BYTE byVramWriteEnable; // 1 if CHR RAM writable
extern BYTE PPU_Latch_Flag;    // Write toggle for $2005/$2006
extern BYTE PPU_UpDown_Clip;   // Vertical clipping mode
extern BYTE FrameIRQ_Enable;   // Mapper frame IRQ enable
extern WORD FrameStep;         // Frame step counter

// Frame / rendering stats
extern WORD FrameSkip;
extern WORD FrameCnt;
extern BYTE ChrBufUpdate; // Pattern cache update flag
extern WORD PalTable[32]; // Current palette (decoded to internal format)

// APU registers
extern BYTE APU_Reg[0x18];
//extern struct ApuEvent_t ApuEventQueue[APU_EVENT_MAX]; // not needed here
extern int cur_event;
extern WORD entertime;
// extern BYTE wave_buffers[5][735]; // Not needed here
extern BYTE ApuCtrl;
extern BYTE ApuCtrlNew;

extern int ApuQuality;

extern DWORD ApuPulseMagic;
extern DWORD ApuTriangleMagic;
extern DWORD ApuNoiseMagic;
extern unsigned int ApuSamplesPerSync16;
extern unsigned int ApuCyclesPerSample;
extern unsigned int ApuSampleRate;
extern DWORD ApuCycleRate;
extern BYTE ApuC1a,
    ApuC1b, ApuC1c, ApuC1d;

extern BYTE *ApuC1Wave;
extern DWORD ApuC1Skip;
extern DWORD ApuC1Index;
extern int32_t ApuC1EnvPhase;
extern BYTE ApuC1EnvVol;
extern BYTE ApuC1Atl;
extern int32_t ApuC1SweepPhase;
extern DWORD ApuC1Freq;

extern BYTE ApuC2a, ApuC2b, ApuC2c, ApuC2d;

extern BYTE *ApuC2Wave;
extern DWORD ApuC2Skip;
extern DWORD ApuC2Index;
extern int32_t ApuC2EnvPhase;
extern BYTE ApuC2EnvVol;
extern BYTE ApuC2Atl;
extern int32_t ApuC2SweepPhase;
extern DWORD ApuC2Freq;

extern BYTE ApuC3a, ApuC3b, ApuC3c, ApuC3d;

extern DWORD ApuC3Skip;
extern DWORD ApuC3Index;
extern BYTE ApuC3Atl;
extern DWORD ApuC3Llc; /* Linear Length Counter */
extern bool ApuC3ReloadFlag;

extern BYTE ApuC4a, ApuC4b, ApuC4c, ApuC4d;

extern DWORD ApuC4Sr;  /* Shift register */
extern DWORD ApuC4Fdc; /* Frequency divide counter */
extern DWORD ApuC4Skip;
extern DWORD ApuC4Index;
extern BYTE ApuC4Atl;
extern BYTE ApuC4EnvVol;
extern int32_t ApuC4EnvPhase;

extern BYTE ApuC5Reg[4];
extern BYTE ApuC5Enable;
extern BYTE ApuC5Looping;
extern BYTE ApuC5CurByte;
extern BYTE ApuC5DpcmValue;
extern int ApuC5Freq;
extern int ApuC5Phaseacc;

extern WORD ApuC5Address, ApuC5CacheAddr;
extern int ApuC5DmaLength, ApuC5CacheDmaLength;
// End of APU externs

// Controller latch/state
extern DWORD PAD1_Latch;
extern DWORD PAD2_Latch;
extern DWORD PAD_System;
extern DWORD PAD1_Bit;
extern DWORD PAD2_Bit;

// Mirroring function
extern void InfoNES_Mirroring(int nType);

/* -------- Optional mapper and APU blob hooks -------- */
extern "C"
{
  // void Mapper_Save(void *&blob, size_t &size);
  // int Mapper_Load(const void *blob, size_t size);
  void pAPU_Save(void *&blob, size_t &size);
  int pAPU_Load(const void *blob, size_t size);
}

// // Weak defaults (if mapper/APU do not implement custom serialization)
// __attribute__((weak)) void Mapper_Save(void *&blob, size_t &size)
// {
//   blob = nullptr;
//   size = 0;
// }
// __attribute__((weak)) int Mapper_Load(const void *blob, size_t size)
// {
//   (void)blob;
//   (void)size;
//   return 0;
// }
__attribute__((weak)) void pAPU_Save(void *&blob, size_t &size)
{
  blob = nullptr;
  size = 0;
}
__attribute__((weak)) int pAPU_Load(const void *blob, size_t size)
{
  (void)blob;
  (void)size;
  return 0;
}

/* -------- File format structures -------- */
#define SAVESTATEFILE_VERSION 1
#define SAVESTATE_FLAG_CHR_RAM      0x01
#define SAVESTATE_FLAG_REGION_SHIFT 1
#define SAVESTATE_FLAG_REGION_MASK  (0x3u << SAVESTATE_FLAG_REGION_SHIFT)
// Backwards compatibility: the old "PAL flag" was just bit 1, which now reads
// as INFONES_REGION_PAL=1 in the 2-bit region field. NTSC=0 (no flag) and
// Dendy=2 fit in bits 1..2 without colliding with old saves.
struct SaveHeader
{
  char magic[8];     // "INFOST\1" magic identifier
  uint32_t version;  // Increment if layout changes
  uint32_t mapperNo; // For compatibility validation
  uint32_t flags;    // bit0: CHR RAM present, bits1..2: region (0/1/2)
};

struct SaveCore
{
  // CPU
  WORD PC;
  BYTE SP;
  BYTE F;
  BYTE A;
  BYTE X;
  BYTE Y;
  BYTE IRQ_State;
  BYTE IRQ_Wiring;
  BYTE NMI_State;
  BYTE NMI_Wiring;
  int g_wPassedClocks;
  int g_wCurrentClocks;

  // PPU primary registers and dynamic state
  BYTE PPU_R0, PPU_R1, PPU_R2, PPU_R3, PPU_R7;
  WORD PPU_Addr, PPU_Temp;
  WORD PPU_Increment;
  WORD PPU_Scanline;
  BYTE PPU_NameTableBank;
  WORD PPU_SP_Height;
  BYTE PPU_Scr_H_Byte;
  BYTE PPU_Scr_H_Bit;
  BYTE PPU_Latch_Flag;
  BYTE PPU_UpDown_Clip;
  BYTE FrameIRQ_Enable;
  WORD FrameStep;
  int SpriteJustHit;

  // Timing / misc
  WORD FrameSkip;
  WORD FrameCnt;
  BYTE ChrBufUpdate;
  BYTE byVramWriteEnable;
  BYTE ROM_Mirroring;
  BYTE reserved0; // Padding/alignment

  // Palette snapshot
  WORD PalTable[32];

  // PPU bank indices (1KB units) and PRG bank indices (8KB)
  BYTE ppuBankIndex[16];
  uint16_t prgBankIndex[4];

  // APU register block
  BYTE APU_Reg[0x18];
  // APU externals snapshot
  int cur_event;
  WORD entertime;
  BYTE ApuCtrl;
  BYTE ApuCtrlNew;
  int ApuQuality;
  DWORD ApuPulseMagic;
  DWORD ApuTriangleMagic;
  DWORD ApuNoiseMagic;
  unsigned int ApuSamplesPerSync16;
  unsigned int ApuCyclesPerSample;
  unsigned int ApuSampleRate;
  DWORD ApuCycleRate;
  BYTE ApuC1a, ApuC1b, ApuC1c, ApuC1d;
  DWORD ApuC1Skip;
  DWORD ApuC1Index;
  int32_t ApuC1EnvPhase;
  BYTE ApuC1EnvVol;
  BYTE ApuC1Atl;
  int32_t ApuC1SweepPhase;
  DWORD ApuC1Freq;
  BYTE ApuC2a, ApuC2b, ApuC2c, ApuC2d;
  DWORD ApuC2Skip;
  DWORD ApuC2Index;
  int32_t ApuC2EnvPhase;
  BYTE ApuC2EnvVol;
  BYTE ApuC2Atl;
  int32_t ApuC2SweepPhase;
  DWORD ApuC2Freq;
  BYTE ApuC3a, ApuC3b, ApuC3c, ApuC3d;
  DWORD ApuC3Skip;
  DWORD ApuC3Index;
  BYTE ApuC3Atl;
  DWORD ApuC3Llc;
  bool ApuC3ReloadFlag;
  BYTE ApuC4a, ApuC4b, ApuC4c, ApuC4d;
  DWORD ApuC4Sr;
  DWORD ApuC4Fdc;
  DWORD ApuC4Skip;
  DWORD ApuC4Index;
  BYTE ApuC4Atl;
  BYTE ApuC4EnvVol;
  int32_t ApuC4EnvPhase;
  BYTE ApuC5Reg[4];
  BYTE ApuC5Enable;
  BYTE ApuC5Looping;
  BYTE ApuC5CurByte;
  BYTE ApuC5DpcmValue;
  int ApuC5Freq;
  int ApuC5Phaseacc;
  WORD ApuC5Address, ApuC5CacheAddr;
  int ApuC5DmaLength, ApuC5CacheDmaLength;
  
  // Controller latch + shift positions
  DWORD PAD1_Latch;
  DWORD PAD2_Latch;
  DWORD PAD_System;
  DWORD PAD1_Bit;
  DWORD PAD2_Bit;
};

/* -------- Helpers -------- */

// Return the base pointer a PPUBANK slot is addressed from.
// Slots 0-7 are the pattern tables: CHR ROM, a mapper-owned CHR RAM buffer, or
// the CHR RAM the core keeps at the bottom of PPURAM.
// Slots 8-15 are always PPURAM (nametables plus the $3000 mirror / palette area),
// so they must not be measured against the CHR base.
static inline BYTE *ppuBankBase(int slot)
{
  if (slot >= 8)
    return PPURAM;
  if (NesHeader.byVRomSize > 0)
    return VROM;
  if (MapperChrRam)
    return MapperChrRam;
  return PPURAM;
}

// Convert current PPUBANK / ROMBANK pointers to linear indices for serialization
static void fillBankIndices(SaveCore &c)
{
  for (int i = 0; i < 16; i++)
    c.ppuBankIndex[i] = (BYTE)((PPUBANK[i] - ppuBankBase(i)) / 0x400);
  for (int i = 0; i < 4; i++)
    c.prgBankIndex[i] = (uint16_t)((ROMBANK[i] - ROM) / 0x2000);
}

// Rebuild PPUBANK pointers from stored indices
static void restorePPUBanks(const SaveCore &c)
{
  for (int i = 0; i < 16; i++)
    PPUBANK[i] = ppuBankBase(i) + c.ppuBankIndex[i] * 0x400;
}

// Rebuild PRG banks
static void restorePRGBanks(const SaveCore &c)
{
  for (int i = 0; i < 4; i++)
    ROMBANK[i] = ROM + c.prgBankIndex[i] * 0x2000;
}

// Recompute pattern table base pointers from PPU_R0 and request pattern cache refresh
static void recalcPatternBases()
{
  BYTE *base0 = ChrBuf;            // decoded tiles for pattern table 0 ($0000)
  BYTE *base1 = ChrBuf + 256 * 64; // decoded tiles for pattern table 1 ($1000)
  PPU_BG_Base = (PPU_R0 & 0x10) ? base1 : base0;
  PPU_SP_Base = (PPU_R0 & 0x08) ? base1 : base0;
  // Force tile decode refresh on next render path
  ChrBufUpdate = 1;
}

/* -------- Public API: Save -------- */
int Emulator_SaveState(const char *path)
{
  SaveHeader hdr{};
  memcpy(hdr.magic, "INFOST\1", 8);
  hdr.version = SAVESTATEFILE_VERSION;
  hdr.mapperNo = MapperNo;
  hdr.flags = 0;
  if (NesHeader.byVRomSize == 0) hdr.flags |= SAVESTATE_FLAG_CHR_RAM;
  hdr.flags |= ((uint32_t)InfoNES_GetRegion() << SAVESTATE_FLAG_REGION_SHIFT)
               & SAVESTATE_FLAG_REGION_MASK;

  struct SaveCore *coreDyn;
  coreDyn = (struct SaveCore *)Frens::f_malloc(sizeof(SaveCore));
  // Use dynamic allocation to avoid stack overflow on constrained systems
  if (!coreDyn) {
    printf("SaveState: out of memory for core data\n");
    return -1;
  }
  // Initialize to zero to avoid uninitialized padding bytes
  memset(coreDyn, 0, sizeof(SaveCore));
  SaveCore &core = *coreDyn;

  // CPU registers & timing
  core.PC = PC;
  core.SP = SP;
  core.F = F;
  core.A = A;
  core.X = X;
  core.Y = Y;
  core.IRQ_State = IRQ_State;
  core.IRQ_Wiring = IRQ_Wiring;
  core.NMI_State = NMI_State;
  core.NMI_Wiring = NMI_Wiring;
  core.g_wPassedClocks = g_wPassedClocks;
  core.g_wCurrentClocks = g_wCurrentClocks;

  // PPU registers/state
  core.PPU_R0 = PPU_R0;
  core.PPU_R1 = PPU_R1;
  core.PPU_R2 = PPU_R2;
  core.PPU_R3 = PPU_R3;
  core.PPU_R7 = PPU_R7;
  core.PPU_Addr = PPU_Addr;
  core.PPU_Temp = PPU_Temp;
  core.PPU_Increment = PPU_Increment;
  core.PPU_Scanline = PPU_Scanline;
  core.PPU_NameTableBank = PPU_NameTableBank;
  core.PPU_SP_Height = PPU_SP_Height;
  core.PPU_Scr_H_Byte = PPU_Scr_H_Byte;
  core.PPU_Scr_H_Bit = PPU_Scr_H_Bit;
  core.PPU_Latch_Flag = PPU_Latch_Flag;
  core.PPU_UpDown_Clip = PPU_UpDown_Clip;
  core.FrameIRQ_Enable = FrameIRQ_Enable;
  core.FrameStep = FrameStep;
  core.SpriteJustHit = SpriteJustHit;

  // Misc/frame
  core.FrameSkip = FrameSkip;
  core.FrameCnt = FrameCnt;
  core.ChrBufUpdate = ChrBufUpdate;
  core.byVramWriteEnable = byVramWriteEnable;
  core.ROM_Mirroring = ROM_Mirroring;
  memcpy(core.PalTable, PalTable, sizeof core.PalTable);
  memcpy(core.APU_Reg, APU_Reg, sizeof core.APU_Reg);
  // APU externals
  core.cur_event = cur_event;
  core.entertime = entertime;
  core.ApuCtrl = ApuCtrl;
  core.ApuCtrlNew = ApuCtrlNew;
  core.ApuQuality = ApuQuality;
  core.ApuPulseMagic = ApuPulseMagic;
  core.ApuTriangleMagic = ApuTriangleMagic;
  core.ApuNoiseMagic = ApuNoiseMagic;
  core.ApuSamplesPerSync16 = ApuSamplesPerSync16;
  core.ApuCyclesPerSample = ApuCyclesPerSample;
  core.ApuSampleRate = ApuSampleRate;
  core.ApuCycleRate = ApuCycleRate;
  core.ApuC1a = ApuC1a;
  core.ApuC1b = ApuC1b;
  core.ApuC1c = ApuC1c;
  core.ApuC1d = ApuC1d;
  core.ApuC1Skip = ApuC1Skip;
  core.ApuC1Index = ApuC1Index;
  core.ApuC1EnvPhase = ApuC1EnvPhase;
  core.ApuC1EnvVol = ApuC1EnvVol;
  core.ApuC1Atl = ApuC1Atl;
  core.ApuC1SweepPhase = ApuC1SweepPhase;
  core.ApuC1Freq = ApuC1Freq;
  core.ApuC2a = ApuC2a;
  core.ApuC2b = ApuC2b;
  core.ApuC2c = ApuC2c;
  core.ApuC2d = ApuC2d;
  core.ApuC2Skip = ApuC2Skip;
  core.ApuC2Index = ApuC2Index;
  core.ApuC2EnvPhase = ApuC2EnvPhase;
  core.ApuC2EnvVol = ApuC2EnvVol;
  core.ApuC2Atl = ApuC2Atl;
  core.ApuC2SweepPhase = ApuC2SweepPhase;
  core.ApuC2Freq = ApuC2Freq;
  core.ApuC3a = ApuC3a;
  core.ApuC3b = ApuC3b;
  core.ApuC3c = ApuC3c;
  core.ApuC3d = ApuC3d;
  core.ApuC3Skip = ApuC3Skip;
  core.ApuC3Index = ApuC3Index;
  core.ApuC3Atl = ApuC3Atl;
  core.ApuC3Llc = ApuC3Llc;
  core.ApuC3ReloadFlag = ApuC3ReloadFlag;
  core.ApuC4a = ApuC4a;
  core.ApuC4b = ApuC4b;
  core.ApuC4c = ApuC4c;
  core.ApuC4d = ApuC4d;
  core.ApuC4Sr = ApuC4Sr;
  core.ApuC4Fdc = ApuC4Fdc;
  core.ApuC4Skip = ApuC4Skip;
  core.ApuC4Index = ApuC4Index;
  core.ApuC4Atl = ApuC4Atl;
  core.ApuC4EnvVol = ApuC4EnvVol;
  core.ApuC4EnvPhase = ApuC4EnvPhase;
  memcpy(core.ApuC5Reg, ApuC5Reg, sizeof core.ApuC5Reg);
  core.ApuC5Enable = ApuC5Enable;
  core.ApuC5Looping = ApuC5Looping;
  core.ApuC5CurByte = ApuC5CurByte;
  core.ApuC5DpcmValue = ApuC5DpcmValue;
  core.ApuC5Freq = ApuC5Freq;
  core.ApuC5Phaseacc = ApuC5Phaseacc;
  core.ApuC5Address = ApuC5Address;
  core.ApuC5CacheAddr = ApuC5CacheAddr;
  core.ApuC5DmaLength = ApuC5DmaLength;
  core.ApuC5CacheDmaLength = ApuC5CacheDmaLength;

  // Controller
  core.PAD1_Latch = PAD1_Latch;
  core.PAD2_Latch = PAD2_Latch;
  core.PAD_System = PAD_System;
  core.PAD1_Bit = PAD1_Bit;
  core.PAD2_Bit = PAD2_Bit;

  // Bank indices
  fillBankIndices(core);

  // Optional mapper / APU supplemental data
  void *mapperBlob = nullptr;
  size_t mapperSize = MapperBlobSize ? MapperBlobSize() : 0;
  if (mapperSize > 0 && MapperSaveBlob) {
    mapperBlob = Frens::f_malloc(mapperSize);
    if (!mapperBlob) {
      printf("SaveState: out of memory for mapper blob (%u bytes)\n", (unsigned)mapperSize);
      Frens::f_free(coreDyn);
      return -1;
    }
    MapperSaveBlob((BYTE *)mapperBlob);
  // } else {
  //   Mapper_Save(mapperBlob, mapperSize);
  }
  void *apuBlob = nullptr;
  size_t apuSize = 0;
  pAPU_Save(apuBlob, apuSize);

  // Open file (overwrite)
  FIL fp;
  if (f_open(&fp, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
    printf("SaveState: failed to open file %s\n", path);
    Frens::f_free(coreDyn);
    if (mapperBlob) Frens::f_free(mapperBlob);
    return -1;
  }

  auto w = [&](const void *buf, UINT len) -> bool
  {
    UINT bw;
    return f_write(&fp, buf, len, &bw) == FR_OK && bw == len;
  };

  // Serialize header + core struct + primary RAM regions
  if (!w(&hdr, sizeof hdr) ||
      !w(&core, sizeof core) ||
      !w(RAM, RAM_SIZE) ||
      !w(SPRRAM, SPRRAM_SIZE) ||
      !w(SRAM, SRAM_SIZE))
  {
    f_close(&fp);
    printf("SaveState: failed to write core data\n");
    Frens::f_free(coreDyn);
    if (mapperBlob) Frens::f_free(mapperBlob);
    return -1;
  }
   Frens::f_free(coreDyn);
  // Always save entire PPURAM (pattern for CHR RAM + nametables + palette area)
  if (!w(PPURAM, PPURAM_SIZE))
  {
    f_close(&fp);
    printf("SaveState: failed to write PPURAM\n");
    if (mapperBlob) Frens::f_free(mapperBlob);
    return -1;
  }

  // CHR RAM the mapper keeps outside PPURAM (mappers 13, 30, 85, 96). Written
  // straight from the buffer so no second copy has to be allocated - an RP2040
  // has no room for one.
  if (MapperChrRam && !w(MapperChrRam, MapperChrRamSize))
  {
    f_close(&fp);
    printf("SaveState: failed to write mapper CHR RAM\n");
    if (mapperBlob) Frens::f_free(mapperBlob);
    return -1;
  }

  // Mapper / APU blobs length + payload
  if (!w(&mapperSize, sizeof mapperSize))
  {
    f_close(&fp);
    printf("SaveState: failed to write mapper blob size\n");
    if (mapperBlob) Frens::f_free(mapperBlob);
    return -1;
  }
  if (mapperSize && !w(mapperBlob, mapperSize))
  {
    f_close(&fp);
    printf("SaveState: failed to write mapper blob\n");
    if (mapperBlob) Frens::f_free(mapperBlob);
    return -1;
  }
  if (!w(&apuSize, sizeof apuSize))
  {
    f_close(&fp);
    printf("SaveState: failed to write APU blob size\n");
    if (mapperBlob) Frens::f_free(mapperBlob);
    return -1;
  }
  if (apuSize && !w(apuBlob, apuSize))
  {
    f_close(&fp);
    printf("SaveState: failed to write APU blob\n");
    if (mapperBlob) Frens::f_free(mapperBlob);
    return -1;
  }
  if (mapperBlob) Frens::f_free(mapperBlob);
  f_close(&fp);
  return 0;
}

/* -------- Public API: Load -------- */
int Emulator_LoadState(const char *path)
{
  FIL fp;
  if (f_open(&fp, path, FA_READ) != FR_OK) {
    printf("LoadState: failed to open file %s\n", path);
    return -1;
  }

  auto r = [&](void *buf, UINT len) -> bool
  {
    UINT br;
    return f_read(&fp, buf, len, &br) == FR_OK && br == len;
  };

  SaveHeader hdr{};
  if (!r(&hdr, sizeof hdr) ||
      memcmp(hdr.magic, "INFOST\1", 8) != 0 ||
      hdr.version != SAVESTATEFILE_VERSION ||
      hdr.mapperNo != MapperNo)
  {
    printf("LoadState: invalid header\n");
    f_close(&fp);
    return -1;
  }

  // Region must match the running emulator. The save carries region-dependent
  // APU magic numbers and timing; loading across regions would mix PAL APU
  // values with NTSC PPU constants (or vice versa) and run at the wrong speed.
  int savedRegion = (hdr.flags & SAVESTATE_FLAG_REGION_MASK) >> SAVESTATE_FLAG_REGION_SHIFT;
  if (savedRegion != InfoNES_GetRegion())
  {
    static const char *names[] = { "NTSC", "PAL", "Dendy" };
    printf("LoadState: region mismatch (save=%s, current=%s)\n",
           names[savedRegion & 3], names[InfoNES_GetRegion() & 3]);
    f_close(&fp);
    return -1;
  }
  struct SaveCore *coreDyn;
  coreDyn = (struct SaveCore *)Frens::f_malloc(sizeof(SaveCore));
  // Use dynamic allocation to avoid stack overflow on constrained systems
  if (!coreDyn) {
    f_close(&fp);
    printf("LoadState: out of memory for core data\n");
    return -1;
  }
  SaveCore &core = *coreDyn;
  if (!r(&core, sizeof core) ||
      !r(RAM, RAM_SIZE) ||
      !r(SPRRAM, SPRRAM_SIZE) ||
      !r(SRAM, SRAM_SIZE))
  {
    f_close(&fp);
    printf("LoadState: failed to read core data\n");
    Frens::f_free(coreDyn);
    return -1;
  }

  // Restore PPURAM (nametables + palette + CHR RAM if present)
  if (!r(PPURAM, PPURAM_SIZE))
  {
    f_close(&fp);
    printf("LoadState: failed to read PPURAM\n");
    Frens::f_free(coreDyn);
    return -1;
  }

  // CHR RAM stored outside PPURAM (see Emulator_SaveState)
  if (MapperChrRam && !r(MapperChrRam, MapperChrRamSize))
  {
    f_close(&fp);
    printf("LoadState: failed to read mapper CHR RAM\n");
    Frens::f_free(coreDyn);
    return -1;
  }

  // Mapper blob
  size_t mapperSize = 0;
  if (!r(&mapperSize, sizeof mapperSize))
  {
    f_close(&fp);
    printf("LoadState: failed to read mapper blob size\n");
    Frens::f_free(coreDyn);
    return -1;
  }
  // mapper blob buffer
  // Sanity bound: mapper blobs are small structs. A wild size here means a
  // stale or corrupt file, and f_malloc panics on an absurd PSRAM request.
  if (mapperSize > 0x10000)
  {
    f_close(&fp);
    printf("LoadState: bogus mapper blob size %u, refusing state\n", (unsigned)mapperSize);
    Frens::f_free(coreDyn);
    return -1;
  }
  BYTE  *mapperBuf = nullptr;
  if (mapperSize && MapperLoadBlob)
  {
    mapperBuf = (BYTE *)Frens::f_malloc(mapperSize);
    if (!mapperBuf)
    {
      f_close(&fp);
      printf("LoadState: out of memory for mapper blob (%u bytes)\n", (unsigned)mapperSize);
      Frens::f_free(coreDyn);
      return -1;
    }
    if (!r(mapperBuf, mapperSize))
    {
      f_close(&fp);
      printf("LoadState: failed to read mapper blob\n");
      Frens::f_free(coreDyn);
      Frens::f_free(mapperBuf);
      return -1;
    }
   
  }

  // APU blob
  size_t apuSize = 0;
  if (!r(&apuSize, sizeof apuSize))
  {
    f_close(&fp);
    printf("LoadState: failed to read APU blob size\n");
    Frens::f_free(coreDyn);
    return -1;
  }
  std::unique_ptr<BYTE[]> apuBuf;
  if (apuSize)
  {
    apuBuf.reset(new BYTE[apuSize]);
    if (!r(apuBuf.get(), apuSize))
    {
      f_close(&fp);
      printf("LoadState: failed to read APU blob\n");
      Frens::f_free(coreDyn);
      return -1;
    }
  }

  f_close(&fp);

  // CPU restore
  PC = core.PC;
  SP = core.SP;
  F = core.F;
  A = core.A;
  X = core.X;
  Y = core.Y;
  IRQ_State = core.IRQ_State;
  IRQ_Wiring = core.IRQ_Wiring;
  NMI_State = core.NMI_State;
  NMI_Wiring = core.NMI_Wiring;
  g_wPassedClocks = core.g_wPassedClocks;
  g_wCurrentClocks = core.g_wCurrentClocks;

  // PPU restore
  PPU_R0 = core.PPU_R0;
  PPU_R1 = core.PPU_R1;
  PPU_R2 = core.PPU_R2;
  PPU_R3 = core.PPU_R3;
  PPU_R7 = core.PPU_R7;
  PPU_Addr = core.PPU_Addr;
  PPU_Temp = core.PPU_Temp;
  PPU_Increment = core.PPU_Increment;
  PPU_Scanline = core.PPU_Scanline;
  PPU_NameTableBank = core.PPU_NameTableBank;
  PPU_SP_Height = core.PPU_SP_Height;
  PPU_Scr_H_Byte = core.PPU_Scr_H_Byte;
  PPU_Scr_H_Bit = core.PPU_Scr_H_Bit;
  PPU_Latch_Flag = core.PPU_Latch_Flag;
  PPU_UpDown_Clip = core.PPU_UpDown_Clip;
  FrameIRQ_Enable = core.FrameIRQ_Enable;
  FrameStep = core.FrameStep;
  SpriteJustHit = core.SpriteJustHit;

  // Misc restore
  FrameSkip = core.FrameSkip;
  FrameCnt = core.FrameCnt;
  byVramWriteEnable = core.byVramWriteEnable;
  ROM_Mirroring = core.ROM_Mirroring;
  memcpy(PalTable, core.PalTable, sizeof PalTable);
  memcpy(APU_Reg, core.APU_Reg, sizeof APU_Reg);
  // APU externals restore
  cur_event = core.cur_event;
  entertime = core.entertime;
  ApuCtrl = core.ApuCtrl;
  ApuCtrlNew = core.ApuCtrlNew;
  ApuQuality = core.ApuQuality;
  ApuPulseMagic = core.ApuPulseMagic;
  ApuTriangleMagic = core.ApuTriangleMagic;
  ApuNoiseMagic = core.ApuNoiseMagic;
  ApuSamplesPerSync16 = core.ApuSamplesPerSync16;
  ApuCyclesPerSample = core.ApuCyclesPerSample;
  ApuSampleRate = core.ApuSampleRate;
  ApuCycleRate = core.ApuCycleRate;
  ApuC1a = core.ApuC1a;
  ApuC1b = core.ApuC1b;
  ApuC1c = core.ApuC1c;
  ApuC1d = core.ApuC1d;
  ApuC1Skip = core.ApuC1Skip;
  ApuC1Index = core.ApuC1Index;
  ApuC1EnvPhase = core.ApuC1EnvPhase;
  ApuC1EnvVol = core.ApuC1EnvVol;
  ApuC1Atl = core.ApuC1Atl;
  ApuC1SweepPhase = core.ApuC1SweepPhase;
  ApuC1Freq = core.ApuC1Freq;
  ApuC2a = core.ApuC2a;
  ApuC2b = core.ApuC2b;
  ApuC2c = core.ApuC2c;
  ApuC2d = core.ApuC2d;
  ApuC2Skip = core.ApuC2Skip;
  ApuC2Index = core.ApuC2Index;
  ApuC2EnvPhase = core.ApuC2EnvPhase;
  ApuC2EnvVol = core.ApuC2EnvVol;
  ApuC2Atl = core.ApuC2Atl;
  ApuC2SweepPhase = core.ApuC2SweepPhase;
  ApuC2Freq = core.ApuC2Freq;
  ApuC3a = core.ApuC3a;
  ApuC3b = core.ApuC3b;
  ApuC3c = core.ApuC3c;
  ApuC3d = core.ApuC3d;
  ApuC3Skip = core.ApuC3Skip;
  ApuC3Index = core.ApuC3Index;
  ApuC3Atl = core.ApuC3Atl;
  ApuC3Llc = core.ApuC3Llc;
  ApuC3ReloadFlag = core.ApuC3ReloadFlag;
  ApuC4a = core.ApuC4a;
  ApuC4b = core.ApuC4b;
  ApuC4c = core.ApuC4c;
  ApuC4d = core.ApuC4d;
  ApuC4Sr = core.ApuC4Sr;
  ApuC4Fdc = core.ApuC4Fdc;
  ApuC4Skip = core.ApuC4Skip;
  ApuC4Index = core.ApuC4Index;
  ApuC4Atl = core.ApuC4Atl;
  ApuC4EnvVol = core.ApuC4EnvVol;
  ApuC4EnvPhase = core.ApuC4EnvPhase;
  memcpy(ApuC5Reg, core.ApuC5Reg, sizeof ApuC5Reg);
  ApuC5Enable = core.ApuC5Enable;
  ApuC5Looping = core.ApuC5Looping;
  ApuC5CurByte = core.ApuC5CurByte;
  ApuC5DpcmValue = core.ApuC5DpcmValue;
  ApuC5Freq = core.ApuC5Freq;
  ApuC5Phaseacc = core.ApuC5Phaseacc;
  ApuC5Address = core.ApuC5Address;
  ApuC5CacheAddr = core.ApuC5CacheAddr;
  ApuC5DmaLength = core.ApuC5DmaLength;
  ApuC5CacheDmaLength = core.ApuC5CacheDmaLength;

  // Controller restore
  PAD1_Latch = core.PAD1_Latch;
  PAD2_Latch = core.PAD2_Latch;
  PAD_System = core.PAD_System;
  PAD1_Bit = core.PAD1_Bit;
  PAD2_Bit = core.PAD2_Bit;

  // Rebind banks & mirroring
  restorePPUBanks(core);
  restorePRGBanks(core);
  InfoNES_Mirroring(ROM_Mirroring);

  // Update pattern table base pointers & schedule decode refresh
  recalcPatternBases();
  Frens::f_free(coreDyn);
  // Mapper / APU custom state restore
  // if (Mapper_Load(mapperBuf.get(), mapperSize) < 0) {
  //   printf("LoadState: failed to load mapper state\n");
  //   return -1;
  // }
  if (pAPU_Load(apuBuf.get(), apuSize) < 0) {
    printf("LoadState: failed to load APU state\n");  
    return -1;
  }
  // Restore mapper state from blob
  if (mapperSize && MapperLoadBlob  ) {
    printf("LoadState: calling MapperLoadBlob\n");
    MapperLoadBlob(mapperBuf);
    Frens::f_free(mapperBuf);
  }
  return 0;
}