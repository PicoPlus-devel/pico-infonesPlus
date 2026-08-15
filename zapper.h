/*===================================================================*/
/*                                                                   */
/*  zapper.h : NES Zapper (light gun) on NES controller port 2       */
/*                                                                   */
/*===================================================================*/
/*
 * Wiring (custom PCB design v2.1 and later, HW_CONFIG 2):
 *
 *   port 2 pin D3 -> ZAPPER_D3 (GPIO28)   light sensor
 *   port 2 pin D4 -> ZAPPER_D4 (GPIO27)   trigger
 *
 * $4016/$4017 read layout (https://www.nesdev.org/wiki/Zapper):
 *
 *   7  bit  0
 *   ---- ----
 *   xxxT WxxS
 *      | |  |
 *      | |  +- Serial data (pad shift register)
 *      | +---- Light sensed at the current scanline (0: detected, 1: not detected)
 *      +------ Trigger (0: released or fully pulled, 1: half-pulled)
 *
 * Both bits are direct pass-throughs of the port pin levels. The Zapper is not
 * emulated: the gun's own circuitry supplies the timing - the photo diode holds
 * "light" for ~19-26 scanlines depending on brightness, and the trigger's RC
 * network holds "half-pulled" for ~100 ms. Nothing is latched here; the levels
 * are sampled at the moment of the $4017 read, because "light sensed at the
 * current scanline" is exactly what the neslcdmod.com LCD-delay calibration
 * measures.
 */
#ifndef ZAPPER_H_INCLUDED
#define ZAPPER_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

/*-------------------------------------------------------------------*/
/*  Enablement                                                       */
/*                                                                   */
/*  Collapses to inline no-ops (and does not pull in                 */
/*  <hardware/gpio.h>) when:                                         */
/*    - ZAPPER_D3 / ZAPPER_D4 are not defined at all (hosttest)      */
/*    - either pin is -1 (boards where GPIO 27/28 are already used)  */
/*    - building without hardware (PICO_NO_HARDWARE)                 */
/*-------------------------------------------------------------------*/
/* Note on the PICO_NO_HARDWARE test below: the Pico SDK compiles *device*
   builds with -DPICO_NO_HARDWARE=0, so the macro is always defined and
   `defined(PICO_NO_HARDWARE)` would disable the Zapper on real hardware. The
   value has to be tested, not the definition. */
#if !defined(ZAPPER_D3) || !defined(ZAPPER_D4)
#define ZAPPER_SUPPORTED 0
#elif (ZAPPER_D3) < 0 || (ZAPPER_D4) < 0
#define ZAPPER_SUPPORTED 0
#elif defined(PICO_NO_HARDWARE) && (PICO_NO_HARDWARE)
#define ZAPPER_SUPPORTED 0
#else
#define ZAPPER_SUPPORTED 1
#endif

/* $4017 bit positions */
#define ZAPPER_BIT_LIGHT 0x08u   /* 0 = light detected  */
#define ZAPPER_BIT_TRIGGER 0x10u /* 1 = half-pulled     */
#define ZAPPER_BITS_MASK (ZAPPER_BIT_LIGHT | ZAPPER_BIT_TRIGGER)
/* Idle: no light, trigger not half-pulled. Also what an empty port or a plain
   NES/SNES pad (which leaves D3/D4 unconnected) reads with the pull-ups on. */
#define ZAPPER_BITS_IDLE ZAPPER_BITS_MASK

/* Line polarity.
   The light line needs inverting on this hardware and the trigger line does not.
   Measured on the PCB with a Tomee Zapp Gun: GPIO28 reads low while the sensor
   is covered and high while it sees light, while $4017 bit 3 is defined the other
   way round (0 = light detected). The NES inverts the light line between the
   connector and the CPU bus, so the spec describes the bus value, not the pin; a
   direct GPIO wire sees the raw sense. GPIO27 already matches the bus definition
   (high while the trigger is half-pulled), so it is passed straight through.
   CMakeLists.txt sets the defaults per board; override with
   -DZAPPER_INVERT_D3=0 / -DZAPPER_INVERT_D4=1 for a gun that differs. */
#ifndef ZAPPER_INVERT_D3
#define ZAPPER_INVERT_D3 0
#endif
#ifndef ZAPPER_INVERT_D4
#define ZAPPER_INVERT_D4 0
#endif
/* Diagnostics. A bit mask, because both kinds interfere with light-gun
   detection and sometimes only one of them can be tolerated:
     1  UART trace, one line per second. The line is ~130 characters, which at
        115200 baud blocks for ~11 ms inside the per-frame poll - enough to
        starve the video path and show up as a visible flicker. Fine for
        checking wiring, not for playing.
     2  On-screen readout. Draws bright pixels on every frame, including the
        all-black and single-white-box frames the games use to test where the
        gun is pointed, so the gun sees light regardless of aim and every shot
        registers as a hit. Never leave this on while actually playing.
   Use 3 for both. Both are off in release builds. */
#ifndef ZAPPER_DEBUG
#define ZAPPER_DEBUG 0
#endif
#define ZAPPER_DEBUG_UART ((ZAPPER_DEBUG) & 1)
#define ZAPPER_DEBUG_OVERLAY ((ZAPPER_DEBUG) & 2)

/* Display-lag measurement mode. Takes over the picture: holds it black long
   enough for the panel and the gun's sensor to settle, flashes one full white
   frame, and times how long until the light line reports light. Reports
   min/average/max over a run of flashes. Point the gun at the middle of the
   screen and keep it still. Not a play mode - the picture is replaced. */
#ifndef ZAPPER_MEASURE
#define ZAPPER_MEASURE 0
#endif

/* Sticky: set once a Zapper has been recognised, never cleared while running.
   Declared unconditionally so the symbol exists on every board. */
extern bool zapperconnected;

void initzapper(void); /* once from main(), after Frens::initAll()          */
void zapperPoll(void); /* once per frame: presence latch + optional trace   */

#if ZAPPER_SUPPORTED

#include "hardware/gpio.h"

/* Live sample of both pins, already shifted into $4017 bit positions.
   gpio_get() is static inline over sio_hw->gpio_in, so this becomes two SIO
   loads and inlines cleanly into the RAM-resident K6502_Read(). */
static inline unsigned zapperBits(void)
{
    unsigned light = gpio_get(ZAPPER_D3) ? 1u : 0u;
    unsigned trigger = gpio_get(ZAPPER_D4) ? 1u : 0u;
#if ZAPPER_INVERT_D3
    light ^= 1u;
#endif
#if ZAPPER_INVERT_D4
    trigger ^= 1u;
#endif
    return (light << 3) | (trigger << 4);
}

#if ZAPPER_DEBUG
/* Diagnostics, counted from inside the register read paths so they see every
   poll the game makes, not just the once-per-frame housekeeping sample. All are
   reset each time zapperPoll() reports.
     r4016 / r4017 - reads of each register. Which one the game uses to sample
                     the gun tells us whether it expects the Zapper in port 2
                     ($4017, the NES arrangement) or port 1 / the Famicom
                     expansion port ($4016).
     lit / dark    - $4017 reads that returned "light detected" / "no light",
                     i.e. what the game was actually told, after inversion.
     trig          - $4017 reads that returned the trigger half-pulled. */
extern volatile uint32_t zapperDbgReads;
extern volatile uint32_t zapperDbgReads4016;
extern volatile uint32_t zapperDbgLit;
extern volatile uint32_t zapperDbgDark;
extern volatile uint32_t zapperDbgTrig;

#if ZAPPER_DEBUG_OVERLAY
/* One-line status readout, rebuilt every frame by zapperPoll() and drawn onto
   the picture by InfoNES_PostDrawLine(). 32 characters wide, which is exactly
   the 256-pixel NES picture at 8 pixels per character. */
#define ZAPPER_DBG_TEXT_LEN 32
extern char zapperDbgText[ZAPPER_DBG_TEXT_LEN + 1];
#endif
#endif

#endif /* ZAPPER_SUPPORTED */

/*-------------------------------------------------------------------*/
/*  Overlay the live Zapper bits onto a $4017 read result.            */
/*                                                                   */
/*  Bit 0 (pad 2 serial data) and bit 6 (open bus) are preserved, so  */
/*  a controller in port 2 keeps working alongside the gun. Returns   */
/*  the value unchanged when no Zapper is connected, and folds away   */
/*  entirely when Zapper support is compiled out.                    */
/*-------------------------------------------------------------------*/
static inline uint8_t zapperApply4017(uint8_t value)
{
#if ZAPPER_SUPPORTED
    if (zapperconnected)
    {
        value = (uint8_t)((value & (uint8_t)~ZAPPER_BITS_MASK) | (uint8_t)zapperBits());
    }
#if ZAPPER_DEBUG
    /* Counted after the overlay, so these are the bits the game was handed.
       Counted even when no Zapper has been detected, so a "the game never even
       reads $4017" case stays distinguishable from a wiring problem. */
    zapperDbgReads++;
    if (value & ZAPPER_BIT_LIGHT)
        zapperDbgDark++;
    else
        zapperDbgLit++;
    if (value & ZAPPER_BIT_TRIGGER)
        zapperDbgTrig++;
#endif
#endif
    return value;
}

/*-------------------------------------------------------------------*/
/*  Display-lag measurement (ZAPPER_MEASURE).                        */
/*                                                                   */
/*  zapperMeasureIsWhite() says what the current frame should be      */
/*  filled with; zapperMeasureLine() is called once per rendered      */
/*  scanline to stamp the flash and watch for first light;            */
/*  zapperMeasureFrame() advances the cycle once per frame and        */
/*  reports a run when it completes.                                  */
/*-------------------------------------------------------------------*/
#if ZAPPER_SUPPORTED && ZAPPER_MEASURE
void zapperMeasureInit(void);
bool zapperMeasureIsWhite(void);
void zapperMeasureLine(int line);
void zapperMeasureFrame(void);
#else
static inline void zapperMeasureInit(void) {}
static inline bool zapperMeasureIsWhite(void) { return false; }
static inline void zapperMeasureLine(int line) { (void)line; }
static inline void zapperMeasureFrame(void) {}
#endif

/*-------------------------------------------------------------------*/
/*  Note a $4016 read, so the diagnostics can tell which register the */
/*  game samples the gun from. Compiles away completely unless the    */
/*  diagnostics are on.                                              */
/*-------------------------------------------------------------------*/
static inline void zapperNote4016(void)
{
#if ZAPPER_SUPPORTED && ZAPPER_DEBUG
    zapperDbgReads4016++;
#endif
}

#endif /* ZAPPER_H_INCLUDED */
