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

## Display timezone button

GP6 (physical pin 9) selects UTC → Eastern → Central → Mountain → Pacific → UTC.
Connect the button to GND; the internal pull-up is enabled. Each press/release is
debounced for 30 ms, and holding does not repeat. Reset returns to UTC.
Authoritative time remains UTC; local time uses contemporary U.S. DST rules only
at the display boundary. See [timezone and HH diagnostics](docs/display-timezone.md)
for rules, serial record formats and bench checks.

## Layout and architecture

- `src/main.cpp`: cooperative GPS/PPS, button, display and USB diagnostic servicing.
- `src/hardware.cpp` and `include/hardware.hpp`: UART and input initialization.
- `src/pd2200.cpp` and `include/pd2200.hpp`: initial Noritake VFD bring-up.
- `include/pins.hpp`: single source of truth for the GPIO contract.
- `include/clock_state.hpp`: authoritative UTC timebase, initially invalid and unlocked.
- `src/display_time.cpp`: presentation-only civil time and contemporary U.S. DST.
- `src/zone_button.cpp`: non-blocking debounced display-zone selection.
- `lib/`: future reusable C++ components; `test/`: host regression tests.
- `Paper-Documents/`: existing PDFs and paper/project documentation. Preserve
  this directory and its contents; it is not generated output or firmware data.

GPS RMC labels are associated with PPS edges by the UTC timebase. Canonical time
stays UTC; local-time conversion belongs at the display boundary. See
[timebase contract](docs/pps-timebase.md) for association and validity behavior.

The reference physical build uses a Posiflex PD-2200 serial VFD, but that display
is not fundamental to the clock architecture: the UTC timekeeping core is
independent of it. The current VFD layer is hardware-specific and can be replaced
or adapted for another serial/UART or embedded display; reproducing the project
does not require finding the same Posiflex model.

The display module sends Posiflex PD-2200 commands in **Noritake mode**
over UART1 through the MAX3232. Select Noritake mode and 9600 baud, 8N1 on the
actual display. Startup waits 500 ms, sends reset (`ESC I`), waits 100 ms,
disables the cursor (`16` hex), sets minimum brightness (`1B 4C 3F` hex),
then clears (`0E` hex) and homes (`0C` hex). Direct cursor positioning
(`ESC H`, zero-based cell address) precedes short initialized fields and changed
characters. The display shows the selected zone, HH:MM:SS and PPS-synchronized
rolling decade, with GPS/PPS/SAT on the lower row. No USB host is required.
The companion [aac-time-bridge](https://github.com/rhaag71/aac-time-bridge) is an
ESP32 network-time/NTP appliance that consumes the Pico's SPI protocol and
qualified TIME_SYNC signal. The Pico remains the authoritative timekeeper and
runs standalone without the ESP32. [Protocol v1](docs/clock-network-protocol.md)
specifies the wiring, packet and phase-delay semantics; nothing received from the
ESP32 can change Pico time.

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
firmware sends the clock/status fields described above.

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
| GP8 | SPI1 RX input / ESP32 MOSI -> Pico (header 11) |
| GP9 | SPI1 CSn input from ESP32 (header 12) |
| GP10 | SPI1 SCK input from ESP32 (header 14) |
| GP11 | SPI1 TX output / Pico -> ESP32 MISO (header 15) |
| GP12 | Qualified TIME_SYNC output (header 16) |
| GP13 | Reserved future ESP32 control/IRQ |
| GP14–GP22 | Open for future expansion |

GP3 and GP7 are also unassigned. GP13 remains reserved and is not initialized. SPI1 uses native hardware, not PIO.
Both UARTs currently use **9600 baud, 8N1**, explicit bring-up assumptions in
`src/main.cpp`; confirm them against the GPS configuration and VFD switches.
USB `Serial` is separate from both hardware UARTs. The network interface works without a connected SPI controller.

## License

This project is licensed under the [MIT License](LICENSE).
