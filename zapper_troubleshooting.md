# NES Zapper - troubleshooting and technical notes

Background for the [NES Zapper (light gun)](README.md#nes-zapper-light-gun) section of the readme.
Everything here concerns the custom PCB (HW_CONFIG 2), which is the only configuration where
the Zapper lines are wired to the Pico: NES controller port 2 pin **D3 -> GPIO28** (light sensor)
and pin **D4 -> GPIO27** (trigger).

Most of this only matters if the gun misbehaves. For normal use, the readme is enough.

## Before anything else: which gun

An **original Nintendo Zapper does not work on a flat panel without a hardware modification**, and
that is by far the most common reason for "nothing happens". Its light sensor is tuned to the short,
bright flash a CRT produces as the electron beam sweeps past the point being aimed at. An LCD or OLED
lights its pixels steadily and much less intensely, so the detector never reaches its threshold. This
is a property of the gun, not of the emulator or of the ROM patch, and neither can compensate for it.

Use a third-party gun designed for modern displays. The **Tomee Zapp Gun for NES** is the only gun
tested here and it works; [neslcdmod.com](https://neslcdmod.com/) lists it among the guns known to
work. Other guns are not guaranteed to: they may need 5 V, and the controller ports on the PCB are
connected to the board's 3 V rail.

The trigger is a plain switch and will usually still register on an unmodified original Zapper, so
"the trigger works but no shot ever hits anything" is exactly what an unsuitable gun looks like.

## If it does not work

| Symptom | What to look at |
| ------- | --------------- |
| Trigger responds, shots always miss | First check the gun is a suitable one (see above) - this is what an unmodified original Zapper does. Otherwise recalibrate, and if that fails the gun may not be seeing the screen: dim the room, move closer, or raise the TV brightness. |
| Every shot hits, wherever you aim | The emulator is telling the game "light detected" all the time. The usual cause is the light line polarity: cover the sensor with your hand and check with `-DZAPPER_DEBUG=1` that `dark` counts up rather than `lit`. If it is the wrong way round, flip `-DZAPPER_INVERT_D3`. Otherwise make sure nothing is drawn on screen (see the note on `ZAPPER_DEBUG` below), and lower the TV brightness/backlight. |
| Shots fire on their own while merely pointing at the screen, and real trigger pulls do nothing | **D3 and D4 are swapped.** The light line is being read as the trigger, so a bright picture fires a shot, and the trigger is being read as light. Aiming at the menu makes it flash and step through the options by itself. Check that port 2 D3 goes to GPIO28 and D4 to GPIO27. Note the idle state looks identical either way round, so the gun is still detected at boot and nothing warns you. |
| Calibration never settles | Try the delay by hand instead of the automatic mode. Note that this emulator does not display the top and bottom 4 scanlines, so a calibration target right at the very top edge of the picture may not be visible to the gun. |
| Nothing responds at all | Build from source with `-DZAPPER_DEBUG=1` and watch the UART: `zapper.cpp` then reports the raw GPIO28/GPIO27 levels and how the game is reading them. With the gun idle and the sensor covered, expect both lines low; pulling the trigger raises GPIO27 for about 100 ms, and aiming at a bright white area raises GPIO28. If the levels are the other way round, flip `-DZAPPER_INVERT_D3` / `-DZAPPER_INVERT_D4`. |

## Line polarity

The light line is inverted in software by default (`ZAPPER_INVERT_D3=1`) and the trigger line is
not. The NES inverts the light line between the controller port and the CPU bus, so `$4017` bit 3
reads 0 for "light detected" even though the pin itself is high when the sensor sees light. The PCB
wires the port straight to the Pico, which sees the raw sense, hence the correction. Measured with a
Tomee Zapp Gun; a gun that behaves differently can be corrected with the two `ZAPPER_INVERT_*`
options.

## Build options

All of these are off by default and only apply to builds made from source.

| Option | Effect |
| ------ | ------ |
| `-DZAPPER_D3=` / `-DZAPPER_D4=` | GPIO pins for the light and trigger lines. Default 28 and 27 on HW_CONFIG 2, `-1` (feature compiled out) elsewhere. |
| `-DZAPPER_INVERT_D3=` / `-DZAPPER_INVERT_D4=` | Line polarity. Default 1 and 0, see above. |
| `-DZAPPER_DEBUG=` | Diagnostics bit mask: `1` UART trace, `2` on-screen readout, `3` both. |
| `-DZAPPER_MEASURE=1` | Display-lag measurement mode. Replaces the picture with a flash pattern, so it is an instrument, not a play mode. |
| `-DZAPPER_MEASURE_WHITE=` | Frames of white per flash in measurement mode. Default 1. |

> [!IMPORTANT]
> `ZAPPER_DEBUG` is a diagnostic aid and both of its modes interfere with the gun. `1` (UART trace)
> blocks while it writes the line, which is long enough to disturb the video output and show as a
> flicker. `2` (on-screen readout) draws bright pixels on every frame, including the black and
> single-white-box frames the games use to locate the gun, so every shot registers as a hit. Use
> them to check wiring, then go back to a normal build to play.

## Measured behaviour

Measured on the PCB with a Raspberry Pi Pico (RP2040) and a Tomee Zapp Gun, using
`-DZAPPER_MEASURE=1`. That mode blacks out the picture, flashes it white, and times how long until
the light line responds - the whole chain of emulator, scanout, panel and sensor.

- **End-to-end lag is about 19-27 ms**, a little over one frame, and very repeatable. This is the
  delay the neslcdmod patches compensate for, and its stability is why calibration holds.
- **A single white frame is detected only about half the time**; three consecutive white frames are
  detected every time. An LCD pixel ramps towards white over several milliseconds and is then
  overwritten with black, so a one-frame flash never reaches full brightness. The games light each
  target for exactly one frame, which is why raising the TV brightness and backlight helps more than
  anything else, and why light gun games are unreliable on a flat panel in the first place.

Both figures came from interrupt-driven capture on the light line. Sampling the pin in software
alone reports a lag roughly 14 ms too high, because the emulator cannot sample while it is waiting
for the next frame.
