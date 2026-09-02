/*===================================================================*/
/*                                                                   */
/*                      Mapper 34 (Nina-1)                           */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 34                                             */
/*-------------------------------------------------------------------*/
void Map34_Init()
{
  /* Initialize Mapper */
  MapperInit = Map34_Init;

  /* Write to Mapper */
  MapperWrite = Map34_Write;

  /* Write to SRAM */
  MapperSram = Map34_Sram;

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
  /* Both boards map one 32KB bank across $8000-$FFFF, so power on with a
     coherent bank rather than a first-16KB/last-16KB mix. The reset vector
     is read from the last page, so the last 32KB bank is the one to pick:
     a BNROM game that jumps below $C000 before writing the bank register
     (DataMan) otherwise lands in the wrong bank and runs BRKs forever. */
  if ( NesHeader.byRomSize >= 2 )   /* at least 32KB of PRG */
  {
    ROMBANK0 = ROMLASTPAGE( 3 );
    ROMBANK1 = ROMLASTPAGE( 2 );
  }
  else
  {
    ROMBANK0 = ROMPAGE( 0 );
    ROMBANK1 = ROMPAGE( 1 );
  }
  ROMBANK2 = ROMLASTPAGE( 1 );
  ROMBANK3 = ROMLASTPAGE( 0 );

  /* Set PPU Banks */
  if ( NesHeader.byVRomSize > 0 )
  {
    for ( int nPage = 0; nPage < 8; ++nPage )
      PPUBANK[ nPage ] = VROMPAGE( nPage );
    InfoNES_SetupChr();
  }

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 ); 
}

/*-------------------------------------------------------------------*/
/*  Mapper 34 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map34_Write( WORD wAddr, BYTE byData )
{
  /* Set ROM Banks */
  byData <<= 2;
  byData %= ( NesHeader.byRomSize << 1);

  ROMBANK0 = ROMPAGE( byData );
  ROMBANK1 = ROMPAGE( byData + 1 );
  ROMBANK2 = ROMPAGE( byData + 2 );
  ROMBANK3 = ROMPAGE( byData + 3 );
}

/*-------------------------------------------------------------------*/
/*  Mapper 34 Write to SRAM Function                                 */
/*-------------------------------------------------------------------*/
void Map34_Sram( WORD wAddr, BYTE byData )
{
  /* The NINA-001 registers only exist on the CHR-ROM variant of mapper 34.
     A BNROM board has plain WRAM here and no VROM at all, so the $7ffe /
     $7fff cases would divide by zero and then point PPUBANK[0..3] at a
     null VROM. */
  if ( NesHeader.byVRomSize == 0 )
    return;

  switch(wAddr)
  {
    /* Set ROM Banks */
    case 0x7ffd:
      byData <<= 2;
      byData %= ( NesHeader.byRomSize << 1 );

      ROMBANK0 = ROMPAGE( byData );
      ROMBANK1 = ROMPAGE( byData + 1 );
      ROMBANK2 = ROMPAGE( byData + 2 );
      ROMBANK3 = ROMPAGE( byData + 3 );
      break;

    /* Set PPU Banks */
    case 0x7ffe:
      byData <<= 2;
      byData %= ( NesHeader.byVRomSize << 3 );
      
      PPUBANK[ 0 ] = VROMPAGE( byData );
      PPUBANK[ 1 ] = VROMPAGE( byData + 1 );
      PPUBANK[ 2 ] = VROMPAGE( byData + 2 );
      PPUBANK[ 3 ] = VROMPAGE( byData + 3 );
      InfoNES_SetupChr();
      break;

    /* Set PPU Banks */
    case 0x7fff:
      byData <<= 2;
      byData %= ( NesHeader.byVRomSize << 3 );
      
      PPUBANK[ 4 ] = VROMPAGE( byData );
      PPUBANK[ 5 ] = VROMPAGE( byData + 1 );
      PPUBANK[ 6 ] = VROMPAGE( byData + 2 );
      PPUBANK[ 7 ] = VROMPAGE( byData + 3 );
      InfoNES_SetupChr();
      break;
  }
}
