/*===================================================================*/
/*                                                                   */
/*                  Mapper 263 (UNL-KOF97)                           */
/*                                                                   */
/*===================================================================*/

/*
 * An MMC3 with the register writes obfuscated: the data byte has four of its
 * bits permuted and three register addresses are moved. Everything else -
 * PRG/CHR banking, the scanline IRQ, WRAM - is stock MMC3, so this mapper is
 * only the write transform on top of Map4.
 *
 * Note the mirroring bit moves with the rest: Map4_Write tests bit 0 of the
 * value it receives, and bit 0 comes from the raw value's bit 1.
 *
 * Boogerman II - The Final Adventure (Asia) (Pirate) is this mapper. It is a
 * NES 2.0 image (mapper 263 = 0x107), which is why it used to load as mapper
 * 7 and show a black screen.
 */

/*-------------------------------------------------------------------*/
/*  Mapper 263 Write Function                                        */
/*-------------------------------------------------------------------*/
void Map263_Write( WORD wAddr, BYTE byData )
{
  /* out = in7 in6 in2 in4 in3 in0 in5 in1 */
  byData = (BYTE)( ( byData & 0xD8 )
                 | ( ( byData & 0x20 ) >> 4 )
                 | ( ( byData & 0x04 ) << 3 )
                 | ( ( byData & 0x02 ) >> 1 )
                 | ( ( byData & 0x01 ) << 2 ) );

  /* Exact addresses, not a mask: the stock MMC3 addresses still work too. */
  if ( wAddr == 0x9000 )
    wAddr = 0x8001;
  else if ( wAddr == 0xd000 )
    wAddr = 0xc001;
  else if ( wAddr == 0xf000 )
    wAddr = 0xe001;

  Map4_Write( wAddr, byData );
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 263                                            */
/*-------------------------------------------------------------------*/
void Map263_Init()
{
  /* Everything - banking, IRQ, the save-state blob - comes from MMC3. */
  Map4_Init();

  /* ... except that this is the reset entry point, and writes are ours. */
  MapperInit = Map263_Init;
  MapperWrite = Map263_Write;
}
