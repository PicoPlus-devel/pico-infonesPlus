/*===================================================================*/
/*                                                                   */
/*       Mapper 25 (Konami VRC4 type B/D, VRC2 type C)               */
/*                                                                   */
/*===================================================================*/

/* Map25_Regs[ 0..7 ] : the eight 1KB CHR bank registers                */
/* Map25_Regs[ 8 ]    : PRG swap mode ( $9002 bit 1 )                   */
BYTE Map25_Regs[ 9 ];

/* Last value written to the $8000 PRG register                        */
BYTE Map25_Prg0;

BYTE Map25_IRQ_Enable;
BYTE Map25_IRQ_Cnt;
BYTE Map25_IRQ_Latch;

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 25                                             */
/*-------------------------------------------------------------------*/
void Map25_Init()
{
  /* Initialize Mapper */
  MapperInit = Map25_Init;

  /* Write to Mapper */
  MapperWrite = Map25_Write;

  /* Write to SRAM */
  MapperSram = Map0_Sram;

  /* Write to APU */
  MapperApu = Map0_Apu;

  /* Read from APU */
  MapperReadApu = Map0_ReadApu;

  /* Callback at VSync */
  MapperVSync = Map0_VSync;

  /* Callback at HSync */
  MapperHSync = Map25_HSync;

  /* Callback at PPU */
  MapperPPU = Map0_PPU;

  /* Callback at Rendering Screen ( 1:BG, 0:Sprite ) */
  MapperRenderScreen = Map0_RenderScreen;

  /* Set SRAM Banks */
  SRAMBANK = SRAM;

  /* Set ROM Banks */
  Map25_Regs[ 8 ] = 0;
  Map25_Prg0 = 0;
  Map25_Set_Prg();
  ROMBANK1 = ROMPAGE( 1 );
  ROMBANK3 = ROMLASTPAGE( 0 );

  /* Set PPU Banks */
  for ( int nPage = 0; nPage < 8; ++nPage )
    Map25_Regs[ nPage ] = nPage;

  if ( NesHeader.byVRomSize > 0 )
  {
    for ( int nPage = 0; nPage < 8; ++nPage )
      PPUBANK[ nPage ] = VROMPAGE( nPage );
    InfoNES_SetupChr();
  }

  /* Initialize IRQ Registers */
  Map25_IRQ_Enable = 0;
  Map25_IRQ_Cnt = 0;
  Map25_IRQ_Latch = 0;

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 ); 
}

/*-------------------------------------------------------------------*/
/*  Mapper 25 Set PRG Banks Function                                 */
/*-------------------------------------------------------------------*/
void Map25_Set_Prg()
{
  /* $9002 bit 1 swaps the switchable $8000 bank with the fixed
   * second-to-last bank at $C000. */
  if ( Map25_Regs[ 8 ] )
  {
    ROMBANK0 = ROMLASTPAGE( 1 );
    ROMBANK2 = ROMPAGE( Map25_Prg0 );
  } else {
    ROMBANK0 = ROMPAGE( Map25_Prg0 );
    ROMBANK2 = ROMLASTPAGE( 1 );
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 25 Set CHR Bank Function                                  */
/*-------------------------------------------------------------------*/
void Map25_Set_Chr( int nBank )
{
  if ( NesHeader.byVRomSize == 0 )
    return;

  PPUBANK[ nBank ] = VROMPAGE( Map25_Regs[ nBank ] % ( NesHeader.byVRomSize << 3 ) );
  InfoNES_SetupChr();
}

/*-------------------------------------------------------------------*/
/*  Mapper 25 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map25_Write( WORD wAddr, BYTE byData )
{
  /* VRC4b/VRC4d/VRC2c differ only in which address bits select the
   * sub-register within each $Xxxx block: VRC4b uses A0/A1, VRC4d uses
   * A2/A3, and in both the two lines are swapped with respect to
   * mapper 23. Canonicalize by OR-ing (A0|A2) and (A1|A3) and then
   * swapping them, so a single decode serves every variant. The other
   * address lines are not decoded by the mapper, so writes such as
   * $8FFF (Teenage Mutant Ninja Turtles) land on the right register. */
  wAddr = ( wAddr & 0xF000 )
        | ( ( ( ( wAddr >> 2 ) | wAddr ) & 0x0001 ) << 1 )
        | ( ( ( ( wAddr >> 2 ) | wAddr ) & 0x0002 ) >> 1 );

  switch ( wAddr )
  {
    /* All four sub-registers of the $8000 and $A000 blocks select the
     * same PRG bank. */
    case 0x8000:
    case 0x8001:
    case 0x8002:
    case 0x8003:
      Map25_Prg0 = byData % ( NesHeader.byRomSize << 1 );
      Map25_Set_Prg();
      break;

    /* Name Table Mirroring. Sub-register 1 is a hardware mirror of 0. */
    case 0x9000:
    case 0x9001:
      switch ( byData & 0x03 )
      {
        case 0x00:
          InfoNES_Mirroring( 1 );   /* Vertical */
          break;
        case 0x01:
          InfoNES_Mirroring( 0 );   /* Horizontal */
          break;
        case 0x02:
          InfoNES_Mirroring( 3 );   /* One Screen 0x2000 */
          break;
        case 0x03:
          InfoNES_Mirroring( 2 );   /* One Screen 0x2400 */
          break;
      }
      break;

    /* PRG swap mode ( bit 1 ). Bit 0 is the WRAM enable, ignored here. */
    case 0x9002:
    case 0x9003:
      Map25_Regs[ 8 ] = byData & 0x02;
      Map25_Set_Prg();
      break;

    case 0xa000:
    case 0xa001:
    case 0xa002:
    case 0xa003:
      byData %= ( NesHeader.byRomSize << 1 );
      ROMBANK1 = ROMPAGE( byData );
      break;

    /* CHR bank registers: each 1KB bank takes its low nibble from
     * sub-register 0 or 2 and its high nibble from sub-register 1 or 3. */
    case 0xb000:
    case 0xb002:
    case 0xc000:
    case 0xc002:
    case 0xd000:
    case 0xd002:
    case 0xe000:
    case 0xe002:
    {
      int nBank = ( ( ( wAddr >> 12 ) - 0x0b ) << 1 ) | ( ( wAddr >> 1 ) & 0x01 );
      Map25_Regs[ nBank ] = ( Map25_Regs[ nBank ] & 0xf0 ) | ( byData & 0x0f );
      Map25_Set_Chr( nBank );
      break;
    }

    case 0xb001:
    case 0xb003:
    case 0xc001:
    case 0xc003:
    case 0xd001:
    case 0xd003:
    case 0xe001:
    case 0xe003:
    {
      int nBank = ( ( ( wAddr >> 12 ) - 0x0b ) << 1 ) | ( ( wAddr >> 1 ) & 0x01 );
      Map25_Regs[ nBank ] = ( Map25_Regs[ nBank ] & 0x0f ) | ( ( byData & 0x0f ) << 4 );
      Map25_Set_Chr( nBank );
      break;
    }

    /* IRQ latch, low and high nibble */
    case 0xf000:
      Map25_IRQ_Latch = ( Map25_IRQ_Latch & 0xf0 ) | ( byData & 0x0f );
      break;

    case 0xf001:
      Map25_IRQ_Latch = ( Map25_IRQ_Latch & 0x0f ) | ( ( byData & 0x0f ) << 4 );
      break;

    /* IRQ control */
    case 0xf002:
      Map25_IRQ_Enable = byData & 0x03;
      if ( Map25_IRQ_Enable & 0x02 )
      {
        Map25_IRQ_Cnt = Map25_IRQ_Latch;
      }
      break;

    /* IRQ acknowledge */
    case 0xf003:
      if ( Map25_IRQ_Enable & 0x01 )
      {
        Map25_IRQ_Enable |= 0x02;
      } else {
        Map25_IRQ_Enable &= 0x01;
      }
      break;
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 25 H-Sync Function                                        */
/*-------------------------------------------------------------------*/
void Map25_HSync()
{
/*
 *  Callback at HSync
 *
 */
  if ( Map25_IRQ_Enable & 0x02 )
  {
    if ( Map25_IRQ_Cnt == 0xff )
    {
      IRQ_REQ;

      Map25_IRQ_Cnt = Map25_IRQ_Latch;
      if ( Map25_IRQ_Enable & 0x01 )
      {
        Map25_IRQ_Enable |= 0x02;
      } else {
        Map25_IRQ_Enable &= 0x01;
      }
    } else {
      Map25_IRQ_Cnt++;
    }
  }
}
