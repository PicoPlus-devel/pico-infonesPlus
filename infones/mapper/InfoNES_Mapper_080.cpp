/*===================================================================*/
/*                                                                   */
/*                     Mapper 80 (Taito X1-005)                      */
/*                                                                   */
/*===================================================================*/

/* Set by mapper 207, whose name tables follow bit 7 of $7EF0/$7EF1 in
   place of the $7EF6 mirroring register. */
static bool Map80_Alt_Mirroring;

/*-------------------------------------------------------------------*/
/*  Save state support: the name table mapping                       */
/*-------------------------------------------------------------------*/
/* state.cpp puts the header mirroring back after restoring the banks,
 * which would undo what the game last wrote to $7EF6 (or, on mapper 207,
 * to bit 7 of $7EF0/$7EF1). */
static int Map80_BlobSize()
{
  return 4;
}

static void Map80_SaveBlob( BYTE *pBuf )
{
  for ( int i = 0; i < 4; ++i )
    pBuf[ i ] = (BYTE)( ( PPUBANK[ NAME_TABLE0 + i ] - VRAMPAGE( 0 ) ) / 0x400 );
}

static void Map80_LoadBlob( BYTE *pBuf )
{
  for ( int i = 0; i < 4; ++i )
    PPUBANK[ NAME_TABLE0 + i ] = VRAMPAGE( pBuf[ i ] & 0x03 );
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 80                                             */
/*-------------------------------------------------------------------*/
void Map80_Init()
{
  /* Initialize Mapper */
  MapperInit = Map80_Init;

  /* Write to Mapper */
  MapperWrite = Map0_Write;

  /* Write to SRAM */
  MapperSram = Map80_Sram;

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

  /* Older dumps of Fudou Myouou Den carry a mapper 80 header */
  Map80_Alt_Mirroring = ( InfoNES_RomCrc == 0x7678F1D5 );

  /* Save state hooks (cleared on every reset, so install them here) */
  MapperBlobSize = Map80_BlobSize;
  MapperSaveBlob = Map80_SaveBlob;
  MapperLoadBlob = Map80_LoadBlob;

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 ); 
}

/*-------------------------------------------------------------------*/
/*  Mapper 80 Write to SRAM Function                                 */
/*-------------------------------------------------------------------*/
void Map80_Sram( WORD wAddr, BYTE byData )
{
  switch ( wAddr )
  {
    /* Set PPU Banks */
    case 0x7ef0:
      /* Mapper 207: bit 7 selects the CIRAM page for nametables 0 and 1 */
      if ( Map80_Alt_Mirroring )
      {
        PPUBANK[ NAME_TABLE0 ] = VRAMPAGE( byData >> 7 );
        PPUBANK[ NAME_TABLE1 ] = VRAMPAGE( byData >> 7 );
      }

      byData %= ( NesHeader.byVRomSize << 3 );
      PPUBANK[ 0 ] = VROMPAGE( byData );
      PPUBANK[ 1 ] = VROMPAGE( byData + 1 );
      InfoNES_SetupChr();
      break;

    case 0x7ef1:
      /* Mapper 207: bit 7 selects the CIRAM page for nametables 2 and 3 */
      if ( Map80_Alt_Mirroring )
      {
        PPUBANK[ NAME_TABLE2 ] = VRAMPAGE( byData >> 7 );
        PPUBANK[ NAME_TABLE3 ] = VRAMPAGE( byData >> 7 );
      }

      byData %= ( NesHeader.byVRomSize << 3 );
      PPUBANK[ 2 ] = VROMPAGE( byData );
      PPUBANK[ 3 ] = VROMPAGE( byData + 1 );
      InfoNES_SetupChr();
      break;
  
    case 0x7ef2:
      byData %= ( NesHeader.byVRomSize << 3 );
      PPUBANK[ 4 ] = VROMPAGE( byData );
      InfoNES_SetupChr();
      break;      

    case 0x7ef3:
      byData %= ( NesHeader.byVRomSize << 3 );
      PPUBANK[ 5 ] = VROMPAGE( byData );
      InfoNES_SetupChr();
      break;  

    case 0x7ef4:
      byData %= ( NesHeader.byVRomSize << 3 );
      PPUBANK[ 6 ] = VROMPAGE( byData );
      InfoNES_SetupChr();
      break; 

    case 0x7ef5:
      byData %= ( NesHeader.byVRomSize << 3 );
      PPUBANK[ 7 ] = VROMPAGE( byData );
      InfoNES_SetupChr();
      break; 

    /* Name Table Mirroring (not connected on mapper 207) */
    case 0x7ef6:
    case 0x7ef7:
      if ( !Map80_Alt_Mirroring )
      {
        if ( byData & 0x01 )
        {
          InfoNES_Mirroring( 1 );
        } else {
          InfoNES_Mirroring( 0 );
        }
      }
      break;

    /* Set ROM Banks */
    case 0x7efa:
    case 0x7efb:
      byData %= ( NesHeader.byRomSize << 1 );
      ROMBANK0 = ROMPAGE( byData );
      break;

    case 0x7efc:
    case 0x7efd:
      byData %= ( NesHeader.byRomSize << 1 );
      ROMBANK1 = ROMPAGE( byData );
      break;

    case 0x7efe:
    case 0x7eff:
      byData %= ( NesHeader.byRomSize << 1 );
      ROMBANK2 = ROMPAGE( byData );
      break;
  }
}
