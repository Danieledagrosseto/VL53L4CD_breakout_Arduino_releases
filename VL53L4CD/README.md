# VL53L4CD breakout library for Arduino

A Pololu-style Arduino interface for the **EZrange VL53L4CD forwarding
breakout**, adapted from the supplied ST VL53L4CD Ultra Lite Driver (ULD)
2.2.2 and breakout transport. This replaces the original VL53L0X library.

**This is not a driver for a directly connected VL53L4CD sensor or a standard
carrier.** The EZrange bridge uses a different I2C packet format.

## Hardware

Connect the host's SDA, SCL, and GND to the breakout's corresponding pins.
Use the supply voltage and I2C pull-up voltage specified for your breakout;
the library does not configure level shifting or I/O voltage. Multiple boards
need distinct host-facing addresses.

The default address is `0x29` in Arduino's **7-bit** notation (ST's equivalent
shifted address is `0x52`). To select a bridge already configured at another
address, construct the object with that address, for example
`VL53L4CD sensor(0x30);`. This does not write to the board.
`setAddress()` instead programs a new address into the currently addressed
bridge. Invalid constructor addresses are rejected on subsequent I/O.

## Installation

Place this library in a folder named `VL53L4CD` inside your Arduino
sketchbook's `libraries` directory, then restart Arduino IDE. Keep
`library.properties`, `VL53L4CD.h`, and `VL53L4CD.cpp` together.

Open **File > Examples > VL53L4CD**:

- [Continuous](examples/Continuous/Continuous.ino): continuous ranging, after
  prompting for the sigma threshold, signal threshold, and ranging offset.
- [MultipleContinuous](examples/MultipleContinuous/MultipleContinuous.ino):
  scan and capture buffered frames from up to six uniquely addressed EZrange
  breakouts, prompting per sensor for the sigma threshold, signal threshold,
  and ranging offset. See [Buffered multi-sensor acquisition](#buffered-multi-sensor-acquisition).
- [Single](examples/Single/Single.ino): start, read one measurement, then stop,
  after the same ranging-parameter prompts as Continuous.
- [OffsetCalibration](examples/OffsetCalibration/OffsetCalibration.ino):
  read the current offset, prompt for a known distance and sample count, and
  calibrate/write the offset register. Uses **400 kHz I2C** and **115200 baud**.
- [ChangeAddress](examples/ChangeAddress/ChangeAddress.ino): scan, prompt for an
  existing old address and unused new address, change the bridge's persistent
  address, and verify sensor ID `0xEBAA` at the new address. Uses **400 kHz I2C**
  and **115200 baud**. Select **Newline** or **Both NL & CR** in Serial Monitor;
  enter **7-bit hexadecimal** addresses such as `21` or `0x21` (range `08`-`77`).
  Empty-write probes scan the bus, check the entered old address and unused
  new address, and precede sensor-ID verification at the new address.
  Missing old devices, non-VL53L4CD IDs, occupied new addresses belonging to
  other devices, malformed input, and bus errors are rejected before writing.
  Entering the old address as the new address verifies the sensor and succeeds
  without writing EEPROM.
  After a write attempt it rescans; an uncertain result is not automatically
  retried. The bridge stores the change in EEPROM: avoid unnecessary writes and
  use the new address in other sketches afterward. Only one physical
  device may occupy the old address; scanning cannot detect address collisions.
- [scani2c](examples/scani2c/scani2c.ino): scan non-reserved 7-bit addresses
  `0x08` through `0x77` every five seconds.

Continuous and Single first prompt for the sensor's existing **7-bit hexadecimal
address** (`08`-`77`, for example `21` or `0x21`). Select **Newline** or
**Both NL & CR** in Serial Monitor. Invalid input or initialization failures
allow another attempt; acquisition starts only after successful initialization.
Selecting an address does not reprogram the bridge's EEPROM. After
initialization, the ranging examples (Continuous, Single, and
MultipleContinuous) display the sensor's current **sigma threshold**
(0-16383 mm), **signal threshold** (0-16384 kcps), and **ranging offset**
(-1024 to 1023 mm), prompt for new decimal values, and apply them. An empty
line keeps the current value; the applied values are read back and printed
(the signal threshold is stored in 8 kcps steps, rounded down). These writes
change sensor registers only, not bridge EEPROM, so they revert on sensor
power cycles. The interactive
examples share the parsers in `VL53L4CDAddressInput.h` and
`VL53L4CDRangingParametersInput.h`; keep those headers with the
library when installing it.

OffsetCalibration prompts for a target distance (**10-1000 mm**) and sample
count (**5-255**), after displaying the current offset. Keep a target at the
entered distance while it takes ten warm-up readings and then the requested
samples. It clears the range, inner, and outer offsets before calibration and
prints the offset read back from the sensor register afterward. This changes
the sensor's offset register; it does not write bridge EEPROM.

Continuous uses **400 kHz I2C** and
the Serial Monitor at **115200 baud**, matching the VS Code monitor task.
All examples use **115200 baud**. The scanner uses only
`Wire` and remains useful for other I2C devices.

## Buffered multi-sensor acquisition

MultipleContinuous uses **400 kHz I2C**, **115200 baud**, and Newline or
Both NL & CR. Assign unique persistent addresses with ChangeAddress before
connecting all boards; scanning cannot detect duplicate addresses. Scanning
checks sensor ID `0xEBAA` before initialization, reports skipped devices, and
aborts on probe errors, no verified sensors, or more than six verified sensors.
Use on a bus of known-compatible devices: an ACK alone does not identify a
forwarding breakout, and ID checks send bridge-format read requests.

Enter three decimal integers, each followed by newline:

1. Timing budget: **10-200 ms** (10 is fastest; shorter budgets can reduce quality).
2. Acquisition period: **0** for back-to-back, or **budget < period <= 5000 ms**.
3. Frame count: **1 to floor(150 / detected sensor count)**, displayed at runtime.

After these, each initialized sensor prompts in turn for its sigma threshold,
signal threshold, and ranging offset, as described above; an empty line keeps
that sensor's current value.

A fixed **600-byte** RAM buffer stores distance, library error, and range status
for every sensor in every frame: 25 frames with six sensors, 150 with one.
There is no dynamic allocation or UART output during capture; pending startup
output is flushed before starting. Sensors run concurrently, but starts and
reads are sequential, not synchronized. Each frame reads every sensor once,
using the driver's blocking readiness check and a **500 ms per-operation
timeout**. The host initially waits one budget after all starts, then targets
the requested period (or budget in back-to-back mode), skipping missed host
deadlines rather than emitting catch-up frames. Actual cadence includes bridge
and bus overhead; this is not a guaranteed sampling rate or a nonblocking example.

After capture, it prints elapsed `millis()` from **capture start, after all
sensors have started, to completion of the last frame**, including the initial
wait and first frame's reads but excluding sensor-start commands and UART transfer.
One frame therefore includes its wait and read time. Sensors are stopped, then all buffered
samples are sent as CSV: `frame,address_hex,distance_mm,error,range_status`.
Frame numbers start at 1; sensors are ordered by ascending address, not physical
position. A sample is valid only when both `error` and `range_status` are zero;
read failures retain the driver's distance sentinel `65535` and error/status.
Errors do not halt an in-progress capture or print during it. Startup failures
cancel capture; stop failures are reported after timing. Reset to capture again.

## Basic single-sensor usage

```cpp
#include <Wire.h>
#include <VL53L4CD.h>

VL53L4CD sensor;

void setup()
{
  Serial.begin(115200);
  Wire.begin();
  sensor.setTimeout(500);
  if (!sensor.init())
  {
    Serial.print("Initialization error: ");
    Serial.println(sensor.getLastError());
    while (1) {}
  }
}

void loop()
{
  uint16_t distance = sensor.readRangeSingleMillimeters();
  if (sensor.getLastError() != VL53L4CD::NoError)
  {
    Serial.print("Read error: ");
    Serial.println(sensor.getLastError());
  }
  else if (sensor.getRangeStatus() == 0)
  {
    Serial.println(distance);
  }
  else
  {
    Serial.print("Invalid measurement, status: ");
    Serial.println(sensor.getRangeStatus());
  }
}
```

## Migration from VL53L0X

Replace `#include <VL53L0X.h>` with `#include <VL53L4CD.h>` and class
`VL53L0X` with `VL53L4CD`. Most method names and return types are retained,
but the hardware configuration and measurement sequence are entirely different.

| Interface | VL53L4CD behavior |
| --- | --- |
| `init()` | Wait for boot, verify ID `0xEBAA`, load ULD configuration, perform VHV initialization, stop, and configure 50 ms continuous timing. |
| `init(bool)` | Deprecated compatibility overload. The argument is ignored; it does **not** select an I/O voltage. |
| `setBus(TwoWire *)`, `getBus()` | Per-object bus selection; no global sensor state. Initialize that bus yourself. A different bus requires a new `init()`. Null buses and changes while ranging are rejected. |
| `setAddress(uint8_t)`, `getAddress()` | Use 7-bit addresses `0x08` through `0x77`. Program the bridge and wait 60 ms for EEPROM; update the object's address only on success. |
| Register and multi-byte access | Same names, but register arguments are now **16-bit**. Data are big-endian. Only mapped register constants remain, with VL53L4CD values. |
| `setSignalRateLimit(float)`, `getSignalRateLimit()` | MCPS externally, converted to the ULD's kcps threshold representation. Accepted range: 0 through 16.384 MCPS, following the supplied API header. Resolution is 0.008 MCPS, rounded down. Default: 1.024 MCPS. |
| `setSigmaThreshold(uint16_t)`, `getSigmaThreshold()` | New; follows the ULD. Sigma threshold in mm, **0 through 16383**, stored in quarter-mm units. |
| `setSignalThreshold(uint16_t)`, `getSignalThreshold()` | New; follows the ULD. Same register as the MCPS methods, expressed in kcps, **0 through 16384**, stored in 8 kcps steps (rounded down). |
| `setOffset(int16_t)`, `getOffset()` | New; follows the ULD. Ranging offset in mm, **-1024 through 1023**. Setting it also clears the inner and outer offsets. |
| `setMeasurementTimingBudget(uint32_t)` | Microseconds, **10000 through 200000**, in whole-millisecond steps only. Invalid inputs are rejected, not rounded. Change timing only while stopped. |
| `getMeasurementTimingBudget()` | Returns the last successfully configured budget in microseconds, not a reconstruction of quantized timing registers. Default: **50000**. Raw register writes do not update this cached configuration. |
| `startContinuous(0)` | Back-to-back ranging, with the timing budget determining the period. |
| `startContinuous(period_ms)` | Autonomous mode. Period must be greater than the timing budget and at most **5000 ms**, per UM2931. |
| `stopContinuous()` | Stops ranging. |
| `readRangeContinuousMillimeters()` | Wait for ready, read range/status, and clear the interrupt. Requires an active ranging session. |
| `readRangeSingleMillimeters()` | Start back-to-back ranging, read once, then stop, including cleanup on read failure. Rejects calls during an existing session rather than disrupting it. |
| `setTimeout(uint16_t)`, `getTimeout()`, `timeoutOccurred()` | Milliseconds; zero disables measurement-ready polling timeouts. Initialization uses 1000 ms when this setting is zero. Timeout indications remain latched until `timeoutOccurred()` clears them. |

`setVcselPulsePeriod()`, `getVcselPulsePeriod()`, `vcselPeriodType`, and
VL53L0X-only register definitions were removed. The VL53L4CD ULD does not
provide equivalent pre-range/final-range pulse-period controls. The old
`LONG_RANGE` example profile is therefore removed, not silently emulated.
The Single example retains optional 20 ms high-speed and 200 ms high-accuracy
timing profiles.

Initialize before using high-level configuration or ranging methods.
Stop before reinitializing, changing the bus, or changing the timing budget.
Calling `startContinuous()` during an existing session is rejected.
If timing writes fail partway through, `init()` is required before ranging
again. Low-level register access remains available before initialization.

## Errors and measurement validity

Check `getLastError()` **immediately after the operation**; a later operation
replaces it. Errors are `NoError`, `InvalidArgument`, `I2CError`, `Timeout`,
`SensorNotFound`, `NotInitialized`, or `InvalidState`.

- `last_status` retains Wire's transmission status. Short reads use status 4;
  transport timeouts use status 5. Argument/state errors do not change it.
- Boolean setters and `init()` return false on failure.
- Range reads return `65535` on communication, timeout, or state errors.
- Failed raw reads return all-one values of their return width. Failed
  `getSignalRateLimit()` returns `NAN`; failed `getSigmaThreshold()` and
  `getSignalThreshold()` return `0xFFFF`; failed `getOffset()` returns zero;
  an uninitialized timing getter returns
  zero. These sentinels must be checked with `getLastError()`.
- `readMulti()` can fill a prefix before a later chunk fails; discard the
  destination on error.
- A returned distance is valid only when **`getRangeStatus() == 0`**.
  Nonzero sensor statuses describe measurement quality/errors, independently
  of I2C errors. The last status starts at 255 and is invalidated on failed reads.

The status mapping follows the supplied ULD and UM2931 section 4.3. For example,
1 means sigma too high, 2 means signal below threshold, 5 means hardware failure,
and 12 means signal too low. This wrapper exposes distance/status plus the
sigma/signal threshold and offset configuration above; it does
not add the ULD's temperature-update, detection-threshold-window, or
extended-result APIs.

## Breakout transport and timeouts

Writes send `[0, register_high, register_low, data...]`, followed by 150 us.
Reads send `[count, register_high, register_low]` with STOP, wait 150 us,
request the data with STOP, then wait another 150 us. Transfers are split into
at most **four data bytes**, matching the supplied bridge transport and fitting
Arduino Uno Wire buffers. Address-register writes also wait 60 ms.

On cores defining `WIRE_HAS_TIMEOUT`, the library configures Wire's timeout
before each transfer, using `setTimeout()` or 1000 ms when it is zero, and
checks the timeout flag. This changes that shared bus's timeout configuration.
On other cores, hardware-transfer blocking is governed by that core's Wire
implementation; a polling timeout cannot interrupt a blocked Wire call.

## VS Code builds and tests

Install Arduino CLI and your board core, then run **Tasks: Run Task >
Arduino: compile example**. Select an example and enter an FQBN, such as
`arduino:avr:uno`. The task uses this local library.

```powershell
arduino-cli compile --fqbn arduino:avr:uno --libraries .. .\examples\Continuous
arduino-cli compile --fqbn arduino:avr:uno --libraries .. .\examples\Single
arduino-cli compile --fqbn arduino:avr:uno .\examples\scani2c
.\tests\run.ps1
```

The host tests require `g++` and use fake Arduino/Wire implementations.
They do not access serial ports or change hardware. Arduino Uno builds and host
tests do not establish electrical or breakout-firmware compatibility; actual
ranging must also be verified on the connected breakout before deployment.

## Provenance

The original interface was the [Pololu VL53L0X library](https://github.com/pololu/vl53l0x-arduino).
Sensor initialization, timing calculations, and status mapping are adapted from
the supplied ST VL53L4CD ULD 2.2.2; bridge framing/delays follow the supplied
EZrange platform implementation. This is a focused Arduino adaptation, not a
bundle of the complete ST C API. See [LICENSE.txt](LICENSE.txt) for retained
notices. UM2931 revision 3, especially sections 2.3, 3.2, and 4.3, describes
the sensor's ranging flow, timing constraints, and result validity.
