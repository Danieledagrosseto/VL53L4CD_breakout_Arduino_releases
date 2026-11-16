/* Interactive offset calibration for the EZrange VL53L4CD forwarding breakout.
   Open the Serial Monitor at 115200 baud and select Newline or Both NL & CR. */

#include <Wire.h>
#include <VL53L4CD.h>
#include <VL53L4CDAddressInput.h>

VL53L4CD sensor;

bool readBoundedInteger(const char * prompt, int16_t minimum, int16_t maximum,
                        int16_t & value)
{
  char input[8];
  uint8_t length = 0;
  bool overflow = false;
  Serial.println(prompt);
  while (true)
  {
    if (!Serial.available()) { continue; }
    char ch = Serial.read();
    if (ch != '\r' && ch != '\n')
    {
      if (length < sizeof(input) - 1) { input[length++] = ch; }
      else { overflow = true; }
      continue;
    }
    if (length == 0 && !overflow) { continue; }

    uint32_t parsed = 0;
    bool valid = !overflow && length != 0;
    for (uint8_t i = 0; i < length && valid; i++)
    {
      if (input[i] < '0' || input[i] > '9') { valid = false; break; }
      parsed = parsed * 10 + (input[i] - '0');
      if (parsed > (uint32_t)maximum) { valid = false; }
    }
    length = 0;
    overflow = false;
    if (valid && parsed >= (uint32_t)minimum)
    {
      value = (int16_t)parsed;
      return true;
    }
    Serial.print("Invalid input. Enter a decimal value from ");
    Serial.print(minimum);
    Serial.print(" to ");
    Serial.println(maximum);
    Serial.println(prompt);
  }
}

int16_t decodeOffsetMillimeters(uint16_t registerValue)
{
  int16_t offset = (registerValue >> 2) & 0x07FF;
  if (offset > 1024) { offset -= 2048; }
  return offset;
}

void printSensorError(const char * operation)
{
  Serial.print(operation);
  Serial.print(" failed. Error: ");
  Serial.print(sensor.getLastError());
  Serial.print(", Wire status: ");
  Serial.println(sensor.last_status);
}

bool calibrateOffset(int16_t target_mm, int16_t sample_count)
{
  sensor.writeReg16Bit(VL53L4CD::ALGO_PART_TO_PART_RANGE_OFFSET_MM, 0);
  if (sensor.getLastError() != VL53L4CD::NoError)
  {
    printSensorError("Clearing range offset");
    return false;
  }
  sensor.writeReg16Bit(VL53L4CD::INNER_OFFSET_MM, 0);
  if (sensor.getLastError() != VL53L4CD::NoError)
  {
    printSensorError("Clearing inner offset");
    return false;
  }
  sensor.writeReg16Bit(VL53L4CD::OUTER_OFFSET_MM, 0);
  if (sensor.getLastError() != VL53L4CD::NoError)
  {
    printSensorError("Clearing outer offset");
    return false;
  }

  sensor.startContinuous();
  if (sensor.getLastError() != VL53L4CD::NoError)
  {
    printSensorError("Starting ranging");
    return false;
  }

  for (uint8_t i = 0; i < 10; i++)
  {
    sensor.readRangeContinuousMillimeters();
    if (sensor.getLastError() != VL53L4CD::NoError)
    {
      printSensorError("Warm-up measurement");
      sensor.stopContinuous();
      if (sensor.getLastError() != VL53L4CD::NoError)
      {
        printSensorError("Stopping ranging");
      }
      return false;
    }
  }

  sensor.stopContinuous();
  if (sensor.getLastError() != VL53L4CD::NoError)
  {
    printSensorError("Stopping after warm-up");
    return false;
  }
  sensor.startContinuous();
  if (sensor.getLastError() != VL53L4CD::NoError)
  {
    printSensorError("Restarting for calibration samples");
    return false;
  }

  uint32_t distance_sum = 0;
  for (int16_t i = 0; i < sample_count; i++)
  {
    uint16_t distance = sensor.readRangeContinuousMillimeters();
    if (sensor.getLastError() != VL53L4CD::NoError)
    {
      printSensorError("Calibration measurement");
      sensor.stopContinuous();
      if (sensor.getLastError() != VL53L4CD::NoError)
      {
        printSensorError("Stopping ranging");
      }
      return false;
    }
    distance_sum += distance;
  }

  sensor.stopContinuous();
  if (sensor.getLastError() != VL53L4CD::NoError)
  {
    printSensorError("Stopping ranging");
    return false;
  }

  int32_t average_distance = distance_sum / sample_count;
  int32_t offset_mm = (int32_t)target_mm - average_distance;
  if (offset_mm < -32768L || offset_mm > 32767L)
  {
    Serial.println("Calibration offset is outside the supported signed range.");
    return false;
  }

  uint16_t encoded_offset = (uint16_t)(offset_mm * 4);
  sensor.writeReg16Bit(VL53L4CD::ALGO_PART_TO_PART_RANGE_OFFSET_MM,
                       encoded_offset);
  if (sensor.getLastError() != VL53L4CD::NoError)
  {
    printSensorError("Writing calibrated offset");
    return false;
  }
  return true;
}

void setup()
{
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(400000);

  Serial.println("Enter sensor's 7-bit I2C address in hex (08-77), then newline:");
  while (true)
  {
    uint8_t address;
    if (!readVL53L4CDAddress(Serial, address)) { continue; }
    sensor = VL53L4CD(address);
    sensor.setTimeout(5000);
    Serial.print("Initializing sensor at 7-bit address 0x");
    Serial.println(sensor.getAddress(), HEX);
    if (sensor.init()) { break; }
    printSensorError("Sensor initialization");
    Serial.println("Check the connection and enter the I2C address again:");
  }

  uint16_t previous_raw = sensor.readReg16Bit(
    VL53L4CD::ALGO_PART_TO_PART_RANGE_OFFSET_MM);
  if (sensor.getLastError() != VL53L4CD::NoError)
  {
    printSensorError("Reading current offset");
    while (true) {}
  }
  Serial.print("Current offset: ");
  Serial.print(decodeOffsetMillimeters(previous_raw));
  Serial.print(" mm (register 0x");
  Serial.print(previous_raw, HEX);
  Serial.println(")");

  int16_t target_mm;
  readBoundedInteger("Enter known target distance in mm (10-1000):",
                     10, 1000, target_mm);
  int16_t sample_count;
  readBoundedInteger("Enter number of samples (5-255):",
                     5, 255, sample_count);

  Serial.println("Keep a target at the entered distance; calibrating...");
  if (!calibrateOffset(target_mm, sample_count))
  {
    Serial.println("Calibration did not complete successfully.");
    while (true) {}
  }

  uint16_t saved_raw = sensor.readReg16Bit(
    VL53L4CD::ALGO_PART_TO_PART_RANGE_OFFSET_MM);
  if (sensor.getLastError() != VL53L4CD::NoError)
  {
    printSensorError("Reading calibrated offset");
    while (true) {}
  }
  Serial.print("Saved offset: ");
  Serial.print(decodeOffsetMillimeters(saved_raw));
  Serial.print(" mm (register 0x");
  Serial.print(saved_raw, HEX);
  Serial.println(")");
}

void loop()
{
}
