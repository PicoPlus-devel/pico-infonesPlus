/*===================================================================*/
/*                                                                   */
/*          Mapper 207 (Taito X1-005, alternate mirroring)           */
/*                                                                   */
/*===================================================================*/

/* Mapper 80 with the name table wiring changed: bit 7 of $7EF0 selects
   the CIRAM page for $2000/$2400, bit 7 of $7EF1 the page for
   $2800/$2C00, and $7EF6 does nothing. Fudou Myouou Den only. */

/*-------------------------------------------------------------------*/
/*  Initialize Mapper 207                                            */
/*-------------------------------------------------------------------*/
void Map207_Init()
{
  Map80_Init();

  /* Initialize Mapper */
  MapperInit = Map207_Init;

  /* Name tables follow the CHR bank registers */
  Map80_Alt_Mirroring = true;
}
