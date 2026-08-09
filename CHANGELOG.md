# CHANGELOG

New PCB design **v2.6** for Pico/Pico 2 with through-holes, now also usable with a Pimoroni Pico Plus 2. This is a hardware-only release: the emulator software is unchanged from v0.44.


# General Info

[Binaries for each configuration and PCB design are at the end of this page](#downloads___).

[Click here for tested configurations](https://github.com/fhoedemakers/pico-infonesPlus/blob/main/testresults.md).

[See setup section in readme how to install and wire up](https://github.com/fhoedemakers/pico-infonesPlus#pico-setup)

## PSRAM with a non-Winbond flash chip

Applies to any release. Some RP2350 boards, notably the Waveshare RP2350-PiZero, ship with a flash chip from a manufacturer other than Winbond, such as Puya. These chips leave the Quad Enable (QE) bit in Status Register 2 unset from the factory, which makes the board lock up once the RP2350 is overclocked ([#191](https://github.com/fhoedemakers/pico-infonesPlus/issues/191)). Boards **without** PSRAM are not affected.

This can be fixed permanently with the [flash_config](https://github.com/fhoedemakers/flash_config) tool: flash **[FLASH_QE_SET_1.uf2](https://github.com/fhoedemakers/flash_config/blob/main/uf2/FLASH_QE_SET_1.uf2)** once via BOOTSEL, then flash the emulator as usual.

Two things to keep in mind: `FLASH_QE_SET_1.uf2` must not be applied twice (recovery then requires erasing the flash with `universal_flash_nuke.uf2` first), and even after the fix these boards top out at 252 MHz — so the **Overclock** setting, and with it the VRC7 audio of *Lagrange Point (JP)*, cannot be used on them.

See also [PSRAM with a non-Winbond flash chip](https://github.com/fhoedemakers/pico-infonesPlus#psram-with-a-non-winbond-flash-chip) in the readme.

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






























