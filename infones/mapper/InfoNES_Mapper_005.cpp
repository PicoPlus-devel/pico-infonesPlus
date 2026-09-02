/*===================================================================*/
/*                                                                   */
/*                        Mapper 5 (MMC5)                            */
/*                                                                   */
/*===================================================================*/


BYTE *Map5_Wram;
BYTE *Map5_Ex_Vram;
BYTE *Map5_Ex_Nam;
BYTE (*mmc5_wave_buffers)[APU_MAX_SAMPLES_PER_SYNC];
BYTE Map5_Prg_Reg[ 8 ];
BYTE Map5_Wram_Reg[ 8 ];
BYTE Map5_Chr_Reg[ 8 ][ 2 ];

BYTE Map5_IRQ_Enable;
BYTE Map5_IRQ_Status;
BYTE Map5_IRQ_Line;

DWORD Map5_Value0;
DWORD Map5_Value1;

BYTE Map5_Wram_Protect0;
BYTE Map5_Wram_Protect1;
BYTE Map5_Prg_Size;
BYTE Map5_Chr_Size;
BYTE Map5_Gfx_Mode;
BYTE Map5_Chr_Upper;
/* Which of the two CHR bank sets was written last: 0 = the sprite ("A") set at
   $5120-$5127, 1 = the background ("B") set at $5128-$512B. With 8x16 sprites
   the PPU uses A for sprite fetches and B for background fetches, but with 8x8
   sprites it uses this one set for both. */
BYTE Map5_Chr_Last_Set;

/* Forward declarations */
void Map5_Sram( WORD wAddr, BYTE byData );
void Map5_Sync_Prg_Banks( void );

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 5                                              */
/*-------------------------------------------------------------------*/
void Map5_Init()
{
  int nPage;

  /* Initialize Mapper */
  MapperInit = Map5_Init;

  /* Write to Mapper */
  MapperWrite = Map5_Write;

  /* Write to SRAM */
  MapperSram = Map5_Sram;

  /* Write to APU */
  MapperApu = Map5_Apu;

  /* Read from APU */
  MapperReadApu = Map5_ReadApu;

  /* Callback at VSync */
  MapperVSync = Map0_VSync;

  /* Callback at HSync */
  MapperHSync = Map5_HSync;

  /* Callback at PPU */
  MapperPPU = Map0_PPU;

  /* Callback at Rendering Screen ( 1:BG, 0:Sprite ) */
  MapperRenderScreen = Map5_RenderScreen;

  /* Set SRAM Banks */
  SRAMBANK = SRAM;

  /* Set ROM Banks */
  ROMBANK0 = ROMLASTPAGE( 0 );
  ROMBANK1 = ROMLASTPAGE( 0 );
  ROMBANK2 = ROMLASTPAGE( 0 );
  ROMBANK3 = ROMLASTPAGE( 0 );

  /* Set PPU Banks */
  for ( nPage = 0; nPage < 8; ++nPage )
    PPUBANK[ nPage ] = VROMPAGE( nPage );
  InfoNES_SetupChr();

  /* Initialize State Registers */
  for ( nPage = 4; nPage < 8; ++nPage )
  {
    Map5_Prg_Reg[ nPage ] = ( NesHeader.byRomSize << 1 ) - 1;
    Map5_Wram_Reg[ nPage ] = 0xff;
  }
  Map5_Wram_Reg[ 3 ] = 0xff;

  /* All eight registers of both sets, not just 4-7: in the 1K banking mode the
     mapper defaults to, the lower half of each pattern table would otherwise
     read page 0 until the game programs it. This leaves both sets as the
     identity mapping the PPUBANK loop above just installed. */
  for ( BYTE byPage = 0; byPage < 8; ++byPage )
  {
    Map5_Chr_Reg[ byPage ][ 0 ] = byPage;
    Map5_Chr_Reg[ byPage ][ 1 ] = byPage;
  }
  Map5_Chr_Last_Set = 1;

  Map5_Wram = (BYTE *)Frens::f_malloc(0x2000 * 8);
  Map5_Ex_Vram = (BYTE *)Frens::f_malloc(0x400);
  Map5_Ex_Nam = (BYTE *)Frens::f_malloc(0x400);

  InfoNES_MemorySet( Map5_Wram, 0x00, 0x2000 * 8 );
  InfoNES_MemorySet( Map5_Ex_Vram, 0x00, 0x400 );
  InfoNES_MemorySet( Map5_Ex_Nam, 0x00, 0x400 );

  Map5_Prg_Size = 3;
  Map5_Wram_Protect0 = 0;
  Map5_Wram_Protect1 = 0;
  Map5_Chr_Size = 3;
  Map5_Gfx_Mode = 0;
  Map5_Chr_Upper = 0;

  Map5_IRQ_Enable = 0;
  Map5_IRQ_Status = 0;
  Map5_IRQ_Line = 0;

  /* Disable Frame IRQ - MMC5 has its own IRQ mechanism */
  FrameIRQ_Enable = 0;

  /* Enable MMC5 expansion audio */
  ApuMmc5Enable = 1;

  /* Allocate MMC5 wave buffers */
  mmc5_wave_buffers = (BYTE (*)[APU_MAX_SAMPLES_PER_SYNC])Frens::f_malloc(3 * APU_MAX_SAMPLES_PER_SYNC);
  InfoNES_MemorySet((void *)mmc5_wave_buffers[0], 0, APU_MAX_SAMPLES_PER_SYNC);
  InfoNES_MemorySet((void *)mmc5_wave_buffers[1], 0, APU_MAX_SAMPLES_PER_SYNC);
  InfoNES_MemorySet((void *)mmc5_wave_buffers[2], 0, APU_MAX_SAMPLES_PER_SYNC);

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 ); 
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 Read from APU Function                                  */
/*-------------------------------------------------------------------*/
BYTE Map5_ReadApu( WORD wAddr )
{
  BYTE byRet = (BYTE)( wAddr >> 8 );

  switch ( wAddr )
  {
    case 0x5204:
      byRet = Map5_IRQ_Status;
      Map5_IRQ_Status &= 0x40;
      IRQ_State = IRQ_Wiring;
      break;

    case 0x5205:
      byRet = (BYTE)( ( Map5_Value0 * Map5_Value1 ) & 0x00ff );
      break;

    case 0x5206:
      byRet = (BYTE)( ( ( Map5_Value0 * Map5_Value1 ) & 0xff00 ) >> 8 );
      break;

    case 0x5015:
      byRet = 0;
      if ( ApuMmc5P1Atl > 0 ) byRet |= 0x01;
      if ( ApuMmc5P2Atl > 0 ) byRet |= 0x02;
      break;

    default:
      /* The MMC5 has a single 1K ExRAM. $5104 only decides who may touch it:
         the CPU can read it back in modes 2 (read/write) and 3 (read-only),
         and gets open bus in the two modes where the PPU owns it. */
      if ( 0x5c00 <= wAddr && wAddr <= 0x5fff && Map5_Gfx_Mode >= 2 )
      {
        byRet = Map5_Ex_Vram[ wAddr - 0x5c00 ];
      }
      break;
  }
  return byRet;
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 Write to APU Function                                   */
/*-------------------------------------------------------------------*/
void Map5_Apu( WORD wAddr, BYTE byData )
{
  int nPage;

  switch ( wAddr )
  {
    case 0x5100:
      /* Re-lay the windows straight away - waiting for the next $5114-$5117
         write leaves the previous mode's layout mapped in the meantime. */
      Map5_Prg_Size = byData & 0x03;
      Map5_Sync_Prg_Banks();
      break;

    case 0x5101:
      Map5_Chr_Size = byData & 0x03;
      break;

    case 0x5102:
      Map5_Wram_Protect0 = byData & 0x03;
      break;

    case 0x5103:
      Map5_Wram_Protect1 = byData & 0x03;
      break;

    case 0x5104:
      Map5_Gfx_Mode = byData & 0x03;
      /* Extended-attribute mode (1) makes the background renderer index CHR
         ROM directly, modulo its size in 1K pages - another division by zero
         with no CHR ROM. Refuse the mode instead of guarding the per-tile
         fetch, which is far too hot to test. */
      if ( Map5_Gfx_Mode == 1 && NesHeader.byVRomSize == 0 )
        Map5_Gfx_Mode = 0;
      break;

    case 0x5130:
      Map5_Chr_Upper = byData & 0x03;
      break;

    case 0x5105:
      for ( nPage = 0; nPage < 4; nPage++ )
      {
        BYTE byNamReg;
        
        byNamReg = byData & 0x03;
        byData = byData >> 2;

        switch ( byNamReg )
        {
          case 0:
#if 1
            PPUBANK[ nPage + 8 ] = VRAMPAGE( 0 );
#else
            PPUBANK[ nPage + 8 ] = CRAMPAGE( 8 );
#endif
            break;
          case 1:
#if 1
            PPUBANK[ nPage + 8 ] = VRAMPAGE( 1 );
#else
            PPUBANK[ nPage + 8 ] = CRAMPAGE( 9 );
#endif
            break;
          case 2:
            PPUBANK[ nPage + 8 ] = Map5_Ex_Vram;
            break;
          case 3:
            PPUBANK[ nPage + 8 ] = Map5_Ex_Nam;
            break;
        }
      }
      break;

    case 0x5106:
      InfoNES_MemorySet( Map5_Ex_Nam, byData, 0x3c0 );
      break;

    case 0x5107:
      byData &= 0x03;
      byData = byData | ( byData << 2 ) | ( byData << 4 ) | ( byData << 6 );
      InfoNES_MemorySet( &( Map5_Ex_Nam[ 0x3c0 ] ), byData, 0x400 - 0x3c0 );
      break;

    case 0x5113:
      Map5_Wram_Reg[ 3 ] = byData & 0x07;
      SRAMBANK = Map5_ROMPAGE( byData & 0x07 );
      break;

    case 0x5114:
    case 0x5115:
    case 0x5116:
    case 0x5117:
      Map5_Prg_Reg[ wAddr & 0x07 ] = byData;
      Map5_Sync_Prg_Banks();
      break;

    case 0x5120:
    case 0x5121:
    case 0x5122:
    case 0x5123:
    case 0x5124:
    case 0x5125:
    case 0x5126:
    case 0x5127:
      Map5_Chr_Reg[ wAddr & 0x07 ][ 0 ] = byData;
      Map5_Chr_Last_Set = 0;
      break;

    case 0x5128:
    case 0x5129:
    case 0x512a:
    case 0x512b:
      Map5_Chr_Reg[ ( wAddr & 0x03 ) + 0 ][ 1 ] = byData;
      Map5_Chr_Reg[ ( wAddr & 0x03 ) + 4 ][ 1 ] = byData;
      Map5_Chr_Last_Set = 1;
      break;

    case 0x5200:
    case 0x5201:
    case 0x5202:
      /* Nothing to do */
      break;

    case 0x5203:
      Map5_IRQ_Line = byData;
      break;

    case 0x5204:
      Map5_IRQ_Enable = byData & 0x80;
      if ( Map5_IRQ_Enable && ( Map5_IRQ_Status & 0x80 ) )
      {
        IRQ_REQ;
      }
      break;

    case 0x5205:
      Map5_Value0 = byData;
      break;

    case 0x5206:
      Map5_Value1 = byData;
      break;

    default:
      if ( 0x5000 <= wAddr && wAddr <= 0x5015 )
      {
        /* MMC5 Expansion Audio */
        switch ( wAddr )
        {
          case 0x5000:
            ApuWriteMmc5P1a( wAddr, byData );
            break;
          case 0x5002:
            ApuWriteMmc5P1c( wAddr, byData );
            break;
          case 0x5003:
            ApuWriteMmc5P1d( wAddr, byData );
            break;
          case 0x5004:
            ApuWriteMmc5P2a( wAddr, byData );
            break;
          case 0x5006:
            ApuWriteMmc5P2c( wAddr, byData );
            break;
          case 0x5007:
            ApuWriteMmc5P2d( wAddr, byData );
            break;
          case 0x5011:
            /* Raw PCM - writing 0 has no effect */
            if ( byData != 0 )
            {
              ApuMmc5PcmValue = byData;
            }
            break;
          case 0x5015:
            ApuWriteMmc5Ctrl( wAddr, byData );
            break;
        }
      } else 
      if ( 0x5c00 <= wAddr && wAddr <= 0x5fff )
      {
        /* One buffer, as above. Mode 3 makes ExRAM read-only. */
        if ( Map5_Gfx_Mode != 3 )
        {
          Map5_Ex_Vram[ wAddr - 0x5c00 ] = byData;
        }
      }
      break;
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 Write to SRAM Function                                  */
/*-------------------------------------------------------------------*/
void Map5_Sram( WORD wAddr, BYTE byData )
{
  if ( Map5_Wram_Protect0 == 0x02 && Map5_Wram_Protect1 == 0x01 )
  {
    if ( Map5_Wram_Reg[ 3 ] != 0xff )
    {
      Map5_Wram[ 0x2000 * Map5_Wram_Reg[ 3 ] + ( wAddr - 0x6000) ] = byData;
    }
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 Write Function                                          */
/*-------------------------------------------------------------------*/
void Map5_Write( WORD wAddr, BYTE byData )
{
  if ( Map5_Wram_Protect0 == 0x02 && Map5_Wram_Protect1 == 0x01 )
  {
    switch ( wAddr & 0xe000 )
    {
      case 0x8000:      /* $8000-$9fff */
        if ( Map5_Wram_Reg[ 4 ] != 0xff )
        {
          Map5_Wram[ 0x2000 * Map5_Wram_Reg[ 4 ] + ( wAddr - 0x8000) ] = byData;
        }
        break;

      case 0xa000:      /* $a000-$bfff */
        if ( Map5_Wram_Reg[ 5 ] != 0xff )
        {
          Map5_Wram[ 0x2000 * Map5_Wram_Reg[ 5 ] + ( wAddr - 0xa000) ] = byData;
        }
        break;

      case 0xc000:      /* $c000-$dfff */
        if ( Map5_Wram_Reg[ 6 ] != 0xff )
        {
          Map5_Wram[ 0x2000 * Map5_Wram_Reg[ 6 ] + ( wAddr - 0xc000) ] = byData;
        }
        break;
    }
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 H-Sync Function                                         */
/*-------------------------------------------------------------------*/
void Map5_HSync()
{
  /* MMC5 has its own IRQ; prevent APU frame IRQ from interfering */
  FrameIRQ_Enable = 0;

  /* This runs in the h-blank between PPU_Scanline and the line after it, so
     the line the MMC5 scanline counter is about to count is PPU_Scanline + 1.
     Comparing PPU_Scanline itself raised the IRQ one scanline too late: the
     CPU only samples the IRQ line at the start of the next K6502_Step, so the
     handler began a full scanline (~114 cycles) after the hardware would have
     entered it. Castlevania III arms its last split near line 239 and relies
     on the handler finishing before the vblank NMI at line 241; with the lost
     scanline the NMI landed inside the split handler, which has already paged
     bank 2/3 in over $8000-$BFFF without updating the game's own bank shadow
     at $21. The NMI then restored $5115 from that stale shadow and returned
     into the middle of the split routine with the wrong bank mapped, so the
     CPU executed data. */
  WORD wLine = PPU_Scanline + 1;

  if ( wLine < 240 && PPU_ScanTable[ wLine ] == SCAN_ON_SCREEN )
  {
    /* In visible frame */
    Map5_IRQ_Status |= 0x40;

    if ( Map5_IRQ_Line != 0 && wLine == Map5_IRQ_Line )
    {
      Map5_IRQ_Status |= 0x80;

      if ( Map5_IRQ_Enable )
      {
        IRQ_REQ;
      }
    }
  }
  else if ( Map5_IRQ_Status & 0x40 )
  {
    /* Transition out of visible frame: clear in-frame flag, keep pending */
    Map5_IRQ_Status &= ~0x40;
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 Rendering Screen Function                               */
/*-------------------------------------------------------------------*/
void Map5_RenderScreen( BYTE byMode )
{
  DWORD dwPage[ 8 ];

  /* Every bank index below is reduced modulo the CHR ROM size in 1K pages,
     which is zero for a cartridge with no CHR ROM at all. A real MMC5 board
     always has CHR ROM, but a malformed header can still claim mapper 5 with
     0 CHR (Kkachi-wa Norae Chingu (Korea) does), and this runs once per
     scanline - so it divided by zero on every line. There is nothing to remap
     in that case: the CHR-RAM banks Map5_Init installed stay as they are. */
  if ( NesHeader.byVRomSize == 0 )
    return;

  /* byMode picks the bank set the way 8x16 sprites work on the real chip: the
     background pass fetches through the "B" registers, the sprite pass through
     "A". With 8x8 sprites there is only one set of fetches, and the chip uses
     whichever set was written last for both passes. Games that only ever write
     one set had their background drawn from registers they never programmed -
     Yakuman Tengoku's title screen and Genchou Hishi's map were both garbage. */
  const BYTE bySet = ( PPU_R0 & R0_SP_SIZE ) ? byMode : Map5_Chr_Last_Set;

  /* $5130 supplies the two bits above the 8 a bank register holds, which is
     what carries CHR larger than 256K in the finer banking modes. */
  #define Map5_CHR_BANK( n ) ( ( (DWORD)Map5_Chr_Upper << 8 ) | Map5_Chr_Reg[ n ][ bySet ] )

  switch ( Map5_Chr_Size )
  {
    case 0:
      dwPage[ 7 ] = ( Map5_CHR_BANK( 7 ) << 3 ) % ( NesHeader.byVRomSize << 3 );

      PPUBANK[ 0 ] = VROMPAGE( dwPage[ 7 ] + 0 );
      PPUBANK[ 1 ] = VROMPAGE( dwPage[ 7 ] + 1 );
      PPUBANK[ 2 ] = VROMPAGE( dwPage[ 7 ] + 2 );
      PPUBANK[ 3 ] = VROMPAGE( dwPage[ 7 ] + 3 );
      PPUBANK[ 4 ] = VROMPAGE( dwPage[ 7 ] + 4 );
      PPUBANK[ 5 ] = VROMPAGE( dwPage[ 7 ] + 5 );
      PPUBANK[ 6 ] = VROMPAGE( dwPage[ 7 ] + 6 );
      PPUBANK[ 7 ] = VROMPAGE( dwPage[ 7 ] + 7 );
      InfoNES_SetupChr();
      break;

    case 1:
      dwPage[ 3 ] = ( Map5_CHR_BANK( 3 ) << 2 ) % ( NesHeader.byVRomSize << 3 );
      dwPage[ 7 ] = ( Map5_CHR_BANK( 7 ) << 2 ) % ( NesHeader.byVRomSize << 3 );

      PPUBANK[ 0 ] = VROMPAGE( dwPage[ 3 ] + 0 );
      PPUBANK[ 1 ] = VROMPAGE( dwPage[ 3 ] + 1 );
      PPUBANK[ 2 ] = VROMPAGE( dwPage[ 3 ] + 2 );
      PPUBANK[ 3 ] = VROMPAGE( dwPage[ 3 ] + 3 );
      PPUBANK[ 4 ] = VROMPAGE( dwPage[ 7 ] + 0 );
      PPUBANK[ 5 ] = VROMPAGE( dwPage[ 7 ] + 1 );
      PPUBANK[ 6 ] = VROMPAGE( dwPage[ 7 ] + 2 );
      PPUBANK[ 7 ] = VROMPAGE( dwPage[ 7 ] + 3 );
     InfoNES_SetupChr();
      break;

    case 2:
      dwPage[ 1 ] = ( Map5_CHR_BANK( 1 ) << 1 ) % ( NesHeader.byVRomSize << 3 );
      dwPage[ 3 ] = ( Map5_CHR_BANK( 3 ) << 1 ) % ( NesHeader.byVRomSize << 3 );
      dwPage[ 5 ] = ( Map5_CHR_BANK( 5 ) << 1 ) % ( NesHeader.byVRomSize << 3 );
      dwPage[ 7 ] = ( Map5_CHR_BANK( 7 ) << 1 ) % ( NesHeader.byVRomSize << 3 );

      PPUBANK[ 0 ] = VROMPAGE( dwPage[ 1 ] + 0 );
      PPUBANK[ 1 ] = VROMPAGE( dwPage[ 1 ] + 1 );
      PPUBANK[ 2 ] = VROMPAGE( dwPage[ 3 ] + 0 );
      PPUBANK[ 3 ] = VROMPAGE( dwPage[ 3 ] + 1 );
      PPUBANK[ 4 ] = VROMPAGE( dwPage[ 5 ] + 0 );
      PPUBANK[ 5 ] = VROMPAGE( dwPage[ 5 ] + 1 );
      PPUBANK[ 6 ] = VROMPAGE( dwPage[ 7 ] + 0 );
      PPUBANK[ 7 ] = VROMPAGE( dwPage[ 7 ] + 1 );
      InfoNES_SetupChr();
      break;

    default:
      dwPage[ 0 ] = Map5_CHR_BANK( 0 ) % ( NesHeader.byVRomSize << 3 );
      dwPage[ 1 ] = Map5_CHR_BANK( 1 ) % ( NesHeader.byVRomSize << 3 );
      dwPage[ 2 ] = Map5_CHR_BANK( 2 ) % ( NesHeader.byVRomSize << 3 );
      dwPage[ 3 ] = Map5_CHR_BANK( 3 ) % ( NesHeader.byVRomSize << 3 );
      dwPage[ 4 ] = Map5_CHR_BANK( 4 ) % ( NesHeader.byVRomSize << 3 );
      dwPage[ 5 ] = Map5_CHR_BANK( 5 ) % ( NesHeader.byVRomSize << 3 );
      dwPage[ 6 ] = Map5_CHR_BANK( 6 ) % ( NesHeader.byVRomSize << 3 );
      dwPage[ 7 ] = Map5_CHR_BANK( 7 ) % ( NesHeader.byVRomSize << 3 );

      PPUBANK[ 0 ] = VROMPAGE( dwPage[ 0 ] );
      PPUBANK[ 1 ] = VROMPAGE( dwPage[ 1 ] );
      PPUBANK[ 2 ] = VROMPAGE( dwPage[ 2 ] );
      PPUBANK[ 3 ] = VROMPAGE( dwPage[ 3 ] );
      PPUBANK[ 4 ] = VROMPAGE( dwPage[ 4 ] );
      PPUBANK[ 5 ] = VROMPAGE( dwPage[ 5 ] );
      PPUBANK[ 6 ] = VROMPAGE( dwPage[ 6 ] );
      PPUBANK[ 7 ] = VROMPAGE( dwPage[ 7 ] );
      InfoNES_SetupChr();
      break;
  }

  #undef Map5_CHR_BANK
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 Sync Program Banks Function                             */
/*-------------------------------------------------------------------*/
void Map5_Sync_Prg_Banks( void )
{
  switch( Map5_Prg_Size )
  {
    case 0:
      Map5_Wram_Reg[ 4 ] = 0xff;
      Map5_Wram_Reg[ 5 ] = 0xff;
      Map5_Wram_Reg[ 6 ] = 0xff;

      ROMBANK0 = ROMPAGE( ( (Map5_Prg_Reg[7] & 0x7c) + 0 ) % ( NesHeader.byRomSize << 1 ) );
      ROMBANK1 = ROMPAGE( ( (Map5_Prg_Reg[7] & 0x7c) + 1 ) % ( NesHeader.byRomSize << 1 ) );
      ROMBANK2 = ROMPAGE( ( (Map5_Prg_Reg[7] & 0x7c) + 2 ) % ( NesHeader.byRomSize << 1 ) );
      ROMBANK3 = ROMPAGE( ( (Map5_Prg_Reg[7] & 0x7c) + 3 ) % ( NesHeader.byRomSize << 1 ) );
      break;

    case 1:
      if ( Map5_Prg_Reg[ 5 ] & 0x80 )
      {
        Map5_Wram_Reg[ 4 ] = 0xff;
        Map5_Wram_Reg[ 5 ] = 0xff;
        ROMBANK0 = ROMPAGE( ( (Map5_Prg_Reg[5] & 0x7e) + 0 ) % ( NesHeader.byRomSize << 1 ) );
        ROMBANK1 = ROMPAGE( ( (Map5_Prg_Reg[5] & 0x7e) + 1 ) % ( NesHeader.byRomSize << 1 ) );
      } else {
        Map5_Wram_Reg[ 4 ] = ( Map5_Prg_Reg[ 5 ] & 0x06 ) + 0;
        Map5_Wram_Reg[ 5 ] = ( Map5_Prg_Reg[ 5 ] & 0x06 ) + 1;
        ROMBANK0 = Map5_ROMPAGE( Map5_Wram_Reg[ 4 ] );
        ROMBANK1 = Map5_ROMPAGE( Map5_Wram_Reg[ 5 ] );
      }

      Map5_Wram_Reg[ 6 ] = 0xff;
      ROMBANK2 = ROMPAGE( ( (Map5_Prg_Reg[7] & 0x7e) + 0 ) % ( NesHeader.byRomSize << 1 ) );
      ROMBANK3 = ROMPAGE( ( (Map5_Prg_Reg[7] & 0x7e) + 1 ) % ( NesHeader.byRomSize << 1 ) );
      break;

    case 2:
      if ( Map5_Prg_Reg[ 5 ] & 0x80 )
      {
        Map5_Wram_Reg[ 4 ] = 0xff;
        Map5_Wram_Reg[ 5 ] = 0xff;
        ROMBANK0 = ROMPAGE( ( (Map5_Prg_Reg[5] & 0x7e) + 0 ) % ( NesHeader.byRomSize << 1 ) );
        ROMBANK1 = ROMPAGE( ( (Map5_Prg_Reg[5] & 0x7e) + 1 ) % ( NesHeader.byRomSize << 1 ) );
      } else {
        Map5_Wram_Reg[ 4 ] = ( Map5_Prg_Reg[ 5 ] & 0x06 ) + 0;
        Map5_Wram_Reg[ 5 ] = ( Map5_Prg_Reg[ 5 ] & 0x06 ) + 1;
        ROMBANK0 = Map5_ROMPAGE( Map5_Wram_Reg[ 4 ] );
        ROMBANK1 = Map5_ROMPAGE( Map5_Wram_Reg[ 5 ] );
      }

      if ( Map5_Prg_Reg[ 6 ] & 0x80 )
      {
        Map5_Wram_Reg[ 6 ] = 0xff;
        ROMBANK2 = ROMPAGE( (Map5_Prg_Reg[6] & 0x7f) % ( NesHeader.byRomSize << 1 ) );
      } else {
        Map5_Wram_Reg[ 6 ] = Map5_Prg_Reg[ 6 ] & 0x07;
        ROMBANK2 = Map5_ROMPAGE( Map5_Wram_Reg[ 6 ] );
      }

      ROMBANK3 = ROMPAGE( (Map5_Prg_Reg[7] & 0x7f) % ( NesHeader.byRomSize << 1 ) );
      break;

    default:
      if ( Map5_Prg_Reg[ 4 ] & 0x80 )
      {
        Map5_Wram_Reg[ 4 ] = 0xff;
        ROMBANK0 = ROMPAGE( (Map5_Prg_Reg[4] & 0x7f) % ( NesHeader.byRomSize << 1 ) );
      } else {
        Map5_Wram_Reg[ 4 ] = Map5_Prg_Reg[ 4 ] & 0x07;
        ROMBANK0 = Map5_ROMPAGE( Map5_Wram_Reg[ 4 ] );
      }

      if ( Map5_Prg_Reg[ 5 ] & 0x80 )
      {
        Map5_Wram_Reg[ 5 ] = 0xff;
        ROMBANK1 = ROMPAGE( (Map5_Prg_Reg[5] & 0x7f) % ( NesHeader.byRomSize << 1 ) );
      } else {
        Map5_Wram_Reg[ 5 ] = Map5_Prg_Reg[ 5 ] & 0x07;
        ROMBANK1 = Map5_ROMPAGE( Map5_Wram_Reg[ 5 ] );
      }

      if ( Map5_Prg_Reg[ 6 ] & 0x80 )
      {
        Map5_Wram_Reg[ 6 ] = 0xff;
        ROMBANK2 = ROMPAGE( (Map5_Prg_Reg[6] & 0x7f) % ( NesHeader.byRomSize << 1 ) );
      } else {
        Map5_Wram_Reg[ 6 ] = Map5_Prg_Reg[ 6 ] & 0x07;
        ROMBANK2 = Map5_ROMPAGE( Map5_Wram_Reg[ 6 ] );
      }

      ROMBANK3 = ROMPAGE( (Map5_Prg_Reg[7] & 0x7f) % ( NesHeader.byRomSize << 1 ) );
      break;
  }
}
