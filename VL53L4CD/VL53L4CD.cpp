// Pololu-style interface adapted from the supplied VL53L4CD ULD 2.2.2
// and EZrange breakout transport. See LICENSE.txt for attribution.
/*
 * Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 */

#include "VL53L4CD.h"
#include <math.h>

// Do not change these delays.
#define VL53L4CD_I2C_DELAY_US 150
#define VL53L4CD_ADDRESS_CHANGE_DELAY_MS 60

namespace
{
  const uint8_t default_configuration[] = {
    0x00, 0x00, 0x00, 0x11, 0x02, 0x00, 0x02, 0x08,
    0x00, 0x08, 0x10, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x20, 0x0B, 0x00, 0x00, 0x02, 0x14, 0x21,
    0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x00, 0xC8, 0x00, 0x00, 0x38, 0xFF, 0x01, 0x00,
    0x08, 0x00, 0x00, 0x01, 0xCC, 0x07, 0x01, 0xF1,
    0x05, 0x00, 0xA0, 0x00, 0x80, 0x08, 0x38, 0x00,
    0x00, 0x00, 0x00, 0x0F, 0x89, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x07, 0x05, 0x06,
    0x06, 0x00, 0x00, 0x02, 0xC7, 0xFF, 0x9B, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00
  };
  static_assert(sizeof(default_configuration) == 0x87 - 0x2D + 1,
                "The default configuration must cover registers 0x2D through 0x87.");

  const uint8_t range_status_map[] = {
    255, 255, 255, 5, 2, 4, 1, 7, 3, 0, 255, 255,
    9, 13, 255, 255, 255, 255, 10, 6, 255, 255, 11, 12
  };

  bool encodeTiming(uint32_t timing_us, uint32_t macro_period_us,
                    uint8_t factor, uint16_t & encoded)
  {
    uint32_t divisor = (macro_period_us * factor) >> 6;
    if (divisor == 0) { return false; }
    uint32_t counts = ((timing_us << 12) + (divisor >> 1)) / divisor;
    if (counts == 0) { return false; }
    uint32_t mantissa = counts - 1;
    uint16_t exponent = 0;
    while (mantissa > 0xFF)
    {
      mantissa >>= 1;
      exponent++;
    }
    encoded = (exponent << 8) | mantissa;
    return true;
  }
}

VL53L4CD::VL53L4CD(uint8_t initial_address)
  : last_status(0)
  , bus(&Wire)
  , address(initial_address)
  , io_timeout(0)
  , did_timeout(false)
  , initialized(false)
  , ranging(false)
  , last_error(initial_address >= 0x08 && initial_address <= 0x77 ?
               NoError : InvalidArgument)
  , range_status(255)
  , measurement_timing_budget_us(50000)
  , inter_measurement_ms(0)
{
}

void VL53L4CD::setBus(TwoWire * new_bus)
{
  last_error = NoError;
  if (new_bus == NULL) { last_error = InvalidArgument; return; }
  if (ranging) { last_error = InvalidState; return; }
  if (bus != new_bus)
  {
    bus = new_bus;
    initialized = false;
    range_status = 255;
  }
}

void VL53L4CD::prepareTransfer()
{
#if defined(WIRE_HAS_TIMEOUT)
  // Keep the bus bounded even when sensor-ready polling has no timeout.
  bus->setWireTimeout((io_timeout ? (uint32_t)io_timeout : 1000UL) * 1000UL, true);
  bus->clearWireTimeoutFlag();
#endif
}

bool VL53L4CD::endTransfer()
{
  last_status = bus->endTransmission();
#if defined(WIRE_HAS_TIMEOUT)
  if (bus->getWireTimeoutFlag())
  {
    last_status = 5;
    last_error = Timeout;
    did_timeout = true;
    return false;
  }
#endif
  if (last_status != 0)
  {
    last_error = last_status == 5 ? Timeout : I2CError;
    if (last_error == Timeout) { did_timeout = true; }
    return false;
  }
  return true;
}

bool VL53L4CD::writeChunk(uint16_t reg, uint8_t const * src, uint8_t count)
{
  prepareTransfer();
  bus->beginTransmission(address);
  if (bus->write((uint8_t)0) != 1 ||
      bus->write((uint8_t)(reg >> 8)) != 1 ||
      bus->write((uint8_t)reg) != 1 ||
      bus->write(src, count) != count)
  {
    last_status = 1;
    last_error = I2CError;
    return false;
  }
  bool success = endTransfer();
  delayMicroseconds(VL53L4CD_I2C_DELAY_US);
  if (success && reg == I2C_SLAVE_DEVICE_ADDRESS)
  {
    delay(VL53L4CD_ADDRESS_CHANGE_DELAY_MS);
  }
  return success;
}

bool VL53L4CD::readChunk(uint16_t reg, uint8_t * dst, uint8_t count)
{
  prepareTransfer();
  bus->beginTransmission(address);
  if (bus->write(count) != 1 ||
      bus->write((uint8_t)(reg >> 8)) != 1 ||
      bus->write((uint8_t)reg) != 1)
  {
    last_status = 1;
    last_error = I2CError;
    return false;
  }
  if (!endTransfer()) { return false; }
  delayMicroseconds(VL53L4CD_I2C_DELAY_US);
  uint8_t received = bus->requestFrom(address, count);
  delayMicroseconds(VL53L4CD_I2C_DELAY_US);
#if defined(WIRE_HAS_TIMEOUT)
  if (bus->getWireTimeoutFlag())
  {
    last_status = 5;
    last_error = Timeout;
    did_timeout = true;
    return false;
  }
#endif
  if (received != count || bus->available() != count)
  {
    last_status = 4;
    last_error = I2CError;
    return false;
  }
  for (uint8_t i = 0; i < count; i++) { dst[i] = bus->read(); }
  return true;
}

void VL53L4CD::writeMulti(uint16_t reg, uint8_t const * src, uint8_t count)
{
  last_error = NoError;
  if (address < 0x08 || address > 0x77 ||
      (count != 0 && src == NULL) || (uint32_t)reg + count > 0x10000UL)
  {
    last_error = InvalidArgument;
    return;
  }
  while (count != 0)
  {
    uint8_t chunk = count > 4 ? 4 : count;
    if (!writeChunk(reg, src, chunk)) { return; }
    reg += chunk;
    src += chunk;
    count -= chunk;
  }
}

void VL53L4CD::readMulti(uint16_t reg, uint8_t * dst, uint8_t count)
{
  last_error = NoError;
  if (address < 0x08 || address > 0x77 ||
      (count != 0 && dst == NULL) || (uint32_t)reg + count > 0x10000UL)
  {
    last_error = InvalidArgument;
    return;
  }
  while (count != 0)
  {
    uint8_t chunk = count > 4 ? 4 : count;
    if (!readChunk(reg, dst, chunk)) { return; }
    reg += chunk;
    dst += chunk;
    count -= chunk;
  }
}

void VL53L4CD::writeReg(uint16_t reg, uint8_t value)
{
  writeMulti(reg, &value, 1);
}

void VL53L4CD::writeReg16Bit(uint16_t reg, uint16_t value)
{
  uint8_t data[] = { (uint8_t)(value >> 8), (uint8_t)value };
  writeMulti(reg, data, sizeof(data));
}

void VL53L4CD::writeReg32Bit(uint16_t reg, uint32_t value)
{
  uint8_t data[] = {
    (uint8_t)(value >> 24), (uint8_t)(value >> 16),
    (uint8_t)(value >> 8), (uint8_t)value
  };
  writeMulti(reg, data, sizeof(data));
}

uint8_t VL53L4CD::readReg(uint16_t reg)
{
  uint8_t value = 0xFF;
  readMulti(reg, &value, 1);
  return value;
}

uint16_t VL53L4CD::readReg16Bit(uint16_t reg)
{
  uint8_t data[2];
  readMulti(reg, data, sizeof(data));
  if (last_error != NoError) { return 0xFFFF; }
  return ((uint16_t)data[0] << 8) | data[1];
}

uint32_t VL53L4CD::readReg32Bit(uint16_t reg)
{
  uint8_t data[4];
  readMulti(reg, data, sizeof(data));
  if (last_error != NoError) { return 0xFFFFFFFFUL; }
  return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8) | data[3];
}

void VL53L4CD::setAddress(uint8_t new_addr)
{
  last_error = NoError;
  if (new_addr < 0x08 || new_addr > 0x77)
  {
    last_error = InvalidArgument;
    return;
  }
  writeReg(I2C_SLAVE_DEVICE_ADDRESS, new_addr);
  if (last_error == NoError) { address = new_addr; }
}

bool VL53L4CD::checkTimeout(uint32_t start_ms, uint32_t timeout_ms)
{
  if (timeout_ms != 0 && (uint32_t)(millis() - start_ms) >= timeout_ms)
  {
    last_error = Timeout;
    did_timeout = true;
    return true;
  }
  return false;
}

bool VL53L4CD::waitForDataReady(uint32_t timeout_ms)
{
  uint32_t start_ms = millis();
  while (true)
  {
    uint8_t polarity = readReg(GPIO_HV_MUX_CTRL);
    if (last_error != NoError) { return false; }
    uint8_t status = readReg(GPIO_TIO_HV_STATUS);
    if (last_error != NoError) { return false; }
    if ((status & 1) == ((polarity & 0x10) ? 0 : 1)) { return true; }
    if (checkTimeout(start_ms, timeout_ms)) { return false; }
    delay(1);
  }
}

void VL53L4CD::stopPreservingError()
{
  Error saved_error = last_error;
  uint8_t saved_status = last_status;
  writeReg(SYSRANGE_START, 0x80);
  if (last_error == NoError) { ranging = false; }
  if (saved_error != NoError)
  {
    last_error = saved_error;
    last_status = saved_status;
  }
}

bool VL53L4CD::init(bool)
{
  return init();
}

bool VL53L4CD::init()
{
  last_error = NoError;
  if (ranging) { last_error = InvalidState; return false; }
  initialized = false;
  range_status = 255;
  uint32_t start_ms = millis();
  uint32_t timeout_ms = io_timeout ? io_timeout : 1000;
  while (true)
  {
    uint8_t boot = readReg(FIRMWARE_SYSTEM_STATUS);
    if (last_error != NoError) { return false; }
    if (boot == 3) { break; }
    if (checkTimeout(start_ms, timeout_ms)) { return false; }
    delay(1);
  }
  uint16_t id = readReg16Bit(IDENTIFICATION_MODEL_ID);
  if (last_error != NoError) { return false; }
  if (id != 0xEBAA) { last_error = SensorNotFound; return false; }

  writeMulti(0x002D, default_configuration, sizeof(default_configuration));
  if (last_error != NoError) { return false; }
  writeReg(SYSRANGE_START, 0x40);
  if (last_error != NoError) { return false; }
  ranging = true;
  if (!waitForDataReady(timeout_ms))
  {
    stopPreservingError();
    return false;
  }
  writeReg(SYSTEM_INTERRUPT_CLEAR, 1);
  stopPreservingError();
  if (last_error != NoError) { return false; }
  writeReg(VHV_CONFIG_TIMEOUT_MACROP_LOOP_BOUND, 9);
  if (last_error != NoError) { return false; }
  writeReg(0x000B, 0);
  if (last_error != NoError) { return false; }
  writeReg16Bit(0x0024, 0x0500);
  if (last_error != NoError || !configureTiming(50000, 0)) { return false; }
  initialized = true;
  return true;
}

bool VL53L4CD::configureTiming(uint32_t budget_us, uint32_t period_ms)
{
  last_error = NoError;
  if (budget_us < 10000 || budget_us > 200000 || budget_us % 1000 != 0 ||
      (period_ms != 0 && (period_ms <= budget_us / 1000 || period_ms > 5000)))
  {
    last_error = InvalidArgument;
    return false;
  }
  uint16_t oscillator = readReg16Bit(OSC_FREQUENCY);
  if (last_error != NoError) { return false; }
  if (oscillator == 0) { last_error = InvalidArgument; return false; }
  uint64_t macro_period = ((uint64_t)2304 * (0x40000000UL / oscillator)) >> 6;
  if (macro_period == 0 || macro_period > 0xFFFFFFFFUL / 16)
  {
    last_error = InvalidArgument;
    return false;
  }
  uint32_t macro_period_us = (uint32_t)macro_period;
  uint32_t timing_us = budget_us - 2500;
  uint32_t inter_measurement = 0;
  if (period_ms != 0)
  {
    uint16_t pll = readReg16Bit(OSC_CALIBRATE_VAL);
    if (last_error != NoError) { return false; }
    pll &= 0x03FF;
    if (pll == 0) { last_error = InvalidArgument; return false; }
    inter_measurement = (uint32_t)(1.055f * period_ms * pll);
    timing_us = (budget_us - 4300) / 2;
  }
  uint16_t config_a, config_b;
  if (!encodeTiming(timing_us, macro_period_us, 16, config_a) ||
      !encodeTiming(timing_us, macro_period_us, 12, config_b))
  {
    last_error = InvalidArgument;
    return false;
  }
  writeReg32Bit(SYSTEM_INTERMEASUREMENT_PERIOD, inter_measurement);
  if (last_error == NoError) { writeReg16Bit(RANGE_CONFIG_A, config_a); }
  if (last_error == NoError) { writeReg16Bit(RANGE_CONFIG_B, config_b); }
  if (last_error != NoError)
  {
    // A partially applied timing configuration must not be used for ranging.
    initialized = false;
    return false;
  }
  measurement_timing_budget_us = budget_us;
  inter_measurement_ms = period_ms;
  return true;
}

bool VL53L4CD::setMeasurementTimingBudget(uint32_t budget_us)
{
  last_error = NoError;
  if (!initialized) { last_error = NotInitialized; return false; }
  if (ranging) { last_error = InvalidState; return false; }
  return configureTiming(budget_us, inter_measurement_ms);
}

uint32_t VL53L4CD::getMeasurementTimingBudget()
{
  last_error = NoError;
  if (!initialized) { last_error = NotInitialized; return 0; }
  return measurement_timing_budget_us;
}

bool VL53L4CD::setSignalRateLimit(float limit_Mcps)
{
  last_error = NoError;
  if (!initialized) { last_error = NotInitialized; return false; }
  if (!isfinite(limit_Mcps) || limit_Mcps < 0 || limit_Mcps > 16.384f)
  {
    last_error = InvalidArgument;
    return false;
  }
  uint16_t kcps = (uint16_t)(limit_Mcps * 1000.0f);
  writeReg16Bit(FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN_LIMIT, kcps >> 3);
  return last_error == NoError;
}

float VL53L4CD::getSignalRateLimit()
{
  last_error = NoError;
  if (!initialized) { last_error = NotInitialized; return NAN; }
  uint16_t raw = readReg16Bit(FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN_LIMIT);
  if (last_error != NoError) { return NAN; }
  return (float)raw * 0.008f;
}

void VL53L4CD::startContinuous(uint32_t period_ms)
{
  last_error = NoError;
  if (!initialized) { last_error = NotInitialized; return; }
  if (ranging) { last_error = InvalidState; return; }
  range_status = 255;
  if (!configureTiming(measurement_timing_budget_us, period_ms)) { return; }
  writeReg(SYSRANGE_START, period_ms == 0 ? 0x21 : 0x40);
  if (last_error == NoError) { ranging = true; }
}

void VL53L4CD::stopContinuous()
{
  last_error = NoError;
  if (!initialized) { last_error = NotInitialized; return; }
  writeReg(SYSRANGE_START, 0x80);
  if (last_error == NoError) { ranging = false; }
}

uint16_t VL53L4CD::readRangeContinuousMillimeters()
{
  last_error = NoError;
  range_status = 255;
  if (!initialized) { last_error = NotInitialized; return 65535; }
  if (!ranging) { last_error = InvalidState; return 65535; }
  if (!waitForDataReady(io_timeout)) { return 65535; }
  uint8_t raw_status = readReg(RESULT_RANGE_STATUS) & 0x1F;
  if (last_error != NoError) { return 65535; }
  uint16_t distance = readReg16Bit(RESULT_RANGE_DISTANCE);
  if (last_error != NoError) { return 65535; }
  writeReg(SYSTEM_INTERRUPT_CLEAR, 1);
  if (last_error != NoError) { return 65535; }
  range_status = raw_status < sizeof(range_status_map) ?
    range_status_map[raw_status] : 255;
  return distance;
}

uint16_t VL53L4CD::readRangeSingleMillimeters()
{
  range_status = 255;
  startContinuous();
  if (last_error != NoError) { return 65535; }
  uint16_t distance = readRangeContinuousMillimeters();
  stopPreservingError();
  if (last_error != NoError) { range_status = 255; return 65535; }
  return distance;
}

bool VL53L4CD::timeoutOccurred()
{
  bool occurred = did_timeout;
  did_timeout = false;
  return occurred;
}
