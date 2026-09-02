/*===================================================================*/
/*                                                                   */
/*                 Mapper 90 (J.Y. Company ASIC)                     */
/*                                                                   */
/*===================================================================*/

/*
 * The JY ASIC used on a large family of unlicensed carts. Ported from Mesen's
 * JyCompany, cross-checked against ares' unl-jy board.
 *
 * Register map, all decoded on A15-A12 + A2-A0 (so every mirror works):
 *
 *   $5000        DIP switches (reads 0)
 *   $5800/$5801  multiplier operands on write, 16-bit product on read
 *   $5803        one byte of scratch RAM
 *   $8000-$8003  PRG bank registers 0-3
 *   $9000-$9007  CHR bank low bytes
 *   $A000-$A007  CHR bank high bytes
 *   $B000-$B003  name table low bytes    (no effect on mapper 90, see below)
 *   $B004-$B007  name table high bytes   (no effect on mapper 90, see below)
 *   $C000        IRQ enable (bit 0) / acknowledge
 *   $C001        IRQ source, prescaler size, count direction
 *   $C002        IRQ acknowledge
 *   $C003        IRQ enable
 *   $C004        IRQ prescaler   (XORed with $C006)
 *   $C005        IRQ counter     (XORed with $C006)
 *   $C006        IRQ XOR register
 *   $C007        "funky mode" register (unimplemented here, and in Mesen
 *                and ares too - no known game uses it)
 *   $D000        PRG mode, CHR mode, name table control, $6000 mapping
 *   $D001        mirroring
 *   $D002        name table RAM select bit (no effect on mapper 90)
 *   $D003        CHR block mode / outer bank / CHR mirroring
 *
 * The one thing to know about mapper 90 specifically: it hard-wires the
 * ASIC's "advanced name table control" OFF. Quoting the nesdev wiki, "mapper
 * 211 behaves as though N were always set, and mapper 090 behaves as though N
 * were always clear" - so $B000-$B007 and $D002 are inert and the name tables
 * always come from CIRAM under $D001. That matters: Mortal Kombat 3 Special
 * writes $D000 = $3E (bit 5 set) at $C089, and the previous implementation
 * took that as "point the name tables at CHR ROM", aiming all four at CHR
 * pages 0x100-0x103. The PPU then read tile bitmaps as tile indices, which is
 * what garbled the title screen and produced the in-game artifacts.
 *
 * Keeping it out is also what keeps save states safe: state.cpp stores each
 * PPUBANK slot as an index relative to a known base, and slots 8-11 must not
 * point into CHR ROM.
 *
 * Mappers 209 and 211 are the same ASIC with different name table defaults.
 * They are deliberately not registered: they additionally need CHR-ROM-backed
 * name tables (which needs a copy-into-CIRAM path to stay save-state safe)
 * and, for 209, the $?FD8/$?FE8 CHR latch. Local test ROMs exist for both
 * (Power Rangers III/IV for 209; Donkey Kong Country 4, Tiny Toon Adventures 6
 * for 211) whenever someone picks that up. Map90_Use_Adv_Nt() is the hook.
 */

/* --- bank registers -------------------------------------------------- */
BYTE Map90_Prg_Reg[ 4 ];
BYTE Map90_Chr_Low_Reg[ 8 ];
BYTE Map90_Chr_High_Reg[ 8 ];
BYTE Map90_Nam_Low_Reg[ 4 ];
BYTE Map90_Nam_High_Reg[ 4 ];
BYTE Map90_Chr_Latch[ 2 ];

/* --- mode registers -------------------------------------------------- */
BYTE Map90_Prg_Mode;         /* $D000 bits 0-2 */
BYTE Map90_Prg_At_6000;      /* $D000 bit 7    */
BYTE Map90_Chr_Mode;         /* $D000 bits 3-4 */
BYTE Map90_Adv_Nt_Control;   /* $D000 bit 5, stored only */
BYTE Map90_Disable_Nt_Ram;   /* $D000 bit 6, stored only */
BYTE Map90_Mirror_Reg;       /* $D001 bits 0-1 */
BYTE Map90_Nt_Ram_Select;    /* $D002 bit 7, stored only */
BYTE Map90_Chr_Block_Mode;   /* $D003 bit 5, inverted */
BYTE Map90_Chr_Block;        /* $D003 bits 4,3,0 */
BYTE Map90_Mirror_Chr;       /* $D003 bit 7 */

/* --- IRQ ------------------------------------------------------------- */
BYTE Map90_IRQ_Enable;
BYTE Map90_IRQ_Source;       /* 0 CPU clock, 1 PPU A12 rise, 2 PPU read, 3 CPU write */
BYTE Map90_IRQ_Direction;    /* 0/3 stopped, 1 up, 2 down */
BYTE Map90_IRQ_Small_Pre;    /* 1 = /8, 0 = /256 */
BYTE Map90_IRQ_Funky;
BYTE Map90_IRQ_Funky_Reg;
BYTE Map90_IRQ_Prescaler;
BYTE Map90_IRQ_Counter;
BYTE Map90_IRQ_Xor;
BYTE Map90_IRQ_Request;

/* --- $5000-$5FFF ----------------------------------------------------- */
BYTE Map90_Mul1, Map90_Mul2, Map90_Reg_Ram;

/* --- derived in Map90_Init, not part of the blob ---------------------- */
static DWORD Map90_Prg_Pages, Map90_Prg_Mask;  /* mask 0 => fall back to % */
static DWORD Map90_Chr_Pages, Map90_Chr_Mask;  /* pages 0 => CHR RAM board */
static WORD Map90_Prg_6000_Page;               /* 0xFFFF = not mapped */

/* Mapper 90 never uses advanced name table control. See the file header. */
static BYTE Map90_Use_Adv_Nt( void )
{
  return 0;
}

/*-------------------------------------------------------------------*/
/*  Bank index folding                                               */
/*-------------------------------------------------------------------*/
/* A mask instead of % because this game rewrites $9000/$9004 from inside its
   IRQ handler on a tight cycle budget, and every CHR sync would otherwise
   cost eight divisions. All real sizes are powers of two; the % is kept for
   a pathological header. */
static DWORD Map90_Prg_Page( DWORD dwPage )
{
  return Map90_Prg_Mask ? ( dwPage & Map90_Prg_Mask ) : ( dwPage % Map90_Prg_Pages );
}

static DWORD Map90_Chr_Page( DWORD dwPage )
{
  return Map90_Chr_Mask ? ( dwPage & Map90_Chr_Mask ) : ( dwPage % Map90_Chr_Pages );
}

/*-------------------------------------------------------------------*/
/*  PRG mode 3 reverses the low seven bits of the bank number         */
/*-------------------------------------------------------------------*/
/* Mesen omits the bit-3 term, which under a 7-bit reversal maps to itself -
   it looks like an oversight. ares uses a rotate instead, which cannot be
   reconciled with "the bits are reversed" either. The complete reversal is
   implemented here; it is unreachable for every ROM in the local corpus
   (they all select PRG mode 2), so this is a documentation decision. */
static BYTE Map90_Invert_Prg_Bits( BYTE byReg )
{
  return (BYTE)( ( ( byReg & 0x01 ) << 6 ) | ( ( byReg & 0x02 ) << 4 )
               | ( ( byReg & 0x04 ) << 2 ) |   ( byReg & 0x08 )
               | ( ( byReg & 0x10 ) >> 2 ) | ( ( byReg & 0x20 ) >> 4 )
               | ( ( byReg & 0x40 ) >> 6 ) );
}

/*-------------------------------------------------------------------*/
/*  $6000-$7FFF window                                               */
/*-------------------------------------------------------------------*/
/* Copy the ROM page into SRAM rather than pointing SRAMBANK at the ROM
   image: K6502_Write stores into SRAMBANK *before* it calls MapperSram, so a
   SRAMBANK aimed at ROM would be written through - silent corruption of the
   PSRAM copy, and an undefined store into XIP flash space otherwise. The old
   code also never restored SRAMBANK once $D000 bit 7 was cleared. */
static void Map90_Sync_Prg_6000( DWORD dw6000 )
{
  if ( Map90_Prg_At_6000 )
  {
    WORD wPage = (WORD)Map90_Prg_Page( dw6000 );
    if ( wPage != Map90_Prg_6000_Page )
    {
      InfoNES_MemoryCopy( SRAM, ROMPAGE( wPage ), 0x2000 );
      Map90_Prg_6000_Page = wPage;
    }
  }
  else if ( Map90_Prg_6000_Page != 0xFFFF )
  {
    InfoNES_MemorySet( SRAM, 0, 0x2000 );   /* the ROM copy clobbered it */
    Map90_Prg_6000_Page = 0xFFFF;
  }
  SRAMBANK = SRAM;
}

/*-------------------------------------------------------------------*/
/*  Mapper 90 Sync Program Banks Function                            */
/*-------------------------------------------------------------------*/
void Map90_Sync_Prg_Banks( void )
{
  const BYTE byInvert = ( ( Map90_Prg_Mode & 0x03 ) == 0x03 );
  DWORD r[ 4 ], dwLast, dwBase, dw6000;
  int i;

  for ( i = 0; i < 4; i++ )
    r[ i ] = byInvert ? Map90_Invert_Prg_Bits( Map90_Prg_Reg[ i ] )
                      : Map90_Prg_Reg[ i ];

  switch ( Map90_Prg_Mode & 0x03 )
  {
    case 0:                                   /* one 32KB window */
      /* $D000 bit 2 selects "last bank comes from register 3" instead of the
         fixed last bank. The register is a 32KB bank index, so it shifts by
         2 - following ares here; Mesen treats it as an 8KB page index in
         this branch, which contradicts its own $6000 handling two lines on. */
      dwLast = ( Map90_Prg_Mode & 0x04 ) ? r[ 3 ] : 0x0F;
      dwBase = dwLast << 2;
      ROMBANK0 = ROMPAGE( Map90_Prg_Page( dwBase + 0 ) );
      ROMBANK1 = ROMPAGE( Map90_Prg_Page( dwBase + 1 ) );
      ROMBANK2 = ROMPAGE( Map90_Prg_Page( dwBase + 2 ) );
      ROMBANK3 = ROMPAGE( Map90_Prg_Page( dwBase + 3 ) );
      dw6000 = ( r[ 3 ] << 2 ) + 3;
      break;

    case 1:                                   /* two 16KB windows */
      dwLast = ( Map90_Prg_Mode & 0x04 ) ? r[ 3 ] : 0x1F;
      ROMBANK0 = ROMPAGE( Map90_Prg_Page( ( r[ 1 ] << 1 ) + 0 ) );
      ROMBANK1 = ROMPAGE( Map90_Prg_Page( ( r[ 1 ] << 1 ) + 1 ) );
      ROMBANK2 = ROMPAGE( Map90_Prg_Page( ( dwLast << 1 ) + 0 ) );
      ROMBANK3 = ROMPAGE( Map90_Prg_Page( ( dwLast << 1 ) + 1 ) );
      dw6000 = ( r[ 3 ] << 1 ) + 1;
      break;

    default:                                  /* four 8KB windows (modes 2, 3) */
      dwLast = ( Map90_Prg_Mode & 0x04 ) ? r[ 3 ] : 0x3F;
      ROMBANK0 = ROMPAGE( Map90_Prg_Page( r[ 0 ] ) );
      ROMBANK1 = ROMPAGE( Map90_Prg_Page( r[ 1 ] ) );
      ROMBANK2 = ROMPAGE( Map90_Prg_Page( r[ 2 ] ) );
      ROMBANK3 = ROMPAGE( Map90_Prg_Page( dwLast ) );
      dw6000 = r[ 3 ];
      break;
  }

  Map90_Sync_Prg_6000( dw6000 );
}

/*-------------------------------------------------------------------*/
/*  CHR bank number assembly                                         */
/*-------------------------------------------------------------------*/
static DWORD Map90_Get_Chr_Reg( int nIdx )
{
  /* $D003 bit 7: in the 2KB and 1KB modes, slots 2/3 mirror slots 0/1. */
  if ( Map90_Chr_Mode >= 2 && Map90_Mirror_Chr && ( nIdx == 2 || nIdx == 3 ) )
    nIdx -= 2;

  if ( Map90_Chr_Block_Mode )
  {
    /* Outer-bank mode: the low register supplies only the bits below the
       block boundary, $D003's 3-bit block supplies the rest, and the high
       registers are ignored entirely. */
    static const BYTE byMask[ 4 ] = { 0x1F, 0x3F, 0x7F, 0xFF };
    static const BYTE byShift[ 4 ] = { 5, 6, 7, 8 };
    return (DWORD)( Map90_Chr_Low_Reg[ nIdx ] & byMask[ Map90_Chr_Mode ] )
         | ( (DWORD)Map90_Chr_Block << byShift[ Map90_Chr_Mode ] );
  }

  /* Linear mode: a 16-bit bank number from the low+high register pair. This
     is the power-on default in both references, and five local mapper-90
     games never write $D003 at all, so they depend on it. */
  return (DWORD)Map90_Chr_Low_Reg[ nIdx ]
       | ( (DWORD)Map90_Chr_High_Reg[ nIdx ] << 8 );
}

/*-------------------------------------------------------------------*/
/*  Mapper 90 Sync Char Banks Function                               */
/*-------------------------------------------------------------------*/
void Map90_Sync_Chr_Banks( void )
{
  DWORD dwBase;
  int i;

  /* No CHR ROM. The old code did "% (byVRomSize << 3)" here, i.e. divided by
     zero. No real mapper-90 board has CHR RAM, but a malformed header must
     not take the emulator down. */
  if ( !Map90_Chr_Pages )
  {
    for ( i = 0; i < 8; i++ )
      PPUBANK[ i ] = CRAMPAGE( i );
    InfoNES_SetupChr();
    return;
  }

  switch ( Map90_Chr_Mode )
  {
    case 0:                                   /* one 8KB bank */
      dwBase = Map90_Get_Chr_Reg( 0 ) << 3;
      for ( i = 0; i < 8; i++ )
        PPUBANK[ i ] = VROMPAGE( Map90_Chr_Page( dwBase + i ) );
      break;

    case 1:                                   /* two 4KB banks */
      dwBase = Map90_Get_Chr_Reg( Map90_Chr_Latch[ 0 ] ) << 2;
      for ( i = 0; i < 4; i++ )
        PPUBANK[ i ] = VROMPAGE( Map90_Chr_Page( dwBase + i ) );
      dwBase = Map90_Get_Chr_Reg( Map90_Chr_Latch[ 1 ] ) << 2;
      for ( i = 0; i < 4; i++ )
        PPUBANK[ 4 + i ] = VROMPAGE( Map90_Chr_Page( dwBase + i ) );
      break;

    case 2:                                   /* four 2KB banks */
      for ( i = 0; i < 4; i++ )
      {
        dwBase = Map90_Get_Chr_Reg( i << 1 ) << 1;
        PPUBANK[ ( i << 1 ) + 0 ] = VROMPAGE( Map90_Chr_Page( dwBase + 0 ) );
        PPUBANK[ ( i << 1 ) + 1 ] = VROMPAGE( Map90_Chr_Page( dwBase + 1 ) );
      }
      break;

    default:                                  /* eight 1KB banks */
      for ( i = 0; i < 8; i++ )
        PPUBANK[ i ] = VROMPAGE( Map90_Chr_Page( Map90_Get_Chr_Reg( i ) ) );
      break;
  }
  InfoNES_SetupChr();
}

/*-------------------------------------------------------------------*/
/*  Mapper 90 Sync Mirror Function                                   */
/*-------------------------------------------------------------------*/
void Map90_Sync_Mirror( void )
{
  if ( Map90_Use_Adv_Nt() )
  {
    /* Reserved for mappers 209/211; see the file header. */
    return;
  }

  switch ( Map90_Mirror_Reg )
  {
    case 0x00: InfoNES_Mirroring( 1 ); break;   /* vertical             */
    case 0x01: InfoNES_Mirroring( 0 ); break;   /* horizontal           */
    case 0x02: InfoNES_Mirroring( 3 ); break;   /* one screen, $2000    */
    default:   InfoNES_Mirroring( 2 ); break;   /* one screen, $2400    */
  }
}

/*-------------------------------------------------------------------*/
/*  IRQ                                                              */
/*-------------------------------------------------------------------*/
static void Map90_Irq_Ack( void )
{
  Map90_IRQ_Enable = 0;
  Map90_IRQ_Request = 0;
  /* Drop an IRQ that was asserted but not yet taken. Without this an IRQ
     raised while the CPU has I set survives the game's own STA $C002 and is
     taken later, landing the handler's CHR and scroll writes at an arbitrary
     scanline. Same shape as Map4_Write's $E000 case. */
  IRQ_State = IRQ_Wiring;
}

/* Advance the prescaler and counter by dwTicks source clocks in one step.
   Closed form rather than a loop, so a scanline's worth of CPU cycles (114)
   costs the same as a scanline's worth of A12 rises (8), and the residue is
   carried into the next scanline instead of being lost. */
static void Map90_Tick_Irq( DWORD dwTicks )
{
  DWORD dwMask, dwPeriod, dwPre, dwCnt, dwClocks, dwWraps = 0;

  if ( !dwTicks )
    return;
  if ( Map90_IRQ_Direction != 1 && Map90_IRQ_Direction != 2 )
    return;                                   /* 0 and 3 halt the counter */

  dwMask = Map90_IRQ_Small_Pre ? 0x07u : 0xFFu;
  dwPeriod = dwMask + 1;
  dwPre = Map90_IRQ_Prescaler & dwMask;
  dwCnt = Map90_IRQ_Counter;

  if ( Map90_IRQ_Direction == 2 )             /* down */
  {
    dwClocks = ( dwTicks + dwMask - dwPre ) / dwPeriod;
    dwPre = ( dwPre - dwTicks ) & dwMask;
    if ( dwClocks )
    {
      dwWraps = ( dwClocks + 0xFF - dwCnt ) >> 8;   /* fires on 0x00 -> 0xFF */
      dwCnt = ( dwCnt - dwClocks ) & 0xFF;
    }
  }
  else                                        /* up */
  {
    dwClocks = ( dwTicks + dwPre ) / dwPeriod;
    dwPre = ( dwPre + dwTicks ) & dwMask;
    if ( dwClocks )
    {
      dwWraps = ( dwClocks + dwCnt ) >> 8;          /* fires on 0xFF -> 0x00 */
      dwCnt = ( dwCnt + dwClocks ) & 0xFF;
    }
  }

  /* $C001 bit 2 only masks the low three bits; the upper five are preserved
     across a /8 <-> /256 change, as in both references. */
  Map90_IRQ_Prescaler = (BYTE)( ( Map90_IRQ_Prescaler & ~dwMask ) | dwPre );
  Map90_IRQ_Counter = (BYTE)dwCnt;

  if ( dwWraps && Map90_IRQ_Enable )
    Map90_IRQ_Request = 0xFF;
}

/* How many source clocks a scanline is worth.
   The whole local JY corpus writes $C001 = $85: count down, /8 prescaler,
   PPU A12 source. With the usual $2000 = $88 (BG at $0000, sprites at $1000)
   the PPU raises A12 exactly 8 times per rendered scanline, so /8 makes this
   an ordinary MMC3-style scanline counter - not an approximation. Only
   sub-scanline delivery is lost, because InfoNES calls MapperHSync once per
   line. */
static DWORD Map90_Ticks_This_Line( void )
{
  switch ( Map90_IRQ_Source )
  {
    case 0:                                   /* CPU M2 rise */
      /* The CPU runs through vblank too, so no scanline gate. The rate is
         exact; the phase inside the line is not. */
      return STEP_PER_SCANLINE;

    case 3:                                   /* CPU write */
      /* No per-cycle write visibility in this core. One write per eight CPU
         cycles is the long-run average for 6502 code. No known JY game
         selects this source. */
      return STEP_PER_SCANLINE >> 3;

    case 1:                                   /* PPU A12 rise */
      if ( PPU_Scanline > 239 )
        return 0;
      if ( !( PPU_R1 & ( R1_SHOW_SCR | R1_SHOW_SP ) ) )
        return 0;
      /* One rise per BG tile fetch when the BG pattern table is at $1000
         (32 visible + 2 prefetch), plus one per sprite fetch slot when the
         sprite table is at $1000. */
      return ( ( PPU_R0 & R0_BG_ADDR ) ? 34u : 0u )
           + ( ( PPU_R0 & R0_SP_ADDR ) ? 8u : 0u );

    default:                                  /* PPU read */
      if ( PPU_Scanline > 239 )
        return 0;
      if ( !( PPU_R1 & ( R1_SHOW_SCR | R1_SHOW_SP ) ) )
        return 0;
      return 170;                             /* 34 + 8 fetch groups, 4 reads each */
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 90 H-Sync Function                                        */
/*-------------------------------------------------------------------*/
void Map90_HSync()
{
  Map90_Tick_Irq( Map90_Ticks_This_Line() );

  /* The JY /IRQ line is level-held until $C000 bit 0 = 0 or $C002 clears it,
     but IRQ_REQ is self-clearing in this core, so re-assert while the request
     stands - same shape as Map4_HSync. */
  if ( Map90_IRQ_Request )
  {
    IRQ_REQ;
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 90 Write to APU Function ($4018-$5FFF)                    */
/*-------------------------------------------------------------------*/
void Map90_Apu( WORD wAddr, BYTE byData )
{
  switch ( wAddr & 0xF803 )
  {
    case 0x5800: Map90_Mul1 = byData; break;
    case 0x5801: Map90_Mul2 = byData; break;
    case 0x5803: Map90_Reg_Ram = byData; break;
    default: break;
  }
}

/*-------------------------------------------------------------------*/
/*  Mapper 90 Read from APU Function                                 */
/*-------------------------------------------------------------------*/
BYTE Map90_ReadApu( WORD wAddr )
{
  switch ( wAddr & 0xF803 )
  {
    /* DIP switches. Mortal Kombat 3 Special does LDA $5000 / AND #$80 during
       reset and needs bit 7 clear. The old code returned the multiplier
       product here, because it had the operands at $5000/$5001. */
    case 0x5000: return 0x00;
    case 0x5800: return (BYTE)( ( Map90_Mul1 * Map90_Mul2 ) & 0xFF );
    case 0x5801: return (BYTE)( ( ( Map90_Mul1 * Map90_Mul2 ) >> 8 ) & 0xFF );
    case 0x5803: return Map90_Reg_Ram;
    default: break;
  }
  return (BYTE)( wAddr >> 8 );                /* open bus stand-in */
}

/*-------------------------------------------------------------------*/
/*  Mapper 90 Write Function                                         */
/*-------------------------------------------------------------------*/
void Map90_Write( WORD wAddr, BYTE byData )
{
  /* A15-A12 + A2-A0, so every register mirror decodes. The old exact-address
     switch dropped them all, along with six registers this game uses. */
  switch ( wAddr & 0xF007 )
  {
    case 0x8000: case 0x8001: case 0x8002: case 0x8003:
    case 0x8004: case 0x8005: case 0x8006: case 0x8007:
      Map90_Prg_Reg[ wAddr & 0x03 ] = byData & 0x7F;
      Map90_Sync_Prg_Banks();
      return;

    case 0x9000: case 0x9001: case 0x9002: case 0x9003:
    case 0x9004: case 0x9005: case 0x9006: case 0x9007:
      Map90_Chr_Low_Reg[ wAddr & 0x07 ] = byData;
      Map90_Sync_Chr_Banks();
      return;

    case 0xA000: case 0xA001: case 0xA002: case 0xA003:
    case 0xA004: case 0xA005: case 0xA006: case 0xA007:
      Map90_Chr_High_Reg[ wAddr & 0x07 ] = byData;
      Map90_Sync_Chr_Banks();
      return;

    /* Stored so the save state is complete and so 209/211 can be added
       later; inert on mapper 90. */
    case 0xB000: case 0xB001: case 0xB002: case 0xB003:
      Map90_Nam_Low_Reg[ wAddr & 0x03 ] = byData;
      return;

    case 0xB004: case 0xB005: case 0xB006: case 0xB007:
      Map90_Nam_High_Reg[ wAddr & 0x03 ] = byData;
      return;

    case 0xC000:
      if ( byData & 0x01 )
        Map90_IRQ_Enable = 1;
      else
        Map90_Irq_Ack();
      return;

    case 0xC001:
      Map90_IRQ_Direction = ( byData >> 6 ) & 0x03;
      Map90_IRQ_Funky = ( byData >> 3 ) & 0x01;
      Map90_IRQ_Small_Pre = ( byData >> 2 ) & 0x01;
      Map90_IRQ_Source = byData & 0x03;
      return;

    case 0xC002: Map90_Irq_Ack(); return;
    case 0xC003: Map90_IRQ_Enable = 1; return;
    case 0xC004: Map90_IRQ_Prescaler = byData ^ Map90_IRQ_Xor; return;
    case 0xC005: Map90_IRQ_Counter = byData ^ Map90_IRQ_Xor; return;
    case 0xC006: Map90_IRQ_Xor = byData; return;
    case 0xC007: Map90_IRQ_Funky_Reg = byData; return;

    case 0xD000:
      Map90_Prg_Mode = byData & 0x07;
      Map90_Chr_Mode = ( byData >> 3 ) & 0x03;
      Map90_Adv_Nt_Control = ( byData >> 5 ) & 0x01;
      Map90_Disable_Nt_Ram = ( byData >> 6 ) & 0x01;
      Map90_Prg_At_6000 = ( byData >> 7 ) & 0x01;
      Map90_Sync_Prg_Banks();
      Map90_Sync_Chr_Banks();
      Map90_Sync_Mirror();
      return;

    case 0xD001:
      Map90_Mirror_Reg = byData & 0x03;
      Map90_Sync_Mirror();
      return;

    case 0xD002:
      Map90_Nt_Ram_Select = byData & 0x80;
      return;

    case 0xD003:
      Map90_Mirror_Chr = ( byData & 0x80 ) ? 1 : 0;
      Map90_Chr_Block_Mode = ( byData & 0x20 ) ? 0 : 1;   /* inverted */
      Map90_Chr_Block = (BYTE)( ( ( byData & 0x18 ) >> 2 ) | ( byData & 0x01 ) );
      Map90_Sync_Chr_Banks();
      return;

    default:
      /* $D004-$D007 and $E000-$FFFF carry no register. ares also decodes an
         outer PRG bank from $D003 bits 1-2 that Mesen does not; no local ROM
         needs more than 512KB of PRG, so it is left out. */
      return;
  }
}

/*-------------------------------------------------------------------*/
/*  Save state support                                               */
/*-------------------------------------------------------------------*/
struct Map90State
{
  BYTE Prg_Reg[ 4 ];
  BYTE Chr_Low_Reg[ 8 ];
  BYTE Chr_High_Reg[ 8 ];
  BYTE Nam_Low_Reg[ 4 ];
  BYTE Nam_High_Reg[ 4 ];
  BYTE Chr_Latch[ 2 ];

  BYTE Prg_Mode, Prg_At_6000;
  BYTE Chr_Mode, Chr_Block_Mode, Chr_Block, Mirror_Chr;
  BYTE Mirror_Reg, Adv_Nt_Control, Disable_Nt_Ram, Nt_Ram_Select;

  BYTE IRQ_Enable, IRQ_Source, IRQ_Direction, IRQ_Small_Pre;
  BYTE IRQ_Funky, IRQ_Funky_Reg;
  BYTE IRQ_Prescaler, IRQ_Counter, IRQ_Xor, IRQ_Request;

  BYTE Mul1, Mul2, Reg_Ram;
  BYTE Pad;
};

static int Map90BlobSize()
{
  return sizeof( struct Map90State );
}

static void Map90SaveBlob( BYTE *pBuf )
{
  struct Map90State *pState = (struct Map90State *)pBuf;
  int i;

  for ( i = 0; i < 4; i++ )
  {
    pState->Prg_Reg[ i ] = Map90_Prg_Reg[ i ];
    pState->Nam_Low_Reg[ i ] = Map90_Nam_Low_Reg[ i ];
    pState->Nam_High_Reg[ i ] = Map90_Nam_High_Reg[ i ];
  }
  for ( i = 0; i < 8; i++ )
  {
    pState->Chr_Low_Reg[ i ] = Map90_Chr_Low_Reg[ i ];
    pState->Chr_High_Reg[ i ] = Map90_Chr_High_Reg[ i ];
  }
  pState->Chr_Latch[ 0 ] = Map90_Chr_Latch[ 0 ];
  pState->Chr_Latch[ 1 ] = Map90_Chr_Latch[ 1 ];

  pState->Prg_Mode = Map90_Prg_Mode;
  pState->Prg_At_6000 = Map90_Prg_At_6000;
  pState->Chr_Mode = Map90_Chr_Mode;
  pState->Chr_Block_Mode = Map90_Chr_Block_Mode;
  pState->Chr_Block = Map90_Chr_Block;
  pState->Mirror_Chr = Map90_Mirror_Chr;
  pState->Mirror_Reg = Map90_Mirror_Reg;
  pState->Adv_Nt_Control = Map90_Adv_Nt_Control;
  pState->Disable_Nt_Ram = Map90_Disable_Nt_Ram;
  pState->Nt_Ram_Select = Map90_Nt_Ram_Select;

  pState->IRQ_Enable = Map90_IRQ_Enable;
  pState->IRQ_Source = Map90_IRQ_Source;
  pState->IRQ_Direction = Map90_IRQ_Direction;
  pState->IRQ_Small_Pre = Map90_IRQ_Small_Pre;
  pState->IRQ_Funky = Map90_IRQ_Funky;
  pState->IRQ_Funky_Reg = Map90_IRQ_Funky_Reg;
  pState->IRQ_Prescaler = Map90_IRQ_Prescaler;
  pState->IRQ_Counter = Map90_IRQ_Counter;
  pState->IRQ_Xor = Map90_IRQ_Xor;
  pState->IRQ_Request = Map90_IRQ_Request;

  pState->Mul1 = Map90_Mul1;
  pState->Mul2 = Map90_Mul2;
  pState->Reg_Ram = Map90_Reg_Ram;
  pState->Pad = 0;
}

static void Map90LoadBlob( BYTE *pBuf )
{
  struct Map90State *pState = (struct Map90State *)pBuf;
  int i;

  for ( i = 0; i < 4; i++ )
  {
    Map90_Prg_Reg[ i ] = pState->Prg_Reg[ i ];
    Map90_Nam_Low_Reg[ i ] = pState->Nam_Low_Reg[ i ];
    Map90_Nam_High_Reg[ i ] = pState->Nam_High_Reg[ i ];
  }
  for ( i = 0; i < 8; i++ )
  {
    Map90_Chr_Low_Reg[ i ] = pState->Chr_Low_Reg[ i ];
    Map90_Chr_High_Reg[ i ] = pState->Chr_High_Reg[ i ];
  }
  Map90_Chr_Latch[ 0 ] = pState->Chr_Latch[ 0 ];
  Map90_Chr_Latch[ 1 ] = pState->Chr_Latch[ 1 ];

  Map90_Prg_Mode = pState->Prg_Mode;
  Map90_Prg_At_6000 = pState->Prg_At_6000;
  Map90_Chr_Mode = pState->Chr_Mode;
  Map90_Chr_Block_Mode = pState->Chr_Block_Mode;
  Map90_Chr_Block = pState->Chr_Block;
  Map90_Mirror_Chr = pState->Mirror_Chr;
  Map90_Mirror_Reg = pState->Mirror_Reg;
  Map90_Adv_Nt_Control = pState->Adv_Nt_Control;
  Map90_Disable_Nt_Ram = pState->Disable_Nt_Ram;
  Map90_Nt_Ram_Select = pState->Nt_Ram_Select;

  Map90_IRQ_Enable = pState->IRQ_Enable;
  Map90_IRQ_Source = pState->IRQ_Source;
  Map90_IRQ_Direction = pState->IRQ_Direction;
  Map90_IRQ_Small_Pre = pState->IRQ_Small_Pre;
  Map90_IRQ_Funky = pState->IRQ_Funky;
  Map90_IRQ_Funky_Reg = pState->IRQ_Funky_Reg;
  Map90_IRQ_Prescaler = pState->IRQ_Prescaler;
  Map90_IRQ_Counter = pState->IRQ_Counter;
  Map90_IRQ_Xor = pState->IRQ_Xor;
  Map90_IRQ_Request = pState->IRQ_Request;

  Map90_Mul1 = pState->Mul1;
  Map90_Mul2 = pState->Mul2;
  Map90_Reg_Ram = pState->Reg_Ram;

  /* Rebuilding from the registers is not optional: state.cpp stores each
     PPUBANK slot as a BYTE 1KB index, so a 512KB CHR ROM's pages 256-511
     alias on save. MapperLoadBlob runs last, after restorePPUBanks() and the
     mirroring call, so this is what repairs them. */
  Map90_Prg_6000_Page = 0xFFFF;
  Map90_Sync_Prg_Banks();
  Map90_Sync_Chr_Banks();
  Map90_Sync_Mirror();
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 90                                             */
/*-------------------------------------------------------------------*/
void Map90_Init()
{
  MapperBlobSize = Map90BlobSize;
  MapperSaveBlob = Map90SaveBlob;
  MapperLoadBlob = Map90LoadBlob;

  MapperInit = Map90_Init;
  MapperWrite = Map90_Write;
  MapperSram = Map0_Sram;
  MapperApu = Map90_Apu;
  MapperReadApu = Map90_ReadApu;
  MapperVSync = Map0_VSync;
  MapperHSync = Map90_HSync;
  /* Deliberately the stub: MapperPPU only sees background pattern fetches,
     and this game's background is at $0000, so it would report no A12 rises
     at all - and enabling it puts ~33 indirect calls per scanline back into
     the hottest loop in the emulator. */
  MapperPPU = Map0_PPU;
  MapperRenderScreen = Map0_RenderScreen;

  Map90_Prg_Pages = (DWORD)NesHeader.byRomSize << 1;
  if ( !Map90_Prg_Pages )
    Map90_Prg_Pages = 1;
  Map90_Prg_Mask = ( ( Map90_Prg_Pages & ( Map90_Prg_Pages - 1 ) ) == 0 )
                   ? Map90_Prg_Pages - 1 : 0;

  Map90_Chr_Pages = (DWORD)NesHeader.byVRomSize << 3;   /* 0 => CHR RAM */
  Map90_Chr_Mask = ( Map90_Chr_Pages &&
                     ( Map90_Chr_Pages & ( Map90_Chr_Pages - 1 ) ) == 0 )
                   ? Map90_Chr_Pages - 1 : 0;

  /* Map90_Init is the power-on and the reset entry point, so every register
     has to be cleared here. The old file left eight mode and multiplier
     globals untouched, so they leaked across resets and across games. */
  InfoNES_MemorySet( Map90_Prg_Reg, 0, sizeof Map90_Prg_Reg );
  InfoNES_MemorySet( Map90_Chr_Low_Reg, 0, sizeof Map90_Chr_Low_Reg );
  InfoNES_MemorySet( Map90_Chr_High_Reg, 0, sizeof Map90_Chr_High_Reg );
  InfoNES_MemorySet( Map90_Nam_Low_Reg, 0, sizeof Map90_Nam_Low_Reg );
  InfoNES_MemorySet( Map90_Nam_High_Reg, 0, sizeof Map90_Nam_High_Reg );

  Map90_Chr_Latch[ 0 ] = 0;
  Map90_Chr_Latch[ 1 ] = 4;

  Map90_Prg_Mode = 0;
  Map90_Prg_At_6000 = 0;
  Map90_Chr_Mode = 0;
  Map90_Chr_Block = 0;
  Map90_Mirror_Chr = 0;
  Map90_Adv_Nt_Control = 0;
  Map90_Disable_Nt_Ram = 0;
  Map90_Nt_Ram_Select = 0;
  Map90_Mirror_Reg = 0;

  /* Not the literal reading of "$D003 = 0 means bit 5 clear means block mode
     on": Mesen's InitMapper and ares' power() both start with block mode OFF,
     and several local mapper-90 games never write $D003 at all, so they
     depend on this default. */
  Map90_Chr_Block_Mode = 0;

  Map90_IRQ_Enable = 0;
  Map90_IRQ_Request = 0;
  Map90_IRQ_Source = 0;
  Map90_IRQ_Direction = 0;                    /* stopped until $C001 */
  Map90_IRQ_Small_Pre = 0;
  Map90_IRQ_Funky = 0;
  Map90_IRQ_Funky_Reg = 0;
  Map90_IRQ_Prescaler = 0;
  Map90_IRQ_Counter = 0;
  Map90_IRQ_Xor = 0;

  Map90_Mul1 = 0;
  Map90_Mul2 = 0;
  Map90_Reg_Ram = 0;

  Map90_Prg_6000_Page = 0xFFFF;
  SRAMBANK = SRAM;

  /* PRG mode 0 puts the last 32KB at $8000, so the reset vector is reachable. */
  Map90_Sync_Prg_Banks();
  Map90_Sync_Chr_Banks();
  Map90_Sync_Mirror();

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 );
}
