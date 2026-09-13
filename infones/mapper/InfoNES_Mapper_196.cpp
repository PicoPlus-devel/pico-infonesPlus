/*===================================================================*/
/*                                                                   */
/*        Mapper 196 : MMC3 with rewired register address lines      */
/*                                                                   */
/*===================================================================*/

/*
 * Pirate MMC3 clones - Super Bros. 11 (Mario Adventures), Super Mario Bros.
 * 17, Master Fighter II - built by copying an MMC3 and swapping the address
 * lines that pick its registers, so that a stock MMC3 driver writes the wrong
 * register and a dump of the board looks unlike its donor cartridge.
 *
 * The chip's A0 (the bit that tells $8000 from $8001) is not wired to CPU A0
 * at all. It is driven by
 *
 *   $c000-$ffff   A2 | A3
 *   $8000-$bffff  A1 | A2 | A3
 *
 * so the game writes $8000/$8002 where an MMC3 game writes $8000/$8001, and
 * $c000/$c004 where it writes $c000/$c001. Undo that and the rest of the
 * board - PRG, CHR, mirroring, the scanline IRQ - is a plain MMC3.
 *
 * One addition: a write anywhere in $6000-$6fff takes PRG banking away from
 * the MMC3 and puts one 32KB bank across the whole ROM window instead
 * (Master Fighter II's UT1374 PCB). The board has no work RAM, so nothing
 * else can be writing there. Once on it stays on, and it has to be reapplied
 * after every MMC3 write, which reprograms the same banks on its way past.
 */

/*-------------------------------------------------------------------*/
/*  Mapper 196 resources                                             */
/*-------------------------------------------------------------------*/

static BYTE Map196_Prg_Override;  /* 0 = the MMC3 keeps its PRG banking  */
static BYTE Map196_Prg;           /* 32KB bank selected at $6000-$6fff   */

/*-------------------------------------------------------------------*/
/*  Mapper 196 Set CPU Banks Function                                */
/*-------------------------------------------------------------------*/
void Map196_Set_CPU_Banks()
{
  DWORD dwPage;

  if ( !Map196_Prg_Override )
  {
    Map4_Set_CPU_Banks();
    return;
  }

  dwPage = (DWORD)Map196_Prg << 2;

  ROMBANK0 = ROMPAGE( ( dwPage + 0 ) % ( NesHeader.byRomSize << 1 ) );
  ROMBANK1 = ROMPAGE( ( dwPage + 1 ) % ( NesHeader.byRomSize << 1 ) );
  ROMBANK2 = ROMPAGE( ( dwPage + 2 ) % ( NesHeader.byRomSize << 1 ) );
  ROMBANK3 = ROMPAGE( ( dwPage + 3 ) % ( NesHeader.byRomSize << 1 ) );
}

/*-------------------------------------------------------------------*/
/*  Mapper 196 Write Function                                        */
/*-------------------------------------------------------------------*/
void Map196_Write( WORD wAddr, BYTE byData )
{
  WORD wReg;

  /* Rebuild the address the MMC3 inside would have seen: its A0 comes from
     the CPU address lines above it, never from CPU A0. */
  if ( wAddr >= 0xc000 )
  {
    wReg = (WORD)( ( wAddr & 0xfffe )
                 | ( ( wAddr >> 2 ) & 0x01 )
                 | ( ( wAddr >> 3 ) & 0x01 ) );
  } else {
    wReg = (WORD)( ( wAddr & 0xfffe )
                 | ( ( wAddr >> 1 ) & 0x01 )
                 | ( ( wAddr >> 2 ) & 0x01 )
                 | ( ( wAddr >> 3 ) & 0x01 ) );
  }

  Map4_Write( wReg, byData );

  /* Map4_Write has just reapplied the MMC3 PRG mapping over ours. */
  if ( Map196_Prg_Override )
  {
    Map196_Set_CPU_Banks();
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 196 Write to SRAM Function                                */
/*-------------------------------------------------------------------*/
void Map196_Sram( WORD wAddr, BYTE byData )
{
  if ( wAddr <= 0x6fff )
  {
    Map196_Prg_Override = 1;
    Map196_Prg = (BYTE)( ( byData & 0x0f ) | ( byData >> 4 ) );
    Map196_Set_CPU_Banks();
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 196 state save/load                                       */
/*-------------------------------------------------------------------*/

/* The MMC3 blob with this board's two registers appended to it. */
struct Map196State
{
  BYTE Prg_Override;
  BYTE Prg;
};

static int Map196BlobSize()
{
  return Map4BlobSize() + (int)sizeof( Map196State );
}

static void Map196SaveBlob( BYTE *pBuf )
{
  Map196State *pState = (Map196State *)( pBuf + Map4BlobSize() );

  Map4SaveBlob( pBuf );

  pState->Prg_Override = Map196_Prg_Override;
  pState->Prg          = Map196_Prg;
}

static void Map196LoadBlob( BYTE *pBuf )
{
  Map196State *pState = (Map196State *)( pBuf + Map4BlobSize() );

  Map4LoadBlob( pBuf );

  Map196_Prg_Override = pState->Prg_Override;
  Map196_Prg          = pState->Prg;

  /* Map4LoadBlob has just put the MMC3 PRG mapping back. */
  Map196_Set_CPU_Banks();
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 196                                            */
/*-------------------------------------------------------------------*/
void Map196_Init()
{
  /* PRG, CHR, mirroring and the scanline IRQ come from MMC3. */
  Map4_Init();

  /* ... the reset entry point, the rewired writes and the blob are ours. */
  MapperInit  = Map196_Init;
  MapperWrite = Map196_Write;
  MapperSram  = Map196_Sram;

  MapperBlobSize = Map196BlobSize;
  MapperSaveBlob = Map196SaveBlob;
  MapperLoadBlob = Map196LoadBlob;

  /* Power on as a plain MMC3: Map4_Init has already set the banks. */
  Map196_Prg_Override = 0;
  Map196_Prg = 0;
}
