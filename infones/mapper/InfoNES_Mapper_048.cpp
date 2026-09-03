/*===================================================================*/
/*                                                                   */
/*                   Mapper 48 (Taito TC0690)                        */
/*                                                                   */
/*===================================================================*/

BYTE Map48_IRQ_Enable;
BYTE Map48_IRQ_Cnt;
BYTE Map48_IRQ_Latch;
BYTE Map48_IRQ_Request;
BYTE Map48_IRQ_Reload;

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 48                                             */
/*-------------------------------------------------------------------*/
void Map48_Init()
{
  /* Initialize Mapper */
  MapperInit = Map48_Init;

  /* Write to Mapper */
  MapperWrite = Map48_Write;

  /* Write to SRAM */
  MapperSram = Map0_Sram;

  /* Write to APU */
  MapperApu = Map0_Apu;

  /* Read from APU */
  MapperReadApu = Map0_ReadApu;

  /* Callback at VSync */
  MapperVSync = Map0_VSync;

  /* Callback at HSync */
  MapperHSync = Map48_HSync;

  /* Callback at PPU */
  MapperPPU = Map0_PPU;

  /* Callback at Rendering Screen ( 1:BG, 0:Sprite ) */
  MapperRenderScreen = Map0_RenderScreen;

  /* Set SRAM Banks */
  SRAMBANK = SRAM;

  /* Set ROM Banks */
  ROMBANK0 = ROMPAGE( 0 );
  ROMBANK1 = ROMPAGE( 1 );
  ROMBANK2 = ROMLASTPAGE( 1 );
  ROMBANK3 = ROMLASTPAGE( 0 );

  /* Set PPU Banks */
  if ( NesHeader.byVRomSize > 0 )
  {
    for ( int nPage = 0; nPage < 8; ++nPage )
      PPUBANK[ nPage ] = VROMPAGE( nPage );
    InfoNES_SetupChr();
  }

  /* Initialize IRQ Registers */
  Map48_IRQ_Enable = 0;
  Map48_IRQ_Cnt = 0;
  Map48_IRQ_Latch = 0;
  Map48_IRQ_Request = 0;
  Map48_IRQ_Reload = 0;

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 ); 
}

/*-------------------------------------------------------------------*/
/*  Mapper 48 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map48_Write( WORD wAddr, BYTE byData )
{
  /* The TC0690 only decodes A0, A1, A13 and A14 */
  switch ( wAddr & 0xe003 )
  {
    /* Set ROM Banks */
    case 0x8000:
      ROMBANK0 = ROMPAGE( byData % ( NesHeader.byRomSize << 1 ) );
      break;

    case 0x8001:
      ROMBANK1 = ROMPAGE( byData % ( NesHeader.byRomSize << 1 ) );
      break;

    /* Set PPU Banks : $8002/$8003 select 2K pages, $a000-$a003 select 1K */
    case 0x8002:
      PPUBANK[ 0 ] = VROMPAGE( ( byData * 2 + 0 ) % ( NesHeader.byVRomSize << 3 ) );
      PPUBANK[ 1 ] = VROMPAGE( ( byData * 2 + 1 ) % ( NesHeader.byVRomSize << 3 ) );
      InfoNES_SetupChr();
      break;

    case 0x8003:
      PPUBANK[ 2 ] = VROMPAGE( ( byData * 2 + 0 ) % ( NesHeader.byVRomSize << 3 ) );
      PPUBANK[ 3 ] = VROMPAGE( ( byData * 2 + 1 ) % ( NesHeader.byVRomSize << 3 ) );
      InfoNES_SetupChr();
      break;

    case 0xa000:
      PPUBANK[ 4 ] = VROMPAGE( byData % ( NesHeader.byVRomSize << 3 ) );
      InfoNES_SetupChr();
      break;

    case 0xa001:
      PPUBANK[ 5 ] = VROMPAGE( byData % ( NesHeader.byVRomSize << 3 ) );
      InfoNES_SetupChr();
      break;

    case 0xa002:
      PPUBANK[ 6 ] = VROMPAGE( byData % ( NesHeader.byVRomSize << 3 ) );
      InfoNES_SetupChr();
      break;

    case 0xa003:
      PPUBANK[ 7 ] = VROMPAGE( byData % ( NesHeader.byVRomSize << 3 ) );
      InfoNES_SetupChr();
      break;

    /* Scanline IRQ, clocked and reloaded like the MMC3 counter.           */
    /* The reload value reaches $c000 inverted.                            */
    case 0xc000:
      Map48_IRQ_Latch = byData ^ 0xff;
      Map48_IRQ_Request = 0;
      IRQ_State = IRQ_Wiring;
      break;

    case 0xc001:
      Map48_IRQ_Reload = 0xff;
      Map48_IRQ_Request = 0;
      IRQ_State = IRQ_Wiring;
      break;

    case 0xc002:
      Map48_IRQ_Enable = 1;
      break;

    case 0xc003:
      Map48_IRQ_Enable = 0;
      Map48_IRQ_Request = 0;
      IRQ_State = IRQ_Wiring;
      break;

    /* Name Table Mirroring */
    case 0xe000:
      if ( byData & 0x40 )
      {
        InfoNES_Mirroring( 0 );
      } else {
        InfoNES_Mirroring( 1 );
      }
      break;
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 48 H-Sync Function                                        */
/*-------------------------------------------------------------------*/
void Map48_HSync()
{
/*
 *  Callback at HSync
 *
 */
  if ( ( 0 <= PPU_Scanline && PPU_Scanline <= 239 ) &&
       ( PPU_R1 & R1_SHOW_SCR || PPU_R1 & R1_SHOW_SP ) )
  {
    if ( Map48_IRQ_Reload )
    {
      Map48_IRQ_Cnt = Map48_IRQ_Latch;
      Map48_IRQ_Reload = 0;
    } else if ( Map48_IRQ_Cnt > 0 ) {
      Map48_IRQ_Cnt--;
    }

    if ( Map48_IRQ_Cnt == 0 )
    {
      if ( Map48_IRQ_Enable )
      {
        Map48_IRQ_Request = 0xff;
      }
      Map48_IRQ_Reload = 0xff;
    }
  }
  if ( Map48_IRQ_Request )
  {
    IRQ_REQ;
  }
}
