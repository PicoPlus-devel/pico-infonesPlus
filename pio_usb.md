# Build with PIO USB

It is possible to build the emulator with PIO USB support. This enables the use of a second USB port for attaching a game controller.

To enable this, you have to build the binary from source using the information below.

The built-in usb port is used for providing power and for flashing the firmware. The PIO USB port is used for attaching a game controller. In this situation the built-in usb port cannot be used to connect a game controller.

> [!CAUTION]
> Do not provide power to the PIO USB port. It is not designed to provide power and may damage the board.

## Supported configurations

### Breadboard

> [!NOTE]
> This works only with a Pico 2 or Pico2 w board. (RP2350)
> Pico and Pico w (RP2040) do not have sufficient memory for this to work.
 
Add this Adafruit USB Type C breakout board to the breadboard: https://www.adafruit.com/product/4090

![image](https://github.com/user-attachments/assets/738d71d8-dbad-421f-b171-c42263338515)


Pinouts:

- **VBUS**: Connect to VBUS or VSYS on the Pico
- **GND**: Connect to GND on the Pico
- **D+**: Connect to GPIO 20 on the Pico
- **D-**: Connect to GPIO 21 on the Pico


![image](https://github.com/user-attachments/assets/743c77c9-b186-4bfe-8cb0-4ecca131e762)

### Adafruit Metro RP2350

Solder this USB type A jack breakout cable to the Adafruit Metro RP2350 USB host port: https://www.adafruit.com/product/4449

![image](https://github.com/user-attachments/assets/4819f7c1-9759-4fc9-9452-e082d315efb2)

You need some headers like these: https://www.adafruit.com/product/392

![image](https://github.com/user-attachments/assets/62bba136-05e2-457b-b42d-a3990d11778e)


Pinouts:
- **GND**: Black
- **D-**: White
- **D+**: Green
- **5V**: Red

![image](https://github.com/user-attachments/assets/b05a4c47-cd3d-45f9-ab04-327c7a6136b9)

## Building

Requirements:

- Pico SDK 2.2.0 or newer with the TinyUSB version it includes (TinyUSB 0.18.0). Do not switch the SDK's TinyUSB submodule to its master branch.

- Get Pico PIO USB: https://github.com/sekigon-gonnoc/Pico-PIO-USB and
- Point the environment variabele PICO_PIO_USB_PATH to this repository

```
cd ~
git clone https://github.com/sekigon-gonnoc/Pico-PIO-USB.git
export PICO_PIO_USB_PATH=~/Pico-PIO-USB
# You may also put this environment var in your .bashrc
```

To build, use this script: **buildAll_with_pio_usb.sh** or to build a specific configuration with PIO-USB, use the -u option in the build script:

```
./bld.sh -c2 -2 -u
```
Builds for HW_CONFIG 2, Pico 2 and PIO-USB


