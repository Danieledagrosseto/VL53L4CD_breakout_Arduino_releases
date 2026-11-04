#include "VL53L4CD.h"
#include <assert.h>
#include <cmath>
#include <iostream>
#include <limits>

TwoWire Wire;
static uint64_t clock_us;
static std::vector<unsigned int> micro_delays;
static std::vector<unsigned long> milli_delays;

unsigned long millis() { return (uint32_t)(clock_us / 1000); }
void delay(unsigned long ms)
{
  clock_us += (uint64_t)ms * 1000;
  milli_delays.push_back(ms);
}
void delayMicroseconds(unsigned int us)
{
  clock_us += us;
  micro_delays.push_back(us);
}

static void reset()
{
  Wire = TwoWire();
  clock_us = 0;
  micro_delays.clear();
  milli_delays.clear();
}

static uint16_t word(FakeDevice const & device, uint16_t reg)
{
  return ((uint16_t)device.registers[reg] << 8) | device.registers[reg + 1];
}

static uint32_t dword(FakeDevice const & device, uint16_t reg)
{
  return ((uint32_t)word(device, reg) << 16) | word(device, reg + 2);
}

static void transport()
{
  reset();
  VL53L4CD sensor;
  sensor.writeReg32Bit(0x0123, 0x12345678);
  assert(sensor.getLastError() == VL53L4CD::NoError);
  assert((Wire.transactions[0].bytes ==
          std::vector<uint8_t>{0, 1, 0x23, 0x12, 0x34, 0x56, 0x78}));
  assert(sensor.readReg32Bit(0x0123) == 0x12345678);
  assert((Wire.transactions[1].bytes == std::vector<uint8_t>{4, 1, 0x23}));
  assert(Wire.transactions[2].read);
  assert(micro_delays.size() == 3);
  for (auto us : micro_delays) { assert(us == 150); }

  uint8_t source[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
  uint8_t destination[sizeof(source)]{};
  Wire.transactions.clear();
  sensor.writeMulti(0x0200, source, sizeof(source));
  assert(Wire.transactions.size() == 3);
  assert(Wire.transactions[0].bytes.size() == 7);
  assert(Wire.transactions[1].bytes[2] == 4);
  assert(Wire.transactions[2].bytes.size() == 4);
  sensor.readMulti(0x0200, destination, sizeof(destination));
  for (size_t i = 0; i < sizeof(source); i++) { assert(source[i] == destination[i]); }
  assert(sensor.getLastError() == VL53L4CD::NoError);

  sensor.writeReg(0x0300, 0x5A);
  assert(sensor.readReg(0x0300) == 0x5A);
  sensor.writeReg16Bit(0x0301, 0x1234);
  assert(sensor.readReg16Bit(0x0301) == 0x1234);
  sensor.writeReg16Bit(0xFFFF, 123);
  assert(sensor.getLastError() == VL53L4CD::InvalidArgument);
  sensor.readMulti(0, NULL, 1);
  assert(sensor.getLastError() == VL53L4CD::InvalidArgument);
  Wire.reject_buffer = true;
  sensor.writeReg(0x100, 1);
  assert(sensor.getLastError() == VL53L4CD::I2CError && sensor.last_status == 1);
  Wire.reject_buffer = false;
  Wire.next_status = 2;
  assert(sensor.readReg16Bit(0) == 0xFFFF);
  assert(sensor.getLastError() == VL53L4CD::I2CError && sensor.last_status == 2);
  Wire.short_read = true;
  assert(sensor.readReg32Bit(0) == 0xFFFFFFFFUL);
  assert(sensor.getLastError() == VL53L4CD::I2CError && sensor.last_status == 4);
}

static void addressesAndBuses()
{
  reset();
  VL53L4CD sensor;
  sensor.setAddress(0x30);
  assert(sensor.getAddress() == 0x30);
  assert(Wire.devices.count(0x30) == 1 && Wire.devices.count(0x29) == 0);
  assert(milli_delays.back() == 60);
  assert(sensor.readReg16Bit(VL53L4CD::IDENTIFICATION_MODEL_ID) == 0xEBAA);
  sensor.setAddress(0x78);
  assert(sensor.getLastError() == VL53L4CD::InvalidArgument);
  assert(sensor.getAddress() == 0x30);
  Wire.next_status = 2;
  sensor.setAddress(0x31);
  assert(sensor.getAddress() == 0x30 && sensor.last_status == 2);
  VL53L4CD second(0x30);
  assert(second.init());
  VL53L4CD invalid(0x80);
  size_t before = Wire.transactions.size();
  assert(!invalid.init() && invalid.getLastError() == VL53L4CD::InvalidArgument);
  assert(before == Wire.transactions.size());

  TwoWire alternate;
  second.setBus(&alternate);
  assert(second.getBus() == &alternate);
  second.startContinuous();
  assert(second.getLastError() == VL53L4CD::NotInitialized);
  second.setBus(NULL);
  assert(second.getLastError() == VL53L4CD::InvalidArgument);
  assert(second.getBus() == &alternate);
  VL53L4CD other;
  other.setBus(&alternate);
  assert(other.init());
  other.startContinuous();
  other.setBus(&Wire);
  assert(other.getLastError() == VL53L4CD::InvalidState);
  assert(other.getBus() == &alternate);
  other.stopContinuous();
  assert(second.getAddress() == 0x30 && other.getAddress() == 0x29);
}

static void initialization()
{
  reset();
  VL53L4CD sensor;
  assert(sensor.init());
  FakeDevice & device = Wire.devices.at(0x29);
  assert(!device.running);
  assert(device.registers[0x0008] == 9);
  assert(word(device, 0x0024) == 0x0500);
  assert(word(device, 0x0064) == 0x00A0);
  assert(word(device, 0x0066) == 0x0080);
  assert(device.registers[0x0081] == 0x9B);
  assert(device.clears == 1);
  assert(dword(device, 0x006C) == 0);
  assert(sensor.getMeasurementTimingBudget() == 50000);
  assert(std::fabs(sensor.getSignalRateLimit() - 1.024f) < 0.0001f);

  reset();
  Wire.devices.at(0x29).registers[0x010F] = 0;
  assert(!sensor.init() && sensor.getLastError() == VL53L4CD::SensorNotFound);
  reset();
  sensor.setTimeout(3);
  Wire.devices.at(0x29).registers[0x00E5] = 0;
  assert(!sensor.init() && sensor.getLastError() == VL53L4CD::Timeout);
  assert(sensor.timeoutOccurred() && !sensor.timeoutOccurred());
  reset();
  Wire.devices.at(0x29).auto_ready = false;
  assert(!sensor.init() && sensor.getLastError() == VL53L4CD::Timeout);
  assert(!Wire.devices.at(0x29).running);
  reset();
  Wire.fail_write_register = VL53L4CD::RANGE_CONFIG_A;
  assert(!sensor.init() && sensor.getLastError() == VL53L4CD::I2CError);
}

static void checkEncodedTiming(FakeDevice const & device, uint32_t budget_us,
                               bool autonomous)
{
  uint32_t macro = (2304UL * (0x40000000UL / word(device, 0x0006))) >> 6;
  for (uint8_t factor : {16, 12})
  {
    uint16_t encoded = word(device, factor == 16 ? 0x005E : 0x0061);
    uint64_t counts = (uint64_t)((encoded & 0xFF) + 1) << (encoded >> 8);
    uint32_t decoded = (uint32_t)(counts * ((macro * factor) >> 6) / 4096);
    decoded = autonomous ? decoded * 2 + 4300 : decoded + 2500;
    uint32_t difference = decoded > budget_us ? decoded - budget_us : budget_us - decoded;
    assert(difference <= budget_us / 100 + 100);
  }
}

static void timingAndThresholds()
{
  reset();
  VL53L4CD sensor;
  assert(sensor.init());
  FakeDevice & device = Wire.devices.at(0x29);
  for (uint32_t ms = 10; ms <= 200; ms++)
  {
    assert(sensor.setMeasurementTimingBudget(ms * 1000));
    assert(sensor.getMeasurementTimingBudget() == ms * 1000);
    checkEncodedTiming(device, ms * 1000, false);
    sensor.startContinuous(5000);
    assert(sensor.getLastError() == VL53L4CD::NoError);
    assert(dword(device, 0x006C) == (uint32_t)(1.055f * 5000 * 512));
    checkEncodedTiming(device, ms * 1000, true);
    sensor.stopContinuous();
    sensor.startContinuous();
    assert(sensor.getLastError() == VL53L4CD::NoError);
    sensor.stopContinuous();
  }
  for (uint32_t invalid : {0, 9999, 10001, 199999, 200001})
  {
    size_t before = Wire.transactions.size();
    assert(!sensor.setMeasurementTimingBudget(invalid));
    assert(sensor.getLastError() == VL53L4CD::InvalidArgument);
    assert(before == Wire.transactions.size());
  }
  for (uint32_t invalid : {1, 200, 5001})
  {
    sensor.startContinuous(invalid);
    assert(sensor.getLastError() == VL53L4CD::InvalidArgument);
    assert(!device.running);
  }
  assert(sensor.setSignalRateLimit(0));
  assert(sensor.getSignalRateLimit() == 0);
  assert(sensor.setSignalRateLimit(0.1f));
  assert(word(device, 0x0066) == 12);
  assert(std::fabs(sensor.getSignalRateLimit() - 0.096f) < 0.0001f);
  assert(sensor.setSignalRateLimit(16.384f));
  assert(word(device, 0x0066) == 2048);
  for (float invalid : {-1.0f, 16.385f, std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::quiet_NaN()})
  {
    assert(!sensor.setSignalRateLimit(invalid));
    assert(sensor.getLastError() == VL53L4CD::InvalidArgument);
  }
  device.registers[0x0006] = 0;
  device.registers[0x0007] = 0;
  assert(!sensor.setMeasurementTimingBudget(50000));
  assert(sensor.getLastError() == VL53L4CD::InvalidArgument);
  device.registers[0x0007] = 1;
  assert(!sensor.setMeasurementTimingBudget(50000));
  assert(sensor.getLastError() == VL53L4CD::InvalidArgument);
  device.registers[0x0006] = 0xBC;
  device.registers[0x0007] = 0xCC;
  device.registers[0x00DE] = 0;
  sensor.startContinuous(1000);
  assert(sensor.getLastError() == VL53L4CD::InvalidArgument);
}

static void rangingAndFailures()
{
  reset();
  VL53L4CD sensor;
  assert(sensor.readRangeSingleMillimeters() == 65535);
  assert(sensor.getLastError() == VL53L4CD::NotInitialized);
  assert(sensor.init());
  FakeDevice & device = Wire.devices.at(0x29);
  assert(sensor.readRangeContinuousMillimeters() == 65535);
  assert(sensor.getLastError() == VL53L4CD::InvalidState);
  assert(sensor.readRangeSingleMillimeters() == 0x0123);
  assert(sensor.getRangeStatus() == 0 && !device.running);
  device.registers[0x0089] = 4;
  assert(sensor.readRangeSingleMillimeters() == 0x0123);
  assert(sensor.getRangeStatus() == 2 && sensor.getLastError() == VL53L4CD::NoError);
  device.registers[0x0089] = 31;
  assert(sensor.readRangeSingleMillimeters() == 0x0123);
  assert(sensor.getRangeStatus() == 255);

  sensor.startContinuous();
  assert(device.running && device.registers[0x0087] == 0x21);
  assert(!sensor.setMeasurementTimingBudget(10000));
  assert(sensor.getLastError() == VL53L4CD::InvalidState);
  device.registers[0x0089] = 9;
  assert(sensor.readRangeContinuousMillimeters() == 0x0123);
  assert(sensor.getRangeStatus() == 0);
  assert(sensor.readRangeSingleMillimeters() == 65535);
  assert(sensor.getLastError() == VL53L4CD::InvalidState && device.running);
  assert(sensor.getRangeStatus() == 255);
  sensor.stopContinuous();
  sensor.setTimeout(3);
  device.auto_ready = false;
  assert(sensor.readRangeSingleMillimeters() == 65535);
  assert(sensor.getLastError() == VL53L4CD::Timeout && !device.running);
  assert(sensor.getRangeStatus() == 255);
  assert(sensor.timeoutOccurred() && !sensor.timeoutOccurred());
  // Exercise millis() wraparound during the ready timeout.
  clock_us = (uint64_t)0xFFFFFFFFUL * 1000;
  assert(sensor.readRangeSingleMillimeters() == 65535);
  assert(sensor.getLastError() == VL53L4CD::Timeout && !device.running);

  device.auto_ready = true;
  Wire.fail_write_register = VL53L4CD::SYSTEM_INTERRUPT_CLEAR;
  assert(sensor.readRangeSingleMillimeters() == 65535);
  assert(sensor.getLastError() == VL53L4CD::I2CError && sensor.last_status == 3);
  assert(!device.running && sensor.getRangeStatus() == 255);
  Wire.fail_write_register = VL53L4CD::RANGE_CONFIG_A;
  assert(!sensor.setMeasurementTimingBudget(50000));
  sensor.startContinuous();
  assert(sensor.getLastError() == VL53L4CD::NotInitialized);
}

static void busTimeouts()
{
#ifdef WIRE_HAS_TIMEOUT
  reset();
  VL53L4CD sensor;
  Wire.timeout_on_end = true;
  sensor.readReg(0);
  assert(sensor.getLastError() == VL53L4CD::Timeout && sensor.last_status == 5);
  assert(sensor.timeoutOccurred() && !sensor.timeoutOccurred());
  assert(Wire.wire_timeout_us == 1000000);
  Wire.timeout_on_end = false;
  Wire.timeout_on_read = true;
  sensor.setTimeout(12);
  sensor.readReg(0);
  assert(sensor.getLastError() == VL53L4CD::Timeout && sensor.last_status == 5);
  assert(Wire.wire_timeout_us == 12000 && sensor.timeoutOccurred());
#endif
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
static void compatibilityOverload()
{
  reset();
  VL53L4CD sensor;
  assert(sensor.init(false));
  auto configuration = Wire.devices.at(0x29).registers;
  reset();
  assert(sensor.init(true));
  assert(configuration == Wire.devices.at(0x29).registers);
}
#pragma GCC diagnostic pop

int main()
{
  transport();
  addressesAndBuses();
  initialization();
  timingAndThresholds();
  rangingAndFailures();
  busTimeouts();
  compatibilityOverload();
  std::cout << "All VL53L4CD transport, timing, ranging, and error tests passed";
#ifdef WIRE_HAS_TIMEOUT
  std::cout << " (Wire timeout API enabled).\n";
#else
  std::cout << " (Wire timeout API unavailable).\n";
#endif
}
