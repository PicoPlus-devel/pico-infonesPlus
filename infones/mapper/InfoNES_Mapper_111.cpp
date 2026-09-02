/*===================================================================*/
/*                                                                   */
/*             Mapper 111 (GTROM / Cheapocabra)                      */
/*                                                                   */
/*===================================================================*/

/*
 * Membler Industries' homebrew board. One register, no IRQ, no PRG RAM:
 *
 *   [.LCM PPPP]
 *    P (bits 0-3) : 32KB PRG bank, the whole $8000-$FFFF window
 *    M (bit 4)    : 8KB CHR RAM bank (16KB of CHR RAM in total)
 *    C (bit 5)    : name table RAM bank - the board carries two, which is
 *                   what lets a game build the next screen off-camera and
 *                   flip to it in one write
 *    L (bit 6)    : green LED, not emulated
 *
 * The register lives at $5000-$5FFF and is mirrored at $7000-$7FFF, so it
 * arrives through MapperApu and MapperSram respectively. $8000-$FFFF is NOT
 * the register: writes there go to the flash chip only.
 *
 * Deviation from hardware worth knowing about: the real board maps 8KB of
 * name table RAM across the whole of $2000-$3FFF, so $3000-$3EFF is separate
 * storage. This core keeps $3000-$3EFF as a hard alias of $2000-$2FFF
 * (K6502_rw.h mirrors every name table write into both), so the banks here
 * are 4KB and cover $2000-$2FFF only. Nothing renders from the upper half -
 * the PPU only ever fetches name tables from $2000-$2FFF - so this is
 * invisible unless a game uses $3000-$3EFF as scratch storage.
 */

/* One allocation: 2 x 8KB CHR RAM banks, then 2 x 4KB name table banks.
 * Keeping them contiguous means state.cpp writes both through MapperChrRam
 * and skips the name table RAM as a sub-range - see mapperNtRamNeedsFile().
 * Allocated lazily in Map111_Init, freed in InfoNES_Fin. */
BYTE *Map111_Chr_Ram;

/* Last value written to the register; re-applied on save state load. */
static BYTE Map111_Reg;

static struct SstFlash_tag Map111_Flash;

#define Map111_CHRPAGE(a) &Map111_Chr_Ram[ ( (a) & 0x0F ) * 0x400 ]
#define Map111_NTPAGE(a)  &Map111_Chr_Ram[ MAP111_CHR_RAM_SIZE + ( ( (a) & 0x07 ) * 0x400 ) ]

/*-------------------------------------------------------------------*/
/*  Apply the current register value to PRG, CHR and name tables      */
/*-------------------------------------------------------------------*/
static void Map111_Sync()
{
  BYTE byBanks = NesHeader.byRomSize ? NesHeader.byRomSize : 1;   /* 16KB units */
  DWORD dwPrg;
  int i;

  /* PRG: one 32KB bank = 4 x 8KB pages. */
  dwPrg = (DWORD)( Map111_Reg & 0x0F ) * 4;
  if ( byBanks >= 2 )
    dwPrg %= ( (DWORD)byBanks << 1 );
  else
    dwPrg = 0;

  ROMBANK0 = SstFlash_Page( &Map111_Flash, dwPrg + 0, 0 );
  ROMBANK1 = SstFlash_Page( &Map111_Flash, dwPrg + 1, 1 );
  ROMBANK2 = SstFlash_Page( &Map111_Flash, dwPrg + 2, 2 );
  ROMBANK3 = SstFlash_Page( &Map111_Flash, dwPrg + 3, 3 );

  if ( Map111_Chr_Ram )
  {
    /* CHR RAM: bit 4 picks one of the two 8KB banks. */
    BYTE byChr = ( Map111_Reg & 0x10 ) ? 8 : 0;
    for ( i = 0; i < 8; i++ )
      PPUBANK[ i ] = Map111_CHRPAGE( byChr + i );

    /* Name tables: bit 5 picks one of the two 4KB banks. Slots 12-15 stay
       the identity mapping into PPURAM so the $3000 alias and the palette
       read-back area keep working - see the header comment. */
    BYTE byNam = ( Map111_Reg & 0x20 ) ? 4 : 0;
    for ( i = 0; i < 4; i++ )
      PPUBANK[ NAME_TABLE0 + i ] = Map111_NTPAGE( byNam + i );
  }
  else
  {
    /* Allocation failed: fall back to the flat 8KB of CHR RAM the core keeps
       at the bottom of PPURAM and to the ordinary name tables. Banking is
       lost, but the game still runs. */
    for ( i = 0; i < 8; i++ )
      PPUBANK[ i ] = CRAMPAGE( i );
    InfoNES_Mirroring( 4 );
  }
  InfoNES_SetupChr();
}

/*-------------------------------------------------------------------*/
/*  Save state                                                        */
/*-------------------------------------------------------------------*/
struct Map111State
{
  BYTE Reg;
  BYTE Pad[ 3 ];
  /* The flash blob follows; the CHR and name table RAM travel separately
     through MapperChrRam. */
};

static int Map111BlobSize()
{
  return sizeof( struct Map111State ) + SstFlash_BlobSize();
}

static void Map111SaveBlob( BYTE *pBuf )
{
  struct Map111State *pState = (struct Map111State *)pBuf;
  pState->Reg = Map111_Reg;
  pState->Pad[ 0 ] = pState->Pad[ 1 ] = pState->Pad[ 2 ] = 0;
  SstFlash_SaveBlob( &Map111_Flash, pBuf + sizeof( struct Map111State ) );
}

static void Map111LoadBlob( BYTE *pBuf )
{
  struct Map111State *pState = (struct Map111State *)pBuf;
  Map111_Reg = pState->Reg;
  SstFlash_LoadBlob( &Map111_Flash, pBuf + sizeof( struct Map111State ) );
  /* Runs after restorePPUBanks() and the mirroring call in state.cpp, so this
     is what re-establishes the banks and the name table mapping. */
  Map111_Sync();
}

/*-------------------------------------------------------------------*/
/*  Register writes                                                   */
/*-------------------------------------------------------------------*/
static void Map111_SetReg( BYTE byData )
{
  Map111_Reg = byData;
  Map111_Sync();
}

/* $5000-$5FFF */
void Map111_Apu( WORD wAddr, BYTE byData )
{
  if ( wAddr >= 0x5000 )
    Map111_SetReg( byData );
}

/* $7000-$7FFF (the core has already stored the byte into SRAMBANK) */
void Map111_Sram( WORD wAddr, BYTE byData )
{
  if ( wAddr >= 0x7000 )
    Map111_SetReg( byData );
}

/* $8000-$FFFF is the flash chip, never the bank register. */
void Map111_Write( WORD wAddr, BYTE byData )
{
  DWORD dwOfs = ( (DWORD)( Map111_Reg & 0x0F ) << 15 ) | ( wAddr & 0x7FFF );
  SstFlash_Write( &Map111_Flash, wAddr, byData, dwOfs );
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 111                                            */
/*-------------------------------------------------------------------*/
void Map111_Init()
{
  MapperBlobSize = Map111BlobSize;
  MapperSaveBlob = Map111SaveBlob;
  MapperLoadBlob = Map111LoadBlob;

  MapperInit = Map111_Init;
  MapperWrite = Map111_Write;
  MapperSram = Map111_Sram;
  MapperApu = Map111_Apu;
  MapperReadApu = Map0_ReadApu;
  MapperVSync = Map0_VSync;
  MapperHSync = Map0_HSync;
  MapperPPU = Map0_PPU;
  MapperRenderScreen = Map0_RenderScreen;

  /* Set SRAM Banks. The board has no PRG RAM; $6000-$7FFF only exists so the
     $7000 register mirror has somewhere to land. */
  SRAMBANK = SRAM;

  /* Header byte 6 bit 3 is set on GTROM images, but it is a board flag, not
     a request for four-screen mirroring: this mapper drives all four name
     tables from its own RAM. Clear it before InfoNES_SetupPPU's choice can
     matter, and so the MMC3-style guards elsewhere stay out of the way. */
  ROM_FourScr = 0;

  /* Allocate the 16KB CHR RAM + 8KB name table RAM once; Map111_Init is also
     the reset entry point. */
  if ( !Map111_Chr_Ram )
  {
    Map111_Chr_Ram = (BYTE *)Frens::f_malloc( MAP111_RAM_SIZE );
    if ( Map111_Chr_Ram )
      InfoNES_MemorySet( Map111_Chr_Ram, 0, MAP111_RAM_SIZE );
  }
  MapperChrRam = Map111_Chr_Ram;
  MapperChrRamSize = Map111_Chr_Ram ? MAP111_RAM_SIZE : 0;
  MapperNtRam = Map111_Chr_Ram ? Map111_Chr_Ram + MAP111_CHR_RAM_SIZE : nullptr;
  MapperNtRamSize = Map111_Chr_Ram ? MAP111_NT_RAM_SIZE : 0;

  /* ID mode swaps ROMBANK0 only: the whole 32KB window is one bank, so the
     flash routine has to be running from CPU RAM, and swapping more than the
     window the game polls would take the reset vectors with it. */
  SstFlash_Init( &Map111_Flash, Map111_Sync, 0x01 );

  Map111_Reg = 0;
  Map111_Sync();

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 );
}
