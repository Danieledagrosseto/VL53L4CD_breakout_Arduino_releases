/* Continuous measurements from the EZrange VL53L4CD forwarding breakout.
   Open the Serial Monitor at 115200 baud and select Newline or Both NL & CR.
   After initialization, prompts for the sigma threshold, signal threshold,
   and offset (empty line keeps the current value). Distances are in
   millimeters. */

#include <Wire.h>
#include <VL53L4CD.h>
#include <VL53L4CDAddressInput.h>
#include <VL53L4CDRangingParametersInput.h>

VL53L4CD sensor;

void setup()
{
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(400000);
  Serial.println("I2C clock configured to 400 kHz.");

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

  if (!promptVL53L4CDRangingParameters(Serial, sensor))
  {
    Serial.println("Ranging parameter configuration failed.");
    while (1) {}
  }

  // Start continuous back-to-back mode (take readings as
  // fast as possible).  To use continuous timed mode
  // instead, provide a desired inter-measurement period in
  // ms (e.g. sensor.startContinuous(100)).
  sensor.startContinuous();
  if (sensor.getLastError() != VL53L4CD::NoError)
  {
    Serial.print("Failed to start ranging. Error: ");
    Serial.print(sensor.getLastError());
    Serial.print(", Wire status: ");
    Serial.println(sensor.last_status);
    while (1) {}
  }
}

void loop()
{
  uint16_t distance = sensor.readRangeContinuousMillimeters();
  if (sensor.getLastError() != VL53L4CD::NoError)
  {
    Serial.print("Read error: ");
    Serial.print(sensor.getLastError());
    Serial.print(", Wire status: ");
    Serial.print(sensor.last_status);
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
