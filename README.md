# EZrange VL53L4CD breakout library for Arduino

Arduino library and API examples for the **EZrange VL53L4CD forwarding
breakout**, with a Pololu-style C++ interface.

The breakout board is the **same board** used by the
[portable MCU API release](https://github.com/Danieledagrosseto/VL53L4CD_breakout_API_releases).
This package is dedicated to Arduino users: it uses Arduino's `Wire` and
timing functions, so no MCU-specific transport hooks need to be implemented.

**Not for a directly connected VL53L4CD sensor or a standard carrier.**
The EZrange breakout uses a bridge-specific I2C protocol.

## Download

[Download Arduino library v2.0.2](https://github.com/Danieledagrosseto/VL53L4CD_breakout_Arduino_releases/releases/download/v2.0.2/VL53L4CD_breakout_Arduino_v2.0.2.zip)

[Latest release](https://github.com/Danieledagrosseto/VL53L4CD_breakout_Arduino_releases/releases/latest)
| [All releases](https://github.com/Danieledagrosseto/VL53L4CD_breakout_Arduino_releases/releases)

The ZIP includes the Arduino library source, API documentation, five example
sketches, board photos for both connector layouts, their schematics, retained
license notices, and host tests. This repository hosts the README and
downloadable release packages; library source and board documentation are
included in the ZIP rather than published separately.

**This is source code, not precompiled firmware.** Compile and upload an
example for your Arduino board.

## Install and start

1. Download the release ZIP, not GitHub's automatic "Source code" archive.
2. In Arduino IDE, select **Sketch > Include Library > Add .ZIP Library**
   and choose `VL53L4CD_breakout_Arduino_v2.0.2.zip`.
   Alternatively, extract its `VL53L4CD` folder into your sketchbook's
   `libraries` directory and restart Arduino IDE.
3. Connect SDA, SCL, and GND to the corresponding breakout pins. Use the
   supply voltage and I2C pull-up voltage specified for your breakout;
   consult the included schematic before connecting power.
4. Select your Arduino board and port, then open
   **File > Examples > VL53L4CD > Continuous** and upload it.
5. Open Serial Monitor at **115200 baud**, select **Newline** or
   **Both NL & CR**, and enter the breakout's existing **7-bit hexadecimal
   address** (for example `29` or `0x29`, the default).

The address can be `08` through `77`. Selecting an address in a ranging
example does not change the board's persistent address.

## Examples

| Example | Purpose |
| --- | --- |
| Continuous | Select an existing address and read continuous distance measurements. |
| MultipleContinuous | Discover up to six sensors, prompt for timing and frame count, buffer all samples, then print capture time and CSV data. |
| Single | Select an existing address and start, read, then stop each measurement. |
| ChangeAddress | Scan the bus, select old and unused new addresses, program the persistent bridge address, and verify the sensor. |
| scani2c | Scan non-reserved 7-bit I2C addresses every five seconds using only `Wire`. |

All examples use **115200 baud**. Continuous, MultipleContinuous, and
ChangeAddress use **400 kHz I2C**. ChangeAddress writes the bridge's EEPROM:
avoid unnecessary writes and use the new address in later sketches. Connect
only one device at the old address; an I2C scan cannot detect address
collisions.

## Version 2.0.2

- Refreshes the package with the latest board photos for both connector layouts.
- The library source, examples, and tests are unchanged from v2.0.1.

## Version 2.0.1

- Adds buffered multi-sensor acquisition with a fixed 600-byte sample buffer:
  up to 25 frames with six sensors or 150 frames with one sensor.
- Prompts for a 10-200 ms timing budget, acquisition period (0 for fastest
  back-to-back mode), and a frame count bounded by available buffer capacity.
- Captures without UART output, prints total capture time including the first
  frame's wait and reads, then sends all distances, errors, and statuses as CSV.
- Includes the current library source, all five examples, updated documentation,
  and host tests for discovery, input validation, buffering, timing, and failures.
  The core driver/API is unchanged from the packaged v2.0.0 source.
- Retains board photos for both connector layouts, both schematics, and license
  notices.

Open Serial Monitor at 115200 baud with Newline or Both NL & CR. Assign each
breakout a unique persistent address before connecting them together. Reset to
configure another capture. The Uno build uses 1251 bytes of static RAM; actual
cadence and measurement quality must be verified with connected hardware.

## Board description and API reference

Inside the ZIP:

- `VL53L4CD/README.md`: hardware connection guidance, API reference, error
  handling, timing limits, transport details, and migration from VL53L0X.
- `VL53L4CD/pictures/with 100mils connector/`: front and back photos and the
  schematic source for the 100-mil connector layout.
- `VL53L4CD/pictures/with jst connector/`: front and back photos and the
  breakout schematic PDF for the JST connector layout.
- `VL53L4CD/examples/`: ready-to-compile Arduino sketches.
- `VL53L4CD/LICENSE.txt`: retained Pololu and ST notices.

Arduino Uno compilation and host tests validate software behavior, not
electrical compatibility or actual ranging on your board. Verify operation
on the connected breakout before deployment.
