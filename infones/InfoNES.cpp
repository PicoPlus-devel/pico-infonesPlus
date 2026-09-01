/*===================================================================*/
/*                                                                   */
/*  InfoNES.cpp : NES Emulator for Win32, Linux(x86), Linux(PS2)     */
/*                                                                   */
/*  2000/05/18  InfoNES Project ( based on pNesX )                   */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------
 * File List :
 *
 * [NES Hardware]
 *   InfoNES.cpp
 *   InfoNES.h
 *   K6502_rw.h
 *
 * [Mapper function]
 *   InfoNES_Mapper.cpp
 *   InfoNES_Mapper.h
 *
 * [The function which depends on a system]
 *   InfoNES_System_ooo.cpp (ooo is a system name. win, ...)
 *   InfoNES_System.h
 *
 * [CPU]
 *   K6502.cpp
 *   K6502.h
 *
 * [Others]
 *   InfoNES_Types.h
 *
 --------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/*  Include files                                                    */
/*-------------------------------------------------------------------*/

#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_Mapper.h"
#include "InfoNES_pAPU.h"
#include "InfoNES_NSF.h"
#include "InfoNES_FDS.h"
#include "K6502.h"
#include <assert.h>
#include <pico.h>
#include <tuple>
#include <cstdio>
#include <cstdlib>
#include <util/work_meter.h>
#include "FrensHelpers.h"
constexpr uint16_t makeTag(int r, int g, int b)
{
  return (r << 10) | (g << 5) | (b);
}
enum
{
  MARKER_START = makeTag(0, 31, 31),
  MARKER_CPU = makeTag(0, 31, 0),
  MARKER_SOUND = makeTag(31, 31, 0),
  MARKER_BG = makeTag(0, 0, 31),
  MARKER_SPRITE = makeTag(31, 0, 0),
};

/*-------------------------------------------------------------------*/
/*  NES resources                                                    */
/*-------------------------------------------------------------------*/

#pragma region buffers
/* RAM */
BYTE *RAM;
// Share this memory with other components (menu.cpp, romselect.cpp, main.cpp)
// void *InfoNes_GetRAM(size_t *size)
// {
//   printf("Acquired RAM Buffer from emulator: %d bytes\n", RAM_SIZE);
//   *size = RAM_SIZE;
//   return SRAM;
// }
/* SRAM */
BYTE *SRAM;

/* Character Buffer */
BYTE *ChrBuf;

// Share this memory with other components (menu.cpp, romselect.cpp, main.cpp)
// void *InfoNes_GetChrBuf(size_t *size)
// {
//   printf("Acquired ChrBuf Buffer from emulator: %d bytes\n", CHRBUF_SIZE);
//   *size = CHRBUF_SIZE;
//   return ChrBuf;
// }
/* PPU RAM */
BYTE *PPURAM;
// Share this memory with other components (menu.cpp, romselect.cpp, main.cpp)
// void *InfoNes_GetPPURAM(size_t *size)
// {
//   printf("Acquired PPURAM Buffer from emulator: %d bytes\n", PPURAM_SIZE);
//   *size = PPURAM_SIZE;
//   return PPURAM;
// }
/* PPU BANK ( 1Kb * 16 ) */
BYTE *PPUBANK[16];
/* Sprite RAM */
BYTE *SPRRAM;
// Share this memory with other components (menu.cpp, romselect.cpp, main.cpp)
// void *InfoNes_GetSPRRAM(size_t *size)
// {
//   printf("Acquired SPRRAM Buffer from emulator: %d bytes\n", SPRRAM_SIZE);
//   *size = SPRRAM_SIZE;
//   return SPRRAM;
// }
/* Scanline Table */
/* Sized for the largest region (PAL/Dendy: 312 scanlines). NTSC uses indices
 * 0..261; PAL/Dendy use 0..311. The init loop fills the full range. */
BYTE PPU_ScanTable[312];
#pragma endregion

bool SRAMwritten = false;

/* ROM */
BYTE *ROM;

/* SRAM BANK ( 8Kb ) */
BYTE *SRAMBANK;

/* ROM BANK ( 8Kb * 4 ) */
BYTE *ROMBANK[4];
// BYTE *ROMBANK0;
// BYTE *ROMBANK1;
// BYTE *ROMBANK2;
// BYTE *ROMBANK3;

/*-------------------------------------------------------------------*/
/*  PPU resources                                                    */
/*-------------------------------------------------------------------*/

/* VROM */
BYTE *VROM;

// BYTE *SPRRAM;
/* PPU Register */
BYTE PPU_R0;
BYTE PPU_R1;
BYTE PPU_R2;
BYTE PPU_R3;
BYTE PPU_R7;

/* Vertical scroll value */
// BYTE PPU_Scr_V;
// BYTE PPU_Scr_V_Next;
// BYTE PPU_Scr_V_Byte;
// BYTE PPU_Scr_V_Byte_Next;
// BYTE PPU_Scr_V_Bit;
// BYTE PPU_Scr_V_Bit_Next;

/* Horizontal scroll value */
// BYTE PPU_Scr_H;
// BYTE PPU_Scr_H_Next;
BYTE PPU_Scr_H_Byte;
// BYTE PPU_Scr_H_Byte_Next;
BYTE PPU_Scr_H_Bit;
// BYTE PPU_Scr_H_Bit_Next;

/* PPU Address */
WORD PPU_Addr;

/* PPU Address */
WORD PPU_Temp;

/* The increase value of the PPU Address */
WORD PPU_Increment;

/* Current Scanline */
WORD PPU_Scanline;

/* Name Table Bank */
BYTE PPU_NameTableBank;

/* BG Base Address */
BYTE *PPU_BG_Base;

/* Sprite Base Address */
BYTE *PPU_SP_Base;

/* Sprite Height */
WORD PPU_SP_Height;

/* Sprite #0 Scanline Hit Position */
int SpriteJustHit;

/* VRAM Write Enable ( 0: Disable, 1: Enable ) */
BYTE byVramWriteEnable;

/* PPU Address and Scroll Latch Flag*/
BYTE PPU_Latch_Flag;

/* Up and Down Clipping Flag ( 0: non-clip, 1: clip ) */
BYTE PPU_UpDown_Clip;

/* Frame IRQ ( 0: Disabled, 1: Enabled )*/
BYTE FrameIRQ_Enable;
WORD FrameStep;

/*-------------------------------------------------------------------*/
/*  Display and Others resouces                                      */
/*-------------------------------------------------------------------*/

/* Frame Skip */
WORD FrameSkip;
WORD FrameCnt;

/* Display Buffer */
#if 0
WORD DoubleFrame[ 2 ][ NES_DISP_WIDTH * NES_DISP_HEIGHT ];
WORD *WorkFrame;
WORD WorkFrameIdx;
#else
// WORD WorkFrame[ NES_DISP_WIDTH * NES_DISP_HEIGHT ];
WORD *WorkLine = nullptr;
void __not_in_flash_func(InfoNES_SetLineBuffer)(WORD *p, WORD size)
{
  assert(size >= NES_DISP_WIDTH);
  WorkLine = p;
}
#endif

/* Update flag for ChrBuf */
BYTE ChrBufUpdate;

/* Palette Table */
WORD PalTable[32];

/* Region-dependent timing. Defaults to NTSC; InfoNES_SetRegion() overrides. */
WORD STEP_PER_SCANLINE = 114;
WORD STEP_PER_FRAME    = 29780;
WORD SCAN_VBLANK_START = 241;
WORD SCAN_VBLANK_END   = 261;

/* Table for Mirroring */
BYTE PPU_MirrorTable[][4] =
    {
        {NAME_TABLE0, NAME_TABLE0, NAME_TABLE1, NAME_TABLE1},
        {NAME_TABLE0, NAME_TABLE1, NAME_TABLE0, NAME_TABLE1},
        {NAME_TABLE1, NAME_TABLE1, NAME_TABLE1, NAME_TABLE1},
        {NAME_TABLE0, NAME_TABLE0, NAME_TABLE0, NAME_TABLE0},
        {NAME_TABLE0, NAME_TABLE1, NAME_TABLE2, NAME_TABLE3},
        {NAME_TABLE0, NAME_TABLE0, NAME_TABLE0, NAME_TABLE1}};

/*-------------------------------------------------------------------*/
/*  APU and Pad resources                                            */
/*-------------------------------------------------------------------*/

/* APU Register */
BYTE APU_Reg[0x18];

/* APU Mute ( 0:OFF, 1:ON ) */
int APU_Mute = 0;

/* Pad data */
DWORD PAD1_Latch;
DWORD PAD2_Latch;
DWORD PAD_System;
DWORD PAD1_Bit;
DWORD PAD2_Bit;

/*-------------------------------------------------------------------*/
/*  Mapper Function                                                  */
/*-------------------------------------------------------------------*/

/* Initialize Mapper */
void (*MapperInit)();
/* Write to Mapper */
void (*MapperWrite)(WORD wAddr, BYTE byData);
/* Write to SRAM */
void (*MapperSram)(WORD wAddr, BYTE byData);
/* Write to Apu */
void (*MapperApu)(WORD wAddr, BYTE byData);
/* Read from Apu */
BYTE(*MapperReadApu)
(WORD wAddr);
/* Callback at VSync */
void (*MapperVSync)();
/* Callback at HSync */
void (*MapperHSync)();
/* Callback at PPU read/write */
void (*MapperPPU)(WORD wAddr); // mapper 96だけ？
/* Callback at sprite pattern fetch - MMC2/MMC4 CHR latch only, else null */
void (*MapperSprPPU)(WORD wAddr);
/* False when MapperPPU is the do-nothing Map0_PPU stub, which is the case for
   all but a handful of mappers. The background tile loop below consults this
   instead of making ~33 indirect calls per scanline into a flash-resident
   no-op - on RP2040 that call and its argument setup are pure loss. */
bool MapperPPUActive;
/* Set whenever OAM or the $2000 sprite bits change, so the MMC2/MMC4
   sprite-fetch trigger list gets rebuilt before it is next used. */
bool SprLatchDirty;
/* Callback at Rendering Screen 1:BG, 0:Sprite */
void (*MapperRenderScreen)(BYTE byMode);

int (*MapperBlobSize)();
void (*MapperSaveBlob)(BYTE *pBuf);
void (*MapperLoadBlob)(BYTE *pBuf);

/*-------------------------------------------------------------------*/
/*  ROM information                                                  */
/*-------------------------------------------------------------------*/

/* .nes File Header */
struct NesHeader_tag NesHeader;

/* Mapper Number */
BYTE MapperNo;

/* Mirroring 0:Horizontal 1:Vertical */
BYTE ROM_Mirroring;
/* It has SRAM */
BYTE ROM_SRAM;
/* It has Trainer */
BYTE ROM_Trainer;
/* Four screen VRAM  */
BYTE ROM_FourScr;

/* True when the loaded image is a Famicom Disk System disk (no iNES header). */
bool IsFDS = false;

/*===================================================================*/
/*                                                                   */
/*                InfoNES_Init() : Initialize InfoNES                */
/*                                                                   */
/*===================================================================*/
void InfoNES_Init()
{
  /*
   *  Initialize InfoNES
   *
   *  Remarks
   *    Initialize memory, K6502 and Scanline Table.
   */

  RAM = (BYTE *)Frens::f_malloc(RAM_SIZE);
  SRAM = (BYTE *)Frens::f_malloc(SRAM_SIZE);
  PPURAM = (BYTE *)Frens::f_malloc(PPURAM_SIZE);
  SPRRAM = (BYTE *)Frens::f_malloc(SPRRAM_SIZE);
  ChrBuf = (BYTE *)Frens::f_malloc(CHRBUF_SIZE);

  int nIdx;

  // Initialize 6502
  K6502_Init();

  // Initialize Scanline Table over the full PAL/Dendy range. SCAN_VBLANK_START
  // is region-dependent (241 for NTSC/PAL, 291 for Dendy) so post-render and
  // vblank boundaries land in the right place per region.
  for (nIdx = 0; nIdx < (int)(sizeof PPU_ScanTable); ++nIdx)
  {
    if (nIdx < SCAN_UNKNOWN_START)
      PPU_ScanTable[nIdx] = SCAN_ON_SCREEN;
    else if (nIdx < SCAN_VBLANK_START)
      PPU_ScanTable[nIdx] = SCAN_UNKNOWN;
    else
      PPU_ScanTable[nIdx] = SCAN_VBLANK;
  }
}

/*===================================================================*/
/*                                                                   */
/*                InfoNES_Fin() : Completion treatment               */
/*                                                                   */
/*===================================================================*/
void InfoNES_Fin()
{
  /*
   *  Completion treatment
   *
   *  Remarks
   *    Release resources
   */
  // Finalize pAPU
  printf("Quit emulation, releasing resources.\n");
  InfoNES_pAPUDone();

  // Release a memory for ROM
  InfoNES_ReleaseRom();
  Frens::f_free(RAM);
  Frens::f_free(SRAM);
  Frens::f_free(PPURAM);
  Frens::f_free(SPRRAM);
  Frens::f_free(ChrBuf);
#if PICO_RP2350
  if (Map5_Wram) { Frens::f_free(Map5_Wram); Map5_Wram = nullptr; }
  if (Map5_Ex_Ram) { Frens::f_free(Map5_Ex_Ram); Map5_Ex_Ram = nullptr; }
  if (Map5_Ex_Vram) { Frens::f_free(Map5_Ex_Vram); Map5_Ex_Vram = nullptr; }
  if (Map5_Ex_Nam) { Frens::f_free(Map5_Ex_Nam); Map5_Ex_Nam = nullptr; }
  Map5_Gfx_Mode = 0;
#endif
  if (Map85_Chr_Ram) { Frens::f_free(Map85_Chr_Ram); Map85_Chr_Ram = nullptr; }
  if (Map30_Chr_Ram) { Frens::f_free(Map30_Chr_Ram); Map30_Chr_Ram = nullptr; }
  if (DRAM) { Frens::f_free(DRAM); DRAM = nullptr; }
}

/*===================================================================*/
/*                                                                   */
/*                  InfoNES_Load() : Load a cassette                 */
/*                                                                   */
/*===================================================================*/
int InfoNES_Load(const char *pszFileName)
{
  /*
   *  Load a cassette
   *
   *  Parameters
   *    const char *pszFileName            (Read)
   *      File name of ROM image
   *
   *  Return values
   *     0 : It was finished normally.
   *    -1 : An error occurred.
   *
   *  Remarks
   *    Read a ROM image in the memory.
   *    Reset InfoNES.
   */

  // Release a memory for ROM
  InfoNES_ReleaseRom();

  // Read a ROM image in the memory
  if (InfoNES_ReadRom(pszFileName) < 0)
    return -1;

  // Reset InfoNES
  if (InfoNES_Reset() < 0)
    return -1;

  // Successful
  return 0;
}

/*===================================================================*/
/*                                                                   */
/*                 InfoNES_Reset() : Reset InfoNES                   */
/*                                                                   */
/*===================================================================*/
int InfoNES_Reset()
{
  /*
   *  Reset InfoNES
   *
   *  Return values
   *     0 : Normally
   *    -1 : Non support mapper
   *
   *  Remarks
   *    Initialize Resources, PPU and Mapper.
   *    Reset CPU.
   */

  int nIdx;

  /*-------------------------------------------------------------------*/
  /*  Get information on the cassette                                  */
  /*-------------------------------------------------------------------*/

  // Get Mapper Number
  // MapperNo = (NesHeader.byInfo1 >> 4) ;

  // // Check bit counts of Mapper No.
  // for (nIdx = 4; nIdx < 8 && NesHeader.byReserve[nIdx] == 0; ++nIdx)
  //   ;

  // if (nIdx == 8)
  // {
  //   // Mapper Number is 8bits
  //   MapperNo |= (NesHeader.byInfo2 & 0xf0);
  // }

  if (IsFDS)
  {
    // Famicom Disk System: no iNES header, dispatch through synthetic mapper 20.
    MapperNo = 20;
    ROM_Mirroring = 0;
    ROM_SRAM = 0;
    ROM_Trainer = 0;
    ROM_FourScr = 0;
  }
  else if (IsNSF)
  {
    // Nintendo Sound Format: dispatch through synthetic mapper 31.
    MapperNo = 31;
    ROM_Mirroring = 0;
    ROM_SRAM = 0;
    ROM_Trainer = 0;
    ROM_FourScr = 0;
  }
  else
  {
    // Mapper Number is 8bits. Always use lower 4bits of byInfo2 for compatibility with old ROMs.
    MapperNo = (NesHeader.byInfo1 >> 4) | (NesHeader.byInfo2 & 0xf0);

    // Get information on the ROM
    ROM_Mirroring = NesHeader.byInfo1 & 1;
    ROM_SRAM = NesHeader.byInfo1 & 2;
    ROM_Trainer = NesHeader.byInfo1 & 4;
    ROM_FourScr = NesHeader.byInfo1 & 8;
  }

  /*-------------------------------------------------------------------*/
  /*  Initialize resources                                             */
  /*-------------------------------------------------------------------*/

  // Clear RAM
  InfoNES_MemorySet(RAM, 0, RAM_SIZE);

  // Reset frame skip and frame count
  FrameSkip = 0;
  FrameCnt = 0;

#if 0
  // Reset work frame
  WorkFrame = DoubleFrame[ 0 ];
  WorkFrameIdx = 0;
#endif

  // Reset update flag of ChrBuf
  ChrBufUpdate = 0xff;

  // Reset palette table
  InfoNES_MemorySet(PalTable, 0, sizeof PalTable);

  // Reset APU register
  InfoNES_MemorySet(APU_Reg, 0, sizeof APU_Reg);

  // Reset joypad
  PAD1_Latch = PAD2_Latch = PAD_System = 0;
  PAD1_Bit = PAD2_Bit = 0;

  /*-------------------------------------------------------------------*/
  /*  Initialize PPU                                                   */
  /*-------------------------------------------------------------------*/

  InfoNES_SetupPPU();

  /*-------------------------------------------------------------------*/
  /*  Initialize pAPU                                                  */
  /*-------------------------------------------------------------------*/

  InfoNES_pAPUInit();

  /*-------------------------------------------------------------------*/
  /*  Initialize Mapper                                                */
  /*-------------------------------------------------------------------*/
  InfoNES_MessageBox("Using Mapper #%d\n", MapperNo);

  // Clear Mapper function pointers for save/load state
  MapperBlobSize = nullptr;
  MapperSaveBlob = nullptr;
  MapperLoadBlob = nullptr;
  // Only the MMC2/MMC4 CHR latch (mappers 9 and 10) needs to see sprite
  // pattern fetches; every other mapper leaves this null and pays nothing.
  MapperSprPPU = nullptr;
  // Mapper 31 in MapperTable is the synthetic NSF dispatch set up by the
  // IsNSF branch above, not the real NSF-compilation multicart mapper. A
  // .nes file whose header claims mapper 31 must still be reported as
  // unsupported rather than booting into the NSF player with an
  // uninitialised NsfHeader.
  if (MapperNo == 31 && !IsNSF)
  {
    InfoNES_Error("Mapper #%d is unsupported.", MapperNo);
    return -1;
  }

  // Get Mapper Table Index
  for (nIdx = 0; MapperTable[nIdx].nMapperNo != -1; ++nIdx)
  {
    if (MapperTable[nIdx].nMapperNo == MapperNo)
      break;
  }

  if (MapperTable[nIdx].nMapperNo == -1)
  {
    // Non support mapper
    InfoNES_Error("Mapper #%d is unsupported.", MapperNo);
    return -1;
  }

  // Set up a mapper initialization function
  MapperTable[nIdx].pMapperInit();

  // Mappers that leave MapperPPU at the Map0_PPU stub want no per-tile
  // callback at all; skip the indirect call for them (see MapperPPUActive).
  MapperPPUActive = (MapperPPU != Map0_PPU);

  /*-------------------------------------------------------------------*/
  /*  Reset CPU                                                        */
  /*-------------------------------------------------------------------*/

  K6502_Reset();

  // NSF: override CPU state after reset (A=track, X=region, SP=$FD, PC=$4100)
  if (IsNSF)
  {
    nsfSetupCpuState();
  }

  // Successful
  return 0;
}

/*===================================================================*/
/*                                                                   */
/*                InfoNES_SetupPPU() : Initialize PPU                */
/*                                                                   */
/*===================================================================*/
void InfoNES_SetupPPU()
{
  /*
   *  Initialize PPU
   *
   */
  int nPage;

  // Clear PPU and Sprite Memory
  InfoNES_MemorySet(PPURAM, 0, PPURAM_SIZE);
  InfoNES_MemorySet(SPRRAM, 0, SPRRAM_SIZE);

  // Reset PPU Register
  PPU_R0 = PPU_R1 = PPU_R2 = PPU_R3 = PPU_R7 = 0;

  // Reset latch flag
  PPU_Latch_Flag = 0;

  // Reset up and down clipping flag
  PPU_UpDown_Clip = 0;

  FrameStep = 0;
  FrameIRQ_Enable = 0;

  // Reset Scroll values
  // PPU_Scr_V = PPU_Scr_V_Next = PPU_Scr_V_Byte = PPU_Scr_V_Byte_Next = PPU_Scr_V_Bit = PPU_Scr_V_Bit_Next = 0;
  // PPU_Scr_H = PPU_Scr_H_Next = PPU_Scr_H_Byte = PPU_Scr_H_Byte_Next = PPU_Scr_H_Bit = PPU_Scr_H_Bit_Next = 0;
  // PPU_Scr_V_Byte = PPU_Scr_V_Bit = 0;
  PPU_Scr_H_Byte = PPU_Scr_H_Bit = 0;

  // Reset PPU address
  PPU_Addr = 0;
  PPU_Temp = 0;

  // Reset scanline
  PPU_Scanline = 0;

  // Reset hit position of sprite #0
  SpriteJustHit = 0;

  // Reset information on PPU_R0
  PPU_Increment = 1;
  PPU_NameTableBank = NAME_TABLE0;
  PPU_BG_Base = ChrBuf;
  PPU_SP_Base = ChrBuf + 256 * 64;
  PPU_SP_Height = 8;

  // Reset PPU banks
  for (nPage = 0; nPage < 16; ++nPage)
    PPUBANK[nPage] = &PPURAM[nPage * 0x400];

  /* Mirroring of Name Table */
  InfoNES_Mirroring(ROM_Mirroring);

  /* Reset VRAM Write Enable */
  byVramWriteEnable = (NesHeader.byVRomSize == 0) ? 1 : 0;
}

/*===================================================================*/
/*                                                                   */
/*       InfoNES_Mirroring() : Set up a Mirroring of Name Table      */
/*                                                                   */
/*===================================================================*/
void InfoNES_Mirroring(int nType)
{
  /*
   *  Set up a Mirroring of Name Table
   *
   *  Parameters
   *    int nType          (Read)
   *      Mirroring Type
   *        0 : Horizontal
   *        1 : Vertical
   *        2 : One Screen 0x2400
   *        3 : One Screen 0x2000
   *        4 : Four Screen
   *        5 : Special for Mapper #233
   */

  PPUBANK[NAME_TABLE0] = &PPURAM[PPU_MirrorTable[nType][0] * 0x400];
  PPUBANK[NAME_TABLE1] = &PPURAM[PPU_MirrorTable[nType][1] * 0x400];
  PPUBANK[NAME_TABLE2] = &PPURAM[PPU_MirrorTable[nType][2] * 0x400];
  PPUBANK[NAME_TABLE3] = &PPURAM[PPU_MirrorTable[nType][3] * 0x400];
}

/*===================================================================*/
/*                                                                   */
/*              InfoNES_Main() : The main loop of InfoNES            */
/*                                                                   */
/*===================================================================*/
namespace
{
  int s_region = INFONES_REGION_NTSC;
}

void InfoNES_SetRegion(int region)
{
  s_region = region;
  switch (region)
  {
  case INFONES_REGION_PAL:
    // PAL: 312 lines/frame at 50.007 Hz, CPU 1.662607 MHz.
    STEP_PER_SCANLINE = 107;
    STEP_PER_FRAME    = 33247;
    SCAN_VBLANK_START = 241;
    SCAN_VBLANK_END   = 311;
    break;

  case INFONES_REGION_DENDY:
    // Dendy: PAL CPU clock + 312-line frame, but NTSC-style late vblank
    // (vblank 291..311). Post-render extends from line 240 to 290.
    STEP_PER_SCANLINE = 107;
    STEP_PER_FRAME    = 33247;
    SCAN_VBLANK_START = 291;
    SCAN_VBLANK_END   = 311;
    break;

  case INFONES_REGION_NTSC:
  default:
    // NTSC: 262 lines/frame at 60.0988 Hz, CPU 1.789773 MHz.
    s_region = INFONES_REGION_NTSC;
    STEP_PER_SCANLINE = 114;
    STEP_PER_FRAME    = 29780;
    SCAN_VBLANK_START = 241;
    SCAN_VBLANK_END   = 261;
    break;
  }
}

int InfoNES_GetRegion()
{
  return s_region;
}

bool InfoNES_IsPal()
{
  return s_region != INFONES_REGION_NTSC;
}

void InfoNES_Main(int region)
{
  /*
   *  The main loop of InfoNES
   *
   */

  // Region must be set before Init() so pAPU/PPU pick up region-dependent
  // constants. Sets PPU timing now; APU rate tables and frame pacing are
  // region-aware in later steps.
  InfoNES_SetRegion(region);

  // Initialize InfoNES
  InfoNES_Init();

  // Main loop
  // while (1)
  // {
  /*-------------------------------------------------------------------*/
  /*  To the menu screen                                               */
  /*-------------------------------------------------------------------*/
  if (InfoNES_Menu() == 0)
  {
    
    /*-------------------------------------------------------------------*/
    /*  Start a NES emulation                                            */
    /*-------------------------------------------------------------------*/
    InfoNES_Cycle();
  }
  //}

  // Completion treatment
  InfoNES_Fin();
}

/*===================================================================*/
/*                                                                   */
/*              InfoNES_Cycle() : The loop of emulation              */
/*                                                                   */
/*===================================================================*/
void __not_in_flash_func(InfoNES_Cycle)()
{
  /*
   *  The loop of emulation
   *
   */

  // Set the PPU adress to the buffered value
  // if ((PPU_R1 & R1_SHOW_SP) || (PPU_R1 & R1_SHOW_SCR))
  //   PPU_Addr = PPU_Temp;

  // Emulation loop
  for (;;)
  {
    util::WorkMeterMark(MARKER_START);

    // Set a flag if a scanning line is a hit in the sprite #0
    if (SpriteJustHit == PPU_Scanline &&
        PPU_ScanTable[PPU_Scanline] == SCAN_ON_SCREEN)
    {
      // # of Steps to execute before sprite #0 hit
      int nStep = SPRRAM[SPR_X] * STEP_PER_SCANLINE / NES_DISP_WIDTH;

      // Execute instructions
      K6502_Step(nStep);

      // Set a sprite hit flag
      if ((PPU_R1 & R1_SHOW_SP) && (PPU_R1 & R1_SHOW_SCR))
        PPU_R2 |= R2_HIT_SP;

      /* Original InfoNES code below treated $2000 bit 6 (R0_NMI_SP = 0x40)
         as "fire NMI when sprite 0 hits a non-transparent BG pixel." That
         is not a real NES feature — $2000 bit 6 is PPU master/slave select
         (controls EXT pins; no-op on stock NES hardware). The original
         behavior generates a spurious NMI for any game that happens to set
         bit 6 of $2000, re-entering the NMI handler mid-frame and corrupting
         scroll/PPU state. Disabled for accuracy. Re-enable only if a
         specific game is found to depend on this non-standard behavior. */
      // NMI is required if there is necessity
      // if ((PPU_R0 & R0_NMI_SP) && (PPU_R1 & R1_SHOW_SP))
      //   NMI_REQ;

      // Execute instructions
      K6502_Step(STEP_PER_SCANLINE - nStep);
    }
    else
    {
      // Execute instructions
      K6502_Step(STEP_PER_SCANLINE);
    }

    // Frame IRQ in H-Sync
    FrameStep += STEP_PER_SCANLINE;
    if (FrameStep > STEP_PER_FRAME && FrameIRQ_Enable)
    {
      FrameStep %= STEP_PER_FRAME;
      IRQ_REQ;
      APU_Reg[0x15] |= 0x40;
    }

    util::WorkMeterMark(MARKER_CPU);

    // A mapper function in H-Sync
    MapperHSync();

#if PICO_RP2350
    fdsCheckPendingRebuild();
#endif

    // A function in H-Sync
    if (InfoNES_HSync() == -1)
      return; // To the menu screen

    // HSYNC Wait
    InfoNES_Wait();
  }
}

/*===================================================================*/
/*                                                                   */
/*              InfoNES_HSync() : A function in H-Sync               */
/*                                                                   */
/*===================================================================*/
int __not_in_flash_func(InfoNES_HSync)()
{
  /*
   *  A function in H-Sync
   *
   *  Return values
   *    0 : Normally
   *   -1 : Exit an emulation
   */

  InfoNES_pAPUHsync(!APU_Mute);
  util::WorkMeterMark(MARKER_SOUND);

  // int tmpv = (PPU_Addr >> 12) + ((PPU_Addr >> 5) << 3);
  // tmpv -= PPU_Scanline >= 240 ? 0 : PPU_Scanline;
  // PPU_Scr_V_Bit = tmpv & 7;
  // PPU_Scr_V_Byte = (tmpv >> 3) & 31;
  PPU_Scr_H_Byte = PPU_Addr & 31;
  PPU_NameTableBank = NAME_TABLE0 + ((PPU_Addr >> 10) & 3);

  /*-------------------------------------------------------------------*/
  /*  Render a scanline                                                */
  /*-------------------------------------------------------------------*/
  if (FrameCnt == 0 &&
      PPU_ScanTable[PPU_Scanline] == SCAN_ON_SCREEN)
  {
    if (PPU_Scanline >= 4 && PPU_Scanline < 240 - 4)
    {
      InfoNES_PreDrawLine(PPU_Scanline);
      /* NSF mode has no PPU work — InfoNES_PostDrawLine paints the
         NSF VU-meter overlay over the line buffer, so skipping the
         PPU pixel pipeline buys back a large slice of CPU time
         (Akumajou1.nsf and other heavy NSFs). */
      if (!IsNSF)
        InfoNES_DrawLine();
      InfoNES_PostDrawLine(PPU_Scanline);
    }
    // todo: 描画しないラインにもスプライトオーバーレジスタとかは反映する必要がある
  }

  util::WorkMeterReset(); // 計測起点はここ

  /*-------------------------------------------------------------------*/
  /*  Set new scroll values                                            */
  /*-------------------------------------------------------------------*/

  //  PPU_Scr_V = PPU_Scr_V_Next;
  // PPU_Scr_V_Byte = PPU_Scr_V_Byte_Next;
  // PPU_Scr_V_Bit = PPU_Scr_V_Bit_Next;

  //  PPU_Scr_H = PPU_Scr_H_Next;
  // PPU_Scr_H_Byte = PPU_Scr_H_Byte_Next;
  // PPU_Scr_H_Bit = PPU_Scr_H_Bit_Next;

  if ((PPU_R1 & R1_SHOW_SP) || (PPU_R1 & R1_SHOW_SCR))
  {
    if (PPU_Scanline == SCAN_VBLANK_END)
    {
      PPU_Addr = PPU_Temp;
    }
    else if (PPU_Scanline < SCAN_UNKNOWN_START)
    {
      PPU_Addr = (PPU_Addr & ~0b10000011111) |
                 (PPU_Temp & 0b10000011111);

      int v = (PPU_Addr >> 12) | ((PPU_Addr >> 2) & (31 << 3));
      if (v == 29 * 8 + 7)
      {
        v = 0;
        PPU_Addr ^= 0x800;
      }
      else if (v == 31 * 8 + 7)
      {
        v = 0;
      }
      else
        ++v;
      PPU_Addr = (PPU_Addr & ~0b111001111100000) |
                 ((v & 7) << 12) | (((v >> 3) & 31) << 5);
    }
  }

  /*-------------------------------------------------------------------*/
  /*  Next Scanline                                                    */
  /*-------------------------------------------------------------------*/
  PPU_Scanline = (PPU_Scanline == SCAN_VBLANK_END) ? 0 : PPU_Scanline + 1;

  /*-------------------------------------------------------------------*/
  /*  Operation in the specific scanning line                          */
  /*-------------------------------------------------------------------*/
  // Refactored from a switch into if/else because SCAN_VBLANK_START is now a
  // runtime value (region-dependent: 241 for NTSC/PAL, 291 for Dendy).
  if (PPU_Scanline == SCAN_TOP_OFF_SCREEN)
  {
    // Reset a PPU status
    PPU_R2 = 0;

    // Set up a character data
    if (NesHeader.byVRomSize == 0 && FrameCnt == 0)
      InfoNES_SetupChr();

    // Get position of sprite #0
    InfoNES_GetSprHitY();

    // Force a rebuild of the MMC2/MMC4 sprite-fetch trigger list even if no
    // OAM write was seen (a game may leave OAM untouched for a whole frame).
    SprLatchDirty = true;
  }
  else if (PPU_Scanline == SCAN_UNKNOWN_START)
  {
    if (FrameCnt == 0)
    {
      // Transfer the contents of work frame on the screen
      InfoNES_LoadFrame();

#if 0
        // Switching of the double buffer
        WorkFrameIdx = 1 - WorkFrameIdx;
        WorkFrame = DoubleFrame[ WorkFrameIdx ];
#endif
    }
  }
  else if (PPU_Scanline == SCAN_VBLANK_START)
  {
    // FrameCnt + 1
    FrameCnt = (FrameCnt >= FrameSkip) ? 0 : FrameCnt + 1;

    // Set a V-Blank flag
    PPU_R2 |= R2_IN_VBLANK;

    // pAPU Sound function in V-Sync
    InfoNES_pAPUVsync();

    // A mapper function in V-Sync
    MapperVSync();

    // Get the condition of the joypad
    InfoNES_PadState(&PAD1_Latch, &PAD2_Latch, &PAD_System);

    // NMI on V-Blank
    if (PPU_R0 & R0_NMI_VB)
    {
      NMI_REQ;
    }

    // Exit an emulation if a QUIT button is pushed
    if (PAD_PUSH(PAD_System, PAD_SYS_QUIT))
      return -1; // Exit an emulation
  }

  // Successful
  return 0;
}

//#pragma GCC optimize("O2")

namespace
{
  void __not_in_flash_func(compositeSprite)(const uint16_t *pal,
                                            const uint8_t *spr,
                                            uint16_t *buf)
  {
    auto sprEnd = spr + NES_DISP_WIDTH;
    do
    {
      auto proc = [=](int i) __attribute__((always_inline))
      {
        int v = spr[i];
        if (v && ((v >> 7) || (buf[i] >> 15)))
        {
          buf[i] = pal[v & 0xf];
        }
      };

#if 1
      proc(0);
      proc(1);
      proc(2);
      proc(3);
      buf += 4;
      spr += 4;
#else
      proc(0);
      buf += 1;
      spr += 1;
#endif
    } while (spr < sprEnd);
  }

  /* MMC2/MMC4 CHR latch, sprite side.

     Real PxROM/FxROM hardware flips its CHR latch on sprite pattern fetches
     as well as background fetches, and Punch-Out!! depends on it: OAM holds
     blank trigger sprites (tile $FD / $FE) whose only job is to switch the
     4K CHR window part-way down the screen. Without this the big "MIKE
     TYSON" letters below the trigger line are drawn from the wrong bank.

     Scanning all 64 OAM entries on every scanline turned out to cost more
     than it is worth (64 * 232 range tests a frame, which on RP2040 is worth
     more than the whole latch fix saves), so the trigger sprites are
     collected into a short event list and each scanline only walks that. The
     list is rebuilt lazily: SprLatchDirty is raised by the $2003/$2004/$4014
     OAM paths and by $2000 writes, and once per frame regardless, so a game
     that rewrites OAM part-way down the screen still gets fresh triggers
     while the common once-per-frame OAM DMA costs a single rebuild.

     Order matters: the PPU walks OAM forwards and the last trigger of a
     scanline wins, so the list is built in OAM order and walked in order.
     Unlike the hardware this ignores the 8-sprites-per-line fetch limit -
     a trigger that hardware would never reach because eight earlier sprites
     already filled the line would still fire here. A game relying on that
     would be broken on real hardware too, so nothing does.

     Only bits 4-13 of the address survive the mapper's mask, so within a
     tile the row only matters for picking the half of an 8x16 sprite. */
  struct SprLatchEvent
  {
    BYTE yStart; // first scanline this pattern is fetched for
    BYTE yEnd;   // one past the last
    WORD addr;   // pattern address, as the mapper hook wants it
  };
  constexpr int SPR_LATCH_MAX = 16;
  SprLatchEvent sprLatchEvents[SPR_LATCH_MAX];
  int sprLatchCount;

  // Is this tile index one of the two the MMC2/MMC4 latch reacts to?
  inline bool isLatchTile(int tile) { return tile == 0xfd || tile == 0xfe; }

  void sprLatchBuild()
  {
    SprLatchDirty = false;
    sprLatchCount = 0;

    const int spHeight = PPU_SP_Height;
    const WORD table8x8 = (PPU_R0 & R0_SP_ADDR) ? 0x1000 : 0x0000;
    const bool big = (PPU_R0 & R0_SP_SIZE) != 0;

    for (const BYTE *spr = SPRRAM; spr < SPRRAM + 64 * 4; spr += 4)
    {
      const int y = spr[SPR_Y] + 1;
      if (y + spHeight > 256)
        continue; // Never fetched

      const int ch = spr[SPR_CHR];

      if (!big)
      {
        if (!isLatchTile(ch))
          continue;
        if (sprLatchCount == SPR_LATCH_MAX)
          break;
        sprLatchEvents[sprLatchCount++] = {
            (BYTE)y, (BYTE)(y + spHeight), (WORD)(table8x8 | (ch << 4))};
        continue;
      }

      /* 8x16: the two halves are separate tiles, and a vertical flip swaps
         which half covers which scanlines. Emit an event per triggering
         half so the row range carries that distinction. */
      const int flip = (spr[SPR_ATTR] & SPR_ATTR_V_FLIP) ? 1 : 0;
      const WORD table = (WORD)((ch & 1) << 12);
      for (int half = 0; half < 2; ++half)
      {
        const int tile = (ch & 0xfe) | half;
        if (!isLatchTile(tile))
          continue;
        if (sprLatchCount == SPR_LATCH_MAX)
          break;
        const int top = y + ((half ^ flip) ? 8 : 0);
        sprLatchEvents[sprLatchCount++] = {
            (BYTE)top, (BYTE)(top + 8), (WORD)(table | (tile << 4))};
      }
    }
  }

  /* Hardware fetches the sprite patterns for line N+1 during line N's
     cycles 257-320, after that line's background fetches, so a trigger
     first takes effect on the next line - the caller passes
     PPU_Scanline + 1. */
  void __not_in_flash_func(sprLatchLine)(int line)
  {
    for (int i = 0; i < sprLatchCount; ++i)
    {
      const SprLatchEvent &e = sprLatchEvents[i];
      if (line >= e.yStart && line < e.yEnd)
        MapperSprPPU(e.addr);
    }
  }
}

/*===================================================================*/
/*                                                                   */
/*              InfoNES_DrawLine() : Render a scanline               */
/*                                                                   */
/*===================================================================*/
void __not_in_flash_func(InfoNES_DrawLine)()
{
  /*
   *  Render a scanline
   *
   */

  int nX;
  int nY;
  int nY4;
  int nYBit;
  WORD *pPalTbl;
  BYTE *pAttrBase;
  WORD *pPoint;
  int nNameTable;
  BYTE *pbyNameTable;
  BYTE *pbyChrData;
  BYTE *pSPRRAM;
  int nAttr;
  int nSprCnt;
  int nIdx;
  int nSprData;
  BYTE bySprCol;
  BYTE pSprBuf[NES_DISP_WIDTH + 7];

  /*-------------------------------------------------------------------*/
  /*  Render Background                                                */
  /*-------------------------------------------------------------------*/

  /* MMC5 VROM switch */
  MapperRenderScreen(1);

  // Pointer to the render position
  //  pPoint = &WorkFrame[PPU_Scanline * NES_DISP_WIDTH];
  assert(WorkLine);
  pPoint = WorkLine;

  // Clear a scanline if screen is off
  if (!(PPU_R1 & R1_SHOW_SCR))
  {
    InfoNES_MemorySet(pPoint, 0, NES_DISP_WIDTH << 1);
  }
  else
  {
    nNameTable = PPU_NameTableBank;

#if 0
    nY = PPU_Scr_V_Byte + (PPU_Scanline >> 3);
    nYBit = PPU_Scr_V_Bit + (PPU_Scanline & 7);

    if (nYBit > 7)
    {
      ++nY;
      nYBit &= 7;
    }
    const int yOfsModBG = nYBit;
    nYBit <<= 3;

    if (nY > 29)
    {
      // Next NameTable (An up-down direction)
      nNameTable ^= NAME_TABLE_V_MASK;
      nY -= 30;
    }
#else
    nY = (PPU_Addr >> 5) & 31;
    const int yOfsModBG = PPU_Addr >> 12;
    nYBit = yOfsModBG << 3;
#endif

    nX = PPU_Scr_H_Byte;

    nY4 = ((nY & 2) << 1);

    //
    const int patternTableIdBG = PPU_R0 & R0_BG_ADDR ? 1 : 0;
    const int bankOfsBG = patternTableIdBG << 2;
    /* PATTBL() of a background tile fetch reduces to this base OR'd with
       (tile << 4) - the tile index the renderer has already loaded. Building
       it that way saves re-reading the name table byte and rebuilding a
       ChrBuf pointer just to subtract ChrBuf off it again, ~33 times per
       scanline. Only mappers that actually watch PPU fetches (MMC2/MMC4 CHR
       latch, mapper 96) pay the call at all - see MapperPPUActive. */
    const int bgPatBase = (patternTableIdBG << 12) | (yOfsModBG << 1);

    /*-------------------------------------------------------------------*/
    /*  Rendering of the block of the left end                           */
    /*-------------------------------------------------------------------*/

    pbyNameTable = PPUBANK[nNameTable] + nY * 32 + nX;
    pbyChrData = PPU_BG_Base + (*pbyNameTable << 6) + nYBit;
    pAttrBase = PPUBANK[nNameTable] + 0x3c0 + (nY / 4) * 8;
#if 0
    pPalTbl = &PalTable[(((pAttrBase[nX >> 2] >> ((nX & 2) + nY4)) & 3) << 2)];

    for (nIdx = PPU_Scr_H_Bit; nIdx < 8; ++nIdx)
    {
      *(pPoint++) = pPalTbl[pbyChrData[nIdx]];
    }
#else
    {
      pPoint += 8 - PPU_Scr_H_Bit;

      const int ch = *pbyNameTable;
#if PICO_RP2350
      const WORD *pal;
      const BYTE *data;
      if (Map5_Gfx_Mode == 1) {
        const BYTE exram = Map5_Ex_Vram[nY * 32 + nX];
        pal = &PalTable[((exram >> 6) & 3) << 2];
        const int chrBank4K = ((int)Map5_Chr_Upper << 6) | (exram & 0x3F);
        const int vromPage = (chrBank4K * 4 + (ch >> 6)) % (NesHeader.byVRomSize << 3);
        data = VROMPAGE(vromPage) + ((ch & 63) << 4) + yOfsModBG;
      } else {
        pal = &PalTable[(((pAttrBase[nX >> 2] >> ((nX & 2) + nY4)) & 3) << 2)];
        data = PPUBANK[(ch >> 6) + bankOfsBG] + ((ch & 63) << 4) + yOfsModBG;
      }
#else
      const auto pal = &PalTable[(((pAttrBase[nX >> 2] >> ((nX & 2) + nY4)) & 3) << 2)];
      const auto data = PPUBANK[(ch >> 6) + bankOfsBG] + ((ch & 63) << 4) + yOfsModBG;
#endif
      const auto pl0 = data[0];
      const auto pl1 = data[8];
      const auto pat0 = (pl0 & 0x55) | ((pl1 << 1) & 0xaa);
      const auto pat1 = ((pl0 >> 1) & 0x55) | (pl1 & 0xaa);
      switch (PPU_Scr_H_Bit)
      {
      case 0:
        pPoint[-8] = pal[(pat1 >> 6) & 3];
      case 1:
        pPoint[-7] = pal[(pat0 >> 6) & 3];
      case 2:
        pPoint[-6] = pal[(pat1 >> 4) & 3];
      case 3:
        pPoint[-5] = pal[(pat0 >> 4) & 3];
      case 4:
        pPoint[-4] = pal[(pat1 >> 2) & 3];
      case 5:
        pPoint[-3] = pal[(pat0 >> 2) & 3];
      case 6:
        pPoint[-2] = pal[(pat1 >> 0) & 3];
      case 7:
        pPoint[-1] = pal[(pat0 >> 0) & 3];
      default:
        break;
      }
    }
#endif

    // Callback at PPU read/write
    if (MapperPPUActive)
      MapperPPU(bgPatBase | (*pbyNameTable << 4));

    ++nX;
    ++pbyNameTable;

    /*-------------------------------------------------------------------*/
    /*  Rendering of the left table                                      */
    /*-------------------------------------------------------------------*/

    auto putBG = [&](int nX) __attribute__((always_inline))
    {
      const int ch = *pbyNameTable;
#if PICO_RP2350
      const WORD *pal;
      const BYTE *data;
      if (Map5_Gfx_Mode == 1) {
        const BYTE exram = Map5_Ex_Vram[nY * 32 + nX];
        pal = &PalTable[((exram >> 6) & 3) << 2];
        const int chrBank4K = ((int)Map5_Chr_Upper << 6) | (exram & 0x3F);
        const int vromPage = (chrBank4K * 4 + (ch >> 6)) % (NesHeader.byVRomSize << 3);
        data = VROMPAGE(vromPage) + ((ch & 63) << 4) + yOfsModBG;
      } else {
        pal = &PalTable[(((pAttrBase[nX >> 2] >> ((nX & 2) + nY4)) & 3) << 2)];
        data = PPUBANK[(ch >> 6) + bankOfsBG] + ((ch & 63) << 4) + yOfsModBG;
      }
#else
      const auto pal = &PalTable[(((pAttrBase[nX >> 2] >> ((nX & 2) + nY4)) & 3) << 2)];
      const int bank = (ch >> 6) + bankOfsBG;
      const int addrOfs = ((ch & 63) << 4) + yOfsModBG;
      const auto data = PPUBANK[bank] + addrOfs;
#endif
      const auto palAddr = reinterpret_cast<uintptr_t>(pal);
      const auto pl0 = data[0];
      const auto pl1 = data[8];
      // const auto pat0 = (pl0 & 0x55) | ((pl1 << 1) & 0xaa);
      // const auto pat1 = ((pl0 >> 1) & 0x55) | (pl1 & 0xaa);
      const auto pat0 = ((pl0 & 0x55) << 1) | ((pl1 & 0x55) << 2);
      const auto pat1 = ((pl0 & 0xaa) << 0) | ((pl1 & 0xaa) << 1);

      auto readPal = [&](int ofs)
      {
        return *reinterpret_cast<const WORD *>(palAddr + ofs);
      };
      pPoint[0] = readPal((pat1 >> 6) & 6);
      pPoint[1] = readPal((pat0 >> 6) & 6);
      pPoint[2] = readPal((pat1 >> 4) & 6);
      pPoint[3] = readPal((pat0 >> 4) & 6);
      pPoint[4] = readPal((pat1 >> 2) & 6);
      pPoint[5] = readPal((pat0 >> 2) & 6);
      pPoint[6] = readPal((pat1 >> 0) & 6);
      pPoint[7] = readPal((pat0 >> 0) & 6);
      pPoint += 8;
      return ch;
    };

    for (; nX < 32; ++nX)
    {
#if 0
      pbyChrData = PPU_BG_Base + (*pbyNameTable << 6) + nYBit;
      pPalTbl = &PalTable[(((pAttrBase[nX >> 2] >> ((nX & 2) + nY4)) & 3) << 2)];

      pPoint[0] = pPalTbl[pbyChrData[0]];
      pPoint[1] = pPalTbl[pbyChrData[1]];
      pPoint[2] = pPalTbl[pbyChrData[2]];
      pPoint[3] = pPalTbl[pbyChrData[3]];
      pPoint[4] = pPalTbl[pbyChrData[4]];
      pPoint[5] = pPalTbl[pbyChrData[5]];
      pPoint[6] = pPalTbl[pbyChrData[6]];
      pPoint[7] = pPalTbl[pbyChrData[7]];
      pPoint += 8;
#else
      const int chBG = putBG(nX);
#endif

      // Callback at PPU read/write
      if (MapperPPUActive)
        MapperPPU(bgPatBase | (chBG << 4));

      ++pbyNameTable;
    }

    // Holizontal Mirror
    nNameTable ^= NAME_TABLE_H_MASK;

    pbyNameTable = PPUBANK[nNameTable] + nY * 32;
    pAttrBase = PPUBANK[nNameTable] + 0x3c0 + (nY / 4) * 8;

    /*-------------------------------------------------------------------*/
    /*  Rendering of the right table                                     */
    /*-------------------------------------------------------------------*/

    for (nX = 0; nX < PPU_Scr_H_Byte; ++nX)
    {
#if 0
      pbyChrData = PPU_BG_Base + (*pbyNameTable << 6) + nYBit;
      pPalTbl = &PalTable[(((pAttrBase[nX >> 2] >> ((nX & 2) + nY4)) & 3) << 2)];

      pPoint[0] = pPalTbl[pbyChrData[0]];
      pPoint[1] = pPalTbl[pbyChrData[1]];
      pPoint[2] = pPalTbl[pbyChrData[2]];
      pPoint[3] = pPalTbl[pbyChrData[3]];
      pPoint[4] = pPalTbl[pbyChrData[4]];
      pPoint[5] = pPalTbl[pbyChrData[5]];
      pPoint[6] = pPalTbl[pbyChrData[6]];
      pPoint[7] = pPalTbl[pbyChrData[7]];
      pPoint += 8;
#else
      const int chBG = putBG(nX);
#endif

      // Callback at PPU read/write
      if (MapperPPUActive)
        MapperPPU(bgPatBase | (chBG << 4));

      ++pbyNameTable;
    }

    /*-------------------------------------------------------------------*/
    /*  Rendering of the block of the right end                          */
    /*-------------------------------------------------------------------*/

#if 0
    pbyChrData = PPU_BG_Base + (*pbyNameTable << 6) + nYBit;
    pPalTbl = &PalTable[(((pAttrBase[nX >> 2] >> ((nX & 2) + nY4)) & 3) << 2)];
    for (nIdx = 0; nIdx < PPU_Scr_H_Bit; ++nIdx)
    {
      pPoint[nIdx] = pPalTbl[pbyChrData[nIdx]];
    }
#else
    {
      const int ch = *pbyNameTable;
#if PICO_RP2350
      const WORD *pal;
      const BYTE *data;
      if (Map5_Gfx_Mode == 1) {
        const BYTE exram = Map5_Ex_Vram[nY * 32 + nX];
        pal = &PalTable[((exram >> 6) & 3) << 2];
        const int chrBank4K = ((int)Map5_Chr_Upper << 6) | (exram & 0x3F);
        const int vromPage = (chrBank4K * 4 + (ch >> 6)) % (NesHeader.byVRomSize << 3);
        data = VROMPAGE(vromPage) + ((ch & 63) << 4) + yOfsModBG;
      } else {
        pal = &PalTable[(((pAttrBase[nX >> 2] >> ((nX & 2) + nY4)) & 3) << 2)];
        data = PPUBANK[(ch >> 6) + bankOfsBG] + ((ch & 63) << 4) + yOfsModBG;
      }
#else
      const auto pal = &PalTable[(((pAttrBase[nX >> 2] >> ((nX & 2) + nY4)) & 3) << 2)];
      const auto data = PPUBANK[(ch >> 6) + bankOfsBG] + ((ch & 63) << 4) + yOfsModBG;
#endif
      const auto pl0 = data[0];
      const auto pl1 = data[8];
      const auto pat0 = (pl0 & 0x55) | ((pl1 << 1) & 0xaa);
      const auto pat1 = ((pl0 >> 1) & 0x55) | (pl1 & 0xaa);
      //      const auto [pat0, pat1] = getPatBG(ch);
      switch (PPU_Scr_H_Bit)
      {
      case 8:
        pPoint[7] = pal[(pat0 >> 0) & 3];
      case 7:
        pPoint[6] = pal[(pat1 >> 0) & 3];
      case 6:
        pPoint[5] = pal[(pat0 >> 2) & 3];
      case 5:
        pPoint[4] = pal[(pat1 >> 2) & 3];
      case 4:
        pPoint[3] = pal[(pat0 >> 4) & 3];
      case 3:
        pPoint[2] = pal[(pat1 >> 4) & 3];
      case 2:
        pPoint[1] = pal[(pat0 >> 6) & 3];
      case 1:
        pPoint[0] = pal[(pat1 >> 6) & 3];
      default:
        break;
      }

      //      pPoint += PPU_Scr_H_Bit;
    }
#endif

    // Callback at PPU read/write
    if (MapperPPUActive)
      MapperPPU(bgPatBase | (*pbyNameTable << 4));

    /*-------------------------------------------------------------------*/
    /*  Backgroud Clipping                                               */
    /*-------------------------------------------------------------------*/
    if (!(PPU_R1 & R1_CLIP_BG))
    {
      WORD *pPointTop;

      // pPointTop = &WorkFrame[PPU_Scanline * NES_DISP_WIDTH];
      pPointTop = WorkLine;
      InfoNES_MemorySet(pPointTop, 0, 8 << 1);
    }

    /*-------------------------------------------------------------------*/
    /*  Clear a scanline if up and down clipping flag is set             */
    /*-------------------------------------------------------------------*/
    if (PPU_UpDown_Clip &&
        (SCAN_ON_SCREEN_START > PPU_Scanline || PPU_Scanline > SCAN_BOTTOM_OFF_SCREEN_START))
    {
      WORD *pPointTop;

      // pPointTop = &WorkFrame[PPU_Scanline * NES_DISP_WIDTH];
      pPointTop = WorkLine;
      InfoNES_MemorySet(pPointTop, 0, NES_DISP_WIDTH << 1);
    }
  }

  util::WorkMeterMark(MARKER_BG);

  /*-------------------------------------------------------------------*/
  /*  Render a sprite                                                  */
  /*-------------------------------------------------------------------*/

  /* MMC5 VROM switch */
  MapperRenderScreen(0);

  if (PPU_R1 & R1_SHOW_SP)
  {
    // Reset Scanline Sprite Count
    PPU_R2 &= ~R2_MAX_SP;

    // Reset sprite buffer
    InfoNES_MemorySet(pSprBuf, 0, sizeof pSprBuf);

    const int patternTableIdSP88 = PPU_R0 & R0_SP_ADDR ? 1 : 0;
    const int bankOfsSP88 = patternTableIdSP88 << 2;

    // Render a sprite to the sprite buffer
    nSprCnt = 0;
    for (pSPRRAM = SPRRAM + (63 << 2); pSPRRAM >= SPRRAM; pSPRRAM -= 4)
    {
      nY = pSPRRAM[SPR_Y] + 1;
      if (nY > PPU_Scanline || nY + PPU_SP_Height <= PPU_Scanline)
        continue; // Next sprite

      /*-------------------------------------------------------------------*/
      /*  A sprite in scanning line                                        */
      /*-------------------------------------------------------------------*/

      // Holizontal Sprite Count +1
      ++nSprCnt;

      nAttr = pSPRRAM[SPR_ATTR];
      nYBit = PPU_Scanline - nY;
      nYBit = (nAttr & SPR_ATTR_V_FLIP) ? (PPU_SP_Height - nYBit - 1) : nYBit;
      const int yOfsModSP = nYBit;
      nYBit <<= 3;

#if 0
      if (PPU_R0 & R0_SP_SIZE)
      {
        // Sprite size 8x16
        if (pSPRRAM[SPR_CHR] & 1)
        {
          pbyChrData = ChrBuf + 256 * 64 + ((pSPRRAM[SPR_CHR] & 0xfe) << 6) + nYBit;
        }
        else
        {
          pbyChrData = ChrBuf + ((pSPRRAM[SPR_CHR] & 0xfe) << 6) + nYBit;
        }
      }
      else
      {
        // Sprite size 8x8
        pbyChrData = PPU_SP_Base + (pSPRRAM[SPR_CHR] << 6) + nYBit;
      }

      nAttr ^= SPR_ATTR_PRI;
      bySprCol = (nAttr & (SPR_ATTR_COLOR | SPR_ATTR_PRI)) << 2;
      nX = pSPRRAM[SPR_X];

      if (nAttr & SPR_ATTR_H_FLIP)
      {
        // Horizontal flip
        if (pbyChrData[7])
          pSprBuf[nX] = bySprCol | pbyChrData[7];
        if (pbyChrData[6])
          pSprBuf[nX + 1] = bySprCol | pbyChrData[6];
        if (pbyChrData[5])
          pSprBuf[nX + 2] = bySprCol | pbyChrData[5];
        if (pbyChrData[4])
          pSprBuf[nX + 3] = bySprCol | pbyChrData[4];
        if (pbyChrData[3])
          pSprBuf[nX + 4] = bySprCol | pbyChrData[3];
        if (pbyChrData[2])
          pSprBuf[nX + 5] = bySprCol | pbyChrData[2];
        if (pbyChrData[1])
          pSprBuf[nX + 6] = bySprCol | pbyChrData[1];
        if (pbyChrData[0])
          pSprBuf[nX + 7] = bySprCol | pbyChrData[0];
      }
      else
      {
        // Non flip
        if (pbyChrData[0])
          pSprBuf[nX] = bySprCol | pbyChrData[0];
        if (pbyChrData[1])
          pSprBuf[nX + 1] = bySprCol | pbyChrData[1];
        if (pbyChrData[2])
          pSprBuf[nX + 2] = bySprCol | pbyChrData[2];
        if (pbyChrData[3])
          pSprBuf[nX + 3] = bySprCol | pbyChrData[3];
        if (pbyChrData[4])
          pSprBuf[nX + 4] = bySprCol | pbyChrData[4];
        if (pbyChrData[5])
          pSprBuf[nX + 5] = bySprCol | pbyChrData[5];
        if (pbyChrData[6])
          pSprBuf[nX + 6] = bySprCol | pbyChrData[6];
        if (pbyChrData[7])
          pSprBuf[nX + 7] = bySprCol | pbyChrData[7];
      }
#else
      int ch = pSPRRAM[SPR_CHR];

      int bankOfs;
      if (PPU_R0 & R0_SP_SIZE)
      {
        // 8x16
        bankOfs = (ch & 1) << 2;
        ch &= 0xfe;
      }
      else
      {
        // 8x8
        bankOfs = bankOfsSP88;
      }

      const int bank = (ch >> 6) + bankOfs;
      const int addrOfs = ((ch & 63) << 4) + ((yOfsModSP & 8) << 1) + (yOfsModSP & 7);
      const auto data = PPUBANK[bank] + addrOfs;
      const uint32_t pl0 = data[0];
      const uint32_t pl1 = data[8];
      const auto pat0 = ((pl0 & 0x55) << 24) | ((pl1 & 0x55) << 25);
      const auto pat1 = ((pl0 & 0xaa) << 23) | ((pl1 & 0xaa) << 24);

      nAttr ^= SPR_ATTR_PRI;
      bySprCol = (nAttr & (SPR_ATTR_COLOR | SPR_ATTR_PRI)) << 2;
      nX = pSPRRAM[SPR_X];
      const auto dst = pSprBuf + nX;

      if (nAttr & SPR_ATTR_H_FLIP)
      {
        // h flip
        if (int v = (pat1 << 0) >> 30)
        {
          dst[7] = bySprCol | v;
        }
        if (int v = (pat0 << 0) >> 30)
        {
          dst[6] = bySprCol | v;
        }
        if (int v = (pat1 << 2) >> 30)
        {
          dst[5] = bySprCol | v;
        }
        if (int v = (pat0 << 2) >> 30)
        {
          dst[4] = bySprCol | v;
        }
        if (int v = (pat1 << 4) >> 30)
        {
          dst[3] = bySprCol | v;
        }
        if (int v = (pat0 << 4) >> 30)
        {
          dst[2] = bySprCol | v;
        }
        if (int v = (pat1 << 6) >> 30)
        {
          dst[1] = bySprCol | v;
        }
        if (int v = (pat0 << 6) >> 30)
        {
          dst[0] = bySprCol | v;
        }
      }
      else
      {
        // non flip
        if (int v = (pat1 << 0) >> 30)
        {
          dst[0] = bySprCol | v;
        }
        if (int v = (pat0 << 0) >> 30)
        {
          dst[1] = bySprCol | v;
        }
        if (int v = (pat1 << 2) >> 30)
        {
          dst[2] = bySprCol | v;
        }
        if (int v = (pat0 << 2) >> 30)
        {
          dst[3] = bySprCol | v;
        }
        if (int v = (pat1 << 4) >> 30)
        {
          dst[4] = bySprCol | v;
        }
        if (int v = (pat0 << 4) >> 30)
        {
          dst[5] = bySprCol | v;
        }
        if (int v = (pat1 << 6) >> 30)
        {
          dst[6] = bySprCol | v;
        }
        if (int v = (pat0 << 6) >> 30)
        {
          dst[7] = bySprCol | v;
        }
      }
#endif
    }

    // Rendering sprite
    pPoint = WorkLine;
    //   pPoint -= (NES_DISP_WIDTH - PPU_Scr_H_Bit);

#if 1
    compositeSprite(PalTable + 0x10, pSprBuf, pPoint);
#else
    {
      const auto *pal = &PalTable[0x10];
      const auto *spr = pSprBuf;
      const auto *sprEnd = spr + NES_DISP_WIDTH;
      // for (nX = 0; nX < NES_DISP_WIDTH; ++nX)
      while (spr != sprEnd)
      {
        // nSprData = pSprBuf[nX];
        auto proc = [=](int i) __attribute__((always_inline))
        {
          int v = spr[i];
          if (v && ((v >> 7) || (pPoint[i] >> 15)))
          {
            pPoint[i] = pal[v & 0xf];
          }
        };

#if 1
        proc(0);
        proc(1);
        proc(2);
        proc(3);
        pPoint += 4;
        spr += 4;
#else
        proc(0);
        pPoint += 1;
        spr += 1;
#endif
      }
    }
#endif

    /*-------------------------------------------------------------------*/
    /*  Sprite Clipping                                                  */
    /*-------------------------------------------------------------------*/
    if (!(PPU_R1 & R1_CLIP_SP))
    {
      WORD *pPointTop;

      // pPointTop = &WorkFrame[PPU_Scanline * NES_DISP_WIDTH];
      pPointTop = WorkLine;
      InfoNES_MemorySet(pPointTop, 0, 8 << 1);
    }

    if (nSprCnt >= 8)
      PPU_R2 |= R2_MAX_SP; // Set a flag of maximum sprites on scanline

    util::WorkMeterMark(MARKER_SPRITE);
  }

  /* MMC2/MMC4 CHR latch, sprite side. See sprLatchLine above. */
  if (MapperSprPPU)
  {
    if (SprLatchDirty)
      sprLatchBuild();
    if (sprLatchCount && (PPU_R1 & (R1_SHOW_SP | R1_SHOW_SCR)))
      sprLatchLine(PPU_Scanline + 1);
  }
}

/*===================================================================*/
/*                                                                   */
/* InfoNES_GetSprHitY() : Get a position of scanline hits sprite #0  */
/*                                                                   */
/*===================================================================*/
void __not_in_flash_func(InfoNES_GetSprHitY)()
{
  /*
   * Get a position of scanline hits sprite #0
   *
   */

#if 0
  int nYBit;
  DWORD *pdwChrData;
  int nOff;

  if (SPRRAM[SPR_ATTR] & SPR_ATTR_V_FLIP)
  {
    // Vertical flip
    nYBit = (PPU_SP_Height - 1) << 3;
    nOff = -2;
  }
  else
  {
    // Non flip
    nYBit = 0;
    nOff = 2;
  }

  if (PPU_R0 & R0_SP_SIZE)
  {
    // Sprite size 8x16
    if (SPRRAM[SPR_CHR] & 1)
    {
      pdwChrData = (DWORD *)(ChrBuf + 256 * 64 + ((SPRRAM[SPR_CHR] & 0xfe) << 6) + nYBit);
    }
    else
    {
      pdwChrData = (DWORD *)(ChrBuf + ((SPRRAM[SPR_CHR] & 0xfe) << 6) + nYBit);
    }
  }
  else
  {
    // Sprite size 8x8
    pdwChrData = (DWORD *)(PPU_SP_Base + (SPRRAM[SPR_CHR] << 6) + nYBit);
  }

  if ((SPRRAM[SPR_Y] + 1 <= SCAN_UNKNOWN_START) && (SPRRAM[SPR_Y] > 0))
  {
    for (int nLine = 0; nLine < PPU_SP_Height; nLine++)
    {
      if (pdwChrData[0] | pdwChrData[1])
      {
        // Scanline hits sprite #0
        SpriteJustHit = SPRRAM[SPR_Y] + 1 + nLine;
        nLine = SCAN_VBLANK_END;
      }
      pdwChrData += nOff;
    }
  }
  else
  {
    // Scanline didn't hit sprite #0
    SpriteJustHit = SCAN_UNKNOWN_START + 1;
  }
#else
  const int patternTableIdSP88 = PPU_R0 & R0_SP_ADDR ? 1 : 0;
  const int bankOfsSP88 = patternTableIdSP88 << 2;

  int yOfsMod;
  int stride;
  if (SPRRAM[SPR_ATTR] & SPR_ATTR_V_FLIP)
  {
    // Vertical flip
    yOfsMod = PPU_SP_Height - 1;
    stride = -1;
  }
  else
  {
    // Non flip
    yOfsMod = 0;
    stride = 1;
  }

  int ch = SPRRAM[SPR_CHR];

  int bankOfs;
  if (PPU_R0 & R0_SP_SIZE)
  {
    // 8x16
    bankOfs = (ch & 1) << 2;
    ch &= 0xfe;
  }
  else
  {
    // 8x8
    bankOfs = bankOfsSP88;
  }

  const int bank = (ch >> 6) + bankOfs;
  const int addrOfs = ((ch & 63) << 4) + ((yOfsMod & 8) << 1) + (yOfsMod & 7);

  auto *data = PPUBANK[bank] + addrOfs;

  if ((SPRRAM[SPR_Y] + 1 <= SCAN_UNKNOWN_START) && (SPRRAM[SPR_Y] > 0))
  {
    for (int nLine = 0; nLine < PPU_SP_Height; nLine++)
    {
      if (data[0] | data[8])
      {
        // Scanline hits sprite #0
        SpriteJustHit = SPRRAM[SPR_Y] + 1 + nLine;
        nLine = SCAN_VBLANK_END;
      }
      data += stride;
    }
  }
  else
  {
    // Scanline didn't hit sprite #0
    SpriteJustHit = SCAN_UNKNOWN_START + 1;
  }

#endif
}

/*===================================================================*/
/*                                                                   */
/*            InfoNES_SetupChr() : Develop character data            */
/*                                                                   */
/*===================================================================*/
void __not_in_flash_func(InfoNES_SetupChr)()
{
  /*
   *  Develop character data
   *
   */

#if 0
  BYTE *pbyBGData;
  BYTE byData1;
  BYTE byData2;
  int nIdx;
  int nY;
  int nOff;
  static BYTE *pbyPrevBank[8];
  int nBank;

  for (nBank = 0; nBank < 8; ++nBank)
  {
    if (pbyPrevBank[nBank] == PPUBANK[nBank] && !((ChrBufUpdate >> nBank) & 1))
      continue; // Next bank

    /*-------------------------------------------------------------------*/
    /*  An address is different from the last time                       */
    /*    or                                                             */
    /*  An update flag is being set                                      */
    /*-------------------------------------------------------------------*/

    for (nIdx = 0; nIdx < 64; ++nIdx)
    {
      nOff = (nBank << 12) + (nIdx << 6);

      for (nY = 0; nY < 8; ++nY)
      {
        pbyBGData = PPUBANK[nBank] + (nIdx << 4) + nY;

        byData1 = ((pbyBGData[0] >> 1) & 0x55) | (pbyBGData[8] & 0xAA);
        byData2 = (pbyBGData[0] & 0x55) | ((pbyBGData[8] << 1) & 0xAA);

        ChrBuf[nOff] = (byData1 >> 6) & 3;
        ChrBuf[nOff + 1] = (byData2 >> 6) & 3;
        ChrBuf[nOff + 2] = (byData1 >> 4) & 3;
        ChrBuf[nOff + 3] = (byData2 >> 4) & 3;
        ChrBuf[nOff + 4] = (byData1 >> 2) & 3;
        ChrBuf[nOff + 5] = (byData2 >> 2) & 3;
        ChrBuf[nOff + 6] = byData1 & 3;
        ChrBuf[nOff + 7] = byData2 & 3;

        nOff += 8;
      }
    }
    // Keep this address
    pbyPrevBank[nBank] = PPUBANK[nBank];
  }

  // Reset update flag
  ChrBufUpdate = 0;
#endif
}
