/*===================================================================*/
/*                                                                   */
/*  InfoNES_System.h : The function which depends on a system        */
/*                                                                   */
/*  2000/05/29  InfoNES Project ( based on pNesX )                   */
/*                                                                   */
/*===================================================================*/

#ifndef InfoNES_SYSTEM_H_INCLUDED
#define InfoNES_SYSTEM_H_INCLUDED

/*-------------------------------------------------------------------*/
/*  Include files                                                    */
/*-------------------------------------------------------------------*/

#include "InfoNES_Types.h"

/*-------------------------------------------------------------------*/
/*  Palette data                                                     */
/*-------------------------------------------------------------------*/
extern WORD NesPalette[];

/*-------------------------------------------------------------------*/
/*  Function prototypes                                              */
/*-------------------------------------------------------------------*/

/* Menu screen */
int InfoNES_Menu();

/* Read ROM image file */
int InfoNES_ReadRom(const char *pszFileName);

/* Release a memory for ROM */
void InfoNES_ReleaseRom();

/* Transfer the contents of work frame on the screen */
int InfoNES_LoadFrame();

/* Get a joypad state */
void InfoNES_PadState(DWORD *pdwPad1, DWORD *pdwPad2, DWORD *pdwSystem);

/* memcpy */
inline void *InfoNES_MemoryCopy(void *dest, const void *src, int count)
{
    __builtin_memcpy(dest, src, count);
    return dest;
}

/* memset */
inline void *InfoNES_MemorySet(void *dest, int c, int count)
{
    __builtin_memset(dest, c, count);
    return dest;
}

/* Print debug message */
void InfoNES_DebugPrint(const char *pszMsg);

/* Wait */
inline void InfoNES_Wait() {}

/* Sound Initialize */
void InfoNES_SoundInit(void);

/* Sound Open */
int InfoNES_SoundOpen(int samples_per_sync, int sample_rate);

/* Sound Close */
void InfoNES_SoundClose(void);

/* Sound Output waves - NES APU (2 Pulse, 1 Triangle, 1 Noise, 1 DPCM) plus
 * an optional 6th input for expansion audio that needs its own mix headroom
 * (currently Sunsoft 5B via Mapper 69). Pass NULL for `wave6` when no
 * expansion audio is active. */
void InfoNES_SoundOutput(int samples, BYTE *wave1, BYTE *wave2, BYTE *wave3, BYTE *wave4, BYTE *wave5, BYTE *wave6);
int InfoNES_GetSoundBufferSize();

/* Print system message */
void InfoNES_MessageBox(const char *pszMsg, ...);

void InfoNES_Error(const char *pszMsg, ...);
void InfoNES_PreDrawLine(int line);
void InfoNES_PostDrawLine(int line);

#endif /* !InfoNES_SYSTEM_H_INCLUDED */
