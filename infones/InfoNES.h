/*===================================================================*/
/*                                                                   */
/*  InfoNES.h : NES Emulator for Win32, Linux(x86), Linux(PS2)       */
/*                                                                   */
/*  2000/05/14  InfoNES Project ( based on pNesX )                   */
/*                                                                   */
/*===================================================================*/

#ifndef InfoNES_H_INCLUDED
#define InfoNES_H_INCLUDED

/*-------------------------------------------------------------------*/
/*  Include files                                                    */
/*-------------------------------------------------------------------*/

#include "InfoNES_Types.h"
#include <cstddef>
/*-------------------------------------------------------------------*/
/*  NES resources                                                    */
/*-------------------------------------------------------------------*/

#define RAM_SIZE 0x2000
#define SRAM_SIZE 0x2000
#define PPURAM_SIZE 0x4000
#define SPRRAM_SIZE 256
#define CHRBUF_SIZE 256 * 2 * 8 * 8

/* RAM */
extern BYTE *RAM;
// extern BYTE *RAM;
/* SRAM */
extern BYTE *SRAM;

extern bool SRAMwritten;

/* ROM */
extern BYTE *ROM;

/* SRAM BANK ( 8Kb ) */
extern BYTE *SRAMBANK;

/* ROM BANK ( 8Kb * 4 ) */
extern BYTE *ROMBANK[4];
// extern BYTE *ROMBANK0;
// extern BYTE *ROMBANK1;
// extern BYTE *ROMBANK2;
// extern BYTE *ROMBANK3;
#define ROMBANK0 (ROMBANK[0])
#define ROMBANK1 (ROMBANK[1])
#define ROMBANK2 (ROMBANK[2])
#define ROMBANK3 (ROMBANK[3])

/*-------------------------------------------------------------------*/
/*  PPU resources                                                    */
/*-------------------------------------------------------------------*/

/* PPU RAM */
extern BYTE *PPURAM;
//extern BYTE *PPURAM;
/* VROM */
extern BYTE *VROM;

/* PPU BANK ( 1Kb * 16 ) */
extern BYTE *PPUBANK[];

#define NAME_TABLE0 8
#define NAME_TABLE1 9
#define NAME_TABLE2 10
#define NAME_TABLE3 11

#define NAME_TABLE_V_MASK 2
#define NAME_TABLE_H_MASK 1

/* Sprite RAM */
extern BYTE *SPRRAM;

#define SPR_Y 0
#define SPR_CHR 1
#define SPR_ATTR 2
#define SPR_X 3
#define SPR_ATTR_COLOR 0x3
#define SPR_ATTR_V_FLIP 0x80
#define SPR_ATTR_H_FLIP 0x40
#define SPR_ATTR_PRI 0x20

/* PPU Register */
extern BYTE PPU_R0;
extern BYTE PPU_R1;
extern BYTE PPU_R2;
extern BYTE PPU_R3;
extern BYTE PPU_R7;

//extern BYTE PPU_Scr_V;
//extern BYTE PPU_Scr_V_Next;
//extern BYTE PPU_Scr_V_Byte;
//extern BYTE PPU_Scr_V_Byte_Next;
//extern BYTE PPU_Scr_V_Bit;
//extern BYTE PPU_Scr_V_Bit_Next;

//extern BYTE PPU_Scr_H;
//extern BYTE PPU_Scr_H_Next;
extern BYTE PPU_Scr_H_Byte;
//extern BYTE PPU_Scr_H_Byte_Next;
extern BYTE PPU_Scr_H_Bit;
//extern BYTE PPU_Scr_H_Bit_Next;

extern BYTE PPU_Latch_Flag;
extern WORD PPU_Addr;
extern WORD PPU_Temp;
extern WORD PPU_Increment;

/* Set when a $2006 second write lands mid-frame with rendering on, i.e. the
   game moved the PPU address to pick a different row for the line about to be
   drawn. See InfoNES_HSync(). */
extern BYTE PPU_MidFrameAddrWrite;

extern BYTE PPU_Latch_Flag;
extern BYTE PPU_UpDown_Clip;

#define R0_NMI_VB 0x80
#define R0_NMI_SP 0x40
#define R0_SP_SIZE 0x20
#define R0_BG_ADDR 0x10
#define R0_SP_ADDR 0x08
#define R0_INC_ADDR 0x04
#define R0_NAME_ADDR 0x03

#define R1_BACKCOLOR 0xe0
#define R1_SHOW_SP 0x10
#define R1_SHOW_SCR 0x08
#define R1_CLIP_SP 0x04
#define R1_CLIP_BG 0x02
#define R1_MONOCHROME 0x01

#define R2_IN_VBLANK 0x80
#define R2_HIT_SP 0x40
#define R2_MAX_SP 0x20
#define R2_WRITE_FLAG 0x10

#define SCAN_TOP_OFF_SCREEN 0
#define SCAN_ON_SCREEN 1
#define SCAN_BOTTOM_OFF_SCREEN 2
#define SCAN_UNKNOWN 3
#define SCAN_VBLANK 4

#define SCAN_TOP_OFF_SCREEN_START 0
#define SCAN_ON_SCREEN_START 8
#define SCAN_BOTTOM_OFF_SCREEN_START 232
#define SCAN_UNKNOWN_START 240
// Region-dependent PPU/CPU timing. Set by InfoNES_SetRegion():
//   NTSC : STEP_PER_SCANLINE=114, STEP_PER_FRAME=29780,
//          SCAN_VBLANK_START=241, SCAN_VBLANK_END=261  (262 lines @ 60.0988 Hz)
//   PAL  : STEP_PER_SCANLINE=107, STEP_PER_FRAME=33247,
//          SCAN_VBLANK_START=241, SCAN_VBLANK_END=311  (312 lines @ 50.007 Hz)
//   Dendy: STEP_PER_SCANLINE=107, STEP_PER_FRAME=33247,
//          SCAN_VBLANK_START=291, SCAN_VBLANK_END=311  (312 lines @ 50 Hz,
//          NTSC-style 21-line vblank — PAL CPU clock with later vblank start)
extern WORD STEP_PER_SCANLINE;
extern WORD STEP_PER_FRAME;
extern WORD SCAN_VBLANK_START;
extern WORD SCAN_VBLANK_END;

/* Develop Scroll Registers */
#if 0
#define InfoNES_SetupScr()                             \
  {                                                    \
    /* V-Scroll Register */                            \
    PPU_Scr_V_Next = (BYTE)(PPU_Addr & 0x001f);        \
    PPU_Scr_V_Byte_Next = PPU_Scr_V_Next >> 3;         \
    PPU_Scr_V_Bit_Next = PPU_Scr_V_Next & 0x07;        \
                                                       \
    /* H-Scroll Register */                            \
    PPU_Scr_H_Next = (BYTE)((PPU_Addr & 0x03e0) >> 5); \
    PPU_Scr_H_Byte_Next = PPU_Scr_H_Next >> 3;         \
    PPU_Scr_H_Bit_Next = PPU_Scr_H_Next & 0x07;        \
  }
#else
#define InfoNES_SetupScr()
#endif

/* Current Scanline */
extern WORD PPU_Scanline;

/* Scanline Table */
extern BYTE PPU_ScanTable[];

/* Name Table Bank */
extern BYTE PPU_NameTableBank;

/* BG Base Address */
extern BYTE *PPU_BG_Base;

/* Sprite Base Address */
extern BYTE *PPU_SP_Base;

/* Sprite Height */
extern WORD PPU_SP_Height;

/* NES display size */
#define NES_DISP_WIDTH 256
#define NES_DISP_HEIGHT 240

/* VRAM Write Enable ( 0: Disable, 1: Enable ) */
extern BYTE byVramWriteEnable;

/* Frame IRQ ( 0: Disabled, 1: Enabled )*/
extern BYTE FrameIRQ_Enable;
extern WORD FrameStep;

/*-------------------------------------------------------------------*/
/*  Display and Others resouces                                      */
/*-------------------------------------------------------------------*/

/* Frame Skip */
extern WORD FrameSkip;
extern WORD FrameCnt;
extern WORD FrameWait;

#if 0
extern WORD DoubleFrame[ 2 ][ NES_DISP_WIDTH * NES_DISP_HEIGHT ];
extern WORD *WorkFrame;
extern WORD WorkFrameIdx;
#else
// FHextern WORD WorkFrame[NES_DISP_WIDTH * NES_DISP_HEIGHT];
#endif

extern BYTE *ChrBuf;


extern BYTE ChrBufUpdate;

extern WORD PalTable[];

/*-------------------------------------------------------------------*/
/*  APU and Pad resources                                            */
/*-------------------------------------------------------------------*/

extern BYTE APU_Reg[];
extern int APU_Mute;

extern DWORD PAD1_Latch;
extern DWORD PAD2_Latch;
extern DWORD PAD_System;
extern DWORD PAD1_Bit;
extern DWORD PAD2_Bit;

#define PAD_SYS_QUIT 1
#define PAD_SYS_OK 2
#define PAD_SYS_CANCEL 4
#define PAD_SYS_UP 8
#define PAD_SYS_DOWN 0x10
#define PAD_SYS_LEFT 0x20
#define PAD_SYS_RIGHT 0x40

#define PAD_PUSH(a, b) (((a) & (b)) != 0)

/*-------------------------------------------------------------------*/
/*  Mapper Function                                                  */
/*-------------------------------------------------------------------*/

/* Initialize Mapper */
extern void (*MapperInit)();
/* Write to Mapper */
extern void (*MapperWrite)(WORD wAddr, BYTE byData);
/* Write to SRAM */
extern void (*MapperSram)(WORD wAddr, BYTE byData);
/* Write to APU */
extern void (*MapperApu)(WORD wAddr, BYTE byData);
/* Read from Apu */
extern BYTE (*MapperReadApu)(WORD wAddr);
/* Callback at VSync */
extern void (*MapperVSync)();
/* Callback at HSync */
extern void (*MapperHSync)();
/* Callback at PPU read/write */
extern void (*MapperPPU)(WORD wAddr);
/* Callback at sprite pattern fetch. Real MMC2/MMC4 hardware flips its CHR
   latch on sprite fetches as well as background fetches, and Punch-Out!!
   relies on it (blank trigger sprites holding tile $FD/$FE). Mappers 9 and
   10 are the only ones that install this; it stays null everywhere else so
   the sprite path costs one null test per scanline. */
extern void (*MapperSprPPU)(WORD wAddr);
/* False when MapperPPU is the Map0_PPU no-op stub - see InfoNES.cpp. */
extern bool MapperPPUActive;
/* Raised when OAM or the $2000 sprite bits change - see InfoNES.cpp. */
extern bool SprLatchDirty;
/* Callback at Rendering Screen 1:BG, 0:Sprite */
extern void (*MapperRenderScreen)(BYTE byMode);


// Helpers for state save/load blobs
extern int (*MapperBlobSize)();            // returns size of mapper blob
extern void (*MapperSaveBlob)(BYTE *pBuf); // saves mapper blob to buffer
extern void (*MapperLoadBlob)(BYTE *pBuf); // loads mapper blob from buffer

// CHR RAM owned by a mapper and living outside PPURAM, registered by the
// mapper's init. Non-owning: the mapper keeps the allocation and InfoNES_Fin
// frees it. state.cpp uses these for PPUBANK index arithmetic and to
// serialize the CHR RAM, so it needs no per-mapper knowledge.
extern BYTE *MapperChrRam;
extern DWORD MapperChrRamSize;

// Name table RAM owned by a mapper and living outside PPURAM: boards that
// bank the name tables (mapper 111 / GTROM) or keep the extra four-screen
// name tables in cartridge RAM. Only PPUBANK slots 8..11 are ever pointed
// here; 12..15 stay the identity mapping into PPURAM so the $3000 alias and
// the palette read-back at $3F00 keep working. May be a sub-range of
// MapperChrRam (one SRAM chip); state.cpp detects that and does not write it
// to the state file twice. Same non-owning contract as MapperChrRam.
extern BYTE *MapperNtRam;
extern DWORD MapperNtRamSize;

/*-------------------------------------------------------------------*/
/*  ROM information                                                  */
/*-------------------------------------------------------------------*/

/* .nes File Header */
struct NesHeader_tag
{
  BYTE byID[4];
  BYTE byRomSize;
  BYTE byVRomSize;
  BYTE byInfo1;
  BYTE byInfo2;
  BYTE byReserve[8];
};

/* .nes File Header */
extern struct NesHeader_tag NesHeader;

/* Mapper No. iNES packs 8 bits into header bytes 6 and 7; NES 2.0 adds a
   third nibble in byte 8, so this has to be 16 bits wide. */
extern WORD MapperNo;

/* NES 2.0 submapper (header byte 8, high nibble). 0 for iNES 1.0 images. */
extern BYTE SubMapperNo;

/* Other */
extern BYTE ROM_Mirroring;
extern BYTE ROM_SRAM;
extern BYTE ROM_Trainer;
extern BYTE ROM_FourScr;

/* True when the loaded image is a Famicom Disk System disk (no iNES header).
   Set by parseROM in main before InfoNES_Reset. RP2350 only - FDS is
   compiled out on RP2040, so this is always false there. PSRAM is not
   required; it only selects multi-side over single-side drive mode. */
extern bool IsFDS;

/* True when the loaded image is an NSF (Nintendo Sound Format) file. */
extern bool IsNSF;

/*-------------------------------------------------------------------*/
/*  Function prototypes                                              */
/*-------------------------------------------------------------------*/

/* Initialize InfoNES */
void InfoNES_Init();

/* Completion treatment */
void InfoNES_Fin();

/* Load a cassette */
int InfoNES_Load(const char *pszFileName);

/* Reset InfoNES */
int InfoNES_Reset();

/* Initialize PPU */
void InfoNES_SetupPPU();

/* Set up a Mirroring of Name Table */
void InfoNES_Mirroring(int nType);

/* Region selectors for InfoNES_SetRegion() / InfoNES_Main(). */
#define INFONES_REGION_NTSC  0
#define INFONES_REGION_PAL   1
#define INFONES_REGION_DENDY 2

/* The main loop of InfoNES. region: 0=NTSC, 1=PAL, 2=Dendy. */
void InfoNES_Main(int region);

/* Select region (0/1/2) for timing. Must be called before InfoNES_Init() /
   InfoNES_pAPUInit() so region-dependent constants are picked up.
   InfoNES_Main() calls this for you. */
void InfoNES_SetRegion(int region);

/* Returns the currently selected region (0/1/2). */
int InfoNES_GetRegion();

/* True for PAL or Dendy (anything that uses the PAL CPU clock + 50 Hz pacing
   + PAL APU period tables). Used by audio init and frame pacing. */
bool InfoNES_IsPal();

/* The loop of emulation */
void InfoNES_Cycle();

/* A function in H-Sync */
int InfoNES_HSync();

/* Render a scanline */
void InfoNES_DrawLine();

/* Get a position of scanline hits sprite #0 */
void InfoNES_GetSprHitY();

/* Develop character data */
void InfoNES_SetupChr();

void InfoNES_SetLineBuffer(WORD *p, WORD size);

// void *InfoNes_GetRAM(size_t *size);

// void *InfoNes_GetChrBuf(size_t *size);

// void * InfoNes_GetPPURAM(size_t *size);

// void *InfoNes_GetSPRRAM(size_t *size);

#endif /* !InfoNES_H_INCLUDED */