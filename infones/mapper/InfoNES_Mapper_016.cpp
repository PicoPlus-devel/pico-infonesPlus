/*===================================================================*/
/*                                                                   */
/*                   Mapper 16 (Bandai Mapper)                       */
/*                                                                   */
/*===================================================================*/

BYTE  Map16_Regs[3];

BYTE  Map16_IRQ_Enable;
DWORD Map16_IRQ_Cnt;
DWORD Map16_IRQ_Latch;

/*-------------------------------------------------------------------*/
/*  24C02 serial EEPROM                                              */
/*-------------------------------------------------------------------*/
/* LZ93D50 boards (submapper 5) keep the battery save in a 256-byte
 * I2C EEPROM instead of SRAM. The game drives SCL and SDA through bits
 * 5 and 6 of $800D and reads SDA back in bit 4 of any address in
 * $6000-$7FFF. Without it Dragon Ball Z II, III and Gaiden, Rokudenashi
 * Blues and SD Gundam Gaiden 2 and 3 wait forever for the chip and stay
 * on a black screen. The protocol follows Mesen2's Eeprom24C02.
 *
 * Nothing else answers in $6000-$7FFF on these boards, and K6502_Read
 * returns SRAMBANK[] there without asking the mapper, so SRAM holds the
 * SDA level in bit 4 of every byte and is refilled only when the level
 * changes. That keeps the CPU read path untouched (see the red-flicker
 * note in K6502_rw.h). The game reads SDA a few thousand times while it
 * loads or saves and not at all in between.
 *
 * Map16_Eeprom is the chip's contents: the .SAV file and the state file
 * carry it through MapperPrgRam, and it is only allocated on boards that
 * have the chip. Allocated once; InfoNES_Fin frees it. */
#define MAP16_EEPROM_SIZE 256

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

static Map16_Eeprom_State Map16_Ee;

static void Map16_Eeprom_Output( BYTE byOutput )
{
  if ( byOutput != Map16_Ee.byOutput )
  {
    Map16_Ee.byOutput = byOutput;
    InfoNES_MemorySet( SRAM, byOutput ? 0x10 : 0x00, SRAM_SIZE );
  }
}

static void Map16_Eeprom_Shift_In( BYTE *pbyDest, BYTE bySda )
{
  if ( Map16_Ee.byCounter < 8 )
  {
    BYTE byBit = 7 - Map16_Ee.byCounter++;
    *pbyDest = ( *pbyDest & ~( 1 << byBit ) ) | ( bySda << byBit );
  }
}

static void Map16_Eeprom_Write( BYTE byScl, BYTE bySda )
{
  Map16_Eeprom_State &e = Map16_Ee;

  if ( e.byScl && byScl && bySda < e.bySda )
  {
    /* START: SDA falls while SCL is high */
    e.byMode = MAP16_EE_CHIP_ADDRESS;
    e.byCounter = 0;
    Map16_Eeprom_Output( 1 );
  }
  else if ( e.byScl && byScl && bySda > e.bySda )
  {
    /* STOP: SDA rises while SCL is high */
    e.byMode = MAP16_EE_IDLE;
    Map16_Eeprom_Output( 1 );
  }
  else if ( byScl > e.byScl )
  {
    /* SCL rises: one bit moves in or out */
    switch ( e.byMode )
    {
      case MAP16_EE_CHIP_ADDRESS:
        Map16_Eeprom_Shift_In( &e.byChipAddress, bySda );
        break;

      case MAP16_EE_ADDRESS:
        Map16_Eeprom_Shift_In( &e.byAddress, bySda );
        break;

      case MAP16_EE_WRITE:
        Map16_Eeprom_Shift_In( &e.byData, bySda );
        break;

      case MAP16_EE_READ:
        if ( e.byCounter < 8 )
        {
          Map16_Eeprom_Output( ( e.byData >> ( 7 - e.byCounter ) ) & 1 );
          e.byCounter++;
        }
        break;

      case MAP16_EE_SEND_ACK:
        Map16_Eeprom_Output( 0 );
        break;

      case MAP16_EE_WAIT_ACK:
        /* The game acknowledged the byte, so it wants the next one */
        if ( !bySda )
        {
          e.byNextMode = MAP16_EE_READ;
          e.byData = Map16_Eeprom[ e.byAddress ];
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
              e.byData = Map16_Eeprom[ e.byAddress ];
            } else {
              e.byNextMode = MAP16_EE_ADDRESS;
            }
          } else {
            /* Not addressed to this chip */
            e.byMode = MAP16_EE_IDLE;
          }
          Map16_Eeprom_Output( 1 );
        }
        break;

      case MAP16_EE_ADDRESS:
        if ( e.byCounter == 8 )
        {
          e.byCounter = 0;
          e.byMode = MAP16_EE_SEND_ACK;
          e.byNextMode = MAP16_EE_WRITE;
          Map16_Eeprom_Output( 1 );
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
          Map16_Eeprom[ e.byAddress++ ] = e.byData;
          SRAMwritten = true;
        }
        break;

      case MAP16_EE_SEND_ACK:
      case MAP16_EE_WAIT_ACK:
        e.byMode = e.byNextMode;
        e.byCounter = 0;
        Map16_Eeprom_Output( 1 );
        break;
    }
  }

  e.byScl = byScl;
  e.bySda = bySda;
}

/*-------------------------------------------------------------------*/
/*  Mapper 16 Save State Functions                                   */
/*-------------------------------------------------------------------*/
/* Only the EEPROM's bus state: its contents are MapperPrgRam, and the
 * SRAM that mirrors its output is in the state file already. */
static int Map16_BlobSize()
{
  return sizeof( Map16_Ee );
}

static void Map16_SaveBlob( BYTE *pBuf )
{
  InfoNES_MemoryCopy( pBuf, &Map16_Ee, sizeof( Map16_Ee ) );
}

static void Map16_LoadBlob( BYTE *pBuf )
{
  InfoNES_MemoryCopy( &Map16_Ee, pBuf, sizeof( Map16_Ee ) );
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

  /* Write to SRAM */
  MapperSram = Map16_Write;

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
  ROMBANK0 = ROMPAGE( 0 );
  ROMBANK1 = ROMPAGE( 1 );
  ROMBANK2 = ROMLASTPAGE( 1 );
  ROMBANK3 = ROMLASTPAGE( 0 );

  /* Initialize State Flag */
  Map16_Regs[ 0 ] = 0;
  Map16_Regs[ 1 ] = 0;
  Map16_Regs[ 2 ] = 0;

  Map16_IRQ_Enable = 0;
  Map16_IRQ_Cnt = 0;
  Map16_IRQ_Latch = 0;

  /* The EEPROM is on LZ93D50 boards with a battery. FCG-1/2 boards
     (submapper 4) have none, and a 512KB board with a battery is the
     mapper 153 layout, whose save is 8KB of SRAM at $6000. An erased
     24C02 reads $FF. */
  if ( ROM_SRAM && SubMapperNo != 4 && NesHeader.byRomSize <= 16 )
  {
    if ( !Map16_Eeprom )
    {
      Map16_Eeprom = (BYTE *)Frens::f_malloc( MAP16_EEPROM_SIZE );
      InfoNES_MemorySet( Map16_Eeprom, 0xff, MAP16_EEPROM_SIZE );
    }
    InfoNES_MemorySet( &Map16_Ee, 0, sizeof( Map16_Ee ) );
    Map16_Ee.byOutput = 1;
    InfoNES_MemorySet( SRAM, 0x10, SRAM_SIZE );

    /* Save state and battery hooks (cleared on every reset, so install them here) */
    MapperBlobSize = Map16_BlobSize;
    MapperSaveBlob = Map16_SaveBlob;
    MapperLoadBlob = Map16_LoadBlob;
    MapperPrgRam = Map16_Eeprom;
    MapperPrgRamSize = MAP16_EEPROM_SIZE;
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
      byData %= ( NesHeader.byVRomSize << 3 );
      PPUBANK[ 0 ] = VROMPAGE( byData );
      InfoNES_SetupChr();
      break;

    case 0x0001:
      byData %= ( NesHeader.byVRomSize << 3 );
      PPUBANK[ 1 ] = VROMPAGE( byData );
      InfoNES_SetupChr();
      break;

    case 0x0002:
      byData %= ( NesHeader.byVRomSize << 3 );
      PPUBANK[ 2 ] = VROMPAGE( byData );
      InfoNES_SetupChr();
      break;

    case 0x0003:
      byData %= ( NesHeader.byVRomSize << 3 );
      PPUBANK[ 3 ] = VROMPAGE( byData );
      InfoNES_SetupChr();
      break;

    case 0x0004:
      byData %= ( NesHeader.byVRomSize << 3 );
      PPUBANK[ 4 ] = VROMPAGE( byData );
      InfoNES_SetupChr();
      break;

    case 0x0005:
      byData %= ( NesHeader.byVRomSize << 3 );
      PPUBANK[ 5 ] = VROMPAGE( byData );
      InfoNES_SetupChr();
      break;

    case 0x0006:
      byData %= ( NesHeader.byVRomSize << 3 );
      PPUBANK[ 6 ] = VROMPAGE( byData );
      InfoNES_SetupChr();
      break;

    case 0x0007:
      byData %= ( NesHeader.byVRomSize << 3 );
      PPUBANK[ 7 ] = VROMPAGE( byData );
      InfoNES_SetupChr();
      break;

    case 0x0008:
      byData <<= 1;
      byData %= ( NesHeader.byRomSize << 1 );
      ROMBANK0 = ROMPAGE( byData );
      ROMBANK1 = ROMPAGE( byData + 1 );
      break;

    case 0x0009:
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
        /* EEPROM control: bit 5 = SCL, bit 6 = SDA */
        if ( Map16_Eeprom )
        {
          Map16_Eeprom_Write( ( byData >> 5 ) & 1, ( byData >> 6 ) & 1 );
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
