/*===================================================================*/
/*                                                                   */
/*                   Mapper 208 (Gouder SL-1798)                     */
/*                                                                   */
/*===================================================================*/

/*
 * Street Fighter IV (Unl), the only game on this board.
 *
 * An MMC3 with its PRG banking taken over and a protection chip bolted on:
 *
 *   $4800-$4fff  32KB PRG bank = ( D & 0x01 ) | ( ( D >> 3 ) & 0x02 )
 *   $6800-$6fff  the same register a second time
 *   $5000-$57ff  protection index
 *   $5800-$5fff  protection data: $5800-$5803 latch D ^ xor( index ) and
 *                read back there
 *   $8000-$ffff  stock MMC3, except that its two PRG registers do nothing
 *
 * CHR banking, mirroring, WRAM and the scanline IRQ are plain MMC3 - see
 * Map208_Init for the one thing the header gets wrong about them - so this
 * mapper is Map4 plus the register file above. Nothing here is allocated:
 * the six bytes below are the whole cost of the board.
 */

/*-------------------------------------------------------------------*/
/*  Mapper 208 resources                                             */
/*-------------------------------------------------------------------*/

static BYTE  Map208_Prot[ 4 ];  /* $5800-$5803, written and read back     */
static BYTE  Map208_Index;      /* $5000-$57ff, picks the XOR value       */
static BYTE  Map208_Prg;        /* 32KB PRG bank in the whole ROM window  */

/*-------------------------------------------------------------------*/
/*  Mapper 208 Protection Value Function                             */
/*-------------------------------------------------------------------*/
static BYTE Map208_Xor( BYTE byIndex )
{
/*
 *  The XOR value the protection chip applies to a write at $5800-$5fff,
 *  selected by the last byte written to $5000-$57ff.
 *
 *  Other emulators carry this as a 256-byte table. The table is regular, and
 *  the three rules below reproduce it entry for entry (verified over all 256
 *  indices), so it costs a few instructions instead of a quarter of a KB:
 *
 *    - only four bits are ever set in the result: 0x40 0x10 0x08 0x01;
 *    - index bits 2 and 5 are ignored;
 *    - the result is 0x59 (or 0x00 when index bit 6 is set), with those four
 *      bits flipped by index bits 1, 0, 4 and 7 respectively - but only when
 *      index bits 3 and 6 differ, otherwise the constant is returned as is.
 */
  BYTE byBase = (BYTE)( ( byIndex & 0x40 ) ? 0x00 : 0x59 );
  BYTE byFlip;

  if ( !( ( ( byIndex >> 3 ) ^ ( byIndex >> 6 ) ) & 0x01 ) )
    return byBase;

  byFlip = (BYTE)( ( ( byIndex & 0x02 ) ? 0x40 : 0x00 )
                 | ( ( byIndex & 0x01 ) ? 0x10 : 0x00 )
                 | ( ( byIndex & 0x10 ) ? 0x08 : 0x00 )
                 | ( ( byIndex & 0x80 ) ? 0x01 : 0x00 ) );

  return (BYTE)( byBase ^ byFlip );
}

/*-------------------------------------------------------------------*/
/*  Mapper 208 Set CPU Banks Function                                */
/*-------------------------------------------------------------------*/
void Map208_Set_CPU_Banks()
{
  /* One 32KB bank fills the whole ROM window: the MMC3's own PRG registers
     and its $8000 swap bit are not wired to anything on this board. */
  DWORD dwPage = (DWORD)Map208_Prg << 2;

  ROMBANK0 = ROMPAGE( ( dwPage + 0 ) % ( NesHeader.byRomSize << 1 ) );
  ROMBANK1 = ROMPAGE( ( dwPage + 1 ) % ( NesHeader.byRomSize << 1 ) );
  ROMBANK2 = ROMPAGE( ( dwPage + 2 ) % ( NesHeader.byRomSize << 1 ) );
  ROMBANK3 = ROMPAGE( ( dwPage + 3 ) % ( NesHeader.byRomSize << 1 ) );
}

/*-------------------------------------------------------------------*/
/*  Mapper 208 Write Function                                        */
/*-------------------------------------------------------------------*/
void Map208_Write( WORD wAddr, BYTE byData )
{
  /* Map4_Write reapplies the MMC3 PRG mapping on its way through, so put
     ours back over it. */
  Map4_Write( wAddr, byData );
  Map208_Set_CPU_Banks();
}

/*-------------------------------------------------------------------*/
/*  Mapper 208 Write to APU Function                                 */
/*-------------------------------------------------------------------*/
void Map208_Apu( WORD wAddr, BYTE byData )
{
  if ( wAddr >= 0x5800 )
  {
    /* $5800-$5fff: only the low two address bits are decoded. */
    Map208_Prot[ wAddr & 0x03 ] = (BYTE)( byData ^ Map208_Xor( Map208_Index ) );
  }
  else if ( wAddr >= 0x5000 )
  {
    /* $5000-$57ff */
    Map208_Index = byData;
  }
  else if ( wAddr >= 0x4800 && wAddr <= 0x4fff )
  {
    Map208_Prg = (BYTE)( ( byData & 0x01 ) | ( ( byData >> 3 ) & 0x02 ) );
    Map208_Set_CPU_Banks();
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 208 Read from APU Function                                */
/*-------------------------------------------------------------------*/
BYTE Map208_ReadApu( WORD wAddr )
{
  if ( wAddr >= 0x5800 )
    return Map208_Prot[ wAddr & 0x03 ];

  return Map0_ReadApu( wAddr );
}

/*-------------------------------------------------------------------*/
/*  Mapper 208 Write to SRAM Function                                */
/*-------------------------------------------------------------------*/
void Map208_Sram( WORD wAddr, BYTE byData )
{
  /* The bank register answers at $6800-$6fff as well. */
  if ( wAddr >= 0x6800 && wAddr <= 0x6fff )
  {
    Map208_Prg = (BYTE)( ( byData & 0x01 ) | ( ( byData >> 3 ) & 0x02 ) );
    Map208_Set_CPU_Banks();
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 208 state save/load                                       */
/*-------------------------------------------------------------------*/

/* The MMC3 blob with this board's registers appended to it. */
struct Map208State
{
  BYTE Prot[ 4 ];
  BYTE Index;
  BYTE Prg;
};

static int Map208BlobSize()
{
  return Map4BlobSize() + (int)sizeof( Map208State );
}

static void Map208SaveBlob( BYTE *pBuf )
{
  Map208State *pState = (Map208State *)( pBuf + Map4BlobSize() );

  Map4SaveBlob( pBuf );

  for ( int nPage = 0; nPage < 4; nPage++ )
  {
    pState->Prot[ nPage ] = Map208_Prot[ nPage ];
  }
  pState->Index = Map208_Index;
  pState->Prg   = Map208_Prg;
}

static void Map208LoadBlob( BYTE *pBuf )
{
  Map208State *pState = (Map208State *)( pBuf + Map4BlobSize() );

  Map4LoadBlob( pBuf );

  for ( int nPage = 0; nPage < 4; nPage++ )
  {
    Map208_Prot[ nPage ] = pState->Prot[ nPage ];
  }
  Map208_Index = pState->Index;
  Map208_Prg   = pState->Prg;

  /* Map4LoadBlob has just put the MMC3 PRG mapping back. */
  Map208_Set_CPU_Banks();
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 208                                            */
/*-------------------------------------------------------------------*/
void Map208_Init()
{
  /* CHR banking, WRAM and the scanline IRQ come from MMC3. */
  Map4_Init();

  /* ... the reset entry point, the register file and the blob are ours. */
  MapperInit    = Map208_Init;
  MapperWrite   = Map208_Write;
  MapperSram    = Map208_Sram;
  MapperApu     = Map208_Apu;
  MapperReadApu = Map208_ReadApu;

  MapperBlobSize = Map208BlobSize;
  MapperSaveBlob = Map208SaveBlob;
  MapperLoadBlob = Map208LoadBlob;

  for ( int nPage = 0; nPage < 4; nPage++ )
  {
    Map208_Prot[ nPage ] = 0x00;
  }
  Map208_Index = 0x00;

  /* Mirroring. The MMC3 mirroring register powers up at 0, which is vertical,
     and Street Fighter IV never writes $a000 - so vertical is what the board
     presents for the whole game, whatever the header says. Map4_Init leaves
     the header's mirroring in place (horizontal here), which put the arena's
     right half and most of the status bar on top of its left half: the frame
     around the energy bars was missing and every stage was half empty.
     Setting it here also matches what Map4LoadBlob reads back out of the
     register on a state load, so the two agree. */
  if ( !ROM_FourScr )
  {
    InfoNES_Mirroring( 1 );
  }

  /* Power on with bank 3, the last one on this 128KB cartridge. */
  Map208_Prg = 0x03;
  Map208_Set_CPU_Banks();
}
