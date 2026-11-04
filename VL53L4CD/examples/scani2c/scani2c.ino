#include <Wire.h>

void setup()
{
  Serial.begin(115200);
  Wire.begin();
}

void loop()
{
  uint8_t devices = 0;
  uint8_t errors = 0;
  Serial.println("Scanning I2C bus (7-bit addresses)...");

  // Skip reserved I2C addresses.
  for (uint8_t address = 0x08; address <= 0x77; address++)
  {
    Wire.beginTransmission(address);
    uint8_t status = Wire.endTransmission();

    if (status == 0)
    {
      Serial.print("Device found at 0x");
      if (address < 0x10) { Serial.print('0'); }
      Serial.println(address, HEX);
      devices++;
    }
    else if (status != 2)
    {
      Serial.print("I2C error ");
      Serial.print(status);
      Serial.print(" at 0x");
      if (address < 0x10) { Serial.print('0'); }
      Serial.println(address, HEX);
      errors++;
    }
  }

  Serial.print("Scan complete. Devices found: ");
  Serial.print(devices);
  Serial.print(", errors: ");
  Serial.println(errors);
  Serial.println();
  delay(5000);
}
