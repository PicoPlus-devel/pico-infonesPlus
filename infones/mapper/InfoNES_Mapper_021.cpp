/*===================================================================*/
/*                                                                   */
/*               Mapper 21 (Konami VRC4 type A/C)                    */
/*                                                                   */
/*===================================================================*/

/* Map21_Regs[ 0..7 ] : the eight 1KB CHR bank registers                */
/* Map21_Regs[ 8 ]    : PRG swap mode ( $9002 bit 1 )                   */
BYTE Map21_Regs[ 9 ];

/* Last value written to the $8000 PRG register                        */
BYTE Map21_Prg0;

BYTE Map21_IRQ_Enable;
BYTE Map21_IRQ_Cnt;
BYTE Map21_IRQ_Latch;

/* The IRQ counter is clocked by a prescaler that runs a scanline's worth of
 * CPU cycles, and arming the IRQ restarts it, so the first clock comes one
 * scanline after the write. The counter here is clocked at the end of each
 * line; when the write was in the second half of the line, the end of the
 * next line is the closer match and the current one is skipped. */
BYTE Map21_IRQ_Skip;
uint32_t Map21_LineStart;

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 21                                             */
/*-------------------------------------------------------------------*/
void Map21_Init()
{
  /* Initialize Mapper */
  MapperInit = Map21_Init;

  /* Write to Mapper */
  MapperWrite = Map21_Write;

  /* Write to SRAM */
  MapperSram = Map0_Sram;

  /* Write to APU */
  MapperApu = Map0_Apu;

  /* Read from APU */
  MapperReadApu = Map0_ReadApu;

  /* Callback at VSync */
  MapperVSync = Map0_VSync;

  /* Callback at HSync */
  MapperHSync = Map21_HSync;

  /* Callback at PPU */
  MapperPPU = Map0_PPU;

  /* Callback at Rendering Screen ( 1:BG, 0:Sprite ) */
  MapperRenderScreen = Map0_RenderScreen;

  /* Set SRAM Banks */
  SRAMBANK = SRAM;

  /* Set ROM Banks */
  Map21_Regs[ 8 ] = 0;
  Map21_Prg0 = 0;
  Map21_Set_Prg();
  ROMBANK1 = ROMPAGE( 1 );
  ROMBANK3 = ROMLASTPAGE( 0 );

  /* Set PPU Banks */
  for ( int nPage = 0; nPage < 8; ++nPage )
    Map21_Regs[ nPage ] = nPage;

  if ( NesHeader.byVRomSize > 0 )
  {
    for ( int nPage = 0; nPage < 8; ++nPage )
      PPUBANK[ nPage ] = VROMPAGE( nPage );
    InfoNES_SetupChr();
  }

  /* Initialize IRQ Registers */
  Map21_IRQ_Enable = 0;
  Map21_IRQ_Cnt = 0;
  Map21_IRQ_Latch = 0;
  Map21_IRQ_Skip = 0;
  Map21_LineStart = K6502_Now();

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 );
}

/*-------------------------------------------------------------------*/
/*  Mapper 21 Set PRG Banks Function                                 */
/*-------------------------------------------------------------------*/
void Map21_Set_Prg()
{
  /* $9002 bit 1 swaps the switchable $8000 bank with the fixed
   * second-to-last bank at $C000. */
  if ( Map21_Regs[ 8 ] )
  {
    ROMBANK0 = ROMLASTPAGE( 1 );
    ROMBANK2 = ROMPAGE( Map21_Prg0 );
  } else {
    ROMBANK0 = ROMPAGE( Map21_Prg0 );
    ROMBANK2 = ROMLASTPAGE( 1 );
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 21 Set CHR Bank Function                                  */
/*-------------------------------------------------------------------*/
void Map21_Set_Chr( int nBank )
{
  if ( NesHeader.byVRomSize == 0 )
    return;

  PPUBANK[ nBank ] = VROMPAGE( Map21_Regs[ nBank ] % ( NesHeader.byVRomSize << 3 ) );
  InfoNES_SetupChr();
}

/*-------------------------------------------------------------------*/
/*  Mapper 21 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map21_Write( WORD wAddr, BYTE byData )
{
  /* VRC4a and VRC4c differ only in which address bits select the
   * sub-register within each $Xxxx block: VRC4a uses A1/A2 ($x002,
   * $x004, $x006, Wai Wai World 2), VRC4c uses A6/A7 ($x040, $x080,
   * $x0C0, Ganbare Goemon Gaiden 2). Canonicalize by OR-ing (A1|A6)
   * and (A2|A7), so a single decode serves both variants. The other
   * address lines, A0 included, are not decoded by the mapper. */
  wAddr = ( wAddr & 0xF000 ) | ( ( ( wAddr >> 1 ) | ( wAddr >> 6 ) ) & 0x0003 );

  switch ( wAddr )
  {
    /* All four sub-registers of the $8000 and $A000 blocks select the
     * same PRG bank. */
    case 0x8000:
    case 0x8001:
    case 0x8002:
    case 0x8003:
      Map21_Prg0 = byData % ( NesHeader.byRomSize << 1 );
      Map21_Set_Prg();
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
      Map21_Regs[ 8 ] = byData & 0x02;
      Map21_Set_Prg();
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
      Map21_Regs[ nBank ] = ( Map21_Regs[ nBank ] & 0xf0 ) | ( byData & 0x0f );
      Map21_Set_Chr( nBank );
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
      Map21_Regs[ nBank ] = ( Map21_Regs[ nBank ] & 0x0f ) | ( ( byData & 0x0f ) << 4 );
      Map21_Set_Chr( nBank );
      break;
    }

    /* IRQ latch, low and high nibble */
    case 0xf000:
      Map21_IRQ_Latch = ( Map21_IRQ_Latch & 0xf0 ) | ( byData & 0x0f );
      break;

    case 0xf001:
      Map21_IRQ_Latch = ( Map21_IRQ_Latch & 0x0f ) | ( ( byData & 0x0f ) << 4 );
      break;

    /* IRQ control */
    case 0xf002:
      Map21_IRQ_Enable = byData & 0x03;
      if ( Map21_IRQ_Enable & 0x02 )
      {
        Map21_IRQ_Cnt = Map21_IRQ_Latch;
        Map21_IRQ_Skip = ( K6502_Now() - Map21_LineStart ) >= STEP_PER_SCANLINE / 2U;
      }
      break;

    /* IRQ acknowledge */
    case 0xf003:
      if ( Map21_IRQ_Enable & 0x01 )
      {
        Map21_IRQ_Enable |= 0x02;
      } else {
        Map21_IRQ_Enable &= 0x01;
      }
      break;
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 21 H-Sync Function                                        */
/*-------------------------------------------------------------------*/
void Map21_HSync()
{
/*
 *  Callback at HSync
 *
 */
  Map21_LineStart = K6502_Now();

  if ( Map21_IRQ_Skip )
  {
    Map21_IRQ_Skip = 0;
  }
  else if ( Map21_IRQ_Enable & 0x02 )
  {
    if ( Map21_IRQ_Cnt == 0xff )
    {
      Map21_IRQ_Cnt = Map21_IRQ_Latch;

      if ( Map21_IRQ_Enable & 0x01 )
      {
        Map21_IRQ_Enable |= 0x02;
      } else {
        Map21_IRQ_Enable &= 0x01;
      }
      IRQ_REQ;
    } else {
      Map21_IRQ_Cnt++;
    }
  }
}
