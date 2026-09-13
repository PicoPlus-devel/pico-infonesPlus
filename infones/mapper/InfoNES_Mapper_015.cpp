/*===================================================================*/
/*                                                                   */
/*                     Mapper 15 (100-in-1)                          */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 15                                             */
/*-------------------------------------------------------------------*/
void Map15_Init()
{
  /* Initialize Mapper */
  MapperInit = Map15_Init;

  /* Write to Mapper */
  MapperWrite = Map15_Write;

  /* Write to SRAM */
  MapperSram = Map0_Sram;

  /* Write to APU */
  MapperApu = Map0_Apu;

  /* Read from APU */
  MapperReadApu = Map0_ReadApu;

  /* Callback at VSync */
  MapperVSync = Map0_VSync;

  /* Callback at HSync */
  MapperHSync = Map0_HSync;

  /* Callback at PPU */
  MapperPPU = Map0_PPU;

  /* Callback at Rendering Screen ( 1:BG, 0:Sprite ) */
  MapperRenderScreen = Map0_RenderScreen;

  /* Set SRAM Banks */
  SRAMBANK = SRAM;

  /* Set ROM Banks */
  ROMBANK0 = ROMPAGE( 0 );
  ROMBANK1 = ROMPAGE( 1 );
  ROMBANK2 = ROMPAGE( 2 );
  ROMBANK3 = ROMPAGE( 3 );

  /* Set up wiring of the interrupt pin */
  K6502_Set_Int_Wiring( 1, 1 ); 
}

/*-------------------------------------------------------------------*/
/*  Mapper 15 Write Function                                         */
/*-------------------------------------------------------------------*/

/* 8Kbytes page index, always wrapped into the image */
#define MAP15_PAGE( p ) ROMPAGE( (int)( p ) % ( NesHeader.byRomSize << 1 ) )

void Map15_Write( WORD wAddr, BYTE byData )
{
  /*
   *  The K-1029 decodes address mask $8000, so only A1/A0 select the banking
   *  mode and the register is mirrored over the whole $8000-$FFFF range.
   *
   *    D~[pMPP PPPP]   P : PRG A19..A14 (6 bits)
   *                    M : mirroring, 0 vertical / 1 horizontal
   *                    p : PRG A13, banking mode 2 only
   *
   *  Note the real PCB write-protects CHR RAM in modes 0 and 3 and carries no
   *  PRG RAM at $6000-$7FFF.  That is deliberately not emulated: every mapper
   *  15 ROM other than the two 100-in-1/168-in-1 multicarts is a mapper hack
   *  (usually of mapper 164 or 227) that needs both relaxed to run at all.
   */
  BYTE byBank = byData & 0x3f;

  /* Mirroring is latched on every write, in every mode */
  InfoNES_Mirroring( byData & 0x40 ? 0 : 1 );

  switch ( wAddr & 0x0003 )
  {
    case 0:
      /* NROM-256 : 32Kbytes, PRG A14 is supplied by CPU A14 */
      byBank &= 0x3e;

      ROMBANK0 = MAP15_PAGE( byBank * 2 );
      ROMBANK1 = MAP15_PAGE( byBank * 2 + 1 );
      ROMBANK2 = MAP15_PAGE( byBank * 2 + 2 );
      ROMBANK3 = MAP15_PAGE( byBank * 2 + 3 );
      break;

    case 1:
      /* UNROM : PRG A14..A16 are forced to 111 while CPU A14 is 1 */
      ROMBANK0 = MAP15_PAGE( byBank * 2 );
      ROMBANK1 = MAP15_PAGE( byBank * 2 + 1 );

      byBank = ( byBank & 0x38 ) | 0x07;

      ROMBANK2 = MAP15_PAGE( byBank * 2 );
      ROMBANK3 = MAP15_PAGE( byBank * 2 + 1 );
      break;

    case 2:
      /* NROM-64 : one 8Kbytes bank mirrored four times, PRG A13 is p */
      byBank = byBank * 2 + ( byData & 0x80 ? 1 : 0 );

      ROMBANK0 = MAP15_PAGE( byBank );
      ROMBANK1 = MAP15_PAGE( byBank );
      ROMBANK2 = MAP15_PAGE( byBank );
      ROMBANK3 = MAP15_PAGE( byBank );
      break;

    case 3:
      /* NROM-128 : one 16Kbytes bank mirrored twice */
      ROMBANK0 = MAP15_PAGE( byBank * 2 );
      ROMBANK1 = MAP15_PAGE( byBank * 2 + 1 );
      ROMBANK2 = MAP15_PAGE( byBank * 2 );
      ROMBANK3 = MAP15_PAGE( byBank * 2 + 1 );
      break;
  }
}
