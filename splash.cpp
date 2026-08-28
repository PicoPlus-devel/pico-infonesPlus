#include "menu.h"
#include "FrensHelpers.h"
#include <cstring>

// called by menu.cpp
// shows emulator specific splash screen
static int fgcolorSplash = DEFAULT_FGCOLOR;
static int bgcolorSplash = DEFAULT_BGCOLOR;
void splash()
{
    char s[SCREEN_COLS + 1];
    ClearScreen(bgcolorSplash);

    strcpy(s, "Pico-Info");
    putText(SCREEN_COLS / 2 - (strlen(s) + 4) / 2, 2, s, fgcolorSplash, bgcolorSplash);

    putText((SCREEN_COLS / 2 - (strlen(s)) / 2) + 7, 2, "N", CRED, bgcolorSplash);
    putText((SCREEN_COLS / 2 - (strlen(s)) / 2) + 8, 2, "E", CGREEN, bgcolorSplash);
    putText((SCREEN_COLS / 2 - (strlen(s)) / 2) + 9, 2, "S", CBLUE, bgcolorSplash);
    putText((SCREEN_COLS / 2 - (strlen(s)) / 2) + 10, 2, "+", fgcolorSplash, bgcolorSplash);

    strcpy(s, "NES emulator for RP2040/2350");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 3, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "Emulator");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 5, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "@jay_kumogata");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 6, s, CLIGHTBLUE, bgcolorSplash);
#if !HSTX
    strcpy(s, "Pico Port");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 9, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "@shuichi_takano");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 10, s, CLIGHTBLUE, bgcolorSplash);
#else
    strcpy(s, "Pico Port___________HDMI Driver");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 9, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "@shuichi_takano_____fliperama86");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 10, s, CLIGHTBLUE, bgcolorSplash);
#endif
    strcpy(s, "Menu System & SD Card Support");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 13, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "@frenskefrens");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 14, s, CLIGHTBLUE, bgcolorSplash);

    strcpy(s, "NES/WII controller support");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 17, s, fgcolorSplash, bgcolorSplash);

    strcpy(s, "@PaintYourDragon @adafruit");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 18, s, CLIGHTBLUE, bgcolorSplash);

    strcpy(s, "PCB Design:");
    putText(2, 21, s, fgcolorSplash, bgcolorSplash);

    strcpy(s, "@johnedgarpark DynaMight");
    putText(13, 21, s, CLIGHTBLUE, bgcolorSplash);

    strcpy(s, "3D Printed Case & artwork: DynaMight");
    putText(2, 23, s, fgcolorSplash, bgcolorSplash);

    strcpy(s, "https://github.com/");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 25, s, CLIGHTBLUE, bgcolorSplash);
    strcpy(s, "PicoPlus-devel/pico-infonesPlus");
    putText(SCREEN_COLS / 2 - strlen(s) / 2, 26, s, CLIGHTBLUE, bgcolorSplash);
}