/*===================================================================*/
/*                                                                   */
/*  InfoNES_FDS.cpp : Famicom Disk System support                    */
/*                                                                   */
/*  Detection and preflight gate (BIOS file + memory), buffer        */
/*  allocation, BIOS load, Mapper 20 init, drive emulation, disk     */
/*  swap UI, save-data sidecar, audio and save-states.               */
/*                                                                   */
/*  PSRAM is not required: without it the disk image stays where the */
/*  loader put it and only the 4 KB pages the game writes to are     */
/*  copied into RAM (copy-on-write). That is what makes it fit on    */
/*  RP2040.                                                          */
/*                                                                   */
/*===================================================================*/

#include <string.h>
#include <stdio.h>
#include <malloc.h>

#include "InfoNES_FDS.h"
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_pAPU.h"
#include "FrensHelpers.h"

extern unsigned int ApuCyclesPerSample;
extern int g_wPassedClocks;   /* K6502.cpp */
#include "ff.h"
#include "K6502.h"
#include "settings.h"

/* RP2040 keeps the FDS audio in flash: its RAM is better spent on the
   disk pages, and non-FDS games would pay for the code as well. */
#if PICO_RP2350
#define FDS_RAMFUNC(f) __not_in_flash_func(f)
#else
#define FDS_RAMFUNC(f) f
#endif

/*-------------------------------------------------------------------*/
/*  FDS state                                                        */
/*-------------------------------------------------------------------*/

BYTE *FDS_Bios       = nullptr;
BYTE *FDS_PrgRam     = nullptr;
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

/* The BIOS disk-header check at $E445 opens with JSR $E6E3. fdsParse points
   that JSR at FDS_HOOK_ADDR, which is open bus on the FDS; fdsApuRead serves
   JMP $E6E3 there and runs fdsAutoInsertCheck() on the way. This replaces a
   PC == $E445 test on every emulated instruction. */
#define FDS_HOOK_ADDR     0x5FF0
#define FDS_BIOS_HOOK_OFS 0x0445
static bool  fds_hook_patched = false;

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

/* Disk image storage. Side s lives at [s * FDS_PER_SIDE_MAX, + fds_side_sizes[s])
   of one flat offset space, split into FDS_PAGE_SIZE pages.
   - With PSRAM, fds_expanded holds the whole expanded image.
   - Without PSRAM, the raw image stays where the loader put it (flash on
     RP2040) and fds_pages[p] is only allocated when the drive first writes
     to page p (copy-on-write). Every other byte is synthesised from the raw
     image and the block tables. A resident page is always a dirty page. */
static BYTE  *fds_expanded        = nullptr;
static BYTE **fds_pages           = nullptr;
static DWORD  fds_disk_total_size = 0;
static DWORD  fds_side_offsets[FDS_MAX_SIDES] = {0};
static DWORD  fds_side_sizes[FDS_MAX_SIDES]   = {0};

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
static inline bool FDS_RAMFUNC(fdsTickEnvelope)(BYTE speed, BYTE env_off,
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
static void FDS_RAMFUNC(fdsUpdateModOutput)(WORD volumePitch)
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
static inline void FDS_RAMFUNC(fdsUpdateModCounter)(int8_t value)
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

static inline bool FDS_RAMFUNC(fdsTickModulator)()
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
 *  fdsBuildDisk() expands the raw .fds at load time into a
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

/* Allocated per side in fdsParse, so a non-FDS game does not carry 5 KB
   of tables in static RAM. */
static DWORD (*fds_block_starts)[FDS_MAX_BLOCKS_PER_SIDE] = nullptr;
/* fds_block_ends[s][i] = position one past the last CRC byte of block i.
   When emit advances pos to this value, we've just finished block i — flip
   gap_ended=0 so the inter-block gap zeros are walked silently rather than
   emitted to BIOS as bogus data bytes. */
static DWORD (*fds_block_ends)[FDS_MAX_BLOCKS_PER_SIDE] = nullptr;
static int   fds_block_counts[FDS_MAX_SIDES] = {0};

static BYTE  fds_gap_ended = 0;   /* 0 = walking through gap zeros + 0x80 mark, 1 = emitting */

/*-------------------------------------------------------------------*/
/*  fdsExpandOneSide: expand a single side of the raw .fds into dest */
/*  (the start of that side in the expanded image), writing gaps +   */
/*  marks + CRC slots. Records block metadata in fds_block_starts/   */
/*  ends/counts for side sideIndex. With dest == nullptr only the    */
/*  metadata is built, for the copy-on-write image.                  */
/*-------------------------------------------------------------------*/
#define FDS_PER_SIDE_MAX  (FDS_SIDE_SIZE + 8192)
#define FDS_BLOCK_OVERHEAD (3 + FDS_INTER_BLOCK_GAP) /* mark + 2 CRC + gap */

static void fdsExpandOneSide(const BYTE *rawDisk, int sideIndex, BYTE *dest)
{
  DWORD writePos = FDS_INITIAL_GAP_BYTES;

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
    /* A side's gaps and marks must not run into the next side. */
    if (writePos + 3 + (DWORD)blockSize > FDS_PER_SIDE_MAX) goto done;

    fds_block_starts[sideIndex][blkIdx] = writePos;
    if (dest)
    {
      dest[writePos] = FDS_MARK_BYTE;
      memcpy(&dest[writePos + 1], &rawDisk[srcBase + srcPos], (size_t)blockSize);
      dest[writePos + 1 + blockSize] = FDS_FAKE_CRC_HI;
      dest[writePos + 2 + blockSize] = FDS_FAKE_CRC_LO;
    }
    writePos += 3 + blockSize;
    srcPos   += blockSize;

    fds_block_ends[sideIndex][blkIdx] = writePos;
    blkIdx++;

    writePos += FDS_INTER_BLOCK_GAP;
  }
done:
  fds_block_counts[sideIndex] = blkIdx;
  if (writePos > FDS_PER_SIDE_MAX) writePos = FDS_PER_SIDE_MAX;
  fds_side_sizes[sideIndex] = writePos < FDS_SIDE_SIZE ? FDS_SIDE_SIZE : writePos;

  FDS_LOG("side %d: %d block(s), expanded size %lu B\n",
          sideIndex, blkIdx, (unsigned long)fds_side_sizes[sideIndex]);
#if FDS_TRACE
  for (int b = 0; b < blkIdx && b < 6; ++b)
  {
    FDS_LOG("  blk[%d] start=%lu end=%lu\n", b,
            (unsigned long)fds_block_starts[sideIndex][b],
            (unsigned long)fds_block_ends[sideIndex][b]);
  }
#endif
}

/*-------------------------------------------------------------------*/
/*  Copy-on-write disk image (no PSRAM).                             */
/*                                                                   */
/*  fdsSynthByte / fdsSynthRange produce the expanded image from the */
/*  raw one, exactly as fdsExpandOneSide would write it. Block i's   */
/*  payload starts at raw offset                                     */
/*    starts[i] - FDS_INITIAL_GAP_BYTES - i * FDS_BLOCK_OVERHEAD,    */
/*  because every earlier block added a mark, two CRC bytes and a    */
/*  gap. This relies on the block tables describing the pristine     */
/*  image: nothing rewrites an existing entry at runtime.            */
/*-------------------------------------------------------------------*/

/* Block index of the last fdsSynthByte hit. The drive reads sequentially,
   so the next byte is nearly always in the same block or the next one. */
static int fds_synth_side = -1;
static int fds_synth_blk  = 0;

static BYTE fdsSynthByte(DWORD off)
{
  int   s   = (int)(off / FDS_PER_SIDE_MAX);
  DWORD pos = off - (DWORD)s * FDS_PER_SIDE_MAX;
  if (s >= FDS_NumSides) return 0;

  const DWORD *starts = fds_block_starts[s];
  const DWORD *ends   = fds_block_ends[s];
  int n = fds_block_counts[s];
  /* Resume from the cached block if every block before it ends at or
     before pos; otherwise scan from the start of the side. */
  int i = fds_synth_blk;
  if (s != fds_synth_side || i > n || (i > 0 && ends[i - 1] > pos))
    i = 0;
  while (i < n && ends[i] <= pos) ++i;
  fds_synth_side = s;
  fds_synth_blk  = i;

  if (i == n || pos < starts[i]) return 0; /* gap */
  DWORD k    = pos - starts[i];
  DWORD size = ends[i] - starts[i] - 3;
  if (k == 0) return FDS_MARK_BYTE;
  if (k <= size)
    return FDS_DiskImage[(DWORD)s * FDS_SIDE_SIZE + starts[i] -
                         FDS_INITIAL_GAP_BYTES - (DWORD)i * FDS_BLOCK_OVERHEAD + k - 1];
  return k == size + 1 ? FDS_FAKE_CRC_HI : FDS_FAKE_CRC_LO;
}

/* Fill dst with bytes [off, off + len) of the expanded image. The range may
   cross a side boundary: FDS_PER_SIDE_MAX is not a multiple of a page. */
static void fdsSynthRange(DWORD off, BYTE *dst, DWORD len)
{
  memset(dst, 0, len);
  while (len)
  {
    int   s     = (int)(off / FDS_PER_SIDE_MAX);
    DWORD pos   = off - (DWORD)s * FDS_PER_SIDE_MAX;
    DWORD chunk = FDS_PER_SIDE_MAX - pos;
    if (chunk > len) chunk = len;
    DWORD end   = pos + chunk;

    for (int i = 0; s < FDS_NumSides && i < fds_block_counts[s]; ++i)
    {
      DWORD bs = fds_block_starts[s][i];
      DWORD be = fds_block_ends[s][i];
      if (be <= pos) continue;
      if (bs >= end) break;
      DWORD size = be - bs - 3;
      const BYTE *payload = FDS_DiskImage + (DWORD)s * FDS_SIDE_SIZE + bs -
                            FDS_INITIAL_GAP_BYTES - (DWORD)i * FDS_BLOCK_OVERHEAD;

      if (bs >= pos) dst[bs - pos] = FDS_MARK_BYTE;
      DWORD a = bs + 1 > pos ? bs + 1 : pos;
      DWORD b = bs + 1 + size < end ? bs + 1 + size : end;
      if (a < b) memcpy(dst + (a - pos), payload + (a - bs - 1), b - a);
      if (be - 2 >= pos && be - 2 < end) dst[be - 2 - pos] = FDS_FAKE_CRC_HI;
      if (be - 1 >= pos && be - 1 < end) dst[be - 1 - pos] = FDS_FAKE_CRC_LO;
    }

    off += chunk;
    dst += chunk;
    len -= chunk;
  }
}

/* Heap left for the disk pages. They are allocated mid-game, where an
   out-of-memory panic would take the emulator down, and without PSRAM
   f_malloc is plain malloc. keepcost is the top chunk, which is where a
   fresh block comes from once pico_shared's claimHeap() has grown the
   heap to its full size. */
#define FDS_HEAP_SLACK   4096   /* left over for save states and the menu */
#define FDS_PAGE_RESERVE (4 * FDS_PAGE_SIZE) /* preflight: room for saves */

static size_t fdsHeapFree()
{
#if PICO_NO_HARDWARE
  return (size_t)-1;
#else
  return (size_t)mallinfo().keepcost;
#endif
}

static bool fdsHeapHasRoom(size_t n)
{
  return fdsHeapFree() >= n + FDS_HEAP_SLACK;
}

static int  fds_resident_pages = 0;
static bool fds_page_alloc_failed = false;

/* Page p of the copy-on-write image, materialised on first use. */
static BYTE *fdsPage(unsigned p)
{
  BYTE *pg = fds_pages[p];
  if (pg) return pg;
  if (!fdsHeapHasRoom(FDS_PAGE_SIZE))
  {
    if (!fds_page_alloc_failed)
      printf("FDS: out of memory for disk page %u, disk write dropped\n", p);
    fds_page_alloc_failed = true;
    return nullptr;
  }
  pg = (BYTE *)Frens::f_malloc(FDS_PAGE_SIZE);
  fdsSynthRange((DWORD)p * FDS_PAGE_SIZE, pg, FDS_PAGE_SIZE);
  fds_pages[p] = pg;
  fds_resident_pages++;
  return pg;
}

static inline BYTE fdsDiskRead(DWORD off)
{
  if (fds_expanded) return fds_expanded[off];
  BYTE *pg = fds_pages[off / FDS_PAGE_SIZE];
  return pg ? pg[off % FDS_PAGE_SIZE] : fdsSynthByte(off);
}

static inline void fdsDiskWrite(DWORD off, BYTE byData)
{
  if (fds_expanded)
  {
    fds_expanded[off] = byData;
  }
  else
  {
    BYTE *pg = fdsPage(off / FDS_PAGE_SIZE);
    if (!pg) return;
    pg[off % FDS_PAGE_SIZE] = byData;
  }
  fdsMarkDirty(off);
}

/* Store n bytes at off and mark their pages dirty. False when a page could
   not be allocated. */
static bool fdsDiskWriteRange(DWORD off, const BYTE *src, DWORD n)
{
  while (n)
  {
    DWORD inPage = FDS_PAGE_SIZE - off % FDS_PAGE_SIZE;
    DWORD chunk  = n < inPage ? n : inPage;
    BYTE *dst;
    if (fds_expanded)
      dst = fds_expanded + off;
    else if ((dst = fdsPage(off / FDS_PAGE_SIZE)) != nullptr)
      dst += off % FDS_PAGE_SIZE;
    else
      return false;
    memcpy(dst, src, chunk);
    fdsMarkDirty(off);
    off += chunk;
    src += chunk;
    n   -= chunk;
  }
  return true;
}

/*-------------------------------------------------------------------*/
/*  fdsBuildDisk: set up the expanded disk image for all sides. With */
/*  PSRAM it is built in full; without, only the block tables are    */
/*  built and pages are materialised on the first write.             */
/*-------------------------------------------------------------------*/
static bool fdsBuildDisk(const BYTE *rawDisk, int sides, bool copyOnWrite)
{
  fds_disk_total_size = (DWORD)sides * FDS_PER_SIDE_MAX;
  unsigned numPages = (fds_disk_total_size + FDS_PAGE_SIZE - 1) / FDS_PAGE_SIZE;

  if (copyOnWrite)
  {
    fds_pages = (BYTE **)Frens::f_malloc(numPages * sizeof(BYTE *));
    if (fds_pages) memset(fds_pages, 0, numPages * sizeof(BYTE *));
  }
  else
  {
    fds_expanded = (BYTE *)Frens::f_malloc(fds_disk_total_size);
    if (fds_expanded) memset(fds_expanded, 0, fds_disk_total_size);
  }
  if (!fds_pages && !fds_expanded)
  {
    InfoNES_Error("FDS: cannot allocate the disk image");
    return false;
  }

  for (int s = 0; s < sides; ++s)
  {
    fds_side_offsets[s] = (DWORD)s * FDS_PER_SIDE_MAX;
    fdsExpandOneSide(rawDisk, s, fds_expanded ? fds_expanded + fds_side_offsets[s] : nullptr);
  }
  fds_synth_side = -1;
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

  if (!fdsImage || fdsImageSize == 0)
  {
    InfoNES_Error("FDS image is empty");
    return false;
  }

  bool copyOnWrite = !Frens::isPsramEnabled();

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

  /* The expansion audio buffer is allocated later, by Map20_Init. */
  DWORD  total  = (DWORD)sides * FDS_PER_SIDE_MAX;
  size_t tables = (size_t)sides * (sizeof(*fds_block_starts) + sizeof(*fds_block_ends));
  size_t need   = FDS_BIOS_SIZE + FDS_PRG_RAM_SIZE + APU_MAX_SAMPLES_PER_SYNC + tables;
  size_t avail;
  if (copyOnWrite)
  {
    need += ((total + FDS_PAGE_SIZE - 1) / FDS_PAGE_SIZE) * sizeof(BYTE *) + FDS_PAGE_RESERVE;
    avail = fdsHeapFree();
    if (!fdsHeapHasRoom(need))
    {
      InfoNES_Error("Not enough memory for FDS (%uK free, %uK needed)",
                    (unsigned)(avail >> 10), (unsigned)((need + FDS_HEAP_SLACK) >> 10));
      return false;
    }
  }
  else
  {
    need += total;
    avail = Frens::GetAvailableMemory();
    if (avail < need)
    {
      InfoNES_Error("Not enough memory for FDS (%u < %u)",
                    (unsigned)avail, (unsigned)need);
      return false;
    }
  }

  if (!fdsBiosFileOk(nullptr))
  {
    /* InfoNES_Error already populated. */
    return false;
  }

  FDS_Bios         = (BYTE *)Frens::f_malloc(FDS_BIOS_SIZE);
  FDS_PrgRam       = (BYTE *)Frens::f_malloc(FDS_PRG_RAM_SIZE);
  fds_block_starts = (DWORD (*)[FDS_MAX_BLOCKS_PER_SIDE])Frens::f_malloc(sides * sizeof(*fds_block_starts));
  fds_block_ends   = (DWORD (*)[FDS_MAX_BLOCKS_PER_SIDE])Frens::f_malloc(sides * sizeof(*fds_block_ends));
  if (!FDS_Bios || !FDS_PrgRam || !fds_block_starts || !fds_block_ends)
  {
    InfoNES_Error("FDS memory allocation failed");
    fdsRelease();
    return false;
  }

  memset(FDS_PrgRam, 0, FDS_PRG_RAM_SIZE);
  /* $6000-$7FFF is the core's SRAM buffer (see Map20_Init). */
  memset(SRAM, 0, SRAM_SIZE);

  if (!fdsLoadBios(FDS_Bios))
  {
    fdsRelease();
    return false;
  }

  /* See FDS_HOOK_ADDR. A BIOS whose header check does not open with
     JSR $E6E3 is left alone; it only loses automatic side switching. */
  BYTE *hook = FDS_Bios + FDS_BIOS_HOOK_OFS;
  fds_hook_patched = hook[0] == 0x20 && hook[1] == 0xE3 && hook[2] == 0xE6;
  if (fds_hook_patched)
  {
    hook[1] = FDS_HOOK_ADDR & 0xFF;
    hook[2] = FDS_HOOK_ADDR >> 8;
  }
  else
  {
    printf("FDS: unknown BIOS, automatic disk side switching is off\n");
  }

  FDS_DiskImage = diskBytes;
  FDS_NumSides = sides;
  FDS_CurrentSide = 0;

  if (!fdsBuildDisk(diskBytes, sides, copyOnWrite))
  {
    fdsRelease();
    return false;
  }

  IsFDS = true;
  fds_eject_counter = 0;
  fds_pending_side  = -1;
  fdsClearDirtyBitmap();

  if (copyOnWrite)
    printf("FDS: image %u bytes, %d side(s), copy-on-write; heap %uK free before, %uK after\n",
           (unsigned)diskSize, sides, (unsigned)(avail >> 10), (unsigned)(fdsHeapFree() >> 10));
  else
    printf("FDS: image %u bytes, %d side(s), multi-side mode; avail before alloc %u, after %u\n",
           (unsigned)diskSize, sides, (unsigned)avail, (unsigned)Frens::GetAvailableMemory());
  return true;
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

static int fdsTotalPages()
{
  return (int)((fds_disk_total_size + FDS_PAGE_SIZE - 1) / FDS_PAGE_SIZE);
}

/* Set when saved data could not be loaded in full. Writing the sidecar
   back would then lose the pages that were left out, so it is skipped. */
static bool fds_sidecar_incomplete = false;

#define FDS_PATH_MAX 280
#define FDS_SIDECAR_ABSENT (-1)

/*-------------------------------------------------------------------*/
/*  fdsApplySidecar: load one sidecar file whose pages cover         */
/*  [base, base + span) of the disk image. The current format is a   */
/*  single file for all sides (base 0, storedSides = FDS_NumSides).  */
/*  Builds before the copy-on-write image kept one file per side on  */
/*  boards without PSRAM, "_s<N>.SAV", with pages relative to that   */
/*  side (base = N * FDS_PER_SIDE_MAX, storedSides 1). Those pages   */
/*  do not line up with the flat ones, hence the byte-range copy.    */
/*  Returns 1 when applied, 0 when ignored or failed, or             */
/*  FDS_SIDECAR_ABSENT.                                              */
/*-------------------------------------------------------------------*/
static int fdsApplySidecar(FIL *fil, const char *filePath, int storedSides,
                           DWORD base, DWORD span)
{
  if (f_open(fil, filePath, FA_READ) != FR_OK)
    return FDS_SIDECAR_ABSENT;

  BYTE hdr[8];
  UINT br = 0;
  if (f_read(fil, hdr, sizeof(hdr), &br) != FR_OK || br != sizeof(hdr) ||
      hdr[0] != FDS_SIDECAR_MAGIC0 || hdr[1] != FDS_SIDECAR_MAGIC1 ||
      hdr[2] != FDS_SIDECAR_MAGIC2 || hdr[3] != FDS_SIDECAR_MAGIC3 ||
      hdr[4] != FDS_SIDECAR_VERSION || hdr[5] != (BYTE)storedSides)
  {
    printf("FDS sidecar header mismatch — ignoring %s.\n", filePath);
    f_close(fil);
    return 0;
  }

  int numPages = (int)((span + FDS_PAGE_SIZE - 1) / FDS_PAGE_SIZE);
  int bmBytes = hdr[6];
  if (bmBytes != (numPages + 7) / 8)
  {
    printf("FDS sidecar bitmap size mismatch — ignoring %s.\n", filePath);
    f_close(fil);
    return 0;
  }

  BYTE bm[(FDS_MAX_PAGES + 7) / 8] = {0};
  if (f_read(fil, bm, bmBytes, &br) != FR_OK || br != (UINT)bmBytes)
  {
    f_close(fil);
    return 0;
  }

  int loaded = 0;
  for (int p = 0; p < numPages; ++p)
  {
    if (!((bm[p >> 3] >> (p & 7)) & 1)) continue;
    DWORD off = (DWORD)p * FDS_PAGE_SIZE;
    DWORD bytes = span - off < FDS_PAGE_SIZE ? span - off : FDS_PAGE_SIZE;

    /* The file always holds a full page; the part past span is padding. */
    BYTE chunk[256];
    for (DWORD done = 0; done < FDS_PAGE_SIZE; done += sizeof(chunk))
    {
      if (f_read(fil, chunk, sizeof(chunk), &br) != FR_OK || br != sizeof(chunk))
      {
        f_close(fil);
        return 0;
      }
      if (done < bytes)
      {
        DWORD n = bytes - done < sizeof(chunk) ? bytes - done : sizeof(chunk);
        if (!fdsDiskWriteRange(base + off + done, chunk, n))
        {
          printf("FDS sidecar: out of memory loading %s, it will not be overwritten.\n", filePath);
          fds_sidecar_incomplete = true;
          f_close(fil);
          return 0;
        }
      }
    }
    ++loaded;
  }
  f_close(fil);
  printf("FDS sidecar: applied %d dirty page(s) from %s\n", loaded, filePath);
  return 1;
}

static bool fdsSaveSidecarToFile(FIL *fil, const char *filePath)
{
  int numPages = fdsTotalPages();
  int bmBytes  = (numPages + 7) / 8;

  FRESULT fr = f_open(fil, filePath, FA_CREATE_ALWAYS | FA_WRITE);
  if (fr != FR_OK)
  {
    printf("FDS sidecar: cannot open %s for write (%d)\n", filePath, fr);
    return false;
  }

  BYTE hdr[8] = {
    FDS_SIDECAR_MAGIC0, FDS_SIDECAR_MAGIC1,
    FDS_SIDECAR_MAGIC2, FDS_SIDECAR_MAGIC3,
    FDS_SIDECAR_VERSION,
    (BYTE)FDS_NumSides,
    (BYTE)bmBytes,
    0
  };
  UINT bw = 0;
  if (f_write(fil, hdr, sizeof(hdr), &bw) != FR_OK || bw != sizeof(hdr)) goto fail;
  if (f_write(fil, fds_dirty_bitmap, bmBytes, &bw) != FR_OK || bw != (UINT)bmBytes) goto fail;

  {
    DWORD totalBytes = fds_disk_total_size;
    int saved = 0;
    for (int p = 0; p < numPages; ++p)
    {
      if (!fdsPageDirty((unsigned)p)) continue;
      DWORD off = (DWORD)p * FDS_PAGE_SIZE;
      DWORD bytes = FDS_PAGE_SIZE;
      if (off + bytes > totalBytes) bytes = totalBytes - off;
      /* A dirty page is always resident in the copy-on-write image. */
      const BYTE *data = fds_expanded ? fds_expanded + off : fds_pages[p];

      if (f_write(fil, data, bytes, &bw) != FR_OK || bw != bytes) goto fail;
      if (bytes < FDS_PAGE_SIZE)
      {
        static const BYTE zeros[64] = {0};
        DWORD remain = FDS_PAGE_SIZE - bytes;
        while (remain)
        {
          DWORD chunk = remain < sizeof(zeros) ? remain : sizeof(zeros);
          if (f_write(fil, zeros, chunk, &bw) != FR_OK || bw != chunk) goto fail;
          remain -= chunk;
        }
      }
      ++saved;
    }
    f_close(fil);
    printf("FDS sidecar: wrote %d dirty page(s) to %s\n", saved, filePath);
    return true;
  }

fail:
  printf("FDS sidecar: write failed for %s\n", filePath);
  f_close(fil);
  return false;
}

static void fdsCompleteSideSwitch(int newSide)
{
  FDS_CurrentSide = newSide;
  FDS_DiskInserted = true;
  fdsResetTransferPosition();
  fds_motor_spinup = 50;
  fds_byte_irq_pend = 0;
  fds_transfer_complete = 0;
}

/*-------------------------------------------------------------------*/
/*  Public sidecar API. basePath is the path stem without extension  */
/*  (e.g. "/saves/game_fds"); the file is basePath + ".SAV".         */
/*-------------------------------------------------------------------*/

bool fdsLoadSidecar(const char *basePath)
{
  if (!IsFDS || !basePath) return false;
  fds_sidecar_incomplete = false;

  char path[FDS_PATH_MAX];
  FIL *fil = (FIL *)Frens::f_malloc(sizeof(FIL));
  if (!fil) return false;
  snprintf(path, sizeof(path), "%s.SAV", basePath);
  if (fdsApplySidecar(fil, path, FDS_NumSides, 0, fds_disk_total_size) == FDS_SIDECAR_ABSENT)
  {
    /* Saves from builds that kept one file per side. They are read once;
       the next save writes everything to basePath.SAV. */
    for (int s = 0; s < FDS_NumSides && !fds_sidecar_incomplete; ++s)
    {
      snprintf(path, sizeof(path), "%s_s%d.SAV", basePath, s);
      fdsApplySidecar(fil, path, 1, fds_side_offsets[s], FDS_PER_SIDE_MAX);
    }
  }
  Frens::f_free(fil);
  return true;
}

bool fdsSaveSidecar(const char *basePath)
{
  if (!IsFDS || !basePath) return false;
  if (fds_sidecar_incomplete)
  {
    printf("FDS sidecar: saved data was not loaded completely, not overwriting it.\n");
    return false;
  }
  if (!fdsHasDirtyPages())
  {
    printf("FDS sidecar: no dirty pages, skipping write of %s.SAV.\n", basePath);
    return true;
  }
  char path[FDS_PATH_MAX];
  FIL *fil = (FIL *)Frens::f_malloc(sizeof(FIL));
  if (!fil) return false;
  snprintf(path, sizeof(path), "%s.SAV", basePath);
  bool ok = fdsSaveSidecarToFile(fil, path);
  Frens::f_free(fil);
  return ok;
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
      buffer[i] = SRAM[addr - 0x6000];
    else if (addr >= 0x8000 && addr < 0xE000)
      buffer[i] = FDS_PrgRam ? FDS_PrgRam[addr - 0x8000] : 0;
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
    fdsCompleteSideSwitch(matchIndex);
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

    /* JMP $E6E3 for the patched BIOS header check (see FDS_HOOK_ADDR).
       Only the opcode fetch runs the check, not a data read of $5FF0.
       The JMP is the emulator's, not the BIOS's, so its 3 cycles are
       handed back to keep the game's timing unchanged. */
    if (fds_hook_patched && (WORD)(wAddr - FDS_HOOK_ADDR) < 3)
    {
      if (wAddr == FDS_HOOK_ADDR)
      {
        if (PC != FDS_HOOK_ADDR + 1) return 0x4C;
        g_wPassedClocks -= 3;
        fdsAutoInsertCheck();
        return 0x4C;
      }
      return wAddr == FDS_HOOK_ADDR + 1 ? 0xE3 : 0xE6;
    }

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
void FDS_RAMFUNC(fdsRenderAudio)(unsigned int n)
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
      fdsCompleteSideSwitch(fds_pending_side);
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
      fdsCompleteSideSwitch(nextSide);
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
           fds_disk_total_size && !fds_end_of_head)
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
    DWORD sideBase = fds_side_offsets[FDS_CurrentSide];

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
        BYTE diskByte = fdsDiskRead(sideBase + fds_byte_pos);
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
        fdsDiskWrite(sideBase + fds_byte_pos, writeData);
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
  if (fds_expanded) { Frens::f_free(fds_expanded); fds_expanded = nullptr; }
  if (fds_pages)
  {
    unsigned numPages = (unsigned)fdsTotalPages();
    for (unsigned p = 0; p < numPages; ++p)
      if (fds_pages[p]) Frens::f_free(fds_pages[p]);
    if (fds_resident_pages)
      printf("FDS: %d disk page(s) were resident (%dK)\n",
             fds_resident_pages, fds_resident_pages * (FDS_PAGE_SIZE >> 10));
    Frens::f_free(fds_pages);
    fds_pages = nullptr;
  }
  fds_resident_pages = 0;
  fds_page_alloc_failed = false;
  fds_sidecar_incomplete = false;
  fds_disk_total_size = 0;
  if (fds_block_starts) { Frens::f_free(fds_block_starts); fds_block_starts = nullptr; }
  if (fds_block_ends)   { Frens::f_free(fds_block_ends);   fds_block_ends   = nullptr; }
  fds_synth_side = -1;
  fds_hook_patched = false;
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
