/*===================================================================*/
/*                                                                   */
/*                        Mapper 13 : CPROM                          */
/*                                                                   */
/*===================================================================*/

/* CHR RAM (16KB = 4 x 4KB banks). PPU $0000-$0FFF is fixed to the first 4KB,
 * $1000-$1FFF selects one of the four. That does not fit in the 8KB the core
 * reserves at the bottom of PPURAM: CRAMPAGE() would put banks 2 and 3 on the
 * nametables. Allocated lazily here, freed in InfoNES_Fin. */
BYTE *Map13_Chr_Ram;

#define Map13_CRAMPAGE(a) &Map13_Chr_Ram[((a) & 0x0F) * 0x400]

/*-------------------------------------------------------------------*/
/*  Select the 4KB CHR RAM bank at PPU $1000-$1FFF                   */
/*-------------------------------------------------------------------*/
static void Map13_SetChrBank( BYTE byBank )
{
  if ( Map13_Chr_Ram )
  {
    BYTE byPage = ( byBank & 0x03 ) << 2;
    for ( int i = 0; i < 4; ++i )
      PPUBANK[ 4 + i ] = Map13_CRAMPAGE( byPage + i );
  }
  else
  {
    /* Allocation failed (only possible on a RAM-constrained RP2040). Stay on
     * bank 0 inside PPURAM: the wrong tiles are drawn, but the nametables are
     * left alone. */
    for ( int i = 0; i < 4; ++i )
      PPUBANK[ 4 + i ] = CRAMPAGE( i );
  }
  InfoNES_SetupChr();
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 13                                             */
/*-------------------------------------------------------------------*/
void Map13_Init()
{
  /* Initialize Mapper */
  MapperInit = Map13_Init;

  /* Write to Mapper */
  MapperWrite = Map13_Write;

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
  ROMBANK2 = ROMPAGE( 2 );
  ROMBANK3 = ROMPAGE( 3 );

  /* Allocate the 16KB CHR RAM once; Map13_Init is also the reset entry point. */
  if ( !Map13_Chr_Ram )
  {
    Map13_Chr_Ram = (BYTE *)Frens::f_malloc( MAP13_CHR_RAM_SIZE );
    if ( Map13_Chr_Ram )
      InfoNES_MemorySet( Map13_Chr_Ram, 0, MAP13_CHR_RAM_SIZE );
  }
  MapperChrRam     = Map13_Chr_Ram;
  MapperChrRamSize = Map13_Chr_Ram ? MAP13_CHR_RAM_SIZE : 0;

  /* Set PPU Banks: $0000-$0FFF fixed to the first 4KB, $1000-$1FFF bank 0 */
  for ( int i = 0; i < 4; ++i )
    PPUBANK[ i ] = Map13_Chr_Ram ? Map13_CRAMPAGE( i ) : CRAMPAGE( i );
  Map13_SetChrBank( 0 );

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 ); 
}

/*-------------------------------------------------------------------*/
/*  Mapper 13 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map13_Write( WORD wAddr, BYTE byData )
{
  /* Set ROM Banks */
  ROMBANK0 = ROMPAGE((((byData&0x30)>>2)+0) % (NesHeader.byRomSize<<1));
  ROMBANK1 = ROMPAGE((((byData&0x30)>>2)+1) % (NesHeader.byRomSize<<1));
  ROMBANK2 = ROMPAGE((((byData&0x30)>>2)+2) % (NesHeader.byRomSize<<1));
  ROMBANK3 = ROMPAGE((((byData&0x30)>>2)+3) % (NesHeader.byRomSize<<1));

  /* Set PPU Banks */
  Map13_SetChrBank( byData & 0x03 );
}
