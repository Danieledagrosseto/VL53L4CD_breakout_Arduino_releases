#ifndef TEST_WIRE_H
#define TEST_WIRE_H

#include "Arduino.h"
#include <array>
#include <map>
#include <vector>

#ifndef TEST_WIRE_WITHOUT_TIMEOUT
#define WIRE_HAS_TIMEOUT
#endif

struct FakeDevice
{
  std::array<uint8_t, 65536> registers{};
  bool running = false;
  bool auto_ready = true;
  unsigned int clears = 0;

  FakeDevice()
  {
    registers[0x00E5] = 3;
    registers[0x010F] = 0xEB;
    registers[0x0110] = 0xAA;
    registers[0x0006] = 0xBC;
    registers[0x0007] = 0xCC;
    registers[0x00DE] = 2;
    registers[0x0089] = 9;
    registers[0x0096] = 1;
    registers[0x0097] = 0x23;
  }
};

struct FakeTransaction
{
  uint8_t address;
  std::vector<uint8_t> bytes;
  bool read;
};

class TwoWire
{
  public:
    std::map<uint8_t, FakeDevice> devices;
    std::vector<FakeTransaction> transactions;
    uint8_t next_status = 0;
    int fail_write_register = -1;
    bool short_read = false;
    bool timeout_on_end = false;
    bool timeout_on_read = false;
    bool reject_buffer = false;
    uint32_t wire_timeout_us = 0;
    uint32_t clock_hz = 100000;

    TwoWire() { devices.emplace(0x29, FakeDevice()); }
    void begin() {}
    void setClock(uint32_t frequency) { clock_hz = frequency; }
    void beginTransmission(uint8_t address)
    {
      target = address;
      tx.clear();
    }
    size_t write(uint8_t value)
    {
      if (reject_buffer || tx.size() == 32) { return 0; }
      tx.push_back(value);
      return 1;
    }
    size_t write(uint8_t const * data, size_t count)
    {
      size_t written = 0;
      while (written < count && write(data[written]) == 1) { written++; }
      return written;
    }
    uint8_t endTransmission()
    {
      transactions.push_back({target, tx, false});
      if (timeout_on_end) { timeout_flag = true; return 5; }
      if (next_status != 0)
      {
        uint8_t status = next_status;
        next_status = 0;
        return status;
      }
      if (devices.count(target) == 0) { return 2; }
      if (tx.empty()) { return 0; }
      if (tx.size() < 3) { return 3; }
      uint16_t reg = ((uint16_t)tx[1] << 8) | tx[2];
      if (tx[0] != 0)
      {
        if (tx.size() != 3 || tx[0] > 4) { return 3; }
        read_register = reg;
        read_count = tx[0];
        return 0;
      }
      if (reg == fail_write_register) { return 3; }
      if (tx.size() < 4 || tx.size() > 7) { return 3; }
      FakeDevice & device = devices.at(target);
      for (size_t i = 3; i < tx.size(); i++)
      {
        uint16_t current = reg + i - 3;
        device.registers[current] = tx[i];
        if (current == 0x0087)
        {
          device.running = tx[i] == 0x21 || tx[i] == 0x40;
        }
        if (current == 0x0086 && tx[i] == 1) { device.clears++; }
      }
      if (reg == 1 && tx[3] != target)
      {
        FakeDevice moved = device;
        devices.erase(target);
        devices.emplace(tx[3], moved);
      }
      return 0;
    }
    uint8_t requestFrom(uint8_t address, uint8_t count)
    {
      transactions.push_back({address, {count}, true});
      rx.clear();
      rx_index = 0;
      if (timeout_on_read) { timeout_flag = true; return 0; }
      if (devices.count(address) == 0 || count != read_count) { return 0; }
      FakeDevice & device = devices.at(address);
      uint8_t received = short_read ? count - 1 : count;
      for (uint8_t i = 0; i < received; i++)
      {
        uint16_t reg = read_register + i;
        uint8_t value = device.registers[reg];
        if (reg == 0x0031)
        {
          uint8_t ready = (device.registers[0x0030] & 0x10) ? 0 : 1;
          value = device.running && device.auto_ready ? ready : ready ^ 1;
        }
        rx.push_back(value);
      }
      return received;
    }
    int available() { return (int)(rx.size() - rx_index); }
    int read() { return rx_index < rx.size() ? rx[rx_index++] : -1; }
#ifdef WIRE_HAS_TIMEOUT
    void setWireTimeout(uint32_t timeout, bool) { wire_timeout_us = timeout; }
    void clearWireTimeoutFlag() { timeout_flag = false; }
    bool getWireTimeoutFlag() { return timeout_flag; }
#endif

  private:
    uint8_t target = 0;
    uint16_t read_register = 0;
    uint8_t read_count = 0;
    bool timeout_flag = false;
    std::vector<uint8_t> tx;
    std::vector<uint8_t> rx;
    size_t rx_index = 0;
};

extern TwoWire Wire;

#endif
