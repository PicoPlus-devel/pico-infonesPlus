/*===================================================================*/
/*                                                                   */
/*      Mapper 555 (NES-EVENT2, Nintendo Campus Challenge 1991)      */
/*                                                                   */
/*===================================================================*/

/* The one cartridge made for the 1991 Campus Challenge: Dr. Mario, Super
 * Mario Bros. 3 and Pin*Bot behind a menu, on an MMC3 with an outer bank
 * register, 2KB more PRG RAM and the competition timer. FCEUmm and
 * NintendulatorNRS agree on all of it and this follows them; Mesen2 has no
 * mapper 555.
 *
 *   $5000-$57FF  2KB PRG RAM (Map555_Ram)
 *   $5800-$5BFF  outer register (Map555_Reg[ 0 ])
 *                  bits 0-1  let the MMC3 set PRG bank bits 3 and 4: a
 *                            64KB window with neither, 256KB with both
 *                  bit 2     upper 256KB of PRG, upper 128KB of CHR
 *                  bits 1-2  01: TQROM mode, CHR bank bit 6 picks the 8KB
 *                            CHR RAM instead of CHR ROM (Pin*Bot)
 *                  bit 3     0 holds the timer at zero, 1 lets it run
 *   $5C00-$5FFF  second register, which nothing is known to use
 *   $5800-$5FFF  read: bit 7 = timer expired, the other bits read $5C
 *
 * The timer counts CPU cycles up to $20000000, five minutes, plus $2000000
 * per step of DIP switches the header cannot describe; like both emulators
 * this runs with the switches at 0. It is counted per scanline, which is
 * the same clock the CPU runs on here.
 *
 * The title screen starts the competition on START of controller 2 (the
 * referee's), which InfoNES_HSync also feeds from controller 1. Held on
 * controller 2 at power-on, A opens a timer test, B starts Pin*Bot and
 * SELECT Dr. Mario on their own. */

#define MAP555_RAM_SIZE      0x800
#define MAP555_TIMER_CYCLES  0x20000000u

BYTE *Map555_Ram;
static BYTE  Map555_Reg[ 2 ];
static DWORD Map555_Count;
static BYTE  Map555_Expired;

/*-------------------------------------------------------------------*/
/*  Mapper 555 Set Banks Function                                    */
/*-------------------------------------------------------------------*/
static void Map555_Set_Banks()
{
  DWORD dwPrgMask = ( ( Map555_Reg[ 0 ] << 3 ) & 0x18 ) | 0x07;
  DWORD dwPrgBase = ( Map555_Reg[ 0 ] << 3 ) & 0x20;
  DWORD dwPages = NesHeader.byRomSize << 1;

  /* The MMC3's own PRG slots, then the outer register over all four */
  DWORD dwPrg[ 4 ];
  if ( Map4_Prg_Swap() )
  {
    dwPrg[ 0 ] = 0xfe;
    dwPrg[ 1 ] = Map4_Prg1;
    dwPrg[ 2 ] = Map4_Prg0;
  } else {
    dwPrg[ 0 ] = Map4_Prg0;
    dwPrg[ 1 ] = Map4_Prg1;
    dwPrg[ 2 ] = 0xfe;
  }
  dwPrg[ 3 ] = 0xff;

  ROMBANK0 = ROMPAGE( ( dwPrgBase | ( dwPrg[ 0 ] & dwPrgMask ) ) % dwPages );
  ROMBANK1 = ROMPAGE( ( dwPrgBase | ( dwPrg[ 1 ] & dwPrgMask ) ) % dwPages );
  ROMBANK2 = ROMPAGE( ( dwPrgBase | ( dwPrg[ 2 ] & dwPrgMask ) ) % dwPages );
  ROMBANK3 = ROMPAGE( ( dwPrgBase | ( dwPrg[ 3 ] & dwPrgMask ) ) % dwPages );

  /* Two 2KB banks and four 1KB banks, swapped between the pattern tables
     by $8000 bit 7 */
  DWORD dwChr[ 8 ];
  DWORD dw2k[ 4 ] = { Map4_Chr01, Map4_Chr01 + 1, Map4_Chr23, Map4_Chr23 + 1 };
  DWORD dw1k[ 4 ] = { Map4_Chr4, Map4_Chr5, Map4_Chr6, Map4_Chr7 };
  for ( int nIdx = 0; nIdx < 4; ++nIdx )
  {
    dwChr[ nIdx ]     = Map4_Chr_Swap() ? dw1k[ nIdx ] : dw2k[ nIdx ];
    dwChr[ 4 + nIdx ] = Map4_Chr_Swap() ? dw2k[ nIdx ] : dw1k[ nIdx ];
  }

  DWORD dwChrBase = ( Map555_Reg[ 0 ] << 5 ) & 0x80;
  bool  bTqrom = ( Map555_Reg[ 0 ] & 0x06 ) == 0x02;
  for ( int nSlot = 0; nSlot < 8; ++nSlot )
  {
    if ( bTqrom && ( dwChr[ nSlot ] & 0x40 ) )
      PPUBANK[ nSlot ] = CRAMPAGE( dwChr[ nSlot ] & 0x07 );
    else
      PPUBANK[ nSlot ] = VROMPAGE( ( dwChrBase | ( dwChr[ nSlot ] & ( bTqrom ? 0xff : 0x7f ) ) )
                                   % ( NesHeader.byVRomSize << 3 ) );
  }
  InfoNES_SetupChr();
}

/*-------------------------------------------------------------------*/
/*  Mapper 555 Write Functions                                       */
/*-------------------------------------------------------------------*/
void Map555_Write( WORD wAddr, BYTE byData )
{
  Map4_Write( wAddr, byData );

  /* Only $8000/$8001 move the MMC3 banks */
  if ( ( wAddr & 0xe000 ) == 0x8000 )
    Map555_Set_Banks();
}

void Map555_Apu( WORD wAddr, BYTE byData )
{
  if ( wAddr < 0x5000 )
    return;

  if ( wAddr & 0x0800 )
  {
    Map555_Reg[ ( wAddr >> 10 ) & 0x01 ] = byData;
    if ( !( Map555_Reg[ 0 ] & 0x08 ) )
    {
      Map555_Count = 0;
      Map555_Expired = 0;
    }
    Map555_Set_Banks();
  } else {
    Map555_Ram[ wAddr & ( MAP555_RAM_SIZE - 1 ) ] = byData;
  }
}

BYTE Map555_ReadApu( WORD wAddr )
{
  if ( wAddr < 0x5000 )
    return Map0_ReadApu( wAddr );

  if ( wAddr & 0x0800 )
    return 0x5c | ( Map555_Expired ? 0x80 : 0x00 );

  return Map555_Ram[ wAddr & ( MAP555_RAM_SIZE - 1 ) ];
}

/*-------------------------------------------------------------------*/
/*  Mapper 555 H-Sync Function                                       */
/*-------------------------------------------------------------------*/
void Map555_HSync()
{
  Map4_HSync();

  if ( ( Map555_Reg[ 0 ] & 0x08 ) && !Map555_Expired )
  {
    Map555_Count += STEP_PER_SCANLINE;
    if ( Map555_Count >= MAP555_TIMER_CYCLES )
      Map555_Expired = 1;
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 555 state save/load                                       */
/*-------------------------------------------------------------------*/

/* The MMC3 blob with the two registers and the timer appended to it. The
   2KB of PRG RAM goes into the state file as MapperPrgRam. */
struct Map555State
{
  BYTE  Reg[ 2 ];
  BYTE  Expired;
  DWORD Count;
};

static int Map555BlobSize()
{
  return Map4BlobSize() + (int)sizeof( Map555State );
}

static void Map555SaveBlob( BYTE *pBuf )
{
  Map555State *pState = (Map555State *)( pBuf + Map4BlobSize() );

  Map4SaveBlob( pBuf );
  InfoNES_MemoryCopy( pState->Reg, Map555_Reg, sizeof( Map555_Reg ) );
  pState->Expired = Map555_Expired;
  pState->Count = Map555_Count;
}

static void Map555LoadBlob( BYTE *pBuf )
{
  Map555State *pState = (Map555State *)( pBuf + Map4BlobSize() );

  Map4LoadBlob( pBuf );
  InfoNES_MemoryCopy( Map555_Reg, pState->Reg, sizeof( Map555_Reg ) );
  Map555_Expired = pState->Expired;
  Map555_Count = pState->Count;

  /* Map4LoadBlob has just put the plain MMC3 mapping back. */
  Map555_Set_Banks();
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 555                                            */
/*-------------------------------------------------------------------*/
void Map555_Init()
{
  /* WRAM at $6000 and the scanline IRQ come from MMC3. */
  Map4_Init();

  MapperInit = Map555_Init;
  MapperWrite = Map555_Write;
  MapperApu = Map555_Apu;
  MapperReadApu = Map555_ReadApu;
  MapperHSync = Map555_HSync;

  MapperBlobSize = Map555BlobSize;
  MapperSaveBlob = Map555SaveBlob;
  MapperLoadBlob = Map555LoadBlob;

  /* Allocated once and kept across a reset, like the RAM on the board;
     InfoNES_Fin frees it. */
  if ( !Map555_Ram )
  {
    Map555_Ram = (BYTE *)Frens::f_malloc( MAP555_RAM_SIZE );
    InfoNES_MemorySet( Map555_Ram, 0, MAP555_RAM_SIZE );
  }
  MapperPrgRam = Map555_Ram;
  MapperPrgRamSize = MAP555_RAM_SIZE;

  InfoNES_MemorySet( Map555_Reg, 0, sizeof( Map555_Reg ) );
  Map555_Count = 0;
  Map555_Expired = 0;

  Map555_Set_Banks();
}
