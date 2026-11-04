/* Change an EZrange bridge's persistent address.
   Serial Monitor: 115200 baud, Newline or Both NL & CR.
   Enter 7-bit hexadecimal addresses, for example 21 or 0x21. */

#include <Wire.h>
#include <VL53L4CD.h>
#include <VL53L4CDAddressInput.h>

enum Phase { Scan, OldAddress, NewAddress };
Phase phase = Scan;
uint8_t old_address;

void printAddress(uint8_t address)
{
  Serial.print("0x");
  if (address < 0x10) { Serial.print('0'); }
  Serial.print(address, HEX);
}

uint8_t probe(uint8_t address)
{
#if defined(WIRE_HAS_TIMEOUT)
  Wire.clearWireTimeoutFlag();
#endif
  Wire.beginTransmission(address);
  uint8_t status = Wire.endTransmission();
#if defined(WIRE_HAS_TIMEOUT)
  if (Wire.getWireTimeoutFlag()) { status = 5; }
#endif
  if (status != 0 && status != 2)
  {
    Serial.print("Probe error at ");
    printAddress(address);
    Serial.print(", Wire status: ");
    Serial.println(status);
  }
  return status;
}

bool readAddress(uint8_t & address)
{
  return readVL53L4CDAddress(Serial, address);
}

bool checkSensor(uint8_t address)
{
  VL53L4CD sensor(address);
  sensor.setTimeout(500);
  uint16_t id = sensor.readReg16Bit(VL53L4CD::IDENTIFICATION_MODEL_ID);
  if (sensor.getLastError() != VL53L4CD::NoError)
  {
    if (sensor.last_status == 2)
    {
      Serial.print("Error: address ");
      printAddress(address);
      Serial.println(" does not ACK.");
      return false;
    }
    Serial.print("Sensor read failed at ");
    printAddress(address);
    Serial.print(", error: ");
    Serial.print(sensor.getLastError());
    Serial.print(", Wire status: ");
    Serial.println(sensor.last_status);
    return false;
  }
  if (id != 0xEBAA)
  {
    Serial.print("Error: unexpected sensor ID at ");
    printAddress(address);
    Serial.print(": 0x");
    Serial.println(id, HEX);
    return false;
  }
  return true;
}

void setup()
{
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(400000);
#if defined(WIRE_HAS_TIMEOUT)
  Wire.setWireTimeout(500000UL, true);
#endif
}

void loop()
{
  if (phase == Scan)
  {
    Serial.println("Scanning 7-bit addresses (400 kHz I2C)...");
    uint8_t found = 0;
    for (uint8_t address = 0x08; address <= 0x77; address++)
    {
      uint8_t status = probe(address);
      if (status == 0)
      {
        Serial.print("Connected: ");
        printAddress(address);
        Serial.println();
        found++;
      }
      else if (status != 2)
      {
        Serial.println("Scan aborted due to bus error; check wiring. Retrying in 5 seconds.");
        delay(5000);
        return;
      }
    }
    Serial.print("Devices found: ");
    Serial.println(found);
    Serial.println("Enter OLD address in hex (08-77):");
    phase = OldAddress;
  }

  uint8_t address;
  if (!readAddress(address)) { return; }
  if (phase == OldAddress)
  {
    uint8_t status = probe(address);
    if (status != 0)
    {
      if (status == 2) { Serial.println("Error: old address is not connected."); }
      Serial.println("Enter OLD address again:");
      return;
    }
    if (!checkSensor(address))
    {
      Serial.println("Enter OLD address again:");
      return;
    }
    old_address = address;
    Serial.println("Enter NEW address in hex (08-77). A changed address writes the bridge EEPROM:");
    phase = NewAddress;
    return;
  }

  if (address == old_address)
  {
    if (checkSensor(address))
    {
      Serial.print("Success: address unchanged at ");
      printAddress(address);
      Serial.println("; VL53L4CD ID 0xEBAA verified. No EEPROM write.");
    }
    phase = Scan;
    return;
  }

  uint8_t status = probe(address);
  if (status != 2)
  {
    if (status == 0) { Serial.println("Error: new address is already present."); }
    Serial.println("Enter another NEW address:");
    return;
  }
  // Recheck the old device immediately before the EEPROM write.
  if (!checkSensor(old_address)) { phase = Scan; return; }
  VL53L4CD sensor(old_address);
  sensor.setTimeout(500);
  sensor.setAddress(address);
  if (sensor.getLastError() != VL53L4CD::NoError)
  {
    Serial.print("Address write failed, error: ");
    Serial.print(sensor.getLastError());
    Serial.print(", Wire status: ");
    Serial.println(sensor.last_status);
    Serial.println("Address may be uncertain; rescanning. Do not retry blindly.");
    phase = Scan;
    return;
  }
  if (probe(address) == 0 && checkSensor(address))
  {
    Serial.print("Success: VL53L4CD ID 0xEBAA responds at ");
    printAddress(address);
    Serial.println(". Update your other sketches to use this address.");
  }
  else
  {
    Serial.println("Verification failed: no confirmed VL53L4CD at the new address. Rescanning.");
  }
  phase = Scan;
}
