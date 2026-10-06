# CHANGELOG

More games now run, among them Famicom Jump II, Devil Man, Saint Seiya - Ougon Densetsu, the Datach games and Super Mario Bros. + Tetris + Nintendo World Cup.
Garbled backgrounds fixed in Kyonshiizu 2, Bible Buffet and several other games.
A color palette can now be chosen in the settings menu.

# General Info

[Binaries for each configuration and PCB design are at the end of this page](#downloads___).

[Click here for tested configurations](https://github.com/PicoPlus-devel/pico-infonesPlus/blob/main/testresults.md).

[See setup section in readme how to install and wire up](https://github.com/PicoPlus-devel/pico-infonesPlus#pico-setup)

## PSRAM with a non-Winbond flash chip

Applies to any release. Some RP2350 boards, notably the Waveshare RP2350-PiZero, ship with a flash chip from a manufacturer other than Winbond, such as Puya. These chips leave the Quad Enable (QE) bit in Status Register 2 unset from the factory, which makes the board lock up once the RP2350 is overclocked ([#191](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/191)). Boards **without** PSRAM are not affected.

This can be fixed permanently with the [flash_config](https://github.com/fhoedemakers/flash_config) tool: flash **[FLASH_QE_SET_1.uf2](https://github.com/fhoedemakers/flash_config/blob/main/uf2/FLASH_QE_SET_1.uf2)** once via BOOTSEL, then flash the emulator as usual.

Keep in mind that `FLASH_QE_SET_1.uf2` must not be applied twice (recovery then requires erasing the flash with `universal_flash_nuke.uf2` first).

See also [PSRAM with a non-Winbond flash chip](https://github.com/PicoPlus-devel/pico-infonesPlus#psram-with-a-non-winbond-flash-chip) in the readme.

# v0.54

## New

- New setting **Button Layout**: with **SNES**, controllers with four face buttons use Y and B (Xbox: X and A, PlayStation: Square and Cross) as the NES B and A buttons ([#260](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/260)).

## Fixes

- SNES controller on the controller port: the first press of B no longer acts as A after a restart or after starting a game.
- Nekketsu Kouha Kunio-kun: part of the status bar scrolled along with the playfield ([#182](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/182)).

# v0.53

## New

- Mappers 152 and 207 are now supported: Saint Seiya - Ougon Densetsu, Arkanoid II, Gegege no Kitarou 2, Pocket Zaurus and newer dumps of Fudou Myouou Den.
- Mappers 37, 153, 154, 155, 157, 159 and 268 are now supported: Super Mario Bros. + Tetris + Nintendo World Cup, Famicom Jump II, Devil Man, the Datach games, SD Gundam Gaiden - Knight Gundam Monogatari, Magical Taruruuto-kun and several unlicensed games. Saving works in the games that have it ([#250](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/250), [#253](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/253)).
- The Limited Run Games versions of Star Wars and Star Wars - The Empire Strikes Back now run on RP2350 boards with PSRAM.
- Nintendo Campus Challenge 1991 (mapper 555) now runs. Press START on the title screen to begin the five-minute competition.
- Nine color palettes to choose from, under **NES Palette** in the settings menu or with START + LEFT/RIGHT during play. Includes the FirebrandX palettes and *Bubbles*, a less saturated one for LCD and OLED screens ([#256](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/256)). See [Color palettes](https://github.com/PicoPlus-devel/pico-infonesPlus#color-palettes). On the Fruit Jam these buttons no longer change the volume; use **Fruit Jam Volume Control** in the settings menu. Thanks to [dragonkn9](https://github.com/dragonkn9) for the Bubbles palette, [szuping](https://github.com/szuping) for the FirebrandX palettes and [chubunov](https://github.com/chubunov) for testing.
- New **Video Clock Fix** setting (boards with HSTX video whose only USB port is the board's own, such as the Pimoroni Pico Plus 2, the Adafruit Metro RP2350 and the Murmulator M2). Turn it on if your TV or monitor shows small dots or lines in the picture with Overclock on. A USB controller can then no longer be used; use a NES, SNES or Wii controller instead. See [Video Clock Fix](https://github.com/PicoPlus-devel/pico-infonesPlus#video-clock-fix).
- All settings return to their defaults once after updating to this version.

## Fixes

- Kyonshiizu 2, Mirai Shinwa Jarvas, Kyuukyoku Harikiri Stadium, Kamen Rider Club and Family Trainer 6 showed garbled or misplaced backgrounds.
- Dragon Ball Z - Kyoushuu! Saiya Jin: saving did not work ([#249](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/249)).
- Bible Buffet, Spiritual Warfare, Joshua, King of Kings and Sunday Funday showed garbled graphics or a black screen. Also fixed: Wally Bear and the No! Gang, Galactic Crusader and Mission Cobra.
- The **Controller Test** screen is now closed by holding SELECT + UP for 2 seconds. SELECT + START conflicted with some 8BitDo wireless controllers ([#255](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/255)).
- The **Overclock** setting always matches the speed the board runs at. It could show on while the board ran at the normal speed, or off while the board was still overclocked.
- AliExpress SNES-style USB controllers: B did not work until Y had been pressed once.

## Known issues

- Datach games: scanning barcode cards is not supported.
- *Over Obj*: a black bar appears in the middle of the screen during gameplay.

# v0.52

## New

- Famicom Disk System (FDS) games now also run on RP2040 boards. The FDS BIOS is still required.
- New **Overscan fix in menu** setting for TVs that cut off the edges of the menu. *Rows* leaves the top and bottom text rows blank, *Rows and columns* also the first and last columns. Games are not affected ([#244](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/244)). Thanks to [chubunov](https://github.com/chubunov).


## Fixes

- The **Controller Test** screen showed a broken controller outline and a misaligned list of input sources.
- Seicross (Rev 1), Spy vs Spy and Bird Week (Japanese versions) started with a black screen.
- High Speed and Pin Bot showed scrambled graphics on the title screen and the pinball table. A smaller artifact remains when the table scrolls up ([#245](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/245)).
- On boards with PSRAM, games could overwrite their own tile graphics while clearing video memory at startup, which left wrong or missing graphics in, among others, 1942, Tokkyuu Shirei Solbrain, Ganbare Goemon 2 and Star Wars - The Empire Strikes Back.
- Ganbare Goemon Gaiden 2 showed scrambled graphics ([#248](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/248)). Thanks to [szuping](https://github.com/szuping).
- Dragon Ball Z II, Dragon Ball Z III, Dragon Ball Z Gaiden, Rokudenashi Blues and SD Gundam Gaiden 2 and 3 could stay on a black screen. Their save data (EEPROM) is now supported, so saving works too ([#246](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/246)).

## Other

- The settings menu shows more options at once: the color palette now appears only while one of the menu colors is selected.
- In the settings menu, SELECT jumps directly to **SAVE**.
- RP2040 boards: about 57 KB of memory freed.
- RP2350 boards without PSRAM: FDS saves are now kept in one file per game, as on boards with PSRAM. Existing saves are picked up automatically.

# previous changes

See [HISTORY.md](https://github.com/PicoPlus-devel/pico-infonesPlus/blob/main/HISTORY.md)


<a name="downloads___"></a>
## Downloads by configuration

Binaries for each configuration are listed below. Binaries for Pico(2) also work for Pico(2)-w. No blinking led however on the -w boards.
For some configurations risc-v binaries are available. It is recommended however to use the arm binaries. 

>[!NOTE]
> Apart from the breadboard and PCB builds, no dedicated binaries are provided for the Pico w or Pico 2w. Instead, use the Pico or Pico 2 binaries. Enabling the LED on these boards causes too many issues. [#136](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/136) 

### Standalone boards

| Board | Binary | Readme | |
|:--|:--|:--|:--|
| Adafruit Metro RP2350 | [piconesPlus_AdafruitMetroRP2350_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitMetroRP2350_arm.uf2) | [Readme](README.md#adafruit-metro-rp2350) | |
| Adafruit Fruit Jam | [piconesPlus_AdafruitFruitJam_arm_piousb.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitFruitJam_arm_piousb.uf2) | [Readme](README.md#adafruit-fruit-jam)| |
| Waveshare RP2040-PiZero | [piconesPlus_WaveShareRP2040PiZero_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_WaveShareRP2040PiZero_arm.uf2) | [Readme](README.md#waveshare-rp2040rp2350-pizero-development-board)| [3-D Printed case](README.md#3d-printed-case-for-rp2040rp2350-pizero) |
| Waveshare RP2350-PiZero (*) | [piconesPlus_WaveShareRP2350PiZero_arm_piousb.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_WaveShareRP2350PiZero_arm_piousb.uf2) | [Readme](README.md#waveshare-rp2040rp2350-pizero-development-board)| [3-D Printed case](README.md#3d-printed-case-for-rp2040rp2350-pizero) |

(*) If you fitted this board with PSRAM and it has a non-Winbond flash chip, apply the [flash_config fix](#psram-with-a-non-winbond-flash-chip) before flashing the emulator.

### Breadboard

| Board | Binary | Readme |
|:--|:--|:--|
| Pico| [piconesPlus_AdafruitDVISD_pico_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico_arm.uf2) | [Readme](README.md#raspberry-pi-pico-or-pico-2-setup-with-adafruit-hardware-and-breadboard) |
| Pico W | [piconesPlus_AdafruitDVISD_pico_w_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico_w_arm.uf2) | [Readme](README.md#raspberry-pi-pico-or-pico-2-setup-with-adafruit-hardware-and-breadboard) |
| Pico 2 | [piconesPlus_AdafruitDVISD_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico2_arm.uf2) | [Readme](README.md#raspberry-pi-pico-or-pico-2-setup-with-adafruit-hardware-and-breadboard) |
| Pico 2 W | [piconesPlus_AdafruitDVISD_pico2_w_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico2_w_arm.uf2) | [Readme](README.md#raspberry-pi-pico-or-pico-2-setup-with-adafruit-hardware-and-breadboard) |
| Adafruit feather rp2040 DVI | [piconesPlus_AdafruitFeatherDVI_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitFeatherDVI_arm.uf2) | [Readme](README.md#adafruit-feather-rp2040-with-dvi-hdmi-output-port-setup) |
| Pimoroni Pico Plus 2 | [piconesPlus_AdafruitDVISD_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico2_arm.uf2) | [Readme](README.md#raspberry-pi-pico-or-pico-2-setup-with-adafruit-hardware-and-breadboard) |


### PCB Pico/Pico2 and Pimoroni Pico Plus 2

| Board | Binary | Readme |
|:--|:--|:--|
| Pico| [piconesPlus_AdafruitDVISD_pico_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico_arm.uf2) | [Readme](README.md#pcb-with-raspberry-pi-pico-or-pico-2-and-pimoroni-pico-plus-2) |
| Pico W| [piconesPlus_AdafruitDVISD_pico_w_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico_w_arm.uf2) | [Readme](README.md#pcb-with-raspberry-pi-pico-or-pico-2-and-pimoroni-pico-plus-2) |
| Pico 2 | [piconesPlus_AdafruitDVISD_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico2_arm.uf2) | [Readme](README.md#pcb-with-raspberry-pi-pico-or-pico-2-and-pimoroni-pico-plus-2) |
| Pico 2 W | [piconesPlus_AdafruitDVISD_pico2_w_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico2_w_arm.uf2) | [Readme](README.md#pcb-with-raspberry-pi-pico-or-pico-2-and-pimoroni-pico-plus-2) |
| Pimoroni Pico Plus 2 (PCB v2.6 and up, headers required) | [piconesPlus_AdafruitDVISD_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_AdafruitDVISD_pico2_arm.uf2) | [Readme](README.md#pcb-with-raspberry-pi-pico-or-pico-2-and-pimoroni-pico-plus-2) |

PCB [pico_nesPCB_v2.6.zip](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/pico_nesPCB_v2.6.zip)

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
| Waveshare RP2040-Zero | [piconesPlus_WaveShareRP2040ZeroWithPCB_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_WaveShareRP2040ZeroWithPCB_arm.uf2) | [Readme](README.md#pcb-with-waveshare-rp2040rp2350-zero) |
| Waveshare RP2350-Zero (*) | [piconesPlus_WaveShareRP2350ZeroWithPCB_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_WaveShareRP2350ZeroWithPCB_arm.uf2) | [Readme](README.md#pcb-with-waveshare-rp2040rp2350-zero) |

(*) If you fitted this board with PSRAM and it has a non-Winbond flash chip, apply the [flash_config fix](#psram-with-a-non-winbond-flash-chip) before flashing the emulator.

PCB: [Gerber_PicoNES_Mini_PCB_v2.0.zip](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/Gerber_PicoNES_Mini_PCB_v2.0.zip)

3D-printed case designs for PCB WS2XX0-Zero:
[https://www.thingiverse.com/thing:7041536](https://www.thingiverse.com/thing:7041536)

### PCB Waveshare RP2350-USBA with PCB
[Binary](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_WaveShare2350USBA_arm_piousb.uf2)

If you fitted this board with PSRAM and it has a non-Winbond flash chip, apply the [flash_config fix](#psram-with-a-non-winbond-flash-chip) before flashing the emulator.

PCB: [Gerber_PicoNES_Micro_v1.2.zip](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/Gerber_PicoNES_Micro_v1.2.zip)

[Readme](README.md#pcb-with-waveshare-rp2350-usb-a)

[Build guide](https://www.instructables.com/PicoNES-RaspberryPi-Pico-Based-NES-Emulator/)

### Pimoroni Pico DV

| Board | Binary | Readme |
|:--|:--| :--|
| Pico/Pico w | [piconesPlus_PimoroniDVI_pico_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_PimoroniDVI_pico_arm.uf2) | [Readme](README.md#raspberry-pi-pico-or-pico-2-setup-for-pimoroni-pico-dv-demo-base) |
| Pico 2/Pico 2 w | [piconesPlus_PimoroniDVI_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_PimoroniDVI_pico2_arm.uf2) | [Readme](README.md#raspberry-pi-pico-or-pico-2-setup-for-pimoroni-pico-dv-demo-base) |
| Pimoroni Pico Plus 2 | [piconesPlus_PimoroniDVI_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_PimoroniDVI_pico2_arm.uf2) | [Readme](README.md#raspberry-pi-pico-or-pico-2-setup-for-pimoroni-pico-dv-demo-base) |

> [!NOTE]
> On Pico W and Pico2 W, the CYW43 driver (used only for blinking the onboard LED) causes a DMA conflict with I2S audio on the Pimoroni Pico DV Demo Base, leading to emulator lock-ups. For now, no Pico W or Pico2 W binaries are provided; please use the Pico or Pico2 binaries instead. (#132)

### SpotPear HDMI

For more info about the SpotPear HDMI see this page : https://spotpear.com/index/product/detail/id/1207.html and https://spotpear.com/index/study/detail/id/971.html

The easiest way to set this up is using an expander board like this: https://shop.pimoroni.com/products/pico-omnibus?variant=32369533321299 

See also https://github.com/PicoPlus-devel/pico-infonesPlus/discussions/127 

| Board | Binary |
|:--|:--|
| Pico/Pico w | [piconesPlus_SpotpearHDMI_pico_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_SpotpearHDMI_pico_arm.uf2) |
| Pico 2/Pico 2 w | [piconesPlus_SpotpearHDMI_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_SpotpearHDMI_pico2_arm.uf2) |

### Murmulator M1

For more info about the Murmulator see this website: https://murmulator.ru/ and [#150](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/150)

| Board | Binary |
|:--|:--|
| Pico/Pico w | [piconesPlus_MurmulatorM1_pico_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_MurmulatorM1_pico_arm.uf2) |
| Pico 2/Pico 2 w | [piconesPlus_MurmulatorM1_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_MurmulatorM1_pico2_arm.uf2) |

### Murmulator M2

For more info about the Murmulator see this website: https://murmulator.ru/ and [#150](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/150)

| Board | Binary |
|:--|:--|
| Pico 2/Pico 2 w | [piconesPlus_MurmulatorM2_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_MurmulatorM2_arm.uf2) |

### Other downloads

- Metadata: [PicoNesMetadata.zip](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/PicoNesMetadata.zip)


Extract the zip file to the root folder of the SD card. Select a game in the menu and press START to show more information and box art. Works for most official released games. Screensaver shows floating random cover art.






























