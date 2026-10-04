/*===================================================================*/
/*                                                                   */
/*            Mapper 152 (Bandai 74161/7432, one-screen)             */
/*                                                                   */
/*===================================================================*/

/* Mapper 70 with bit 7 of the register wired to one-screen mirroring
   (0: $2000, 1: $2400). Bits 6-4 select the 16KB PRG bank at $8000,
   bits 3-0 the 8KB CHR bank; $C000 is fixed to the last bank. */

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 152                                            */
/*-------------------------------------------------------------------*/
void Map152_Init()
{
  Map70_Init();

  /* Initialize Mapper */
  MapperInit = Map152_Init;

  /* Mirroring is under program control from the start */
  Map70_Mirror_Ctl = true;
}
