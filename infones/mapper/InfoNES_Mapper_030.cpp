/*===================================================================*/
/*                                                                   */
/*               Mapper 30 (UNROM 512 / NESmaker)                    */
/*                                                                   */
/*===================================================================*/

// Mirroring mode derived from iNES header flags
// byInfo1 bit 0 = mirroring, bit 3 = four-screen
// 0x00 -> fixed horizontal
// 0x01 -> fixed vertical
// 0x08 -> 1-screen, mapper-controlled via bit 7
// 0x09 -> four-screen (not supported here, treat as 1-screen)
static BYTE Map30_MirrorMode;

/* CHR RAM (32KB = 4 x 8KB banks). UNROM 512 always carries 32KB of CHR RAM,
 * which does not fit in the 8KB the core reserves at the bottom of PPURAM:
 * CRAMPAGE() masks the page index with 0x1F, so bank 1 would alias straight
 * onto the nametables and banks 2-3 would run past the end of the 16KB PPURAM
 * allocation. Allocated lazily in Map30_Init, freed in InfoNES_Fin.
 * MAP30_CHR_RAM_SIZE lives in InfoNES_Mapper.h because state.cpp needs it too. */
BYTE *Map30_Chr_Ram;

/* Last value written to the bank register; re-applied on save state load. */
static BYTE Map30_Reg;

#define Map30_CRAMPAGE(a) &Map30_Chr_Ram[((a) & 0x1F) * 0x400]

/*-------------------------------------------------------------------*/
/*  Apply the current register value to the PRG / CHR banks           */
/*-------------------------------------------------------------------*/
static void Map30_Sync()
{
  BYTE byData = Map30_Reg;

  /* Set PRG ROM bank at $8000-$BFFF */
  BYTE byBanks = NesHeader.byRomSize ? NesHeader.byRomSize : 1;
  BYTE byPrg = ( byData & 0x1F ) % byBanks;
  byPrg <<= 1;
  ROMBANK0 = ROMPAGE( byPrg );
  ROMBANK1 = ROMPAGE( byPrg + 1 );

  /* Set CHR RAM bank at PPU $0000-$1FFF */
  if ( Map30_Chr_Ram )
  {
    BYTE byChr = ( ( byData >> 5 ) & 0x03 ) << 3;
    for ( int i = 0; i < 8; ++i )
      PPUBANK[ i ] = Map30_CRAMPAGE( byChr + i );
  }
  else
  {
    /* Allocation failed (only possible on a RAM-constrained RP2040). Stay on
     * bank 0 inside PPURAM: the wrong tiles are drawn for banks 1-3, but the
     * nametables and the heap are left alone. */
    for ( int i = 0; i < 8; ++i )
      PPUBANK[ i ] = CRAMPAGE( i );
  }
  InfoNES_SetupChr();

  /* Set mirroring if 1-screen mode is active */
  if ( Map30_MirrorMode == 0x08 )
  {
    /* Bit 7: 0 = one-screen lower (0x2000), 1 = one-screen upper (0x2400) */
    if ( byData & 0x80 )
    {
      InfoNES_Mirroring( 2 );  /* One Screen 0x2400 */
    }
    else
    {
      InfoNES_Mirroring( 3 );  /* One Screen 0x2000 */
    }
  }
}

/*-------------------------------------------------------------------*/
/*  Save state support                                               */
/*-------------------------------------------------------------------*/
/* The blob carries only the bank register. The 32KB of CHR RAM is written
 * straight to the state file next to PPURAM by state.cpp: routing it through
 * the blob would make LoadState/SaveState allocate a second 32KB buffer on top
 * of Map30_Chr_Ram, which an RP2040 does not have to spare. */
static int Map30BlobSize()
{
  return 1;
}

static void Map30SaveBlob( BYTE *pBuf )
{
  pBuf[ 0 ] = Map30_Reg;
}

static void Map30LoadBlob( BYTE *pBuf )
{
  Map30_Reg = pBuf[ 0 ];

  /* MapperLoadBlob runs last in LoadState, after restorePPUBanks() and
   * InfoNES_Mirroring(), so this is what puts PPUBANK[0..7] and the
   * mapper-controlled mirroring back. */
  Map30_Sync();
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 30                                             */
/*-------------------------------------------------------------------*/
void Map30_Init()
{
  /* Initialize Mapper */
  MapperInit = Map30_Init;

  /* Write to Mapper */
  MapperWrite = Map30_Write;

  /* Write to SRAM */
  MapperSram = Map0_Sram;

  /* Write to APU */
  MapperApu = Map0_Apu;

  /* Read from APU */
  MapperReadApu = Map0_ReadApu;

  /* Callback at VSync */
  MapperVSync = Map0_VSync;

  /* Callback at HSync */
  MapperHSync = Map0_HSync;

  /* Callback at PPU */
  MapperPPU = Map0_PPU;

  /* Callback at Rendering Screen ( 1:BG, 0:Sprite ) */
  MapperRenderScreen = Map0_RenderScreen;

  /* Save state hooks (cleared on every reset, so install them here) */
  MapperBlobSize = Map30BlobSize;
  MapperSaveBlob = Map30SaveBlob;
  MapperLoadBlob = Map30LoadBlob;

  /* Set SRAM Banks */
  SRAMBANK = SRAM;

  /* Determine mirroring mode from header */
  Map30_MirrorMode = NesHeader.byInfo1 & 0x09;

  /* Allocate the 32KB CHR RAM once; Map30_Init is also the reset entry point. */
  if ( !Map30_Chr_Ram )
  {
    Map30_Chr_Ram = (BYTE *)Frens::f_malloc( MAP30_CHR_RAM_SIZE );
    if ( Map30_Chr_Ram )
      InfoNES_MemorySet( Map30_Chr_Ram, 0, MAP30_CHR_RAM_SIZE );
  }
  MapperChrRam     = Map30_Chr_Ram;
  MapperChrRamSize = Map30_Chr_Ram ? MAP30_CHR_RAM_SIZE : 0;

  /* Set ROM Banks: last 16KB fixed, first 16KB switchable (set by Map30_Sync) */
  ROMBANK2 = ROMLASTPAGE( 1 );
  ROMBANK3 = ROMLASTPAGE( 0 );

  /* Reset the bank register and apply it: PRG bank 0, CHR bank 0 */
  Map30_Reg = 0;
  Map30_Sync();

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 );
}

/*-------------------------------------------------------------------*/
/*  Mapper 30 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map30_Write( WORD wAddr, BYTE byData )
{
  /*
   * Register format: [MCCP PPPP]
   *  P (bits 0-4): PRG ROM bank select (16KB at $8000)
   *  C (bits 5-6): CHR RAM bank select (8KB at PPU $0000)
   *  M (bit 7):    Mirroring select (1-screen mode only)
   */
  Map30_Reg = byData;
  Map30_Sync();
}
