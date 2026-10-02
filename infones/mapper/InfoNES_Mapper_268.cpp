/*===================================================================*/
/*                                                                   */
/*            Mapper 268 (COOLBOY / MINDKIDS MMC3 clone)             */
/*                                                                   */
/*===================================================================*/

/* An MMC3 with four outer bank registers, at $6000-$6003 (submapper 0,
 * COOLBOY) or $5000-$5003 (submapper 1, MINDKIDS), mirrored across the
 * range. They set the PRG/CHR base and mask, and bit 4 of the fourth one
 * switches to a mode where the outer registers pick the pages themselves.
 * Writing the fourth with bit 7 set and bit 4 clear locks all four. The
 * bank math is Mesen2's MMC3_Coolboy, which takes it from FCEUX.
 *
 * Submappers 8/9 (SMD72A, the Limited Run Games reissues) carry 256KB of
 * CHR RAM, which only fits in PSRAM (Map268_Fits). The nesdev wiki drops
 * the PRG offset bits above A19 for them, but The Empire Strikes Back only
 * runs with the full COOLBOY decoding, as in Mesen. Their CHR RAM write
 * protect (bit 5 of the first register) is not emulated. */

static BYTE Map268_Ex[ 4 ];

/* The raw MMC3 bank registers R0-R7. Map4 keeps R0/R1 with bit 0 cleared,
   and does not keep the CHR registers at all on a flat 8KB CHR RAM board,
   but the outer modes need them as written. */
static BYTE Map268_R[ 8 ];

/*-------------------------------------------------------------------*/
/*  Mapper 268 Set Banks Function                                    */
/*-------------------------------------------------------------------*/
static DWORD Map268_Prg_Page( int nSlot, DWORD dwPage )
{
  DWORD dwAddr = 0x8000 + nSlot * 0x2000;
  DWORD dwMask = ( ( 0x3f | ( Map268_Ex[ 1 ] & 0x40 ) | ( ( Map268_Ex[ 1 ] & 0x20 ) << 2 ) )
                   ^ ( ( Map268_Ex[ 0 ] & 0x40 ) >> 2 ) )
                 ^ ( ( Map268_Ex[ 1 ] & 0x80 ) >> 2 );
  DWORD dwBase = ( Map268_Ex[ 0 ] & 0x07 )
               | ( ( Map268_Ex[ 1 ] & 0x10 ) >> 1 )
               | ( ( Map268_Ex[ 1 ] & 0x0c ) << 2 )
               | ( ( Map268_Ex[ 0 ] & 0x30 ) << 2 );

  if ( ( Map268_Ex[ 3 ] & 0x40 ) && dwPage >= 0xfe && Map4_Prg_Swap() )
  {
    switch ( nSlot )
    {
      case 1:
      case 3:
        dwPage = 0;
        break;
    }
  }

  if ( !( Map268_Ex[ 3 ] & 0x10 ) )
    return ( ( dwBase << 4 ) & ~dwMask ) | ( dwPage & dwMask );

  /* The outer registers pick the 16KB page, the MMC3 only A13 */
  dwMask &= 0xf0;
  DWORD dwEmask;
  if ( Map268_Ex[ 1 ] & 0x02 )
    dwEmask = ( Map268_Ex[ 3 ] & 0x0c ) | ( ( dwAddr & 0x4000 ) >> 13 );
  else
    dwEmask = Map268_Ex[ 3 ] & 0x0e;

  return ( ( dwBase << 4 ) & ~dwMask ) | ( dwPage & dwMask ) | dwEmask | ( nSlot & 0x01 );
}

static DWORD Map268_Chr_Page( int nSlot, DWORD dwPage )
{
  DWORD dwAddr  = nSlot * 0x400;
  DWORD dwMask  = 0xff ^ ( Map268_Ex[ 0 ] & 0x80 );
  DWORD dwCbase = Map4_Chr_Swap() ? 0x1000 : 0x0000;
  DWORD dwOuter = ( ( Map268_Ex[ 0 ] & 0x08 ) << 4 ) & ~dwMask;

  if ( Map268_Ex[ 3 ] & 0x10 )
  {
    /* The outer register picks the 8KB bank, the slot the 1KB page */
    if ( Map268_Ex[ 3 ] & 0x40 )
    {
      switch ( dwCbase ^ dwAddr )
      {
        case 0x0400:
        case 0x0c00:
          dwPage &= 0x7f;
          break;
      }
    }
    return ( dwPage & 0x80 & dwMask ) | dwOuter | ( ( Map268_Ex[ 2 ] & 0x0f ) << 3 ) | nSlot;
  }

  if ( Map268_Ex[ 3 ] & 0x40 )
  {
    switch ( dwCbase ^ dwAddr )
    {
      case 0x0000:
        dwPage = Map268_R[ 0 ];
        break;

      case 0x0800:
        dwPage = Map268_R[ 1 ];
        break;

      case 0x0400:
      case 0x0c00:
        dwPage = 0;
        break;
    }
  }
  return ( dwPage & dwMask ) | dwOuter;
}

static void Map268_Set_Banks()
{
  DWORD dwPages = NesHeader.byRomSize << 1;

  if ( Map4_Prg_Swap() )
  {
    ROMBANK0 = ROMPAGE( Map268_Prg_Page( 0, 0xfe ) % dwPages );
    ROMBANK1 = ROMPAGE( Map268_Prg_Page( 1, Map268_R[ 7 ] ) % dwPages );
    ROMBANK2 = ROMPAGE( Map268_Prg_Page( 2, Map268_R[ 6 ] ) % dwPages );
    ROMBANK3 = ROMPAGE( Map268_Prg_Page( 3, 0xff ) % dwPages );
  } else {
    ROMBANK0 = ROMPAGE( Map268_Prg_Page( 0, Map268_R[ 6 ] ) % dwPages );
    ROMBANK1 = ROMPAGE( Map268_Prg_Page( 1, Map268_R[ 7 ] ) % dwPages );
    ROMBANK2 = ROMPAGE( Map268_Prg_Page( 2, 0xfe ) % dwPages );
    ROMBANK3 = ROMPAGE( Map268_Prg_Page( 3, 0xff ) % dwPages );
  }

  /* The MMC3's own slot order: two 2KB banks and four 1KB banks, swapped
     between the pattern tables by $8000 bit 7 */
  DWORD dwPage[ 8 ];
  DWORD dw2k[ 4 ] = { (DWORD)( Map268_R[ 0 ] & 0xfe ), (DWORD)( Map268_R[ 0 ] | 0x01 ),
                      (DWORD)( Map268_R[ 1 ] & 0xfe ), (DWORD)( Map268_R[ 1 ] | 0x01 ) };
  for ( int nIdx = 0; nIdx < 4; ++nIdx )
  {
    if ( Map4_Chr_Swap() )
    {
      dwPage[ nIdx ] = Map268_R[ 2 + nIdx ];
      dwPage[ 4 + nIdx ] = dw2k[ nIdx ];
    } else {
      dwPage[ nIdx ] = dw2k[ nIdx ];
      dwPage[ 4 + nIdx ] = Map268_R[ 2 + nIdx ];
    }
  }

  for ( int nSlot = 0; nSlot < 8; ++nSlot )
  {
    DWORD dwChr = Map268_Chr_Page( nSlot, dwPage[ nSlot ] );

    if ( NesHeader.byVRomSize > 0 )
      PPUBANK[ nSlot ] = VROMPAGE( dwChr % ( NesHeader.byVRomSize << 3 ) );
    else if ( MapperChrRam )
      PPUBANK[ nSlot ] = Map4_CRAMPAGE( dwChr );
    else
      PPUBANK[ nSlot ] = CRAMPAGE( dwChr & 0x07 );
  }
  InfoNES_SetupChr();
}

/*-------------------------------------------------------------------*/
/*  Mapper 268 Write Functions                                       */
/*-------------------------------------------------------------------*/
void Map268_Write( WORD wAddr, BYTE byData )
{
  if ( ( wAddr & 0xe001 ) == 0x8001 )
    Map268_R[ Map4_Regs[ 0 ] & 0x07 ] = byData;

  Map4_Write( wAddr, byData );

  /* Only $8000/$8001 move the MMC3 banks */
  if ( ( wAddr & 0xe000 ) == 0x8000 )
    Map268_Set_Banks();
}

static void Map268_Outer_Write( WORD wAddr, BYTE byData )
{
  if ( ( Map268_Ex[ 3 ] & 0x90 ) != 0x80 )
  {
    Map268_Ex[ wAddr & 0x03 ] = byData;
    Map268_Set_Banks();
  }
}

/* Submapper 0: $6000-$7FFF. The PRG RAM behind it (when enabled) has
   already taken the write. */
void Map268_Sram( WORD wAddr, BYTE byData )
{
  Map268_Outer_Write( wAddr, byData );
}

/* Submapper 1: $5000-$5FFF */
void Map268_Apu( WORD wAddr, BYTE byData )
{
  if ( wAddr >= 0x5000 )
    Map268_Outer_Write( wAddr, byData );
}

/*-------------------------------------------------------------------*/
/*  Mapper 268 state save/load                                       */
/*-------------------------------------------------------------------*/

/* The MMC3 blob with the outer registers and the raw bank registers
   appended to it. */
struct Map268State
{
  BYTE Ex[ 4 ];
  BYTE R[ 8 ];
};

static int Map268BlobSize()
{
  return Map4BlobSize() + (int)sizeof( Map268State );
}

static void Map268SaveBlob( BYTE *pBuf )
{
  Map268State *pState = (Map268State *)( pBuf + Map4BlobSize() );

  Map4SaveBlob( pBuf );
  InfoNES_MemoryCopy( pState->Ex, Map268_Ex, sizeof( Map268_Ex ) );
  InfoNES_MemoryCopy( pState->R, Map268_R, sizeof( Map268_R ) );
}

static void Map268LoadBlob( BYTE *pBuf )
{
  Map268State *pState = (Map268State *)( pBuf + Map4BlobSize() );

  Map4LoadBlob( pBuf );
  InfoNES_MemoryCopy( Map268_Ex, pState->Ex, sizeof( Map268_Ex ) );
  InfoNES_MemoryCopy( Map268_R, pState->R, sizeof( Map268_R ) );

  /* Map4LoadBlob has just put the plain MMC3 mapping back. */
  Map268_Set_Banks();
}

/*-------------------------------------------------------------------*/
/*  Mapper 268 CHR RAM size                                          */
/*-------------------------------------------------------------------*/
/* The CHR RAM the NES 2.0 header declares (byte 11), 0 if none */
static DWORD Map268_Chr_Ram_Size()
{
  if ( NesHeader.byVRomSize != 0 || ( NesHeader.byInfo2 & 0x0c ) != 0x08 )
    return 0;

  BYTE byShift = NesHeader.byReserve[ 3 ] & 0x0f;
  return byShift ? ( 64u << byShift ) : 0;
}

/* Whether this board can hold the cartridge's CHR RAM. More than MMC3's
   32KB (the Limited Run Games reissues bank 256KB) needs an RP2350 with
   PSRAM; InfoNES_Reset reports the mapper as unsupported otherwise. */
bool Map268_Fits()
{
  if ( Map268_Chr_Ram_Size() <= MAP4_CHR_RAM_SIZE )
    return true;

#if PICO_RP2350
  return Frens::isPsramEnabled();
#else
  return false;
#endif
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 268                                            */
/*-------------------------------------------------------------------*/
void Map268_Init()
{
  /* The whole CHR RAM, up to the 256KB the chip addresses */
  DWORD dwChrRam = Map268_Chr_Ram_Size();
  if ( dwChrRam > MAP4_CHR_RAM_SIZE )
    Map4_Chr_Ram_Limit = ( dwChrRam > 0x40000 ) ? 0x40000 : dwChrRam;

  /* WRAM, the CHR RAM buffer and the scanline IRQ come from MMC3. */
  Map4_Init();

  MapperInit = Map268_Init;
  MapperWrite = Map268_Write;
  if ( SubMapperNo == 1 )
    MapperApu = Map268_Apu;
  else
    MapperSram = Map268_Sram;

  MapperBlobSize = Map268BlobSize;
  MapperSaveBlob = Map268SaveBlob;
  MapperLoadBlob = Map268LoadBlob;

  InfoNES_MemorySet( Map268_Ex, 0, sizeof( Map268_Ex ) );

  /* The MMC3 power-on banks Map4_Init sets */
  static const BYTE byPowerOn[ 8 ] = { 0, 2, 4, 5, 6, 7, 0, 1 };
  InfoNES_MemoryCopy( Map268_R, byPowerOn, sizeof( Map268_R ) );

  Map268_Set_Banks();
}
