/*===================================================================*/
/*                                                                   */
/*              Mapper 154 (Namco 108, one-screen)                   */
/*                                                                   */
/*===================================================================*/

/* Mapper 88 with bit 6 of every register write wired to one-screen
   mirroring (0: $2000, 1: $2400). Devil Man only. The chip decodes A15
   and A0 alone, so a write anywhere in $8000-$FFFF lands on $8000 or
   $8001, and the two 2KB CHR registers only reach the lower 64KB. */

/* 0: $2000, 1: $2400, 0xff: no write yet (header mirroring) */
static BYTE Map154_Mirror;

/*-------------------------------------------------------------------*/
/*  Mapper 154 Write Function                                        */
/*-------------------------------------------------------------------*/
void Map154_Write( WORD wAddr, BYTE byData )
{
  Map154_Mirror = ( byData & 0x40 ) ? 1 : 0;
  InfoNES_Mirroring( Map154_Mirror ? 2 : 3 );

  wAddr &= 0x8001;
  if ( wAddr == 0x8001 && ( Map88_Regs[ 0 ] & 0x07 ) < 2 )
    byData &= 0x3f;

  Map88_Write( wAddr, byData );
}

/*-------------------------------------------------------------------*/
/*  Mapper 154 state save/load                                       */
/*-------------------------------------------------------------------*/
/* The bank select, which the next $8001 write builds on, and the
   mirroring, which the state loader resets to the header's. */
static int Map154BlobSize()
{
  return 2;
}

static void Map154SaveBlob( BYTE *pBuf )
{
  pBuf[ 0 ] = Map88_Regs[ 0 ];
  pBuf[ 1 ] = Map154_Mirror;
}

static void Map154LoadBlob( BYTE *pBuf )
{
  Map88_Regs[ 0 ] = pBuf[ 0 ];
  Map154_Mirror = pBuf[ 1 ];

  if ( Map154_Mirror != 0xff )
    InfoNES_Mirroring( Map154_Mirror ? 2 : 3 );
}

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 154                                            */
/*-------------------------------------------------------------------*/
void Map154_Init()
{
  Map88_Init();

  /* Initialize Mapper */
  MapperInit = Map154_Init;

  /* Write to Mapper */
  MapperWrite = Map154_Write;

  MapperBlobSize = Map154BlobSize;
  MapperSaveBlob = Map154SaveBlob;
  MapperLoadBlob = Map154LoadBlob;

  Map88_Regs[ 0 ] = 0;
  Map154_Mirror = 0xff;
}
