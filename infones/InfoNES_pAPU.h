/*===================================================================*/
/*                                                                   */
/*  InfoNES_pAPU.h : InfoNES Sound Emulation Function                */
/*                                                                   */
/*  2000/05/29  InfoNES Project ( based on DarcNES and NesterJ )     */
/*                                                                   */
/*===================================================================*/

#ifndef InfoNES_PAPU_H_INCLUDED
#define InfoNES_PAPU_H_INCLUDED

#include <stdint.h>

/*-------------------------------------------------------------------*/
/*  Macros                                                           */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Rectangle Wave #0                                                 */
/* Reg0: 0-3=Volume, 4=Envelope, 5=Hold, 6-7=Duty Cycle              */
/* Reg1: 0-2=sweep shifts, 3=sweep inc, 4-6=sweep length, 7=sweep on */
/* Reg2: 8 bits of freq                                              */
/* Reg3: 0-2=high freq, 7-4=vbl length counter                       */
/*-------------------------------------------------------------------*/
#define ApuC1Vol (ApuC1a & 0x0f)
#define ApuC1Env (ApuC1a & 0x10)
#define ApuC1Hold (ApuC1a & 0x20)
#define ApuC1DutyCycle (ApuC1a & 0xc0)
//#define ApuC1EnvDelay       ( ( WORD )( ApuC1a & 0x0f ) << 8 )
#define ApuC1EnvDelay ((ApuC1a & 0x0f) + 1)
#define ApuC1SweepOn (ApuC1b & 0x80)
#define ApuC1SweepIncDec (ApuC1b & 0x08)
#define ApuC1SweepShifts (ApuC1b & 0x07)
//#define ApuC1SweepDelay     ( ( ( ( WORD )ApuC1b & 0x70 ) >> 4 ) << 8 )
#define ApuC1SweepDelay (((ApuC1b & 0x70) >> 4) + 1)
#define ApuC1FreqLimit (ApuFreqLimit[(ApuC1b & 0x07)])

/*-------------------------------------------------------------------*/
/* Rectangle Wave #1                                                 */
/* Reg0: 0-3=Volume, 4=Envelope, 5=Hold, 6-7=Duty Cycle              */
/* Reg1: 0-2=sweep shifts, 3=sweep inc, 4-6=sweep length, 7=sweep on */
/* Reg2: 8 bits of freq                                              */
/* Reg3: 0-2=high freq, 7-4=vbl length counter                       */
/*-------------------------------------------------------------------*/
#define ApuC2Vol (ApuC2a & 0x0f)
#define ApuC2Env (ApuC2a & 0x10)
#define ApuC2Hold (ApuC2a & 0x20)
#define ApuC2DutyCycle (ApuC2a & 0xc0)
//#define ApuC2EnvDelay       ( ( WORD )( ApuC2a & 0x0f ) << 8 )
#define ApuC2EnvDelay ((ApuC2a & 0x0f) + 1)
#define ApuC2SweepOn (ApuC2b & 0x80)
#define ApuC2SweepIncDec (ApuC2b & 0x08)
#define ApuC2SweepShifts (ApuC2b & 0x07)
//#define ApuC2SweepDelay     ( ( ( ( WORD )ApuC2b & 0x70 ) >> 4 ) << 8 )
#define ApuC2SweepDelay (((ApuC2b & 0x70) >> 4) + 1)
#define ApuC2FreqLimit (ApuFreqLimit[(ApuC2b & 0x07)])

/*-------------------------------------------------------------------*/
/* Triangle Wave                                                     */
/* Reg0: 7=Holdnote, 6-0=Linear Length Counter                       */
/* Reg2: 8 bits of freq                                              */
/* Reg3: 0-2=high freq, 7-4=vbl length counter                       */
/*-------------------------------------------------------------------*/
#define ApuC3Holdnote (ApuC3a & 0x80)
#define ApuC3LinearLength (((WORD)ApuC3a & 0x7f) << 6)
#define ApuC3LengthCounter (ApuAtl[((ApuC3d & 0xf8) >> 3)])
#define ApuC3Freq ((((WORD)ApuC3d & 0x07) << 8) + ApuC3c)

/*-------------------------------------------------------------------*/
/* White Noise Channel                                               */
/* Reg0: 0-3=Volume, 4=Envelope, 5=Hold                              */
/* Reg2: 7=Small(93byte) sample, 3-0=Freq Lookup                     */
/* Reg3: 7-3=vbl length counter                                      */
/*-------------------------------------------------------------------*/
//#define ApuC4Vol            ( ( ApuC4a & 0x0f ) | ( ( ApuC4a & 0x0f ) << 4 ) )
#define ApuC4Vol (ApuC4a & 0x0f)
//#define ApuC4EnvDelay       ( ( WORD )( ApuC4a & 0x0f ) << 8 )
#define ApuC4EnvDelay ((ApuC4a & 0x0f) + 1)
#define ApuC4Env (ApuC4a & 0x10)
#define ApuC4Hold (ApuC4a & 0x20)
#define ApuC4Freq (ApuNoiseFreq[(ApuC4c & 0x0f)])
#define ApuC4Small (ApuC4c & 0x80)
//#define ApuC4LengthCounter  ( ApuAtl[ ( ( ApuC4d & 0xf8 ) >> 3 ) ] )
#define ApuC4LengthCounter (ApuAtl[(ApuC4d >> 3)] << 1)

/*-------------------------------------------------------------------*/
/* DPCM Channel                                                      */
/* Reg0: 0-3=Frequency, 6=Looping                                    */
/* Reg1: 0-6=DPCM Value                                              */
/* Reg2: 0-7=Cache Addr                                              */
/* Reg3: 0-7=Cache DMA Length                                        */
/*-------------------------------------------------------------------*/
#if 0
#define ApuC5Freq (ApuDpcmCycles[(ApuC5a & 0x0F)])
#define ApuC5Looping (ApuC5a & 0x40)
#define ApuC5DpcmValue ((ApuC5b & 0x7F) >> 1)
#define ApuC5CacheAddr (0xc000 + (WORD)(ApuC5c << 6))
#define ApuC5CacheDmaLength (((ApuC5d << 4) + 1) << 3)
#endif

/*-------------------------------------------------------------------*/
/*  pAPU Event resources                                             */
/*-------------------------------------------------------------------*/

//#define APU_EVENT_MAX 15000
#define APU_EVENT_MAX 100

struct ApuEvent_t
{
  short time;
  BYTE type;
  BYTE data;
};

#define APUET_MASK 0xfc
#define APUET_C1 0x00
#define APUET_W_C1A 0x00
#define APUET_W_C1B 0x01
#define APUET_W_C1C 0x02
#define APUET_W_C1D 0x03
#define APUET_C2 0x04
#define APUET_W_C2A 0x04
#define APUET_W_C2B 0x05
#define APUET_W_C2C 0x06
#define APUET_W_C2D 0x07
#define APUET_C3 0x08
#define APUET_W_C3A 0x08
#define APUET_W_C3B 0x09
#define APUET_W_C3C 0x0a
#define APUET_W_C3D 0x0b
#define APUET_C4 0x0c
#define APUET_W_C4A 0x0c
#define APUET_W_C4B 0x0d
#define APUET_W_C4C 0x0e
#define APUET_W_C4D 0x0f
#define APUET_C5 0x10
#define APUET_W_C5A 0x10
#define APUET_W_C5B 0x11
#define APUET_W_C5C 0x12
#define APUET_W_C5D 0x13
#define APUET_W_CTRL 0x20
#define APUET_SYNC 0x40

/*-------------------------------------------------------------------*//*  MMC5 Audio Event Types                                           */
/*-------------------------------------------------------------------*/
#define APUET_MMC5_P1     0x50
#define APUET_W_MMC5_P1A  0x50
#define APUET_W_MMC5_P1B  0x51
#define APUET_W_MMC5_P1C  0x52
#define APUET_W_MMC5_P1D  0x53
#define APUET_MMC5_P2     0x54
#define APUET_W_MMC5_P2A  0x54
#define APUET_W_MMC5_P2B  0x55
#define APUET_W_MMC5_P2C  0x56
#define APUET_W_MMC5_P2D  0x57
#define APUET_W_MMC5_CTRL 0x60

/*-------------------------------------------------------------------*/
/*  VRC6 Audio Event Types                                           */
/*-------------------------------------------------------------------*/
#define APUET_VRC6_P1     0x80
#define APUET_W_VRC6_P1A  0x80
#define APUET_W_VRC6_P1B  0x81
#define APUET_W_VRC6_P1C  0x82
#define APUET_VRC6_P2     0x84
#define APUET_W_VRC6_P2A  0x84
#define APUET_W_VRC6_P2B  0x85
#define APUET_W_VRC6_P2C  0x86
#define APUET_VRC6_SAW    0x88
#define APUET_W_VRC6_SAWA 0x88
#define APUET_W_VRC6_SAWB 0x89
#define APUET_W_VRC6_SAWC 0x8A
#define APUET_W_VRC6_FREQ 0x8C

/*-------------------------------------------------------------------*/
/*  MMC5 Pulse 1 Macros                                              */
/*-------------------------------------------------------------------*/
#define ApuMmc5P1Vol        ( ApuMmc5P1a & 0x0f )
#define ApuMmc5P1Env        ( ApuMmc5P1a & 0x10 )
#define ApuMmc5P1Hold       ( ApuMmc5P1a & 0x20 )
#define ApuMmc5P1DutyCycle  ( ApuMmc5P1a & 0xc0 )
#define ApuMmc5P1EnvDelay   ( ( ApuMmc5P1a & 0x0f ) + 1 )

/*-------------------------------------------------------------------*/
/*  MMC5 Pulse 2 Macros                                              */
/*-------------------------------------------------------------------*/
#define ApuMmc5P2Vol        ( ApuMmc5P2a & 0x0f )
#define ApuMmc5P2Env        ( ApuMmc5P2a & 0x10 )
#define ApuMmc5P2Hold       ( ApuMmc5P2a & 0x20 )
#define ApuMmc5P2DutyCycle  ( ApuMmc5P2a & 0xc0 )
#define ApuMmc5P2EnvDelay   ( ( ApuMmc5P2a & 0x0f ) + 1 )

/*-------------------------------------------------------------------*//*  Function prototypes                                              */
/*-------------------------------------------------------------------*/
typedef void (*ApuWritefunc)(WORD addr, BYTE value);
extern ApuWritefunc pAPUSoundRegs[20];
void ApuWriteControl(WORD addr, BYTE value);

#define InfoNES_pAPUWriteControl(addr, value) \
  {                                           \
    ApuWriteControl(addr, value);             \
  }

void InfoNES_pAPUInit(void);
void InfoNES_pAPUDone(void);
void InfoNES_pAPUVsync(void);
void InfoNES_pAPUHsync(bool enabled);

/*-------------------------------------------------------------------*/
/*  DMC IRQ timing                                                   */
/*-------------------------------------------------------------------*/
/* When a DMC sample ends, to the CPU cycle, so the DMC IRQ can be raised.
   Cycles are K6502_Now() values. Saved in save states as is. */
struct ApuDmcIrq_t
{
  uint32_t nextTick;   /* cycle of the next output timer tick */
  uint32_t startAt;    /* cycle of the first fetch after a $4015 restart */
  uint16_t period;     /* cycles per timer tick ($4010 rate) */
  uint16_t length;     /* sample length in bytes ($4013) */
  uint16_t bytes;      /* bytes still to fetch */
  uint8_t bits;        /* ticks left in the 8-bit output cycle */
  uint8_t bufferFull;  /* sample buffer holds a byte */
  uint8_t startPending;
  uint8_t irqEnable;
  uint8_t loop;
  uint8_t irqFlag;
  uint8_t ownsIrqLine; /* IRQ_State was asserted by the DMC */
  uint8_t reserved[3];
};
extern ApuDmcIrq_t ApuDmcIrq;

void ApuDmcIrqReset(void);
void ApuDmcIrqRearm(void);
void ApuDmcIrqWrite(WORD addr, BYTE value); /* $4010-$4013 */
void ApuDmcIrqWrite4015(BYTE value);
BYTE ApuDmcIrqStatus(void); /* $4015 read: bits 7 and 4 */
void ApuDmcIrqBreak(void);  /* the CPU reached K6502_BreakAt() */

/*-------------------------------------------------------------------*/
/*  pAPU Quality resources                                           */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* ApuQuality is used to control the sound playback rate.            */
/* 1 is 11015 Hz.                                                    */
/* 2 is 22050 Hz.                                                    */
/* 3 is 44100 Hz.                                                    */
/* these values subject to change without notice.                    */
/*-------------------------------------------------------------------*/
extern int ApuQuality;
#define pAPU_QUALITY 3

/* Maximum samples produced per Vsync at the highest quality setting.
 * NTSC @ 44100 Hz / 60 Hz = 735; PAL @ 44100 Hz / 50 Hz = 882. Buffers are
 * sized to PAL worst-case so the same allocations cover both regions. */
#define APU_MAX_SAMPLES_PER_SYNC 882

/*-------------------------------------------------------------------*/
/*  Rectangle Wave #1 resources                                      */
/*-------------------------------------------------------------------*/

extern BYTE ApuC1Atl;

/*-------------------------------------------------------------------*/
/*  Rectangle Wave #2 resources                                      */
/*-------------------------------------------------------------------*/

extern BYTE ApuC2Atl;

/*-------------------------------------------------------------------*/
/*  Triangle Wave resources                                          */
/*-------------------------------------------------------------------*/

extern BYTE ApuC3a;
extern BYTE ApuC3Atl;
extern DWORD ApuC3Llc; /* Linear Length Counter */

/*-------------------------------------------------------------------*/
/*  Noise resources                                                  */
/*-------------------------------------------------------------------*/

extern BYTE ApuC4Atl;

/*-------------------------------------------------------------------*/
/*  DPCM resources                                                   */
/*-------------------------------------------------------------------*/

extern int ApuC5DmaLength;

/*-------------------------------------------------------------------*/
/*  MMC5 Audio State                                                 */
/*-------------------------------------------------------------------*/
extern BYTE ApuMmc5Enable;
extern BYTE (*mmc5_wave_buffers)[APU_MAX_SAMPLES_PER_SYNC];
extern BYTE ApuMmc5P1a;
extern BYTE ApuMmc5P2a;
extern BYTE ApuMmc5P1Atl;
extern BYTE ApuMmc5P2Atl;
extern BYTE ApuMmc5PcmValue;

/*-------------------------------------------------------------------*/
/*  MMC5 Audio Write Functions                                       */
/*-------------------------------------------------------------------*/
void ApuWriteMmc5P1a(WORD addr, BYTE value);
void ApuWriteMmc5P1c(WORD addr, BYTE value);
void ApuWriteMmc5P1d(WORD addr, BYTE value);
void ApuWriteMmc5P2a(WORD addr, BYTE value);
void ApuWriteMmc5P2c(WORD addr, BYTE value);
void ApuWriteMmc5P2d(WORD addr, BYTE value);
void ApuWriteMmc5Ctrl(WORD addr, BYTE value);

/*-------------------------------------------------------------------*/
/*  VRC6 Audio State                                                 */
/*-------------------------------------------------------------------*/
extern BYTE ApuVrc6Enable;
extern BYTE (*vrc6_wave_buffers)[APU_MAX_SAMPLES_PER_SYNC];

/*-------------------------------------------------------------------*/
/*  VRC6 Audio Write Functions                                       */
/*-------------------------------------------------------------------*/
void ApuWriteVrc6P1a(WORD addr, BYTE value);
void ApuWriteVrc6P1b(WORD addr, BYTE value);
void ApuWriteVrc6P1c(WORD addr, BYTE value);
void ApuWriteVrc6P2a(WORD addr, BYTE value);
void ApuWriteVrc6P2b(WORD addr, BYTE value);
void ApuWriteVrc6P2c(WORD addr, BYTE value);
void ApuWriteVrc6SawA(WORD addr, BYTE value);
void ApuWriteVrc6SawB(WORD addr, BYTE value);
void ApuWriteVrc6SawC(WORD addr, BYTE value);
void ApuWriteVrc6Freq(WORD addr, BYTE value);

/*-------------------------------------------------------------------*/
/*  Sunsoft 5B Audio State                                           */
/*-------------------------------------------------------------------*/
extern BYTE ApuSunsoft5BEnable;
extern BYTE (*s5b_wave_buffers)[APU_MAX_SAMPLES_PER_SYNC];

/*-------------------------------------------------------------------*/
/*  FDS Audio State (defined in InfoNES_FDS.cpp)                     */
/*-------------------------------------------------------------------*/
extern BYTE  ApuFdsEnable;
extern BYTE *fds_wave_buffer;

/*-------------------------------------------------------------------*/
/*  Sunsoft 5B Audio Write Function                                  */
/*    reg: 4-bit register index (0-15) previously latched via $C000 */
/*    value: byte previously written to $E000                        */
/*-------------------------------------------------------------------*/
void ApuWriteSunsoft5B(BYTE reg, BYTE value);

/*-------------------------------------------------------------------*/
/*  VRC7 (Konami OPLL) Audio — Mapper 85, RP2350-only                */
/*    Lagrange Point JP, Tiny Toon Adventures 2 JP                   */
/*    ApuWriteVrc7Reg : value previously written to $9010 (reg sel)  */
/*    ApuWriteVrc7Data: value previously written to $9030 (data)     */
/*-------------------------------------------------------------------*/
extern BYTE  ApuVrc7Enable;
extern BYTE *vrc7_wave_buffer;
void ApuWriteVrc7Reg (BYTE value);
void ApuWriteVrc7Data(BYTE value);
void ApuRenderingVrc7(int n);
void ApuVrc7Reset(void);
void ApuVrc7Free(void);

#endif /* InfoNES_PAPU_H_INCLUDED */

/*
 * End of InfoNES_pAPU.h
 */