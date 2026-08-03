# History of changes

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