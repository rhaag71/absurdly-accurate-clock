# Absurdly Accurate Clock

Minimal C++ firmware for Raspberry Pi Pico 2 (RP2350), using PlatformIO and
Earle Philhower's Arduino-Pico core. Open this directory in PlatformIO.

## Build

```sh
pio run -e pico2
pio run -e pico2 -t upload
pio device monitor -b 115200
```

The first build needs network access for the platform, core, and tools.
For the first USB upload, hold BOOTSEL while connecting the Pico 2. Alternatively,
copy `.pio/build/pico2/firmware.uf2` to its BOOTSEL drive. Uploading is separate
from building; no hardware is programmed by `pio run` alone.

The platform is pinned to `5d4561a05e3b212660ac6fdd3fbfb328d1988aa1` in
[maxgerhardt/platform-raspberrypi](https://github.com/maxgerhardt/platform-raspberrypi).
Its [rpipico2 board manifest](https://github.com/maxgerhardt/platform-raspberrypi/blob/5d4561a05e3b212660ac6fdd3fbfb328d1988aa1/boards/rpipico2.json)
selects RP2350, Cortex-M33, 150 MHz, 4 MiB flash and 512 KiB RAM.
This follows the [Arduino-Pico PlatformIO instructions](https://arduino-pico.readthedocs.io/en/latest/platformio.html).
Do not substitute the original Pico's `pico` board configuration. The explicit
integration URL avoids relying on ambiguous registry platform support.

## Layout and architecture

- `src/main.cpp`: cooperative application entry point; no clock behavior yet.
- `src/hardware.cpp` and `include/hardware.hpp`: UART and input initialization.
- `src/pd2200.cpp` and `include/pd2200.hpp`: initial Noritake VFD bring-up.
- `include/pins.hpp`: single source of truth for the GPIO contract.
- `include/clock_state.hpp`: UTC snapshot, initially invalid and unlocked.
- `lib/`: future reusable C++ components; `test/`: future behavioral tests.
- `Paper-Documents/`: existing PDFs and paper/project documentation. Preserve
  this directory and its contents; it is not generated output or firmware data.

Planned data flow: GPS NMEA/UBX supplies UTC epoch/time data; PPS supplies the
precise second boundary. A future timekeeper must associate messages with the
correct pulse and handle validity, signal loss and leap seconds before claiming
synchronization. Canonical time stays UTC; any local-time formatting belongs at
the display boundary. There is no timing-accuracy claim in this scaffold.

The display module sends Posiflex PD-2200 commands in **Noritake mode**
over UART1 through the MAX3232. Select Noritake mode and 9600 baud, 8N1 on the
actual display. Startup waits 500 ms, sends reset (`ESC I`), waits 100 ms,
disables the cursor (`16` hex), sets minimum brightness (`1B 4C 3F` hex),
then clears (`0E` hex) and homes (`0C` hex). Direct cursor positioning
(`ESC H 00` / `ESC H 14`, addresses in hex) precedes each 20-byte row:
`ABSURD CLOCK        ` and `PICO 2 ONLINE       `. Compile-time checks enforce
the row lengths; no terminators or newlines are sent to the VFD. USB Serial
reports when transmission completes, without waiting for a USB host.
Future ESP32 integration receives
UTC/time status over SPI1 plus a TIME_SYNC boundary output; protocol, SPI role,
pulse width and IRQ direction remain to be specified. No ESP32/NTP code yet.

## Verified VFD wiring

Bench testing verified Pico 2 GP4 (physical pin 6) UART TX at 9600 baud on an
oscilloscope using continuous `0x55` (ASCII `U`). The VFD then successfully
displayed those characters through the MAX3232 with the wiring below.

The tested MAX3232 module has misleading signal-direction labels: connect
**Pico GP4 TX to the module header labeled TXD, not RXD**. GP4 remains the
project's VFD UART TX pin; the module labeling does not change the GPIO contract.

| Source | Destination |
| --- | --- |
| Pico GP4 / physical pin 6 | MAX3232 TTL header labeled TXD |
| Pico GND | MAX3232 GND |
| MAX3232 DB9 pin 2 (RS-232 transmit output) | Posiflex DB9 pin 3 (receive input) |
| MAX3232 DB9 pin 5 | Posiflex DB9 pin 5 (signal ground) |

The **DB9 pin 2 to pin 3 crossover is required** for this tested module/display
combination. The PD-2200 is powered separately and configured for Noritake mode,
9600 baud, 8N1. These labeling and wiring findings apply to the tested module.
The temporary continuous-`U` diagnostic firmware has been removed; normal
firmware sends the two startup rows described above.

## GPIO contract

All numbers below are GPIO numbers, not physical header positions. TX/RX and
input/output directions are relative to the Pico.

| GPIO | Assignment |
| --- | --- |
| GP0 | GPS UART0 TX (`Serial1`) |
| GP1 | GPS UART0 RX (`Serial1`) |
| GP2 | GPS PPS input |
| GP4 | PD-2200 UART1 TX (`Serial2`) via MAX3232 |
| GP5 | UART1 RX, reserved and disabled |
| GP6 | UI button input; pull-up, button to GND |
| GP8 | Reserved SPI1 MISO |
| GP9 | Reserved SPI1 CS |
| GP10 | Reserved SPI1 SCK |
| GP11 | Reserved SPI1 MOSI |
| GP12 | Reserved TIME_SYNC output |
| GP13 | Reserved future ESP32 control/IRQ |
| GP14–GP22 | Open for future expansion |

GP3 and GP7 are also unassigned. Reserved expansion pins are not initialized.
Both UARTs currently use **9600 baud, 8N1**, explicit bring-up assumptions in
`src/main.cpp`; confirm them against the GPS configuration and VFD switches.
USB `Serial` is separate from both hardware UARTs. PPS capture, GPS parsing,
button handling, display formatting and SPI transfer are intentionally pending.
