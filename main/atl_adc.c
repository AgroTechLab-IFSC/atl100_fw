
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2c.h>
#include <esp_log.h>
#include <esp_err.h>
#include <cJSON.h>
#include "atl_config.h"
#include "atl_telemetry.h"
#include "atl_i2c.h"
#include "atl_led.h"
#include "include/ads1x15.h"
#include "atl_mqtt.h"
#include "atl_telemetry.h"

typedef struct {
    uint8_t i2c_address;
    uint8_t i2c_port;
} atl_adc_task_args_t;

/* Constants */
static const char *TAG = "atl-adc";                             /**< Module identification. */

/* Global external variables */
extern atl_config_t atl_config;                                 /**< Global configuration variable. */

/* Global variables */
TaskHandle_t atl_adc_handle[4] = { NULL, NULL, NULL, NULL };    /**< ADC task handle */

/**
 * @fn moving_average(void)
 * @brief Compute moving average value.
 * @return float - moving average value. 
 */
static float moving_average(float *values_vector_ptr, float new_value, uint16_t window_size) {

  float acc = 0.0f;       // accumulator
  
  // Move vector excluding the older value
  for (uint8_t i = window_size - 1; i > 0; i--) {
    *(values_vector_ptr + i) = *(values_vector_ptr + i - 1);
  }

  // Put the last ADC value readed in the first vector position
  *values_vector_ptr = new_value;

  // Sum all vector values
  for (uint8_t i = 0; i < window_size; i++) {
    acc += *(values_vector_ptr + i);
  }

  /* Return moving average value */
  return acc / window_size;
}

/**
 * @brief ADC task
 * 
 * @param args 
 */
void atl_adc_task(void *args) {
    atl_adc_task_args_t task_args = *((atl_adc_task_args_t *)args);       
    float values_sampled[atl_config.telemetry.adc[task_args.i2c_port].sampling_period];    
    atl_data_adc_telemetry_t adc_values;

    /* Set default ADS1X15 configuration */
    ads1x15_config_t ads1x15_config;
    ads1x15_config.i2c_addr = task_args.i2c_address;
    ads1x15_config.gain = GAIN_TWOTHIRDS;
    ads1x15_config.range = 6.144;
    ads1x15_config.sps_mask = ADS1115_REG_CONFIG_DR_128SPS;
    ads1x15_config.next_reading_time = 0;
    ads1x15_config.continuous = false;
    ads1x15_config.comparator = false;

    /* If analog input in 4-20mA mode, adjust gain to two (2.048V range) to get maximum resolution for 4-20mA range */
    if (atl_config.telemetry.adc[task_args.i2c_port].mode == ATL_ADC_INPUT_4_20_MA) {        
        ads1x15_setGain(&ads1x15_config, GAIN_TWO);
    }   

    /* Reset telemetry struct values */
    adc_values.last_sample = 0.0f;
    adc_values.avg_sample = 0.0f;
    adc_values.min_sample = 0.0f;
    adc_values.max_sample = 0.0f; 

    /* Task looping */
    while (true) {
        // ESP_LOGI(TAG, "Running ADC task (addr: 0x%02x - port %d)", task_args.i2c_address, task_args.i2c_port);

        /* Get current sample value */        
        if (atl_config.telemetry.adc[task_args.i2c_port].mode == ATL_ADC_INPUT_4_20_MA) {
            adc_values.last_sample = ads1x15_read4to20mA(&ads1x15_config, task_args.i2c_port);            
        } else if (atl_config.telemetry.adc[task_args.i2c_port].mode == ATL_ADC_INPUT_0_10_V) {
            adc_values.last_sample = ads1x15_readVoltage(&ads1x15_config, task_args.i2c_port);            
        }        

        /* Compute moving average value */
        adc_values.avg_sample = moving_average(values_sampled, adc_values.last_sample, 
                                            atl_config.telemetry.adc[task_args.i2c_port].sampling_window);
    
        /* Update minimum sample value */
        if (adc_values.last_sample < adc_values.min_sample) {
            adc_values.min_sample = adc_values.last_sample;
        }

        /* Update maximum sample value */
        if (adc_values.last_sample > adc_values.max_sample) {
            adc_values.max_sample = adc_values.last_sample;
        }

        /* Update current analog input port telemetry values */
        atl_telemetry_set_adc_data(task_args.i2c_port, adc_values);

        /* Assessment period */
        vTaskDelay(pdMS_TO_TICKS(atl_config.telemetry.adc[task_args.i2c_port].sampling_period * 1000));
    }
}

/**
 * @fn atl_adc_init(void)
 * @brief Initialize Analog Input interface.
 * @details Initialize Analog Input interface.
 * @return esp_err_t - If ESP_OK success. 
 */
esp_err_t atl_adc_init(void) {            
    esp_err_t err = ESP_OK;
    atl_adc_task_args_t task_args;

    ESP_LOGI(TAG, "Starting ADC inputs!");

    /* Update device address and create a task to each analog port enabled */            
    for (uint8_t i = 0; i < 4; i++) {
        if (atl_config.telemetry.adc[i].mode != ATL_ADC_DISABLED) {
            ESP_LOGI(TAG, "Creating ADC task (address 0x%02x) to port %u", CONFIG_ATL_ADC_I2C_ADDR, i);
            task_args.i2c_address = CONFIG_ATL_ADC_I2C_ADDR;
            task_args.i2c_port = i;
            xTaskCreate(atl_adc_task, "atl_adc_task", 4096, &task_args, 10, &atl_adc_handle[i]);
        }
    }            
    
    return err;   
}