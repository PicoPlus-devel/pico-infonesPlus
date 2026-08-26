/*===================================================================*/
/*                                                                   */
/*  K6502_RW.h : 6502 Reading/Writing Operation for NES              */
/*               This file is included in K6502.cpp                  */
/*                                                                   */
/*  2000/5/23   InfoNES Project ( based on pNesX )                   */
/*                                                                   */
/*===================================================================*/

#ifndef K6502_RW_H_INCLUDED
#define K6502_RW_H_INCLUDED

/*-------------------------------------------------------------------*/
/*  Include files                                                    */
/*-------------------------------------------------------------------*/

#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_pAPU.h"
#include <pico.h>
#include "zapper.h" /* NES Zapper on controller port 2 ($4017 bits 3/4) */

/* CPU cycle counter (defined in K6502.cpp), needed here for OAM DMA penalty. */
extern int g_wPassedClocks;

/* NSF mode reads $8000-$FFFF via this 4KB-granular pointer table
   (defined in InfoNES_NSF.cpp). Direct flash pointers for clean pages,
   prebuilt shadow buffers for partial/out-of-range pages. */
extern BYTE *NsfBank4K[8];

/*===================================================================*/
/*                                                                   */
/*            K6502_ReadZp() : Reading from the zero page            */
/*                                                                   */
/*===================================================================*/
static inline BYTE K6502_ReadZp(BYTE byAddr)
{
  /*
 *  Reading from the zero page
 *
 *  Parameters
 *    BYTE byAddr              (Read)
 *      An address inside the zero page
 *
 *  Return values
 *    Read Data
 */

  return RAM[byAddr];
}

/*===================================================================*/
/*                                                                   */
/*               K6502_Read() : Reading operation                    */
/*                                                                   */
/*===================================================================*/
static inline BYTE __not_in_flash_func(K6502_Read)(WORD wAddr)
{
  /*
 *  Reading operation
 *
 *  Parameters
 *    WORD wAddr              (Read)
 *      Address to read
 *
 *  Return values
 *    Read data
 *
 *  Remarks
 *    0x0000 - 0x1fff  RAM ( 0x800 - 0x1fff is mirror of 0x0 - 0x7ff )
 *    0x2000 - 0x3fff  PPU
 *    0x4000 - 0x5fff  Sound
 *    0x6000 - 0x7fff  SRAM ( Battery Backed )
 *    0x8000 - 0xffff  ROM
 *
 */
  BYTE byRet;

  if (wAddr >= 0x8000)
  {
    /* This is the single hottest branch in the emulator: every opcode
       fetch and most operand fetches land here. Anything added to it is
       paid ~2x per emulated instruction, i.e. ~16000 times per frame.
       On RP2040 the `if (IsNSF)` test below cost ~5 cycles a read
       (~0.3ms per frame at 252MHz), and the code it added spread the
       opcode dispatch in step() past the +-254 byte reach of a Thumb
       conditional branch, so every `beq.n target` in the dispatch chain
       became a `bne.n .+4; b.n target` pair on top of that. Together
       with the FDS $E445 trap in K6502.cpp that came to roughly 0.6ms
       of the 16.67ms frame budget - enough to make core0 miss the DVI
       line deadline on heavy games, at which
       point dvi::DMA::update() paints the scanline from listActiveError_
       (solid red) and the frame rate halves to 30. That was the
       Prince of Persia red flicker reported against v0.41.

       So NSF gets its own read path only on RP2350, where there is
       headroom. On RP2040, NSF mode materialises its 4KB banks into a
       32KB SRAM window that ROMBANK0..3 point at (see NsfApplyBank in
       InfoNES_NSF.cpp), which keeps this branch free of any NSF test. */
#if PICO_RP2350
    if (IsNSF)
    {
      /* NSF reset/IRQ vectors are hardcoded so NsfBank4K can point
         directly into flash without a shadow buffer holding patched
         vectors at $7FFC-$7FFF. */
      if (wAddr >= 0xFFFC)
      {
        switch (wAddr)
        {
          case 0xFFFC: return 0x00;
          case 0xFFFD: return 0x41;  /* reset → $4100 */
          case 0xFFFE: return 0x10;
          case 0xFFFF: return 0x41;  /* IRQ   → $4110 */
        }
      }
      return NsfBank4K[(wAddr - 0x8000) >> 12][wAddr & 0xFFF];
    }
#endif
    return ROMBANK[(wAddr - 0x8000) >> 13][wAddr & 0x1fff];
  }

  switch (wAddr & 0xe000)
  {
  case 0x0000: /* RAM */
    return RAM[wAddr & 0x7ff];

  case 0x2000:                /* PPU */
    if ((wAddr & 0x7) == 0x7) /* PPU Memory */
    {
      WORD addr = PPU_Addr;

      // Increment PPU Address
      PPU_Addr += PPU_Increment;
      addr &= 0x3fff;

      // Set return value;
      byRet = PPU_R7;

      // Read PPU Memory
      PPU_R7 = PPUBANK[addr >> 10][addr & 0x3ff];

      return byRet;
    }
    else if ((wAddr & 0x7) == 0x4) /* SPR_RAM I/O Register */
    {
      return SPRRAM[PPU_R3++];
    }
    else if ((wAddr & 0x7) == 0x2) /* PPU Status */
    {
      // Set return value
      byRet = PPU_R2;

      // Reset a V-Blank flag
      PPU_R2 &= ~R2_IN_VBLANK;

      // Reset address latch
      PPU_Latch_Flag = 0;

      /* Original InfoNES cleared the name table select bits ($2000 bits 0-1,
         and with them bits 10-11 of the "t" register) on any $2002 read taken
         during V-Blank with NMI disabled. Reading $2002 has no such effect on
         real hardware: it only returns the status bits, clears the V-Blank
         flag and resets the $2005/$2006 write latch.

         The hack breaks every game that programs $2000 and then reads $2002 to
         reset the latch before writing the scroll registers -- Final Fantasy
         does exactly that in its set-scroll routine. t bits 10-11 were wiped
         after the game had just set them, the pre-render line copied t into v,
         and the top of the frame was drawn from name table 0 until the game's
         next (mid-frame) $2000 write restored the bit -- a flickering band of
         the wrong half of the map across the top of the screen. */
      return byRet;
    }
    break;

  case 0x4000: /* Sound */
    if (wAddr == 0x4015)
    {
      // APU status (read register is separate from write register)
      byRet = 0;
      if (ApuC1Atl > 0)
        byRet |= (1 << 0);
      if (ApuC2Atl > 0)
        byRet |= (1 << 1);
      if (!ApuC3Holdnote)
      {
        if (ApuC3Atl > 0)
          byRet |= (1 << 2);
      }
      else
      {
        if (ApuC3Llc > 0)
          byRet |= (1 << 2);
      }
      if (ApuC4Atl > 0)
        byRet |= (1 << 3);
      if (ApuC5DmaLength > 0)
        byRet |= (1 << 4);

      // FrameIRQ
      if (APU_Reg[0x15] & 0x40)
        byRet |= 0x40;
      APU_Reg[0x15] &= ~0x40;
      return byRet;
    }
    else if (wAddr == 0x4016)
    {
      // Set Joypad1 data
      byRet = (BYTE)((PAD1_Latch >> PAD1_Bit) & 1) | 0x40;
      PAD1_Bit = (PAD1_Bit == 23) ? 0 : (PAD1_Bit + 1);
      zapperNote4016(); // diagnostics only; compiles away in release builds
      return byRet;
    }
    else if (wAddr == 0x4017)
    {
      // Set Joypad2 data
      byRet = (BYTE)((PAD2_Latch >> PAD2_Bit) & 1) | 0x40;
      PAD2_Bit = (PAD2_Bit == 23) ? 0 : (PAD2_Bit + 1);
      // A Zapper on controller port 2 overlays bit 3 (light sensed at the
      // current scanline) and bit 4 (trigger half-pulled) with the live GPIO
      // levels, sampled right here rather than latched once per frame. Bit 0
      // (the pad 2 serial data shifted above) and bit 6 (open bus) are left
      // alone, so a controller in port 2 keeps working. Folds away to nothing
      // when no Zapper is connected or support is compiled out.
      return zapperApply4017(byRet);
    }
    else
    {
      /* Return Mapper Register*/
      return MapperReadApu(wAddr);
    }
    break;
    // The other sound registers are not readable.

  case 0x6000: /* SRAM */
    return SRAMBANK[wAddr & 0x1fff];

    // case 0x8000: /* ROM BANK 0 */
    //   return ROMBANK0[wAddr & 0x1fff];

    // case 0xa000: /* ROM BANK 1 */
    //   return ROMBANK1[wAddr & 0x1fff];

    // case 0xc000: /* ROM BANK 2 */
    //   return ROMBANK2[wAddr & 0x1fff];

    // case 0xe000: /* ROM BANK 3 */
    //   return ROMBANK3[wAddr & 0x1fff];
  }

  return (wAddr >> 8); /* when a register is not readable the upper half
                            address is returned. */
}

/*===================================================================*/
/*                                                                   */
/*               K6502_Write() : Writing operation                    */
/*                                                                   */
/*===================================================================*/
static inline void __not_in_flash_func(K6502_Write)(WORD wAddr, BYTE byData)
{
  /*
 *  Writing operation
 *
 *  Parameters
 *    WORD wAddr              (Read)
 *      Address to write
 *
 *    BYTE byData             (Read)
 *      Data to write
 *
 *  Remarks
 *    0x0000 - 0x1fff  RAM ( 0x800 - 0x1fff is mirror of 0x0 - 0x7ff )
 *    0x2000 - 0x3fff  PPU
 *    0x4000 - 0x5fff  Sound
 *    0x6000 - 0x7fff  SRAM ( Battery Backed )
 *    0x8000 - 0xffff  ROM
 *
 */

  switch (wAddr & 0xe000)
  {
  case 0x0000: /* RAM */
  {
    auto addr = wAddr & 0x7ff;
    RAM[addr] = byData;
  }
  break;

  case 0x2000: /* PPU */
    switch (wAddr & 0x7)
    {
    case 0: /* 0x2000 */
      PPU_R0 = byData;
      PPU_Increment = (PPU_R0 & R0_INC_ADDR) ? 32 : 1;
      PPU_NameTableBank = NAME_TABLE0 + (PPU_R0 & R0_NAME_ADDR);
      PPU_BG_Base = (PPU_R0 & R0_BG_ADDR) ? ChrBuf + 256 * 64 : ChrBuf;
      PPU_SP_Base = (PPU_R0 & R0_SP_ADDR) ? ChrBuf + 256 * 64 : ChrBuf;
      PPU_SP_Height = (PPU_R0 & R0_SP_SIZE) ? 16 : 8;

      // Account for Loopy's scrolling discoveries
      PPU_Temp = (PPU_Temp & 0xF3FF) | ((((WORD)byData) & 0x0003) << 10);
      break;

    case 1: /* 0x2001 */
      PPU_R1 = byData;
      break;

    case 2: /* 0x2002 */
#if 0	  
          PPU_R2 = byData;     // 0x2002 is not writable
#endif
      break;

    case 3: /* 0x2003 */
      // Sprite RAM Address
      PPU_R3 = byData;
      break;

    case 4: /* 0x2004 */
      // Write data to Sprite RAM
      SPRRAM[PPU_R3++] = byData;
      break;

    case 5: /* 0x2005 */
      // Set Scroll Register
      if (PPU_Latch_Flag)
      {
        // V-Scroll Register
        // Added : more Loopy Stuff
        PPU_Temp = (PPU_Temp & 0xFC1F) | ((((WORD)byData) & 0xF8) << 2);
        PPU_Temp = (PPU_Temp & 0x8FFF) | ((((WORD)byData) & 0x07) << 12);
      }
      else
      {
        // H-Scroll Register
        PPU_Scr_H_Bit = byData & 7;
        // Added : more Loopy Stuff
        PPU_Temp = (PPU_Temp & 0xFFE0) | ((((WORD)byData) & 0xF8) >> 3);
      }
      PPU_Latch_Flag ^= 1;
      break;

    case 6: /* 0x2006 */
      // Set PPU Address
      if (PPU_Latch_Flag)
      {
        /* Low */
        PPU_Temp = (PPU_Temp & 0xFF00) | (((WORD)byData) & 0x00FF);
        PPU_Addr = PPU_Temp;
        InfoNES_SetupScr();
      }
      else
      {
        /* High */
        PPU_Temp = (PPU_Temp & 0x00FF) | ((((WORD)byData) & 0x003F) << 8);
      }
      PPU_Latch_Flag ^= 1;
      break;

    case 7: /* 0x2007 */
    {
      WORD addr = PPU_Addr;

      // Increment PPU Address
      PPU_Addr += PPU_Increment;
      addr &= 0x3fff;

      // Write to PPU Memory
      if (addr < 0x2000 && byVramWriteEnable)
      {
        // Pattern Data
        ChrBufUpdate |= (1 << (addr >> 10));
        PPUBANK[addr >> 10][addr & 0x3ff] = byData;
      }
      else if (addr < 0x3f00) /* 0x2000 - 0x3eff */
      {
        // Name Table and mirror
        PPUBANK[addr >> 10][addr & 0x3ff] = byData;
        PPUBANK[(addr ^ 0x1000) >> 10][addr & 0x3ff] = byData;
      }
      else if (!(addr & 0xf)) /* 0x3f00 or 0x3f10 */
      {
        // Palette mirror
        PPURAM[0x3f10] = PPURAM[0x3f14] = PPURAM[0x3f18] = PPURAM[0x3f1c] =
            PPURAM[0x3f00] = PPURAM[0x3f04] = PPURAM[0x3f08] = PPURAM[0x3f0c] = byData;
        PalTable[0x00] = PalTable[0x04] = PalTable[0x08] = PalTable[0x0c] =
            PalTable[0x10] = PalTable[0x14] = PalTable[0x18] = PalTable[0x1c] = NesPalette[byData] | 0x8000;
      }
      else if (addr & 3)
      {
        // Palette
        PPURAM[addr] = byData;
        PalTable[addr & 0x1f] = NesPalette[byData];
      }
    }
    break;
    }
    break;

  case 0x4000: /* Sound */
    if (wAddr <= 0x4017)
    {
    switch (wAddr & 0x1f)
    {
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07:
    case 0x08:
    case 0x09:
    case 0x0a:
    case 0x0b:
    case 0x0c:
    case 0x0d:
    case 0x0e:
    case 0x0f:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
      // Call Function corresponding to Sound Registers
      if (!APU_Mute)
        pAPUSoundRegs[wAddr & 0x1f](wAddr, byData);
      break;

    case 0x14: /* 0x4014 */
      // Sprite DMA
      // On real NES, OAM DMA costs 513-514 CPU cycles (the CPU is halted
      // while the DMA unit transfers 256 bytes). InfoNES previously charged
      // 0 cycles, which caused the entire NMI handler (and any cycle-counted
      // raster effects following it) to run ~4.5 scanlines too early. Rush'n
      // Attack relies on a cycle-counted delay in its NMI handler to split
      // the screen between a fixed HUD and a scrolling playfield; without
      // the DMA penalty, the scroll change leaked into the last few lines
      // of the HUD area.
      g_wPassedClocks += 514;
      switch (byData >> 5)
      {
      case 0x0: /* RAM */
        InfoNES_MemoryCopy(SPRRAM, &RAM[((WORD)byData << 8) & 0x7ff], SPRRAM_SIZE);
        break;

      case 0x3: /* SRAM */
        InfoNES_MemoryCopy(SPRRAM, &SRAM[((WORD)byData << 8) & 0x1fff], SPRRAM_SIZE);
        break;

      case 0x4: /* ROM BANK 0 */
        InfoNES_MemoryCopy(SPRRAM, &ROMBANK0[((WORD)byData << 8) & 0x1fff], SPRRAM_SIZE);
        break;

      case 0x5: /* ROM BANK 1 */
        InfoNES_MemoryCopy(SPRRAM, &ROMBANK1[((WORD)byData << 8) & 0x1fff], SPRRAM_SIZE);
        break;

      case 0x6: /* ROM BANK 2 */
        InfoNES_MemoryCopy(SPRRAM, &ROMBANK2[((WORD)byData << 8) & 0x1fff], SPRRAM_SIZE);
        break;

      case 0x7: /* ROM BANK 3 */
        InfoNES_MemoryCopy(SPRRAM, &ROMBANK3[((WORD)byData << 8) & 0x1fff], SPRRAM_SIZE);
        break;
      }
      break;

    case 0x15: /* 0x4015 */
      InfoNES_pAPUWriteControl(wAddr, byData);
#if 0
          /* Unknown */
          if ( byData & 0x10 ) 
          {
	    byData &= ~0x80;
	  }
#endif
      break;

    case 0x16: /* 0x4016 */
      // Reset joypad
      if (!(APU_Reg[0x16] & 1) && (byData & 1))
      {
        PAD1_Bit = 0;
        PAD2_Bit = 0;
      }
      break;

    case 0x17: /* 0x4017 */
      // Frame IRQ
      FrameStep = 0;
      if (!(byData & 0xc0))
      {
        FrameIRQ_Enable = 1;
      }
      else
      {
        FrameIRQ_Enable = 0;
      }
      break;
    }

    /* Write to APU Register */
    APU_Reg[wAddr & 0x1f] = byData;
    }
    else
    {
      /* Write to Mapper ($4018-$5FFF) */
      MapperApu(wAddr, byData);
    }
    break;

  case 0x6000: /* SRAM */
    SRAMBANK[wAddr & 0x1fff] = byData;
    SRAMwritten = true;

    /* Let mapper handle SRAM writes (e.g. bankswitched WRAM) */
    MapperSram(wAddr, byData);
    break;

  case 0x8000: /* ROM BANK 0 */
  case 0xa000: /* ROM BANK 1 */
  case 0xc000: /* ROM BANK 2 */
  case 0xe000: /* ROM BANK 3 */
    // Write to Mapper
    MapperWrite(wAddr, byData);
    break;
  }
}

// Reading/Writing operation (WORD version)
static inline WORD K6502_ReadW(WORD wAddr) { return K6502_Read(wAddr) | (WORD)K6502_Read(wAddr + 1) << 8; };
static inline void K6502_WriteW(WORD wAddr, WORD wData)
{
  K6502_Write(wAddr, wData & 0xff);
  K6502_Write(wAddr + 1, wData >> 8);
};
static inline WORD K6502_ReadZpW(BYTE byAddr) { return K6502_ReadZp(byAddr) | (K6502_ReadZp(byAddr + 1) << 8); };

// 6502's indirect absolute jmp(opcode: 6C) has a bug (added at 01/08/15 )
static inline WORD K6502_ReadW2(WORD wAddr)
{
  if (0x00ff == (wAddr & 0x00ff))
  {
    return K6502_Read(wAddr) | (WORD)K6502_Read(wAddr - 0x00ff) << 8;
  }
  else
  {
    return K6502_Read(wAddr) | (WORD)K6502_Read(wAddr + 1) << 8;
  }
}

#endif /* !K6502_RW_H_INCLUDED */