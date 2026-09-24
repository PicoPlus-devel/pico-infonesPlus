# History of changes

# v0.47

## Display

- Fix regression introduced in v0.43 that caused DVI monitors to no longer show an image. Since v0.43 the **DVI** setting sent an HDMI signal without audio, which monitors with a DVI input reject outright ([#217](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/217)). v0.42 was the last working version. HDMI mode is unchanged. Thanks to [javavi](https://github.com/javavi) for testing.

## Fixes

- RP2040 Clone boards no longer crash upon booting the emulator.[#214](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/214). Thanks to [chubunov](https://github.com/chubunov) for testing.
- Fixed a regression from v0.41 that made some games on RP2040 boards flicker red and drop from 60 to 30 fps. Two extra checks added in v0.41 slowed down the emulator core just enough for demanding games like *Prince of Persia* to fall behind. NSF playback was never affected.
- Other RP2040 performance fixes.
- A `.nes` file claiming **mapper 31** reports "unsupported" again instead of booting into the NSF player.


## Other
- On HSTX boards, the framerate overlay now also shows a resync counter (`R<n>`) — the number of times the watchdog has had to resync the display since boot. A steadily rising count points to a display that is struggling to hold sync.

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

See [NES Zapper (light gun)](https://github.com/PicoPlus-devel/pico-infonesPlus#nes-zapper-light-gun) in the readme for patching and calibration, and [zapper_troubleshooting.md](https://github.com/PicoPlus-devel/pico-infonesPlus/blob/main/zapper_troubleshooting.md) for troubleshooting, build options and measured timings.

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

When using headers, make sure to download the **latest** 3D printed top case from [Thingiverse](https://www.thingiverse.com/thing:6689537). The Pico sits higher on the board when headers are used, and only the newest top cover leaves room for the USB cable to fit. See also [3D printed case for PCB](https://github.com/PicoPlus-devel/pico-infonesPlus#3d-printed-case-for-pcb) in the readme.



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

# v0.43

## Audio

- Better audio mixing for NES games.
- Fixed a DC offset on the I2S audio.
- Added an alternative I2S audio driver based on the official pico-extras driver. This is an opt-in build option; the default driver is unchanged.

Special thanks go to [szuping](https://github.com/szuping) for his contribution to the audio fixes.

## Game fixes

- Fixed flickering on the bottom line of the top HUD in *Ganbare Goemon! - Karakuri Douchuu (Japan)*.

## HDMI

- More stable HDMI/DVI output on HSTX-based boards. Some monitors previously lost the picture intermittently and recovered with a visible glitch; the updated signal shape keeps the picture stable.

## Settings menu

- The options list is now scrollable, with up/down arrows when items extend beyond the visible window. SAVE/CANCEL/DEFAULT, the palette, and the help text stay anchored at fixed rows.
- Note: the settings file format was bumped; existing `settings_nes.dat` files will be reset to defaults on first boot.

## Developer

- Added a headless Linux host harness for the InfoNES core, so PPU/CPU/mapper bugs can be reproduced on a PC without flashing a board.



# v0.42

## Features

**Famicom Disk System**

- PSRAM is no longer required to run Famicom Disk System games. The only requirement now is an RP2350-based board.
- BIOS screen now displays correctly.
- Added "FDS Auto Insert Disk 1 on Start" setting. When set to Off, the BIOS animation keeps playing until the user presses Button2 (A) to insert the disk.

**NSF Player**

- Fixed audio clipping.
- Fixed pause/resume: elapsed time is preserved, and the player no longer skips to the next track when resuming.
- Fixed audio delay on PicoDVI boards. Audio now starts in sync with playback right from the first track.

**HDMI**

- Added 8:7 pixel aspect ratio support for HSTX boards.
- Added "Scanline Type" setting (HSTX boards only). Simple darkens odd lines; LCD adds a visible pixel-grid effect by also darkening alternating output columns. LCD is only available in 1:1 screen mode.
- Screen mode and scanline settings now behave consistently across all HDMI boards.

**USB Controllers**

- Added two-player USB controller support. On the Adafruit Fruit Jam, two controllers can be connected directly to the board’s USB ports for multiplayer games. 

This feature is confirmed working on the Adafruit Fruit Jam, Raspberry Pi Pico, and Pico 2. For the Raspberry Pi Pico and Pico 2, a USB Y-cable and USB hub are required.
Currently, this does not yet work on the Waveshare RP2350-PiZero.

Other configurations may also work when using a USB hub, but these have not yet been tested.

**Other**

- Added support for mapper 210 [#200](https://github.com/fhoedemakers/pico-infonesPlus/issues/200)
- Test builds (VX.X) now show the build date and time on the splash screen.
- Built against the latest TinyUSB version.


## Fixes

- Fixed external audio (PCM5000A) not working on RP2040 PicoDVI boards when enabled at boot, and resolved an intermittent audio glitch on the same boards.
- Fixed audio distortion during loud sound effects on the Adafruit Fruit Jam.
- Fixed volume imbalance between headphones and speaker on Adafruit Fruit Jam. Headphone volume is now automatically attenuated when headphones are inserted, so the volume control can be set for a comfortable speaker level without blasting headphones.
- Better audio mixing for VRC6 games like Akumajou Densetsu (Castlevania III JP) [#199](https://github.com/fhoedemakers/pico-infonesPlus/issues/199)
- Fixed background jitter in Akumajou Densetsu (Castlevania III JP) during vertical scroll sections. The playfield no longer shifts up and down by a pixel between frames.
- Fixed HUD scroll glitches in Rush'n Attack, Galaxian (JP) and Robocop 3.
- Fixed missing HUD in Alien 3.
- Fixed crash when opening the settings menu.
- Fixed a memory allocation bug in the HDMI driver on HSTX boards that wasted RAM.
- Fixed mapper 19 not working correctly [#200](https://github.com/fhoedemakers/pico-infonesPlus/issues/200)
- Improved display sync and fixed audio clipping on first launch by feeding blank frames.
- Fixed settings menu always showing unsaved changes after the new scanline setting was added.
- Updated the build configuration to be compatible with the latest TinyUSB version. [#202](https://github.com/fhoedemakers/pico-infonesPlus/issues/202) [#203](https://github.com/fhoedemakers/pico-infonesPlus/issues/203)



# v0.41 

## Features

**Famicom Disk System**

Note that FDS support requires an RP2350 board with PSRAM and a BIOS file at `/bios/fds-bios.rom`.

- Implement save games for games that support write save data back to disk, like Metroid and Zelda. Saves are stored as `/SAVES/gametitle_fds.sav` [#193](https://github.com/fhoedemakers/pico-infonesPlus/issues/193)
- Added an option to the settings menu to automatically swap disk sides. This setting is disabled by default. When it’s off, you can manually swap disks in-game using SELECT + START.

Audio is not perfect but acceptable.

**NSF sound playback**

- Added NSF playback. Emulator can load and play `.nsf` (Nintendo Sound Format) roms.
- Controls:
	- LEFT/RIGHT change track
	- Button2 Stop
	- Button1 Resume

**Settings menu**

- Better use of screen real estate:
	- SAVE / DEFAULT / CANCEL are on the same row.
	- FG/BG color codes now placed to the left to the color grid.

## Fixes

**Famicom Disk System**

- Fix disk error 24 in Metroid and possible in other games too. [#192](https://github.com/fhoedemakers/pico-infonesPlus/issues/192)
- Fix for game lock-up in Zelda when moving to the next screen during gameplay.

## Use of AI

FDS, NSF, additional mappers developed with the help of [Anthropic Claude Opus 4.6](https://www.anthropic.com/claude/opus)

# v0.40 (This is a re-release of v0.39 with some fixes and improvements)

- Fix incorrect parsing of region in NES 2.0 header. [#197](https://github.com/fhoedemakers/pico-infonesPlus/issues/197) Thanks to [@Lome-one](https://github.com/Lome-one) for reporting.
- The emulatortype is now correctly set to "NES" for Famicom Disk System games.

# v0.39

**Features:**
- Famicom Disk System (.fds) support on RP2350 boards with PSRAM (with limitations—see [#192](https://github.com/fhoedemakers/pico-infonesPlus/issues/192), [#193](https://github.com/fhoedemakers/pico-infonesPlus/issues/193), [#194](https://github.com/fhoedemakers/pico-infonesPlus/issues/194), [#195](https://github.com/fhoedemakers/pico-infonesPlus/issues/195)). Requires a BIOS file at `/bios/fds-bios.rom`. Disk swapping is done via the settings menu (SELECT+START).
- Added "reset game" option to the in-game settings menu.
- Removed unused 360 folder from metadata.

## Fixes

**Regional Support:**
- PAL/Dendy games now run at the correct frame rate on RP2350 boards (50Hz instead of 60Hz). RP2040 boards still run PAL/Dendy at 60Hz due to hardware constraints.

**Mapper & Game-Specific Fixes:**
- Mapper 85 now supported (tested with Tiny Toon Adventures 2 JP, Lagrange Point JP). Note: expansion audio for Mapper 85 is not yet emulated.
- Akumajou Special: Boku Dracula-kun (Mapper 23) - fixed black screen issue.
- Gimmick! (JP) - fixed black playfield after pressing Start.
- Robocop 3 (USA) - fixed black screen on startup (Mapper 1 fix).
- Castlevania III US (Mapper 5) and Castlevania III JP (Mapper 24) - fixed sound effects cutting out mid-level.
- Akumajou Densetsu (Castlevania III JP) - fixed graphical glitches in intro screen.
- Galaxian (JP) - fixed handling of incorrect ROM header info.
- Battletoads - Double Dragon - fixed missing sound effects.
- Double Dragon - partial fix for sound glitch.
- Added Sunsoft 5B expansion audio emulation for Mapper 69 (Gimmick!, Hebereke).

**Performance & Stability:**
- Fixed stack overflow when sorting large directory contents.
- Removed 40K fixed buffer used for Mapper 235 from heap memory.
- DVI mode: added watchdog function on core 1 to recover from occasional signal drops 

**Adafruit Fruit Jam:**
- Headphone detection now works correctly; plugging in headphones automatically mutes the speaker.
- External audio setting now enables the Fruit Jam's built-in speaker.

# v0.38

## New and improved Mapper support

- **RP2350 only:** Added support for Mapper 5 (MMC5 – *Castlevania III* US). Graphical glitches may still occur. These MMC 5 games are tested:
  - Castlevania III US
  - Gemfire (USA version)
  - Romance of the Three Kingdoms II (Japanese version)
  - Nobunaga’s Ambition II (Japanese/USA version)
- **All platforms (RP2040/RP2350):** Added support for Mapper 24 (VRC6A – *Castlevania III/Akumajou Densetsu* JP).
- Mapper 30 (NesMaker) now working.
- Fix HUD not displaying in Parodius DA! (Jap) - mapper 23
- Fix HUD not displaying in Fudou Myouou Den (Japan) - mapper 80
- ~~Fixed missing hit sound effects in Battletoads and Battletoads & Double Dragon. [#111](https://github.com/fhoedemakers/pico-infonesPlus/issues/111)~~
- Fix corrupt graphics in Punch Out! and Fire Emblem Gaiden (JP)

Many thanks to [@szuping](https://github.com/szuping) for testing the mapper changes.

Mapper fixes were developed with the help of [Anthropic Claude](https://www.anthropic.com/claude/opus).

## Display & audio

- Added a new **"Display Mode"** option on HSTX boards, allowing selection between HDMI and DVI. When DVI is selected, external audio (when available) is enabled by default. DVI does not have audio over HDMI.
- Enabling **External audio** no longer forces DVI mode.
- **Adafruit Fruit Jam:**
  - Headphone detection now works correctly. Plugging in headphones automatically mutes the internal speaker; unplugging them re-enables it.
  - Removed the setting and pushbutton1 functionality for muting the internal speaker. Headphone detection now automatically mutes the internal speaker.

## Fixes

- Updated the metadata and cover art pack with missing entries, including artwork for several Japanese titles. See the [Downloads section](#downloads___) below for the download link and instructions. Thanks again to [@DynaMight1124](https://github.com/DynaMight1124)

# v0.37

- Added support for DVI (Video only) mode on HSTX boards (GPIO 12–19) as an alternative to HDMI (Video + Audio). This allows the emulator to work on displays that do not support HDMI but do support DVI. [#171](https://github.com/fhoedemakers/pico-infonesPlus/issues/171). 
- SELECT + Button1: Force DVI mode (HSTX only). Useful if a DVI monitor shows no picture. This will restore the image.
- Enabling **External audio** also forces DVI mode. 
- DVI mode is the default on the Murmulator M2. Other HSTX boards default to HDMI mode.

## Fixes
- Some minor improvements to the menu and settings display.

# v0.36

For RP2350 boards using HSTX instead of PicoDVI, HDMI audio is now supported via the new HSTX video driver — this was not possible before. Huge thanks to [@fliperama86](https://github.com/fliperama86) for the awesome [pico_hdmi](https://github.com/fliperama86/pico_hdmi) driver and support.

HSTX boards with HDMI audio:
- Adafruit Fruit Jam
- Murmulator M2

Other RP2350 configurations that now use HSTX (GPIO 12–19) instead of PicoDVI:

- [Breadboard](https://github.com/fhoedemakers/pico-infonesPlus?tab=readme-ov-file#raspberry-pi-pico-or-pico-2-setup-with-adafruit-hardware-and-breadboard)
- [PCB](https://github.com/fhoedemakers/pico-infonesPlus?tab=readme-ov-file#pcb-with-raspberry-pi-pico-or-pico-2-and-pimoroni-pico-plus-2)
- [Adafruit Metro RP2350](https://github.com/fhoedemakers/pico-infonesPlus?tab=readme-ov-file#adafruit-metro-rp2350)
  
All other boards continue to use PicoDVI.

To use HDMI audio, disable External Audio in the Settings menu.
  
- Add [build-time ROM embedding](https://github.com/fhoedemakers/pico-infonesPlus?tab=readme-ov-file#building-with-an-embedded-rom): pass `-DEMBED_NES_ROM=/path/to/rom.nes` to CMake to embed a ROM into the firmware. Boots straight into the game without an SD card or menu. [@fliperama86](https://github.com/fliperama86)
- In-game BOOTSEL shortcut: SELECT + START + UP + A. [@fliperama86](https://github.com/fliperama86)
- Added option in Settings menu to enter BOOTSEL for flashing firmware.

## Fixes

- Various fixes and improvements

# v0.35 release notes

## Fixes

- The -s option (PSRAM cs pin) is removed from pico_shared/bld.sh script as it caused issues with the PSRAM settings in pico_shared/BoardConfigs.cmake. The PSRAM CS pin must be set correctly in pico_shared/BoardConfigs.cmake. (default is 47)
- Murmulator M1 and M2 fixes [#165](https://github.com/fhoedemakers/pico-infonesPlus/issues/165):
  - Second NES controller now works.
  - PS_RAM setting fixed for M2 board.
  - PS_RAM setting added for M1 board.

# v0.34 release notes

- Implemented savestates [#140](https://github.com/fhoedemakers/pico-infonesPlus/issues/140)
  - Up to 5 manual save state slots per game, accessible via the in-game menu (SELECT + START).
  - In-game quick savestate Save/Restore via (START + DOWN) and (START + UP).
  - Auto Save can be enabled per game, which allows to save the current state when exiting to the menu. When the game is launched, player can choose to restore that state.
  
  When loading a state, the game mostly resumes paused. Press START to continue playing.

  Save States should work for  mapper 0,1,2,3 and 4. Other mappers may or may not work. Below the games that use these mappers.

  - https://nesdir.github.io/mapper1.html
  - https://nesdir.github.io/mapper2.html
  - https://nesdir.github.io/mapper3.html
  - https://nesdir.github.io/mapper4.html

  The mapper number is also shown in the Save State screen.


- Added support for [Murmulator M1 and M2 boards](https://murmulator.ru). [@javavi](https://github.com/javavi)  [#150](https://github.com/fhoedemakers/pico-infonesPlus/issues/150)
  - M1: RP2040/RP2350
  - M2: RP2350 only
- **Fruit Jam only**: Add volume controls to settings menu. Can also be changed in-game via (START + LEFT/RIGHT). Note that too high volume levels may cause distortion. (Ext speaker, advised 16 db max, internal advised 18 dB max). Latest metadata package includes a sample.wav file to test the volume level.
- Updated PicoNesMetaData.zip: Added **sample.wav**. This sample will be played when using the Fruit Jam volume control in the settings menu. Note when **/soundrecorder.wav** is found, this file will be played in stead.
- **RP2350 only**: Updated the menu to also list .wav audio files.
- **RP2350 Only**: Added basic wav audio playback from within the menu. Press BUTTON2 or START to play the wav file. Tested with https://lonepeakmusic.itch.io/retro-midi-music-pack-1 The wav file must have the following specs:
  - 16/24 bit PCM wav files only.  (24 bit files are downsampled to 16 bit) 
  - 2ch stereo only.
  - Sample rate supported: 44100.
- **RP2350 with PSRAM only**: Record about 30 seconds of audio by pressing START to pause the game and then START + BUTTON1. Audio is recorded to **/soundrecorder.wav** on the SD-card.

## Fixes

- Fruit Jam audio fixes.
- Settings changed by in-game button combos are saved when exiting to menu.
- DVI audio volume was somewhat too low, fixed. [#146](https://github.com/fhoedemakers/pico-infonesPlus/issues/146)

# v0.33 release notes

- Added support for [Retro-bit 8 button Genesis-USB](https://www.retro-bit.com/controllers/genesis/#usb)
- Settings are saved to /settings_nes.dat instead of /settings.dat. This allows to have separate settings files for different emulators (e.g. pico-infonesPlus and pico-peanutGB etc.).
- Added a settings menu.
  - Main menu: press SELECT to open; adjust options without using in-game button combos.
  - In-game: press SELECT+START to open; from here you can also quit from the game.
- Switched to Fatfs R0.16.
- removed the build_* scripts. Use `bld.sh` in stead. Use `./bld.sh -h` for an overview of build options.

## Fixes

- Show correct buttonlabels in menus.
- removed wrappers for f_chdir en f_cwd, fixed in Fatfs R0.16. (there was a long standing issue with f_chdir and f_cwd not working with exFAT formatted SD cards.)

# v0.32 release notes

- Added support for Waveshare RP2350-USBA with PCB. More info and build guide at: https://www.instructables.com/PicoNES-RaspberryPi-Pico-Based-NES-Emulator/
- Added support for [Spotpear HDMI](https://spotpear.com/index/product/detail/id/1207.html) board.

## Known issues

- Pimoroni Pico DV: [#132](https://github.com/fhoedemakers/pico-infonesPlus/issues/132). Conflict with LED and I2S audio on Pico W and Pico2 W. No binaries provided for these boards. Use Pico or Pico2 binaries instead.
- PCB and breadboard [#136](https://github.com/fhoedemakers/pico-infonesPlus/issues/136). If you experience a red flashing screen during gameplay on Pico W/Pico 2W, use the Pico/Pico2 binaries instead.

# v0.31 release notes

- Adafruit Fruit Jam:
  - NeoPixel leds act as a VU meter. Can be toggled on or of via Button2 on the Fruit Jam, or SELECT + RIGHT on the controller.

- Screensaver
  - Block screensaver, which is shown when no metadata is available, is replaced by static floating image.

## Fixes

Better error handling in screensaver function and other minor fixes.

# v0.30 release notes

- Added support for [Adafruit Fruit Jam](https://www.adafruit.com/product/6200):  
  - Uses HSTX for video output.  
  - Audio is not supported over HSTX — connect speakers via the **audio jack** or the **4–8 Ω speaker connector**.  
  - Audio is simultaneousy played through speaker and jack. Speaker audio can be muted with **Button 1**.  
  - Controller options:  
    - **USB gamepad** on USB 1.  
    - **Wii Classic controller** via [Adafruit Wii Nunchuck Adapter](https://www.adafruit.com/product/4836) on the STEMMA QT port.  
  - Two-player mode:  
    - Player 1: USB gamepad (USB 1).  
    - Player 2: Wii Classic controller.  
    - Dual USB (USB 1 + USB 2) multiplayer is **not yet supported**.  
  - Scanlines can be toggled with **SELECT + UP**.  

- Added support for [Waveshare RP2350-PiZero](https://www.waveshare.com/rp2350-pizero.htm):  
  - Gamepad must be connected via the **PIO USB port**.  
  - The built-in USB port is now dedicated to **power and firmware flashing**, removing the need for a USB-Y cable.  
  - Optional: when you solder the optional PSRAM chip on the board, the emulator will make use of it. Roms will be loaded much faster using PSRAM.

- **RP2350 Only** Framebuffer implemented in SRAM. This eliminates the red flicker during slow operations, such as SD card I/O.

- **Cover art and metadata support**:  
  - Download pack [here](https://github.com/fhoedemakers/pico-infonesPlus/releases/latest/download/PicoNesMetadata.zip).  
  - Extract the zip contents to the **root of the SD card**.  
  - In the menu:  
    - Highlight a game and press **START** → show cover art and metadata.  
    - Press **SELECT** → show full game description.  
    - Press **B** → return to menu.  
    - Press **START** or **A** → start the game.

Huge thanks to [Gavin Knight](https://github.com/DynaMight1124) for providing the metadata and images as well as testing the different builds!

>[!NOTE]
> Cover art and metadata is available for most official released games.

- **Screensaver update**: when cover art is installed, the screensaver displays **floating random cover art** from the SD card.  
- Updated to **Pico SDK 2.2.0**  
- Updated to **lwmem V2.2.3**

## fixes

- Fixed a compiler error in pico_lib using SDK 2.2.2  [#129](https://github.com/fhoedemakers/pico-infonesPlus/issues/129)
- Moved the NES controller port 1 PIO from PIO0 to PIO1. This resolves an issue where polling the NES controller would hang in case HDMI (also driven by PIO0) uses GPIO pin numbers 32 and higher, resulting in no image.
- **RP2350 Only** Red screen flicker issue fixed. This was caused by slow operations such as SDcard I/O, which prevented the screen getting updated in time.


# v0.29 release notes 

- PSRAM will be used if detected. (RP2350 only, default pin 47). ROMs load from the SD card into PSRAM instead of flash. This speeds up loading because the board no longer has to reboot to copy the ROM from the SD card to flash. Based on https://github.com/AndrewCapon/PicoPlusPsram Boards with PSRAM are the [Adafruit Metro RP2350 with PSRAM](https://www.adafruit.com/product/6267) and [Pimoroni Pico Plus 2](https://shop.pimoroni.com/products/pimoroni-pico-plus-2?variant=42092668289107).
- Added -s option to bld.sh to allow an alternative GPIO pin for PSRAM chip select.
- Added support for [Pimoroni Pico Plus 2](https://shop.pimoroni.com/products/pimoroni-pico-plus-2?variant=42092668289107). (Uses hardware configuration 2, which is also used for breadboard and PCB). No extra binary needed.
- In some configurations, a second USB port can be added. This port can be used to connect a gamepad. The built-in usb port will be used for power and flashing the firmware. With this there is no need to use a USB-Y cable anymore. For more info, see [pio_usb.md](pio_usb.md). You have to build the firmware from source to enable this feature. The pre-built binaries do not support this.

> [!NOTE]
> Some low USB speed devices like keyboards do not work properly when connected to the second USB port. See https://github.com/sekigon-gonnoc/Pico-PIO-USB/issues/18

## Fixes
- Make PIO USB only available for RP2350, because of memory limitations on RP2040.
- Move PIO USB to Pio2, this fixes the NES controller not working on controller port 2.
- Fix save games not working when using PSRAM.

# v0.28 release notes 

- Enable I2S audio on the Pimoroni Pico DV Demo Base. This allows audio output through external speakers connected to the line-out jack of the Pimoroni Pico DV Demo Base. You can toggle audio output to this jack with SELECT + LEFT. Thanks to [Layer812](https://github.com/Layer812) for testing and providing feedback.

## Fixes
- improved error handling in build scripts.
- Github action can be started manual.

All changes are in the pico_shared submodule. When building from source, make sure you do a **git submodule update --init** from within the source folder to get the latest pico_shared module.