// Live SDL2 front end for the host harness (NES_LIVE=1, see README.md).
//
// Deliberately free of InfoNES headers: host_sdl.cpp only sees plain pixels,
// samples and an NES pad mask, so SDL's and the core's macros and typedefs
// never meet in one translation unit.
#ifndef HOST_SDL_H
#define HOST_SDL_H

#include <cstdint>

// Event bits returned by hsdl_poll().
#define HSDL_EV_QUIT  (1 << 0)   // Esc, window close or Ctrl-C
#define HSDL_EV_DUMP  (1 << 1)   // F12: dump the frame on screen
#define HSDL_EV_STEP  (1 << 2)   // N while paused: run one frame

// Opens the window (256x240 * scale) and, when audio_on, a 44.1 kHz s16
// stereo output. frame_us is the frame period used for pacing when there is
// no audio device. Returns false if the window cannot be created.
bool     hsdl_init(const char *title, int scale, bool audio_on, uint32_t frame_us);
void     hsdl_quit();

// One 256x240 frame of XRGB8888 pixels.
void     hsdl_present(const uint32_t *px);
void     hsdl_redraw();

// Queue one video frame of mixed audio (interleaved stereo). Dropped while
// paused or fast-forwarding, and while the output is stalled.
void     hsdl_audio(const int16_t *buf, int frames);

// Pump SDL events. Returns the keyboard's NES pad mask (A=01 B=02 SEL=04
// START=08 U=10 D=20 L=40 R=80) and ORs HSDL_EV_* bits into *events.
uint8_t  hsdl_poll(int *events);
bool     hsdl_paused();
void     hsdl_idle();              // short sleep for the pause loop

// Wait until the next frame is due: on the audio queue when there is an
// audio device, else on a timer. Returns at once while fast-forwarding.
void     hsdl_pace();

// Refresh the window title (ROM, frame, measured fps, state) about once a
// second, or at once when pause or fast-forward changes.
void     hsdl_status(int frame);

#endif
