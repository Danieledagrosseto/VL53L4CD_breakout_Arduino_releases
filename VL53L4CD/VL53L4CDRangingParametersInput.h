#ifndef VL53L4CD_RANGING_PARAMETERS_INPUT_H
#define VL53L4CD_RANGING_PARAMETERS_INPUT_H

#include <Arduino.h>
#include "VL53L4CD.h"

// Host-test fakes do not provide Arduino's flash-string macro.
#ifndef F
#define F(text) text
#endif

// Shared by the ranging examples; blocks until a CR/LF-terminated decimal
// line arrives. An empty line keeps defaultValue. crPending carries CR/LF
// pairing state across consecutive prompts of one session. PromptType may be
// a C string or an F() flash string.
template<typename SerialType, typename PromptType>
long readVL53L4CDRangingValue(SerialType & serial, PromptType prompt,
                              long minimum, long maximum, long defaultValue,
                              bool & crPending)
{
  char input[12];
  uint8_t length = 0;
  bool overflow = false;
  serial.println(prompt);
  while (true)
  {
    if (!serial.available()) { continue; }
    char ch = serial.read();
    if (ch == '\n' && crPending)
    {
      // Second half of a CR/LF pair, not an empty line.
      crPending = false;
      continue;
    }
    crPending = false;
    if (ch != '\r' && ch != '\n')
    {
      if (length < (uint8_t)(sizeof(input) - 1)) { input[length++] = ch; }
      else { overflow = true; }
      continue;
    }
    if (ch == '\r') { crPending = true; }

    uint8_t start = 0;
    uint8_t end = length;
    while (start < end && (input[start] == ' ' || input[start] == '\t')) { start++; }
    while (end > start && (input[end - 1] == ' ' || input[end - 1] == '\t')) { end--; }
    bool valid = !overflow;
    long value = defaultValue;
    if (valid && end > start)
    {
      bool negative = input[start] == '-';
      if (negative) { start++; }
      valid = end > start;
      long magnitude = 0;
      for (uint8_t i = start; i < end && valid; i++)
      {
        if (input[i] < '0' || input[i] > '9') { valid = false; break; }
        magnitude = magnitude * 10 + (input[i] - '0');
        if (magnitude > 100000L) { valid = false; }
      }
      if (valid) { value = negative ? -magnitude : magnitude; }
    }
    length = 0;
    overflow = false;
    if (valid && value >= minimum && value <= maximum) { return value; }
    serial.print(F("Error: enter a decimal integer from "));
    serial.print(minimum);
    serial.print(F(" to "));
    serial.print(maximum);
    serial.println(F(", or an empty line to keep the current value."));
    serial.println(prompt);
  }
}

template<typename SerialType, typename OperationType>
void printVL53L4CDParameterError(SerialType & serial, VL53L4CD & sensor,
                                 OperationType operation)
{
  serial.print(operation);
  serial.print(F(" failed. Error: "));
  serial.print(sensor.getLastError());
  serial.print(F(", Wire status: "));
  serial.println(sensor.last_status);
}

template<typename SerialType>
bool readVL53L4CDRangingParameters(SerialType & serial, VL53L4CD & sensor,
                                   uint16_t & sigma_mm, uint16_t & signal_kcps,
                                   int16_t & offset_mm)
{
  sigma_mm = sensor.getSigmaThreshold();
  if (sensor.getLastError() != VL53L4CD::NoError)
  {
    printVL53L4CDParameterError(serial, sensor, F("Reading sigma threshold"));
    return false;
  }
  signal_kcps = sensor.getSignalThreshold();
  if (sensor.getLastError() != VL53L4CD::NoError)
  {
    printVL53L4CDParameterError(serial, sensor, F("Reading signal threshold"));
    return false;
  }
  offset_mm = sensor.getOffset();
  if (sensor.getLastError() != VL53L4CD::NoError)
  {
    printVL53L4CDParameterError(serial, sensor, F("Reading offset"));
    return false;
  }
  return true;
}

template<typename SerialType, typename LabelType>
void printVL53L4CDRangingParameters(SerialType & serial,
                                    LabelType label,
                                    uint16_t sigma_mm, uint16_t signal_kcps,
                                    int16_t offset_mm)
{
  serial.print(label);
  serial.print(F(" sigma threshold: "));
  serial.print(sigma_mm);
  serial.print(F(" mm; signal threshold: "));
  serial.print(signal_kcps);
  serial.print(F(" kcps; offset: "));
  serial.print(offset_mm);
  serial.println(F(" mm"));
}

// Shows the sensor's current sigma threshold, signal threshold, and offset,
// prompts for new values (an empty line keeps the current one), applies
// them, and prints the values read back from the sensor. The sensor must be
// initialized and not ranging. Returns false after printing an error.
template<typename SerialType>
bool promptVL53L4CDRangingParameters(SerialType & serial, VL53L4CD & sensor)
{
  // A previous CR/LF-terminated line (such as an address or frame-count
  // entry) may have left its LF buffered; discard it so it is not read as
  // an empty first answer. Typed answers cannot be buffered yet because the
  // first prompt has not been printed.
  delay(10);
  if (serial.available() && serial.peek() == '\n') { serial.read(); }

  uint16_t sigma_mm;
  uint16_t signal_kcps;
  int16_t offset_mm;
  if (!readVL53L4CDRangingParameters(serial, sensor, sigma_mm, signal_kcps,
                                     offset_mm))
  {
    return false;
  }
  printVL53L4CDRangingParameters(serial, F("Current"), sigma_mm, signal_kcps,
                                 offset_mm);

  bool crPending = false;
  long sigma = readVL53L4CDRangingValue(serial,
    F("Sigma threshold in mm (0-16383, empty line keeps current):"),
    0, 16383, sigma_mm, crPending);
  long signal = readVL53L4CDRangingValue(serial,
    F("Signal threshold in kcps (0-16384, empty line keeps current):"),
    0, 16384, signal_kcps, crPending);
  long offset = readVL53L4CDRangingValue(serial,
    F("Offset correction in mm (-1024 to 1023, empty line keeps current):"),
    -1024, 1023, offset_mm, crPending);

  if (!sensor.setSigmaThreshold((uint16_t)sigma))
  {
    printVL53L4CDParameterError(serial, sensor, F("Setting sigma threshold"));
    return false;
  }
  if (!sensor.setSignalThreshold((uint16_t)signal))
  {
    printVL53L4CDParameterError(serial, sensor, F("Setting signal threshold"));
    return false;
  }
  if (!sensor.setOffset((int16_t)offset))
  {
    printVL53L4CDParameterError(serial, sensor, F("Setting offset"));
    return false;
  }

  // Read back: the signal threshold is stored in 8 kcps steps.
  if (!readVL53L4CDRangingParameters(serial, sensor, sigma_mm, signal_kcps,
                                     offset_mm))
  {
    return false;
  }
  printVL53L4CDRangingParameters(serial, F("Applied"), sigma_mm, signal_kcps,
                                 offset_mm);
  return true;
}

#endif
