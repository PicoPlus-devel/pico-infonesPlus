/*===================================================================*/
/*                                                                   */
/*  InfoNES_Mapper.cpp : InfoNES Mapper Function                     */
/*                                                                   */
/*  2000/05/16  InfoNES Project ( based on NesterJ and pNesX )       */
/*                                                                   */
/*===================================================================*/

/*-------------------------------------------------------------------*/
/*  Include files                                                    */
/*-------------------------------------------------------------------*/

#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_Mapper.h"
#include "InfoNES_pAPU.h"
#include "FrensHelpers.h"
#include "K6502.h"
#include "InfoNES_NSF.h"
#include <pico.h>
#if PICO_RP2350
#include "InfoNES_FDS.h"
#endif

/*-------------------------------------------------------------------*/
/*  Mapper resources                                                 */
/*-------------------------------------------------------------------*/

/* Disk System RAM */
BYTE *DRAM;   // [DRAM_SIZE];

/*-------------------------------------------------------------------*/
/*  Table of Mapper initialize function                              */
/*-------------------------------------------------------------------*/

const struct MapperTable_tag MapperTable[] =
    {
        {0, Map0_Init},
        {1, Map1_Init},
        {2, Map2_Init},
        {3, Map3_Init},
        {4, Map4_Init},
#if PICO_RP2350
        {5, Map5_Init},
#endif
        // {6, Map6_Init},
        {7, Map7_Init},
        {8, Map8_Init},
        {9, Map9_Init},
        {10, Map10_Init},
        {11, Map11_Init},
        {13, Map13_Init},
        {15, Map15_Init},
        {16, Map16_Init},
        {17, Map17_Init},
        {18, Map18_Init},
        {19, Map19_Init},
#if PICO_RP2350
        {20, Map20_Init},
#endif
        {21, Map21_Init},
        {22, Map22_Init},
        {23, Map23_Init},
        {24, Map24_Init},
        {25, Map25_Init},
        {26, Map26_Init},
        {30, Map30_Init},
        {31, MapNsf_Init},
        {32, Map32_Init},
        {33, Map33_Init},
        {34, Map34_Init},
        {40, Map40_Init},
        {41, Map41_Init},
        {42, Map42_Init},
        {43, Map43_Init},
        {44, Map44_Init},
        {45, Map45_Init},
        {46, Map46_Init},
        {47, Map47_Init},
        {48, Map48_Init},
        {49, Map49_Init},
        {50, Map50_Init},
        {51, Map51_Init},
        {57, Map57_Init},
        {58, Map58_Init},
        {60, Map60_Init},
        {61, Map61_Init},
        {62, Map62_Init},
        {64, Map64_Init},
        {65, Map65_Init},
        {66, Map66_Init},
        {67, Map67_Init},
        {68, Map68_Init},
        {69, Map69_Init},
        {70, Map70_Init},
        {71, Map71_Init},
        {72, Map72_Init},
        {73, Map73_Init},
        {74, Map74_Init},
        {75, Map75_Init},
        {76, Map76_Init},
        {77, Map77_Init},
        {78, Map78_Init},
        {79, Map79_Init},
        {80, Map80_Init},
        {82, Map82_Init},
        {83, Map83_Init},
#if PICO_RP2350
        {85, Map85_Init},
#endif
        {86, Map86_Init},
        {87, Map87_Init},
        {88, Map88_Init},
        {89, Map89_Init},
        {90, Map90_Init},
        {91, Map91_Init},
        {92, Map92_Init},
        {93, Map93_Init},
        {94, Map94_Init},
        {95, Map95_Init},
        {96, Map96_Init},
        {97, Map97_Init},
        {100, Map100_Init},
        {101, Map101_Init},
        {105, Map105_Init},
        {107, Map107_Init},
        {108, Map108_Init},
        {109, Map109_Init},
        {110, Map110_Init},
        {111, Map111_Init},
        {112, Map112_Init},
        {113, Map113_Init},
        {114, Map114_Init},
        {115, Map115_Init},
        {116, Map116_Init},
        {117, Map117_Init},
        {118, Map118_Init},
        {119, Map119_Init},
        {122, Map122_Init},
        {133, Map133_Init},
        {134, Map134_Init},
        {135, Map135_Init},
        {140, Map140_Init},
        {151, Map151_Init},
        {160, Map160_Init},
        {180, Map180_Init},
        {181, Map181_Init},
        {182, Map182_Init},
        {183, Map183_Init},
        {184, Map184_Init},
        {185, Map185_Init},
        {187, Map187_Init},
        {188, Map188_Init},
        {189, Map189_Init},
        {191, Map191_Init},
        {193, Map193_Init},
        {194, Map194_Init},
        {200, Map200_Init},
        {201, Map201_Init},
        {202, Map202_Init},
        {206, Map206_Init},
        {208, Map208_Init},
        {210, Map210_Init},
        {222, Map222_Init},
        {225, Map225_Init},
        {226, Map226_Init},
        {227, Map227_Init},
        {228, Map228_Init},
        {229, Map229_Init},
        {230, Map230_Init},
        {231, Map231_Init},
        {232, Map232_Init},
        {233, Map233_Init},
        {234, Map234_Init},
        {235, Map235_Init},
        {236, Map236_Init},
        {240, Map240_Init},
        {241, Map241_Init},
        {242, Map242_Init},
        {243, Map243_Init},
        {244, Map244_Init},
        {245, Map245_Init},
        {246, Map246_Init},
        {248, Map248_Init},
        {249, Map249_Init},
        {251, Map251_Init},
        {252, Map252_Init},
        {255, Map255_Init},
        {263, Map263_Init},
        {-1, NULL}};

/*-------------------------------------------------------------------*/
/*  body of Mapper functions                                         */
/*-------------------------------------------------------------------*/

/* Shared by mappers 30 and 111, so it has to come first. */
#include "mapper/InfoNES_SstFlash.cpp"

#include "mapper/InfoNES_Mapper_000.cpp"
#include "mapper/InfoNES_Mapper_001.cpp"
#include "mapper/InfoNES_Mapper_002.cpp"
#include "mapper/InfoNES_Mapper_003.cpp"
#include "mapper/InfoNES_Mapper_004.cpp"
#if PICO_RP2350
#include "mapper/InfoNES_Mapper_005.cpp"
#endif
// #include "mapper/InfoNES_Mapper_006.cpp"
#include "mapper/InfoNES_Mapper_007.cpp"
#include "mapper/InfoNES_Mapper_008.cpp"
#include "mapper/InfoNES_Mapper_009.cpp"
#include "mapper/InfoNES_Mapper_010.cpp"
#include "mapper/InfoNES_Mapper_011.cpp"
#include "mapper/InfoNES_Mapper_013.cpp"
#include "mapper/InfoNES_Mapper_015.cpp"
#include "mapper/InfoNES_Mapper_016.cpp"
#include "mapper/InfoNES_Mapper_017.cpp"
#include "mapper/InfoNES_Mapper_018.cpp"
#include "mapper/InfoNES_Mapper_019.cpp"
#if PICO_RP2350
#include "mapper/InfoNES_Mapper_020.cpp"
#endif
#include "mapper/InfoNES_Mapper_021.cpp"
#include "mapper/InfoNES_Mapper_022.cpp"
#include "mapper/InfoNES_Mapper_023.cpp"
#include "mapper/InfoNES_Mapper_024.cpp"
#include "mapper/InfoNES_Mapper_025.cpp"
#include "mapper/InfoNES_Mapper_026.cpp"
#include "mapper/InfoNES_Mapper_030.cpp"
#include "mapper/InfoNES_Mapper_032.cpp"
#include "mapper/InfoNES_Mapper_033.cpp"
#include "mapper/InfoNES_Mapper_034.cpp"
#include "mapper/InfoNES_Mapper_040.cpp"
#include "mapper/InfoNES_Mapper_041.cpp"
#include "mapper/InfoNES_Mapper_042.cpp"
#include "mapper/InfoNES_Mapper_043.cpp"
#include "mapper/InfoNES_Mapper_044.cpp"
#include "mapper/InfoNES_Mapper_045.cpp"
#include "mapper/InfoNES_Mapper_046.cpp"
#include "mapper/InfoNES_Mapper_047.cpp"
#include "mapper/InfoNES_Mapper_048.cpp"
#include "mapper/InfoNES_Mapper_049.cpp"
#include "mapper/InfoNES_Mapper_050.cpp"
#include "mapper/InfoNES_Mapper_051.cpp"
#include "mapper/InfoNES_Mapper_057.cpp"
#include "mapper/InfoNES_Mapper_058.cpp"
#include "mapper/InfoNES_Mapper_060.cpp"
#include "mapper/InfoNES_Mapper_061.cpp"
#include "mapper/InfoNES_Mapper_062.cpp"
#include "mapper/InfoNES_Mapper_064.cpp"
#include "mapper/InfoNES_Mapper_065.cpp"
#include "mapper/InfoNES_Mapper_066.cpp"
#include "mapper/InfoNES_Mapper_067.cpp"
#include "mapper/InfoNES_Mapper_068.cpp"
#include "mapper/InfoNES_Mapper_069.cpp"
#include "mapper/InfoNES_Mapper_070.cpp"
#include "mapper/InfoNES_Mapper_071.cpp"
#include "mapper/InfoNES_Mapper_072.cpp"
#include "mapper/InfoNES_Mapper_073.cpp"
#include "mapper/InfoNES_Mapper_074.cpp"
#include "mapper/InfoNES_Mapper_075.cpp"
#include "mapper/InfoNES_Mapper_076.cpp"
#include "mapper/InfoNES_Mapper_077.cpp"
#include "mapper/InfoNES_Mapper_078.cpp"
#include "mapper/InfoNES_Mapper_079.cpp"
#include "mapper/InfoNES_Mapper_080.cpp"
#include "mapper/InfoNES_Mapper_082.cpp"
#include "mapper/InfoNES_Mapper_083.cpp"
#include "mapper/InfoNES_Mapper_085.cpp"
#include "mapper/InfoNES_Mapper_086.cpp"
#include "mapper/InfoNES_Mapper_087.cpp"
#include "mapper/InfoNES_Mapper_088.cpp"
#include "mapper/InfoNES_Mapper_089.cpp"
#include "mapper/InfoNES_Mapper_090.cpp"
#include "mapper/InfoNES_Mapper_091.cpp"
#include "mapper/InfoNES_Mapper_092.cpp"
#include "mapper/InfoNES_Mapper_093.cpp"
#include "mapper/InfoNES_Mapper_094.cpp"
#include "mapper/InfoNES_Mapper_095.cpp"
#include "mapper/InfoNES_Mapper_096.cpp"
#include "mapper/InfoNES_Mapper_097.cpp"
#include "mapper/InfoNES_Mapper_100.cpp"
#include "mapper/InfoNES_Mapper_101.cpp"
#include "mapper/InfoNES_Mapper_105.cpp"
#include "mapper/InfoNES_Mapper_107.cpp"
#include "mapper/InfoNES_Mapper_108.cpp"
#include "mapper/InfoNES_Mapper_109.cpp"
#include "mapper/InfoNES_Mapper_110.cpp"
#include "mapper/InfoNES_Mapper_111.cpp"
#include "mapper/InfoNES_Mapper_112.cpp"
#include "mapper/InfoNES_Mapper_113.cpp"
#include "mapper/InfoNES_Mapper_114.cpp"
#include "mapper/InfoNES_Mapper_115.cpp"
#include "mapper/InfoNES_Mapper_116.cpp"
#include "mapper/InfoNES_Mapper_117.cpp"
#include "mapper/InfoNES_Mapper_118.cpp"
#include "mapper/InfoNES_Mapper_119.cpp"
#include "mapper/InfoNES_Mapper_122.cpp"
#include "mapper/InfoNES_Mapper_133.cpp"
#include "mapper/InfoNES_Mapper_134.cpp"
#include "mapper/InfoNES_Mapper_135.cpp"
#include "mapper/InfoNES_Mapper_140.cpp"
#include "mapper/InfoNES_Mapper_151.cpp"
#include "mapper/InfoNES_Mapper_160.cpp"
#include "mapper/InfoNES_Mapper_180.cpp"
#include "mapper/InfoNES_Mapper_181.cpp"
#include "mapper/InfoNES_Mapper_182.cpp"
#include "mapper/InfoNES_Mapper_183.cpp"
#include "mapper/InfoNES_Mapper_184.cpp"
#include "mapper/InfoNES_Mapper_185.cpp"
#include "mapper/InfoNES_Mapper_187.cpp"
#include "mapper/InfoNES_Mapper_188.cpp"
#include "mapper/InfoNES_Mapper_189.cpp"
#include "mapper/InfoNES_Mapper_191.cpp"
#include "mapper/InfoNES_Mapper_193.cpp"
#include "mapper/InfoNES_Mapper_194.cpp"
#include "mapper/InfoNES_Mapper_200.cpp"
#include "mapper/InfoNES_Mapper_201.cpp"
#include "mapper/InfoNES_Mapper_202.cpp"
#include "mapper/InfoNES_Mapper_206.cpp"
#include "mapper/InfoNES_Mapper_208.cpp"
#include "mapper/InfoNES_Mapper_210.cpp"
#include "mapper/InfoNES_Mapper_222.cpp"
#include "mapper/InfoNES_Mapper_225.cpp"
#include "mapper/InfoNES_Mapper_226.cpp"
#include "mapper/InfoNES_Mapper_227.cpp"
#include "mapper/InfoNES_Mapper_228.cpp"
#include "mapper/InfoNES_Mapper_229.cpp"
#include "mapper/InfoNES_Mapper_230.cpp"
#include "mapper/InfoNES_Mapper_231.cpp"
#include "mapper/InfoNES_Mapper_232.cpp"
#include "mapper/InfoNES_Mapper_233.cpp"
#include "mapper/InfoNES_Mapper_234.cpp"
#include "mapper/InfoNES_Mapper_235.cpp"
#include "mapper/InfoNES_Mapper_236.cpp"
#include "mapper/InfoNES_Mapper_240.cpp"
#include "mapper/InfoNES_Mapper_241.cpp"
#include "mapper/InfoNES_Mapper_242.cpp"
#include "mapper/InfoNES_Mapper_243.cpp"
#include "mapper/InfoNES_Mapper_244.cpp"
#include "mapper/InfoNES_Mapper_245.cpp"
#include "mapper/InfoNES_Mapper_246.cpp"
#include "mapper/InfoNES_Mapper_248.cpp"
#include "mapper/InfoNES_Mapper_249.cpp"
#include "mapper/InfoNES_Mapper_251.cpp"
#include "mapper/InfoNES_Mapper_252.cpp"
#include "mapper/InfoNES_Mapper_255.cpp"
#include "mapper/InfoNES_Mapper_263.cpp"

/* End of InfoNES_Mapper.cpp */
