/*===================================================================*/
/*                                                                   */
/*                         Mapper 185  (Tecmo)                       */
/*                                                                   */
/*===================================================================*/

/* CNROM with a copy protection. The board has one 8KB CHR ROM whose chip
 * enable is driven by the CNROM latch, so writing the wrong value to
 * $8000-$FFFF disconnects the CHR ROM and the PPU reads an open bus with a
 * pull-up on D0 (Map185_Dummy_Chr_Rom, 0xff). Each game reads a few bytes of
 * the pattern table back and refuses to start if the answer is not the one it
 * expects - some check that the data IS there, others that it is NOT.
 *
 * Which latch values enable the CHR ROM differs per cartridge. NES 2.0
 * encodes it in the submapper (4-7: CHR on when the low two bits of the latch
 * hold 0, 1, 2 or 3 respectively); an iNES 1.0 image carries no submapper, so
 * the value is guessed the way Mesen does it and the carts the guess gets
 * wrong are listed by CRC below. */

BYTE *Map185_Dummy_Chr_Rom;

/* The last value written to the latch, kept for the save state. */
static BYTE Map185_Reg;

/* 4-7 select one of the NES 2.0 submapper rules, 0 the heuristic. */
static BYTE Map185_Sub;

/* iNES 1.0 images that the heuristic gets wrong, with the submapper rule they
 * need. Seicross writes $21 and expects the eight bytes it reads back from
 * $0700 NOT to match the copy held in PRG - the heuristic enables the CHR ROM
 * for $21, the check then passes and the game spins at $809f forever. Under
 * submapper 4 $21 disconnects the CHR ROM and the following $20 restores it. */
static const struct { uint32_t dwCrc; BYTE bySub; } Map185_Sub_Crcs[] =
{
  { 0x0F05FF0A, 4 },   /* Seicross (Japan) (Rev 1) */
};

/*-------------------------------------------------------------------*/
/*  Is the CHR ROM connected for this latch value?                   */
/*-------------------------------------------------------------------*/
static bool Map185_ChrEnabled( BYTE byData )
{
  if ( Map185_Sub >= 4 && Map185_Sub <= 7 )
    return ( byData & 0x03 ) == (BYTE)( Map185_Sub - 4 );

  /* No submapper: CHR ROM is enabled unless the low nibble is zero, with $13
   * (Spy vs Spy) as the known exception. */
  return ( byData & 0x0f ) != 0 && byData != 0x13;
}

/*-------------------------------------------------------------------*/
/*  Point the pattern tables at the CHR ROM or at the open bus        */
/*-------------------------------------------------------------------*/
static void Map185_Set_Chr()
{
  if ( Map185_ChrEnabled( Map185_Reg ) )
  {
    for ( int nPage = 0; nPage < 8; ++nPage )
      PPUBANK[ nPage ] = VROMPAGE( nPage );
  } else {
    for ( int nPage = 0; nPage < 8; ++nPage )
      PPUBANK[ nPage ] = Map185_Dummy_Chr_Rom;
  }
  InfoNES_SetupChr();
}

/*-------------------------------------------------------------------*/
/*  Save state support: the latch                                     */
/*-------------------------------------------------------------------*/
/* While the CHR ROM is disconnected the pattern table banks point at
 * Map185_Dummy_Chr_Rom, which is not part of the CHR ROM, so the bank indices
 * state.cpp stores are meaningless. Re-applying the latch after the load puts
 * them back. */
static int Map185_BlobSize()
{
  return 1;
}

static void Map185_SaveBlob( BYTE *pBuf )
{
  pBuf[ 0 ] = Map185_Reg;
}

static void Map185_LoadBlob( BYTE *pBuf )
{
  Map185_Reg = pBuf[ 0 ];
  Map185_Set_Chr();
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 185                                            */
/*-------------------------------------------------------------------*/
void Map185_Init()
{
  /* Initialize Mapper */
  MapperInit = Map185_Init;

  /* Write to Mapper */
  MapperWrite = Map185_Write;

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

  /* Set ROM Banks. Bird Week has a single 16KB bank, which the second half of
     the address space mirrors. */
  ROMBANK0 = ROMPAGE( 0 );
  ROMBANK1 = ROMPAGE( 1 );
  if ( NesHeader.byRomSize > 1 )
  {
    ROMBANK2 = ROMPAGE( 2 );
    ROMBANK3 = ROMPAGE( 3 );
  } else {
    ROMBANK2 = ROMPAGE( 0 );
    ROMBANK3 = ROMPAGE( 1 );
  }

  /* Initialize Dummy VROM (allocated once; InfoNES_Fin frees it) */
  if ( !Map185_Dummy_Chr_Rom )
  {
    Map185_Dummy_Chr_Rom = (BYTE *)Frens::f_malloc( 0x400 );
  }
  InfoNES_MemorySet( Map185_Dummy_Chr_Rom, 0xff, 0x400 );

  /* Pick the enable rule: the NES 2.0 submapper, else the CRC list, else the
     heuristic. */
  Map185_Sub = ( SubMapperNo >= 4 && SubMapperNo <= 7 ) ? SubMapperNo : 0;
  if ( Map185_Sub == 0 )
  {
    for ( const auto &entry : Map185_Sub_Crcs )
      if ( entry.dwCrc == InfoNES_RomCrc )
        Map185_Sub = entry.bySub;
  }

  /* Power on with the CHR ROM connected, as a cartridge that never writes the
     latch has no other way of showing anything. */
  Map185_Reg = ( Map185_Sub >= 4 ) ? (BYTE)( Map185_Sub - 4 ) : 0xff;
  Map185_Set_Chr();

  /* Save state hooks (cleared on every reset, so install them here) */
  MapperBlobSize = Map185_BlobSize;
  MapperSaveBlob = Map185_SaveBlob;
  MapperLoadBlob = Map185_LoadBlob;

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 ); 
}

/*-------------------------------------------------------------------*/
/*  Mapper 185 Write Function                                        */
/*-------------------------------------------------------------------*/
void Map185_Write( WORD wAddr, BYTE byData )
{
  Map185_Reg = byData;
  Map185_Set_Chr();
}
