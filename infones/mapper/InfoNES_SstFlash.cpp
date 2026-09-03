/*===================================================================*/
/*                                                                   */
/*   SST39SF040 flash emulation, shared by mappers 30 and 111        */
/*                                                                   */
/*===================================================================*/

/*
 * UNROM 512 (mapper 30) and GTROM / Cheapocabra (mapper 111) both put the
 * game in a flash chip the cartridge can rewrite itself, and both drive it
 * with the usual JEDEC command sequences:
 *
 *   $5555 = AA, $2AAA = 55, $5555 = 90     enter software ID mode
 *   $5555 = AA, $2AAA = 55, $5555 = A0     arm byte program, next write is data
 *   $5555 = AA, $2AAA = 55, $5555 = 80,
 *   $5555 = AA, $2AAA = 55, $xxxx = 30     erase the 4KB sector holding $xxxx
 *   anything  = F0                          leave software ID mode
 *
 * In software ID mode the chip answers $BF (SST) at offset 0 and $B7
 * (39SF040) at offset 1 of every 512-byte window, and $FF elsewhere.
 *
 * Two things shape this implementation:
 *
 *  - There is no mapper hook on CPU reads from $8000-$FFFF. That path
 *    (K6502_rw.h) is the hottest in the emulator and adding a test to it has
 *    caused a visible regression before. So software ID mode is served by
 *    pointing the mapper's PRG bank pointers at a prepared 8KB page instead.
 *
 *  - The PRG image itself may be read-only: it sits in XIP flash on boards
 *    without PSRAM. Programming therefore goes into an 8KB RAM shadow of one
 *    PRG page, which the mapper maps in place of the real page. That also
 *    keeps the data-poll loop a flash routine ends with from spinning
 *    forever - the game reads back the value it just wrote on the first try.
 *
 * Neither buffer is allocated until a game actually issues a flash command,
 * and both allocation failures degrade to "no flash chip" rather than
 * failing. Flash writes live for the session only: nothing writes the
 * modified image back to the card.
 */

/* One flash mapper is active at a time, so one of each is enough. */
static BYTE *SstFlash_IdPage;   /* 8KB, the software ID pattern           */
static BYTE *SstFlash_Shadow;   /* 8KB RAM copy of one programmed PRG page */

#define SSTFLASH_PAGE_SIZE 0x2000
#define SSTFLASH_NO_PAGE   0xFFFFFFFFu

/* Command sequence positions */
#define SSTFLASH_IDLE        0
#define SSTFLASH_GOT_AA      1
#define SSTFLASH_GOT_55      2
#define SSTFLASH_ERASE_AA    3
#define SSTFLASH_ERASE_55    4
#define SSTFLASH_ERASE_CMD   5
#define SSTFLASH_PROGRAM_ARM 6

void SstFlash_Init(struct SstFlash_tag *pFlash, void (*pfnResync)(), BYTE bySlotMaskId)
{
  pFlash->pfnResync = pfnResync;
  pFlash->bySlotMaskId = bySlotMaskId;
  pFlash->byStep = SSTFLASH_IDLE;
  pFlash->byIdMode = 0;
  pFlash->dwShadowPage = SSTFLASH_NO_PAGE;
}

/*-------------------------------------------------------------------*/
/*  The 8KB page the CPU sees while software ID mode is active        */
/*-------------------------------------------------------------------*/
static BYTE *SstFlash_GetIdPage()
{
  if ( !SstFlash_IdPage )
  {
    SstFlash_IdPage = (BYTE *)Frens::f_malloc( SSTFLASH_PAGE_SIZE );
    if ( SstFlash_IdPage )
    {
      /* The chip decodes only the low 9 address bits in ID mode, so the
         512-byte answer repeats through the whole window. */
      InfoNES_MemorySet( SstFlash_IdPage, 0xFF, SSTFLASH_PAGE_SIZE );
      for ( int nBlock = 0; nBlock < SSTFLASH_PAGE_SIZE; nBlock += 0x200 )
      {
        SstFlash_IdPage[ nBlock + 0 ] = 0xBF;  /* manufacturer: SST      */
        SstFlash_IdPage[ nBlock + 1 ] = 0xB7;  /* device: SST39SF040     */
      }
    }
  }
  return SstFlash_IdPage;
}

/*-------------------------------------------------------------------*/
/*  Pointer the mapper must install in ROMBANK[nSlot] for PRG page a  */
/*-------------------------------------------------------------------*/
BYTE *SstFlash_Page( struct SstFlash_tag *pFlash, DWORD dwPage, int nSlot )
{
  if ( pFlash->byIdMode && SstFlash_IdPage &&
       ( pFlash->bySlotMaskId & ( 1u << nSlot ) ) )
    return SstFlash_IdPage;

  if ( SstFlash_Shadow && pFlash->dwShadowPage == dwPage )
    return SstFlash_Shadow;

  return ROMPAGE( dwPage );
}

/*-------------------------------------------------------------------*/
/*  Byte program: NOR flash can only clear bits, so AND               */
/*-------------------------------------------------------------------*/
static void SstFlash_Program( struct SstFlash_tag *pFlash, DWORD dwOfs, BYTE byData )
{
  DWORD dwPage = dwOfs / SSTFLASH_PAGE_SIZE;

  if ( !SstFlash_Shadow )
  {
    SstFlash_Shadow = (BYTE *)Frens::f_malloc( SSTFLASH_PAGE_SIZE );
    if ( !SstFlash_Shadow )
      return;                       /* no room: the write is simply lost */
    pFlash->dwShadowPage = SSTFLASH_NO_PAGE;
  }

  if ( pFlash->dwShadowPage != dwPage )
  {
    /* Only one page is shadowed at a time. A game that programs across a
       page boundary loses the earlier page's writes, which is a limitation
       worth knowing about but costs 8KB instead of 512KB. */
    InfoNES_MemoryCopy( SstFlash_Shadow, ROMPAGE( dwPage ), SSTFLASH_PAGE_SIZE );
    pFlash->dwShadowPage = dwPage;
  }

  SstFlash_Shadow[ dwOfs & ( SSTFLASH_PAGE_SIZE - 1 ) ] &= byData;
  pFlash->pfnResync();              /* make the shadow visible immediately */
}

/*-------------------------------------------------------------------*/
/*  Sector erase: 4KB of 0xFF                                         */
/*-------------------------------------------------------------------*/
static void SstFlash_Erase( struct SstFlash_tag *pFlash, DWORD dwOfs )
{
  DWORD dwSector = dwOfs & ~0xFFFu;
  DWORD dwPage = dwSector / SSTFLASH_PAGE_SIZE;

  if ( !SstFlash_Shadow )
  {
    SstFlash_Shadow = (BYTE *)Frens::f_malloc( SSTFLASH_PAGE_SIZE );
    if ( !SstFlash_Shadow )
      return;
    pFlash->dwShadowPage = SSTFLASH_NO_PAGE;
  }

  if ( pFlash->dwShadowPage != dwPage )
  {
    InfoNES_MemoryCopy( SstFlash_Shadow, ROMPAGE( dwPage ), SSTFLASH_PAGE_SIZE );
    pFlash->dwShadowPage = dwPage;
  }

  InfoNES_MemorySet( SstFlash_Shadow + ( dwSector & ( SSTFLASH_PAGE_SIZE - 1 ) ),
                     0xFF, 0x1000 );
  pFlash->pfnResync();
}

/*-------------------------------------------------------------------*/
/*  Command state machine                                             */
/*-------------------------------------------------------------------*/
/*
 * wAddr is the CPU address, dwOfs the PRG byte offset it currently maps to
 * (only the mapper knows that: mapper 30 uses (bank << 14) | (addr & 0x3FFF),
 * mapper 111 (bank << 15) | (addr & 0x7FFF)).
 *
 * Returns true when the write was consumed as a flash command cycle.
 * The unlock addresses $5555 and $2AAA are matched on the low 12 bits, which
 * covers mapper 30's $9555/$AAAA and mapper 111's $D555/$AAAA alike.
 */
bool SstFlash_Write( struct SstFlash_tag *pFlash, WORD wAddr, BYTE byData, DWORD dwOfs )
{
  WORD wCmd = wAddr & 0x0FFF;

  /* Software ID exit is accepted at any address and in any state. */
  if ( byData == 0xF0 )
  {
    pFlash->byStep = SSTFLASH_IDLE;
    if ( pFlash->byIdMode )
    {
      pFlash->byIdMode = 0;
      pFlash->pfnResync();
    }
    return true;
  }

  switch ( pFlash->byStep )
  {
    case SSTFLASH_IDLE:
      if ( byData == 0xAA && wCmd == 0x555 )
      {
        pFlash->byStep = SSTFLASH_GOT_AA;
        return true;
      }
      return false;

    case SSTFLASH_GOT_AA:
      if ( byData == 0x55 && wCmd == 0xAAA )
      {
        pFlash->byStep = SSTFLASH_GOT_55;
        return true;
      }
      pFlash->byStep = SSTFLASH_IDLE;
      return false;

    case SSTFLASH_GOT_55:
      pFlash->byStep = SSTFLASH_IDLE;
      if ( wCmd != 0x555 )
        return false;
      switch ( byData )
      {
        case 0x90:                                  /* software ID entry */
          if ( SstFlash_GetIdPage() )
          {
            pFlash->byIdMode = 1;
            pFlash->pfnResync();
          }
          return true;

        case 0xA0:                                  /* byte program      */
          pFlash->byStep = SSTFLASH_PROGRAM_ARM;
          return true;

        case 0x80:                                  /* erase setup       */
          pFlash->byStep = SSTFLASH_ERASE_AA;
          return true;

        default:
          return false;
      }

    case SSTFLASH_ERASE_AA:
      if ( byData == 0xAA && wCmd == 0x555 )
      {
        pFlash->byStep = SSTFLASH_ERASE_55;
        return true;
      }
      pFlash->byStep = SSTFLASH_IDLE;
      return false;

    case SSTFLASH_ERASE_55:
      if ( byData == 0x55 && wCmd == 0xAAA )
      {
        pFlash->byStep = SSTFLASH_ERASE_CMD;
        return true;
      }
      pFlash->byStep = SSTFLASH_IDLE;
      return false;

    case SSTFLASH_ERASE_CMD:
      pFlash->byStep = SSTFLASH_IDLE;
      if ( byData == 0x30 )                         /* sector erase       */
      {
        SstFlash_Erase( pFlash, dwOfs );
        return true;
      }
      /* 0x10 is chip erase: 512KB of shadow is not affordable, and no game
         is known to use it on these boards. Swallow it so it cannot be
         mistaken for a bank write. */
      return byData == 0x10;

    case SSTFLASH_PROGRAM_ARM:
      pFlash->byStep = SSTFLASH_IDLE;
      SstFlash_Program( pFlash, dwOfs, byData );
      return true;

    default:
      pFlash->byStep = SSTFLASH_IDLE;
      return false;
  }
}

/*-------------------------------------------------------------------*/
/*  Save state                                                        */
/*-------------------------------------------------------------------*/
/* The 8KB shadow page itself is deliberately NOT in here. state.cpp f_mallocs
   a staging copy of the whole mapper blob on both save and load, and an 8KB
   copy is more than an RP2040 can spare beside a mapper 30 cartridge's 32KB of
   CHR RAM - Knight on the Moon panicked out of memory in both paths. The
   shadow lives in a session-global buffer instead; see SstFlash_LoadBlob. */
struct SstFlashState
{
  BYTE byStep;
  BYTE byIdMode;
  BYTE byHaveShadow;
  BYTE byPad;
  DWORD dwShadowPage;
};

int SstFlash_BlobSize()
{
  return sizeof( struct SstFlashState );
}

void SstFlash_SaveBlob( struct SstFlash_tag *pFlash, BYTE *pBuf )
{
  struct SstFlashState *pState = (struct SstFlashState *)pBuf;

  pState->byStep = pFlash->byStep;
  pState->byIdMode = pFlash->byIdMode;
  pState->byPad = 0;
  pState->dwShadowPage = pFlash->dwShadowPage;
  pState->byHaveShadow = ( SstFlash_Shadow && pFlash->dwShadowPage != SSTFLASH_NO_PAGE ) ? 1 : 0;
}

void SstFlash_LoadBlob( struct SstFlash_tag *pFlash, BYTE *pBuf )
{
  struct SstFlashState *pState = (struct SstFlashState *)pBuf;

  DWORD dwLive = pFlash->dwShadowPage;

  pFlash->byStep = pState->byStep;
  pFlash->byIdMode = pState->byIdMode;

  /* Whatever this session has already programmed stays where it is: if the
     shadow still holds the page the state was saved with, keep it, otherwise
     fall back to the real ROM. A state reloaded in a later session therefore
     loses flash writes made before it was taken - which costs nothing, because
     those writes never survived a reboot in the first place. */
  pFlash->dwShadowPage =
      ( pState->byHaveShadow && SstFlash_Shadow && dwLive == pState->dwShadowPage )
          ? pState->dwShadowPage : SSTFLASH_NO_PAGE;

  /* ID mode needs its page back before the mapper re-applies its banks. */
  if ( pFlash->byIdMode && !SstFlash_GetIdPage() )
    pFlash->byIdMode = 0;

  /* The caller's Sync() re-installs both, so nothing to do here. */
}

void SstFlash_Release()
{
  if ( SstFlash_IdPage ) { Frens::f_free( SstFlash_IdPage ); SstFlash_IdPage = nullptr; }
  if ( SstFlash_Shadow ) { Frens::f_free( SstFlash_Shadow ); SstFlash_Shadow = nullptr; }
}
