# CHANGELOG
Fix for crashing RP2040 clone boards on boot. Regression fix for heavy screenflicker op RP2040 in some games.
Beta **NES Zapper** (light gun) support on the custom PCB, a **Recently played** list of the last 20 games, no more reflashing a game that is already in flash on boards without PSRAM, and correct A/B buttons for **SNES controllers wired to a NES port**.


# General Info

[Binaries for each configuration and PCB design are at the end of this page](#downloads___).

[Click here for tested configurations](https://github.com/fhoedemakers/pico-infonesPlus/blob/main/testresults.md).

[See setup section in readme how to install and wire up](https://github.com/fhoedemakers/pico-infonesPlus#pico-setup)

## PSRAM with a non-Winbond flash chip

Applies to any release. Some RP2350 boards, notably the Waveshare RP2350-PiZero, ship with a flash chip from a manufacturer other than Winbond, such as Puya. These chips leave the Quad Enable (QE) bit in Status Register 2 unset from the factory, which makes the board lock up once the RP2350 is overclocked ([#191](https://github.com/fhoedemakers/pico-infonesPlus/issues/191)). Boards **without** PSRAM are not affected.

This can be fixed permanently with the [flash_config](https://github.com/fhoedemakers/flash_config) tool: flash **[FLASH_QE_SET_1.uf2](https://github.com/fhoedemakers/flash_config/blob/main/uf2/FLASH_QE_SET_1.uf2)** once via BOOTSEL, then flash the emulator as usual.

Two things to keep in mind: `FLASH_QE_SET_1.uf2` must not be applied twice (recovery then requires erasing the flash with `universal_flash_nuke.uf2` first), and even after the fix these boards top out at 252 MHz — so the **Overclock** setting, and with it the VRC7 audio of *Lagrange Point (JP)*, cannot be used on them.

See also [PSRAM with a non-Winbond flash chip](https://github.com/fhoedemakers/pico-infonesPlus#psram-with-a-non-winbond-flash-chip) in the readme.

# v0.47

## Display

- **DVI mode sends a real DVI signal again.** Since v0.43 the **DVI** setting sent an HDMI signal without audio, which monitors with a DVI input reject outright — no picture at all on those displays ([#217](https://github.com/fhoedemakers/pico-infonesPlus/issues/217)). v0.42 was the last working version. HDMI mode is unchanged. Thanks to [javavi](https://github.com/javavi) for testing. 
- Some displays lose sync on a plain DVI signal — that is why v0.43 changed it. If yours does, use **HDMI** mode.

## Fixes

- **RP2040: red flicker and a frame rate flipping between 60 and 30 are gone.** Two feature tests added in v0.41 sat on the 6502 core's hottest paths and cost ~3.5% of the frame budget — enough for heavier games such as *Prince of Persia* to miss the DVI scanline deadline, which paints the line red. NSF playback is unaffected.
- A `.nes` file claiming **mapper 31** reports "unsupported" again instead of booting into the NSF player.
- A flag test in `K6502_Read`'s ROM branch or `step()`'s loop costs 1-2% of the RP2040 frame budget. Keep feature dispatch out of both.
- RP2040 Clone boards no longer crash upon booting the emulator.[#214](https://github.com/fhoedemakers/pico-infonesPlus/issues/214). Thanks to [chubunov](https://github.com/chubunov) for testing.

# v0.46

## Menu

### Recently played

The menu now keeps a list of the **last 20 games you started**, newest first. Open it with **X** in the ROM browser - that is button 3 on any pad: X on a SNES controller, Y on XInput, Triangle on PlayStation, C on Genesis - or from the new **Recently played** entry at the top of the settings menu.

In the list, **A** starts the highlighted game, **SELECT** removes it from the list, **START** shows its artwork, and **B** closes the list. The settings menu only offers the entry when it is opened from the ROM browser, not from inside a running game.

The list is plain text in `/recent_NES.txt` in the SD card root, one line per game, so it survives a reboot and can be edited or deleted on a PC. A game that is no longer on the card is reported as missing when you try to start it and can be dropped with SELECT. A damaged or unreadable list simply comes up empty - unlike the settings file, nothing gets reset.

On boards without PSRAM, the game whose image is currently in flash is tagged **[READY]**: that is the one that starts without a reflash.

### No more reflashing a game that is already in flash

Boards without PSRAM copy the ROM into flash and reboot to start a game, and until now they did that on **every** launch - including for the game that was already in flash, costing seconds of blank screen for nothing. The emulator now records what it wrote (`/flashedrom.dat`) and skips programming when the selected game is exactly the image already there. The check is not just the file name: emulator, load address, byte swap, path, file size and timestamp must all match, and the flash contents are then verified with a CRC, so an image overwritten by another emulator under emuLoader is caught. Anything that does not match is flashed as before, and the record is dropped before the first erase, so a power cut during flashing can never leave a record that lies.

This replaces the old `/START` marker file, which is now gone. It could only say "do not flash", never *which* ROM was in flash, and nothing had created it since an earlier refactor - had it, the game would have run with a zero CRC and its save states would have gone to `/SAVESTATES/NES/00000000/`.

## Controllers

### NES Zapper (light gun)

Beta support for the **NES Zapper** (light gun) in NES controller **port 2**.

This requires the custom PCB: design **v2.1 and later** (so also the current v2.6) route port 2's D3 line to GPIO27 (light sensor) and its D4 line to GPIO28 (trigger), and the emulator now reads those two lines into bits 3 and 4 of `$4017`, the way a real NES does. Only the **piconesPlus_AdafruitDVISD_*** binaries carry the feature; on every other supported board GPIO27 and GPIO28 are already used for something else (I2S clock pins, NES port 2, DVI/TMDS pairs, the PIO USB DP pin), so it is compiled out there.

Note that the **v2.1 silkscreen labels the D3 and D4 pads the wrong way round** - what is printed as D3 is the physical D4 line and vice versa. Only the printing is wrong: the routing is the same on v2.1 and v2.6, a controller port soldered into the footprint works on both, and no firmware difference is needed. **v2.6 corrects the labels.**

The Zapper **cannot be used on the Murmulator M1 and M2 boards**. Those PCBs leave D3 and D4 of the controller ports unconnected, so the gun's light and trigger lines never reach the board at all. This cannot be fixed in firmware.

There is no setting to switch on. The gun is detected automatically when plugged in, since a Zapper holds its trigger line low while the trigger is released. A regular NES or SNES controller in port 2 keeps working alongside it - the pad's own data line in bit 0 is left untouched and only bits 3 and 4 come from the gun.

Games need the LCD-lag correction patches from [neslcdmod.com](https://neslcdmod.com/) - available for *Duck Hunt*, *Wild Gunman*, *Hogan's Alley*, *Duck Hunt VS* and *Barker Bill's Trick Shooting*. Unpatched originals cannot work on a flat panel, because they time their light detection against a CRT; the same games fail the same way on real NES hardware connected to a modern TV.

A suitable gun is needed as well: an **original Nintendo Zapper does not work on a flat panel without a hardware modification**, because its sensor is built around the brief bright flash of a CRT rather than the steady light of an LCD. Use a third-party gun made for modern displays - the **Tomee Zapp Gun for NES** is confirmed working and is what this was developed and tested with.

See [NES Zapper (light gun)](https://github.com/fhoedemakers/pico-infonesPlus#nes-zapper-light-gun) in the readme for patching and calibration, and [zapper_troubleshooting.md](https://github.com/fhoedemakers/pico-infonesPlus/blob/main/zapper_troubleshooting.md) for troubleshooting, build options and measured timings.

### SNES controllers on a NES controller port

A **SNES controller wired to a NES controller port** now uses its A and B buttons instead of B and Y. Such a pad shifts out B and Y where a NES pad has A and B, so those were the two buttons that acted as NES A and B, and physical A did nothing at all — in games and in the menu, where "choose" landed on B. Its four face buttons are now named rather than taken positionally: **A is NES A, B is NES B**, and in the menu A chooses while B goes back, the same as on USB and Wii Classic pads. X, Y, L and R have no NES equivalent and are ignored, as on those pads. NES pads are unaffected, and so are SNES->NES adapter cables with conversion logic inside, which report NES buttons in NES order.

The 12-button (16-clock) read is now confirmed against genuine SNES hardware, with a SNES controller port wired straight to the NES port GPIOs. Adapter *cables* are the thing to watch out for: several contain a converter, sometimes moulded into the plug, and then only 8 buttons can ever arrive.

### Controller Test

The **Controller Test** screen now names the buttons of a GPIO-wired pad according to what is actually attached. It used to label them in SNES order unconditionally, which is wrong for a NES pad: a NES pad shifts out the same first bits with different meanings (bit 0 is A, not B, and bit 1 is B, not Y). A NES pad now gets NES names with its A/X/L/R cells blanked, and a SNES pad gets SNES names. A port that has not identified itself yet - an idle SNES pad, an empty port and an 8-bit adapter cable are indistinguishable on the wire - shows NES names but keeps A/X/L/R on screen, so pressing one of those switches it to SNES names.

The screen also shows the **detected pad type** and, for the two GPIO ports, the **raw word the pad shifted out** (`Sent by pad: 0002 hex`), taken before any NES/SNES interpretation. This tells a button that never reaches the Pico apart from one that is decoded wrong - which is what identified a SNES->NES adapter cable as an active converter rather than a passive rewire.

## Fixes

- Leaving the **Controller Test** screen no longer drops into the screensaver. The settings menu's idle timeout mistook the "just came back from another screen" marker for a timestamp, so anything that opened a screen of its own looked like a minute of inactivity on return.
- Fixed a one-byte out-of-bounds write when byte-swapping an odd-sized ROM file, which corrupted the heap block next to it.

## Developer

- picoDVI (non-HSTX boards): line buffers queued for a line that a blank-margin change later puts inside a margin are now retired instead of being stranded, which used to deadlock the display with red lines. The menu's "do not reset the margins when a framebuffer is used" workaround was there for this. The line buffer pool can also be sized independently now with `-DDVI_N_LINE_BUFFERS=n` (default unchanged at 5).
- The HSTX debug dump reports HDMI audio underruns **per second** next to the cumulative count. The cumulative counter runs from boot and includes the ~11025/s produced while browsing ROMs, so it says nothing about whether underruns are still happening.
- `bld.sh` passes `$EXTRA_CMAKE_ARGS` through to cmake, so project-specific options can be set without changing the shared script.

# v0.45

This release only adds the new PCB design. There are no functional changes or fixes in the emulator software itself; the binaries are the same as in v0.44.

## PCB

New PCB **pico_nesPCB_v2.6.zip** includes through-holes, allowing a Raspberry Pi Pico, Pico 2, or [Pimoroni Pico Plus 2](https://shop.pimoroni.com/products/pimoroni-pico-plus-2?variant=42092668289107) **with pin headers** installed to be used. Soldering a headerless Pico or Pico 2 flat onto the board works as before. Earlier designs had no through-holes, which is why the Pico Plus 2 could not be used: its SP/CE connector on the back is in the way when the board lies flat.

When using headers, make sure to download the **latest** 3D printed top case from [Thingiverse](https://www.thingiverse.com/thing:6689537). The Pico sits higher on the board when headers are used, and only the newest top cover leaves room for the USB cable to fit. See also [3D printed case for PCB](https://github.com/fhoedemakers/pico-infonesPlus#3d-printed-case-for-pcb) in the readme.



# v0.44

## Game support

- Added VRC7 (Yamaha OPLL) FM synthesis for *Lagrange Point (JP)*, mapper 85. HSTX boards with PSRAM only; requires the new Overclock setting to be enabled in the settings menu. Audio may still exhibit occasional glitches.
- Fixed MMC5 expansion audio staying silent: a misspelled build flag left the MMC5 sound channel mixing out of every build. Games that use the MMC5's extra pulse/PCM channels (e.g. *Just Breed*, *Metal Slader Glory (JP)*) now play them on RP2350-based boards. (*Castlevania III (US)* is unaffected — it uses the MMC5 mapper but not its sound channels.)

## Settings menu

- New **Overclock** setting raises the CPU clock from 252 MHz to 378 MHz (with a matching core voltage increase). It only appears on HSTX boards with PSRAM and is currently only required for *Lagrange Point (JP)*. The chosen clock is stored in flash and applied at boot.
- The file browser now starts in `/roms/NES` instead of the SD card root. If that folder does not exist, it falls back to the root folder. Putting your ROMs in `/roms/NES` is now the recommended layout.
- When you leave a subfolder, the file browser now re-highlights the folder you came out of instead of jumping back to the top of the list.
- Note: the settings file format was bumped; existing `settings_nes.dat` files will be reset to defaults on first boot.

## Controllers

- New **Controller Test** screen, accessible from the settings menu. It shows a gamepad graphic that follows whichever controller you last pressed a button on (GPIO-wired NES/SNES pads, USB gamepads 1 and 2, and the Wii Classic controller), lighting up pressed buttons in green, plus a status list showing which input sources are connected. Useful for checking wiring and button mappings without starting a game. Hold SELECT+START for 2 seconds to exit.
- Improved USB gamepad support. Shoulder buttons now map correctly on the DualShock 4, DualSense, MantaPad and XInput controllers, and on XInput pads the left analog stick can be used as the D-pad.

## HDMI

- More reliable HDMI audio on HSTX boards: audio packets are now scheduled precisely against the video clock and carry proper IEC 60958 channel-status data. This fixes audio dropouts and improves compatibility with picky TVs and AV receivers.

## pico-bootLoader

- The emulator can now be built to run under the new [pico-bootLoader](https://github.com/fhoedemakers/pico-bootLoader) bootloader, which allows multiple emulators to be installed on a single board and selected at startup. Build with `-DBUILD_FOR_BOOTLOADER=ON`, optionally pinning the image to a 2 MB slot with `-DBUILD_FOR_BOOTLOADER_SLOT=N`. These builds show a new **Return to emulator selection** item in the menu. Standalone builds are unchanged.
- The bootloader's reserved flash region was reduced from 1 MB to 512 KB, handing that space back to the emulator (on 2 MB slots this raises the usable image size from 1.0 MB to 1.5 MB).

## Other fixes

- Synced the SD card driver with upstream pico_fatfs: improved RP2350 A/B detection and more stable SD card access.




# previous changes

See [HISTORY.md](https://github.com/fhoedemakers/pico-infonesPlus/blob/main/HISTORY.md)


<a name="downloads___"></a>
## Downloads by configuration

Binaries for each configuration are listed below. Binaries for Pico(2) also work for Pico(2)-w. No blinking led however on the -w boards.
For some configurations risc-v binaries are available. It is recommended however to use the arm binaries. 

>[!NOTE]
> No dedicated binaries are provided for the Pico w or Pico 2w. Instead, use the Pico or Pico 2 binaries. Enabling the LED on these boards causes too many issues. [#136](https://github.com/fhoedemakers/pico-infonesPlus/issues/136) 

### Standalone boards

| Board | Binary | Readme | |
|:--|:--|:--|:--|
| Adafruit Metro RP2350 | [piconesPlus_AdafruitMetroRP2350_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitMetroRP2350_arm.uf2) | [Readme](README.md#adafruit-metro-rp2350) | |
| Adafruit Fruit Jam | [piconesPlus_AdafruitFruitJam_arm_piousb.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitFruitJam_arm_piousb.uf2) | [Readme](README.md#adafruit-fruit-jam)| |
| Waveshare RP2040-PiZero | [piconesPlus_WaveShareRP2040PiZero_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_WaveShareRP2040PiZero_arm.uf2) | [Readme](README.md#waveshare-rp2040rp2350-pizero-development-board)| [3-D Printed case](README.md#3d-printed-case-for-rp2040rp2350-pizero) |
| Waveshare RP2350-PiZero (*) | [piconesPlus_WaveShareRP2350PiZero_arm_piousb.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_WaveShareRP2350PiZero_arm_piousb.uf2) | [Readme](README.md#waveshare-rp2040rp2350-pizero-development-board)| [3-D Printed case](README.md#3d-printed-case-for-rp2040rp2350-pizero) |

(*) If you fitted this board with PSRAM and it has a non-Winbond flash chip, apply the [flash_config fix](#psram-with-a-non-winbond-flash-chip) before flashing the emulator.

### Breadboard

| Board | Binary | Readme |
|:--|:--|:--|
| Pico| [piconesPlus_AdafruitDVISD_pico_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico_arm.uf2) | [Readme](README.md#raspberry-pi-pico-or-pico-2-setup-with-adafruit-hardware-and-breadboard) |
| Pico W | [piconesPlus_AdafruitDVISD_pico_w_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico_w_arm.uf2) | [Readme](README.md#raspberry-pi-pico-or-pico-2-setup-with-adafruit-hardware-and-breadboard) |
| Pico 2 | [piconesPlus_AdafruitDVISD_pico2_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico2_arm.uf2) | [Readme](README.md#raspberry-pi-pico-or-pico-2-setup-with-adafruit-hardware-and-breadboard) |
| Pico 2 W | [piconesPlus_AdafruitDVISD_pico2_w_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico2_w_arm.uf2) | [Readme](README.md#raspberry-pi-pico-or-pico-2-setup-with-adafruit-hardware-and-breadboard) |
| Adafruit feather rp2040 DVI | [piconesPlus_AdafruitFeatherDVI_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitFeatherDVI_arm.uf2) | [Readme](README.md#adafruit-feather-rp2040-with-dvi-hdmi-output-port-setup) |
| Pimoroni Pico Plus 2 | [piconesPlus_AdafruitDVISD_pico2_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico2_arm.uf2) | [Readme](README.md#raspberry-pi-pico-or-pico-2-setup-with-adafruit-hardware-and-breadboard) |


### PCB Pico/Pico2 and Pimoroni Pico Plus 2

| Board | Binary | Readme |
|:--|:--|:--|
| Pico| [piconesPlus_AdafruitDVISD_pico_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico_arm.uf2) | [Readme](README.md#pcb-with-raspberry-pi-pico-or-pico-2-and-pimoroni-pico-plus-2) |
| Pico W| [piconesPlus_AdafruitDVISD_pico_w_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico_w_arm.uf2) | [Readme](README.md#pcb-with-raspberry-pi-pico-or-pico-2-and-pimoroni-pico-plus-2) |
| Pico 2 | [piconesPlus_AdafruitDVISD_pico2_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico2_arm.uf2) | [Readme](README.md#pcb-with-raspberry-pi-pico-or-pico-2-and-pimoroni-pico-plus-2) |
| Pico 2 W | [piconesPlus_AdafruitDVISD_pico2_w_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico2_w_arm.uf2) | [Readme](README.md#pcb-with-raspberry-pi-pico-or-pico-2-and-pimoroni-pico-plus-2) |
| Pimoroni Pico Plus 2 (PCB v2.6 and up, headers required) | [piconesPlus_AdafruitDVISD_pico2_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico2_arm.uf2) | [Readme](README.md#pcb-with-raspberry-pi-pico-or-pico-2-and-pimoroni-pico-plus-2) |

PCB [pico_nesPCB_v2.6.zip](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/pico_nesPCB_v2.6.zip)

3D-printed case designs for PCB:

[https://www.thingiverse.com/thing:6689537](https://www.thingiverse.com/thing:6689537). 
For the latest two player PCB 2.0, you need:

- Top_v2.0_with_Bootsel_Button.stl. This allows for software upgrades without removing the cover. (*)
- Base_v2.0.stl
- Power_Switch.stl.
(*) in case you don't want to access the bootsel button on the Pico, you can choose Top_v2.0.stl

### PCB WS2XX0-Zero (PCB required)

| Board | Binary | Readme |
|:--|:--|:--|
| Waveshare RP2040-Zero | [piconesPlus_WaveShareRP2040ZeroWithPCB_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_WaveShareRP2040ZeroWithPCB_arm.uf2) | [Readme](README.md#pcb-with-waveshare-rp2040rp2350-zero) |
| Waveshare RP2350-Zero (*) | [piconesPlus_WaveShareRP2350ZeroWithPCB_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_WaveShareRP2350ZeroWithPCB_arm.uf2) | [Readme](README.md#pcb-with-waveshare-rp2040rp2350-zero) |

(*) If you fitted this board with PSRAM and it has a non-Winbond flash chip, apply the [flash_config fix](#psram-with-a-non-winbond-flash-chip) before flashing the emulator.

PCB: [Gerber_PicoNES_Mini_PCB_v2.0.zip](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/Gerber_PicoNES_Mini_PCB_v2.0.zip)

3D-printed case designs for PCB WS2XX0-Zero:
[https://www.thingiverse.com/thing:7041536](https://www.thingiverse.com/thing:7041536)

### PCB Waveshare RP2350-USBA with PCB
[Binary](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_WaveShare2350USBA_arm_piousb.uf2)

If you fitted this board with PSRAM and it has a non-Winbond flash chip, apply the [flash_config fix](#psram-with-a-non-winbond-flash-chip) before flashing the emulator.

PCB: [Gerber_PicoNES_Micro_v1.2.zip](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/Gerber_PicoNES_Micro_v1.2.zip)

[Readme](README.md#pcb-with-waveshare-rp2350-usb-a)

[Build guide](https://www.instructables.com/PicoNES-RaspberryPi-Pico-Based-NES-Emulator/)

### Pimoroni Pico DV

| Board | Binary | Readme |
|:--|:--| :--|
| Pico/Pico w | [piconesPlus_PimoroniDVI_pico_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_PimoroniDVI_pico_arm.uf2) | [Readme](README.md#raspberry-pi-pico-or-pico-2-setup-for-pimoroni-pico-dv-demo-base) |
| Pico 2/Pico 2 w | [piconesPlus_PimoroniDVI_pico2_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_PimoroniDVI_pico2_arm.uf2) | [Readme](README.md#raspberry-pi-pico-or-pico-2-setup-for-pimoroni-pico-dv-demo-base) |
| Pimoroni Pico Plus 2 | [piconesPlus_PimoroniDVI_pico2_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_PimoroniDVI_pico2_arm.uf2) | [Readme](README.md#raspberry-pi-pico-or-pico-2-setup-for-pimoroni-pico-dv-demo-base) |

> [!NOTE]
> On Pico W and Pico2 W, the CYW43 driver (used only for blinking the onboard LED) causes a DMA conflict with I2S audio on the Pimoroni Pico DV Demo Base, leading to emulator lock-ups. For now, no Pico W or Pico2 W binaries are provided; please use the Pico or Pico2 binaries instead. (#132)

### SpotPear HDMI

For more info about the SpotPear HDMI see this page : https://spotpear.com/index/product/detail/id/1207.html and https://spotpear.com/index/study/detail/id/971.html

The easiest way to set this up is using an expander board like this: https://shop.pimoroni.com/products/pico-omnibus?variant=32369533321299 

See also https://github.com/fhoedemakers/pico-infonesPlus/discussions/127 

| Board | Binary |
|:--|:--|
| Pico/Pico w | [piconesPlus_SpotpearHDMI_pico_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_SpotpearHDMI_pico_arm.uf2) |
| Pico 2/Pico 2 w | [piconesPlus_SpotpearHDMI_pico2_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_SpotpearHDMI_pico2_arm.uf2) |

### Murmulator M1

For more info about the Murmulator see this website: https://murmulator.ru/ and [#150](https://github.com/fhoedemakers/pico-infonesPlus/issues/150)

| Board | Binary |
|:--|:--|
| Pico/Pico w | [piconesPlus_MurmulatorM1_pico_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_MurmulatorM1_pico_arm.uf2) |
| Pico 2/Pico 2 w | [piconesPlus_MurmulatorM1_pico2_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_MurmulatorM1_pico2_arm.uf2) |

### Murmulator M2

For more info about the Murmulator see this website: https://murmulator.ru/ and [#150](https://github.com/fhoedemakers/pico-infonesPlus/issues/150)

| Board | Binary |
|:--|:--|
| Pico/Pico w | [piconesPlus_MurmulatorM2_arm.uf2](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/piconesPlus_MurmulatorM2_arm.uf2) |

### Other downloads

- Metadata: [PicoNesMetadata.zip](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/PicoNesMetadata.zip)


Extract the zip file to the root folder of the SD card. Select a game in the menu and press START to show more information and box art. Works for most official released games. Screensaver shows floating random cover art.






























