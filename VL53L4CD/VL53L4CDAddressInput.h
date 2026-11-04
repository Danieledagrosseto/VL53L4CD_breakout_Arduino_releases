#ifndef VL53L4CD_ADDRESS_INPUT_H
#define VL53L4CD_ADDRESS_INPUT_H

#include <Arduino.h>

// Shared by the interactive examples; consumes complete CR/LF-terminated lines.
template<typename SerialType>
bool readVL53L4CDAddress(SerialType & serial, uint8_t & address)
{
  static char input[12];
  static uint8_t length = 0;
  static bool overflow = false;
  while (serial.available())
  {
    char ch = serial.read();
    if (ch != '\r' && ch != '\n')
    {
      if (length < sizeof(input) - 1) { input[length++] = ch; }
      else { overflow = true; }
      continue;
    }
    if (length == 0 && !overflow) { continue; }
    input[length] = '\0';
    uint8_t end = length;
    uint8_t start = 0;
    while (start < end && (input[start] == ' ' || input[start] == '\t')) { start++; }
    while (end > start && (input[end - 1] == ' ' || input[end - 1] == '\t')) { end--; }
    if (end - start >= 2 && input[start] == '0' &&
        (input[start + 1] == 'x' || input[start + 1] == 'X')) { start += 2; }
    bool valid = !overflow && end > start && end - start <= 2;
    uint16_t value = 0;
    for (uint8_t i = start; i < end && valid; i++)
    {
      char digit = input[i];
      uint8_t nibble;
      if (digit >= '0' && digit <= '9') { nibble = digit - '0'; }
      else if (digit >= 'a' && digit <= 'f') { nibble = digit - 'a' + 10; }
      else if (digit >= 'A' && digit <= 'F') { nibble = digit - 'A' + 10; }
      else { valid = false; break; }
      value = value * 16 + nibble;
    }
    length = 0;
    overflow = false;
    if (!valid || value < 0x08 || value > 0x77)
    {
      serial.println("Error: enter a hexadecimal 7-bit address from 08 to 77. Try again:");
      return false;
    }
    address = (uint8_t)value;
    return true;
  }
  return false;
}

#endif
