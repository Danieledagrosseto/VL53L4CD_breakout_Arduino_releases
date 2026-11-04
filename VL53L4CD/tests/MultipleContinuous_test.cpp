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
    bool forbidOutput = false;
    bool flushed = false;
    void begin(unsigned long rate) { baud = rate; }
    int available() { return (int)input.size(); }
    int read()
    {
      char ch = input[0];
      input.erase(0, 1);
      return ch;
    }
    void flush() { flushed = true; }
    template<typename T> void print(T value)
    {
      assert(!forbidOutput);
      output << value;
    }
    void print(uint8_t value) { print((unsigned int)value); }
    template<typename T> void print(T value, int)
    {
      assert(!forbidOutput);
      output << std::hex << (unsigned int)value << std::dec;
    }
    template<typename T> void println(T value) { print(value); print('\n'); }
    template<typename T> void println(T value, int base) { print(value, base); print('\n'); }
};

FakeSerial Serial;
TwoWire Wire;
static uint32_t clock_ms = 0;
unsigned long millis() { return clock_ms; }
void delay(unsigned long ms) { clock_ms += ms; }
void delayMicroseconds(unsigned int) {}

#define F(text) text
#define HEX 16
#include "..\examples\MultipleContinuous\MultipleContinuous.ino"

static bool printed(std::string const & text)
{
  return Serial.output.str().find(text) != std::string::npos;
}

static void reset(uint8_t count, std::string const & input = "10\n0\n3\n")
{
  Wire = TwoWire();
  Wire.devices.clear();
  for (uint8_t i = 0; i < count; i++) { Wire.devices.emplace(0x30 + i, FakeDevice()); }
  Serial.input = input;
  Serial.output.str("");
  Serial.output.clear();
  Serial.flushed = false;
  Serial.forbidOutput = false;
  sensorCount = 0;
  frameCount = 0;
  configured = false;
  finished = false;
  acquisitionTimeMs = 0;
  clock_ms = 0;
}

static void startSensors()
{
  for (uint8_t i = 0; i < sensorCount; i++)
  {
    sensors[i].startContinuous(periodMs);
    assert(sensors[i].getLastError() == VL53L4CD::NoError);
  }
}

int main()
{
  reset(6, "9\n201\n999999999999999999999\nabc\n10\n10\n5001\n0\n0\n26\n25\n");
  setup();
  assert(configured && sensorCount == 6 && frameCount == 25);
  assert(sizeof(samples) == 600);
  assert(Serial.baud == 115200 && Serial.flushed && Wire.clock_hz == 400000);
  assert(printed("Error: nonzero period"));
  assert(printed("Number of frames (1-25)"));
  assert(printed("decimal integer from 1 to 25"));
  loop();
  assert(finished && acquisitionTimeMs == 250);
  assert(printed("Acquisition time, capture start to last completed frame (ms): 250"));
  for (uint16_t i = 0; i < 150; i++)
  {
    assert(samples[i].distance == 0x123);
    assert(samples[i].error == VL53L4CD::NoError && samples[i].status == 0);
  }
  for (auto const & item : Wire.devices) { assert(!item.second.running); }
  assert(printed("25,0x35,291,0,0"));
  size_t timePosition = Serial.output.str().find("Acquisition time");
  size_t dataPosition = Serial.output.str().find("frame,address_hex");
  assert(timePosition < dataPosition);
  std::string output = Serial.output.str();
  std::istringstream csv(output.substr(dataPosition));
  std::string line;
  std::getline(csv, line);
  assert(line == "frame,address_hex,distance_mm,error,range_status");
  unsigned int rows = 0;
  while (std::getline(csv, line))
  {
    if (line.find("Capture complete") == 0) { break; }
    assert(line.find(",291,0,0") != std::string::npos);
    rows++;
  }
  assert(rows == 150);
  loop();
  assert(Serial.output.str() == output);
  for (auto const & transaction : Wire.transactions)
  {
    // Discovery/configuration must never program the persistent address.
    assert(transaction.read || transaction.bytes.size() < 4 ||
           transaction.bytes[0] != 0 || transaction.bytes[1] != 0 ||
           transaction.bytes[2] != 1);
  }

  reset(2, "20\r\n50\r\n3\r\n");
  setup();
  startSensors();
  Wire.devices.at(0x31).registers[0x0089] = 4;
  Serial.forbidOutput = true;
  acquireFrames();
  Serial.forbidOutput = false;
  assert(acquisitionTimeMs == 120);
  assert(samples[1].error == VL53L4CD::NoError && samples[1].status == 2);

  reset(1, "10\n0\n1\n");
  setup();
  loop();
  assert(acquisitionTimeMs == 10 && printed("frame,address_hex"));

  reset(1, "10\n0\n150\n");
  setup();
  startSensors();
  clock_ms = UINT32_MAX - 5;
  Serial.forbidOutput = true;
  acquireFrames();
  Serial.forbidOutput = false;
  assert(frameCount == 150 && acquisitionTimeMs == 1500);

  reset(2, "10\n0\n2\n");
  setup();
  startSensors();
  Wire.devices.at(0x30).auto_ready = false;
  Serial.forbidOutput = true;
  acquireFrames();
  Serial.forbidOutput = false;
  assert(samples[0].distance == 65535 && samples[0].error == VL53L4CD::Timeout);
  assert(samples[0].status == 255);
  assert(samples[1].error == VL53L4CD::NoError && samples[3].distance == 0x123);
  assert(acquisitionTimeMs == 1020);

  reset(1);
  setup();
  startSensors();
  Wire.short_read = true;
  acquireFrames();
  assert(samples[0].distance == 65535 && samples[0].error == VL53L4CD::I2CError);

  reset(0);
  setup();
  assert(!configured && printed("No verified sensors"));
  loop();
  assert(!printed("frame,address_hex"));

  reset(1);
  Wire.devices.at(0x30).registers[0x010F] = 0;
  setup();
  assert(!configured && printed("Skipping unverified device"));

  reset(7);
  setup();
  assert(!configured && printed("Too many sensors"));

  reset(1);
  Wire.next_status = 4;
  setup();
  assert(!configured && printed("Scan aborted"));

#ifdef WIRE_HAS_TIMEOUT
  reset(1);
  Wire.timeout_on_end = true;
  setup();
  assert(!configured && printed("Wire status: 5"));
#endif

  reset(1);
  Wire.fail_write_register = 0x002D;
  setup();
  assert(!configured && printed("Configuration failed"));

  reset(2);
  setup();
  Wire.devices.erase(0x31);
  loop();
  assert(printed("Start failed") && !printed("frame,address_hex"));
  assert(!Wire.devices.at(0x30).running);

  reset(2);
  setup();
  Wire.fail_write_register = VL53L4CD::SYSRANGE_START;
  loop();
  assert(printed("Start failed") && !printed("frame,address_hex"));

  reset(1);
  setup();
  startSensors();
  Wire.fail_write_register = VL53L4CD::SYSRANGE_START;
  stopSensors(sensorCount);
  assert(printed("Sensor 0x30, error:"));
  std::cout << "MultipleContinuous discovery, input, buffer limits, cadence, rollover, "
               "silent capture, CSV, validity, and failure tests passed.\n";
}
