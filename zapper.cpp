/*===================================================================*/
/*                                                                   */
/*  zapper.cpp : NES Zapper (light gun) on NES controller port 2     */
/*                                                                   */
/*  See zapper.h for the wiring and the $4017 bit layout. The live   */
/*  sampling itself lives in the header so it can be inlined into    */
/*  K6502_Read(); this file only owns the pin setup and the          */
/*  once-per-frame presence detection.                               */
/*                                                                   */
/*===================================================================*/

#include "zapper.h"
#include <stdio.h>
#if ZAPPER_SUPPORTED && ZAPPER_MEASURE
#include "pico/time.h"
#include "hardware/irq.h"
#endif

/* Defined unconditionally so the flag exists on every board; stays false when
   Zapper support is compiled out. */
bool zapperconnected = false;

#if ZAPPER_SUPPORTED && ZAPPER_DEBUG
volatile uint32_t zapperDbgReads = 0;
volatile uint32_t zapperDbgReads4016 = 0;
volatile uint32_t zapperDbgLit = 0;
volatile uint32_t zapperDbgDark = 0;
volatile uint32_t zapperDbgTrig = 0;

/* Edge counts on the two lines.
 *
 * The read counters above cannot tell one real 100 ms trigger pull from a short
 * spurious glitch: both leave the bit set across many $4017 reads. Counting the
 * edges answers that directly - a pull is one edge, repeated phantom assertions
 * are many - and an interrupt sees pulses far too short for the per-frame poll.
 * Only compiled in when the measurement mode is off, so the two do not both try
 * to service the light line. */
#if !ZAPPER_MEASURE
#include "hardware/irq.h"
static volatile uint32_t m_lightEdges = 0;
static volatile uint32_t m_trigEdges = 0;

static void zapperDbgIrq(void)
{
    uint32_t ev = gpio_get_irq_event_mask(ZAPPER_D3);
    if (ev)
    {
        gpio_acknowledge_irq(ZAPPER_D3, ev);
        if (!(zapperBits() & ZAPPER_BIT_LIGHT)) /* became "light detected" */
            m_lightEdges++;
    }
    ev = gpio_get_irq_event_mask(ZAPPER_D4);
    if (ev)
    {
        gpio_acknowledge_irq(ZAPPER_D4, ev);
        if (zapperBits() & ZAPPER_BIT_TRIGGER) /* became "half-pulled" */
            m_trigEdges++;
    }
}

static void zapperDbgIrqInit(void)
{
    gpio_add_raw_irq_handler_masked((1u << ZAPPER_D3) | (1u << ZAPPER_D4), zapperDbgIrq);
    gpio_set_irq_enabled(ZAPPER_D3, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(ZAPPER_D4, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    irq_set_enabled(IO_IRQ_BANK0, true);
}
#endif
#if ZAPPER_DEBUG_OVERLAY
char zapperDbgText[ZAPPER_DBG_TEXT_LEN + 1] = "";
#endif
#endif

void initzapper(void)
{
#if ZAPPER_SUPPORTED
    /* Internal pull-ups: an empty port, and a plain NES/SNES pad (which leaves
       D3/D4 unconnected), both read high on each pin. */
    gpio_init(ZAPPER_D3);
    gpio_set_dir(ZAPPER_D3, GPIO_IN);
    gpio_pull_up(ZAPPER_D3);

    gpio_init(ZAPPER_D4);
    gpio_set_dir(ZAPPER_D4, GPIO_IN);
    gpio_pull_up(ZAPPER_D4);

    zapperconnected = false;
#if ZAPPER_DEBUG && !ZAPPER_MEASURE
    zapperDbgIrqInit();
#endif
    printf("Zapper: enabled on NES port 2 - D3 (light) = GPIO%d%s, D4 (trigger) = GPIO%d%s\n",
           ZAPPER_D3, ZAPPER_INVERT_D3 ? " inverted" : "",
           ZAPPER_D4, ZAPPER_INVERT_D4 ? " inverted" : "");
#if ZAPPER_MEASURE
    /* Printed so a measurement build identifies itself before any run finishes -
       the only other place the flash width appears is the [ZAPM] line, which
       takes several seconds to arrive. */
    printf("Zapper: display-lag MEASUREMENT build, white=%d frame(s) per flash, "
           "picture is replaced by a flash pattern\n",
           ZAPPER_MEASURE_WHITE);
#endif
#endif
}

void zapperPoll(void)
{
#if ZAPPER_SUPPORTED
    unsigned bits = zapperBits();

    /* Presence detection.
     *
     * Nothing plugged in, or a plain NES/SNES pad: both pins are held high by
     * the internal pull-ups, so bits == ZAPPER_BITS_IDLE (0x18).
     *
     * A Zapper actively drives the trigger line to the "not half-pulled" state
     * (logical 0) for as long as the trigger is released, and drives the light
     * line to 0 whenever its sensor sees light. Either makes bits != 0x18.
     *
     * The latch is sticky and never cleared while running: while the trigger is
     * half-pulled and the sensor sees no light, a connected Zapper reads 0x18
     * as well and is momentarily indistinguishable from an empty port. Clearing
     * the flag there would drop the very trigger event being reported.
     */
    if (!zapperconnected && bits != ZAPPER_BITS_IDLE)
    {
        zapperconnected = true;
        printf("Zapper: detected on NES controller port 2 (bits=0x%02x)\n", bits);
    }

#if ZAPPER_DEBUG
    /* Take and clear the counters filled from inside the register read paths. */
    uint32_t r17 = zapperDbgReads;
    uint32_t r16 = zapperDbgReads4016;
    uint32_t lit = zapperDbgLit;
    uint32_t dark = zapperDbgDark;
    uint32_t trig = zapperDbgTrig;
    zapperDbgReads = 0;
    zapperDbgReads4016 = 0;
    zapperDbgLit = 0;
    zapperDbgDark = 0;
    zapperDbgTrig = 0;

#if ZAPPER_DEBUG_OVERLAY
    /* On-screen readout, refreshed every frame. The raw levels are taken before
       ZAPPER_INVERT_* is applied, so they can be compared against the port pins
       directly. */
    snprintf(zapperDbgText, sizeof(zapperDbgText),
             "Z%c %d%d 17=%-4lu li=%-4lu da=%-4lu",
             zapperconnected ? '+' : '-',
             gpio_get(ZAPPER_D3) ? 1 : 0, gpio_get(ZAPPER_D4) ? 1 : 0,
             (unsigned long)(r17 > 9999 ? 9999 : r17),
             (unsigned long)(lit > 9999 ? 9999 : lit),
             (unsigned long)(dark > 9999 ? 9999 : dark));
#endif

#if ZAPPER_DEBUG_UART
    /* On the UART every ZAPPER_DEBUG_UART_FRAMES frames. The write is blocking -
       even this short line is ~55 characters, which at 115200 baud holds up the
       frame by a few ms and shows as a flicker - so it is deliberately
       infrequent, and the counters are accumulated over the whole interval so
       nothing is lost between reports. */
#define ZAPPER_DEBUG_UART_FRAMES 180 /* about 3 seconds */
    static unsigned frames = 0;
    static uint32_t s17 = 0, s16 = 0, sLit = 0, sDark = 0, sTrig = 0;
    s17 += r17;
    s16 += r16;
    sLit += lit;
    sDark += dark;
    sTrig += trig;
    if (++frames >= ZAPPER_DEBUG_UART_FRAMES)
    {
        /* ledge/tedge are hardware edge counts: how many times each line actually
           asserted, independent of how often the game happened to read it. One
           trigger pull is one tedge; phantom assertions show as many. */
        unsigned long le = 0, te = 0;
#if !ZAPPER_MEASURE
        le = (unsigned long)m_lightEdges;
        te = (unsigned long)m_trigEdges;
        m_lightEdges = 0;
        m_trigEdges = 0;
#endif
        printf("[ZAP] c%d D%d%d 16=%lu 17=%lu lit=%lu dark=%lu trg=%lu ledge=%lu tedge=%lu\n",
               zapperconnected ? 1 : 0,
               gpio_get(ZAPPER_D3) ? 1 : 0, gpio_get(ZAPPER_D4) ? 1 : 0,
               (unsigned long)s16, (unsigned long)s17,
               (unsigned long)sLit, (unsigned long)sDark, (unsigned long)sTrig,
               le, te);
        frames = 0;
        s17 = s16 = sLit = sDark = sTrig = 0;
    }
#endif /* ZAPPER_DEBUG_UART */

    (void)r16;
    (void)r17;
    (void)lit;
    (void)dark;
    (void)trig;
    (void)bits;
#endif /* ZAPPER_DEBUG */
#endif /* ZAPPER_SUPPORTED */
}

#if ZAPPER_SUPPORTED && ZAPPER_MEASURE
/*===================================================================*/
/*  Display-lag measurement                                          */
/*                                                                   */
/*  One cycle: hold the picture black for BLACK_FRAMES so the panel   */
/*  and the gun's sensor fully settle, then fill exactly one frame    */
/*  white. t0 is stamped when the first scanline of that white frame  */
/*  is handed to the video path; the light line is then sampled once  */
/*  per rendered scanline until it reports light, and the elapsed     */
/*  wall-clock time is the end-to-end lag: emulator -> scanout ->     */
/*  panel -> sensor -> GPIO.                                         */
/*                                                                   */
/*  Resolution caveat, worth keeping in mind when reading the spread: */
/*  sampling is ~63 us apart while a frame is being rendered, but     */
/*  core 0 then blocks in paceFrame() waiting for vsync and cannot    */
/*  sample at all. A flash whose light arrives inside that blind      */
/*  window is first seen at the start of the next frame, so a         */
/*  measurement can overshoot by up to the length of the window.      */
/*  That is a property of the emulator's pacing, not of the panel.    */
/*===================================================================*/

#define ZAPPER_M_BLACK_FRAMES 20 /* ~1/3 s of black between flashes       */
#define ZAPPER_M_TIMEOUT 8       /* give up waiting after this many frames */
#define ZAPPER_M_RUN 16          /* flashes per reported run               */
#define ZAPPER_M_BUCKETS 8       /* lag histogram, in whole frames         */

/* Frames of white per flash. One frame is the honest test - it is what the games
   actually give the gun - but an LCD may not reach full brightness that fast, and
   the sensor integrates over several scanlines. Widen it with
   -DZAPPER_MEASURE_WHITE=n to tell "panel too slow for one frame" apart from
   "lag too long": if misses vanish at 2 or 3 frames, the panel is the limit. */
#ifndef ZAPPER_MEASURE_WHITE
#define ZAPPER_MEASURE_WHITE 1
#endif

static int m_kind = 0; /* 0 = black frame, 1 = white frame */
static int m_countdown = ZAPPER_M_BLACK_FRAMES;
static int m_whiteLeft = 0;
static uint32_t m_t0 = 0;
static bool m_armed = false;
static bool m_seen = false;
static bool m_pendingArm = false;
static int m_waited = 0;

static uint32_t m_n = 0, m_min = 0xffffffffu, m_max = 0, m_miss = 0;
static uint64_t m_sum = 0;
static uint32_t m_frameN = 0, m_framePrev = 0;
static uint64_t m_frameSum = 0;
/* Lag histogram in whole frames, so a couple of stray late detections cannot
   hide the shape of the distribution the way they do in a 16-sample mean. */
static uint32_t m_bucket[ZAPPER_M_BUCKETS];
static uint32_t m_framePeriod = 16667; /* running estimate, for bucketing */
static uint32_t m_nIrq = 0, m_nPoll = 0;

/* Edge-interrupt capture.
 *
 * Polling alone cannot be trusted here: core 0 sits blocked in paceFrame() for
 * 3.6-6.6 ms of every frame and cannot sample at all, while the gun's light
 * output is a decaying pulse only ~1-2 ms wide. A pulse can therefore assert and
 * decay entirely inside the blind window, which would be indistinguishable from
 * the sensor never seeing the flash. An edge interrupt fires regardless of what
 * core 0 is doing, so comparing the two tells us which it was.
 *
 * The handler only stamps a timestamp and raises a flag; the statistics are
 * folded in from the polling context, so there is no race on the counters. */
static volatile uint32_t m_irqT = 0;
static volatile bool m_irqPending = false;

bool zapperMeasureIsWhite(void) { return m_kind == 1; }

static inline bool m_lightNow(void)
{
    /* bit 3 clear means light detected, after ZAPPER_INVERT_D3 */
    return (zapperBits() & ZAPPER_BIT_LIGHT) == 0;
}

static void m_gpioIrq(void)
{
    uint32_t ev = gpio_get_irq_event_mask(ZAPPER_D3);
    if (!ev)
        return;
    gpio_acknowledge_irq(ZAPPER_D3, ev);
    /* Both edges are enabled and the level is re-read, so this stays correct
       whichever way round ZAPPER_INVERT_D3 is set. */
    if (!m_irqPending && m_lightNow())
    {
        m_irqT = time_us_32();
        m_irqPending = true;
    }
}

void zapperMeasureInit(void)
{
    gpio_add_raw_irq_handler(ZAPPER_D3, m_gpioIrq);
    gpio_set_irq_enabled(ZAPPER_D3, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    irq_set_enabled(IO_IRQ_BANK0, true);
    printf("Zapper: light-line edge interrupt armed on GPIO%d\n", ZAPPER_D3);
}

static void m_record(uint32_t dt, bool fromIrq);

static void m_note(void)
{
    /* An interrupt-captured edge wins: its timestamp has no blind window. */
    if (m_irqPending)
    {
        uint32_t t = m_irqT;
        m_irqPending = false;
        uint32_t dt = t - m_t0;
        /* Belt and braces against a stale or wrapped capture: anything beyond the
           arm window cannot belong to this flash. */
        if (m_armed && !m_seen && dt <= (uint32_t)ZAPPER_M_TIMEOUT * m_framePeriod)
        {
            m_seen = true;
            m_armed = false;
            m_record(dt, true);
            return;
        }
    }
    if (!m_armed || m_seen)
        return;
    if (m_lightNow())
    {
        uint32_t dt = time_us_32() - m_t0;
        m_seen = true;
        m_armed = false;
        m_record(dt, false);
    }
}

static void m_record(uint32_t dt, bool fromIrq)
{
    m_n++;
    if (fromIrq)
        m_nIrq++;
    else
        m_nPoll++;
    m_sum += dt;
    if (dt < m_min)
        m_min = dt;
    if (dt > m_max)
        m_max = dt;
    uint32_t f = m_framePeriod ? dt / m_framePeriod : 0;
    if (f >= ZAPPER_M_BUCKETS)
        f = ZAPPER_M_BUCKETS - 1;
    m_bucket[f]++;
}

void zapperMeasureLine(int line)
{
    /* Stamped on the first rendered line of a flash. Note this cannot key off
       line == 0: InfoNES only calls the draw hooks for scanlines 4..235
       (InfoNES.cpp:873), so line 0 never arrives. Arming is driven by a flag set
       when the flash is scheduled instead, which stays correct whatever the
       first rendered line happens to be. */
    (void)line;
    if (m_pendingArm)
    {
        m_pendingArm = false;
        /* Drop any edge captured before this flash began. The interrupt fires on
           stray light during the black period too, and consuming a stale capture
           here makes t - m_t0 underflow into a ~4.29e9 us "measurement". */
        m_irqPending = false;
        m_t0 = time_us_32();
        m_armed = true;
        m_seen = false;
        m_waited = 0;
    }
    m_note();
}

void zapperMeasureFrame(void)
{
    /* Also sample here, immediately after paceFrame() has returned, so the
       blind window costs at most one frame rather than being skipped. */
    m_note();

    uint32_t now = time_us_32();
    if (m_framePrev)
    {
        m_frameSum += (uint32_t)(now - m_framePrev);
        m_frameN++;
        /* keep a running estimate so bucketing works from the first flash */
        m_framePeriod = (uint32_t)(m_frameSum / m_frameN);
    }
    m_framePrev = now;

    if (m_armed)
    {
        if (++m_waited >= ZAPPER_M_TIMEOUT)
        {
            m_armed = false;
            m_miss++;
        }
    }

    if (m_kind == 1)
    {
        if (--m_whiteLeft <= 0)
        {
            m_kind = 0;
            m_countdown = ZAPPER_M_BLACK_FRAMES;
        }
    }
    else if (--m_countdown <= 0)
    {
        m_kind = 1;
        m_whiteLeft = ZAPPER_MEASURE_WHITE;
        m_pendingArm = true; /* stamp t0 on the flash's first rendered line */
    }

    if (m_n + m_miss >= ZAPPER_M_RUN)
    {
        uint32_t frame = m_frameN ? (uint32_t)(m_frameSum / m_frameN) : 16667;
        /* Modal bucket: the frame delay the flash actually lands on most often.
           Far more trustworthy than the mean, which a single late stray detection
           shifts by several ms over a 16-sample run. */
        uint32_t modeIdx = 0;
        for (uint32_t i = 1; i < ZAPPER_M_BUCKETS; i++)
            if (m_bucket[i] > m_bucket[modeIdx])
                modeIdx = i;

        /* hist is the count of detections at 0,1,2..6,7+ frames of lag.
           irq/poll says how many were caught by the edge interrupt versus by
           polling: irq-heavy means the pulse was landing in core 0's blind
           window, i.e. earlier "misses" were the instrument, not the gun. */
        printf("[ZAPM] w%d n=%lu miss=%lu irq=%lu poll=%lu fr=%lu min=%lu max=%lu mode=%lu "
               "hist=%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu\n",
               ZAPPER_MEASURE_WHITE,
               (unsigned long)m_n, (unsigned long)m_miss,
               (unsigned long)m_nIrq, (unsigned long)m_nPoll, (unsigned long)frame,
               (unsigned long)(m_n ? m_min : 0), (unsigned long)m_max,
               (unsigned long)modeIdx,
               (unsigned long)m_bucket[0], (unsigned long)m_bucket[1],
               (unsigned long)m_bucket[2], (unsigned long)m_bucket[3],
               (unsigned long)m_bucket[4], (unsigned long)m_bucket[5],
               (unsigned long)m_bucket[6], (unsigned long)m_bucket[7]);
        for (uint32_t i = 0; i < ZAPPER_M_BUCKETS; i++)
            m_bucket[i] = 0;
        m_n = m_miss = 0;
        m_nIrq = m_nPoll = 0;
        m_min = 0xffffffffu;
        m_max = 0;
        m_sum = 0;
        m_frameN = 0;
        m_frameSum = 0;
    }
}
#endif /* ZAPPER_SUPPORTED && ZAPPER_MEASURE */
