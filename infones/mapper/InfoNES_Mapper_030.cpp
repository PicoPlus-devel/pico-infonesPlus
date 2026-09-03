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
// 0x09 -> four-screen (the core sets it up; this mapper leaves it alone)
static BYTE Map30_MirrorMode;

/* Self-flashing boards (the ones with the battery flag set) decode the bank
 * latch at $C000-$FFFF only, so that $8000-$BFFF writes reach the flash chip
 * without disturbing the banking. Boards without flash latch on the whole
 * $8000-$FFFF range, which is what this mapper always used to do.
 * Dungeons & Doomknights is the flash case: its STA $9555 / STA $AAAA command
 * sequences were being taken as bank writes, scrambling PRG bank, CHR bank
 * and mirroring on the title screen. */
static BYTE Map30_HasFlash;
static struct SstFlash_tag Map30_Flash;

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
  ROMBANK0 = SstFlash_Page( &Map30_Flash, byPrg, 0 );
  ROMBANK1 = SstFlash_Page( &Map30_Flash, byPrg + 1, 1 );

  /* Last 16KB fixed at $C000-$FFFF. Routed through the flash helper too so a
     programmed page shows up there; the ID page never is (mask 0x03), because
     the flash routine and the NMI/IRQ vectors live in this window. */
  ROMBANK2 = SstFlash_Page( &Map30_Flash, (DWORD)( byBanks << 1 ) - 2, 2 );
  ROMBANK3 = SstFlash_Page( &Map30_Flash, (DWORD)( byBanks << 1 ) - 1, 3 );

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
/* The blob carries the bank register and the flash state. The 32KB of CHR RAM
 * is written straight to the state file next to PPURAM by state.cpp: routing
 * it through the blob would make LoadState/SaveState allocate a second 32KB
 * buffer on top of Map30_Chr_Ram, which an RP2040 does not have to spare. */
static int Map30BlobSize()
{
  return 4 + SstFlash_BlobSize();
}

static void Map30SaveBlob( BYTE *pBuf )
{
  pBuf[ 0 ] = Map30_Reg;
  pBuf[ 1 ] = pBuf[ 2 ] = pBuf[ 3 ] = 0;
  SstFlash_SaveBlob( &Map30_Flash, pBuf + 4 );
}

static void Map30LoadBlob( BYTE *pBuf )
{
  Map30_Reg = pBuf[ 0 ];
  SstFlash_LoadBlob( &Map30_Flash, pBuf + 4 );

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

  /* Determine mirroring mode from header. 0x09 is real four-screen, which
     InfoNES_SetupPPU has already selected from ROM_FourScr; leave it alone. */
  Map30_MirrorMode = NesHeader.byInfo1 & 0x09;
  if ( Map30_MirrorMode == 0x08 )
  {
    /* One-screen mode: bit 3 is a mode bit here, not four-screen. */
    ROM_FourScr = 0;
    InfoNES_Mirroring( 3 );
  }

  /* The battery flag is what marks a self-flashing board (Mesen keys off the
     same bit). Only those split the bank latch at $C000. */
  Map30_HasFlash = ROM_SRAM ? 1 : 0;

  /* ID mode swaps ROMBANK0/1, the switchable $8000-$BFFF window the game
     polls; the fixed last 16KB, where the flash routine and the vectors
     live, must stay real ROM. */
  SstFlash_Init( &Map30_Flash, Map30_Sync, 0x03 );

  /* Allocate the 32KB CHR RAM once; Map30_Init is also the reset entry point. */
  if ( !Map30_Chr_Ram )
  {
    Map30_Chr_Ram = (BYTE *)Frens::f_malloc( MAP30_CHR_RAM_SIZE );
    if ( Map30_Chr_Ram )
      InfoNES_MemorySet( Map30_Chr_Ram, 0, MAP30_CHR_RAM_SIZE );
  }
  MapperChrRam     = Map30_Chr_Ram;
  MapperChrRamSize = Map30_Chr_Ram ? MAP30_CHR_RAM_SIZE : 0;

  /* Reset the bank register and apply it: PRG bank 0, CHR bank 0.
     Map30_Sync sets all four ROM banks, last 16KB fixed. */
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
  if ( Map30_HasFlash )
  {
    DWORD dwOfs;

    /* Self-flashing board: only $C000-$FFFF drives the latch. */
    if ( wAddr < 0xC000 )
    {
      dwOfs = ( (DWORD)( Map30_Reg & 0x1F ) << 14 ) | ( wAddr & 0x3FFF );
      SstFlash_Write( &Map30_Flash, wAddr, byData, dwOfs );
      return;
    }
  }

  Map30_Reg = byData;
  Map30_Sync();
}
