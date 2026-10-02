/*===================================================================*/
/*                                                                   */
/*                 Mapper 70 (74161/32 Bandai)                       */
/*                                                                   */
/*===================================================================*/

/* Same board as mapper 152, which also wires bit 7 to one-screen
   mirroring. As in Mesen, a cart that ever sets bit 7 is taken to use it. */
static bool Map70_Mirror_Ctl;

/*-------------------------------------------------------------------*/
/*  Save state support: the one-screen selection                     */
/*-------------------------------------------------------------------*/
/* state.cpp puts the header mirroring back after restoring the banks,
 * which would undo the one-screen page the game last selected. */
static int Map70_BlobSize()
{
  return 5;
}

static void Map70_SaveBlob( BYTE *pBuf )
{
  for ( int i = 0; i < 4; ++i )
    pBuf[ i ] = (BYTE)( ( PPUBANK[ NAME_TABLE0 + i ] - VRAMPAGE( 0 ) ) / 0x400 );
  pBuf[ 4 ] = Map70_Mirror_Ctl;
}

static void Map70_LoadBlob( BYTE *pBuf )
{
  for ( int i = 0; i < 4; ++i )
    PPUBANK[ NAME_TABLE0 + i ] = VRAMPAGE( pBuf[ i ] & 0x03 );
  Map70_Mirror_Ctl = pBuf[ 4 ];
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 70                                             */
/*-------------------------------------------------------------------*/
void Map70_Init()
{
  /* Initialize Mapper */
  MapperInit = Map70_Init;

  /* Write to Mapper */
  MapperWrite = Map70_Write;

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

  /* Set SRAM Banks */
  SRAMBANK = SRAM;

  /* Set ROM Banks */
  ROMBANK0 = ROMPAGE( 0 );
  ROMBANK1 = ROMPAGE( 1 );
  ROMBANK2 = ROMLASTPAGE( 1 );
  ROMBANK3 = ROMLASTPAGE( 0 );

  /* Set PPU Banks */
  if ( NesHeader.byVRomSize > 0 )
  {
    for ( int nPage = 0; nPage < 8; ++nPage )
      PPUBANK[ nPage ] = VROMPAGE( nPage );
    InfoNES_SetupChr();
  }

  /* Vertical, whatever the header says: Kamen Rider Club's header is wrong */
  InfoNES_Mirroring( 1 );
  Map70_Mirror_Ctl = false;

  /* Save state hooks (cleared on every reset, so install them here) */
  MapperBlobSize = Map70_BlobSize;
  MapperSaveBlob = Map70_SaveBlob;
  MapperLoadBlob = Map70_LoadBlob;

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 );
}

/*-------------------------------------------------------------------*/
/*  Mapper 70 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map70_Write( WORD wAddr, BYTE byData )
{
  /* Set ROM Banks */
  int nPrgBank = ( ( byData >> 4 ) & 0x07 ) << 1;
  nPrgBank %= ( NesHeader.byRomSize << 1 );
  ROMBANK0 = ROMPAGE( nPrgBank );
  ROMBANK1 = ROMPAGE( nPrgBank + 1 );

  /* Set PPU Banks */
  if ( NesHeader.byVRomSize > 0 )
  {
    int nChrBank = ( byData & 0x0f ) << 3;
    nChrBank %= ( NesHeader.byVRomSize << 3 );
    for ( int nPage = 0; nPage < 8; ++nPage )
      PPUBANK[ nPage ] = VROMPAGE( nChrBank + nPage );
    InfoNES_SetupChr();
  }

  /* Name Table Mirroring: bit 7 selects the one-screen page */
  if ( byData & 0x80 )
    Map70_Mirror_Ctl = true;

  if ( Map70_Mirror_Ctl )
  {
    if ( byData & 0x80 )
    {
      InfoNES_Mirroring( 2 );
    } else {
      InfoNES_Mirroring( 3 );
    }
  }
}
