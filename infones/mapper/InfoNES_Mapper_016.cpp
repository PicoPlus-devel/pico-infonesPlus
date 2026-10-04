/*===================================================================*/
/*                                                                   */
/*                   Mapper 16 (Bandai Mapper)                       */
/*                                                                   */
/*===================================================================*/

/* The same chip on other boards runs mappers 153, 157 and 159 through
 * this code as well; MapperNo tells them apart (see Map16_Remap):
 *    16  FCG-1/2 or LZ93D50, CHR ROM, a 24C02 EEPROM when battery-backed
 *   153  LZ93D50, 512KB PRG, 8KB battery SRAM at $6000 (Famicom Jump II)
 *   157  Datach Joint ROM System: CHR RAM, a 24C02 in the base unit, an
 *        extra 24C01 on some game cartridges and a barcode reader
 *   159  LZ93D50 with a 128-byte 24C01 EEPROM
 * The board differences follow Mesen2's BandaiFcg. */

BYTE  Map16_Regs[3];

BYTE  Map16_IRQ_Enable;
DWORD Map16_IRQ_Cnt;
DWORD Map16_IRQ_Latch;

/* 512KB boards take PRG A18 from bit 0 of the CHR bank registers, which
   they do not use for CHR. Map16_Chr_Bit0 keeps bit 0 of each of the eight
   registers; any one of them set selects the upper 256KB. */
static BYTE Map16_Outer_Prg;
static BYTE Map16_Prg;
static BYTE Map16_Chr_Bit0;

/* $x009 as last written, 0xff before the first write */
static BYTE Map16_Mirror;

/*-------------------------------------------------------------------*/
/*  Serial EEPROMs                                                   */
/*-------------------------------------------------------------------*/
/* LZ93D50 boards (submapper 5) keep the battery save in a 256-byte
 * I2C EEPROM instead of SRAM. The game drives SCL and SDA through bits
 * 5 and 6 of $800D and reads SDA back in bit 4 of any address in
 * $6000-$7FFF. Without it Dragon Ball Z II, III and Gaiden, Rokudenashi
 * Blues and SD Gundam Gaiden 2 and 3 wait forever for the chip and stay
 * on a black screen. The protocol follows Mesen2's Eeprom24C02.
 *
 * Mapper 159 has the 128-byte 24C01 instead, which takes a 7-bit word
 * address and shifts bits LSB first (Mesen2's Eeprom24C01). Datach games
 * (157) talk to the base unit's 24C02, and some also to a 24C01 on the game
 * cartridge; that one is clocked by bit 3 of $x000-$x003 and shares SDA.
 *
 * Nothing else answers in $6000-$7FFF on these boards, and K6502_Read
 * returns SRAMBANK[] there without asking the mapper, so SRAM holds the
 * SDA level in bit 4 of every byte and is refilled only when the level
 * changes. That keeps the CPU read path untouched (see the red-flicker
 * note in K6502_rw.h). The game reads SDA a few thousand times while it
 * loads or saves and not at all in between. Bit 3 is the Datach barcode
 * reader, which reads 0 while no card is being swiped: a 1 there (erased
 * flash in SRAM) keeps the Datach games on a black screen.
 *
 * Map16_Eeprom is the chips' contents, the board's chip first and the
 * Datach cartridge's 24C01 after it: the .SAV file and the state file carry
 * it through MapperPrgRam, and it is only allocated on boards that have a
 * chip. Allocated once; InfoNES_Fin frees it. */
#define MAP16_EEPROM_SIZE   256   /* 24C02 */
#define MAP16_EEPROM01_SIZE 128   /* 24C01 */

BYTE *Map16_Eeprom;

enum
{
  MAP16_EE_IDLE,
  MAP16_EE_CHIP_ADDRESS,
  MAP16_EE_ADDRESS,
  MAP16_EE_READ,
  MAP16_EE_WRITE,
  MAP16_EE_SEND_ACK,
  MAP16_EE_WAIT_ACK
};

struct Map16_Eeprom_State
{
  BYTE byMode;
  BYTE byNextMode;
  BYTE byChipAddress;
  BYTE byAddress;
  BYTE byData;
  BYTE byCounter;  /* bits shifted in or out of the current byte */
  BYTE byOutput;   /* SDA as the chip drives it */
  BYTE byScl;      /* SCL and SDA as the game last wrote them */
  BYTE bySda;
};

/* The board's chip, and the Datach cartridge's 24C01 */
static Map16_Eeprom_State Map16_Ee;
static Map16_Eeprom_State Map16_Ee2;

/* Which chips the board has; set on every reset */
enum
{
  MAP16_CHIP_NONE,
  MAP16_CHIP_24C02,
  MAP16_CHIP_24C01
};

static BYTE Map16_Chip;
static BYTE Map16_Extra;

/* The SDA level SRAM currently mirrors */
static BYTE Map16_Sda;

static void Map16_Eeprom_Sync()
{
  /* Either chip can pull SDA low */
  BYTE bySda = Map16_Ee.byOutput;
  if ( Map16_Extra )
    bySda &= Map16_Ee2.byOutput;

  if ( bySda != Map16_Sda )
  {
    Map16_Sda = bySda;
    InfoNES_MemorySet( SRAM, bySda ? 0x10 : 0x00, SRAM_SIZE );
  }
}

static void Map16_Eeprom_Shift_In( Map16_Eeprom_State &e, BYTE *pbyDest, BYTE bySda )
{
  if ( e.byCounter < 8 )
  {
    BYTE byBit = 7 - e.byCounter++;
    *pbyDest = ( *pbyDest & ~( 1 << byBit ) ) | ( bySda << byBit );
  }
}

/* 24C02: device address byte, then an 8-bit word address, MSB first */
static void Map16_Eeprom02_Write( Map16_Eeprom_State &e, BYTE *pbyRom, BYTE byScl, BYTE bySda )
{
  if ( e.byScl && byScl && bySda < e.bySda )
  {
    /* START: SDA falls while SCL is high */
    e.byMode = MAP16_EE_CHIP_ADDRESS;
    e.byCounter = 0;
    e.byOutput = 1;
  }
  else if ( e.byScl && byScl && bySda > e.bySda )
  {
    /* STOP: SDA rises while SCL is high */
    e.byMode = MAP16_EE_IDLE;
    e.byOutput = 1;
  }
  else if ( byScl > e.byScl )
  {
    /* SCL rises: one bit moves in or out */
    switch ( e.byMode )
    {
      case MAP16_EE_CHIP_ADDRESS:
        Map16_Eeprom_Shift_In( e, &e.byChipAddress, bySda );
        break;

      case MAP16_EE_ADDRESS:
        Map16_Eeprom_Shift_In( e, &e.byAddress, bySda );
        break;

      case MAP16_EE_WRITE:
        Map16_Eeprom_Shift_In( e, &e.byData, bySda );
        break;

      case MAP16_EE_READ:
        if ( e.byCounter < 8 )
        {
          e.byOutput = ( e.byData >> ( 7 - e.byCounter ) ) & 1;
          e.byCounter++;
        }
        break;

      case MAP16_EE_SEND_ACK:
        e.byOutput = 0;
        break;

      case MAP16_EE_WAIT_ACK:
        /* The game acknowledged the byte, so it wants the next one */
        if ( !bySda )
        {
          e.byNextMode = MAP16_EE_READ;
          e.byData = pbyRom[ e.byAddress ];
        }
        break;
    }
  }
  else if ( byScl < e.byScl )
  {
    /* SCL falls: a byte or an acknowledge is complete */
    switch ( e.byMode )
    {
      case MAP16_EE_CHIP_ADDRESS:
        if ( e.byCounter == 8 )
        {
          e.byCounter = 0;
          if ( ( e.byChipAddress & 0xa0 ) == 0xa0 )
          {
            e.byMode = MAP16_EE_SEND_ACK;
            if ( e.byChipAddress & 0x01 )
            {
              /* Read, starting at the current address */
              e.byNextMode = MAP16_EE_READ;
              e.byData = pbyRom[ e.byAddress ];
            } else {
              e.byNextMode = MAP16_EE_ADDRESS;
            }
          } else {
            /* Not addressed to this chip */
            e.byMode = MAP16_EE_IDLE;
          }
          e.byOutput = 1;
        }
        break;

      case MAP16_EE_ADDRESS:
        if ( e.byCounter == 8 )
        {
          e.byCounter = 0;
          e.byMode = MAP16_EE_SEND_ACK;
          e.byNextMode = MAP16_EE_WRITE;
          e.byOutput = 1;
        }
        break;

      case MAP16_EE_READ:
        if ( e.byCounter == 8 )
        {
          e.byMode = MAP16_EE_WAIT_ACK;
          e.byAddress++;
        }
        break;

      case MAP16_EE_WRITE:
        if ( e.byCounter == 8 )
        {
          e.byCounter = 0;
          e.byMode = MAP16_EE_SEND_ACK;
          e.byNextMode = MAP16_EE_WRITE;
          pbyRom[ e.byAddress++ ] = e.byData;
          SRAMwritten = true;
        }
        break;

      case MAP16_EE_SEND_ACK:
      case MAP16_EE_WAIT_ACK:
        e.byMode = e.byNextMode;
        e.byCounter = 0;
        e.byOutput = 1;
        break;
    }
  }

  e.byScl = byScl;
  e.bySda = bySda;
}

/* 24C01: no device address; the first byte is a 7-bit word address plus
   the read/write bit, and every byte goes LSB first */
static void Map16_Eeprom01_Shift_In( Map16_Eeprom_State &e, BYTE *pbyDest, BYTE bySda )
{
  if ( e.byCounter < 8 )
  {
    BYTE byBit = e.byCounter++;
    *pbyDest = ( *pbyDest & ~( 1 << byBit ) ) | ( bySda << byBit );
  }
}

static void Map16_Eeprom01_Write( Map16_Eeprom_State &e, BYTE *pbyRom, BYTE byScl, BYTE bySda )
{
  if ( e.byScl && byScl && bySda < e.bySda )
  {
    /* START */
    e.byMode = MAP16_EE_ADDRESS;
    e.byAddress = 0;
    e.byCounter = 0;
    e.byOutput = 1;
  }
  else if ( e.byScl && byScl && bySda > e.bySda )
  {
    /* STOP */
    e.byMode = MAP16_EE_IDLE;
    e.byOutput = 1;
  }
  else if ( byScl > e.byScl )
  {
    /* SCL rises */
    switch ( e.byMode )
    {
      case MAP16_EE_ADDRESS:
        if ( e.byCounter < 7 )
        {
          Map16_Eeprom01_Shift_In( e, &e.byAddress, bySda );
        }
        else if ( e.byCounter == 7 )
        {
          /* The eighth bit says read or write */
          e.byCounter = 8;
          if ( bySda )
          {
            e.byNextMode = MAP16_EE_READ;
            e.byData = pbyRom[ e.byAddress & 0x7f ];
          } else {
            e.byNextMode = MAP16_EE_WRITE;
          }
        }
        break;

      case MAP16_EE_SEND_ACK:
        e.byOutput = 0;
        break;

      case MAP16_EE_READ:
        if ( e.byCounter < 8 )
        {
          e.byOutput = ( e.byData >> e.byCounter ) & 1;
          e.byCounter++;
        }
        break;

      case MAP16_EE_WRITE:
        Map16_Eeprom01_Shift_In( e, &e.byData, bySda );
        break;

      case MAP16_EE_WAIT_ACK:
        if ( !bySda )
        {
          e.byNextMode = MAP16_EE_IDLE;
        }
        break;
    }
  }
  else if ( byScl < e.byScl )
  {
    /* SCL falls */
    switch ( e.byMode )
    {
      case MAP16_EE_ADDRESS:
        if ( e.byCounter == 8 )
        {
          e.byMode = MAP16_EE_SEND_ACK;
          e.byOutput = 1;
        }
        break;

      case MAP16_EE_SEND_ACK:
        e.byMode = e.byNextMode;
        e.byCounter = 0;
        e.byOutput = 1;
        break;

      case MAP16_EE_READ:
        if ( e.byCounter == 8 )
        {
          e.byMode = MAP16_EE_WAIT_ACK;
          e.byAddress = ( e.byAddress + 1 ) & 0x7f;
        }
        break;

      case MAP16_EE_WRITE:
        if ( e.byCounter == 8 )
        {
          e.byMode = MAP16_EE_SEND_ACK;
          e.byNextMode = MAP16_EE_IDLE;
          pbyRom[ e.byAddress & 0x7f ] = e.byData;
          e.byAddress = ( e.byAddress + 1 ) & 0x7f;
          SRAMwritten = true;
        }
        break;
    }
  }

  e.byScl = byScl;
  e.bySda = bySda;
}

/*-------------------------------------------------------------------*/
/*  Mapper 16 Save State Functions                                   */
/*-------------------------------------------------------------------*/
/* Mapper 16: only the EEPROM's bus state. Its contents are MapperPrgRam,
 * and the SRAM that mirrors its output is in the state file already.
 * The other boards also keep the registers a later write builds on, and
 * the mirroring, which the state loader resets to the header's. */
struct Map16_Board_State
{
  Map16_Eeprom_State Ee;
  Map16_Eeprom_State Ee2;
  BYTE byPrg;
  BYTE byChrBit0;
  BYTE byMirror;
};

static int Map16_BlobSize()
{
  if ( MapperNo == 16 )
    return sizeof( Map16_Ee );

  return sizeof( Map16_Board_State );
}

static void Map16_SaveBlob( BYTE *pBuf )
{
  if ( MapperNo == 16 )
  {
    InfoNES_MemoryCopy( pBuf, &Map16_Ee, sizeof( Map16_Ee ) );
    return;
  }

  Map16_Board_State *pState = (Map16_Board_State *)pBuf;
  pState->Ee = Map16_Ee;
  pState->Ee2 = Map16_Ee2;
  pState->byPrg = Map16_Prg;
  pState->byChrBit0 = Map16_Chr_Bit0;
  pState->byMirror = Map16_Mirror;
}

static void Map16_Set_Mirroring( BYTE byData );

static void Map16_LoadBlob( BYTE *pBuf )
{
  if ( MapperNo == 16 )
  {
    InfoNES_MemoryCopy( &Map16_Ee, pBuf, sizeof( Map16_Ee ) );
    Map16_Sda = Map16_Ee.byOutput;
    return;
  }

  Map16_Board_State *pState = (Map16_Board_State *)pBuf;
  Map16_Ee = pState->Ee;
  Map16_Ee2 = pState->Ee2;
  Map16_Prg = pState->byPrg;
  Map16_Chr_Bit0 = pState->byChrBit0;
  Map16_Mirror = pState->byMirror;

  /* SRAM came back from the state file with the SDA level in it */
  Map16_Sda = Map16_Extra ? ( Map16_Ee.byOutput & Map16_Ee2.byOutput ) : Map16_Ee.byOutput;

  if ( Map16_Mirror != 0xff )
    Map16_Set_Mirroring( Map16_Mirror );
}

/*-------------------------------------------------------------------*/
/*  Mapper 16 header remap                                           */
/*-------------------------------------------------------------------*/
/* Famicom Jump II, the Datach games and some 24C01 boards circulate with
 * an iNES mapper 16 header. Their real numbers by CRC32 of PRG+CHR, taken
 * from the Mesen2 game database. A dump that is not listed but has CHR RAM
 * is not a plain mapper 16 either: Famicom Jump II is the only 512KB one,
 * and the Datach games are the rest. */
static const struct
{
  DWORD dwCrc;
  WORD  wMapper;
} Map16_Remap_Table[] =
{
  { 0x3F15D20D, 153 },  /* Famicom Jump II - Saikyou no 7 Nin */
  { 0x0BE0A328, 157 },  /* Datach games */
  { 0x0C661EA1, 157 },
  { 0x19E81461, 157 },
  { 0x5B457641, 157 },
  { 0x65F2AA05, 157 },
  { 0x894EFDBC, 157 },
  { 0x983D8175, 157 },
  { 0xBE06853F, 157 },
  { 0xF51A7F46, 157 },
  { 0x183859D2, 159 },  /* LZ93D50 + 24C01 games */
  { 0x2194BB29, 159 },
  { 0x27B9CBAB, 159 },
  { 0x276AC722, 159 },
  { 0x0CF42E69, 159 },
  { 0x70A26AF3, 159 },
  { 0x836FDC60, 159 },
  { 0xAD23BB7C, 159 },
  { 0xB7F28915, 159 },
  { 0xDCB972CE, 159 },
  { 0xDD942A96, 159 },
  { 0xE170404C, 159 },
  { 0xF6E67BB4, 159 },
  { 0xFE6C086F, 159 },
};

WORD Map16_Remap()
{
  for ( unsigned int nIdx = 0; nIdx < sizeof( Map16_Remap_Table ) / sizeof( Map16_Remap_Table[ 0 ] ); ++nIdx )
  {
    if ( Map16_Remap_Table[ nIdx ].dwCrc == InfoNES_RomCrc )
      return Map16_Remap_Table[ nIdx ].wMapper;
  }

  if ( NesHeader.byVRomSize == 0 )
    return ( NesHeader.byRomSize >= 32 ) ? 153 : 157;

  return 16;
}

/*-------------------------------------------------------------------*/
/*  Mapper 16 Bank Functions                                         */
/*-------------------------------------------------------------------*/
static void Map16_Set_Prg()
{
  DWORD dwOuter = Map16_Chr_Bit0 ? 0x10 : 0x00;
  DWORD dwPages = NesHeader.byRomSize << 1;
  DWORD dwPrg   = ( ( Map16_Prg | dwOuter ) << 1 ) % dwPages;
  DWORD dwFixed = ( ( 0x0f | dwOuter ) << 1 ) % dwPages;

  ROMBANK0 = ROMPAGE( dwPrg );
  ROMBANK1 = ROMPAGE( dwPrg + 1 );
  ROMBANK2 = ROMPAGE( dwFixed );
  ROMBANK3 = ROMPAGE( dwFixed + 1 );
}

static void Map16_Set_Mirroring( BYTE byData )
{
  switch ( byData & 0x03 )
  {
    case 0x00:
      InfoNES_Mirroring( 1 );
      break;

    case 0x01:
      InfoNES_Mirroring( 0 );
      break;

    case 0x02:
      InfoNES_Mirroring( 3 );
      break;

    case 0x03:
      InfoNES_Mirroring( 2 );
      break;
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 157/159 Write to SRAM Function                            */
/*-------------------------------------------------------------------*/
/* Nothing at $6000-$7FFF takes writes on these boards, so put the SDA
   mirror back over whatever the game stored there. */
static void Map16_Sram( WORD wAddr, BYTE byData )
{
  SRAMBANK[ wAddr & 0x1fff ] = Map16_Sda ? 0x10 : 0x00;
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 16                                             */
/*-------------------------------------------------------------------*/
void Map16_Init()
{
  /* Initialize Mapper */
  MapperInit = Map16_Init;

  /* Write to Mapper */
  MapperWrite = Map16_Write;

  /* Write to SRAM. FCG-1/2 boards answer at $6000 as well; mapper 153 has
     real SRAM there, and 157 and 159 only the SDA mirror. */
  if ( MapperNo == 16 )
    MapperSram = Map16_Write;
  else if ( MapperNo == 153 )
    MapperSram = Map0_Sram;
  else
    MapperSram = Map16_Sram;

  /* Write to APU */
  MapperApu = Map0_Apu;

  /* Read from APU */
  MapperReadApu = Map0_ReadApu;

  /* Callback at VSync */
  MapperVSync = Map0_VSync;

  /* Callback at HSync */
  MapperHSync = Map16_HSync;

  /* Callback at PPU */
  MapperPPU = Map0_PPU;

  /* Callback at Rendering Screen ( 1:BG, 0:Sprite ) */
  MapperRenderScreen = Map0_RenderScreen;

  /* Set SRAM Banks */
  SRAMBANK = SRAM;

  /* Set ROM Banks */
  Map16_Outer_Prg = ( MapperNo == 153 || NesHeader.byRomSize >= 32 );
  Map16_Prg = 0;
  Map16_Chr_Bit0 = 0;
  Map16_Mirror = 0xff;
  if ( Map16_Outer_Prg )
  {
    Map16_Set_Prg();
  } else {
    ROMBANK0 = ROMPAGE( 0 );
    ROMBANK1 = ROMPAGE( 1 );
    ROMBANK2 = ROMLASTPAGE( 1 );
    ROMBANK3 = ROMLASTPAGE( 0 );
  }

  /* Initialize State Flag */
  Map16_Regs[ 0 ] = 0;
  Map16_Regs[ 1 ] = 0;
  Map16_Regs[ 2 ] = 0;

  Map16_IRQ_Enable = 0;
  Map16_IRQ_Cnt = 0;
  Map16_IRQ_Latch = 0;

  /* EEPROMs. On mapper 16 the 24C02 is on LZ93D50 boards with a battery.
     FCG-1/2 boards (submapper 4) have none, and a 512KB board with a
     battery is the mapper 153 layout, whose save is 8KB of SRAM at $6000.
     Every Datach game talks to the base unit's 24C02. The extra 24C01 is on
     Battle Rush only, but an iNES header cannot say so, so it is fitted
     unless a NES 2.0 header declares some other PRG-NVRAM size. */
  Map16_Chip = MAP16_CHIP_NONE;
  Map16_Extra = 0;
  switch ( MapperNo )
  {
    case 16:
      if ( ROM_SRAM && SubMapperNo != 4 && NesHeader.byRomSize <= 16 )
        Map16_Chip = MAP16_CHIP_24C02;
      break;

    case 157:
      Map16_Chip = MAP16_CHIP_24C02;
      Map16_Extra = ( ( NesHeader.byInfo2 & 0x0c ) != 0x08 ||
                      ( NesHeader.byReserve[ 2 ] >> 4 ) == 1 );
      break;

    case 159:
      Map16_Chip = MAP16_CHIP_24C01;
      break;
  }

  if ( Map16_Chip != MAP16_CHIP_NONE )
  {
    /* An erased 24C0x reads $FF */
    if ( !Map16_Eeprom )
    {
      Map16_Eeprom = (BYTE *)Frens::f_malloc( MAP16_EEPROM_SIZE + MAP16_EEPROM01_SIZE );
      InfoNES_MemorySet( Map16_Eeprom, 0xff, MAP16_EEPROM_SIZE + MAP16_EEPROM01_SIZE );
    }
    InfoNES_MemorySet( &Map16_Ee, 0, sizeof( Map16_Ee ) );
    InfoNES_MemorySet( &Map16_Ee2, 0, sizeof( Map16_Ee2 ) );
    Map16_Ee.byOutput = 1;
    Map16_Ee2.byOutput = 1;
    Map16_Sda = 1;
    InfoNES_MemorySet( SRAM, 0x10, SRAM_SIZE );

    /* Battery hook (cleared on every reset, so install it here). The 24C01
       of a Datach game cartridge sits behind the 24C02. */
    MapperPrgRam = Map16_Eeprom;
    MapperPrgRamSize = ( Map16_Chip == MAP16_CHIP_24C01 ) ? MAP16_EEPROM01_SIZE : MAP16_EEPROM_SIZE;
    if ( Map16_Extra )
      MapperPrgRamSize += MAP16_EEPROM01_SIZE;

    /* The chips hold the save whatever the header's battery bit says, and
       the .SAV code only writes MapperPrgRam for a battery-backed cart */
    ROM_SRAM = 2;
  }

  /* Save state hooks */
  if ( Map16_Chip != MAP16_CHIP_NONE || MapperNo != 16 )
  {
    MapperBlobSize = Map16_BlobSize;
    MapperSaveBlob = Map16_SaveBlob;
    MapperLoadBlob = Map16_LoadBlob;
  }

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 );
}

/*-------------------------------------------------------------------*/
/*  Mapper 16 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map16_Write( WORD wAddr, BYTE byData )
{
  switch ( wAddr & 0x000f )
  {
    case 0x0000:
    case 0x0001:
    case 0x0002:
    case 0x0003:
    case 0x0004:
    case 0x0005:
    case 0x0006:
    case 0x0007:
      if ( Map16_Outer_Prg )
      {
        BYTE byBit = 1 << ( wAddr & 0x07 );
        if ( byData & 0x01 )
          Map16_Chr_Bit0 |= byBit;
        else
          Map16_Chr_Bit0 &= ~byBit;
        Map16_Set_Prg();
      }
      else if ( NesHeader.byVRomSize > 0 )
      {
        byData %= ( NesHeader.byVRomSize << 3 );
        PPUBANK[ wAddr & 0x07 ] = VROMPAGE( byData );
        InfoNES_SetupChr();
      }

      /* The Datach cartridge's 24C01 is clocked by bit 3 of the first four */
      if ( Map16_Extra && ( wAddr & 0x000f ) <= 3 )
      {
        Map16_Eeprom01_Write( Map16_Ee2, Map16_Eeprom + MAP16_EEPROM_SIZE,
                              ( byData >> 3 ) & 1, Map16_Ee2.bySda );
        Map16_Eeprom_Sync();
      }
      break;

    case 0x0008:
      if ( Map16_Outer_Prg )
      {
        Map16_Prg = byData & 0x0f;
        Map16_Set_Prg();
      } else {
        byData <<= 1;
        byData %= ( NesHeader.byRomSize << 1 );
        ROMBANK0 = ROMPAGE( byData );
        ROMBANK1 = ROMPAGE( byData + 1 );
      }
      break;

    case 0x0009:
      Map16_Mirror = byData & 0x03;
      Map16_Set_Mirroring( byData );
      break;

      case 0x000a:
        Map16_IRQ_Enable = byData & 0x01;
        Map16_IRQ_Cnt = Map16_IRQ_Latch;
        break;

      case 0x000b:
        Map16_IRQ_Latch = ( Map16_IRQ_Latch & 0xff00 ) | byData;
        break;

      case 0x000c:
        Map16_IRQ_Latch = ( (DWORD)byData << 8 ) | ( Map16_IRQ_Latch & 0x00ff );
        break;

      case 0x000d:
        /* EEPROM control: bit 5 = SCL, bit 6 = SDA. On mapper 153 bit 5
           enables the SRAM instead, which is left enabled here. */
        if ( Map16_Chip == MAP16_CHIP_24C02 )
        {
          Map16_Eeprom02_Write( Map16_Ee, Map16_Eeprom, ( byData >> 5 ) & 1, ( byData >> 6 ) & 1 );
        }
        else if ( Map16_Chip == MAP16_CHIP_24C01 )
        {
          Map16_Eeprom01_Write( Map16_Ee, Map16_Eeprom, ( byData >> 5 ) & 1, ( byData >> 6 ) & 1 );
        }
        if ( Map16_Extra )
        {
          Map16_Eeprom01_Write( Map16_Ee2, Map16_Eeprom + MAP16_EEPROM_SIZE,
                                Map16_Ee2.byScl, ( byData >> 6 ) & 1 );
        }
        if ( Map16_Chip != MAP16_CHIP_NONE )
        {
          Map16_Eeprom_Sync();
        }
        break;
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 16 H-Sync Function                                        */
/*-------------------------------------------------------------------*/
void Map16_HSync()
{
  if ( Map16_IRQ_Enable )
  {
    /* Normal IRQ */
    if ( Map16_IRQ_Cnt <= 114 )
    {
      IRQ_REQ;
      Map16_IRQ_Cnt = 0;
      Map16_IRQ_Enable = 0;
    } else {
      Map16_IRQ_Cnt -= 114;
    }
  }
}
