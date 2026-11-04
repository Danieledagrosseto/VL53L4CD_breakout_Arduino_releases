/* One measurement per call from the EZrange VL53L4CD forwarding breakout.
   Open the Serial Monitor at 115200 baud. Distances are in millimeters. */

#include <Wire.h>
#include <VL53L4CD.h>
#include <VL53L4CDAddressInput.h>

VL53L4CD sensor;

// Uncomment ONE option; the default timing budget is 50 ms.
//#define HIGH_SPEED
//#define HIGH_ACCURACY

void setup()
{
  Serial.begin(115200);
  Wire.begin();

  Serial.println("Enter sensor's 7-bit I2C address in hex (08-77), then newline:");
  while (true)
  {
    uint8_t address;
    if (!readVL53L4CDAddress(Serial, address)) { continue; }
    sensor = VL53L4CD(address);
    sensor.setTimeout(500);
    Serial.print("Initializing sensor at 7-bit address 0x");
    Serial.println(sensor.getAddress(), HEX);
    if (sensor.init()) { break; }
    Serial.print("Failed to initialize sensor. Error: ");
    Serial.print(sensor.getLastError());
    Serial.print(", Wire status: ");
    Serial.println(sensor.last_status);
    Serial.println("Check the connection and enter the I2C address again:");
  }

#if defined HIGH_SPEED
  bool configured = sensor.setMeasurementTimingBudget(20000);
#elif defined HIGH_ACCURACY
  bool configured = sensor.setMeasurementTimingBudget(200000);
#else
  bool configured = true;
#endif
  if (!configured)
  {
    Serial.print("Failed to configure timing. Error: ");
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
    Serial.print(sensor.getLastError());
  }
  else
  {
    Serial.print(distance);
    if (sensor.getRangeStatus() != 0)
    {
      Serial.print(" INVALID RANGE, status: ");
      Serial.print(sensor.getRangeStatus());
    }
  }
  if (sensor.timeoutOccurred()) { Serial.print(" TIMEOUT"); }
  Serial.println();
}
