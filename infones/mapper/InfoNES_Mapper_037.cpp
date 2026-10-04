/*===================================================================*/
/*                                                                   */
/*            Mapper 37 (MMC3 with an outer bank latch)              */
/*                                                                   */
/*===================================================================*/

/* Super Mario Bros. + Tetris + Nintendo World Cup (PAL-ZZ). A latch at
 * $6000-$7FFF, written like PRG RAM, selects the game by driving the outer
 * PRG and CHR lines; the MMC3 banks inside that block (Mesen2 MMC3_37):
 *   block 0-2  PRG 64KB at $00000   (Super Mario Bros.)
 *   block 3    PRG 64KB at $10000   (Tetris)
 *   block 4-6  PRG 128KB at $20000  (Nintendo World Cup)
 *   block 7    PRG 64KB at $40000
 * Blocks 4-7 also select the upper 128KB of CHR. */

static BYTE Map37_Block;

/*-------------------------------------------------------------------*/
/*  Mapper 37 Set Banks Function                                     */
/*-------------------------------------------------------------------*/
static DWORD Map37_Prg( DWORD dwPage )
{
  if ( Map37_Block <= 2 )
    dwPage &= 0x07;
  else if ( Map37_Block == 3 )
    dwPage = ( dwPage & 0x07 ) | 0x08;
  else if ( Map37_Block == 7 )
    dwPage = ( dwPage & 0x07 ) | 0x20;
  else
    dwPage = ( dwPage & 0x0f ) | 0x10;

  return dwPage % ( NesHeader.byRomSize << 1 );
}

static BYTE *Map37_Chr( DWORD dwPage )
{
  DWORD dwOuter = ( Map37_Block >= 4 ) ? 0x80 : 0x00;

  return VROMPAGE( ( ( dwPage & 0x7f ) | dwOuter ) % ( NesHeader.byVRomSize << 3 ) );
}

/* Map4 has already set the banks as a plain MMC3; put them in the block.
   The fixed pages ($FE/$FF) go through the same rule, so they are the
   last two of the block. */
static void Map37_Set_Banks()
{
  if ( Map4_Prg_Swap() )
  {
    ROMBANK0 = ROMPAGE( Map37_Prg( 0xfe ) );
    ROMBANK1 = ROMPAGE( Map37_Prg( Map4_Prg1 ) );
    ROMBANK2 = ROMPAGE( Map37_Prg( Map4_Prg0 ) );
    ROMBANK3 = ROMPAGE( Map37_Prg( 0xff ) );
  } else {
    ROMBANK0 = ROMPAGE( Map37_Prg( Map4_Prg0 ) );
    ROMBANK1 = ROMPAGE( Map37_Prg( Map4_Prg1 ) );
    ROMBANK2 = ROMPAGE( Map37_Prg( 0xfe ) );
    ROMBANK3 = ROMPAGE( Map37_Prg( 0xff ) );
  }

  if ( NesHeader.byVRomSize == 0 )
    return;

  if ( Map4_Chr_Swap() )
  {
    PPUBANK[ 0 ] = Map37_Chr( Map4_Chr4 );
    PPUBANK[ 1 ] = Map37_Chr( Map4_Chr5 );
    PPUBANK[ 2 ] = Map37_Chr( Map4_Chr6 );
    PPUBANK[ 3 ] = Map37_Chr( Map4_Chr7 );
    PPUBANK[ 4 ] = Map37_Chr( Map4_Chr01 + 0 );
    PPUBANK[ 5 ] = Map37_Chr( Map4_Chr01 + 1 );
    PPUBANK[ 6 ] = Map37_Chr( Map4_Chr23 + 0 );
    PPUBANK[ 7 ] = Map37_Chr( Map4_Chr23 + 1 );
  } else {
    PPUBANK[ 0 ] = Map37_Chr( Map4_Chr01 + 0 );
    PPUBANK[ 1 ] = Map37_Chr( Map4_Chr01 + 1 );
    PPUBANK[ 2 ] = Map37_Chr( Map4_Chr23 + 0 );
    PPUBANK[ 3 ] = Map37_Chr( Map4_Chr23 + 1 );
    PPUBANK[ 4 ] = Map37_Chr( Map4_Chr4 );
    PPUBANK[ 5 ] = Map37_Chr( Map4_Chr5 );
    PPUBANK[ 6 ] = Map37_Chr( Map4_Chr6 );
    PPUBANK[ 7 ] = Map37_Chr( Map4_Chr7 );
  }
  InfoNES_SetupChr();
}

/*-------------------------------------------------------------------*/
/*  Mapper 37 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map37_Write( WORD wAddr, BYTE byData )
{
  Map4_Write( wAddr, byData );

  /* Only $8000/$8001 move the MMC3 banks */
  if ( ( wAddr & 0xe000 ) == 0x8000 )
    Map37_Set_Banks();
}

/*-------------------------------------------------------------------*/
/*  Mapper 37 Write to SRAM Function                                 */
/*-------------------------------------------------------------------*/
void Map37_Sram( WORD wAddr, BYTE byData )
{
  /* The latch takes the write when the MMC3 would let it through to PRG
     RAM: enabled ($A001 bit 7) and not write-protected (bit 6) */
  if ( ( Map4_Regs[ 3 ] & 0xc0 ) == 0x80 )
  {
    Map37_Block = byData & 0x07;
    Map37_Set_Banks();
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 37 state save/load                                        */
/*-------------------------------------------------------------------*/

/* The MMC3 blob with the block latch appended to it. */
static int Map37BlobSize()
{
  return Map4BlobSize() + 1;
}

static void Map37SaveBlob( BYTE *pBuf )
{
  Map4SaveBlob( pBuf );
  pBuf[ Map4BlobSize() ] = Map37_Block;
}

static void Map37LoadBlob( BYTE *pBuf )
{
  Map4LoadBlob( pBuf );
  Map37_Block = pBuf[ Map4BlobSize() ];

  /* Map4LoadBlob has just put the plain MMC3 mapping back. */
  Map37_Set_Banks();
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 37                                             */
/*-------------------------------------------------------------------*/
void Map37_Init()
{
  /* CHR banking and the scanline IRQ come from MMC3. */
  Map4_Init();

  MapperInit = Map37_Init;
  MapperWrite = Map37_Write;
  MapperSram = Map37_Sram;

  MapperBlobSize = Map37BlobSize;
  MapperSaveBlob = Map37SaveBlob;
  MapperLoadBlob = Map37LoadBlob;

  /* Power on in the menu, which is in the first block. */
  Map37_Block = 0;
  Map37_Set_Banks();
}
