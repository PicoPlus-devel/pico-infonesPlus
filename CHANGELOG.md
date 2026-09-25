# CHANGELOG

Famicom Disk System games now also run on RP2040 boards.
New **Overscan fix in menu** setting for TVs that cut off the edges of the menu.
Optimized memory usage on RP2040 boards, freeing up about 57 KB of RAM.

# General Info

[Binaries for each configuration and PCB design are at the end of this page](#downloads___).

[Click here for tested configurations](https://github.com/PicoPlus-devel/pico-infonesPlus/blob/main/testresults.md).

[See setup section in readme how to install and wire up](https://github.com/PicoPlus-devel/pico-infonesPlus#pico-setup)

## PSRAM with a non-Winbond flash chip

Applies to any release. Some RP2350 boards, notably the Waveshare RP2350-PiZero, ship with a flash chip from a manufacturer other than Winbond, such as Puya. These chips leave the Quad Enable (QE) bit in Status Register 2 unset from the factory, which makes the board lock up once the RP2350 is overclocked ([#191](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/191)). Boards **without** PSRAM are not affected.

This can be fixed permanently with the [flash_config](https://github.com/fhoedemakers/flash_config) tool: flash **[FLASH_QE_SET_1.uf2](https://github.com/fhoedemakers/flash_config/blob/main/uf2/FLASH_QE_SET_1.uf2)** once via BOOTSEL, then flash the emulator as usual.

Two things to keep in mind: `FLASH_QE_SET_1.uf2` must not be applied twice (recovery then requires erasing the flash with `universal_flash_nuke.uf2` first)

See also [PSRAM with a non-Winbond flash chip](https://github.com/PicoPlus-devel/pico-infonesPlus#psram-with-a-non-winbond-flash-chip) in the readme.

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

# v0.51

## New

- New **Sprite Limit (8 per line)** setting. v0.50 limited the number of sprites per line to 8, like a real NES, which makes some games flicker. Turn the setting off for less flicker. It is on by default, because a few games such as *Felix the Cat* need it.

## Fixes

- *Arkanoid* on the custom PCB and the Adafruit breadboard setup (the `piconesPlus_AdafruitDVISD_*` binaries): the paddle was stuck on the right side of the screen and ignored the D-pad. Since v0.46 the emulator thought a Zapper was plugged into port 2, even when the port was empty or had a normal controller in it ([#234](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/234)). Thanks to [PetersonL-tech](https://github.com/PetersonL-tech).
- RP2040 boards: starting a MMC5 game (*Castlevania III*, *Just Breed*, ...) which was already in flash caused an out of memory panic. ([#242](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/242)). Thanks to [chubunov](https://github.com/chubunov).
- Games that use the sound chip's sample channel as a timer now work: *Over Obj* no longer shows a black screen.

## Known issues

- *Over Obj*: a black bar appears in the middle of the screen during gameplay.

# v0.50

## New

- **Mapper 196** is now supported, so *Super Bros. 11 - Mario Adventures* runs.
- MMC5 games such as *Castlevania III*, *Just Breed* and the Koei strategy games now run on RP2040 boards, not only on RP2350.

## Fixes

| Game | Mapper | Symptom |
|---|---|---|
| Battery-backed MMC5 games such as *Romance of the Three Kingdoms II*, *Nobunaga's Ambition II* and *Just Breed* | 5 | Saved games were not kept, and save states did not restore correctly |
| *Project Blue* | 111 | Blank screen at startup |
| *Bio Hazard*, *Yuefei*, *Bao Xiao San Guo* | 15 | Did not start |
| *Indiana Jones and the Last Crusade* (Taito) | 1 | Flickering black bars and a shaking logo on the title screen |
| *Battletoads & Double Dragon* | 7 | Flickering line through the title logo |
| *The Lion King* (unlicensed), *Jurassic Park - The Lost World* (unlicensed) | 4 | Black line through the picture |

- On boards using the DVI output, two shades of grey were shown as black, so dimmed text was invisible. In *Bio Hazard* only the highlighted menu entry could be read. Boards using HDMI were not affected.
- A maximum of 8 sprites per line is now shown, as on a real NES. In *Felix the Cat* Felix now disappears into the magic bag before a bonus level instead of staying visible on top of it. Busy scenes in some games may flicker more, as they do on the console ([#240](https://github.com/PicoPlus-devel/pico-infonesPlus/pull/240)). Thanks to [magistr6x9](https://github.com/magistr6x9).

# v0.49

## New

- **Mapper 111** is now supported, so *The Storied Sword* runs.
- **Mapper 263** is now supported, so *Boogerman II - The Final Adventure* runs instead of showing a black screen.
- **Mapper 208** is now supported, so *Street Fighter IV* runs.

## Fixes

| Game | Mapper | Symptom |
|---|---|---|
| *Mike Tyson's Punch-Out!!* | 9 | Scrambled line of big letters on the boxer name screens |
| *Fire Emblem Gaiden (JP)* | 10 | Stray specks on the title screen |
| *Mike Tyson's Punch-Out!!*, *Indiana Jones and the Last Crusade*, *Lunar Pool* | any | Wrong colours, and different colours on different boards |
| *Laser Invasion*, *Gun Sight (JP)*, *Anticipation*, *Al Unser Jr. Turbo Racing*, *Defenders of Dynatron City*, *Contra (JP)*, *Goemon (JP)* | any | Wrong or dropped sound channel |
| *240p Test Suite* | any | 8000 Hz sound test silent |
| *240p Test Suite* | any | Hill zone scroll test frozen and strobing |
| *Indiana Jones and the Last Crusade*, *Kid Kool* (and its Japanese version) | any | Garbled title screen |
| *Castlevania III: Dracula's Curse* | 5 | Status-bar lettering fills the playfield after the intro has run a while |
| *Romance of the Three Kingdoms II* | 5 | Patches of wrong tiles left on the map after a window closes |
| *Shin 4 Nin Uchi Mahjong (JP)* | 5 | Garbled title screen |
| *Genchou Hishi (JP)* | 5 | Corrupted map and portraits |
| *Just Breed (JP)* | 5 | Stray line of wrong tiles |
| Any MMC5 game shipping no character ROM | 5 | Division by zero on every scanline |
| *Knight on the Moon* | 30 | Flickering, garbled graphics |
| *Dungeons & Doomknights* | 30 | Flickering title screen, game unresponsive |
| *Full Quiet Steam* | 4 | Scrambled player, enemies and status bar |
| *Rad Racer II* | 4 | Wrong game screen, far half of the road 128 pixels off |
| *Gauntlet* | 4 | Fourth screen page on the cartridge was unused |
| *Napoleon Senki (JP)* | 77 | Fourth screen page on the cartridge was unused |
| *Rally Bike*, *Dash Yarou (JP)* | 2 | Stray line at the edge of the status bar |
| *Knight Rider* | 1 | Stray line at the edge of the status bar |
| *DataMan* | 34 | Black screen |
| *Street Fighter VI* | 4 | Black screen |
| *Millionaire (PAL)* | 79 | Black screen |
| *Mortal Kombat 3 - Special 56 Peoples* and other J.Y. Company games | 90 | Screen artifacts and a broken title screen |
| *Teenage Mutant Ninja Turtles (JP)*, *Teenage Mutant Ninja Turtles 2 - The Manhattan Project (JP)*, *Batman 4*, *FIFA International 2' 96*, *Pizza Pop Mario* | 25 | Black screen |
| *Gradius II (JP)*, *Bio Miracle Bokutte Upa (JP)* | 25 | Missing status bar |
| *Racer Mini Yonku (JP)* | 25 | Garbled copyright line |
| *The Jetsons - Cogswell's Caper (JP)* | 48 | Title screen unreadable |
| *The Flintstones - The Rescue of Dino & Hoppy (JP)*, *Captain Saver (JP)*, *Bubble Bobble 2 (JP)*, *Don Doko Don 2 (JP)*, *Bakushou!! Jinsei Gekijou 3 (JP)* | 48 | Garbled sprites |

Not tied to one game:

- Games that carry a newer cartridge header are now identified correctly instead of being run as whatever older cartridge type their number happened to match.
- Writing to one particular part of the screen layout no longer corrupts the stored colour palette.

# v0.48

## New

- **USB drive mode**: the settings menu can now show the SD card on your computer over USB, so you can add or remove games without taking the card out. Open the menu with SELECT from the game list, pick *USB drive mode*, then eject the drive on your computer or press B when you are done. It is not available while a game is running.
- On boards where controllers plug into the console's own USB port, that same port is the one you connect to the computer, so a USB controller cannot be used in this mode: press B on a controller in the NES port, or eject from the computer. The console restarts afterwards. On RP2040 boards the screen also goes black while the card is mounted; the menu explains this first and lets you back out.

## Fixes

- Fixed a flickering band of wrong graphics across part of the screen in games that scroll between two name tables: *Final Fantasy*, *Zelda II*, *The Addams Family* and *Super Xevious*. The emulator kept drawing the wrong half of the map until partway down the frame.
- On HSTX boards the picture sat four lines too low, leaving a wide black band at the top and none at the bottom. It is now centered again ([#225](https://github.com/PicoPlus-devel/pico-infonesPlus/pull/225)). Thanks to [zZmiz](https://github.com/zZmiz).

# previous changes

See [HISTORY.md](https://github.com/PicoPlus-devel/pico-infonesPlus/blob/main/HISTORY.md)


<a name="downloads___"></a>
## Downloads by configuration

Binaries for each configuration are listed below. Binaries for Pico(2) also work for Pico(2)-w. No blinking led however on the -w boards.
For some configurations risc-v binaries are available. It is recommended however to use the arm binaries. 

>[!NOTE]
> No dedicated binaries are provided for the Pico w or Pico 2w. Instead, use the Pico or Pico 2 binaries. Enabling the LED on these boards causes too many issues. [#136](https://github.com/PicoPlus-devel/pico-infonesPlus/issues/136) 

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
| Pico/Pico w | [piconesPlus_MurmulatorM2_arm.uf2](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/piconesPlus_MurmulatorM2_arm.uf2) |

### Other downloads

- Metadata: [PicoNesMetadata.zip](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/PicoNesMetadata.zip)


Extract the zip file to the root folder of the SD card. Select a game in the menu and press START to show more information and box art. Works for most official released games. Screensaver shows floating random cover art.






























