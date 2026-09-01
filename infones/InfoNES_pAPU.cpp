/*===================================================================*/
/*                                                                   */
/*  InfoNES_pAPU.cpp : InfoNES Sound Emulation Function              */
/*                                                                   */
/*  2000/05/29  InfoNES Project ( based on DarcNES and NesterJ )     */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------*/
/*  Include files                                                    */
/*-------------------------------------------------------------------*/
#include "K6502.h"
#include "K6502_rw.h"
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_pAPU.h"
#include "InfoNES_FDS.h"
#include "FrensHelpers.h"
#include <algorithm>
#include <string.h>

/*-------------------------------------------------------------------*/
/*   APU Event resources                                             */
/*-------------------------------------------------------------------*/

struct ApuEvent_t ApuEventQueue[APU_EVENT_MAX];
int cur_event;
WORD entertime;

/*-------------------------------------------------------------------*/
/*   APU Register Write Functions                                    */
/*-------------------------------------------------------------------*/

#define APU_WRITEFUNC(name, evtype)                                \
  void ApuWrite##name(WORD addr, BYTE value)                       \
  {                                                                \
    if (cur_event >= APU_EVENT_MAX) return;                         \
    ApuEventQueue[cur_event].time = getPassedClocks() - entertime; \
    ApuEventQueue[cur_event].type = APUET_W_##evtype;              \
    ApuEventQueue[cur_event].data = value;                         \
    cur_event++;                                                   \
  }

// 普通にバグってる

APU_WRITEFUNC(C1a, C1A);
APU_WRITEFUNC(C1b, C1B);
APU_WRITEFUNC(C1c, C1C);
APU_WRITEFUNC(C1d, C1D);

APU_WRITEFUNC(C2a, C2A);
APU_WRITEFUNC(C2b, C2B);
APU_WRITEFUNC(C2c, C2C);
APU_WRITEFUNC(C2d, C2D);

APU_WRITEFUNC(C3a, C3A);
APU_WRITEFUNC(C3b, C3B);
APU_WRITEFUNC(C3c, C3C);
APU_WRITEFUNC(C3d, C3D);

APU_WRITEFUNC(C4a, C4A);
APU_WRITEFUNC(C4b, C4B);
APU_WRITEFUNC(C4c, C4C);
APU_WRITEFUNC(C4d, C4D);

APU_WRITEFUNC(C5a, C5A);
APU_WRITEFUNC(C5b, C5B);
APU_WRITEFUNC(C5c, C5C);
APU_WRITEFUNC(C5d, C5D);

APU_WRITEFUNC(Control, CTRL);

/* MMC5 Audio Write Functions */
APU_WRITEFUNC(Mmc5P1a, MMC5_P1A);
APU_WRITEFUNC(Mmc5P1c, MMC5_P1C);
APU_WRITEFUNC(Mmc5P1d, MMC5_P1D);
APU_WRITEFUNC(Mmc5P2a, MMC5_P2A);
APU_WRITEFUNC(Mmc5P2c, MMC5_P2C);
APU_WRITEFUNC(Mmc5P2d, MMC5_P2D);
APU_WRITEFUNC(Mmc5Ctrl, MMC5_CTRL);

/* VRC6 Audio Write Functions */
APU_WRITEFUNC(Vrc6P1a, VRC6_P1A);
APU_WRITEFUNC(Vrc6P1b, VRC6_P1B);
APU_WRITEFUNC(Vrc6P1c, VRC6_P1C);
APU_WRITEFUNC(Vrc6P2a, VRC6_P2A);
APU_WRITEFUNC(Vrc6P2b, VRC6_P2B);
APU_WRITEFUNC(Vrc6P2c, VRC6_P2C);
APU_WRITEFUNC(Vrc6SawA, VRC6_SAWA);
APU_WRITEFUNC(Vrc6SawB, VRC6_SAWB);
APU_WRITEFUNC(Vrc6SawC, VRC6_SAWC);
APU_WRITEFUNC(Vrc6Freq, VRC6_FREQ);

ApuWritefunc pAPUSoundRegs[20] =
    {
        ApuWriteC1a,
        ApuWriteC1b,
        ApuWriteC1c,
        ApuWriteC1d,
        ApuWriteC2a,
        ApuWriteC2b,
        ApuWriteC2c,
        ApuWriteC2d,
        ApuWriteC3a,
        ApuWriteC3b,
        ApuWriteC3c,
        ApuWriteC3d,
        ApuWriteC4a,
        ApuWriteC4b,
        ApuWriteC4c,
        ApuWriteC4d,
        ApuWriteC5a,
        ApuWriteC5b,
        ApuWriteC5c,
        ApuWriteC5d,
};

/*-------------------------------------------------------------------*/
/*   APU resources                                                   */
/*-------------------------------------------------------------------*/

BYTE (*wave_buffers)[APU_MAX_SAMPLES_PER_SYNC]; /* PAL worst case: 44100/50 = 882 */


BYTE ApuCtrl;
BYTE ApuCtrlNew;

/*-------------------------------------------------------------------*/
/*   APU Quality resources                                           */
/*-------------------------------------------------------------------*/

int ApuQuality;

DWORD ApuPulseMagic;
DWORD ApuTriangleMagic;
DWORD ApuNoiseMagic;
unsigned int ApuSamplesPerSync16;
unsigned int ApuCyclesPerSample;
unsigned int ApuSampleRate;
DWORD ApuCycleRate;

struct ApuQualityData_t
{
  DWORD pulse_magic;
  DWORD triangle_magic;
  DWORD noise_magic;
  unsigned int samples_per_sync_16;
  unsigned int cycles_per_sample;
  unsigned int sample_rate;
  DWORD cycle_rate;
} ApuQual[] = {
    // {0xa2567000, 0xa2567000, 0xa2567000, 183, 164, 11025, 1062658},
    // {0x512b3800, 0x512b3800, 0x512b3800, 367, 82, 22050, 531329},
    // {0x289d9c00, 0x289d9c00, 0x289d9c00, 735, 41, 44100, 265664},
    {0xa2567000, 0xa2567000, 0xa2567000, 45963, 164, 11025, 664935},
    {0x512b3800, 0x512b3800, 0x512b3800, 91926, 82, 22050, 1329870},
    {0x289d9c00, 0x289d9c00, 0x289d9c00, 184402, 41, 44100, 2659741},
};

// PAL ApuQual: CPU clock 1.662607 MHz, 50.007 Hz, 312 scanlines.
//   magic: scaled by ratio 1662607/1789773 = 0.928953 (CPU-clock dependent).
//   samples_per_sync_16: per-Hsync, sample_rate*65536 / (50 * 312).
//     44100/50/312*65536 = 185262.77; 22050/50/312*65536 = 92631.38;
//     11025/50/312*65536 = 46315.69
//   cycles_per_sample: round(1662607 / sample_rate).
//   cycle_rate: 1662607 / sample_rate * 65536
//     1662607/44100*65536 = 2470797.9; 1662607/22050*65536 = 1235398.95;
//     1662607/11025*65536 = 617699.5
ApuQualityData_t ApuQualPal[] = {
    {0x96D5DA00, 0x96D5DA00, 0x96D5DA00, 46316, 151, 11025, 617700},
    {0x4B6AED00, 0x4B6AED00, 0x4B6AED00, 92631, 75, 22050, 1235399},
    {0x25C5E680, 0x25C5E680, 0x25C5E680, 185263, 38, 44100, 2470798},
};

// 44100/60/262*65536 = 183850.99236641222
// (44100*1.001)/60/262*65536 = 184034.84335877863
// (44100*1.003)/60/262*65536 = 184402.54534351142

// cycle_rate
// 1789773 / 44100 * 65536 = 2659740.665034014

/*-------------------------------------------------------------------*/
/*  Rectangle Wave #1 resources                                      */
/*-------------------------------------------------------------------*/
BYTE ApuC1a,
    ApuC1b, ApuC1c, ApuC1d;

BYTE *ApuC1Wave;
DWORD ApuC1Skip;
DWORD ApuC1Index;
int32_t ApuC1EnvPhase;
BYTE ApuC1EnvVol;
BYTE ApuC1Atl;
int32_t ApuC1SweepPhase;
DWORD ApuC1Freq;

/*-------------------------------------------------------------------*/
/*  Rectangle Wave #2 resources                                      */
/*-------------------------------------------------------------------*/
BYTE ApuC2a, ApuC2b, ApuC2c, ApuC2d;

BYTE *ApuC2Wave;
DWORD ApuC2Skip;
DWORD ApuC2Index;
int32_t ApuC2EnvPhase;
BYTE ApuC2EnvVol;
BYTE ApuC2Atl;
int32_t ApuC2SweepPhase;
DWORD ApuC2Freq;

/*-------------------------------------------------------------------*/
/*  Triangle Wave resources                                          */
/*-------------------------------------------------------------------*/
BYTE ApuC3a, ApuC3b, ApuC3c, ApuC3d;

DWORD ApuC3Skip;
DWORD ApuC3Index;
BYTE ApuC3Atl;
DWORD ApuC3Llc; /* Linear Length Counter */
bool ApuC3ReloadFlag;
// BYTE ApuC3WriteLatency;
// BYTE ApuC3CounterStarted;

/*-------------------------------------------------------------------*/
/*  Noise resources                                                  */
/*-------------------------------------------------------------------*/
BYTE ApuC4a, ApuC4b, ApuC4c, ApuC4d;

DWORD ApuC4Sr;  /* Shift register */
DWORD ApuC4Fdc; /* Frequency divide counter */
DWORD ApuC4Skip;
DWORD ApuC4Index;
BYTE ApuC4Atl;
BYTE ApuC4EnvVol;
int32_t ApuC4EnvPhase;

/*-------------------------------------------------------------------*/
/*  DPCM resources                                                   */
/*-------------------------------------------------------------------*/
BYTE ApuC5Reg[4];
BYTE ApuC5Enable;
BYTE ApuC5Looping;
BYTE ApuC5CurByte;
BYTE ApuC5DpcmValue;

int ApuC5Freq;
int ApuC5Phaseacc;

WORD ApuC5Address, ApuC5CacheAddr;
int ApuC5DmaLength, ApuC5CacheDmaLength;

/*-------------------------------------------------------------------*/
/*  MMC5 Audio resources                                             */
/*-------------------------------------------------------------------*/
BYTE ApuMmc5Enable = 0;

/* MMC5 Pulse 1 */
BYTE ApuMmc5P1a, ApuMmc5P1c, ApuMmc5P1d;
BYTE *ApuMmc5P1Wave;
DWORD ApuMmc5P1Skip;
DWORD ApuMmc5P1Index;
int32_t ApuMmc5P1EnvPhase;
BYTE ApuMmc5P1EnvVol;
BYTE ApuMmc5P1Atl;
DWORD ApuMmc5P1Freq;

/* MMC5 Pulse 2 */
BYTE ApuMmc5P2a, ApuMmc5P2c, ApuMmc5P2d;
BYTE *ApuMmc5P2Wave;
DWORD ApuMmc5P2Skip;
DWORD ApuMmc5P2Index;
int32_t ApuMmc5P2EnvPhase;
BYTE ApuMmc5P2EnvVol;
BYTE ApuMmc5P2Atl;
DWORD ApuMmc5P2Freq;

/* MMC5 PCM */
BYTE ApuMmc5PcmValue;

/* MMC5 Control ($5015) */
BYTE ApuMmc5Ctrl;
BYTE ApuMmc5CtrlNew;

/*-------------------------------------------------------------------*/
/*  VRC6 Audio resources                                             */
/*-------------------------------------------------------------------*/
BYTE ApuVrc6Enable = 0;

/* VRC6 Pulse 1 */
BYTE ApuVrc6P1a, ApuVrc6P1b, ApuVrc6P1c;
DWORD ApuVrc6P1Skip;
DWORD ApuVrc6P1Index;

/* VRC6 Pulse 2 */
BYTE ApuVrc6P2a, ApuVrc6P2b, ApuVrc6P2c;
DWORD ApuVrc6P2Skip;
DWORD ApuVrc6P2Index;

/* VRC6 Sawtooth */
BYTE ApuVrc6SawRate;
BYTE ApuVrc6SawFreqL;
BYTE ApuVrc6SawFreqH;
BYTE ApuVrc6SawAccum;
BYTE ApuVrc6SawStep;
int  ApuVrc6SawPhaseAcc;

/* VRC6 Frequency Control ($9003) */
BYTE ApuVrc6FreqCtrl;

/*-------------------------------------------------------------------*/
/*  Sunsoft 5B (YM2149-style PSG) Audio resources                    */
/*                                                                   */
/*  3 tone channels + shared 17-bit LFSR noise generator + shared    */
/*  envelope generator. I/O port registers ($0E/$0F) are unused on   */
/*  the cart and ignored.                                            */
/*-------------------------------------------------------------------*/
BYTE ApuSunsoft5BEnable = 0;

/* Raw register file ($00-$0F) */
BYTE ApuS5B_Reg[16];

/* Per-channel tone phase accumulators and increments */
DWORD ApuS5B_Skip[3];
DWORD ApuS5B_Index[3];

/* Noise generator (shared across all channels) */
DWORD ApuS5B_NoiseSkip;
DWORD ApuS5B_NoiseIndex;
DWORD ApuS5B_NoiseLfsr;   /* 17-bit LFSR, taps at bit 0 XOR bit 3 */
BYTE  ApuS5B_NoiseOut;    /* Current LFSR output bit, 0 or 1 */

/* Envelope generator (shared across all channels) */
DWORD ApuS5B_EnvSkip;
DWORD ApuS5B_EnvIndex;
BYTE  ApuS5B_EnvStep;     /* 5-bit counter, 0..31 */
BYTE  ApuS5B_EnvHold;     /* 1 = envelope frozen at end-of-shape */
BYTE  ApuS5B_EnvDecay;    /* 1 = step is inverted (descending volume) */
BYTE  ApuS5B_EnvShape;    /* cached shape register ($0D low nibble) */

/* YM2149 volume 4-bit → 8-bit amplitude. Uses the MAME-reference curve
 * rather than the ~3dB-per-step AY-3-8910 datasheet values. The datasheet
 * curve is perceptually more "correct" but compresses low-mid volumes so
 * far toward zero (vol 6 → amplitude 10) that fixed-volume SFX — which are
 * typically mid-range — become physically inaudible once averaged and
 * mixed with the NES APU. The MAME curve is still perceptually log-shaped
 * but gives mid-range values enough amplitude to survive the mix without
 * over-driving high-volume notes used by music. */
static const BYTE ApuS5B_VolTable[16] = {
    0,  12,  21,  30,  38,  49,  61,  74,
   91, 110, 128, 149, 176, 205, 230, 255
};

/*-------------------------------------------------------------------*/
/*  Wave Data                                                        */
/*-------------------------------------------------------------------*/
BYTE __not_in_flash_func(pulse_25)[0x20] = {
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
};

BYTE __not_in_flash_func(pulse_50)[0x20] = {
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
};

BYTE __not_in_flash_func(pulse_75)[0x20] = {
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
};

BYTE __not_in_flash_func(pulse_87)[0x20] = {
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x11,
    0x00,
    0x00,
    0x00,
    0x00,
};

BYTE __not_in_flash_func(triangle_50)[0x20] = {
    0x00,
    0x10,
    0x20,
    0x30,
    0x40,
    0x50,
    0x60,
    0x70,
    0x80,
    0x90,
    0xa0,
    0xb0,
    0xc0,
    0xd0,
    0xe0,
    0xf0,
    0xff,
    0xef,
    0xdf,
    0xcf,
    0xbf,
    0xaf,
    0x9f,
    0x8f,
    0x7f,
    0x6f,
    0x5f,
    0x4f,
    0x3f,
    0x2f,
    0x1f,
    0x0f,
};

BYTE *__not_in_flash_func(pulse_waves)[4] = {
    pulse_87,
    pulse_75,
    pulse_50,
    pulse_25,
};

/*-------------------------------------------------------------------*/
/*  Active Time Left Data                                            */
/*-------------------------------------------------------------------*/
BYTE __not_in_flash_func(ApuAtl)[0x20] =
    {
        5,
        127,
        10,
        1,
        19,
        2,
        40,
        3,
        80,
        4,
        30,
        5,
        7,
        6,
        13,
        7,
        6,
        8,
        12,
        9,
        24,
        10,
        48,
        11,
        96,
        12,
        36,
        13,
        8,
        14,
        16,
        15,
};

/*-------------------------------------------------------------------*/
/* Frequency Limit of Rectangle Channels                             */
/*-------------------------------------------------------------------*/
WORD __not_in_flash_func(ApuFreqLimit)[8] =
    {
        0x3FF, 0x555, 0x666, 0x71C, 0x787, 0x7C1, 0x7E0, 0x7F0};

/*-------------------------------------------------------------------*/
/* Phase increment of a rectangle channel for a given period         */
/*-------------------------------------------------------------------*/
/* The divisor is period / 2, so a period of 0 *or* 1 divides by zero:
   a SIGFPE on the host harness, and an undefined quotient on ARM where
   the hardware does not trap. Both are unreachable in audible output
   because ApuRenderingWave1/2 already silence the channel while the
   period is below 8 - exactly as the APU's sweep unit mutes it - so the
   Skip value is never read in that range. Returning 0 below the
   threshold therefore removes the division without altering a single
   audible sample. The frequency sweep can walk a period down into this
   range, which is how Konami VRC titles reached it. */
static inline DWORD ApuPulseSkip(WORD wFreq)
{
  return (wFreq < 8) ? 0 : ApuPulseMagic / (wFreq / 2);
}

/*-------------------------------------------------------------------*/
/* Noise Frequency Lookup Tables (NTSC + PAL)                        */
/*-------------------------------------------------------------------*/
static DWORD __not_in_flash_func(ApuNoiseFreqNtsc)[16] =
    {
        4, 8, 16, 32, 64, 96, 128, 160,
        202, 254, 380, 508, 762, 1016, 2034, 4068};

static DWORD __not_in_flash_func(ApuNoiseFreqPal)[16] =
    {
        4, 8, 14, 30, 60, 88, 118, 148,
        188, 236, 354, 472, 708, 944, 1890, 3778};

/* Active noise period table; selected by region in InfoNES_pAPUInit(). */
DWORD *ApuNoiseFreq = ApuNoiseFreqNtsc;

/*-------------------------------------------------------------------*/
/* DMC Transfer Clocks Tables (NTSC + PAL)                           */
/*-------------------------------------------------------------------*/
static DWORD __not_in_flash_func(ApuDpcmCyclesNtsc)[16] =
    {
        428, 380, 340, 320, 286, 254, 226, 214,
        190, 160, 142, 128, 106, 85, 72, 54};

static DWORD __not_in_flash_func(ApuDpcmCyclesPal)[16] =
    {
        398, 354, 316, 298, 276, 236, 210, 198,
        176, 148, 132, 118, 98, 78, 66, 50};

/* Active DMC period table; selected by region in InfoNES_pAPUInit(). */
DWORD *ApuDpcmCycles = ApuDpcmCyclesNtsc;

/*===================================================================*/
/*                                                                   */
/*      ApuRenderingWave1() : Rendering Rectangular Wave #1          */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------*/
/* Write registers of rectangular wave #1                            */
/*-------------------------------------------------------------------*/

int __not_in_flash_func(ApuWriteWave1)(int cycles, int event)
{
  /* APU Reg Write Event */
  while ((event < cur_event) && (ApuEventQueue[event].time < cycles))
  {
    if ((ApuEventQueue[event].type & APUET_MASK) == APUET_C1)
    {
      switch (ApuEventQueue[event].type & 0x03)
      {
      case 0:
        ApuC1a = ApuEventQueue[event].data;
        ApuC1Wave = pulse_waves[ApuC1DutyCycle >> 6];
        break;

      case 1:
        ApuC1b = ApuEventQueue[event].data;
        break;

      case 2:
        ApuC1c = ApuEventQueue[event].data;
        ApuC1Freq = ((((WORD)ApuC1d & 0x07) << 8) + ApuC1c);
        ApuC1Atl = ApuAtl[(ApuC1d & 0xf8) >> 3];

        ApuC1Skip = ApuPulseSkip(ApuC1Freq);
        break;

      case 3:
        ApuC1d = ApuEventQueue[event].data;
        ApuC1Freq = ((((WORD)ApuC1d & 0x07) << 8) + ApuC1c);
        ApuC1Atl = ApuAtl[(ApuC1d & 0xf8) >> 3];

        ApuC1Skip = ApuPulseSkip(ApuC1Freq);

        ApuC1EnvVol = 15;
        break;
      }
    }
    else if (ApuEventQueue[event].type == APUET_W_CTRL)
    {
      ApuCtrlNew = ApuEventQueue[event].data;

      if (!(ApuEventQueue[event].data & (1 << 0)))
      {
        ApuC1Atl = 0;
      }
    }
    event++;
  }
  return event;
}

/*-------------------------------------------------------------------*/
/* Rendering rectangular wave #1                                     */
/*-------------------------------------------------------------------*/

void __not_in_flash_func(ApuRenderingWave1)(int n)
{
  ApuCtrlNew = ApuCtrl;
  ApuWriteWave1(ApuCyclesPerSample * (n + 1), 0);

  if ((ApuCtrlNew & 0x01) && ApuC1Atl &&
      !(ApuC1Freq < 8 || (!ApuC1SweepIncDec && ApuC1Freq > ApuC1FreqLimit)))
  {
    auto vol = ApuC1Env ? ApuC1Vol : ApuC1EnvVol;
    for (unsigned int i = 0; i < n; i++)
    {
      /* Wave Rendering */
      ApuC1Index += ApuC1Skip;
      ApuC1Index &= 0x1fffffff;
      wave_buffers[0][i] = ApuC1Wave[ApuC1Index >> 24] * vol;
    }
  }
  else
  {
    memset(wave_buffers[0], 0, 2 * n);
  }
}

/*===================================================================*/
/*                                                                   */
/*      ApuRenderingWave2() : Rendering Rectangular Wave #2          */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------*/
/* Write registers of rectangular wave #2                           */
/*-------------------------------------------------------------------*/

int __not_in_flash_func(ApuWriteWave2)(int cycles, int event)
{
  /* APU Reg Write Event */
  while ((event < cur_event) && (ApuEventQueue[event].time < cycles))
  {
    if ((ApuEventQueue[event].type & APUET_MASK) == APUET_C2)
    {
      switch (ApuEventQueue[event].type & 0x03)
      {
      case 0:
        ApuC2a = ApuEventQueue[event].data;
        ApuC2Wave = pulse_waves[ApuC2DutyCycle >> 6];
        break;

      case 1:
        ApuC2b = ApuEventQueue[event].data;
        break;

      case 2:
        ApuC2c = ApuEventQueue[event].data;
        ApuC2Freq = ((((WORD)ApuC2d & 0x07) << 8) + ApuC2c);
        ApuC2Atl = ApuAtl[(ApuC2d & 0xf8) >> 3];

        ApuC2Skip = ApuPulseSkip(ApuC2Freq);
        break;

      case 3:
        ApuC2d = ApuEventQueue[event].data;
        ApuC2Freq = ((((WORD)ApuC2d & 0x07) << 8) + ApuC2c);
        ApuC2Atl = ApuAtl[(ApuC2d & 0xf8) >> 3];

        ApuC2Skip = ApuPulseSkip(ApuC2Freq);
        ApuC2EnvVol = 15;
        break;
      }
    }
    else if (ApuEventQueue[event].type == APUET_W_CTRL)
    {
      ApuCtrlNew = ApuEventQueue[event].data;

      if (!(ApuEventQueue[event].data & (1 << 1)))
      {
        ApuC2Atl = 0;
      }
    }
    event++;
  }
  return event;
}

/*-------------------------------------------------------------------*/
/* Rendering rectangular wave #2                                     */
/*-------------------------------------------------------------------*/

void __not_in_flash_func(ApuRenderingWave2)(int n)
{
  ApuCtrlNew = ApuCtrl;
  ApuWriteWave2(ApuCyclesPerSample * (n + 1), 0);

  if ((ApuCtrlNew & 0x02) && ApuC2Atl &&
      !(ApuC2Freq < 8 || (!ApuC2SweepIncDec && ApuC2Freq > ApuC2FreqLimit)))
  {
    auto vol = ApuC2Env ? ApuC2Vol : ApuC2EnvVol;
    for (unsigned int i = 0; i < n; i++)
    {
      /* Wave Rendering */
      ApuC2Index += ApuC2Skip;
      ApuC2Index &= 0x1fffffff;
      wave_buffers[1][i] = ApuC2Wave[ApuC2Index >> 24] * vol;
    }
  }
  else
  {
    memset(wave_buffers[1], 0, 2 * n);
  }
}

/*===================================================================*/
/*                                                                   */
/*      ApuRenderingWave3() : Rendering Triangle Wave                */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------*/
/* Write registers of triangle wave #3                              */
/*-------------------------------------------------------------------*/

int __not_in_flash_func(ApuWriteWave3)(int cycles, int event)
{
  /* APU Reg Write Event */
  while ((event < cur_event) && (ApuEventQueue[event].time < cycles))
  {
    if ((ApuEventQueue[event].type & APUET_MASK) == APUET_C3)
    {
      switch (ApuEventQueue[event].type & 3)
      {
      case 0:
        ApuC3a = ApuEventQueue[event].data;
        break;

      case 1:
        ApuC3b = ApuEventQueue[event].data;
        break;

      case 2:
        ApuC3c = ApuEventQueue[event].data;
        if (ApuC3Freq)
        {
          ApuC3Skip = ApuTriangleMagic / ApuC3Freq;
        }
        else
        {
          ApuC3Skip = 0;
        }
        break;

      case 3:
        ApuC3d = ApuEventQueue[event].data;
        ApuC3Atl = ApuC3LengthCounter;
        ApuC3ReloadFlag = true;
        if (ApuC3Freq)
        {
          ApuC3Skip = ApuTriangleMagic / ApuC3Freq;
        }
        else
        {
          ApuC3Skip = 0;
        }
      }
    }
    else if (ApuEventQueue[event].type == APUET_W_CTRL)
    {
      ApuCtrlNew = ApuEventQueue[event].data;

      if (!(ApuEventQueue[event].data & (1 << 2)))
      {
        ApuC3Atl = 0;
        ApuC3Llc = 0;
      }
    }
    event++;
  }
  return event;
}

/*-------------------------------------------------------------------*/
/* Rendering triangle wave #3                                        */
/*-------------------------------------------------------------------*/

void __not_in_flash_func(ApuRenderingWave3)(int n)
{
  ApuCtrlNew = ApuCtrl;
  ApuWriteWave3(ApuCyclesPerSample * (n + 1), 0);

  if ((ApuCtrlNew & 0x04) && ApuC3Atl > 0 && ApuC3Llc > 0 && ApuC3Freq >= 8)
  {
    for (unsigned int i = 0; i < n; i++)
    {
      /* Wave Rendering */
      ApuC3Index += ApuC3Skip;
      ApuC3Index &= 0x1fffffff;
      wave_buffers[2][i] = triangle_50[ApuC3Index >> 24];
    }
  }
  else
  {
    memset(wave_buffers[2], 0, 2 * n);
  }
}

/*===================================================================*/
/*                                                                   */
/*      ApuRenderingWave4() : Rendering Noise                        */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------*/
/* Write registers of noise channel #4                              */
/*-------------------------------------------------------------------*/

int __not_in_flash_func(ApuWriteWave4)(int cycles, int event)
{
  /* APU Reg Write Event */
  while ((event < cur_event) && (ApuEventQueue[event].time < cycles))
  {
    if ((ApuEventQueue[event].type & APUET_MASK) == APUET_C4)
    {
      switch (ApuEventQueue[event].type & 3)
      {
      case 0:
        ApuC4a = ApuEventQueue[event].data;
        break;

      case 1:
        ApuC4b = ApuEventQueue[event].data;
        break;

      case 2:
        ApuC4c = ApuEventQueue[event].data;

        // if (ApuC4Small)
        // {
        //   ApuC4Sr = 0x001f;
        // }
        // else
        // {
        //   ApuC4Sr = 0x01ff;
        // }

        /* Frequency */
        if (ApuC4Freq)
        {
          ApuC4Skip = ApuNoiseMagic / ApuC4Freq;
        }
        else
        {
          ApuC4Skip = 0;
        }
        ApuC4Atl = ApuC4LengthCounter;
        break;

      case 3:
        ApuC4d = ApuEventQueue[event].data;

        /* Frequency */
        if (ApuC4Freq)
        {
          ApuC4Skip = ApuNoiseMagic / ApuC4Freq;
        }
        else
        {
          ApuC4Skip = 0;
        }
        ApuC4Atl = ApuC4LengthCounter;
        ApuC4EnvVol = 15;
      }
    }
    else if (ApuEventQueue[event].type == APUET_W_CTRL)
    {
      ApuCtrlNew = ApuEventQueue[event].data;

      if (!(ApuEventQueue[event].data & (1 << 3)))
      {
        ApuC4Atl = 0;
      }
    }
    event++;
  }
  return event;
}

/*-------------------------------------------------------------------*/
/* Rendering noise channel #4                                        */
/*-------------------------------------------------------------------*/

void __not_in_flash_func(ApuRenderingWave4)(int n)
{
  ApuCtrlNew = ApuCtrl;
  ApuWriteWave4(ApuCyclesPerSample * (n + 1), 0);

  if ((ApuCtrlNew & 0x08) && ApuC4Atl)
  {
    int shift = ApuC4Small ? 6 : 1;
    for (unsigned int i = 0; i < n; i++)
    {
      /* Wave Rendering */
      ApuC4Index += ApuC4Skip;
      if (ApuC4Index > 0xffffff)
      {
        int f = (ApuC4Sr ^ (ApuC4Sr >> shift)) & 1;
        ApuC4Sr = (ApuC4Sr >> 1) | (f << 14);

        ApuC4Index &= 0xffffff;
      }

      if (!(ApuC4Sr & 1))
      {
        if (ApuC4Env)
        {
          wave_buffers[3][i] = ApuC4Vol;
        }
        else
        {
          wave_buffers[3][i] = ApuC4EnvVol;
        }
      }
      else
      {
        wave_buffers[3][i] = 0;
      }
    }
  }
  else
  {
    memset(wave_buffers[3], 0, n << 1);
  }
}

/*===================================================================*/
/*                                                                   */
/*      ApuRenderingWave5() : Rendering DPCM channel #5              */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------*/
/* Write registers of DPCM channel #5                               */
/*-------------------------------------------------------------------*/

int __not_in_flash_func(ApuWriteWave5)(int cycles, int event)
{
  /* APU Reg Write Event */
  while ((event < cur_event) && (ApuEventQueue[event].time < cycles))
  {
    if ((ApuEventQueue[event].type & APUET_MASK) == APUET_C5)
    {
      ApuC5Reg[ApuEventQueue[event].type & 3] = ApuEventQueue[event].data;

      switch (ApuEventQueue[event].type & 3)
      {
      case 0:
        ApuC5Freq = ApuDpcmCycles[(ApuEventQueue[event].data & 0x0F)] << 16;
        ApuC5Looping = ApuEventQueue[event].data & 0x40;
        break;
      case 1:
        ApuC5DpcmValue = (ApuEventQueue[event].data & 0x7F) >> 1;
        break;
      case 2:
        ApuC5CacheAddr = 0xC000 + (WORD)(ApuEventQueue[event].data << 6);
        break;
      case 3:
        ApuC5CacheDmaLength = ((ApuEventQueue[event].data << 4) + 1) << 3;
        break;
      }
    }
    else if (ApuEventQueue[event].type == APUET_W_CTRL)
    {
      ApuCtrlNew = ApuEventQueue[event].data;

      if (!(ApuEventQueue[event].data & (1 << 4)))
      {
        ApuC5Enable = 0;
        ApuC5DmaLength = 0;
      }
      else
      {
        ApuC5Enable = 0xFF;
        if (!ApuC5DmaLength)
        {
          ApuC5Address = ApuC5CacheAddr;
          ApuC5DmaLength = ApuC5CacheDmaLength;
        }
      }
    }
    event++;
  }
  return event;
}

/*-------------------------------------------------------------------*/
/* Rendering DPCM channel #5                                         */
/*-------------------------------------------------------------------*/

void __not_in_flash_func(ApuRenderingWave5)(int n)
{
  ApuCtrlNew = ApuCtrl;
  ApuWriteWave5(ApuCyclesPerSample * (n + 1), 0);

  /* $4011 direct DAC writes affect output regardless of DPCM DMA enable ($4015 bit 4).
     Always output ApuC5DpcmValue; only run DMA engine when enabled and loaded. */
  for (unsigned int i = 0; i < n; i++)
  {
    if ((ApuCtrlNew & 0x10) && ApuC5DmaLength)
    {
      ApuC5Phaseacc -= ApuCycleRate;

      while (ApuC5Phaseacc < 0)
      {
        ApuC5Phaseacc += ApuC5Freq;
        if (!(ApuC5DmaLength & 7))
        {
          ApuC5CurByte = K6502_Read(ApuC5Address);
          if (0xFFFF == ApuC5Address)
            ApuC5Address = 0x8000;
          else
            ApuC5Address++;
        }
        if (!(--ApuC5DmaLength))
        {
          if (ApuC5Looping)
          {
            ApuC5Address = ApuC5CacheAddr;
            ApuC5DmaLength = ApuC5CacheDmaLength;
          }
          else
          {
            ApuC5Enable = 0;
            break;
          }
        }

        // positive delta
        if (ApuC5CurByte & (1 << ((ApuC5DmaLength & 7) ^ 7)))
        {
          if (ApuC5DpcmValue < 0x3F)
            ApuC5DpcmValue += 1;
        }
        else
        {
          // negative delta
          if (ApuC5DpcmValue > 1)
            ApuC5DpcmValue -= 1;
        }
      }
    }

    /* Wave Rendering — always emit current DAC value */
    wave_buffers[4][i] = ApuC5DpcmValue;
  }
}

/*===================================================================*/
/*                                                                   */
/*  ApuRenderingMmc5Pulse1() : Rendering MMC5 Pulse Wave #1          */
/*                                                                   */
/*===================================================================*/

int __not_in_flash_func(ApuWriteMmc5Wave1)(int cycles, int event)
{
  while ((event < cur_event) && (ApuEventQueue[event].time < cycles))
  {
    if ((ApuEventQueue[event].type & APUET_MASK) == APUET_MMC5_P1)
    {
      switch (ApuEventQueue[event].type & 0x03)
      {
      case 0:
        ApuMmc5P1a = ApuEventQueue[event].data;
        ApuMmc5P1Wave = pulse_waves[ApuMmc5P1DutyCycle >> 6];
        break;
      case 2:
        ApuMmc5P1c = ApuEventQueue[event].data;
        ApuMmc5P1Freq = (((WORD)ApuMmc5P1d & 0x07) << 8) + ApuMmc5P1c;
        if (ApuMmc5P1Freq)
        {
          ApuMmc5P1Skip = (ApuPulseMagic << 1) / ApuMmc5P1Freq;
        }
        else
        {
          ApuMmc5P1Skip = 0;
        }
        break;
      case 3:
        ApuMmc5P1d = ApuEventQueue[event].data;
        ApuMmc5P1Freq = (((WORD)ApuMmc5P1d & 0x07) << 8) + ApuMmc5P1c;
        ApuMmc5P1Atl = ApuAtl[(ApuMmc5P1d & 0xf8) >> 3];
        if (ApuMmc5P1Freq)
        {
          ApuMmc5P1Skip = (ApuPulseMagic << 1) / ApuMmc5P1Freq;
        }
        else
        {
          ApuMmc5P1Skip = 0;
        }
        break;
      }
    }
    else if (ApuEventQueue[event].type == APUET_W_MMC5_CTRL)
    {
      ApuMmc5CtrlNew = ApuEventQueue[event].data;
      if (!(ApuEventQueue[event].data & (1 << 0)))
      {
        ApuMmc5P1Atl = 0;
      }
    }
    event++;
  }
  return event;
}

void __not_in_flash_func(ApuRenderingMmc5Pulse1)(int n)
{
  ApuMmc5CtrlNew = ApuMmc5Ctrl;
  ApuWriteMmc5Wave1(ApuCyclesPerSample * (n + 1), 0);

  if ((ApuMmc5CtrlNew & 0x01) && (ApuMmc5P1Atl || ApuMmc5P1Hold))
  {
    auto vol = ApuMmc5P1Env ? ApuMmc5P1Vol : ApuMmc5P1EnvVol;
    for (unsigned int i = 0; i < n; i++)
    {
      ApuMmc5P1Index += ApuMmc5P1Skip;
      ApuMmc5P1Index &= 0x1fffffff;
      mmc5_wave_buffers[0][i] = ApuMmc5P1Wave[ApuMmc5P1Index >> 24] * vol;
    }
  }
  else
  {
    memset(mmc5_wave_buffers[0], 0, n);
  }
}

/*===================================================================*/
/*                                                                   */
/*  ApuRenderingMmc5Pulse2() : Rendering MMC5 Pulse Wave #2          */
/*                                                                   */
/*===================================================================*/

int __not_in_flash_func(ApuWriteMmc5Wave2)(int cycles, int event)
{
  while ((event < cur_event) && (ApuEventQueue[event].time < cycles))
  {
    if ((ApuEventQueue[event].type & APUET_MASK) == APUET_MMC5_P2)
    {
      switch (ApuEventQueue[event].type & 0x03)
      {
      case 0:
        ApuMmc5P2a = ApuEventQueue[event].data;
        ApuMmc5P2Wave = pulse_waves[ApuMmc5P2DutyCycle >> 6];
        break;
      case 2:
        ApuMmc5P2c = ApuEventQueue[event].data;
        ApuMmc5P2Freq = (((WORD)ApuMmc5P2d & 0x07) << 8) + ApuMmc5P2c;
        if (ApuMmc5P2Freq)
        {
          ApuMmc5P2Skip = (ApuPulseMagic << 1) / ApuMmc5P2Freq;
        }
        else
        {
          ApuMmc5P2Skip = 0;
        }
        break;
      case 3:
        ApuMmc5P2d = ApuEventQueue[event].data;
        ApuMmc5P2Freq = (((WORD)ApuMmc5P2d & 0x07) << 8) + ApuMmc5P2c;
        ApuMmc5P2Atl = ApuAtl[(ApuMmc5P2d & 0xf8) >> 3];
        if (ApuMmc5P2Freq)
        {
          ApuMmc5P2Skip = (ApuPulseMagic << 1) / ApuMmc5P2Freq;
        }
        else
        {
          ApuMmc5P2Skip = 0;
        }
        break;
      }
    }
    else if (ApuEventQueue[event].type == APUET_W_MMC5_CTRL)
    {
      ApuMmc5CtrlNew = ApuEventQueue[event].data;
      if (!(ApuEventQueue[event].data & (1 << 1)))
      {
        ApuMmc5P2Atl = 0;
      }
    }
    event++;
  }
  return event;
}

void __not_in_flash_func(ApuRenderingMmc5Pulse2)(int n)
{
  ApuMmc5CtrlNew = ApuMmc5Ctrl;
  ApuWriteMmc5Wave2(ApuCyclesPerSample * (n + 1), 0);

  if ((ApuMmc5CtrlNew & 0x02) && (ApuMmc5P2Atl || ApuMmc5P2Hold))
  {
    auto vol = ApuMmc5P2Env ? ApuMmc5P2Vol : ApuMmc5P2EnvVol;
    for (unsigned int i = 0; i < n; i++)
    {
      ApuMmc5P2Index += ApuMmc5P2Skip;
      ApuMmc5P2Index &= 0x1fffffff;
      mmc5_wave_buffers[1][i] = ApuMmc5P2Wave[ApuMmc5P2Index >> 24] * vol;
    }
  }
  else
  {
    memset(mmc5_wave_buffers[1], 0, n);
  }
}

/*===================================================================*/
/*                                                                   */
/*  ApuRenderingVrc6Pulse1() : Rendering VRC6 Pulse Wave #1          */
/*                                                                   */
/*===================================================================*/

int __not_in_flash_func(ApuWriteVrc6Wave1)(int cycles, int event)
{
    while ((event < cur_event) && (ApuEventQueue[event].time < cycles))
    {
      if ((ApuEventQueue[event].type & APUET_MASK) == APUET_VRC6_P1)
      {
        switch (ApuEventQueue[event].type & 0x03)
        {
        case 0:  /* $9000: MDDDVVVV */
          ApuVrc6P1a = ApuEventQueue[event].data;
          break;
        case 1:  /* $9001: freq low */
          ApuVrc6P1b = ApuEventQueue[event].data;
          {
            DWORD freq = (((DWORD)(ApuVrc6P1c & 0x0F) << 8) | ApuVrc6P1b);
            if (ApuVrc6FreqCtrl & 0x04) freq >>= 8;
            else if (ApuVrc6FreqCtrl & 0x02) freq >>= 4;
            ApuVrc6P1Skip = freq ? (ApuPulseMagic << 1) / freq : 0;
          }
          break;
        case 2:  /* $9002: E...FFFF */
          ApuVrc6P1c = ApuEventQueue[event].data;
          if (!(ApuVrc6P1c & 0x80))
          {
            ApuVrc6P1Index = 0;
          }
          {
            DWORD freq = (((DWORD)(ApuVrc6P1c & 0x0F) << 8) | ApuVrc6P1b);
            if (ApuVrc6FreqCtrl & 0x04) freq >>= 8;
            else if (ApuVrc6FreqCtrl & 0x02) freq >>= 4;
            ApuVrc6P1Skip = freq ? (ApuPulseMagic << 1) / freq : 0;
          }
          break;
        }
      }
      else if (ApuEventQueue[event].type == APUET_W_VRC6_FREQ)
      {
        ApuVrc6FreqCtrl = ApuEventQueue[event].data;
        DWORD freq = (((DWORD)(ApuVrc6P1c & 0x0F) << 8) | ApuVrc6P1b);
        if (ApuVrc6FreqCtrl & 0x04) freq >>= 8;
        else if (ApuVrc6FreqCtrl & 0x02) freq >>= 4;
        ApuVrc6P1Skip = freq ? (ApuPulseMagic << 1) / freq : 0;
      }
      event++;
    }
    return event;
}

void __not_in_flash_func(ApuRenderingVrc6Pulse1)(int n)
{
  ApuWriteVrc6Wave1(ApuCyclesPerSample * (n + 1), 0);

  /* Halted by $9003 bit 0: leave channel silent for this sync. */
  if (ApuVrc6FreqCtrl & 0x01)
  {
    memset(vrc6_wave_buffers[0], 0, n);
    return;
  }

  /* Disabled by $9002 bit 7. */
  if (!(ApuVrc6P1c & 0x80))
  {
    memset(vrc6_wave_buffers[0], 0, n);
    return;
  }

  /* Hoist invariants — these only change when an event reprograms $9000. */
  const BYTE vol  = ApuVrc6P1a & 0x0F;
  const BYTE duty = (ApuVrc6P1a >> 4) & 0x07;
  const BYTE mode = ApuVrc6P1a >> 7;
  const BYTE volScaled = vol * 17;  /* scale 4-bit to 0..255 */

  for (unsigned int i = 0; i < n; i++)
  {
    ApuVrc6P1Index += ApuVrc6P1Skip;
    ApuVrc6P1Index &= 0x1fffffff;

    BYTE step = ApuVrc6P1Index >> 25;  /* 0..15 */

    vrc6_wave_buffers[0][i] = (mode || step >= (15 - duty)) ? volScaled : 0;
  }
}

/*===================================================================*/
/*                                                                   */
/*  ApuRenderingVrc6Pulse2() : Rendering VRC6 Pulse Wave #2          */
/*                                                                   */
/*===================================================================*/

int __not_in_flash_func(ApuWriteVrc6Wave2)(int cycles, int event)
{
    while ((event < cur_event) && (ApuEventQueue[event].time < cycles))
    {
      if ((ApuEventQueue[event].type & APUET_MASK) == APUET_VRC6_P2)
      {
        switch (ApuEventQueue[event].type & 0x03)
        {
        case 0:  /* $A000: MDDDVVVV */
          ApuVrc6P2a = ApuEventQueue[event].data;
          break;
        case 1:  /* $A001: freq low */
          ApuVrc6P2b = ApuEventQueue[event].data;
          {
            DWORD freq = (((DWORD)(ApuVrc6P2c & 0x0F) << 8) | ApuVrc6P2b);
            if (ApuVrc6FreqCtrl & 0x04) freq >>= 8;
            else if (ApuVrc6FreqCtrl & 0x02) freq >>= 4;
            ApuVrc6P2Skip = freq ? (ApuPulseMagic << 1) / freq : 0;
          }
          break;
        case 2:  /* $A002: E...FFFF */
          ApuVrc6P2c = ApuEventQueue[event].data;
          if (!(ApuVrc6P2c & 0x80))
          {
            ApuVrc6P2Index = 0;
          }
          {
            DWORD freq = (((DWORD)(ApuVrc6P2c & 0x0F) << 8) | ApuVrc6P2b);
            if (ApuVrc6FreqCtrl & 0x04) freq >>= 8;
            else if (ApuVrc6FreqCtrl & 0x02) freq >>= 4;
            ApuVrc6P2Skip = freq ? (ApuPulseMagic << 1) / freq : 0;
          }
          break;
        }
      }
      else if (ApuEventQueue[event].type == APUET_W_VRC6_FREQ)
      {
        ApuVrc6FreqCtrl = ApuEventQueue[event].data;
        DWORD freq = (((DWORD)(ApuVrc6P2c & 0x0F) << 8) | ApuVrc6P2b);
        if (ApuVrc6FreqCtrl & 0x04) freq >>= 8;
        else if (ApuVrc6FreqCtrl & 0x02) freq >>= 4;
        ApuVrc6P2Skip = freq ? (ApuPulseMagic << 1) / freq : 0;
      }
      event++;
    }
    return event;
}

void __not_in_flash_func(ApuRenderingVrc6Pulse2)(int n)
{
  ApuWriteVrc6Wave2(ApuCyclesPerSample * (n + 1), 0);

  if (ApuVrc6FreqCtrl & 0x01)
  {
    memset(vrc6_wave_buffers[1], 0, n);
    return;
  }

  if (!(ApuVrc6P2c & 0x80))
  {
    memset(vrc6_wave_buffers[1], 0, n);
    return;
  }

  const BYTE vol  = ApuVrc6P2a & 0x0F;
  const BYTE duty = (ApuVrc6P2a >> 4) & 0x07;
  const BYTE mode = ApuVrc6P2a >> 7;
  const BYTE volScaled = vol * 17;

  for (unsigned int i = 0; i < n; i++)
  {
    ApuVrc6P2Index += ApuVrc6P2Skip;
    ApuVrc6P2Index &= 0x1fffffff;

    BYTE step = ApuVrc6P2Index >> 25;

    vrc6_wave_buffers[1][i] = (mode || step >= (15 - duty)) ? volScaled : 0;
  }
}

/*===================================================================*/
/*                                                                   */
/*  ApuRenderingVrc6Saw() : Rendering VRC6 Sawtooth Wave             */
/*                                                                   */
/*===================================================================*/

int __not_in_flash_func(ApuWriteVrc6SawWave)(int cycles, int event)
{
    while ((event < cur_event) && (ApuEventQueue[event].time < cycles))
    {
      if ((ApuEventQueue[event].type & APUET_MASK) == APUET_VRC6_SAW)
      {
        switch (ApuEventQueue[event].type & 0x03)
        {
        case 0:  /* $B000: ..AAAAAA */
          ApuVrc6SawRate = ApuEventQueue[event].data & 0x3F;
          break;
        case 1:  /* $B001: freq low */
          ApuVrc6SawFreqL = ApuEventQueue[event].data;
          break;
        case 2:  /* $B002: E...FFFF */
          ApuVrc6SawFreqH = ApuEventQueue[event].data;
          if (!(ApuVrc6SawFreqH & 0x80))
          {
            ApuVrc6SawAccum = 0;
            ApuVrc6SawStep = 0;
          }
          break;
        }
      }
      else if (ApuEventQueue[event].type == APUET_W_VRC6_FREQ)
      {
        ApuVrc6FreqCtrl = ApuEventQueue[event].data;
      }
      event++;
    }
    return event;
}

void __not_in_flash_func(ApuRenderingVrc6Saw)(int n)
{
  ApuWriteVrc6SawWave(ApuCyclesPerSample * (n + 1), 0);

  /* Halted: hold the current accumulator value across the sync. */
  if (ApuVrc6FreqCtrl & 0x01)
  {
    BYTE held = (ApuVrc6SawAccum >> 3) * 8;
    memset(vrc6_wave_buffers[2], held, n);
    return;
  }

  /* Disabled: silence. */
  if (!(ApuVrc6SawFreqH & 0x80))
  {
    memset(vrc6_wave_buffers[2], 0, n);
    return;
  }

  /* Hoist the divider period — only changes on writes to $B001/$B002 or
     $9003 (freq-ctrl), all of which are processed by ApuWriteVrc6SawWave
     before the per-sample loop runs. */
  DWORD freq_reg = (((DWORD)(ApuVrc6SawFreqH & 0x0F) << 8) | ApuVrc6SawFreqL);
  if (ApuVrc6FreqCtrl & 0x04) freq_reg >>= 8;
  else if (ApuVrc6FreqCtrl & 0x02) freq_reg >>= 4;
  const int period = (int)freq_reg + 1;
  const int decrement = (int)ApuCyclesPerSample;

  for (unsigned int i = 0; i < n; i++)
  {
    /* Advance divider - one divider tick per (period) CPU cycles */
    ApuVrc6SawPhaseAcc -= decrement;
    while (ApuVrc6SawPhaseAcc < 0)
    {
      ApuVrc6SawPhaseAcc += period;
      ApuVrc6SawStep++;
      if (ApuVrc6SawStep >= 14)
      {
        ApuVrc6SawStep = 0;
        ApuVrc6SawAccum = 0;
      }
      else if ((ApuVrc6SawStep & 1) == 0)
      {
        ApuVrc6SawAccum = (ApuVrc6SawAccum + ApuVrc6SawRate) & 0xFF;
      }
    }

    /* Output high 5 bits, scaled to 0..248 */
    vrc6_wave_buffers[2][i] = (ApuVrc6SawAccum >> 3) * 8;
  }
}

/*===================================================================*/
/*                                                                   */
/*  Sunsoft 5B — phase-increment helpers                             */
/*                                                                   */
/*  Tone/noise/envelope clocks all share the same chip divisor so    */
/*  each uses `(ApuPulseMagic << 1) / period`, matching the NES APU  */
/*  pulse divisor. Envelope period is 16-bit; tone is 12-bit; noise  */
/*  is 5-bit.                                                        */
/*===================================================================*/
static void ApuS5B_UpdateToneSkip(int ch)
{
  DWORD period = ((DWORD)(ApuS5B_Reg[ch * 2 + 1] & 0x0F) << 8)
               |  (DWORD) ApuS5B_Reg[ch * 2];
  ApuS5B_Skip[ch] = period ? (ApuPulseMagic << 1) / period : 0;
}

static void ApuS5B_UpdateNoiseSkip(void)
{
  DWORD period = ApuS5B_Reg[0x06] & 0x1F;
  ApuS5B_NoiseSkip = period ? (ApuPulseMagic << 1) / period : 0;
}

static void ApuS5B_UpdateEnvSkip(void)
{
  DWORD period = ((DWORD)ApuS5B_Reg[0x0C] << 8) | (DWORD)ApuS5B_Reg[0x0B];
  /* Envelope step clock is f_in/256 on a YM2149 (f_in/128 on Sunsoft 5B,
   * which ties /SEL low), whereas the tone clock is f_in/16 (f_in/8 on 5B).
   * That makes the envelope ~32× slower than a tone at the same period
   * register value. Using the tone skip formula directly produced audible
   * envelopes of ~7 ms per cycle — effectively a click — so the peak
   * amplitude was imperceptible. The `<< 5` divisor here slows the
   * envelope accumulator by 32× to match real-hardware proportions. */
  ApuS5B_EnvSkip = period ? (ApuPulseMagic << 1) / (period << 5) : 0;
}

/*  Retrigger the envelope generator on a write to $0D (shape reg). */
static void ApuS5B_RetriggerEnvelope(BYTE shape)
{
  ApuS5B_EnvShape = shape & 0x0F;
  ApuS5B_EnvIndex = 0;
  ApuS5B_EnvStep  = 0;
  ApuS5B_EnvHold  = 0;
  /* Attack bit (shape bit 2): 0 = decay-first (start high), 1 = attack-first (start low). */
  ApuS5B_EnvDecay = (shape & 0x04) ? 0 : 1;
}

/*===================================================================*/
/*                                                                   */
/*  ApuWriteSunsoft5B() : register file write (from mapper 69)       */
/*                                                                   */
/*===================================================================*/
void __not_in_flash_func(ApuWriteSunsoft5B)(BYTE reg, BYTE value)
{
  if (!ApuSunsoft5BEnable) return;

  reg &= 0x0F;
  ApuS5B_Reg[reg] = value;

  switch (reg)
  {
    case 0x00: case 0x01: ApuS5B_UpdateToneSkip(0); break;
    case 0x02: case 0x03: ApuS5B_UpdateToneSkip(1); break;
    case 0x04: case 0x05: ApuS5B_UpdateToneSkip(2); break;
    case 0x06:            ApuS5B_UpdateNoiseSkip(); break;
    case 0x0B: case 0x0C: ApuS5B_UpdateEnvSkip();   break;
    case 0x0D:            ApuS5B_RetriggerEnvelope(value); break;
    /* $07 mixer, $08-$0A volumes, $0E-$0F I/O — read directly each sample */
    default: break;
  }
}

/*===================================================================*/
/*                                                                   */
/*  Envelope — advance by one sample, step the 5-bit counter when    */
/*  the phase accumulator crosses a 1/32 boundary. On wrap, consult  */
/*  the shape bits to decide: stop, continue, alternate, or hold.    */
/*===================================================================*/
static inline BYTE ApuS5B_EnvelopeOutput(void)
{
  /* EnvStep 0..31, inverted by Decay flag → logical "0..31 attack" */
  BYTE step = ApuS5B_EnvDecay ? (ApuS5B_EnvStep ^ 0x1F) : ApuS5B_EnvStep;
  return step >> 1;                                 /* 0..15 */
}

static inline void ApuS5B_AdvanceEnvelope(void)
{
  if (ApuS5B_EnvHold) return;

  DWORD prev = ApuS5B_EnvIndex;
  ApuS5B_EnvIndex += ApuS5B_EnvSkip;

  /* 32 envelope steps per full accumulator cycle → 2^27 per step. */
  BYTE newStep  = (ApuS5B_EnvIndex >> 27) & 0x1F;
  BYTE prevStep = (prev             >> 27) & 0x1F;

  if (newStep == prevStep) return;

  /* End-of-cycle when the step counter wraps down (e.g. 31 → 0). */
  if (newStep < prevStep)
  {
    BYTE shape = ApuS5B_EnvShape;
    BYTE cont  = shape & 0x08;  /* bit 3: continue */
    BYTE alt   = shape & 0x02;  /* bit 1: alternate */
    BYTE hold  = shape & 0x01;  /* bit 0: hold      */

    if (!cont)
    {
      /* Shapes 0-7: one pass, then silence (volume 0). */
      ApuS5B_EnvHold  = 1;
      ApuS5B_EnvDecay = 1;
      ApuS5B_EnvStep  = 0x1F;   /* output = (0x1F ^ 0x1F) >> 1 = 0 */
      return;
    }
    if (hold)
    {
      /* Shapes with Hold set: freeze at the endpoint. Alt flips decay
       * once to invert the held level. */
      ApuS5B_EnvHold = 1;
      if (alt) ApuS5B_EnvDecay ^= 1;
      ApuS5B_EnvStep = 0x1F;
      return;
    }
    /* Continue without hold: Alt toggles direction (triangle), else repeats (saw). */
    if (alt) ApuS5B_EnvDecay ^= 1;
  }
  ApuS5B_EnvStep = newStep;
}

/*===================================================================*/
/*                                                                   */
/*  Noise LFSR — 17-bit, taps bit0 ^ bit3. Clock it on each top-bit  */
/*  transition of the noise phase accumulator.                       */
/*===================================================================*/
static inline void ApuS5B_AdvanceNoise(void)
{
  DWORD prev = ApuS5B_NoiseIndex;
  ApuS5B_NoiseIndex += ApuS5B_NoiseSkip;
  if ((ApuS5B_NoiseIndex ^ prev) & 0x80000000u)
  {
    DWORD bit = (ApuS5B_NoiseLfsr ^ (ApuS5B_NoiseLfsr >> 3)) & 1;
    ApuS5B_NoiseLfsr = (ApuS5B_NoiseLfsr >> 1) | (bit << 16);
    ApuS5B_NoiseOut  = ApuS5B_NoiseLfsr & 1;
  }
}

/*===================================================================*/
/*                                                                   */
/*  ApuRenderingSunsoft5B() : render all 3 tone channels              */
/*                                                                   */
/*  Noise & envelope are shared across channels, so we advance them  */
/*  once per sample inside a single combined loop rather than having */
/*  3 per-channel render functions.                                  */
/*===================================================================*/
void __not_in_flash_func(ApuRenderingSunsoft5B)(int n)
{
  const BYTE mixer = ApuS5B_Reg[0x07];

  for (int i = 0; i < n; i++)
  {
    ApuS5B_AdvanceEnvelope();
    ApuS5B_AdvanceNoise();

    /* Envelope amplitude uses a LINEAR mapping (0..15 → 0..255). The log LUT
     * would leave envelope-driven SFX inaudible because decay envelopes spend
     * most of the cycle in the log LUT's near-zero region. Fixed-volume output
     * (below) keeps the log LUT, which is correct for YM2149 tones and
     * preserves music character: Gimmick's music uses mid-range fixed volumes
     * for harmony lines, which logarithmic scaling maps to low amplitudes —
     * exactly how real hardware attenuates them. Using linear for fixed-vol
     * would over-drive those harmony notes and distort the music. */
    BYTE env_vol = ApuS5B_EnvelopeOutput() * 17;

    for (int ch = 0; ch < 3; ch++)
    {
      /* Mixer bits: 1 = disabled. Tones in 0-2, noise in 3-5. */
      BYTE tone_dis  = (mixer >> ch)       & 1;
      BYTE noise_dis = (mixer >> (ch + 3)) & 1;

      ApuS5B_Index[ch] += ApuS5B_Skip[ch];
      BYTE tone_bit  = (ApuS5B_Index[ch] >> 31) & 1;
      BYTE noise_bit = ApuS5B_NoiseOut;

      /* YM2149 channel gate: (tone | tone_disabled) AND (noise | noise_disabled).
       * If both sources are disabled the channel is effectively DC-high, letting
       * the envelope generator drive the amplitude directly. */
      BYTE gated = (tone_bit | tone_dis) & (noise_bit | noise_dis);

      BYTE vol_reg = ApuS5B_Reg[0x08 + ch];
      BYTE vol = (vol_reg & 0x10) ? env_vol
                                  : ApuS5B_VolTable[vol_reg & 0x0F];

      s5b_wave_buffers[ch][i] = gated ? vol : 0;
    }
  }
}

/*===================================================================*/
/*                                                                   */
/*     InfoNES_pApuVsync() : Callback Function per Vsync             */
/*                                                                   */
/*===================================================================*/

void InfoNES_pAPUVsync()
{
  if (ApuC1Atl && !ApuC1Hold)
  {
    ApuC1Atl--;
  }

  /* Envelope decay at a rate of ( Envelope Delay + 1 ) / 240 secs */
  ApuC1EnvPhase -= 4;
  while (ApuC1EnvPhase < 0)
  {
    ApuC1EnvPhase += ApuC1EnvDelay;

    if (ApuC1Hold)
    {
      ApuC1EnvVol = (ApuC1EnvVol - 1) & 0x0f;
    }
    else if (ApuC1EnvVol > 0)
    {
      --ApuC1EnvVol;
    }
  }

  /* Frequency sweeping at a rate of ( Sweep Delay + 1) / 120 secs */
  if (ApuC1SweepOn && ApuC1SweepShifts)
  {
    ApuC1SweepPhase -= 2; /* 120/60 */
    while (ApuC1SweepPhase < 0)
    {
      ApuC1SweepPhase += ApuC1SweepDelay;

      if (ApuC1SweepIncDec) /* ramp up */
      {
        /* Rectangular #1 */
        ApuC1Freq += ~(ApuC1Freq >> ApuC1SweepShifts);
      }
      else
      {
        /* ramp down */
        ApuC1Freq += (ApuC1Freq >> ApuC1SweepShifts);
      }

      ApuC1Skip = ApuPulseSkip(ApuC1Freq);
    }
  }

  if (ApuC2Atl && !ApuC2Hold)
  {
    ApuC2Atl--;
  }

  /* Envelope decay at a rate of ( Envelope Delay + 1 ) / 240 secs */
  ApuC2EnvPhase -= 4;
  while (ApuC2EnvPhase < 0)
  {
    ApuC2EnvPhase += ApuC2EnvDelay;

    if (ApuC2Hold)
    {
      ApuC2EnvVol = (ApuC2EnvVol - 1) & 0x0f;
    }
    else if (ApuC2EnvVol > 0)
    {
      --ApuC2EnvVol;
    }
  }
  //  printf("C2:%d:%d:%d\n", ApuC2Env, ApuC2EnvPhase, ApuC2EnvVol);

  /* Frequency sweeping at a rate of ( Sweep Delay + 1) / 120 secs */
  if (ApuC2SweepOn && ApuC2SweepShifts)
  {
    ApuC2SweepPhase -= 2; /* 120/60 */
    while (ApuC2SweepPhase < 0)
    {
      ApuC2SweepPhase += ApuC2SweepDelay;

      if (ApuC2SweepIncDec) /* ramp up */
      {
        /* Rectangular #2 */
        ApuC2Freq -= (ApuC2Freq >> ApuC2SweepShifts);
      }
      else
      {
        /* ramp down */
        ApuC2Freq += (ApuC2Freq >> ApuC2SweepShifts);
      }
      ApuC2Skip = ApuPulseSkip(ApuC2Freq);
    }
  }

  if (ApuC3ReloadFlag)
  {
    ApuC3Llc = ApuC3LinearLength;
  }
  else
  {
    if (ApuC3Llc > 0)
    {
      ApuC3Llc = std::max<int>(0, (int)ApuC3Llc - 4 * 64);
    }
  }
  if (!ApuC3Holdnote)
  {
    ApuC3ReloadFlag = false;
  }

  if (ApuC3Atl > 0 && !ApuC3Holdnote)
  {
    ApuC3Atl--;
  }
  // printf("3: %d, %d, %d, %d\n", ApuC3Atl, ApuC3Llc, ApuC3Freq, ApuC3Holdnote);

  if (ApuC4Atl && !ApuC4Hold)
  {
    ApuC4Atl--;
  }

  /* Envelope decay at a rate of ( Envelope Delay + 1 ) / 240 secs */
  ApuC4EnvPhase -= 4;
  while (ApuC4EnvPhase < 0)
  {
    ApuC4EnvPhase += ApuC4EnvDelay;

    if (ApuC4Hold)
    {
      ApuC4EnvVol = (ApuC4EnvVol - 1) & 0x0f;
    }
    else if (ApuC4EnvVol > 0)
    {
      --ApuC4EnvVol;
    }
  }
  // printf("C2: %02x %02x %02x %02x: %d %d\n", ApuC2a, ApuC2b, ApuC2c, ApuC2d, ApuC2Atl, ApuC2EnvVol);
  // printf("C4: %02x  %02x %02x: %d %d\n", ApuC4a, ApuC4c, ApuC4d, ApuC4Atl, ApuC4EnvVol);
  // printf("C5: %02x %02x %02x %02x, lp%d, v%02x, %04x, %d\n",
  //        ApuC5Reg[0], ApuC5Reg[1], ApuC5Reg[2], ApuC5Reg[3],
  //        ApuC5Looping, ApuC5DpcmValue, ApuC5Address, ApuC5DmaLength);

  /*-------------------------------------------------------------------*/
  /*  MMC5 Pulse envelope decay and length counter                     */
  /*-------------------------------------------------------------------*/
  if (ApuMmc5Enable)
  {
    /* MMC5 Pulse 1 envelope decay */
    ApuMmc5P1EnvPhase -= 4;
    while (ApuMmc5P1EnvPhase < 0)
    {
      ApuMmc5P1EnvPhase += ApuMmc5P1EnvDelay;
      if (ApuMmc5P1Hold)
      {
        ApuMmc5P1EnvVol = (ApuMmc5P1EnvVol + 1) & 0x0f;
      }
      else if (ApuMmc5P1EnvVol < 0x0f)
      {
        ApuMmc5P1EnvVol++;
      }
    }
    /* MMC5 Pulse 1 length counter */
    if (ApuMmc5P1Atl > 1) { ApuMmc5P1Atl -= 2; } else { ApuMmc5P1Atl = 0; }

    /* MMC5 Pulse 2 envelope decay */
    ApuMmc5P2EnvPhase -= 4;
    while (ApuMmc5P2EnvPhase < 0)
    {
      ApuMmc5P2EnvPhase += ApuMmc5P2EnvDelay;
      if (ApuMmc5P2Hold)
      {
        ApuMmc5P2EnvVol = (ApuMmc5P2EnvVol + 1) & 0x0f;
      }
      else if (ApuMmc5P2EnvVol < 0x0f)
      {
        ApuMmc5P2EnvVol++;
      }
    }
    /* MMC5 Pulse 2 length counter */
    if (ApuMmc5P2Atl > 1) { ApuMmc5P2Atl -= 2; } else { ApuMmc5P2Atl = 0; }
  }
}

/*===================================================================*/
/*                                                                   */
/*     InfoNES_pApuHsync() : Callback Function per Hsync             */
/*                                                                   */
/*===================================================================*/

uint32_t leftSamples16 = 0;

void __not_in_flash_func(InfoNES_pAPUHsync)(bool enabled)
{
  auto n16 = ApuSamplesPerSync16 + leftSamples16;
  auto n = n16 >> 16;
  leftSamples16 = n16 - (n << 16);

  int bufferLeft = InfoNES_GetSoundBufferSize();
  n = std::min<int>(bufferLeft, n);

  /* In NSF mode, skip audio rendering when playback is stopped — otherwise
     expansion-chip oscillators (VRC6 in particular) keep regenerating their
     buffers from preserved phase state and the last tone bleeds through B's
     stop until a new track starts. The else-branch zeros every output buffer
     SoundOutput touches so stale samples can't leak via wave6 either. */
  extern bool NsfIsPlaying;
  if (enabled && (!IsNSF || NsfIsPlaying))
  {
    ApuRenderingWave1(n);
    ApuRenderingWave2(n);
    ApuRenderingWave3(n);
    ApuRenderingWave4(n);
    ApuRenderingWave5(n);
    ApuCtrl = ApuCtrlNew;
#if PICO_RP2350
    /* Render and mix MMC5 expansion audio */
    if (ApuMmc5Enable)
    {
      ApuRenderingMmc5Pulse1(n);
      ApuRenderingMmc5Pulse2(n);
      ApuMmc5Ctrl = ApuMmc5CtrlNew;

      for (unsigned int i = 0; i < n; i++)
      {
        int mmc5_mix = (mmc5_wave_buffers[0][i] + mmc5_wave_buffers[1][i] + ApuMmc5PcmValue) / 3;
        int combined = wave_buffers[0][i] + mmc5_mix;
        wave_buffers[0][i] = (combined > 255) ? 255 : combined;
      }
    }
#endif
    /* Render and mix VRC6 expansion audio */
    if (ApuVrc6Enable)
    {
      ApuRenderingVrc6Pulse1(n);
      ApuRenderingVrc6Pulse2(n);
      ApuRenderingVrc6Saw(n);

      for (unsigned int i = 0; i < n; i++)
      {
        vrc6_wave_buffers[0][i] = (vrc6_wave_buffers[0][i]
                                 + vrc6_wave_buffers[1][i]
                                 + vrc6_wave_buffers[2][i] * 2) / 4;
      }
    }

    /* Render and mix FDS expansion audio (wavetable + modulation).
     * FDS audio is passed as the wave6 channel (shared with S5B, which
     * is mutually exclusive since they're different mappers). This
     * ensures FDS audio is mixed equally into L and R (mono) rather
     * than being biased toward the pulse-1 stereo side. */
    if (ApuFdsEnable)
    {
      fdsRenderAudio(n);
    }

    /* Render Sunsoft 5B expansion audio (Mapper 69 — Gimmick!, Hebereke) into
     * its OWN output buffer.
     *
     * Use /3 averaging (matching VRC6/MMC5) rather than a straight sum with
     * clip-to-255. The straight sum + hard clip audibly distorts music when
     * Gimmick drives all three tone channels simultaneously (sum 400-700
     * clipping to 255 = heavy harmonic distortion). /3 averaging keeps the
     * buffer in 0..255 without any clipping and a cleaner mix in
     * InfoNES_SoundOutput compensates with a larger per-sample gain. */
    if (ApuSunsoft5BEnable)
    {
      ApuRenderingSunsoft5B(n);

      for (unsigned int i = 0; i < n; i++)
      {
        s5b_wave_buffers[0][i] = (s5b_wave_buffers[0][i]
                                + s5b_wave_buffers[1][i]
                                + s5b_wave_buffers[2][i]) / 3;
      }
    }
#if PICO_RP2350
    /* VRC7 (Konami OPLL — Lagrange Point, Tiny Toon 2 JP). Synth writes
     * its 6-channel mix straight to vrc7_wave_buffer in 0..255 BYTE
     * form, so no further mixing pass is needed here.                  */
    if (ApuVrc7Enable)
    {
      ApuRenderingVrc7(n);
    }
#endif
  }
  else
  {
    memset(&wave_buffers[0][0], 0, n);
    memset(&wave_buffers[1][0], 0, n);
    memset(&wave_buffers[2][0], 0, n);
    memset(&wave_buffers[3][0], 0, n);
    memset(&wave_buffers[4][0], 0, n);
    /* SoundOutput's wave6 reads s5b_wave_buffers[0] or fds_wave_buffer
       depending on which expansion is enabled — zero those too so stopped
       NSF (or muted APU) doesn't bleed expansion audio into the output. */
    if (vrc6_wave_buffers) memset(&vrc6_wave_buffers[0][0], 0, n);
    if (s5b_wave_buffers) memset(&s5b_wave_buffers[0][0], 0, n);
    if (fds_wave_buffer)  memset(fds_wave_buffer, 0, n);
#if PICO_RP2350
    if (vrc7_wave_buffer) memset(vrc7_wave_buffer, 0x80, n);
#endif
  }

  InfoNES_SoundOutput(n,
                      wave_buffers[0], wave_buffers[1], wave_buffers[2],
                      wave_buffers[3], wave_buffers[4],
                      ApuVrc6Enable ? vrc6_wave_buffers[0] :
                      (ApuSunsoft5BEnable ? s5b_wave_buffers[0] :
                      (ApuFdsEnable ? fds_wave_buffer :
#if PICO_RP2350
                      (ApuVrc7Enable ? vrc7_wave_buffer : nullptr)
#else
                      nullptr
#endif
                      )));


  entertime = getPassedClocks();
  cur_event = 0;
}

/*===================================================================*/
/*                                                                   */
/*            InfoNES_pApuInit() : Initialize pApu                   */
/*                                                                   */
/*===================================================================*/

void InfoNES_pAPUInit(void)
{
  /* Sound Hardware Init */
  InfoNES_SoundInit();

  ApuQuality = pAPU_QUALITY - 1; // 1: 22050, 2: 44100 [samples/sec]

  /* Pick the per-region quality table and period tables. */
  ApuQualityData_t *qual = InfoNES_IsPal() ? ApuQualPal : ApuQual;
  ApuNoiseFreq        = InfoNES_IsPal() ? ApuNoiseFreqPal  : ApuNoiseFreqNtsc;
  ApuDpcmCycles       = InfoNES_IsPal() ? ApuDpcmCyclesPal : ApuDpcmCyclesNtsc;

  ApuPulseMagic = qual[ApuQuality].pulse_magic;
  ApuTriangleMagic = qual[ApuQuality].triangle_magic;
  ApuNoiseMagic = qual[ApuQuality].noise_magic;
  ApuSamplesPerSync16 = qual[ApuQuality].samples_per_sync_16;
  ApuCyclesPerSample = qual[ApuQuality].cycles_per_sample;
  ApuSampleRate = qual[ApuQuality].sample_rate;
  ApuCycleRate = qual[ApuQuality].cycle_rate;

  InfoNES_SoundOpen((ApuSamplesPerSync16 + 65535) >> 16, ApuSampleRate);

  /*-------------------------------------------------------------------*/
  /* Initialize Rectangular, Noise Wave's Regs                         */
  /*-------------------------------------------------------------------*/
  ApuCtrl = ApuCtrlNew = 0;
  ApuC1Wave = pulse_50;
  ApuC2Wave = pulse_50;

  ApuC1a = ApuC1b = ApuC1c = ApuC1d = 0;
  ApuC2a = ApuC2b = ApuC2c = ApuC2d = 0;
  ApuC4a = ApuC4b = ApuC4c = ApuC4d = 0;

  ApuC1Skip = ApuC2Skip = ApuC4Skip = 0;
  ApuC1Index = ApuC2Index = ApuC4Index = 0;
  ApuC1EnvPhase = ApuC2EnvPhase = ApuC4EnvPhase = 0;
  ApuC1EnvVol = ApuC2EnvVol = ApuC4EnvVol = 0;
  ApuC1Atl = ApuC2Atl = ApuC4Atl = 0;
  ApuC1SweepPhase = ApuC2SweepPhase = 0;
  ApuC1Freq = ApuC2Freq = ApuC4Freq = 0;
  ApuC4Sr = 1;
  ApuC4Fdc = 0;

  /*-------------------------------------------------------------------*/
  /*   Initialize Triangle Wave's Regs                                 */
  /*-------------------------------------------------------------------*/
  ApuC3a = ApuC3b = ApuC3c = ApuC3d = 0;
  ApuC3Atl = ApuC3Llc = 0;
  ApuC3ReloadFlag = false;
  // ApuC3WriteLatency = 3; /* Magic Number */
  //  ApuC3CounterStarted = 0x00;

  /*-------------------------------------------------------------------*/
  /*   Initialize DPCM's Regs                                          */
  /*-------------------------------------------------------------------*/
  ApuC5Reg[0] = ApuC5Reg[1] = ApuC5Reg[2] = ApuC5Reg[3] = 0;
  ApuC5Enable = ApuC5Looping = ApuC5CurByte = ApuC5DpcmValue = 0;
  ApuC5Freq = ApuC5Phaseacc;
  ApuC5Address = ApuC5CacheAddr = 0;
  ApuC5DmaLength = ApuC5CacheDmaLength = 0;

  /*-------------------------------------------------------------------*/
  /*   Initialize Wave Buffers                                         */
  /*-------------------------------------------------------------------*/
  wave_buffers = (BYTE (*)[APU_MAX_SAMPLES_PER_SYNC])Frens::f_malloc(5 * APU_MAX_SAMPLES_PER_SYNC);
  InfoNES_MemorySet((void *)wave_buffers[0], 0, APU_MAX_SAMPLES_PER_SYNC);
  InfoNES_MemorySet((void *)wave_buffers[1], 0, APU_MAX_SAMPLES_PER_SYNC);
  InfoNES_MemorySet((void *)wave_buffers[2], 0, APU_MAX_SAMPLES_PER_SYNC);
  InfoNES_MemorySet((void *)wave_buffers[3], 0, APU_MAX_SAMPLES_PER_SYNC);
  InfoNES_MemorySet((void *)wave_buffers[4], 0, APU_MAX_SAMPLES_PER_SYNC);

  /*-------------------------------------------------------------------*/
  /*   Initialize MMC5 Audio                                           */
  /*-------------------------------------------------------------------*/
  ApuMmc5Enable = 0;
  ApuMmc5Ctrl = ApuMmc5CtrlNew = 0;
  ApuMmc5P1Wave = pulse_50;
  ApuMmc5P2Wave = pulse_50;
  ApuMmc5P1a = ApuMmc5P1c = ApuMmc5P1d = 0;
  ApuMmc5P2a = ApuMmc5P2c = ApuMmc5P2d = 0;
  ApuMmc5P1Skip = ApuMmc5P2Skip = 0;
  ApuMmc5P1Index = ApuMmc5P2Index = 0;
  ApuMmc5P1EnvPhase = ApuMmc5P2EnvPhase = 0;
  ApuMmc5P1EnvVol = ApuMmc5P2EnvVol = 0;
  ApuMmc5P1Atl = ApuMmc5P2Atl = 0;
  ApuMmc5P1Freq = ApuMmc5P2Freq = 0;
  ApuMmc5PcmValue = 0;

  /*-------------------------------------------------------------------*/
  /*   Initialize VRC6 Audio                                           */
  /*-------------------------------------------------------------------*/
  ApuVrc6Enable = 0;
  ApuVrc6FreqCtrl = 0;
  ApuVrc6P1a = ApuVrc6P1b = ApuVrc6P1c = 0;
  ApuVrc6P2a = ApuVrc6P2b = ApuVrc6P2c = 0;
  ApuVrc6P1Skip = ApuVrc6P2Skip = 0;
  ApuVrc6P1Index = ApuVrc6P2Index = 0;
  ApuVrc6SawRate = 0;
  ApuVrc6SawFreqL = ApuVrc6SawFreqH = 0;
  ApuVrc6SawAccum = 0;
  ApuVrc6SawStep = 0;
  ApuVrc6SawPhaseAcc = 0;

  /*-------------------------------------------------------------------*/
  /*   Initialize FDS Audio                                            */
  /*-------------------------------------------------------------------*/
  ApuFdsEnable = 0;

  /*-------------------------------------------------------------------*/
  /*   Initialize Sunsoft 5B Audio                                     */
  /*-------------------------------------------------------------------*/
  ApuSunsoft5BEnable = 0;
  InfoNES_MemorySet(ApuS5B_Reg, 0, sizeof(ApuS5B_Reg));
  /* Mixer reg $07 resets to all-bits-set on real hardware (all channels
   * muted until the game configures them). */
  ApuS5B_Reg[0x07] = 0xFF;
  ApuS5B_Skip[0] = ApuS5B_Skip[1] = ApuS5B_Skip[2] = 0;
  ApuS5B_Index[0] = ApuS5B_Index[1] = ApuS5B_Index[2] = 0;

  /* Noise generator — LFSR seeded non-zero, otherwise it would stay stuck. */
  ApuS5B_NoiseSkip  = 0;
  ApuS5B_NoiseIndex = 0;
  ApuS5B_NoiseLfsr  = 0x1FFFF;
  ApuS5B_NoiseOut   = 1;

  /* Envelope — start held so it contributes nothing until $0D is written. */
  ApuS5B_EnvSkip  = 0;
  ApuS5B_EnvIndex = 0;
  ApuS5B_EnvStep  = 0x1F;    /* with Decay=1 → output 0 */
  ApuS5B_EnvHold  = 1;
  ApuS5B_EnvDecay = 1;
  ApuS5B_EnvShape = 0;

#if PICO_RP2350
  /*-------------------------------------------------------------------*/
  /*   Initialize VRC7 (OPLL) — Mapper 85 will allocate the buffer and */
  /*   flip ApuVrc7Enable when a VRC7 cart is loaded.                  */
  /*-------------------------------------------------------------------*/
  ApuVrc7Enable = 0;
#endif

  entertime = getPassedClocks();
  cur_event = 0;
}

/*===================================================================*/
/*                                                                   */
/*            InfoNES_pApuDone() : Finalize pApu                     */
/*                                                                   */
/*===================================================================*/

void InfoNES_pAPUDone(void)
{
  InfoNES_SoundClose();

  if (wave_buffers) { Frens::f_free(wave_buffers); wave_buffers = nullptr; }
#if PICO_RP2350
  if (mmc5_wave_buffers) { Frens::f_free(mmc5_wave_buffers); mmc5_wave_buffers = nullptr; }
#endif
  if (vrc6_wave_buffers) { Frens::f_free(vrc6_wave_buffers); vrc6_wave_buffers = nullptr; }
  if (fds_wave_buffer) { Frens::f_free(fds_wave_buffer); fds_wave_buffer = nullptr; }
  if (s5b_wave_buffers) { Frens::f_free(s5b_wave_buffers); s5b_wave_buffers = nullptr; }
#if PICO_RP2350
  if (vrc7_wave_buffer) { Frens::f_free(vrc7_wave_buffer); vrc7_wave_buffer = nullptr; }
  ApuVrc7Free();
#endif
 
}

/*
 * End of InfoNES_pAPU.cpp
 */