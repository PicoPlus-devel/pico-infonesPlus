/*===================================================================*/
/*                                                                   */
/*                   Mapper 96 : Bandai 74161                        */
/*                                                                   */
/*===================================================================*/

BYTE	Map96_Reg[2];

/* CHR RAM (32KB = 8 x 4KB banks). Map96_Reg[0] is 0 or 1 and Map96_Reg[1] is
 * 0..3, so the selected 4KB bank runs 0..7 and the 1KB page index reaches 31 -
 * well past the 8KB the core reserves at the bottom of PPURAM. Through
 * CRAMPAGE() pages 8-15 land on the nametables and 16-31 run off the end of the
 * 16KB allocation entirely. Allocated lazily in Map96_Init, freed in
 * InfoNES_Fin. */
BYTE *Map96_Chr_Ram;

#define Map96_CRAMPAGE(a) &Map96_Chr_Ram[((a) & 0x1F) * 0x400]

/*-------------------------------------------------------------------*/
/*  Save state support: the two bank registers                       */
/*-------------------------------------------------------------------*/
/* Map96_Reg[1] is refreshed constantly by Map96_PPU, but Map96_Reg[0] only
 * changes on a mapper write, so a stale value after a restore would mis-bank
 * until the game next writes. The CHR RAM itself is written straight to the
 * state file by state.cpp. */
static int Map96BlobSize()
{
  return 2;
}

static void Map96SaveBlob( BYTE *pBuf )
{
  pBuf[ 0 ] = Map96_Reg[ 0 ];
  pBuf[ 1 ] = Map96_Reg[ 1 ];
}

static void Map96LoadBlob( BYTE *pBuf )
{
  Map96_Reg[ 0 ] = pBuf[ 0 ];
  Map96_Reg[ 1 ] = pBuf[ 1 ];
  Map96_Set_Banks();
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 96                                             */
/*-------------------------------------------------------------------*/
void Map96_Init()
{
  /* Initialize Mapper */
  MapperInit = Map96_Init;

  /* Write to Mapper */
  MapperWrite = Map96_Write;

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
  MapperPPU = Map96_PPU;

  /* Callback at Rendering Screen ( 1:BG, 0:Sprite ) */
  MapperRenderScreen = Map0_RenderScreen;

  /* Set SRAM Banks */
  SRAMBANK = SRAM;

  /* Save state hooks (cleared on every reset, so install them here) */
  MapperBlobSize = Map96BlobSize;
  MapperSaveBlob = Map96SaveBlob;
  MapperLoadBlob = Map96LoadBlob;

  /* Set Registers */
  Map96_Reg[0] = Map96_Reg[1] = 0;

  /* Allocate the 32KB CHR RAM once; Map96_Init is also the reset entry point. */
  if ( !Map96_Chr_Ram )
  {
    Map96_Chr_Ram = (BYTE *)Frens::f_malloc( MAP96_CHR_RAM_SIZE );
    if ( Map96_Chr_Ram )
      InfoNES_MemorySet( Map96_Chr_Ram, 0, MAP96_CHR_RAM_SIZE );
  }
  MapperChrRam     = Map96_Chr_Ram;
  MapperChrRamSize = Map96_Chr_Ram ? MAP96_CHR_RAM_SIZE : 0;

  /* Set ROM Banks */
  ROMBANK0 = ROMPAGE( 0 );
  ROMBANK1 = ROMPAGE( 1 );
  ROMBANK2 = ROMPAGE( 2 );
  ROMBANK3 = ROMPAGE( 3 );

  /* Set PPU Banks */
  Map96_Set_Banks();
  InfoNES_Mirroring( 3 );

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 ); 
}

/*-------------------------------------------------------------------*/
/*  Mapper 96 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map96_Write( WORD wAddr, BYTE byData )
{
  ROMBANK0 = ROMPAGE((((byData & 0x03)<<2)+0) % (NesHeader.byRomSize<<1));
  ROMBANK1 = ROMPAGE((((byData & 0x03)<<2)+1) % (NesHeader.byRomSize<<1));
  ROMBANK2 = ROMPAGE((((byData & 0x03)<<2)+2) % (NesHeader.byRomSize<<1));
  ROMBANK3 = ROMPAGE((((byData & 0x03)<<2)+3) % (NesHeader.byRomSize<<1));
  
  Map96_Reg[0] = (byData & 0x04) >> 2;
  Map96_Set_Banks();
}

/*-------------------------------------------------------------------*/
/*  Mapper 96 PPU Function                                           */
/*-------------------------------------------------------------------*/
void Map96_PPU( WORD wAddr )
{
  if( (wAddr & 0xF000) == 0x2000 ) {
    Map96_Reg[1] = (wAddr>>8)&0x03;
    Map96_Set_Banks();
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 96 Set Banks Function                                     */
/*-------------------------------------------------------------------*/
void Map96_Set_Banks()
{
  if ( Map96_Chr_Ram )
  {
    BYTE byLow  = ( Map96_Reg[0] * 4 + Map96_Reg[1] ) << 2;  /* $0000-$0FFF */
    BYTE byHigh = ( Map96_Reg[0] * 4 + 0x03         ) << 2;  /* $1000-$1FFF */
    for ( int i = 0; i < 4; ++i )
    {
      PPUBANK[ 0 + i ] = Map96_CRAMPAGE( byLow  + i );
      PPUBANK[ 4 + i ] = Map96_CRAMPAGE( byHigh + i );
    }
  }
  else
  {
    /* Allocation failed (only possible on a RAM-constrained RP2040). Stay on
     * the first 8KB inside PPURAM: the wrong tiles are drawn, but the
     * nametables and the heap are left alone. */
    for ( int i = 0; i < 8; ++i )
      PPUBANK[ i ] = CRAMPAGE( i );
  }
  InfoNES_SetupChr();
}

