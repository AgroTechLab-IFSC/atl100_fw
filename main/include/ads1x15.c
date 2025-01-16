#include <stdint.h>
#include <stdbool.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include "atl_i2c.h"
#include "include/ads1x15.h"

/* Constants */
static const char *TAG = "ads1x15";                     /**< Module identification. */

/* Global variables */
i2c_master_dev_handle_t ads1x15_i2c_handle;             /**< I2C device handle */

/* Global external variables */
extern i2c_master_bus_handle_t bus_handle;              /**< I2C bus handle */

// #ifndef __AVR__
#define TWI_FREQ 100000
// #else
// #include "utility/twi.h"
// #endif

// Time it takes to write a register in the I2C bus, in microseconds
//  2 x 4 bytes x 9 bits x bit time
//  Bit time = 1000000 us / I2C frequency
#define I2C_WRITE_REGISTER_TIME (2 * 4 * 10 * 1000000 / TWI_FREQ)

static uint16_t ads1x15_getConfig(ads1x15_config_t *config_ptr) {
  
  /* Creates a default register configuration */
  return ADS1015_REG_CONFIG_CPOL_ACTVLOW | // Alert/Rdy active low   (default val)
         ADS1015_REG_CONFIG_CMODE_TRAD   | // Traditional comparator (default val)
         ADS1015_REG_CONFIG_CLAT_NONLAT  | // ALERT/RDY pin in non-latching mode.
         config_ptr->sps_mask            | // Samples per second
                                           // Mode: single shot or continuous
         (config_ptr->continuous ? ADS1015_REG_CONFIG_MODE_CONTIN : ADS1015_REG_CONFIG_MODE_SINGLE) |
                                           // Mode: comparator or not.
         (config_ptr->comparator ? ADS1015_REG_CONFIG_CQUE_1CONV : ADS1015_REG_CONFIG_CQUE_NONE) |
         config_ptr->gain;                           // PGA/voltage range
}

static uint32_t ads1x15_getNextReadingTime(ads1x15_config_t *config_ptr) {
  // Mark the time to read the next sample:
  //  - Continuous mode: current time + time for two conversions + time for I2C communication
  //  - Single-shot mode: current time + time for one conversion + time for I2C communication
  if (config_ptr->continuous) {
    // return micros() + 2 * 1000000L / getSampleRate() + I2C_WRITE_REGISTER_TIME;
    return esp_timer_get_time() + 2 * 1000000L / ads1x15_getSampleRate(config_ptr) + I2C_WRITE_REGISTER_TIME;
  }
  // return micros() + 1000000L / getSampleRate() + I2C_WRITE_REGISTER_TIME;
  return esp_timer_get_time() + 1000000L / ads1x15_getSampleRate(config_ptr) + I2C_WRITE_REGISTER_TIME;
}

/**
 * @brief Create I2C device handle to ADS1X15 sensor
 * @param[in] dev_addr I2C device address of sensor
 * @return
 *     - ESP_OK Success
 *     - Others Fail
 */
esp_err_t ads1x15_create(uint16_t dev_addr) {
    esp_err_t err = ESP_OK;

    /* Create device I2C configuration */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = ATL_I2C_MASTER_FREQ_HZ,
        .flags.disable_ack_check = true,
    };

    /* Create device handle */
    err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &ads1x15_i2c_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail creating I2C device handle!");
    }
    return err;
}

/**
 * @brief Delete and release I2C handle of BH1750 sensor
 * @return
 *     - ESP_OK Success
 *     - Others Fail
 */
esp_err_t ads1x15_delete(void) {
    esp_err_t err = ESP_OK;
    
    err = i2c_master_bus_rm_device(ads1x15_i2c_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail removing I2C device handle!");
    }

    return err;
}

void ads1x15_setGain(ads1x15_config_t *config_ptr, ads1x15_gain_t gain) {
  config_ptr->gain = gain;
  
  if (gain) {
    config_ptr->range = 4.096 / (1 << ((gain >> 9) - 1));
  } else {
    config_ptr->range = 6.144;
  }
}

ads1x15_gain_t ads1x15_getGain(ads1x15_config_t *config_ptr) {
  return config_ptr->gain;
}

float ads1x15_getRange(ads1x15_config_t *config_ptr) {
  return config_ptr->range;
}

void ads1x15_setContinuous(ads1x15_config_t *config_ptr, bool c) {
  config_ptr->continuous = c;
}

bool ads1x15_isContinuous(ads1x15_config_t *config_ptr) {
  return config_ptr->continuous;
}

void ads1x15_setSampleRate(ads1x15_config_t *config_ptr, uint16_t sps) {
  if (sps >= 860) {
    config_ptr->sps_mask = ADS1115_REG_CONFIG_DR_860SPS;
  } else if (sps >= 475) {
    config_ptr->sps_mask = ADS1115_REG_CONFIG_DR_475SPS;
  } else if (sps >= 250) {
    config_ptr->sps_mask = ADS1115_REG_CONFIG_DR_250SPS;
  } else if (sps >= 128) {
    config_ptr->sps_mask = ADS1115_REG_CONFIG_DR_128SPS;
  } else if (sps >= 64) {
    config_ptr->sps_mask = ADS1115_REG_CONFIG_DR_64SPS;
  } else if (sps >= 32) {
    config_ptr->sps_mask = ADS1115_REG_CONFIG_DR_32SPS;
  } else if (sps >= 16) {
    config_ptr->sps_mask = ADS1115_REG_CONFIG_DR_16SPS;
  } else {
    config_ptr->sps_mask = ADS1115_REG_CONFIG_DR_8SPS;
  }
}

uint16_t ads1x15_getSampleRate(ads1x15_config_t *config_ptr) {
  switch (config_ptr->sps_mask) {
    case ADS1115_REG_CONFIG_DR_860SPS:
      return 860;
    case ADS1115_REG_CONFIG_DR_475SPS:
      return 475;
    case ADS1115_REG_CONFIG_DR_250SPS:
      return 250;
    case ADS1115_REG_CONFIG_DR_128SPS:
      return 128;
    case ADS1115_REG_CONFIG_DR_64SPS:
      return 64;
    case ADS1115_REG_CONFIG_DR_32SPS:
      return 32;
    case ADS1115_REG_CONFIG_DR_16SPS:
      return 16;
    default:
      return 8;
  }
}

int16_t ads1x15_readADC_SingleEnded(ads1x15_config_t *config_ptr, uint8_t channel) {
  uint16_t config = ads1x15_getConfig(config_ptr);

  if (channel > 3) {
    return 0;
  }
  
  // Set single-ended input channel
  switch (channel) {
    case (0):
      config |= ADS1015_REG_CONFIG_MUX_SINGLE_0;
      break;
    case (1):
      config |= ADS1015_REG_CONFIG_MUX_SINGLE_1;
      break;
    case (2):
      config |= ADS1015_REG_CONFIG_MUX_SINGLE_2;
      break;
    case (3):
      config |= ADS1015_REG_CONFIG_MUX_SINGLE_3;
      break;
  }

  // Set 'start single-conversion' bit
  config |= ADS1015_REG_CONFIG_OS_SINGLE;

  // Write config register to the ADC
  ads1x15_writeRegister(config_ptr, ADS1015_REG_POINTER_CONFIG, config);
  
  // Update next reading time
  config_ptr->next_reading_time = ads1x15_getNextReadingTime(config_ptr);
  
  // If in continuous mode, return immediately, otherwise wait for conversion
  return config_ptr->continuous ? 0 : ads1x15_readNext(config_ptr);
}

int16_t ads1x15_readADC_Differential_0_1(ads1x15_config_t *config_ptr) {
  uint16_t config = ads1x15_getConfig(config_ptr);
                    
  // Set channels
  config |= ADS1015_REG_CONFIG_MUX_DIFF_0_1;  // AIN0 = P, AIN1 = N

  // Set 'start single-conversion' bit
  config |= ADS1015_REG_CONFIG_OS_SINGLE;

  // Write config register to the ADC
  ads1x15_writeRegister(config_ptr, ADS1015_REG_POINTER_CONFIG, config);

  // Update next reading time
  config_ptr->next_reading_time = ads1x15_getNextReadingTime(config_ptr);
  
  // If in continuous mode, return immediately, otherwise wait for conversion
  return config_ptr->continuous ? 0 : ads1x15_readNext(config_ptr);
}


int16_t ads1x15_readADC_Differential_2_3(ads1x15_config_t *config_ptr) {
  uint16_t config = ads1x15_getConfig(config_ptr);

  // Set channels
  config |= ADS1015_REG_CONFIG_MUX_DIFF_2_3;  // AIN2 = P, AIN3 = N

  // Set 'start single-conversion' bit
  config |= ADS1015_REG_CONFIG_OS_SINGLE;

  // Write config register to the ADC
  ads1x15_writeRegister(config_ptr, ADS1015_REG_POINTER_CONFIG, config);
  
  // Update next reading time
  config_ptr->next_reading_time = ads1x15_getNextReadingTime(config_ptr);
  
  // If in continuous mode, return immediately, otherwise wait for conversion
  return config_ptr->continuous ? 0 : ads1x15_readNext(config_ptr);
}

void ads1x15_setComparator(ads1x15_config_t *config_ptr, uint8_t channel, int16_t highThreshold, int16_t lowThreshold) {
  ads1x15_startComparator_SingleEnded(config_ptr, channel, highThreshold, lowThreshold);
}

void ads1x15_setNotComparator(ads1x15_config_t *config_ptr) {
  config_ptr->comparator = false;
}

bool ads1x15_isComparator(ads1x15_config_t *config_ptr) {
  return config_ptr->comparator;
}

void ads1x15_startComparator_SingleEnded(ads1x15_config_t *config_ptr, uint8_t channel, int16_t highThreshold, int16_t lowThreshold) {
  // Comparator must be used in continuous mode
  config_ptr->comparator = true;
  config_ptr->continuous = true;

  uint16_t config = ads1x15_getConfig(config_ptr);
                    
  // Set single-ended input channel
  switch (channel) {
    case (0):
      config |= ADS1015_REG_CONFIG_MUX_SINGLE_0;
      break;
    case (1):
      config |= ADS1015_REG_CONFIG_MUX_SINGLE_1;
      break;
    case (2):
      config |= ADS1015_REG_CONFIG_MUX_SINGLE_2;
      break;
    case (3):
      config |= ADS1015_REG_CONFIG_MUX_SINGLE_3;
      break;
  }

  // Set the high threshold register
  // Shift 12-bit results left 4 bits for the ADS1015
  ads1x15_writeRegister(config_ptr, ADS1015_REG_POINTER_HITHRESH, highThreshold);

  // Set the low threshold register
  // Shift 12-bit results left 4 bits for the ADS1015
  ads1x15_writeRegister(config_ptr, ADS1015_REG_POINTER_LOWTHRESH, lowThreshold);  

  // Write config register to the ADC
  ads1x15_writeRegister(config_ptr, ADS1015_REG_POINTER_CONFIG, config);

  // Update next reading time
  config_ptr->next_reading_time = ads1x15_getNextReadingTime(config_ptr);
}

int16_t ads1x15_getLastConversionResults(ads1x15_config_t *config_ptr) {
  return ads1x15_readNext(config_ptr);
}

int16_t ads1x15_readNext(ads1x15_config_t *config_ptr) {
  
  /* Wait until end of conversion */
  while (!ads1x15_conversionDone(config_ptr));

  /* Mark the time to read the next sample */
  config_ptr->next_reading_time += 1000000L / ads1x15_getSampleRate(config_ptr);

  /* Read the conversion results */
  uint16_t res = ads1x15_readRegister(config_ptr, ADS1015_REG_POINTER_CONVERT);
  return (int16_t)res;
}

bool ads1x15_conversionDone(ads1x15_config_t *config_ptr) {
  // return micros() >= m_nextReadingTime;
  return esp_timer_get_time() >= config_ptr->next_reading_time;
}

void ads1x15_writeRegister(ads1x15_config_t *config_ptr, uint8_t reg, uint16_t value) {
  uint16_t data = value;
//   atl_i2c_write_register(config_ptr->i2c_addr, reg, (uint8_t *)&data, sizeof(data));
}

uint16_t ads1x15_readRegister(ads1x15_config_t *config_ptr, uint8_t reg) {
  uint16_t data = 0;
//   atl_i2c_read_register(config_ptr->i2c_addr, reg, (uint8_t *)&data, sizeof(data));
  return data;
}

float ads1x15_read4to20mA(ads1x15_config_t *config_ptr, uint8_t channel) {
  return 1000 * ads1x15_readVoltage(config_ptr, channel) / 100;
}

float ads1x15_readVoltage(ads1x15_config_t *config_ptr, uint8_t channel) {
  return (config_ptr->continuous ? ads1x15_readNext(config_ptr) : ads1x15_readADC_SingleEnded(config_ptr, channel)) * config_ptr->range / 32767;
}

float ads1x15_readDifferentialVoltage01(ads1x15_config_t *config_ptr) {
  return (config_ptr->continuous ? ads1x15_readNext(config_ptr) : ads1x15_readADC_Differential_0_1(config_ptr)) * config_ptr->range / 32767;
}

float ads1x15_readDifferentialVoltage23(ads1x15_config_t *config_ptr) {
  return (config_ptr->continuous ? ads1x15_readNext(config_ptr) : ads1x15_readADC_Differential_2_3(config_ptr)) * config_ptr->range / 32767;
}