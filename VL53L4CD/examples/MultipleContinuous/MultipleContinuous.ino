/* Buffered acquisition from up to six EZrange VL53L4CD forwarding breakouts.
   Serial Monitor: 115200 baud, Newline or Both NL & CR.
   Addresses must already be unique. Reset to configure another capture. */

#include <Wire.h>
#include <VL53L4CD.h>

const uint8_t MAX_SENSORS = 6;
const uint16_t BUFFER_BYTES = 600;

struct Sample
{
  uint16_t distance;
  uint8_t error;
  uint8_t status;
};
static_assert(sizeof(Sample) == 4, "Sample must occupy four bytes.");

VL53L4CD sensors[MAX_SENSORS];
Sample samples[BUFFER_BYTES / sizeof(Sample)];
uint8_t sensorCount = 0;
uint16_t frameCount = 0;
uint16_t budgetMs = 0;
uint16_t periodMs = 0;
uint32_t acquisitionTimeMs = 0;
bool configured = false;
bool finished = false;

uint16_t readNumber(uint16_t minimum, uint16_t maximum)
{
  uint32_t value = 0;
  bool digits = false;
  bool invalid = false;
  while (true)
  {
    if (!Serial.available()) { continue; }
    char ch = Serial.read();
    if (ch == '\r' || ch == '\n')
    {
      if (!digits && !invalid) { continue; }
      if (!invalid && value >= minimum && value <= maximum)
      {
        return (uint16_t)value;
      }
      Serial.print(F("Error: enter a decimal integer from "));
      Serial.print(minimum);
      Serial.print(F(" to "));
      Serial.println(maximum);
      value = 0;
      digits = false;
      invalid = false;
    }
    else if (ch >= '0' && ch <= '9')
    {
      digits = true;
      if (!invalid)
      {
        value = value * 10 + (ch - '0');
        if (value > maximum) { invalid = true; }
      }
    }
    else { invalid = true; }
  }
}

void printSensorError(uint8_t index)
{
  Serial.print(F("Sensor 0x"));
  Serial.print(sensors[index].getAddress(), HEX);
  Serial.print(F(", error: "));
  Serial.print(sensors[index].getLastError());
  Serial.print(F(", Wire status: "));
  Serial.println(sensors[index].last_status);
}

bool discoverSensors()
{
  Serial.println(F("Scanning 7-bit addresses at 400 kHz..."));
  for (uint8_t address = 0x08; address <= 0x77; address++)
  {
#if defined(WIRE_HAS_TIMEOUT)
    Wire.clearWireTimeoutFlag();
#endif
    Wire.beginTransmission(address);
    uint8_t status = Wire.endTransmission();
#if defined(WIRE_HAS_TIMEOUT)
    if (Wire.getWireTimeoutFlag()) { status = 5; }
#endif
    if (status == 2) { continue; }
    if (status != 0)
    {
      Serial.print(F("Scan aborted at 0x"));
      Serial.print(address, HEX);
      Serial.print(F(", Wire status: "));
      Serial.println(status);
      return false;
    }

    VL53L4CD candidate(address);
    candidate.setTimeout(500);
    uint16_t id = candidate.readReg16Bit(VL53L4CD::IDENTIFICATION_MODEL_ID);
    if (candidate.getLastError() != VL53L4CD::NoError || id != 0xEBAA)
    {
      Serial.print(F("Skipping unverified device at 0x"));
      Serial.print(address, HEX);
      Serial.print(F(", ID: 0x"));
      Serial.print(id, HEX);
      Serial.print(F(", error: "));
      Serial.print(candidate.getLastError());
      Serial.print(F(", Wire status: "));
      Serial.println(candidate.last_status);
      continue;
    }
    if (sensorCount == MAX_SENSORS)
    {
      Serial.println(F("Too many sensors: maximum is 6. Capture cancelled."));
      return false;
    }
    sensors[sensorCount] = candidate;
    Serial.print(F("Sensor found at 0x"));
    Serial.println(address, HEX);
    sensorCount++;
  }
  if (sensorCount == 0)
  {
    Serial.println(F("No verified sensors. Check wiring and reset."));
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
  if (!discoverSensors()) { return; }

  Serial.println(F("Timing budget in ms (10-200; 10 is fastest):"));
  budgetMs = readNumber(10, 200);
  while (true)
  {
    Serial.println(F("Period in ms (0 = fastest, otherwise budget < period <= 5000):"));
    periodMs = readNumber(0, 5000);
    if (periodMs == 0 || periodMs > budgetMs) { break; }
    Serial.println(F("Error: nonzero period must exceed the timing budget."));
  }
  uint16_t maxFrames = (sizeof(samples) / sizeof(samples[0])) / sensorCount;
  Serial.print(F("Number of frames (1-"));
  Serial.print(maxFrames);
  Serial.println(F("):"));
  frameCount = readNumber(1, maxFrames);

  for (uint8_t i = 0; i < sensorCount; i++)
  {
    if (!sensors[i].init() ||
        !sensors[i].setMeasurementTimingBudget((uint32_t)budgetMs * 1000))
    {
      printSensorError(i);
      Serial.println(F("Configuration failed. Reset to retry."));
      return;
    }
  }
  Serial.println(F("Acquiring; UART output resumes after capture."));
  Serial.flush();
  configured = true;
}

void stopSensors(uint8_t count)
{
  for (uint8_t i = 0; i < count; i++)
  {
    sensors[i].stopContinuous();
    if (sensors[i].getLastError() != VL53L4CD::NoError) { printSensorError(i); }
  }
}

void acquireFrames()
{
  uint32_t start = millis();
  uint32_t lastFrame = 0;
  uint32_t due = budgetMs;
  uint16_t cadence = periodMs == 0 ? budgetMs : periodMs;
  for (uint16_t frame = 0; frame < frameCount; frame++)
  {
    while ((uint32_t)(millis() - start) < due) { delay(1); }
    for (uint8_t i = 0; i < sensorCount; i++)
    {
      Sample & sample = samples[frame * sensorCount + i];
      sample.distance = sensors[i].readRangeContinuousMillimeters();
      sample.error = sensors[i].getLastError();
      sample.status = sensors[i].getRangeStatus();
    }
    lastFrame = millis();
    due += cadence;
    // Skip missed host deadlines instead of issuing catch-up frames.
    uint32_t elapsed = lastFrame - start;
    if (elapsed >= due) { due += ((elapsed - due) / cadence + 1) * cadence; }
  }
  acquisitionTimeMs = lastFrame - start;
}

void loop()
{
  if (!configured || finished) { return; }
  finished = true;
  for (uint8_t i = 0; i < sensorCount; i++)
  {
    sensors[i].startContinuous(periodMs);
    if (sensors[i].getLastError() != VL53L4CD::NoError)
    {
      printSensorError(i);
      stopSensors(i);
      Serial.println(F("Start failed. No capture. Reset to retry."));
      return;
    }
  }

  acquireFrames();
  Serial.print(F("Acquisition time, capture start to last completed frame (ms): "));
  Serial.println(acquisitionTimeMs);
  stopSensors(sensorCount);
  Serial.println(F("frame,address_hex,distance_mm,error,range_status"));
  for (uint16_t frame = 0; frame < frameCount; frame++)
  {
    for (uint8_t i = 0; i < sensorCount; i++)
    {
      Sample const & sample = samples[frame * sensorCount + i];
      Serial.print(frame + 1);
      Serial.print(F(",0x"));
      Serial.print(sensors[i].getAddress(), HEX);
      Serial.print(',');
      Serial.print(sample.distance);
      Serial.print(',');
      Serial.print(sample.error);
      Serial.print(',');
      Serial.println(sample.status);
    }
  }
  Serial.println(F("Capture complete. Reset for another capture."));
}
