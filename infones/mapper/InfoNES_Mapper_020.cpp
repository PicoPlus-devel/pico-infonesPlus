/*===================================================================*/
/*                                                                   */
/*       Mapper 20 (Famicom Disk System)                             */
/*                                                                   */
/*  Phase 3: bank wiring only. Disk drive register I/O ($4020-$4026  */
/*  W, $4030-$4033 R), byte/timer IRQs and FDS audio are added in    */
/*  phases 4 and 7.                                                  */
/*                                                                   */
/*  Memory map (CPU):                                                */
/*    $6000-$7FFF  SRAMBANK   = SRAM                (8 KB)           */
/*    $8000-$9FFF  ROMBANK0   = FDS_PrgRam[0x0000]                   */
/*    $A000-$BFFF  ROMBANK1   = FDS_PrgRam[0x2000]                   */
/*    $C000-$DFFF  ROMBANK2   = FDS_PrgRam[0x4000]                   */
/*    $E000-$FFFF  ROMBANK3   = FDS_Bios            (8 KB, RO)       */
/*                                                                   */
/*  PPU $0000-$1FFF: PPUBANK[0..7] = CRAMPAGE(0..7), the pattern-    */
/*  table half of PPURAM (8 KB CHR-RAM).                             */
/*===================================================================*/

/* On RP2040 the drive code these hooks call stays in flash, so hooks in
   RAM would only add long-branch veneers to it. */
#if PICO_RP2350
#define MAP20_RAMFUNC(f) __not_in_flash_func(f)
#else
#define MAP20_RAMFUNC(f) f
#endif

void Map20_Write(WORD wAddr, BYTE byData);
void Map20_Apu(WORD wAddr, BYTE byData);
BYTE Map20_ReadApu(WORD wAddr);
void Map20_HSync();

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 20                                             */
/*-------------------------------------------------------------------*/
void Map20_Init()
{
  /* Initialize Mapper */
  MapperInit = Map20_Init;

  /* Write to Mapper ($8000-$FFFF) */
  MapperWrite = Map20_Write;

  /* Write to SRAM ($6000-$7FFF). The K6502 dispatch already stores
     into SRAMBANK before calling MapperSram, so this is a no-op. */
  MapperSram = Map0_Sram;

  /* Write/Read to APU ($4018-$5FFF). Disk drive registers live here
     ($4020-$4026 write, $4030-$4033 read); FDS audio is $4040-$4097.
     Stubbed in phase 3, real handlers in phases 4/7. */
  MapperApu = Map20_Apu;
  MapperReadApu = Map20_ReadApu;

  /* Callback at VSync / HSync / PPU / Render. HSync drives disk-byte
     and timer IRQ pacing for the FDS drive. */
  MapperVSync = Map0_VSync;
  MapperHSync = Map20_HSync;
  MapperPPU = Map0_PPU;
  MapperRenderScreen = Map0_RenderScreen;

  /* CPU bank wiring. $6000-$7FFF is the core's SRAM buffer, so
     reads/writes there go directly through the existing dispatch in
     K6502_rw.h, and OAM DMA from $6xxx (which copies from SRAM) sees
     the same bytes. ROMBANK0..2 cover $8000-$DFFF; ROMBANK3 is the
     BIOS at $E000-$FFFF. */
  SRAMBANK = SRAM;
  ROMBANK0 = FDS_PrgRam;
  ROMBANK1 = FDS_PrgRam + 0x2000;
  ROMBANK2 = FDS_PrgRam + 0x4000;
  ROMBANK3 = FDS_Bios;

  /* PPU bank wiring. CHR-RAM occupies pattern tables $0000-$1FFF of
     PPURAM, as for any cart without CHR-ROM; name tables are set up by
     InfoNES_SetupPPU. */
  for (int nPage = 0; nPage < 8; ++nPage)
    PPUBANK[nPage] = CRAMPAGE(nPage);
  InfoNES_SetupChr();

  /* FDS defaults to horizontal mirroring; $4025 bit 3 toggles it
     once the drive registers are in place (phase 4). */
  InfoNES_Mirroring(0);

  /* Enable IRQ wiring; disk-drive IRQs feed the IRQ pin via fdsHsync. */
  K6502_Set_Int_Wiring(1, 1);

  /* Initialise disk drive state (timer regs, transfer position, ...). */
  fdsResetDrive();

  /* Allocate FDS expansion audio output buffer and enable rendering. */
  if (!fds_wave_buffer)
    fds_wave_buffer = (BYTE *)Frens::f_malloc(APU_MAX_SAMPLES_PER_SYNC);
  if (fds_wave_buffer)
    InfoNES_MemorySet(fds_wave_buffer, 0, APU_MAX_SAMPLES_PER_SYNC);
  ApuFdsEnable = 1;
}

/*-------------------------------------------------------------------*/
/*  Map20 Write ($8000-$FFFF)                                        */
/*  $8000-$DFFF is writable PRG-RAM. $E000-$FFFF is BIOS, dropped.   */
/*-------------------------------------------------------------------*/
void __not_in_flash_func(Map20_Write)(WORD wAddr, BYTE byData)
{
  if (wAddr < 0xE000)
  {
    FDS_PrgRam[wAddr - 0x8000] = byData;
  }
}

/*-------------------------------------------------------------------*/
/*  Map20 APU-range hooks ($4018-$5FFF). Disk-drive registers live   */
/*  at $4020-$4026 (W) and $4030-$4033 (R); FDS audio at $4040-$4097 */
/*  is implemented in phase 7.                                       */
/*-------------------------------------------------------------------*/
void MAP20_RAMFUNC(Map20_Apu)(WORD wAddr, BYTE byData)
{
  fdsApuWrite(wAddr, byData);
}

BYTE MAP20_RAMFUNC(Map20_ReadApu)(WORD wAddr)
{
  return fdsApuRead(wAddr);
}

void MAP20_RAMFUNC(Map20_HSync)()
{
  fdsHsync();
}
