#ifndef VL53L4CD_h
#define VL53L4CD_h

#include <Arduino.h>
#include <Wire.h>

class VL53L4CD
{
  public:
    enum regAddr : uint16_t
    {
      SOFT_RESET                                 = 0x0000,
      I2C_SLAVE_DEVICE_ADDRESS                    = 0x0001,
      OSC_FREQUENCY                              = 0x0006,
      VHV_CONFIG_TIMEOUT_MACROP_LOOP_BOUND        = 0x0008,
      ALGO_PART_TO_PART_RANGE_OFFSET_MM           = 0x001E,
      GPIO_HV_MUX_CTRL                           = 0x0030,
      GPIO_HV_MUX_ACTIVE_HIGH                    = GPIO_HV_MUX_CTRL,
      GPIO_TIO_HV_STATUS                         = 0x0031,
      SYSTEM_INTERRUPT_CONFIG_GPIO               = 0x0046,
      RANGE_CONFIG_A                             = 0x005E,
      RANGE_CONFIG_B                             = 0x0061,
      FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN_LIMIT = 0x0066,
      SYSTEM_INTERMEASUREMENT_PERIOD             = 0x006C,
      SYSTEM_THRESH_HIGH                         = 0x0072,
      SYSTEM_THRESH_LOW                          = 0x0074,
      SYSTEM_INTERRUPT_CLEAR                     = 0x0086,
      SYSRANGE_START                             = 0x0087,
      RESULT_RANGE_STATUS                        = 0x0089,
      RESULT_RANGE_DISTANCE                      = 0x0096,
      OSC_CALIBRATE_VAL                           = 0x00DE,
      FIRMWARE_SYSTEM_STATUS                     = 0x00E5,
      IDENTIFICATION_MODEL_ID                    = 0x010F
    };

    enum Error
    {
      NoError,
      InvalidArgument,
      I2CError,
      Timeout,
      SensorNotFound,
      NotInitialized,
      InvalidState
    };

    uint8_t last_status;

    explicit VL53L4CD(uint8_t address = 0x29);

    void setBus(TwoWire * bus);
    TwoWire * getBus() { return bus; }
    void setAddress(uint8_t new_addr);
    uint8_t getAddress() { return address; }

    bool init();
    bool init(bool io_2v8)
      __attribute__((deprecated("Use init(); the argument does not configure I/O voltage.")));

    void writeReg(uint16_t reg, uint8_t value);
    void writeReg16Bit(uint16_t reg, uint16_t value);
    void writeReg32Bit(uint16_t reg, uint32_t value);
    uint8_t readReg(uint16_t reg);
    uint16_t readReg16Bit(uint16_t reg);
    uint32_t readReg32Bit(uint16_t reg);
    void writeMulti(uint16_t reg, uint8_t const * src, uint8_t count);
    void readMulti(uint16_t reg, uint8_t * dst, uint8_t count);

    bool setSignalRateLimit(float limit_Mcps);
    float getSignalRateLimit();
    bool setMeasurementTimingBudget(uint32_t budget_us);
    uint32_t getMeasurementTimingBudget();

    void startContinuous(uint32_t period_ms = 0);
    void stopContinuous();
    uint16_t readRangeContinuousMillimeters();
    uint16_t readRangeSingleMillimeters();

    void setTimeout(uint16_t timeout) { io_timeout = timeout; }
    uint16_t getTimeout() { return io_timeout; }
    bool timeoutOccurred();
    Error getLastError() const { return last_error; }
    uint8_t getRangeStatus() const { return range_status; }

  private:
    TwoWire * bus;
    uint8_t address;
    uint16_t io_timeout;
    bool did_timeout;
    bool initialized;
    bool ranging;
    Error last_error;
    uint8_t range_status;
    uint32_t measurement_timing_budget_us;
    uint32_t inter_measurement_ms;

    void prepareTransfer();
    bool endTransfer();
    bool writeChunk(uint16_t reg, uint8_t const * src, uint8_t count);
    bool readChunk(uint16_t reg, uint8_t * dst, uint8_t count);
    bool configureTiming(uint32_t budget_us, uint32_t period_ms);
    bool waitForDataReady(uint32_t timeout_ms);
    bool checkTimeout(uint32_t start_ms, uint32_t timeout_ms);
    void stopPreservingError();
};

#endif
