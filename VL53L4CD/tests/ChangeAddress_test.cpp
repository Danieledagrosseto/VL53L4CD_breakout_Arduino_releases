#include "VL53L4CD.h"
#include <assert.h>
#include <iostream>
#include <sstream>
#include <string>

class FakeSerial
{
  public:
    std::string input;
    std::ostringstream output;
    unsigned long baud = 0;
    void begin(unsigned long rate) { baud = rate; }
    int available() { return (int)input.size(); }
    int read()
    {
      char ch = input[0];
      input.erase(0, 1);
      return ch;
    }
    template<typename T> void print(T value) { output << value; }
    void print(uint8_t value) { output << (unsigned int)value; }
    template<typename T> void print(T value, int)
    {
      output << std::hex << (unsigned int)value << std::dec;
    }
    void println() { output << '\n'; }
    template<typename T> void println(T value) { print(value); println(); }
    template<typename T> void println(T value, int base) { print(value, base); println(); }
};

FakeSerial Serial;
TwoWire Wire;
static unsigned long clock_ms = 0;
unsigned long millis() { return clock_ms; }
void delay(unsigned long ms) { clock_ms += ms; }
void delayMicroseconds(unsigned int) {}

#define HEX 16
#include "..\examples\ChangeAddress\ChangeAddress.ino"

#define setup continuousSetup
#define loop continuousLoop
#define sensor continuousSensor
#include "..\examples\Continuous\Continuous.ino"
#undef sensor
#undef loop
#undef setup

#define setup singleSetup
#define loop singleLoop
#define sensor singleSensor
#include "..\examples\Single\Single.ino"
#undef sensor
#undef loop
#undef setup

static void enter(std::string const & line)
{
  Serial.input += line;
  loop();
}

static bool printed(std::string const & text)
{
  return Serial.output.str().find(text) != std::string::npos;
}

static unsigned int addressWrites()
{
  unsigned int count = 0;
  for (auto const & transaction : Wire.transactions)
  {
    if (!transaction.read && transaction.bytes.size() >= 4 &&
        transaction.bytes[0] == 0 && transaction.bytes[1] == 0 &&
        transaction.bytes[2] == 1) { count++; }
  }
  return count;
}

int main()
{
  setup();
  assert(Serial.baud == 115200 && Wire.clock_hz == 400000);
  loop();
  assert(phase == OldAddress && printed("Devices found: 1"));
  assert(Wire.transactions.size() == 0x77 - 0x08 + 1);
  for (auto const & transaction : Wire.transactions)
  {
    assert(!transaction.read && transaction.bytes.empty());
  }
  for (char const * invalid : {"07\n", "78\n", "xyz\n", "0x\n", "21junk\n",
                                     "0000000000000000000000000029\n"})
  {
    enter(invalid);
    assert(phase == OldAddress && addressWrites() == 0);
  }
  enter("21\n");
  assert(phase == OldAddress && printed("old address is not connected"));
  Wire.devices.at(0x29).registers[0x010F] = 0;
  enter("29\n");
  assert(phase == OldAddress && printed("unexpected sensor ID"));
  Wire.devices.at(0x29).registers[0x010F] = 0xEB;
  size_t oldCheck = Wire.transactions.size();
  enter(" 0X29 \r\n");
  assert(phase == NewAddress);
  assert(Wire.transactions[oldCheck].bytes.empty());
  assert(Wire.transactions[oldCheck + 1].bytes == std::vector<uint8_t>({2, 1, 0x0F}));
  enter("29\n");
  assert(phase == Scan && printed("Success: address unchanged"));
  assert(addressWrites() == 0 && Wire.devices.count(0x29) == 1);
  loop();
  enter("29\n");
  assert(phase == NewAddress);
  Wire.devices.emplace(0x30, FakeDevice());
  enter("30\n");
  assert(phase == NewAddress && addressWrites() == 0);
  assert(printed("new address is already present"));
  Wire.next_status = 4;
  enter("31\n");
  assert(phase == NewAddress && printed("Probe error"));
  assert(addressWrites() == 0);
  enter("0x31\n");
  assert(phase == Scan && printed("Success: VL53L4CD"));
  size_t verified = Wire.transactions.size();
  assert(Wire.transactions[verified - 3].address == 0x31);
  assert(Wire.transactions[verified - 3].bytes.empty());
  assert(Wire.transactions[verified - 2].bytes == std::vector<uint8_t>({2, 1, 0x0F}));
  assert(addressWrites() == 1 && Wire.devices.count(0x31) == 1);
  assert(Wire.devices.count(0x29) == 0);
  assert(Wire.devices.at(0x31).registers[0x010F] == 0xEB);

  loop();
  enter("31\n");
  assert(phase == NewAddress);
  Wire.fail_write_register = 1;
  enter("32\n");
  assert(phase == Scan && printed("Address write failed"));
  assert(Wire.devices.count(0x31) == 1 && Wire.devices.count(0x32) == 0);
  Wire.fail_write_register = -1;
  loop();
  enter("31\n");
  Wire.short_read = true;
  unsigned int before = addressWrites();
  enter("32\n");
  assert(phase == Scan && printed("Sensor read failed"));
  assert(addressWrites() == before);
  Wire.short_read = false;
  loop();
  enter("31\n");
  assert(phase == NewAddress);
  Wire.short_read = true;
  before = addressWrites();
  enter("31\n");
  assert(phase == Scan && addressWrites() == before);
  assert(Wire.devices.count(0x31) == 1);
  Wire.short_read = false;
  Wire.next_status = 4;
  loop();
  assert(phase == Scan && printed("Scan aborted due to bus error"));
  Wire.timeout_on_end = true;
  loop();
  assert(phase == Scan && printed("Wire status: 5"));
  Wire.timeout_on_end = false;
  Wire = TwoWire();
  Serial.input = "invalid\n78\n21\n0x29\r\n";
  continuousSetup();
  assert(continuousSensor.getAddress() == 0x29);
  assert(continuousSensor.getLastError() == VL53L4CD::NoError);
  assert(Wire.devices.at(0x29).running);
  assert(addressWrites() == 0 && Serial.baud == 115200);
  continuousLoop();
  assert(continuousSensor.getRangeStatus() == 0);
  Wire = TwoWire();
  Wire.devices.emplace(0x21, FakeDevice());
  Wire.devices.at(0x21).registers[0x010F] = 0;
  Serial.input = "0x\n21\n 29 \n";
  singleSetup();
  assert(singleSensor.getAddress() == 0x29);
  assert(singleSensor.getLastError() == VL53L4CD::NoError);
  assert(!Wire.devices.at(0x29).running);
  assert(addressWrites() == 0 && Serial.baud == 115200);
  singleLoop();
  assert(singleSensor.getRangeStatus() == 0);
  assert(!Wire.devices.at(0x29).running);
  assert(printed("Check the connection and enter the I2C address again"));
  std::cout << "ChangeAddress input, scanning, probes, rejection, write, and verification tests passed.\n";
  std::cout << "Continuous and Single address selection and retry tests passed.\n";
}
