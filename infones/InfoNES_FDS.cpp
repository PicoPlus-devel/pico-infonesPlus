/*===================================================================*/
/*                                                                   */
/*  InfoNES_FDS.cpp : Famicom Disk System support                    */
/*                                                                   */
/*  Detection and preflight gate (BIOS file + memory), buffer        */
/*  allocation, BIOS load, Mapper 20 init, drive emulation, disk     */
/*  swap UI, save-data sidecar, audio and save-states.               */
/*                                                                   */
/*  RP2350 only - main.cpp calls fdsParse() under #if PICO_RP2350.   */
/*  PSRAM is not required: without it the drive runs in single-side  */
/*  mode (fds_single_side_mode), rebuilding one side at a time.      */
/*                                                                   */
/*===================================================================*/

#include <string.h>
#include <stdio.h>

#include "InfoNES_FDS.h"
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_pAPU.h"
#include "FrensHelpers.h"

extern unsigned int ApuCyclesPerSample;
#include "ff.h"
#include "K6502.h"
#include "settings.h"

/*-------------------------------------------------------------------*/
/*  FDS state                                                        */
/*-------------------------------------------------------------------*/

BYTE *FDS_Bios       = nullptr;
BYTE *FDS_PrgRam     = nullptr;
BYTE *FDS_ChrRam     = nullptr;
BYTE *FDS_DiskImage  = nullptr;
int   FDS_NumSides   = 0;
int   FDS_CurrentSide = 0;
bool  FDS_DiskInserted = true;

/*-------------------------------------------------------------------*/
/*  Disk drive emulation state                                       */
/*-------------------------------------------------------------------*/
/*
 *  Disk timing: a real FDS reads ~10.4k bytes/s, which on NTSC CPU
 *  (1.789773 MHz) is ~149 cycles/byte. We accumulate STEP_PER_SCANLINE
 *  CPU cycles per HSync and emit a disk byte every FDS_CYC_PER_BYTE.
 */
#define FDS_CYC_PER_BYTE  149

/* $4020/$4021 timer reload ($4022 starts/stops). */
static WORD fds_timer_reload   = 0;
static WORD fds_timer_counter  = 0;
static BYTE fds_timer_irq_en   = 0;   /* $4022 bit 1 */
static BYTE fds_timer_repeat   = 0;   /* $4022 bit 0 */
static BYTE fds_timer_irq_pend = 0;

/* $4023 master enable. Bit 0 = disk regs, bit 1 = sound regs. */
static BYTE fds_master_enable  = 0;

/* $4025 disk control bits, decoded. */
static BYTE fds_motor_on       = 0;
static BYTE fds_xfer_reset     = 1;   /* hold at gap until BIOS releases */
static BYTE fds_read_mode      = 1;
static BYTE fds_byte_irq_en    = 0;
static BYTE fds_scan_start     = 0;   /* $4025 bit 6 (== Mesen2's _diskReady) */
static BYTE fds_crc_control    = 0;   /* $4025 bit 4 — drive emits/checks CRC */
/* fds_gap_ended is defined later, alongside the linear-walk model. */
/* Mesen2 behavior: once the drive has *started* a scan since the last
   motor toggle, rst=1 alone no longer pauses it. BIOS file-scan
   routines (e.g. Metroid) rely on this — they set rst=1 + scan=1 to
   fast-forward past blocks they don't want, and would deadlock if
   rst=1 stopped the drive cold. Set this on the first produced byte
   and clear it on motor 0→1. */
static BYTE fds_scanning_started = 0;

/* Disk transfer state. */
static DWORD fds_byte_pos        = 0;  /* payload index inside current side */
static DWORD fds_block_start_pos = 0;  /* byte_pos at start of current block */
static BYTE  fds_read_buf        = 0;
static BYTE  fds_byte_irq_pend   = 0;
static BYTE  fds_transfer_complete = 0; /* Mesen2 _transferComplete: byte ready in $4031 */
static BYTE  fds_end_of_head     = 0;
static WORD  fds_cycle_acc     = 0;
static int   fds_motor_spinup  = 0;   /* scanlines to wait after motor on */

/* $4024 staged write byte (real write-back lands in phase 6). */
static BYTE  fds_write_buf     = 0;

/* Phase 5: disk swap state. fds_eject_counter is in HSync ticks; while
   it is positive, FDS_DiskInserted is forced to false so the BIOS
   sees disk-not-inserted at $4032 bit 0. When it reaches zero, we
   re-insert with fds_pending_side as the new FDS_CurrentSide. -1 in
   fds_pending_side means "stay ejected". */
#define FDS_SWAP_EJECT_HSYNCS  (60 * 262)  /* ~1 s of NTSC hsyncs */
static int   fds_eject_counter = 0;
static int   fds_pending_side  = -1;

/* Single-side mode: only one side's expanded buffer is kept in memory
   at a time. Enabled when PSRAM is not available (RP2350 without PSRAM).
   On side swap, dirty pages are flushed and the buffer is rebuilt. */
static bool  fds_single_side_mode = false;
static int   fds_expanded_side    = -1;

/* Deferred rebuild: in single-side mode, fdsRebuildForSide does SD I/O
   which overflows the stack when called from the deep fdsHsync chain.
   Instead, we set a pending side and do the rebuild at frame boundary. */
static volatile int fds_rebuild_pending_side = -1;
#define FDS_SAVE_BASE_LEN 256
static char  fds_save_base[FDS_SAVE_BASE_LEN] = {0};
static char  fds_sidecar_path[FDS_SAVE_BASE_LEN + 16] = {0};

/* Mesen2-style automatic disk-side switching. When the BIOS repeatedly
   polls $4032 (disk-not-inserted check) in quick succession, it means
   the game needs a different disk side. We auto-eject, wait ~77 frames,
   then re-insert. When the BIOS calls $E445 to verify the disk header,
   we intercept and insert the correct side by matching the 10-byte
   header buffer against all disk-side headers. */
static bool  fds_auto_insert_enabled = true;
/* User-facing setting: when false, the entire auto-insert mechanism
   (auto-eject, auto-side-switch, $E445 hook) is disabled.
   Configurable via settings menu; reads directly from settings.flags.autoSwapFDS. */
static int   fds_4032_read_count     = 0;
static int   fds_last_read_hsync     = 0;  /* hsync tick of last $4032 read */
static int   fds_hsync_counter       = 0;  /* monotonic hsync counter */
static int   fds_auto_eject_delay    = -1; /* hsyncs until auto-insert; -1=idle */
static int   fds_auto_retry_counter  = -1; /* hsyncs to retry if game didn't accept */
static int   fds_auto_insert_attempts = 0; /* guard against infinite retry */
static int   fds_previous_side = 0;       /* side before auto-eject (for cycling) */
/* Mesen2: when the head reaches end-of-disk, a cooldown counter starts.
   The $4032 polling detection can only fire after this expires. */
static int   fds_end_of_head_cooldown = 0; /* frames remaining; 0=ready */
static bool  fds_disk_read_once = false;   /* true after first end-of-head */
static bool  fds_manual_insert_pending = false; /* true when disk starts ejected awaiting A press */
static int   fds_manual_insert_delay = 0;       /* frames to skip before accepting A press */
#define FDS_AUTO_EJECT_FRAMES    77
#define FDS_AUTO_RETRY_FRAMES    200
#define FDS_AUTO_MAX_ATTEMPTS    5
#define FDS_4032_CHECK_WINDOW    (100 * 262)  /* ~100 frames of hsyncs */
#define FDS_4032_CHECK_THRESHOLD 20
#define FDS_END_OF_HEAD_COOLDOWN_FRAMES 77

/* Mesen2-style expanded disk image. The raw .fds file contains only
   block payloads back to back; the BIOS expects a richer byte stream
   that includes inter-block gaps and sync marks so its file-scan
   routine can fast-forward (rst=1 + scan=1) past blocks it doesn't
   want. We build that expanded layout once at load time and walk it
   linearly at runtime — no phase tracking, no auto-transitions.

     Per side: [3537 zero gap]
               for each block: [0x80 mark][payload][0x4D 0x62 fake CRC]
                                [122 zero gap]
*/
#define FDS_INITIAL_GAP_BYTES  3537   /* 28300 bits / 8 */
#define FDS_INTER_BLOCK_GAP    122    /* 976 bits / 8 */
#define FDS_MARK_BYTE          0x80
#define FDS_FAKE_CRC_HI        0x4D
#define FDS_FAKE_CRC_LO        0x62

static BYTE  *fds_expanded            = nullptr;
static DWORD  fds_expanded_total_size = 0;
static DWORD  fds_side_offsets[FDS_MAX_SIDES] = {0};
static DWORD  fds_side_sizes[FDS_MAX_SIDES]   = {0};

static inline BYTE *fdsCurrentSideBuf()
{
  return fds_expanded ? fds_expanded + fds_side_offsets[FDS_CurrentSide] : nullptr;
}

static inline DWORD fdsCurrentSideSize()
{
  return fds_side_sizes[FDS_CurrentSide];
}

/* Phase 6: dirty-page bitmap for save-data writeback. Per 4 KB page
   on the EXPANDED image (not the raw .fds), since that's what we
   actually mutate at runtime. */
#define FDS_PAGE_SIZE         4096
/* Worst-case expanded total: 8 sides * (3537 + ~30 blocks * 125 + 65500)
   ≈ 600 KB. 152 pages covers it with margin. */
#define FDS_MAX_PAGES         160
static BYTE fds_dirty_bitmap[(FDS_MAX_PAGES + 7) / 8] = {0};

static inline void fdsMarkDirty(DWORD byteOffset)
{
  unsigned page = (unsigned)(byteOffset / FDS_PAGE_SIZE);
  if (page >= (unsigned)FDS_MAX_PAGES) return;
  fds_dirty_bitmap[page >> 3] |= (BYTE)(1u << (page & 7));
}

static inline bool fdsPageDirty(unsigned page)
{
  if (page >= (unsigned)FDS_MAX_PAGES) return false;
  return (fds_dirty_bitmap[page >> 3] >> (page & 7)) & 1u;
}

static inline void fdsClearDirtyBitmap()
{
  for (size_t i = 0; i < sizeof(fds_dirty_bitmap); ++i)
    fds_dirty_bitmap[i] = 0;
}

/* Tracing knobs. The full per-byte trace floods the UART so hard
   it stalls the emulator; the slim trace only emits block-boundary
   transitions and $4025 writes — that's enough to diagnose ERR.24
   without overwhelming the UART. */
#define FDS_TRACE      0   /* slim: block#X / 4025 / END */
#define FDS_TRACE_BYTE 0   /* per-byte read/write — only enable for deep dives */
#if FDS_TRACE
#define FDS_LOG(...) printf("[FDS] " __VA_ARGS__)
#else
#define FDS_LOG(...) do {} while (0)
#endif
#if FDS_TRACE_BYTE
#define FDS_BYTELOG(...) printf("[FDS] " __VA_ARGS__)
#else
#define FDS_BYTELOG(...) do {} while (0)
#endif

static int fds_block_count = 0;

/*-------------------------------------------------------------------*/
/*  FDS expansion audio state (Mesen2-style wavetable + modulation)  */
/*-------------------------------------------------------------------*/

/* Wave table: 64 entries, 6-bit each (0-63). */
static BYTE  fds_wave_table[64] = {0};
static BYTE  fds_wave_write_enabled = 0; /* $4089 bit 7 */
static BYTE  fds_wave_position = 0;      /* 0-63 */
static WORD  fds_wave_overflow = 0;      /* 16-bit phase accumulator */

/* Master volume: 2-bit index into WaveVolumeTable. */
static BYTE  fds_master_volume = 0;
static const unsigned int __not_in_flash("fds_audio") FdsWaveVolumeTable[4] = {36, 24, 17, 14};

/* Master envelope speed — BIOS inits to $E8. */
static BYTE  fds_master_env_speed = 0xE8;

/* Volume envelope ($4080/$4082/$4083). */
static BYTE  fds_vol_speed     = 0;
static BYTE  fds_vol_gain      = 0;   /* 0-32 */
static BYTE  fds_vol_env_off   = 1;   /* $4080 bit 7: 1=envelope disabled */
static BYTE  fds_vol_increase  = 0;   /* $4080 bit 6: direction */
static DWORD fds_vol_timer     = 0;
static WORD  fds_vol_frequency = 0;   /* 12-bit: $4082 low, $4083 low nibble */

/* Waveform control ($4083). */
static BYTE  fds_disable_envelopes = 0; /* bit 6 */
static BYTE  fds_halt_waveform     = 0; /* bit 7 */

/* Modulation envelope ($4084). */
static BYTE  fds_mod_speed     = 0;
static BYTE  fds_mod_gain      = 0;   /* 0-32 */
static BYTE  fds_mod_env_off   = 1;
static BYTE  fds_mod_increase  = 0;
static DWORD fds_mod_timer     = 0;

/* Modulation unit ($4085-$4088). */
static WORD  fds_mod_frequency     = 0;   /* 12-bit */
static BYTE  fds_mod_disabled      = 1;   /* $4087 bit 7 */
static BYTE  fds_mod_table[64]     = {0}; /* 3-bit entries */
static BYTE  fds_mod_table_pos     = 0;
static int8_t fds_mod_counter      = 0;   /* signed 7-bit */
static WORD  fds_mod_overflow      = 0;   /* 16-bit phase accumulator */
static int   fds_mod_output        = 0;   /* pitch modulation delta */

/* Audio output buffer — allocated in Map20_Init, freed in pAPUDone. */
BYTE  ApuFdsEnable = 0;
BYTE *fds_wave_buffer = nullptr;

/*-------------------------------------------------------------------*/
/*  FDS audio envelope tick helper (shared by volume + mod channels) */
/*-------------------------------------------------------------------*/
static inline bool __not_in_flash_func(fdsTickEnvelope)(BYTE speed, BYTE env_off,
                                   BYTE increase, BYTE *gain,
                                   DWORD *timer)
{
  if (!env_off && fds_master_env_speed > 0)
  {
    if (*timer == 0)
    {
      *timer = (DWORD)8 * (speed + 1) * fds_master_env_speed;
    }
    (*timer)--;
    if (*timer == 0)
    {
      *timer = (DWORD)8 * (speed + 1) * fds_master_env_speed;
      if (increase && *gain < 32)
        (*gain)++;
      else if (!increase && *gain > 0)
        (*gain)--;
      return true;
    }
  }
  return false;
}

/*-------------------------------------------------------------------*/
/*  FDS modulation output calculation (Mesen2 algorithm).            */
/*-------------------------------------------------------------------*/
static void __not_in_flash_func(fdsUpdateModOutput)(WORD volumePitch)
{
  /* Mesen2 "ModChannel::UpdateOutput" — NesDev wiki algorithm.
     counter = $4085 signed 7-bit, gain = $4084 6-bit unsigned. */
  int temp = (int)fds_mod_counter * (int)fds_mod_gain;
  int remainder = temp & 0xF;
  temp >>= 4;
  if (remainder > 0 && (temp & 0x80) == 0)
  {
    temp += (fds_mod_counter < 0) ? -1 : 2;
  }
  if (temp >= 192)
    temp -= 256;
  else if (temp < -64)
    temp += 256;

  temp = (int)volumePitch * temp;
  remainder = temp & 0x3F;
  temp >>= 6;
  if (remainder >= 32)
    temp += 1;

  fds_mod_output = temp;
}

/*-------------------------------------------------------------------*/
/*  FDS modulation counter update (Mesen2 ModChannel::UpdateCounter) */
/*-------------------------------------------------------------------*/
static inline void __not_in_flash_func(fdsUpdateModCounter)(int8_t value)
{
  fds_mod_counter = value;
  if (fds_mod_counter >= 64)
    fds_mod_counter -= 128;
  else if (fds_mod_counter < -64)
    fds_mod_counter += 128;
}

/*-------------------------------------------------------------------*/
/*  FDS modulator tick — advance mod table on 16-bit overflow.       */
/*-------------------------------------------------------------------*/
static const int __not_in_flash("fds_audio") fds_mod_lut[8] = {0, 1, 2, 4, 0x7F, -4, -2, -1};
#define FDS_MOD_RESET 0x7F

static inline bool __not_in_flash_func(fdsTickModulator)()
{
  if (!fds_mod_disabled && fds_mod_frequency > 0)
  {
    WORD prev = fds_mod_overflow;
    fds_mod_overflow += fds_mod_frequency;
    if (fds_mod_overflow < prev) /* 16-bit overflow */
    {
      int offset = fds_mod_lut[fds_mod_table[fds_mod_table_pos]];
      fdsUpdateModCounter(offset == FDS_MOD_RESET ? 0 :
                          (int8_t)(fds_mod_counter + offset));
      fds_mod_table_pos = (fds_mod_table_pos + 1) & 0x3F;
      return true;
    }
  }
  return false;
}

/*-------------------------------------------------------------------*/
/*  fdsResetAudio: clear all FDS audio state. Called from            */
/*  fdsResetDrive().                                                 */
/*-------------------------------------------------------------------*/
void fdsResetAudio()
{
  memset(fds_wave_table, 0, sizeof(fds_wave_table));
  fds_wave_write_enabled = 0;
  fds_wave_position = 0;
  fds_wave_overflow = 0;

  fds_master_volume = 0;
  fds_master_env_speed = 0xE8;

  fds_vol_speed = 0;
  fds_vol_gain = 0;
  fds_vol_env_off = 1;
  fds_vol_increase = 0;
  fds_vol_timer = 0;
  fds_vol_frequency = 0;

  fds_disable_envelopes = 0;
  fds_halt_waveform = 0;

  fds_mod_speed = 0;
  fds_mod_gain = 0;
  fds_mod_env_off = 1;
  fds_mod_increase = 0;
  fds_mod_timer = 0;
  fds_mod_frequency = 0;
  fds_mod_disabled = 1;
  memset(fds_mod_table, 0, sizeof(fds_mod_table));
  fds_mod_table_pos = 0;
  fds_mod_counter = 0;
  fds_mod_overflow = 0;
  fds_mod_output = 0;
}

/*
 *  Linear-walk drive emulation (Mesen2-style).
 *
 *  fdsBuildExpandedBuffer() expands the raw .fds at load time into a
 *  wire-format buffer that contains gaps and fake CRC slots. The
 *  drive walks this buffer linearly — one byte per FDS_CYC_PER_BYTE
 *  cycles. No phase machine, no auto-transitions.
 *
 *  Per side:
 *    [FDS_INITIAL_GAP_BYTES zeros]
 *    For each block: [payload from .fds][2 fake CRC bytes][FDS_INTER_BLOCK_GAP zeros]
 *
 *  fds_block_starts[s][i] holds the offset (within side s) of the
 *  i-th block's first PAYLOAD byte (the block-type byte), used by
 *  the drive to know where gap zeros end and emission begins.
 *
 *  fds_gap_ended distinguishes "walking through gap zeros silently"
 *  (false) from "emitting bytes via byte-ready IRQ" (true). It flips
 *  to true the moment pos lands on a known block-start offset, and
 *  back to false on rst=1 (the BIOS's bit-synchroniser reset).
 *
 *  We deliberately do NOT place a 0x80 sync mark byte in the buffer:
 *  the FDS BIOS waits for the first non-zero byte after a gap as the
 *  block's type byte (0x01-0x04). A literal 0x80 between gap and
 *  payload would be read as an invalid block type → ERR.23.
 */
#define FDS_MAX_BLOCKS_PER_SIDE 80

static DWORD fds_block_starts[FDS_MAX_SIDES][FDS_MAX_BLOCKS_PER_SIDE] = {{0}};
/* fds_block_ends[s][i] = position one past the last CRC byte of block i.
   When emit advances pos to this value, we've just finished block i — flip
   gap_ended=0 so the inter-block gap zeros are walked silently rather than
   emitted to BIOS as bogus data bytes. */
static DWORD fds_block_ends[FDS_MAX_SIDES][FDS_MAX_BLOCKS_PER_SIDE] = {{0}};
static int   fds_block_counts[FDS_MAX_SIDES] = {0};

static BYTE  fds_gap_ended = 0;   /* 0 = walking through gap zeros + 0x80 mark, 1 = emitting */

/*-------------------------------------------------------------------*/
/*  fdsExpandOneSide: expand a single side of the raw .fds into dest */
/*  at destStart, writing gaps + marks + CRC slots. Records block    */
/*  metadata in fds_block_starts/ends/counts for side sideIndex.     */
/*  Returns the write position after expansion.                      */
/*-------------------------------------------------------------------*/
#define FDS_PER_SIDE_MAX  (FDS_SIDE_SIZE + 8192)

static DWORD fdsExpandOneSide(BYTE *rawDisk, int sideIndex,
                              BYTE *dest, DWORD destStart)
{
  DWORD writePos = destStart;

  writePos += FDS_INITIAL_GAP_BYTES;

  DWORD srcBase = (DWORD)sideIndex * FDS_SIDE_SIZE;
  DWORD srcPos  = 0;
  int   blkIdx  = 0;
  WORD  lastFileSize = 0;

  while (srcPos < FDS_SIDE_SIZE && blkIdx < FDS_MAX_BLOCKS_PER_SIDE)
  {
    BYTE type = rawDisk[srcBase + srcPos];
    int  blockSize = 0;
    switch (type)
    {
    case 0x01: blockSize = 56; break;
    case 0x02: blockSize = 2;  break;
    case 0x03:
      blockSize = 16;
      if (srcPos + 14 < FDS_SIDE_SIZE)
      {
        lastFileSize = rawDisk[srcBase + srcPos + 13] |
                       ((WORD)rawDisk[srcBase + srcPos + 14] << 8);
      }
      break;
    case 0x04: blockSize = 1 + lastFileSize; break;
    default:
      goto done;
    }

    if ((DWORD)(srcPos + blockSize) > FDS_SIDE_SIZE) goto done;

    dest[writePos++] = FDS_MARK_BYTE;
    fds_block_starts[sideIndex][blkIdx] = (writePos - 1) - destStart;

    memcpy(&dest[writePos], &rawDisk[srcBase + srcPos], (size_t)blockSize);
    writePos += blockSize;
    srcPos   += blockSize;

    dest[writePos++] = FDS_FAKE_CRC_HI;
    dest[writePos++] = FDS_FAKE_CRC_LO;

    fds_block_ends[sideIndex][blkIdx] = writePos - destStart;
    blkIdx++;

    writePos += FDS_INTER_BLOCK_GAP;
  }
done:
  fds_block_counts[sideIndex] = blkIdx;
  {
    DWORD contentSize = writePos - destStart;
    fds_side_sizes[sideIndex] = contentSize < FDS_SIDE_SIZE ? FDS_SIDE_SIZE : contentSize;
  }

  FDS_LOG("side %d: %d block(s), expanded size %lu B\n",
          sideIndex, blkIdx, (unsigned long)fds_side_sizes[sideIndex]);
  for (int b = 0; b < blkIdx && b < 6; ++b)
  {
    DWORD pos = fds_block_starts[sideIndex][b];
    BYTE val = dest[destStart + pos];
    FDS_LOG("  blk[%d] start=%lu end=%lu byte=0x%02X\n",
            b, (unsigned long)pos,
            (unsigned long)fds_block_ends[sideIndex][b], val);
  }

  return writePos;
}

/*-------------------------------------------------------------------*/
/*  fdsBuildExpandedBuffer: allocate and build the expanded disk      */
/*  image. In multi-side mode (PSRAM) all sides are built; in        */
/*  single-side mode only side 0 is built initially.                 */
/*-------------------------------------------------------------------*/
static bool fdsBuildExpandedBuffer(BYTE *rawDisk, int sides)
{
  if (fds_expanded) { Frens::f_free(fds_expanded); fds_expanded = nullptr; }

  int numSidesToBuild = fds_single_side_mode ? 1 : sides;
  DWORD totalMax = (DWORD)numSidesToBuild * FDS_PER_SIDE_MAX;

  fds_expanded = (BYTE *)Frens::f_malloc(totalMax);
  if (!fds_expanded)
  {
    InfoNES_Error("FDS: cannot allocate %u bytes for expanded buffer", (unsigned)totalMax);
    return false;
  }
  memset(fds_expanded, 0, totalMax);

  if (fds_single_side_mode)
  {
    fds_side_offsets[0] = 0;
    fdsExpandOneSide(rawDisk, 0, fds_expanded, 0);
    fds_expanded_side = 0;
    fds_expanded_total_size = FDS_PER_SIDE_MAX;
  }
  else
  {
    DWORD writePos = 0;
    for (int s = 0; s < sides; ++s)
    {
      fds_side_offsets[s] = writePos;
      fdsExpandOneSide(rawDisk, s, fds_expanded, writePos);
      writePos += FDS_PER_SIDE_MAX;
    }
    fds_expanded_total_size = (DWORD)sides * FDS_PER_SIDE_MAX;
  }

  return true;
}

/*-------------------------------------------------------------------*/
/*  Helpers                                                          */
/*-------------------------------------------------------------------*/

static inline void fdsResetTransferPosition()
{
  fds_byte_pos = 0;
  fds_block_start_pos = 0;
  fds_cycle_acc = 0;
  fds_end_of_head = 0;
  fds_gap_ended = 0;
  fds_scanning_started = 0;
}

bool fdsIsFdsFilename(const char *filename)
{
  if (!filename) return false;
  char ext[8];
  Frens::getextensionfromfilename(filename, ext, sizeof(ext));
  return strcasecmp(ext, ".fds") == 0;
}

/*-------------------------------------------------------------------*/
/*  Preflight checks                                                 */
/*-------------------------------------------------------------------*/

#if PICO_RP2350
static bool fdsBiosFileOk(size_t *outSize)
{
  FILINFO fno;
  FRESULT fr = f_stat(FDS_BIOS_PATH, &fno);
  if (fr != FR_OK )
  {
    InfoNES_Error("No FDS BIOS at %s", FDS_BIOS_PATH);
    return false;
  }
  if (fno.fsize != FDS_BIOS_SIZE )
  {
    InfoNES_Error("FDS BIOS must be %d bytes (is %lu)",
                  (int)FDS_BIOS_SIZE, (unsigned long)fno.fsize);
    return false;
  }
  if (outSize) *outSize = (size_t)fno.fsize;
  return true;
}

static bool fdsLoadBios(BYTE *dst)
{
  FIL fil;
  FRESULT fr = f_open(&fil, FDS_BIOS_PATH, FA_READ);
  if (fr != FR_OK)
  {
    InfoNES_Error("Cannot open FDS BIOS (%d)", fr);
    return false;
  }
  UINT br = 0;
  fr = f_read(&fil, dst, FDS_BIOS_SIZE, &br);
  f_close(&fil);
  if (fr != FR_OK || br != FDS_BIOS_SIZE)
  {
    InfoNES_Error("Failed reading FDS BIOS (%d)", fr);
    return false;
  }
  return true;
}
#endif

/*-------------------------------------------------------------------*/
/*  fdsParse: gate + allocate + load BIOS                            */
/*-------------------------------------------------------------------*/

bool fdsParse(BYTE *fdsImage, size_t fdsImageSize)
{
  /* Always start from a clean slate so a previous failed launch leaves
     no dangling pointers. */
  fdsRelease();
  IsFDS = false;
  FDS_DiskImage = nullptr;
  FDS_NumSides = 0;
  FDS_CurrentSide = 0;

  /* Zero NesHeader so InfoNES_SetupPPU sees byVRomSize == 0 and
     enables CHR-RAM writes. Otherwise leftover values from a prior
     iNES game would silently drop PPU $0000-$1FFF writes. */
  memset(&NesHeader, 0, sizeof(NesHeader));

#if !PICO_RP2350
  (void)fdsImage; (void)fdsImageSize;
  InfoNES_Error("FDS support requires RP2350");
  return false;
#else
  if (!fdsImage || fdsImageSize == 0)
  {
    InfoNES_Error("FDS image is empty");
    return false;
  }

  fds_single_side_mode = !Frens::isPsramEnabled();

  /* Strip optional 16-byte fwNES header. */
  BYTE *diskBytes = fdsImage;
  size_t diskSize = fdsImageSize;
  if (diskSize >= FDS_FWNES_HDR_SIZE && memcmp(diskBytes, "FDS\x1a", 4) == 0)
  {
    diskBytes += FDS_FWNES_HDR_SIZE;
    diskSize  -= FDS_FWNES_HDR_SIZE;
  }

  if (diskSize == 0 || (diskSize % FDS_SIDE_SIZE) != 0)
  {
    InfoNES_Error("FDS image size %u is not a multiple of %d",
                  (unsigned)diskSize, (int)FDS_SIDE_SIZE);
    return false;
  }
  int sides = (int)(diskSize / FDS_SIDE_SIZE);
  if (sides < 1 || sides > FDS_MAX_SIDES)
  {
    InfoNES_Error("FDS image has unsupported side count %d", sides);
    return false;
  }

  size_t need = FDS_BIOS_SIZE + FDS_PRG_RAM_SIZE + FDS_CHR_RAM_SIZE;
  if (fds_single_side_mode)
    need += FDS_PER_SIDE_MAX;
  uint avail = Frens::GetAvailableMemory();
  if (avail < need)
  {
    InfoNES_Error("Not enough memory for FDS (%u < %u)",
                  (unsigned)avail, (unsigned)need);
    return false;
  }

  if (!fdsBiosFileOk(nullptr))
  {
    /* InfoNES_Error already populated. */
    return false;
  }

  FDS_Bios   = (BYTE *)Frens::f_malloc(FDS_BIOS_SIZE);
  FDS_PrgRam = (BYTE *)Frens::f_malloc(FDS_PRG_RAM_SIZE);
  FDS_ChrRam = (BYTE *)Frens::f_malloc(FDS_CHR_RAM_SIZE);
  if (!FDS_Bios || !FDS_PrgRam || !FDS_ChrRam)
  {
    InfoNES_Error("FDS memory allocation failed");
    fdsRelease();
    return false;
  }

  memset(FDS_PrgRam, 0, FDS_PRG_RAM_SIZE);
  memset(FDS_ChrRam, 0, FDS_CHR_RAM_SIZE);

  if (!fdsLoadBios(FDS_Bios))
  {
    fdsRelease();
    return false;
  }

  FDS_DiskImage = diskBytes;
  FDS_NumSides = sides;
  FDS_CurrentSide = 0;

  if (!fdsBuildExpandedBuffer(diskBytes, sides))
  {
    fdsRelease();
    return false;
  }

  IsFDS = true;
  fds_eject_counter = 0;
  fds_pending_side  = -1;
  fdsClearDirtyBitmap();

  printf("FDS: image %u bytes, %d side(s), %s mode; avail before alloc %u, after %u\n",
         (unsigned)diskSize, sides,
         fds_single_side_mode ? "single-side" : "multi-side",
         (unsigned)avail, (unsigned)Frens::GetAvailableMemory());
  return true;
#endif
}

/*-------------------------------------------------------------------*/
/*  Disk swap UI hooks (phase 5).                                    */
/*-------------------------------------------------------------------*/
void fdsRequestSwap(int newSide)
{
  if (!IsFDS) return;
  if (newSide < 0 || newSide >= FDS_NumSides) return;
  /* Hold the disk ejected for ~1 s of game time, then re-insert with
     the requested side. The BIOS samples $4032 bit 0 to detect the
     disk being pulled and re-seated; without this pulse it would not
     re-read the file table for the new side. */
  FDS_DiskInserted = false;
  fds_pending_side = newSide;
  fds_eject_counter = FDS_SWAP_EJECT_HSYNCS;
}

void fdsRequestEject()
{
  if (!IsFDS) return;
  FDS_DiskInserted = false;
  fds_pending_side = -1;
  fds_eject_counter = 0;
}

int fdsCurrentSwapValue()
{
  if (!IsFDS) return 0;
  /* While the eject pulse is in flight, report the pending target so
     the menu reflects the user's choice immediately rather than
     flicking to "Eject" for the duration of the pulse. After the
     pulse resolves, FDS_CurrentSide is the source of truth. */
  if (fds_eject_counter > 0 && fds_pending_side >= 0 &&
      fds_pending_side < FDS_NumSides)
  {
    return fds_pending_side;
  }
  if (!FDS_DiskInserted) return FDS_NumSides;
  return FDS_CurrentSide;
}

int fdsGetNumSides()
{
  return FDS_NumSides;
}

/*-------------------------------------------------------------------*/
/*  Sidecar (save-data) persistence (phase 6).                       */
/*  File format:                                                     */
/*    bytes 0..3 : "FDSV" magic                                      */
/*    byte  4    : version = 1                                       */
/*    byte  5    : sides (must equal FDS_NumSides at load time)      */
/*    byte  6    : bitmap byte count (= ceil(num_pages / 8))         */
/*    byte  7    : reserved (0)                                      */
/*    bytes 8..  : bitmap, 1 bit per FDS_PAGE_SIZE page              */
/*    bytes ..   : for each set bit (in order), FDS_PAGE_SIZE bytes  */
/*                 of disk-image content.                            */
/*  Page indexing is flat across all sides: page p covers disk image */
/*  bytes [p*PAGE_SIZE .. p*PAGE_SIZE+PAGE_SIZE-1]. The last page    */
/*  may extend past total_disk_bytes; we save FDS_PAGE_SIZE anyway   */
/*  (zero-padded) so file layout stays predictable.                  */
/*-------------------------------------------------------------------*/

#define FDS_SIDECAR_MAGIC0 'F'
#define FDS_SIDECAR_MAGIC1 'D'
#define FDS_SIDECAR_MAGIC2 'S'
#define FDS_SIDECAR_MAGIC3 'V'
#define FDS_SIDECAR_VERSION 1

bool fdsHasDirtyPages()
{
  for (size_t i = 0; i < sizeof(fds_dirty_bitmap); ++i)
    if (fds_dirty_bitmap[i]) return true;
  return false;
}

#if PICO_RP2350
static int fdsTotalPages()
{
  /* Page count covers the expanded buffer (which is what we actually
     mutate at runtime), not the raw .fds. */
  return (int)((fds_expanded_total_size + FDS_PAGE_SIZE - 1) / FDS_PAGE_SIZE);
}
#endif

/*-------------------------------------------------------------------*/
/*  Internal sidecar I/O: work with a concrete file path.            */
/*  storedSideCount is written into / checked against hdr[5].        */
/*  In multi-side mode this is FDS_NumSides (all sides in one file). */
/*  In single-side mode this is 1 (one side per file).               */
/*-------------------------------------------------------------------*/
#if PICO_RP2350
static FIL fds_sidecar_fil;

static bool fdsLoadSidecarFromFile(const char *filePath, int expectedSideCount)
{
  if (!IsFDS || !fds_expanded || !filePath) return false;

  if (f_open(&fds_sidecar_fil, filePath, FA_READ) != FR_OK)
    return true; /* absent is OK */

  BYTE hdr[8];
  UINT br = 0;
  if (f_read(&fds_sidecar_fil, hdr, sizeof(hdr), &br) != FR_OK || br != sizeof(hdr) ||
      hdr[0] != FDS_SIDECAR_MAGIC0 || hdr[1] != FDS_SIDECAR_MAGIC1 ||
      hdr[2] != FDS_SIDECAR_MAGIC2 || hdr[3] != FDS_SIDECAR_MAGIC3 ||
      hdr[4] != FDS_SIDECAR_VERSION || hdr[5] != (BYTE)expectedSideCount)
  {
    printf("FDS sidecar header mismatch — ignoring %s.\n", filePath);
    f_close(&fds_sidecar_fil);
    return true;
  }

  int numPages = fdsTotalPages();
  int bmBytes = hdr[6];
  if (bmBytes != (numPages + 7) / 8)
  {
    printf("FDS sidecar bitmap size mismatch — ignoring %s.\n", filePath);
    f_close(&fds_sidecar_fil);
    return true;
  }

  BYTE bm[(FDS_MAX_PAGES + 7) / 8] = {0};
  if (f_read(&fds_sidecar_fil, bm, bmBytes, &br) != FR_OK || br != (UINT)bmBytes)
  {
    f_close(&fds_sidecar_fil);
    return false;
  }

  DWORD totalBytes = fds_expanded_total_size;
  int loaded = 0;
  for (int p = 0; p < numPages; ++p)
  {
    if (!((bm[p >> 3] >> (p & 7)) & 1)) continue;
    DWORD off = (DWORD)p * FDS_PAGE_SIZE;
    DWORD bytes = FDS_PAGE_SIZE;
    if (off >= totalBytes) bytes = 0;
    else if (off + bytes > totalBytes) bytes = totalBytes - off;

    if (bytes)
    {
      if (f_read(&fds_sidecar_fil, fds_expanded + off, bytes, &br) != FR_OK ||
          br != bytes)
      {
        f_close(&fds_sidecar_fil);
        return false;
      }
      fdsMarkDirty(off);
    }
    if (bytes < FDS_PAGE_SIZE)
    {
      BYTE skip[64];
      DWORD remain = FDS_PAGE_SIZE - bytes;
      while (remain)
      {
        DWORD chunk = remain < sizeof(skip) ? remain : sizeof(skip);
        if (f_read(&fds_sidecar_fil, skip, chunk, &br) != FR_OK) { f_close(&fds_sidecar_fil); return false; }
        remain -= chunk;
      }
    }
    ++loaded;
  }
  f_close(&fds_sidecar_fil);
  printf("FDS sidecar: applied %d dirty page(s) from %s\n", loaded, filePath);
  return true;
}

static bool fdsSaveSidecarToFile(const char *filePath, int storedSideCount)
{
  if (!IsFDS || !fds_expanded || !filePath) return false;
  if (!fdsHasDirtyPages())
  {
    printf("FDS sidecar: no dirty pages, skipping write of %s.\n", filePath);
    return true;
  }

  int numPages = fdsTotalPages();
  int bmBytes  = (numPages + 7) / 8;

  FRESULT fr = f_open(&fds_sidecar_fil, filePath, FA_CREATE_ALWAYS | FA_WRITE);
  if (fr != FR_OK)
  {
    printf("FDS sidecar: cannot open %s for write (%d)\n", filePath, fr);
    return false;
  }

  BYTE hdr[8] = {
    FDS_SIDECAR_MAGIC0, FDS_SIDECAR_MAGIC1,
    FDS_SIDECAR_MAGIC2, FDS_SIDECAR_MAGIC3,
    FDS_SIDECAR_VERSION,
    (BYTE)storedSideCount,
    (BYTE)bmBytes,
    0
  };
  UINT bw = 0;
  if (f_write(&fds_sidecar_fil, hdr, sizeof(hdr), &bw) != FR_OK || bw != sizeof(hdr)) goto fail;
  if (f_write(&fds_sidecar_fil, fds_dirty_bitmap, bmBytes, &bw) != FR_OK || bw != (UINT)bmBytes) goto fail;

  {
    DWORD totalBytes = fds_expanded_total_size;
    int saved = 0;
    for (int p = 0; p < numPages; ++p)
    {
      if (!fdsPageDirty((unsigned)p)) continue;
      DWORD off = (DWORD)p * FDS_PAGE_SIZE;
      DWORD bytes = FDS_PAGE_SIZE;
      if (off >= totalBytes) bytes = 0;
      else if (off + bytes > totalBytes) bytes = totalBytes - off;

      if (bytes)
      {
        if (f_write(&fds_sidecar_fil, fds_expanded + off, bytes, &bw) != FR_OK ||
            bw != bytes) goto fail;
      }
      if (bytes < FDS_PAGE_SIZE)
      {
        static const BYTE zeros[64] = {0};
        DWORD remain = FDS_PAGE_SIZE - bytes;
        while (remain)
        {
          DWORD chunk = remain < sizeof(zeros) ? remain : sizeof(zeros);
          if (f_write(&fds_sidecar_fil, zeros, chunk, &bw) != FR_OK || bw != chunk) goto fail;
          remain -= chunk;
        }
      }
      ++saved;
    }
    f_close(&fds_sidecar_fil);
    printf("FDS sidecar: wrote %d dirty page(s) to %s\n", saved, filePath);
    return true;
  }

fail:
  printf("FDS sidecar: write failed for %s\n", filePath);
  f_close(&fds_sidecar_fil);
  return false;
}

/*-------------------------------------------------------------------*/
/*  fdsRebuildForSide: flush dirty pages for the current side, then  */
/*  re-expand a different side from the raw disk image. Only used in */
/*  single-side mode (no PSRAM). No-op in multi-side mode.           */
/*-------------------------------------------------------------------*/
static bool fdsRebuildForSide(int newSide)
{
  if (!fds_single_side_mode) return true;
  if (fds_expanded_side == newSide) return true;
  if (!fds_expanded || !FDS_DiskImage) return false;

  printf("FDS rebuild: save dirty pages for side %d\n", fds_expanded_side);
  if (fds_save_base[0] && fdsHasDirtyPages())
  {
    snprintf(fds_sidecar_path, sizeof(fds_sidecar_path),
             "%s_s%d.SAV", fds_save_base, fds_expanded_side);
    fdsSaveSidecarToFile(fds_sidecar_path, 1);
  }
  fdsClearDirtyBitmap();

  printf("FDS rebuild: expanding side %d\n", newSide);
  memset(fds_expanded, 0, FDS_PER_SIDE_MAX);
  fds_side_offsets[newSide] = 0;
  fdsExpandOneSide(FDS_DiskImage, newSide, fds_expanded, 0);
  fds_expanded_side = newSide;
  fds_expanded_total_size = FDS_PER_SIDE_MAX;

  if (fds_save_base[0])
  {
    printf("FDS rebuild: loading sidecar for side %d\n", newSide);
    snprintf(fds_sidecar_path, sizeof(fds_sidecar_path),
             "%s_s%d.SAV", fds_save_base, newSide);
    fdsLoadSidecarFromFile(fds_sidecar_path, 1);
  }

  printf("FDS: rebuilt expanded buffer for side %d\n", newSide);
  return true;
}

#endif

static void fdsCompleteSideSwitch(int newSide)
{
  FDS_CurrentSide = newSide;
  FDS_DiskInserted = true;
  fdsResetTransferPosition();
  fds_motor_spinup = 50;
  fds_byte_irq_pend = 0;
  fds_transfer_complete = 0;
}

#if PICO_RP2350
void fdsCheckPendingRebuild()
{
  int side = fds_rebuild_pending_side;
  if (side < 0) return;
  fds_rebuild_pending_side = -1;
  printf("FDS: deferred rebuild starting for side %d (from side %d)\n",
         side, fds_expanded_side);
  fdsRebuildForSide(side);
  printf("FDS: deferred rebuild complete, switching to side %d\n", side);
  fdsCompleteSideSwitch(side);
}
#endif

/*-------------------------------------------------------------------*/
/*  Public sidecar API. basePath is the path stem without extension   */
/*  (e.g. "/saves/game_fds"). Multi-side mode appends ".SAV";        */
/*  single-side mode appends "_s0.SAV", "_s1.SAV", etc.              */
/*-------------------------------------------------------------------*/

void fdsSetSaveBasePath(const char *basePath)
{
  if (basePath)
    strncpy(fds_save_base, basePath, FDS_SAVE_BASE_LEN - 1);
  fds_save_base[FDS_SAVE_BASE_LEN - 1] = '\0';
}

bool fdsLoadSidecar(const char *basePath)
{
#if !PICO_RP2350
  (void)basePath;
  return false;
#else
  if (basePath) fdsSetSaveBasePath(basePath);
  if (fds_single_side_mode)
  {
    snprintf(fds_sidecar_path, sizeof(fds_sidecar_path),
             "%s_s%d.SAV", basePath, FDS_CurrentSide);
    return fdsLoadSidecarFromFile(fds_sidecar_path, 1);
  }
  snprintf(fds_sidecar_path, sizeof(fds_sidecar_path), "%s.SAV", basePath);
  return fdsLoadSidecarFromFile(fds_sidecar_path, FDS_NumSides);
#endif
}

bool fdsSaveSidecar(const char *basePath)
{
#if !PICO_RP2350
  (void)basePath;
  return false;
#else
  if (basePath) fdsSetSaveBasePath(basePath);
  if (fds_single_side_mode)
  {
    snprintf(fds_sidecar_path, sizeof(fds_sidecar_path),
             "%s_s%d.SAV", basePath, FDS_CurrentSide);
    return fdsSaveSidecarToFile(fds_sidecar_path, 1);
  }
  snprintf(fds_sidecar_path, sizeof(fds_sidecar_path), "%s.SAV", basePath);
  return fdsSaveSidecarToFile(fds_sidecar_path, FDS_NumSides);
#endif
}

/*-------------------------------------------------------------------*/
/*  fdsAutoInsertCheck: Mesen2 $E445 intercept.                      */
/*  Called when the CPU PC reaches $E445 (BIOS disk-verify routine). */
/*  Reads the 10-byte disk-header buffer that BIOS expects at the    */
/*  address stored in zero-page $00-$01, matches it against all      */
/*  disk-side headers, and auto-inserts the correct side.            */
/*-------------------------------------------------------------------*/
void fdsAutoInsertCheck()
{
  if (!FDS_AutoInsertEnabled || !fds_auto_insert_enabled || !IsFDS || !FDS_DiskImage) {
    FDS_LOG("$E445: skip (enabled=%d IsFDS=%d img=%p)\n",
            fds_auto_insert_enabled, IsFDS, FDS_DiskImage);
    return;
  }

  /* Read buffer address from zero-page $00-$01. */
  WORD bufferAddr = RAM[0x00] | ((WORD)RAM[0x01] << 8);
  FDS_LOG("$E445: bufAddr=$%04X curSide=%d\n", bufferAddr, FDS_CurrentSide);

  /* Read 10 bytes from that address (using direct memory access). */
  BYTE buffer[10];
  for (int i = 0; i < 10; i++)
  {
    WORD addr = bufferAddr + i;
    /* Prevent infinite recursion: skip if the address is $E445 itself */
    if (addr == 0xE445) { buffer[i] = 0xFF; continue; }
    /* Read from the appropriate memory region */
    if (addr < 0x2000)
      buffer[i] = RAM[addr & 0x7FF];
    else if (addr >= 0x6000 && addr < 0x8000)
      buffer[i] = FDS_PrgRam ? FDS_PrgRam[addr - 0x6000] : 0;
    else if (addr >= 0x8000 && addr < 0xE000)
      buffer[i] = FDS_PrgRam ? FDS_PrgRam[(addr - 0x8000) + 0x2000] : 0;
    else if (addr >= 0xE000)
      buffer[i] = FDS_Bios ? FDS_Bios[addr & 0x1FFF] : 0;
    else
      buffer[i] = 0xFF; /* PPU/APU range — treat as wildcard */
  }

  /* Match against disk-side headers. Each side's header starts at
     offset 0 in the raw .fds side data (block type 1 = disk info block,
     56 bytes). The 10-byte comparison region starts at offset 14
     (manufacturer code + game name). 0xFF in the buffer = wildcard. */
  /* Dump buffer bytes for debugging. */
  FDS_LOG("$E445: buf[10]=");
  for (int i = 0; i < 10; i++) FDS_LOG(" %02X", buffer[i]);
  FDS_LOG("\n");

  int matchCount = 0;
  int matchIndex = -1;
  for (int s = 0; s < FDS_NumSides; s++)
  {
    /* Offset 15 = manufacturer code (skip block type 0x01 + 14-byte
       "*NINTENDO-HVC*" string).  Mesen2 uses [i+14] but its header
       array already strips the block-type byte. */
    BYTE *sideHeader = FDS_DiskImage + (DWORD)s * FDS_SIDE_SIZE + 15;
    FDS_LOG("$E445: hdr[%d]=", s);
    for (int i = 0; i < 10; i++) FDS_LOG(" %02X", sideHeader[i]);
    FDS_LOG("\n");
    bool match = true;
    for (int i = 0; i < 10; i++)
    {
      if (buffer[i] != 0xFF && buffer[i] != sideHeader[i])
      {
        match = false;
        break;
      }
    }
    if (match)
    {
      matchCount++;
      matchIndex = (matchCount > 1) ? -1 : s;
    }
  }

  FDS_LOG("$E445: matchCount=%d matchIndex=%d\n", matchCount, matchIndex);

  if (matchCount > 1)
  {
    /* Multiple sides match — disable auto-insert to avoid loops. */
    fds_auto_insert_enabled = false;
    FDS_LOG("Auto-insert disabled: %d sides match header\n", matchCount);
    return;
  }

  if (matchIndex >= 0 && matchIndex != FDS_CurrentSide)
  {
    /* Switch to the matched side. */
    if (fds_single_side_mode) {
      fds_rebuild_pending_side = matchIndex;
    } else {
      fdsCompleteSideSwitch(matchIndex);
    }
    /* Cancel retry timer — we found the right disk. */
    fds_auto_retry_counter = -1;
    fds_auto_eject_delay = -1;
    fds_4032_read_count = 0;
    FDS_LOG("Auto-inserted: Disk %d Side %c\n",
            (matchIndex / 2) + 1, (matchIndex & 1) ? 'B' : 'A');
  }
  else if (matchIndex >= 0)
  {
    /* Same side already inserted — cancel retry, BIOS will proceed.
       Reset disk_read_once so the $4032 polling detection won't
       fire spuriously before the next full read cycle. */
    fds_auto_retry_counter = -1;
    fds_auto_eject_delay = -1;
    fds_4032_read_count = 0;
    fds_disk_read_once = false;
  }
}

/*-------------------------------------------------------------------*/
/*  fdsResetDrive: clear all drive state. Called from Map20_Init.    */
/*-------------------------------------------------------------------*/
void fdsResetDrive()
{
  fds_timer_reload   = 0;
  fds_timer_counter  = 0;
  fds_timer_irq_en   = 0;
  fds_timer_repeat   = 0;
  fds_timer_irq_pend = 0;

  fds_master_enable  = 0;

  fds_motor_on   = 0;
  fds_xfer_reset = 1;
  fds_read_mode  = 1;
  fds_byte_irq_en = 0;
  fds_scan_start = 0;

  fds_byte_pos      = 0;
  fds_read_buf      = 0;
  fds_byte_irq_pend = 0;
  fds_transfer_complete = 0;
  fds_end_of_head   = 0;
  fds_cycle_acc     = 0;
  fds_motor_spinup  = 0;
  fds_scanning_started = 0;
  fds_crc_control   = 0;
  fds_gap_ended     = 0;

  /* Cancel any in-flight disk swap so a reset during the eject pulse
     doesn't leave the disk permanently ejected. */
  fds_eject_counter = 0;
  fds_pending_side  = -1;
  if (settings.flags.autoInsertDiskA)
  {
    FDS_DiskInserted = true;
    fds_manual_insert_pending = false;
  }
  else
  {
    FDS_DiskInserted = false;
    fds_manual_insert_pending = true;
  }
  fds_manual_insert_delay = 60;

  /* Reset auto-disk-insert state. */
  fds_auto_insert_enabled = true;
  fds_4032_read_count     = 0;
  fds_last_read_hsync     = 0;
  fds_hsync_counter       = 0;
  fds_auto_eject_delay    = -1;
  fds_auto_retry_counter  = -1;
  fds_auto_insert_attempts = 0;
  fds_previous_side = 0;
  fds_end_of_head_cooldown = 0;
  fds_disk_read_once = false;

  fds_write_buf = 0;
  fds_block_count = 0;

  /* Reset FDS expansion audio state. */
  fdsResetAudio();

  FDS_LOG("reset drive\n");
}

/*-------------------------------------------------------------------*/
/*  fdsApuWrite: $4020-$4026 disk drive registers.                   */
/*  Called for every write in $4018-$5FFF; ignore other addresses.   */
/*-------------------------------------------------------------------*/
void fdsApuWrite(WORD wAddr, BYTE byData)
{
  switch (wAddr)
  {
  case 0x4020: /* IRQ reload low byte */
    fds_timer_reload = (fds_timer_reload & 0xFF00) | byData;
    FDS_LOG("4020 = %02X (reload=%04X)\n", byData, fds_timer_reload);
    break;

  case 0x4021: /* IRQ reload high byte */
    fds_timer_reload = (fds_timer_reload & 0x00FF) | ((WORD)byData << 8);
    FDS_LOG("4021 = %02X (reload=%04X)\n", byData, fds_timer_reload);
    break;

  case 0x4022: /* IRQ control */
    fds_timer_repeat = byData & 0x01;
    fds_timer_irq_en = (byData >> 1) & 0x01;
    FDS_LOG("4022 = %02X (en=%d rep=%d reload=%04X)\n",
            byData, (int)fds_timer_irq_en, (int)fds_timer_repeat,
            fds_timer_reload);
    if (fds_timer_irq_en)
    {
      /* Load counter on enable, regardless of master_enable. Counter
         only counts down when master_enable bit 0 is set, but the
         load itself happens here so a BIOS that configures $4022
         before $4023 still ends up with a primed counter. */
      fds_timer_counter = fds_timer_reload;
    }
    else
    {
      /* Bit 1 = 0 acks the timer IRQ. */
      fds_timer_irq_pend = 0;
    }
    break;

  case 0x4023: /* Master enable */
    FDS_LOG("4023 = %02X\n", byData);
    fds_master_enable = byData & 0x03;
    if (!(fds_master_enable & 0x01))
    {
      /* Disabling disk regs only acks pending IRQs — fds_timer_irq_en
         must NOT be cleared here (BIOS uses $4023 to ack/re-enable). */
      fds_timer_irq_pend = 0;
      fds_byte_irq_pend = 0;
      fds_transfer_complete = 0;
    }
    break;

  case 0x4024: /* Disk write data */
    fds_write_buf = byData;
    fds_byte_irq_pend = 0;
    fds_transfer_complete = 0;
    /* Mirror the $4031 read: reset the byte-pacing accumulator so the
       next byte production is a full FDS_CYC_PER_BYTE cycles away.
       Without this, the drive emits multiple write IRQs faster than
       the BIOS can run its handler, so BIOS overshoots past payload
       into the CRC slots before it gets a chance to assert crc=1. */
    fds_cycle_acc = 0;
    break;

  case 0x4025: /* Disk control */
  {
    BYTE prev_motor    = fds_motor_on;
    BYTE prev_xfer_rst = fds_xfer_reset;
    BYTE prev_scan     = fds_scan_start;
    BYTE prev_crc_control = fds_crc_control;
    (void)prev_scan;
    if (fds_block_count <= 80) {
      FDS_LOG("4025 = %02X (mot=%d rst=%d rd=%d mir=%d crc=%d scan=%d irq=%d) pos=%lu ge=%d\n",
              byData,
              byData & 1, (byData >> 1) & 1, (byData >> 2) & 1,
              (byData >> 3) & 1, (byData >> 4) & 1,
              (byData >> 6) & 1, (byData >> 7) & 1,
              (unsigned long)fds_byte_pos, (int)fds_gap_ended);
    }

    fds_motor_on    = byData & 0x01;
    fds_xfer_reset  = (byData >> 1) & 0x01;
    fds_read_mode   = (byData >> 2) & 0x01;
    /* bit 3: 1 = horizontal mirroring, 0 = vertical (opposite of NES). */
    InfoNES_Mirroring((byData & 0x08) ? 0 : 1);
    fds_crc_control = (byData >> 4) & 0x01;
    fds_scan_start  = (byData >> 6) & 0x01;
    fds_byte_irq_en = (byData >> 7) & 0x01;

    /* Per Mesen2 (Fds.cpp $4025 handler): every $4025 write clears
       the IRQ source, but NOT the transfer-complete flag.
       The game may poll $4030 for bit 1 without using IRQs. */
    fds_byte_irq_pend = 0;
    /* NOTE: do NOT clear fds_transfer_complete here. */

    /* Motor turning on: head returns to start of disk. */
    if (!prev_motor && fds_motor_on)
    {
      fdsResetTransferPosition();
      fds_motor_spinup = 50;
    }
    if (!fds_motor_on)
    {
      fds_scanning_started = 0;
    }

    /* Transfer reset asserted (rst 0→1): the BIOS uses this to reset
       the bit-stream synchroniser before starting a new block scan.
       byte_pos is unchanged; gap_ended → 0 makes the next scan walk
       silently to the next block start, which is how file-skip and
       block-boundary resync both work. */
    if (!prev_xfer_rst && fds_xfer_reset)
    {
      fds_gap_ended = 0;
    }

    /* NOTE: do NOT reset gap_ended on CRC control rising edge in read
       mode.  Mesen2 doesn't do this — the BIOS itself clears gap_ended
       by writing scan_start=0 ($4025 bit 6) between blocks.  Our fake
       CRC bytes are non-zero, so resetting gap_ended here would cause
       the CRC byte to immediately re-trigger gap_ended=1. */

    /* CRC control rising edge during a write: BIOS just signalled
       end-of-payload. For a synthesized block (one we created on the
       fly past the last existing block_end), this is the moment we
       learn how big it is. Set bends[lastBlock] = byte_pos + 2 so the
       auto-flip-ge=0 trigger fires after the 2 CRC slots are written.
       Existing blocks already have a baked-in bends from the buffer
       build and don't need updating. */
    if (!prev_crc_control && fds_crc_control && !fds_read_mode &&
        fds_gap_ended && IsFDS &&
        FDS_CurrentSide >= 0 && FDS_CurrentSide < FDS_MAX_SIDES)
    {
      int n = fds_block_counts[FDS_CurrentSide];
      if (n > 0)
      {
        DWORD lastStart = fds_block_starts[FDS_CurrentSide][n - 1];
        DWORD lastEnd   = fds_block_ends[FDS_CurrentSide][n - 1];
        if (lastStart == lastEnd && fds_byte_pos > lastStart)
        {
          fds_block_ends[FDS_CurrentSide][n - 1] = fds_byte_pos + 2;
        }
      }
    }
    break;
  }

  case 0x4026: /* External output, unused by most software */
    break;

  default:
    /*---------------------------------------------------------------*/
    /*  FDS expansion audio registers ($4040-$408A).                 */
    /*---------------------------------------------------------------*/
    if (wAddr >= 0x4040 && wAddr <= 0x407F)
    {
      /* Wavetable write — only when wave write is enabled ($4089.7). */
      if (fds_wave_write_enabled)
        fds_wave_table[wAddr & 0x3F] = byData & 0x3F;
    }
    else
    {
      switch (wAddr)
      {
      case 0x4080: /* Volume envelope */
        fds_vol_speed    = byData & 0x3F;
        fds_vol_increase = (byData >> 6) & 0x01;
        fds_vol_env_off  = (byData >> 7) & 0x01;
        /* Writing resets the envelope timer. */
        fds_vol_timer = (DWORD)8 * (fds_vol_speed + 1) * fds_master_env_speed;
        if (fds_vol_env_off)
          fds_vol_gain = fds_vol_speed; /* gain = speed when envelope disabled */
        break;

      case 0x4082: /* Wave frequency low */
        fds_vol_frequency = (fds_vol_frequency & 0x0F00) | byData;
        fdsUpdateModOutput(fds_vol_frequency);
        break;

      case 0x4083: /* Wave frequency high + control */
        fds_disable_envelopes = (byData >> 6) & 0x01;
        fds_halt_waveform     = (byData >> 7) & 0x01;
        if (fds_halt_waveform)
          fds_wave_position = 0;
        if (fds_disable_envelopes)
        {
          fds_vol_timer = (DWORD)8 * (fds_vol_speed + 1) * fds_master_env_speed;
          fds_mod_timer = (DWORD)8 * (fds_mod_speed + 1) * fds_master_env_speed;
        }
        fds_vol_frequency = (fds_vol_frequency & 0xFF) | (((WORD)byData & 0x0F) << 8);
        fdsUpdateModOutput(fds_vol_frequency);
        break;

      case 0x4084: /* Mod envelope */
        fds_mod_speed    = byData & 0x3F;
        fds_mod_increase = (byData >> 6) & 0x01;
        fds_mod_env_off  = (byData >> 7) & 0x01;
        fds_mod_timer = (DWORD)8 * (fds_mod_speed + 1) * fds_master_env_speed;
        if (fds_mod_env_off)
          fds_mod_gain = fds_mod_speed;
        fdsUpdateModOutput(fds_vol_frequency);
        break;

      case 0x4085: /* Mod counter */
        fdsUpdateModCounter((int8_t)(byData & 0x7F));
        fdsUpdateModOutput(fds_vol_frequency);
        break;

      case 0x4086: /* Mod frequency low */
        fds_mod_frequency = (fds_mod_frequency & 0x0F00) | byData;
        break;

      case 0x4087: /* Mod frequency high + disable */
        fds_mod_frequency = (fds_mod_frequency & 0xFF) | (((WORD)byData & 0x0F) << 8);
        fds_mod_disabled = (byData >> 7) & 0x01;
        if (fds_mod_disabled)
          fds_mod_overflow = 0;
        break;

      case 0x4088: /* Mod table write */
        /* Only effective when mod unit is disabled ($4087.7). */
        if (fds_mod_disabled)
        {
          fds_mod_table[fds_mod_table_pos & 0x3F] = byData & 0x07;
          fds_mod_table[(fds_mod_table_pos + 1) & 0x3F] = byData & 0x07;
          fds_mod_table_pos = (fds_mod_table_pos + 2) & 0x3F;
        }
        break;

      case 0x4089: /* Master volume + wave write enable */
        fds_master_volume = byData & 0x03;
        fds_wave_write_enabled = (byData >> 7) & 0x01;
        break;

      case 0x408A: /* Master envelope speed */
        fds_master_env_speed = byData;
        break;

      default:
        break;
      }
    }
    break;
  }
}

/*-------------------------------------------------------------------*/
/*  fdsApuRead: $4030-$4033 disk drive status / data.                */
/*-------------------------------------------------------------------*/
BYTE fdsApuRead(WORD wAddr)
{
  switch (wAddr)
  {
  case 0x4030: /* Disk status */
  {
    BYTE r = 0;
    if (fds_timer_irq_pend)   r |= 0x01;
    if (fds_transfer_complete) r |= 0x02;
    /* Mesen2: bit 6 (_endOfHead) is NOT reported in $4030.
       Reporting it causes games like Zelda to misinterpret the status
       after reaching end-of-disk and hang instead of retrying. */
    /* Reading $4030 acknowledges the timer IRQ (bit 0) and
       the transfer-complete flag (bit 1), and clears the IRQ line. */
    fds_timer_irq_pend    = 0;
    fds_byte_irq_pend     = 0;
    fds_transfer_complete = 0;
    if (r) FDS_LOG("  4030 -> %02X (pos=%lu ge=%d)\n", r,
                   (unsigned long)fds_byte_pos, (int)fds_gap_ended);
    return r;
  }

  case 0x4031: /* Disk read data */
    FDS_BYTELOG("  4031 -> %02X (pos=%lu ge=%d pend=%d)\n",
                fds_read_buf, (unsigned long)fds_byte_pos, (int)fds_gap_ended,
                (int)fds_transfer_complete);
    fds_transfer_complete = 0;
    fds_byte_irq_pend = 0;
    /* Reset the byte-pacing accumulator so the next byte production
       is a full FDS_CYC_PER_BYTE cycles away. Our HSync-quantised IRQ
       delivery would otherwise hand the BIOS the next byte before it
       can finish its IRQ handler and write $4025 (which clears the
       IRQ source). Without this delay the BIOS handler runs back-to-
       back twice — first time it gets the last data byte, second
       time it gets the CRC byte that the BIOS interprets as the next
       block's type byte (→ ERR.23 / ERR.24). */
    fds_cycle_acc = 0;
    return fds_read_buf;

  case 0x4032: /* Drive status: bit0=disk-not-inserted, bit1=disk-not-ready, bit2=write-protect */
  {
    BYTE r = 0;
    if (!FDS_DiskInserted)  r |= 0x05; /* not-inserted and not-ready */
    else if (!fds_motor_on || fds_motor_spinup > 0) r |= 0x02;
    /* Bit 2 (write protect) left clear: simulating a writable disk. */

    /* Mesen2-style auto-disk-insert: track successive $4032 reads.
       When the game polls this register many times in quick succession
       it usually means it's waiting for a different disk side.
       Only fires after end-of-head cooldown has expired (Mesen2:
       _autoDiskEjectCounter == 0). */
    if (FDS_AutoInsertEnabled && fds_auto_insert_enabled && FDS_DiskInserted &&
        fds_auto_eject_delay < 0 && fds_eject_counter == 0 &&
        fds_disk_read_once && fds_end_of_head_cooldown == 0)
    {
      int elapsed = fds_hsync_counter - fds_last_read_hsync;
      if (elapsed < FDS_4032_CHECK_WINDOW)
      {
        fds_4032_read_count++;
      }
      else
      {
        fds_4032_read_count = 0;
      }
      fds_last_read_hsync = fds_hsync_counter;

      if (fds_4032_read_count > FDS_4032_CHECK_THRESHOLD &&
          fds_auto_insert_attempts < FDS_AUTO_MAX_ATTEMPTS)
      {
        /* Auto-eject: the game wants a different side. */
        fds_previous_side = FDS_CurrentSide;
        FDS_DiskInserted = false;
        fds_auto_eject_delay = FDS_AUTO_EJECT_FRAMES * 262;
        fds_4032_read_count = 0;
        fds_auto_insert_attempts++;
        r |= 0x05; /* reflect the ejection immediately */
        FDS_LOG("Auto-eject triggered (attempt %d)\n", fds_auto_insert_attempts);
      }
    }
    return r;
  }

  case 0x4033: /* External input: battery good = $80 */
    return 0x80;

  default:
    /*---------------------------------------------------------------*/
    /*  FDS expansion audio read registers.                          */
    /*---------------------------------------------------------------*/
    if (wAddr >= 0x4040 && wAddr <= 0x407F)
    {
      BYTE v = (BYTE)(wAddr >> 8) & 0xC0; /* open bus high bits */
      if (fds_wave_write_enabled)
        v |= fds_wave_table[wAddr & 0x3F];
      else
        v |= fds_wave_table[fds_wave_position];
      return v;
    }
    if (wAddr == 0x4090)
      return ((BYTE)(wAddr >> 8) & 0xC0) | (fds_vol_gain & 0x3F);
    if (wAddr == 0x4092)
      return ((BYTE)(wAddr >> 8) & 0xC0) | (fds_mod_gain & 0x3F);

    return (BYTE)(wAddr >> 8); /* open bus */
  }
}

/*-------------------------------------------------------------------*/
/*  fdsRenderAudio: produce n audio samples into fds_wave_buffer.    */
/*  Called from InfoNES_pAPUHsync() once per scanline, typically     */
/*  producing ~3 samples at 44100 Hz.                                */
/*                                                                   */
/*  Adapts Mesen2's per-CPU-cycle ClockAudio() to the batch model    */
/*  using event-driven simulation: between events (envelope ticks,   */
/*  modulator ticks), the wave-phase accumulator advances linearly,  */
/*  so a span of N event-free cycles collapses to a single 32-bit    */
/*  multiply+shift instead of N per-cycle adds. This is critical for */
/*  RP2350 performance — the naive per-cycle loop costs ~3ms/frame.  */
/*                                                                   */
/*  At sample-period boundaries we point-sample the output using     */
/*  Mesen2's exact per-cycle formula:                                */
/*    output = (waveTable[pos] * gain * volTable[masterVol]) / 1152  */
/*  giving a 0..63 range that's then mixed via wave6 (×18 in mixer). */
/*-------------------------------------------------------------------*/
void __not_in_flash_func(fdsRenderAudio)(unsigned int n)
{
  if (!fds_wave_buffer) return;

  const unsigned int cyc_per_sample = ApuCyclesPerSample;

  for (unsigned int i = 0; i < n; i++)
  {
    unsigned int cycles_left = cyc_per_sample;

    while (cycles_left > 0)
    {
      /* Determine how many cycles until the next state-changing event.
         If no events possible (envelopes off, mod disabled, halted),
         we can fast-forward the entire remaining sample period. */
      unsigned int span = cycles_left;

      bool env_active = !fds_halt_waveform && !fds_disable_envelopes &&
                        fds_master_env_speed > 0;

      if (env_active)
      {
        if (!fds_vol_env_off && fds_vol_timer > 0 && fds_vol_timer < span)
          span = (unsigned int)fds_vol_timer;
        if (!fds_mod_env_off && fds_mod_timer > 0 && fds_mod_timer < span)
          span = (unsigned int)fds_mod_timer;
      }

      /* Modulator overflow event: cycles until 16-bit overflow of
         (fds_mod_overflow + k * fds_mod_frequency). */
      bool mod_active = !fds_mod_disabled && fds_mod_frequency > 0;
      if (mod_active)
      {
        unsigned int needed = 0x10000u - (unsigned int)fds_mod_overflow;
        unsigned int cycles_to_overflow =
            (needed + fds_mod_frequency - 1) / fds_mod_frequency;
        if (cycles_to_overflow == 0) cycles_to_overflow = 1;
        if (cycles_to_overflow < span) span = cycles_to_overflow;
      }

      if (span == 0) span = 1;

      /* Advance wave-phase accumulator over `span` cycles in one shot. */
      int delta = (int)fds_vol_frequency + fds_mod_output;
      if (!fds_halt_waveform && delta > 0)
      {
        uint32_t total = (uint32_t)fds_wave_overflow +
                         (uint32_t)span * (uint32_t)delta;
        unsigned int carries = total >> 16;
        fds_wave_overflow = (WORD)(total & 0xFFFF);
        if (carries)
          fds_wave_position = (BYTE)((fds_wave_position + carries) & 0x3F);
      }

      /* Advance modulator overflow accumulator. */
      if (mod_active)
      {
        uint32_t mod_total = (uint32_t)fds_mod_overflow +
                             (uint32_t)span * (uint32_t)fds_mod_frequency;
        if (mod_total >= 0x10000u)
        {
          /* One overflow per span by construction (span = cycles to
             next overflow). Tick the modulator. */
          int offset = fds_mod_lut[fds_mod_table[fds_mod_table_pos]];
          fdsUpdateModCounter(offset == FDS_MOD_RESET ? 0 :
                              (int8_t)(fds_mod_counter + offset));
          fds_mod_table_pos = (fds_mod_table_pos + 1) & 0x3F;
          fds_mod_overflow = (WORD)(mod_total & 0xFFFF);
          fdsUpdateModOutput(fds_vol_frequency);
        }
        else
        {
          fds_mod_overflow = (WORD)mod_total;
        }
      }

      /* Advance envelope timers in bulk. */
      if (env_active)
      {
        if (!fds_vol_env_off)
        {
          if (fds_vol_timer > span)
          {
            fds_vol_timer -= span;
          }
          else
          {
            /* Timer expires within this span: tick the envelope. */
            fds_vol_timer = (DWORD)8 * (fds_vol_speed + 1) * fds_master_env_speed;
            if (fds_vol_increase && fds_vol_gain < 32)
              fds_vol_gain++;
            else if (!fds_vol_increase && fds_vol_gain > 0)
              fds_vol_gain--;
          }
        }
        if (!fds_mod_env_off)
        {
          if (fds_mod_timer > span)
          {
            fds_mod_timer -= span;
          }
          else
          {
            fds_mod_timer = (DWORD)8 * (fds_mod_speed + 1) * fds_master_env_speed;
            if (fds_mod_increase && fds_mod_gain < 32)
              fds_mod_gain++;
            else if (!fds_mod_increase && fds_mod_gain > 0)
              fds_mod_gain--;
            fdsUpdateModOutput(fds_vol_frequency);
          }
        }
      }

      cycles_left -= span;
    }

    /* Point-sample the output ONCE per sample period using Mesen2's
       formula: (waveTable[pos] * gain * volTable) / 1152, range 0..63.
       Boost ×1.5 to give more weight to FDS audio (real Famicom FDS is
       louder than internal APU). ×2 would clip on bass-heavy passages
       when summed with pulse/triangle through the int16 DVI gain stage;
       ×1.5 gives audible bass without distortion. Final range 0..94. */
    BYTE out = 0;
    if (!fds_wave_write_enabled)
    {
      unsigned int gain = fds_vol_gain < 32 ? fds_vol_gain : 32;
      unsigned int level = gain * FdsWaveVolumeTable[fds_master_volume];
      unsigned int raw = ((unsigned int)fds_wave_table[fds_wave_position] * level) / 1152;
      out = (BYTE)((raw * 3) / 2);
    }
    fds_wave_buffer[i] = out;
  }
}

/*-------------------------------------------------------------------*/
/*  fdsHsync: advance timer + disk byte clocks once per scanline.    */
/*-------------------------------------------------------------------*/
void fdsHsync()
{
  /* When auto-insert is off, wait for A button to insert disk.
     Delay by ~1 second so a held A from the ROM selector doesn't
     immediately trigger insertion. */
  if (fds_manual_insert_pending)
  {
    if (fds_manual_insert_delay > 0 && (fds_hsync_counter % 262) == 0)
      fds_manual_insert_delay--;
    else if (fds_manual_insert_delay == 0 && (PAD1_Latch & 0x01))
    {
      FDS_DiskInserted = true;
      fds_manual_insert_pending = false;
    }
  }

  /* Monotonic hsync counter for auto-insert timing. */
  fds_hsync_counter++;

  /* End-of-head cooldown: counts down once per frame (~262 hsyncs).
     Mesen2 uses a per-frame counter; we approximate with hsyncs. */
  if (fds_end_of_head_cooldown > 0 && (fds_hsync_counter % 262) == 0)
  {
    fds_end_of_head_cooldown--;
  }

  /* Drive the disk-swap eject pulse before anything else: the BIOS
     reads $4032 in its disk-status loop and we want it to see the
     disk-not-inserted bit reliably while fds_eject_counter > 0. */
  if (fds_eject_counter > 0)
  {
    fds_eject_counter--;
    if (fds_eject_counter == 0 && fds_pending_side >= 0 &&
        fds_pending_side < FDS_NumSides)
    {
      if (fds_single_side_mode) {
        fds_rebuild_pending_side = fds_pending_side;
      } else {
        fdsCompleteSideSwitch(fds_pending_side);
      }
      fds_pending_side = -1;
    }
  }

  /* Auto-disk-insert countdown: after auto-eject, wait ~77 frames then
     re-insert disk (side 0 as a placeholder — the $E445 intercept will
     select the correct side when BIOS verifies the header). */
  if (fds_auto_eject_delay > 0)
  {
    fds_auto_eject_delay--;
    if (fds_auto_eject_delay == 0)
    {
      /* Cycle to the next side (Mesen2 behavior).  The $E445 hook
         may still override this if the BIOS buffer requests a
         specific different side. */
      int nextSide = (fds_previous_side + 1) % FDS_NumSides;
      if (fds_single_side_mode) {
        fds_rebuild_pending_side = nextSide;
      } else {
        fdsCompleteSideSwitch(nextSide);
      }
      fds_auto_retry_counter = FDS_AUTO_RETRY_FRAMES * 262;
      FDS_LOG("Auto-insert: side %d (%c) inserted\n",
              nextSide, (nextSide & 1) ? 'B' : 'A');
    }
  }

  /* Auto-insert retry: if the game hasn't successfully read the disk
     within ~200 frames of insertion, eject and try again. */
  if (fds_auto_retry_counter > 0)
  {
    fds_auto_retry_counter--;
    if (fds_auto_retry_counter == 0 &&
        fds_auto_insert_attempts < FDS_AUTO_MAX_ATTEMPTS)
    {
      /* Eject and retry. */
      FDS_DiskInserted = false;
      fds_auto_eject_delay = (FDS_AUTO_EJECT_FRAMES / 2) * 262;
      fds_auto_insert_attempts++;
      FDS_LOG("Auto-insert retry (attempt %d)\n", fds_auto_insert_attempts);
    }
  }

  /* Advance timer. Counts down at CPU rate; reloads if repeat=1.
     Loops so reload values smaller than a scanline still fire the
     correct number of IRQs (otherwise the WORD counter wraps and
     stalls). */
  if (fds_timer_irq_en && (fds_master_enable & 0x01))
  {
    int remaining = STEP_PER_SCANLINE;
    while (remaining > 0 && fds_timer_irq_en)
    {
      if (fds_timer_counter <= (WORD)remaining)
      {
        remaining -= fds_timer_counter;
        fds_timer_irq_pend = 1;
        if (fds_timer_repeat)
        {
          fds_timer_counter = fds_timer_reload;
          if (fds_timer_reload == 0) break; /* avoid infinite loop */
        }
        else
        {
          fds_timer_irq_en = 0;
          fds_timer_counter = 0;
        }
      }
      else
      {
        fds_timer_counter -= remaining;
        remaining = 0;
      }
    }
  }

  /* Drain motor spin-up before the drive starts feeding bytes. */
  if (fds_motor_spinup > 0)
  {
    fds_motor_spinup--;
    /* Once spinup finishes, set scanning_started so rst=1 won't block.
       Mesen2: _scanningDisk=true after delay expires. */
    if (fds_motor_spinup == 0)
      fds_scanning_started = 1;
  }
  else if (fds_motor_on && FDS_DiskInserted &&
           fds_expanded && !fds_end_of_head)
  {
    /* Mesen2: if(_resetTransfer && !_scanningDisk) return;
       rst=1 prevents advancement only until the drive has started
       scanning. Once scanning_started is true, rst=1 just resets
       gap detection — the drive keeps advancing. */
    if (fds_xfer_reset && !fds_scanning_started)
    {
      /* Drive blocked — waiting for spinup or rst release. */
    }
    else
    {
    DWORD sideSize = fds_side_sizes[FDS_CurrentSide];
    BYTE *side     = fds_expanded + fds_side_offsets[FDS_CurrentSide];
    int   numBlocks = fds_block_counts[FDS_CurrentSide];
    DWORD *bends    = fds_block_ends[FDS_CurrentSide];

    fds_cycle_acc += STEP_PER_SCANLINE;
    while (fds_cycle_acc >= FDS_CYC_PER_BYTE && !fds_end_of_head)
    {
      fds_cycle_acc -= FDS_CYC_PER_BYTE;

      if (fds_byte_pos >= sideSize)
      {
        /* End of disk reached. Mesen2 behavior:
           1. Turn off motor (BIOS will turn it back on to rewind)
           2. Fire disk IRQ if enabled ($4025 bit 7)
           3. Start cooldown before auto-eject detection can fire
           NOTE: Mesen2 does NOT set transferComplete here — that flag
           means "a byte is ready in the read buffer", not end-of-head.
           Setting it caused $4030 to return 0x03 instead of 0x01
           after the timer fire, confusing the BIOS retry logic. */
        fds_end_of_head = 1;
        fds_motor_on = 0;
        fds_end_of_head_cooldown = FDS_END_OF_HEAD_COOLDOWN_FRAMES;
        fds_disk_read_once = true;
        if (fds_byte_irq_en) fds_byte_irq_pend = 1;
        FDS_LOG("END of head (side %d, pos=%lu)\n", FDS_CurrentSide,
                (unsigned long)fds_byte_pos);
        break;
      }

      fds_scanning_started = 1;

      if (fds_read_mode)
      {
        /* READ MODE: gap detection applies.
           Mesen2: when diskReady (scan_start) is false, force gap_ended=0.
           When true and byte is non-zero while gap_ended==0, set
           gap_ended=1 and SUPPRESS the IRQ for that one byte.
           The expanded buffer includes a 0x80 mark byte before each
           block (matching Mesen2's AddGaps).  The BIOS expects to
           read the 0x80 sync mark via polling before the IRQ-driven
           block-type byte — so we deliver it normally (no skip). */
        BYTE diskByte = side[fds_byte_pos];
        BYTE need_irq = fds_byte_irq_en;

        if (!fds_scan_start)
        {
          fds_gap_ended = 0;
        }
        else if (diskByte && !fds_gap_ended)
        {
          fds_gap_ended = 1;
          need_irq = 0;  /* Mesen2: suppress IRQ on gap-end byte */
          FDS_LOG("GAP END: pos=%lu byte=0x%02X\n",
                  (unsigned long)fds_byte_pos, diskByte);
        }

        if (fds_gap_ended)
        {
          fds_transfer_complete = 1;
          fds_read_buf = diskByte;
          if (need_irq) fds_byte_irq_pend = 1;
        }

        fds_byte_pos++;
      }
      else
      {
        /* WRITE MODE: Mesen2 always transfers in write mode —
           no gap detection. The BIOS manages positioning via scan_start.
           When crc_control is set, the drive emits CRC bytes (we just
           write zeros as placeholders). When !scan_start (diskReady=0),
           the drive writes 0x00 (creating gap). */
        BYTE writeData;
        if (!fds_scan_start)
        {
          writeData = 0x00;  /* gap byte */
        }
        else if (fds_crc_control)
        {
          writeData = 0x00;  /* CRC placeholder */
        }
        else
        {
          writeData = fds_write_buf;
          fds_transfer_complete = 1;
          fds_byte_irq_pend = 1;
        }
        side[fds_byte_pos] = writeData;
        fdsMarkDirty(fds_side_offsets[FDS_CurrentSide] + fds_byte_pos);
        fds_byte_pos++;
        fds_gap_ended = 0;  /* Mesen2: _gapEnded=false after write */
      }

      /* Cancel any pending auto-insert retry since disk is active. */
      if (fds_auto_retry_counter > 0)
      {
        fds_auto_retry_counter = -1;
        fds_4032_read_count = 0;
      }
    }
    } /* end else (rst check) */
  }

  /* Timer IRQ fires unconditionally on underflow (bit 0). The byte-
     ready flag only asserts the IRQ line when bit 7 of $4025 is set. */
  if (fds_master_enable & 0x01)
  {
    if (fds_timer_irq_pend ||
        (fds_byte_irq_pend && fds_byte_irq_en))
    {
      IRQ_REQ;
    }
  }
}

void fdsRelease()
{
  if (FDS_Bios)   { Frens::f_free(FDS_Bios);   FDS_Bios   = nullptr; }
  if (FDS_PrgRam) { Frens::f_free(FDS_PrgRam); FDS_PrgRam = nullptr; }
  if (FDS_ChrRam) { Frens::f_free(FDS_ChrRam); FDS_ChrRam = nullptr; }
  if (fds_expanded) { Frens::f_free(fds_expanded); fds_expanded = nullptr; }
  fds_expanded_total_size = 0;
  fds_expanded_side = -1;
  fds_single_side_mode = false;
  fds_rebuild_pending_side = -1;
  fds_save_base[0] = '\0';
  for (int s = 0; s < FDS_MAX_SIDES; ++s) {
    fds_side_offsets[s] = 0;
    fds_side_sizes[s]   = 0;
    fds_block_counts[s] = 0;
  }
  /* FDS_DiskImage is a pointer into the loader-owned buffer
     (ROM_FILE_ADDR in PSRAM or flash); freed by the existing ROM unload path. */
  FDS_DiskImage = nullptr;
  FDS_NumSides = 0;
  FDS_CurrentSide = 0;
  fdsClearDirtyBitmap();
}
