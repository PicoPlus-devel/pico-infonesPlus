/*===================================================================*/
/*                                                                   */
/*                        Mapper 5 (MMC5)                            */
/*                                                                   */
/*===================================================================*/


/* 32KB is the most PRG RAM any licensed MMC5 board carries (EWROM). */
#define MAP5_WRAM_SIZE 0x8000

/* PRG+CHR CRC32s of the MMC5 games on two-chip (2 x 8KB) boards such as
   ETROM, from the Mesen game database. */
static const uint32_t Map5_Two_Chip_Crcs[] = {
  0x15FE6D0F, 0x1CED086F, 0x39F2CE4B, 0x6396B988, 0x8CE478DB, 0x9C18762B,
  0xACA15643, 0xE5584D9F, 0xEEE9A682, 0xF9B4240F, 0xFDC7C50B, 0xFE3488D1,
};

BYTE *Map5_Wram;
/* The 8KB page of Map5_Wram each of the eight RAM bank numbers selects */
BYTE Map5_Wram_Page[ 8 ];
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
/* The last $5105 value, and whether the game has written one yet. Kept so a
   loaded state can put the name table mapping back. */
BYTE Map5_Nt_Reg;
BYTE Map5_Nt_Set;

/* PPU bank pointers for the two CHR register sets (0 = "A", 1 = "B"), and the
   64 4K banks extended attribute mode can pick under the current $5130. Both
   are rebuilt when the registers behind them are written, so the per-scanline
   and per-tile paths never divide - the RP2040 has no divide instruction. */
BYTE *Map5_Chr_Bank[ 2 ][ 8 ];
BYTE *Map5_Ex_Chr_Bank[ 64 ];

/* Forward declarations */
void Map5_Sram( WORD wAddr, BYTE byData );
void Map5_Sync_Prg_Banks( void );
static void Map5_Sync_Chr_Set( BYTE bySet );
static void Map5_Sync_Ex_Chr_Banks( void );
static void Map5_Sync_Nametables( void );
static int Map5_BlobSize( void );
static void Map5_SaveBlob( BYTE *pBuf );
static void Map5_LoadBlob( BYTE *pBuf );

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

  /* Set PPU Banks. A real MMC5 board always has CHR ROM, but a malformed
     header can claim mapper 5 with none (Kkachi-wa Norae Chingu (Korea)
     does). VROM is then null and VROMPAGE() would hand the PPU eight
     pointers into address 0 - a hard fault on the device the moment anything
     reads a pattern. Fall back to the 8KB of CHR RAM the core keeps at the
     bottom of PPURAM, which is what Map5_SetBank_CPPU already assumes when
     it returns early for a CHR-RAM cartridge. */
  for ( nPage = 0; nPage < 8; ++nPage )
    PPUBANK[ nPage ] = ( NesHeader.byVRomSize > 0 ) ? VROMPAGE( nPage )
                                                    : CRAMPAGE( nPage );
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

  /* Bank numbers 0-7 each select 8KB of PRG RAM, but boards wire them
     differently: two-chip boards use bank bit 2 to pick the chip and ignore
     bits 0-1, while single-chip boards (8KB or 32KB) use bits 0-1. An iNES
     1.0 header carries no RAM size, so the two-chip boards are recognised by
     CRC; everything else gets the 32KB layout. */
  bool bTwoChips = false;
  for ( uint32_t dwCrc : Map5_Two_Chip_Crcs )
    if ( dwCrc == InfoNES_RomCrc )
      bTwoChips = true;
  for ( BYTE byBank = 0; byBank < 8; ++byBank )
    Map5_Wram_Page[ byBank ] = bTwoChips ? ( byBank >> 2 ) : ( byBank & 0x03 );

  Map5_Wram = (BYTE *)Frens::f_malloc(MAP5_WRAM_SIZE);
  Map5_Ex_Vram = (BYTE *)Frens::f_malloc(0x400);
  Map5_Ex_Nam = (BYTE *)Frens::f_malloc(0x400);

  InfoNES_MemorySet( Map5_Wram, 0x00, MAP5_WRAM_SIZE );
  InfoNES_MemorySet( Map5_Ex_Vram, 0x00, 0x400 );
  InfoNES_MemorySet( Map5_Ex_Nam, 0x00, 0x400 );

  /* Save state and battery hooks (cleared on every reset, so install them here) */
  MapperBlobSize = Map5_BlobSize;
  MapperSaveBlob = Map5_SaveBlob;
  MapperLoadBlob = Map5_LoadBlob;
  MapperPrgRam = Map5_Wram;
  MapperPrgRamSize = MAP5_WRAM_SIZE;

  Map5_Prg_Size = 3;
  Map5_Wram_Protect0 = 0;
  Map5_Wram_Protect1 = 0;
  Map5_Chr_Size = 3;
  Map5_Gfx_Mode = 0;
  Map5_Chr_Upper = 0;
  Map5_Sync_Chr_Set( 0 );
  Map5_Sync_Chr_Set( 1 );
  Map5_Sync_Ex_Chr_Banks();
  Map5_Nt_Reg = 0;
  Map5_Nt_Set = 0;

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
      Map5_Sync_Chr_Set( 0 );
      Map5_Sync_Chr_Set( 1 );
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
      if ( Map5_Chr_Upper != ( byData & 0x03 ) )
      {
        Map5_Chr_Upper = byData & 0x03;
        Map5_Sync_Chr_Set( 0 );
        Map5_Sync_Chr_Set( 1 );
        Map5_Sync_Ex_Chr_Banks();
      }
      break;

    case 0x5105:
      Map5_Nt_Reg = byData;
      Map5_Nt_Set = 1;
      Map5_Sync_Nametables();
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
      Map5_Sync_Chr_Set( 0 );
      break;

    case 0x5128:
    case 0x5129:
    case 0x512a:
    case 0x512b:
      Map5_Chr_Reg[ ( wAddr & 0x03 ) + 0 ][ 1 ] = byData;
      Map5_Chr_Reg[ ( wAddr & 0x03 ) + 4 ][ 1 ] = byData;
      Map5_Chr_Last_Set = 1;
      Map5_Sync_Chr_Set( 1 );
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
      Map5_ROMPAGE( Map5_Wram_Reg[ 3 ] )[ wAddr - 0x6000 ] = byData;
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
          Map5_ROMPAGE( Map5_Wram_Reg[ 4 ] )[ wAddr - 0x8000 ] = byData;
          SRAMwritten = true;
        }
        break;

      case 0xa000:      /* $a000-$bfff */
        if ( Map5_Wram_Reg[ 5 ] != 0xff )
        {
          Map5_ROMPAGE( Map5_Wram_Reg[ 5 ] )[ wAddr - 0xa000 ] = byData;
          SRAMwritten = true;
        }
        break;

      case 0xc000:      /* $c000-$dfff */
        if ( Map5_Wram_Reg[ 6 ] != 0xff )
        {
          Map5_ROMPAGE( Map5_Wram_Reg[ 6 ] )[ wAddr - 0xc000 ] = byData;
          SRAMwritten = true;
        }
        break;
    }
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 H-Sync Function                                         */
/*-------------------------------------------------------------------*/
void __not_in_flash_func(Map5_HSync)()
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
void __not_in_flash_func(Map5_RenderScreen)( BYTE byMode )
{
  /* A real MMC5 board always has CHR ROM, but a malformed header can still
     claim mapper 5 with none (Kkachi-wa Norae Chingu (Korea) does). There is
     nothing to remap in that case: the CHR-RAM banks Map5_Init installed stay
     as they are. */
  if ( NesHeader.byVRomSize == 0 )
    return;

  /* byMode picks the bank set the way 8x16 sprites work on the real chip: the
     background pass fetches through the "B" registers, the sprite pass through
     "A". With 8x8 sprites there is only one set of fetches, and the chip uses
     whichever set was written last for both passes. Games that only ever write
     one set had their background drawn from registers they never programmed -
     Yakuman Tengoku's title screen and Genchou Hishi's map were both garbage. */
  const BYTE bySet = ( PPU_R0 & R0_SP_SIZE ) ? byMode : Map5_Chr_Last_Set;

  /* This runs twice per scanline, so the pointers are worked out when a CHR
     register is written (Map5_Sync_Chr_Set) and only copied here. */
  BYTE * const *ppBank = Map5_Chr_Bank[ bySet ];
  for ( int nPage = 0; nPage < 8; ++nPage )
    PPUBANK[ nPage ] = ppBank[ nPage ];
  InfoNES_SetupChr();
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 Sync Character Banks Functions                          */
/*-------------------------------------------------------------------*/
static void Map5_Sync_Chr_Set( BYTE bySet )
{
  /* Bank numbers are reduced modulo the CHR ROM size in 1K pages, which is
     zero with no CHR ROM at all (see Map5_RenderScreen). */
  const DWORD dwPages = NesHeader.byVRomSize << 3;
  if ( dwPages == 0 )
    return;

  BYTE **ppBank = Map5_Chr_Bank[ bySet ];

  /* $5130 supplies the two bits above the 8 a bank register holds, which is
     what carries CHR larger than 256K in the finer banking modes. */
  #define Map5_CHR_BANK( n ) ( ( (DWORD)Map5_Chr_Upper << 8 ) | Map5_Chr_Reg[ n ][ bySet ] )

  switch ( Map5_Chr_Size )
  {
    case 0:
    {
      const DWORD dwPage = ( Map5_CHR_BANK( 7 ) << 3 ) % dwPages;
      for ( int nPage = 0; nPage < 8; ++nPage )
        ppBank[ nPage ] = VROMPAGE( dwPage + nPage );
      break;
    }

    case 1:
    {
      const DWORD dwPage3 = ( Map5_CHR_BANK( 3 ) << 2 ) % dwPages;
      const DWORD dwPage7 = ( Map5_CHR_BANK( 7 ) << 2 ) % dwPages;
      for ( int nPage = 0; nPage < 4; ++nPage )
      {
        ppBank[ nPage + 0 ] = VROMPAGE( dwPage3 + nPage );
        ppBank[ nPage + 4 ] = VROMPAGE( dwPage7 + nPage );
      }
      break;
    }

    case 2:
      for ( int nPage = 0; nPage < 8; nPage += 2 )
      {
        const DWORD dwPage = ( Map5_CHR_BANK( nPage + 1 ) << 1 ) % dwPages;
        ppBank[ nPage + 0 ] = VROMPAGE( dwPage + 0 );
        ppBank[ nPage + 1 ] = VROMPAGE( dwPage + 1 );
      }
      break;

    default:
      for ( int nPage = 0; nPage < 8; ++nPage )
        ppBank[ nPage ] = VROMPAGE( Map5_CHR_BANK( nPage ) % dwPages );
      break;
  }

  #undef Map5_CHR_BANK
}

static void Map5_Sync_Ex_Chr_Banks( void )
{
  const DWORD dwPages = NesHeader.byVRomSize << 3;
  if ( dwPages == 0 )
    return;

  /* In extended attribute mode each tile's 4K bank is its ExRAM byte's low 6
     bits under the two $5130 bits. The page count is a multiple of 8 and a 4K
     bank starts on a multiple of 4, so the 1K page within it that the tile
     index adds (0-3) never needs reducing again. */
  for ( int nBank = 0; nBank < 64; ++nBank )
    Map5_Ex_Chr_Bank[ nBank ] =
      VROMPAGE( ( ( ( (DWORD)Map5_Chr_Upper << 6 ) | nBank ) << 2 ) % dwPages );
}

/*-------------------------------------------------------------------*/
/*  Mapper 5 Sync Name Tables Function                               */
/*-------------------------------------------------------------------*/
static void Map5_Sync_Nametables( void )
{
  /* $5105 gives each of the four name table slots a source, two bits each:
     CIRAM page 0 or 1, ExRAM, or the fill-mode table. */
  BYTE byNt = Map5_Nt_Reg;
  for ( int nPage = 0; nPage < 4; ++nPage, byNt >>= 2 )
  {
    switch ( byNt & 0x03 )
    {
      case 0:
        PPUBANK[ nPage + 8 ] = VRAMPAGE( 0 );
        break;
      case 1:
        PPUBANK[ nPage + 8 ] = VRAMPAGE( 1 );
        break;
      case 2:
        PPUBANK[ nPage + 8 ] = Map5_Ex_Vram;
        break;
      case 3:
        PPUBANK[ nPage + 8 ] = Map5_Ex_Nam;
        break;
    }
  }
}

/*-------------------------------------------------------------------*/
/*  Save state support                                               */
/*-------------------------------------------------------------------*/
/* The blob carries the registers, ExRAM and the fill-mode name table. The
   32KB of PRG RAM goes straight into the state file (MapperPrgRam), so it is
   not staged a second time here - an RP2040 has no room for that. Only bytes,
   so the layout has no padding. */
struct Map5_Blob
{
  BYTE Prg_Reg[ 8 ];
  BYTE Wram_Reg[ 8 ];
  BYTE Chr_Reg[ 8 ][ 2 ];
  BYTE IRQ_Enable, IRQ_Status, IRQ_Line;
  BYTE Value0, Value1;
  BYTE Wram_Protect0, Wram_Protect1;
  BYTE Prg_Size, Chr_Size, Gfx_Mode, Chr_Upper, Chr_Last_Set;
  BYTE Nt_Reg, Nt_Set;
  BYTE Ex_Vram[ 0x400 ];
  BYTE Ex_Nam[ 0x400 ];
};

static int Map5_BlobSize( void )
{
  return sizeof( Map5_Blob );
}

static void Map5_SaveBlob( BYTE *pBuf )
{
  Map5_Blob *pBlob = (Map5_Blob *)pBuf;

  InfoNES_MemoryCopy( pBlob->Prg_Reg, Map5_Prg_Reg, sizeof( Map5_Prg_Reg ) );
  InfoNES_MemoryCopy( pBlob->Wram_Reg, Map5_Wram_Reg, sizeof( Map5_Wram_Reg ) );
  InfoNES_MemoryCopy( pBlob->Chr_Reg, Map5_Chr_Reg, sizeof( Map5_Chr_Reg ) );
  pBlob->IRQ_Enable = Map5_IRQ_Enable;
  pBlob->IRQ_Status = Map5_IRQ_Status;
  pBlob->IRQ_Line = Map5_IRQ_Line;
  pBlob->Value0 = (BYTE)Map5_Value0;
  pBlob->Value1 = (BYTE)Map5_Value1;
  pBlob->Wram_Protect0 = Map5_Wram_Protect0;
  pBlob->Wram_Protect1 = Map5_Wram_Protect1;
  pBlob->Prg_Size = Map5_Prg_Size;
  pBlob->Chr_Size = Map5_Chr_Size;
  pBlob->Gfx_Mode = Map5_Gfx_Mode;
  pBlob->Chr_Upper = Map5_Chr_Upper;
  pBlob->Chr_Last_Set = Map5_Chr_Last_Set;
  pBlob->Nt_Reg = Map5_Nt_Reg;
  pBlob->Nt_Set = Map5_Nt_Set;
  InfoNES_MemoryCopy( pBlob->Ex_Vram, Map5_Ex_Vram, 0x400 );
  InfoNES_MemoryCopy( pBlob->Ex_Nam, Map5_Ex_Nam, 0x400 );
}

static void Map5_LoadBlob( BYTE *pBuf )
{
  const Map5_Blob *pBlob = (const Map5_Blob *)pBuf;

  InfoNES_MemoryCopy( Map5_Prg_Reg, pBlob->Prg_Reg, sizeof( Map5_Prg_Reg ) );
  InfoNES_MemoryCopy( Map5_Wram_Reg, pBlob->Wram_Reg, sizeof( Map5_Wram_Reg ) );
  InfoNES_MemoryCopy( Map5_Chr_Reg, pBlob->Chr_Reg, sizeof( Map5_Chr_Reg ) );
  Map5_IRQ_Enable = pBlob->IRQ_Enable;
  Map5_IRQ_Status = pBlob->IRQ_Status;
  Map5_IRQ_Line = pBlob->IRQ_Line;
  Map5_Value0 = pBlob->Value0;
  Map5_Value1 = pBlob->Value1;
  Map5_Wram_Protect0 = pBlob->Wram_Protect0;
  Map5_Wram_Protect1 = pBlob->Wram_Protect1;
  Map5_Prg_Size = pBlob->Prg_Size;
  Map5_Chr_Size = pBlob->Chr_Size;
  Map5_Gfx_Mode = pBlob->Gfx_Mode;
  Map5_Chr_Upper = pBlob->Chr_Upper;
  Map5_Chr_Last_Set = pBlob->Chr_Last_Set;
  Map5_Nt_Reg = pBlob->Nt_Reg;
  Map5_Nt_Set = pBlob->Nt_Set;
  InfoNES_MemoryCopy( Map5_Ex_Vram, pBlob->Ex_Vram, 0x400 );
  InfoNES_MemoryCopy( Map5_Ex_Nam, pBlob->Ex_Nam, 0x400 );

  /* MapperLoadBlob runs last in LoadState, after state.cpp has rebuilt
     ROMBANK and PPUBANK from bank indices. Those indices are measured from
     ROM, VROM and PPURAM, so every window MMC5 had pointed into its own
     memory - PRG RAM at $6000-$DFFF, ExRAM or the fill table as a name
     table - came back wrong. Rebuild them all from the registers. */
  Map5_Sync_Prg_Banks();
  SRAMBANK = ( Map5_Wram_Reg[ 3 ] != 0xff ) ? Map5_ROMPAGE( Map5_Wram_Reg[ 3 ] ) : SRAM;
  Map5_Sync_Chr_Set( 0 );
  Map5_Sync_Chr_Set( 1 );
  Map5_Sync_Ex_Chr_Banks();
  /* Until the game writes $5105 the header mirroring state.cpp restored
     stands. */
  if ( Map5_Nt_Set )
    Map5_Sync_Nametables();
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
