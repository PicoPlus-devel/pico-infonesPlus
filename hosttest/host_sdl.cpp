// Live SDL2 front end for the host harness (NES_LIVE=1, see README.md).
//
// Shows each frame in a window, plays the mixed audio and reads the keyboard
// as controller 1, so a game can be watched, heard and played on the desktop.
// host_main.cpp stays in charge of the emulation; this file only moves
// pixels, samples and key state, and paces the loop to real time.
//
// Pacing follows the audio clock: after each frame the loop waits until no
// more than three frames of audio are queued, so emulation runs exactly as
// fast as the output consumes the samples the APU produces per frame. The
// wait never exceeds one frame period, so a stalled output cannot slow the
// game down. Without an audio device a timer on the frame period does the
// same job.

#include <cstdio>
#include <cstring>
#include <SDL.h>

#include "host_sdl.h"

static constexpr int W = 256, H = 240;

static SDL_Window       *win;
static SDL_Renderer     *ren;
static SDL_Texture      *tex;
static SDL_AudioDeviceID adev;
static bool              have_frame;
static char              title_base[160];

static uint32_t frame_period_us;
static uint64_t next_tick;
static uint32_t chunk_bytes;    // size of the last queued frame of audio
static bool     paused;
static bool     fast_forward;
static bool     title_dirty = true;
static int      last_frame = -1;
static uint32_t fps_t0, fps_frames;
static double   fps;

bool hsdl_init(const char *title, int scale, bool audio_on, uint32_t frame_us)
{
    frame_period_us = frame_us ? frame_us : 16639;
    snprintf(title_base, sizeof title_base, "%s", title);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "LIVE: SDL video init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   // nearest neighbour
    if (scale < 1) scale = 1;
    win = SDL_CreateWindow(title_base, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           W * scale, H * scale, SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "LIVE: cannot open a window: %s\n", SDL_GetError());
        return false;
    }
    // No PRESENTVSYNC: the audio clock paces the loop, a display vsync would
    // fight it.
    ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) {
        fprintf(stderr, "LIVE: cannot create a renderer: %s\n", SDL_GetError());
        return false;
    }
    SDL_RenderSetLogicalSize(ren, W, H);
    SDL_RenderSetIntegerScale(ren, SDL_TRUE);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, W, H);
    if (!tex) {
        fprintf(stderr, "LIVE: cannot create a texture: %s\n", SDL_GetError());
        return false;
    }

    if (audio_on) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            fprintf(stderr, "LIVE: no audio (%s), pacing on a timer\n", SDL_GetError());
        } else {
            SDL_AudioSpec want, have;
            memset(&want, 0, sizeof want);
            want.freq     = 44100;
            want.format   = AUDIO_S16SYS;
            want.channels = 2;
            want.samples  = 512;
            adev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
            if (!adev)
                fprintf(stderr, "LIVE: cannot open audio output (%s), pacing on a timer\n",
                        SDL_GetError());
            else
                SDL_PauseAudioDevice(adev, 0);
        }
    }

    printf("LIVE: video %s, audio %s, window %dx%d\n",
           SDL_GetCurrentVideoDriver(),
           adev ? SDL_GetCurrentAudioDriver() : "off",
           W * scale, H * scale);
    printf("LIVE: pad  arrows=D-pad  Z=A  X=B  S=Start  A=Select\n"
           "LIVE: keys Space=pause  N=step  Tab=fast-forward (hold)  F12=dump frame  "
           "Esc=quit\n");
    fflush(stdout);
    return true;
}

void hsdl_quit()
{
    if (adev) SDL_CloseAudioDevice(adev);
    if (tex)  SDL_DestroyTexture(tex);
    if (ren)  SDL_DestroyRenderer(ren);
    if (win)  SDL_DestroyWindow(win);
    adev = 0; tex = nullptr; ren = nullptr; win = nullptr;
    SDL_Quit();
}

void hsdl_redraw()
{
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (have_frame)
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);
}

void hsdl_present(const uint32_t *px)
{
    SDL_UpdateTexture(tex, nullptr, px, W * (int)sizeof(uint32_t));
    have_frame = true;
    hsdl_redraw();
}

void hsdl_audio(const int16_t *buf, int frames)
{
    if (!adev || paused || fast_forward || frames <= 0) return;
    chunk_bytes = (uint32_t)frames * 2 * sizeof(int16_t);
    if (SDL_GetQueuedAudioSize(adev) > 8 * chunk_bytes)
        return;                 // output stalled: drop, see hsdl_pace()
    SDL_QueueAudio(adev, buf, chunk_bytes);
}

// Keyboard -> NES pad. Same keys as a USB keyboard on the device
// (pico_shared/hid_app.cpp), matched by position like the HID usage codes.
static uint8_t keyboard_pad()
{
    static const struct { SDL_Scancode key; uint8_t bit; } map[] = {
        { SDL_SCANCODE_Z,     0x01 },   // A
        { SDL_SCANCODE_X,     0x02 },   // B
        { SDL_SCANCODE_A,     0x04 },   // Select
        { SDL_SCANCODE_S,     0x08 },   // Start
        { SDL_SCANCODE_UP,    0x10 },
        { SDL_SCANCODE_DOWN,  0x20 },
        { SDL_SCANCODE_LEFT,  0x40 },
        { SDL_SCANCODE_RIGHT, 0x80 },
    };
    const Uint8 *ks = SDL_GetKeyboardState(nullptr);
    uint8_t pad = 0;
    for (const auto &m : map)
        if (ks[m.key]) pad |= m.bit;
    return pad;
}

uint8_t hsdl_poll(int *events)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            *events |= HSDL_EV_QUIT;
        } else if (e.type == SDL_KEYDOWN) {
            const bool repeat = e.key.repeat;
            switch (e.key.keysym.scancode) {
            case SDL_SCANCODE_ESCAPE:
                *events |= HSDL_EV_QUIT;
                break;
            case SDL_SCANCODE_SPACE:
                if (repeat) break;
                paused = !paused;
                title_dirty = true;
                if (paused && adev) SDL_ClearQueuedAudio(adev);
                next_tick = 0;
                break;
            case SDL_SCANCODE_N:            // held: keeps stepping
                if (paused) *events |= HSDL_EV_STEP;
                break;
            case SDL_SCANCODE_F12:
                if (!repeat) *events |= HSDL_EV_DUMP;
                break;
            default:
                break;
            }
        } else if (e.type == SDL_WINDOWEVENT) {
            if (e.window.event == SDL_WINDOWEVENT_EXPOSED ||
                e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                hsdl_redraw();
        }
    }
    const bool ff = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_TAB] != 0;
    if (ff != fast_forward) {
        fast_forward = ff;
        title_dirty = true;
        next_tick = 0;
        if (ff && adev) SDL_ClearQueuedAudio(adev);
    }
    return keyboard_pad();
}

bool hsdl_paused()
{
    return paused;
}

void hsdl_idle()
{
    SDL_Delay(15);
}

void hsdl_pace()
{
    if (paused || fast_forward) return;
    const uint64_t freq   = SDL_GetPerformanceFrequency();
    const uint64_t period = freq * frame_period_us / 1000000u;
    uint64_t now = SDL_GetPerformanceCounter();
    if (!next_tick || now > next_tick + 4 * period)
        next_tick = now;                        // start, or resync after a stall
    next_tick += period;
    if (adev) {
        // Keep ~3 frames (~50 ms) queued. A healthy output drains one frame
        // of audio per frame period, and each frame that paces on it
        // re-anchors the schedule, so the audio clock rules. The WSLg
        // PulseAudio sink sometimes stalls for seconds: then the wait gives
        // up one period past the schedule, which keeps the game at full
        // speed (hsdl_audio stops queueing at 8 frames, so latency stays
        // bounded and the sound comes back within a few frames once it
        // resumes).
        const uint32_t limit    = 3 * chunk_bytes;
        const uint64_t deadline = next_tick + period;
        while (SDL_GetQueuedAudioSize(adev) > limit) {
            if ((now = SDL_GetPerformanceCounter()) >= deadline) break;
            SDL_Delay(1);
        }
        if (now < deadline)
            next_tick = now;
        return;
    }
    while ((now = SDL_GetPerformanceCounter()) < next_tick) {
        const uint64_t ms = (next_tick - now) * 1000u / freq;
        SDL_Delay(ms > 1 ? (uint32_t)(ms - 1) : 0);
    }
}

void hsdl_status(int frame)
{
    const uint32_t t = SDL_GetTicks();
    if (frame != last_frame) {
        if (paused) title_dirty = true;         // show every single step
        last_frame = frame;
        fps_frames++;
    }
    if (!fps_t0) fps_t0 = t;
    if (t - fps_t0 >= 1000) {
        fps = fps_frames * 1000.0 / (double)(t - fps_t0);
        fps_frames = 0;
        fps_t0 = t;
        title_dirty = true;
    }
    if (!title_dirty) return;
    title_dirty = false;
    char buf[256];
    snprintf(buf, sizeof buf, "%s | frame %d | %.1f fps%s", title_base, frame, fps,
             paused ? " | PAUSED (N = step)" : fast_forward ? " | FAST-FORWARD" : "");
    SDL_SetWindowTitle(win, buf);
}
