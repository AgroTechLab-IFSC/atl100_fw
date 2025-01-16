#include <driver/gpio.h>
#include <driver/pulse_cnt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_err.h>
#include <esp_log.h>
#include <cJSON.h>
#include <sdkconfig.h>
#include <dht.h>
// #include <bme280.h>
// #include <bh1750.h>
#include "atl_telemetry.h"
#include "atl_config.h"
#include "atl_modbus.h"
#include "atl_i2c.h"
#include "atl_adc.h"

/* Constants */
static const char *TAG = "atl-telemetry";           /**< Module identification. */

/* Global variables */
SemaphoreHandle_t atl_telemetry_data_mutex;         /**< Telemetry data mutex */
atl_data_telemetry_t atl_telemetry;                 /**< Telemetry data */
atl_data_telemetry_t atl_gw_telemetry;              /**< Gateway telemetry data */
TaskHandle_t atl_pwr_handle = NULL;                 /**< Power task handle */
TaskHandle_t atl_bme280_handle = NULL;              /**< BME280 task handle */
TaskHandle_t atl_dht_handle = NULL;                 /**< DHT task handle */
TaskHandle_t atl_uv_handle = NULL;                  /**< UV task handle */
TaskHandle_t atl_light_handle = NULL;               /**< Light task handle */
TaskHandle_t atl_anem_handle = NULL;                /**< Anemometer task handle */
TaskHandle_t atl_pluv_handle = NULL;                /**< Pluviometer task handle */
TaskHandle_t atl_ws_handle = NULL;                  /**< Windsock task handle */
TaskHandle_t atl_soil_handle = NULL;                /**< Soil task handle */
TaskHandle_t atl_telemetry_handle = NULL;           /**< Telemetry task handle */
adc_oneshot_unit_handle_t adc1_handle;              /**< ADC1 handle */
adc_cali_handle_t adc1_cali_chan3_handle = NULL;    /**< ADC1 channel 3 calibration handle */
adc_cali_handle_t adc1_cali_chan4_handle = NULL;    /**< ADC1 channel 4 calibration handle */
adc_cali_handle_t adc1_cali_chan5_handle = NULL;    /**< ADC1 channel 5 calibration handle */
bool do_calibration1_chan3 = false;                 /**< Do calibration flag for ADC1 channel 3 */
bool do_calibration1_chan4 = false;                 /**< Do calibration flag for ADC1 channel 4 */
bool do_calibration1_chan5 = false;                 /**< Do calibration flag for ADC1 channel 4 */

/* Global external variables */
extern atl_config_t atl_config;
extern i2c_master_bus_handle_t bus_handle;
extern void* mb_rs485_master_handler;               /**< Modbus Master RS-485 handler */

/**
 * @brief ADC calibration initialization
 * @param unit 
 * @param channel 
 * @param atten 
 * @param out_handle 
 * @return true 
 * @return false 
 */
static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle) {
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

/**
 * @fn atl_dht_task(void *args)
 * @brief DHT task
 * @param [in] args - Pointer to task arguments 
*/
static void atl_dht_task(void *args) {
    uint32_t sampling_period = 0;
    float air_temperature = 0.0f;
    float air_humidity = 0.0f;

    /* DHT sensor configuration */
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 1ULL << CONFIG_ATL_DHT_GPIO;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    /* Task looping */
    while (true) {

        /* Toogle period first for stabilization of DHT22 sensor */
        vTaskDelay(pdMS_TO_TICKS(sampling_period));

        /* Get DHT values */
        if (dht_read_float_data(DHT_TYPE_AM2301, CONFIG_ATL_DHT_GPIO, &air_humidity, &air_temperature) == ESP_OK) {
            ESP_LOGI(TAG, "Temperature: %.2f oC, Humidity: %.2f %%", atl_telemetry.air_temperature, atl_telemetry.air_humidity);
            
            /* Set telemetry data */
            if (xSemaphoreTake(atl_telemetry_data_mutex, portMAX_DELAY) == pdTRUE) {
                atl_telemetry.air_temperature = air_temperature;
                atl_telemetry.air_humidity = air_humidity;
                xSemaphoreGive(atl_telemetry_data_mutex);
            } else {
                ESP_LOGW(TAG, "[%s:%d] Error setting telemetry data! Semaphore fail!", __func__, __LINE__);
            }
        } else {
            ESP_LOGW(TAG, "Fail reading DHT sensor!");
        }

        /* Get telemetry configuration */
        if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
            sampling_period = atl_config.telemetry.dht.sampling_period * 1000;
            xSemaphoreGive(atl_config_mutex);
        } else {
            sampling_period = CONFIG_ATL_DHT_SAMPLING_PERIOD * 1000;
            ESP_LOGE(TAG, "[%s:%d] Error getting DHT sampling period! Semaphore fail!", __func__, __LINE__);
        }
        
    }  
}

/**
 * @fn atl_be280_task(void *args)
 * @brief BME280 task
 * @param [in] args - Pointer to task arguments 
*/
static void atl_bme280_task(void *args) {
    // esp_err_t err = ESP_OK;
    uint32_t sampling_period = 0;
    // float air_temperature = 0.0f;
    // float air_humidity = 0.0f;
    // float air_pressure = 0.0f;

    /* Initialize BME280 */  

        /* Initialize BH1750 */
        // err = bh1750_create(CONFIG_ATL_LIGHT_I2C_ADDR, &bh1750_i2c_handle);
        // if (err != ESP_OK) {
        //     ESP_LOGE(TAG, "[%s:%d] Error creating BH1750 sensor!", __func__, __LINE__);
        //     vTaskSuspend(NULL);
        // } else {
        //     /* Define sensor at Continuous Mode High Resolution */
        //     if (bh1750_set_measure_mode(&bh1750_i2c_handle, BH1750_CONTINUE_1LX_RES) != ESP_OK) {
        //         ESP_LOGE(TAG, "[%s:%d] Error setting BH1750 sensor mode!", __func__, __LINE__);
        //         vTaskSuspend(NULL);
        //     }
        //     /* Wait for the maximum waiting time for the first measurement of BH1750 */
        //     vTaskDelay(pdMS_TO_TICKS(200));
        // }

    /* Task looping */
    while (true) {

        /* Get Light values */
        // if (bh1750_get_data(&bh1750_i2c_handle, &light_intensity) != ESP_OK) {
        //     ESP_LOGE(TAG, "[%s:%d] Error reading BH1750 sensor!", __func__, __LINE__);
        // } else {
        //     ESP_LOGI(TAG, "[%s] Light intensity: %.2f lux", __func__, light_intensity);
            
        //     /* Set telemetry data */
        //     if (xSemaphoreTake(atl_telemetry_data_mutex, portMAX_DELAY) == pdTRUE) {
        //         atl_telemetry.light_intensity = light_intensity;
        //         xSemaphoreGive(atl_telemetry_data_mutex);
        //     } else {
        //         ESP_LOGW(TAG, "[%s:%d] Error setting telemetry data! Semaphore fail!", __func__, __LINE__);
        //     }
        // }

        /* Get telemetry configuration */
        if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
            sampling_period = atl_config.telemetry.bme280.sampling_period * 1000;
            xSemaphoreGive(atl_config_mutex);
        } else {
            sampling_period = CONFIG_ATL_BME280_SAMPLING_PERIOD * 1000;
            ESP_LOGE(TAG, "[%s:%d] Error getting BME280 sampling period! Semaphore fail!", __func__, __LINE__);
        }

        /* Toogle period */
        vTaskDelay(pdMS_TO_TICKS(sampling_period));
    }  
    
}

/**
 * @fn atl_pwr_task(void *args)
 * @brief Power task
 * @param [in] args - Pointer to task arguments 
*/
static void atl_pwr_task(void *args) {
    esp_err_t err = ESP_OK;
    uint32_t sampling_period = 0;
    int adc_reading = 0;
    int adc_voltage = 0;

    /* Task looping */
    while (true) {

        /* Get power values */
        err = adc_oneshot_read(adc1_handle, ADC_CHANNEL_3, &adc_reading);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Fail reading ADC 1 channel 3!");
        } else {
            ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, ADC_CHANNEL_3, adc_reading);
            if (do_calibration1_chan3) {
                err = adc_cali_raw_to_voltage(adc1_cali_chan3_handle, adc_reading, &adc_voltage);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Fail converting raw data to voltage at ADC%d Channel[%d]! Error: %s", 
                        ADC_UNIT_1 + 1, ADC_CHANNEL_3, esp_err_to_name(err));
                } else {
                    ESP_LOGI(TAG, "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT_1 + 1, ADC_CHANNEL_3, adc_voltage);
                    atl_telemetry.power_voltage = (float)((adc_voltage * 5.0)/1000);
                    ESP_LOGI(TAG, "Power: %.2fV", atl_telemetry.power_voltage);
                }
            }
        }
        
        /* Get telemetry configuration */
        if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
            sampling_period = atl_config.telemetry.power.sampling_period * 1000;
            xSemaphoreGive(atl_config_mutex);
        } else {
            sampling_period = CONFIG_ATL_PWR_SAMPLING_PERIOD * 1000;
            ESP_LOGE(TAG, "[%s:%d] Error getting PWR sampling period! Semaphore fail!", __func__, __LINE__);
        }

        /* Toogle period */
        vTaskDelay(pdMS_TO_TICKS(sampling_period));
    }  
}

/**
 * @fn atl_uv_task(void *args)
 * @brief UV task
 * @param [in] args - Pointer to task arguments 
*/
static void atl_uv_task(void *args) {
    esp_err_t err = ESP_OK;
    uint32_t sampling_period = 0;
    int adc_reading = 0;
    int adc_voltage = 0;

    /* Task looping */
    while (true) {

        /* Get power values */
        err = adc_oneshot_read(adc1_handle, ADC_CHANNEL_4, &adc_reading);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Fail reading ADC 1 channel 4!");
        } else {
            ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, ADC_CHANNEL_4, adc_reading);
            if (do_calibration1_chan4) {
                err = adc_cali_raw_to_voltage(adc1_cali_chan4_handle, adc_reading, &adc_voltage);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Fail converting raw data to voltage at ADC%d Channel[%d]! Error: %s", 
                        ADC_UNIT_1 + 1, ADC_CHANNEL_4, esp_err_to_name(err));
                } else {
                    ESP_LOGI(TAG, "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT_1 + 1, ADC_CHANNEL_4, adc_voltage);
                    if (adc_voltage > 0 && adc_voltage <= 227) {
                        atl_telemetry.uv_index = 0;
                    } else if (adc_voltage > 227 && adc_voltage <= 318) {
                        atl_telemetry.uv_index = 1;
                    } else if (adc_voltage > 318 && adc_voltage <= 408) {
                        atl_telemetry.uv_index = 2;
                    } else if (adc_voltage > 408 && adc_voltage <= 503) {
                        atl_telemetry.uv_index = 3;
                    } else if (adc_voltage > 503 && adc_voltage <= 606) {
                        atl_telemetry.uv_index = 4;
                    } else if (adc_voltage > 606 && adc_voltage <= 696) {
                        atl_telemetry.uv_index = 5;
                    } else if (adc_voltage > 696 && adc_voltage <= 795) {
                        atl_telemetry.uv_index = 6;
                    } else if (adc_voltage > 795 && adc_voltage <= 881) {
                        atl_telemetry.uv_index = 7;
                    } else if (adc_voltage > 881 && adc_voltage <= 976) {
                        atl_telemetry.uv_index = 8;
                    } else if (adc_voltage > 976 && adc_voltage <= 1079) {
                        atl_telemetry.uv_index = 9;
                    } else if (adc_voltage > 1079 && adc_voltage <= 1170) {
                        atl_telemetry.uv_index = 10;
                    } else if (adc_voltage > 1170) {
                        atl_telemetry.uv_index = 11;
                    }
                    ESP_LOGI(TAG, "UV index: %u", atl_telemetry.uv_index);
                }
            }
        }
        
        /* Get telemetry configuration */
        if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
            sampling_period = atl_config.telemetry.power.sampling_period * 1000;
            xSemaphoreGive(atl_config_mutex);
        } else {
            sampling_period = CONFIG_ATL_PWR_SAMPLING_PERIOD * 1000;
            ESP_LOGE(TAG, "[%s:%d] Error getting PWR sampling period! Semaphore fail!", __func__, __LINE__);
        }

        /* Toogle period */
        vTaskDelay(pdMS_TO_TICKS(sampling_period));
    }   
}

/**
 * @fn atl_anem_task(void *args)
 * @brief Anemometer task
 * @param [in] args - Pointer to task arguments 
*/
static void atl_anem_task(void *args) {
    esp_err_t err = ESP_OK;
    uint32_t sampling_period = 0;    
    uint32_t now = 0;
    uint32_t interval = 0;
    uint32_t lastWindSampling = 0;
    float RPM = 0.0f;
    int pulse_count = 0;
    const float pi = 3.1415926;             /**< PI used in anemometer computation. */
    const float anemometer_radius = 0.147;  /**< Anemometer radius (in m). */

    ESP_LOGI(TAG, "Install PCNT unit at anemometer task!");
    pcnt_unit_config_t unit_config = {
        .high_limit = 1000,
        .low_limit = -1000,
    };
    pcnt_unit_handle_t pcnt_unit = NULL;
    err = pcnt_new_unit(&unit_config, &pcnt_unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error creating PCNT unit! Error: %d (%s)", err, esp_err_to_name(err));
        vTaskSuspend(NULL);
    }

    /* Set glitch filter */
    ESP_LOGI(TAG, "Set glitch filter at anemometer task!");
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    err = pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error setting glitch filter! Error: %d (%s)", err, esp_err_to_name(err));
        vTaskSuspend(NULL);
    }

    /* Install PCNT channels */
    ESP_LOGI(TAG, "Installing PCNT channel at anemometer task!");
    pcnt_chan_config_t chan_config = {
        .edge_gpio_num = CONFIG_ATL_ANEMOMETER_GPIO,
        .level_gpio_num = -1,
    };
    pcnt_channel_handle_t pcnt_chan = NULL;
    err = pcnt_new_channel(pcnt_unit, &chan_config, &pcnt_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error installing PCNT channel! Error: %d (%s)", err, esp_err_to_name(err));
        vTaskSuspend(NULL);
    }
    
    /* Set edge action */
    ESP_LOGI(TAG, "Setting edge action for PCNT channel at anemometer task!");
    err = pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error setting edge action! Error: %d (%s)", err, esp_err_to_name(err));
        vTaskSuspend(NULL);
    }    

    /* Enable PCNT unit */    
    ESP_LOGI(TAG, "Enabling PCNT unit at anemometer task!");
    err = pcnt_unit_enable(pcnt_unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error enabling PCNT unit! Error: %d (%s)", err, esp_err_to_name(err));
        vTaskSuspend(NULL);
    }

    /* Clear count at PCNT unit */
    ESP_LOGI(TAG, "Clear PCNT unit at anemometer task!");
    err = pcnt_unit_clear_count(pcnt_unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error clearing PCNT unit! Error: %d (%s)", err, esp_err_to_name(err));
        vTaskSuspend(NULL);
    }

    /* Start PCNT unit */
    ESP_LOGI(TAG, "Starting PCNT unit at anemometer task!");
    err = pcnt_unit_start(pcnt_unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error starting PCNT unit! Error: %d (%s)", err, esp_err_to_name(err));
        vTaskSuspend(NULL);
    }

    /* Task looping */
    while (true) {

        /* Get telemetry configuration */
        if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
            sampling_period = atl_config.telemetry.anemometer.sampling_period * 1000;
            xSemaphoreGive(atl_config_mutex);
        } else {
            sampling_period = CONFIG_ATL_ANEMOMETER_SAMPLING_PERIOD * 1000;
            ESP_LOGE(TAG, "[%s:%d] Error getting anemometer sampling period! Semaphore fail!", __func__, __LINE__);
        }

        /* Toogle period */
        vTaskDelay(pdMS_TO_TICKS(sampling_period));

        /* Get timestamp now */
        now = pdTICKS_TO_MS(xTaskGetTickCount());

        /* Compute sampling interval - check turn around time (~ after 50 days) */        
        if (now > lastWindSampling) {
            interval = now - lastWindSampling;
        } else {        
            interval = UINT32_MAX - sampling_period + now;
        }
        
        /* Update last sampling */
        lastWindSampling = now;    

        /* Get pulses count */
        err = pcnt_unit_get_count(pcnt_unit, &pulse_count);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error getting PCNT count! Error: %d (%s)", err, esp_err_to_name(err));            
        } else {
            /* Clear count at PCNT unit */
            ESP_LOGI(TAG, "Clear PCNT unit at anemometer task!");
            err = pcnt_unit_clear_count(pcnt_unit);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error clearing PCNT unit! Error: %d (%s)", err, esp_err_to_name(err));                
            }

            /* Compute RPM (Revolution Per Minute) */
            RPM = (pulse_count * 60) / (interval / 1000);
    
            /* Compute wind speed (in Km/h) */
            atl_telemetry.anemometer = 2 * 3.6 * pi * anemometer_radius * RPM / 60;
        }
    }   
}

/**
 * @fn atl_pluv_task(void *args)
 * @brief Pluviometer task
 * @param [in] args - Pointer to task arguments 
*/
static void atl_pluv_task(void *args) {
    esp_err_t err = ESP_OK;
    uint32_t sampling_period = 0;           
    int pulse_count = 0;
    const uint16_t pluv_area = 7850;     /**< Pluviometer area (in mm2). */ 

    ESP_LOGI(TAG, "Install PCNT unit at pluviometer task!");
    pcnt_unit_config_t unit_config = {
        .high_limit = 1000,
        .low_limit = -1000,
    };
    pcnt_unit_handle_t pcnt_unit = NULL;
    err = pcnt_new_unit(&unit_config, &pcnt_unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error creating PCNT unit! Error: %d (%s)", err, esp_err_to_name(err));
        vTaskSuspend(NULL);
    }

    /* Set glitch filter */
    ESP_LOGI(TAG, "Set glitch filter at pluviometer task!");
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    err = pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error setting glitch filter! Error: %d (%s)", err, esp_err_to_name(err));
        vTaskSuspend(NULL);
    }

    /* Install PCNT channels */
    ESP_LOGI(TAG, "Installing PCNT channel at pluviometer task!");
    pcnt_chan_config_t chan_config = {
        .edge_gpio_num = CONFIG_ATL_PLUVIOMETER_GPIO,
        .level_gpio_num = -1,
    };
    pcnt_channel_handle_t pcnt_chan = NULL;
    err = pcnt_new_channel(pcnt_unit, &chan_config, &pcnt_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error installing PCNT channel! Error: %d (%s)", err, esp_err_to_name(err));
        vTaskSuspend(NULL);
    }
    
    /* Set edge action */
    ESP_LOGI(TAG, "Setting edge action for PCNT channel at pluviometer task!");
    err = pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error setting edge action! Error: %d (%s)", err, esp_err_to_name(err));
        vTaskSuspend(NULL);
    }    

    /* Enable PCNT unit */    
    ESP_LOGI(TAG, "Enabling PCNT unit at pluviometer task!");
    err = pcnt_unit_enable(pcnt_unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error enabling PCNT unit! Error: %d (%s)", err, esp_err_to_name(err));
        vTaskSuspend(NULL);
    }

    /* Clear count at PCNT unit */
    ESP_LOGI(TAG, "Clear PCNT unit at pluviometer task!");
    err = pcnt_unit_clear_count(pcnt_unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error clearing PCNT unit! Error: %d (%s)", err, esp_err_to_name(err));
        vTaskSuspend(NULL);
    }

    /* Start PCNT unit */
    ESP_LOGI(TAG, "Starting PCNT unit at pluviometer task!");
    err = pcnt_unit_start(pcnt_unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error starting PCNT unit! Error: %d (%s)", err, esp_err_to_name(err));
        vTaskSuspend(NULL);
    }

    /* Task looping */
    while (true) {

        /* Get telemetry configuration */
        if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
            sampling_period = atl_config.telemetry.anemometer.sampling_period * 1000;
            xSemaphoreGive(atl_config_mutex);
        } else {
            sampling_period = CONFIG_ATL_ANEMOMETER_SAMPLING_PERIOD * 1000;
            ESP_LOGE(TAG, "[%s:%d] Error getting pluviometer sampling period! Semaphore fail!", __func__, __LINE__);
        }

        /* Toogle period */
        vTaskDelay(pdMS_TO_TICKS(sampling_period));          

        /* Get pulses count */
        err = pcnt_unit_get_count(pcnt_unit, &pulse_count);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error getting PCNT count! Error: %d (%s)", err, esp_err_to_name(err));            
        } else {
            /* Clear count at PCNT unit */
            ESP_LOGI(TAG, "Clear PCNT unit at pluviometer task!");
            err = pcnt_unit_clear_count(pcnt_unit);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error clearing PCNT unit! Error: %d (%s)", err, esp_err_to_name(err));                
            }
    
            /**
             * Compute precipitation (P = V / A)
             *    P - precipitation (in mm)
             *    V - collected volume (in mm3)
             *    A - pluviometer area (in mm2)
             */
            atl_telemetry.pluviometer = ((pulse_count * 25.4) * 1000) / pluv_area;
        }
    }   
}

/**
 * @fn atl_ws_task(void *args)
 * @brief Windsock task
 * @param [in] args - Pointer to task arguments 
*/
static void atl_ws_task(void *args) {
    esp_err_t err = ESP_OK;
    uint32_t sampling_period = 0;
    int adc_reading = 0;
    int adc_voltage = 0;

    /* Task looping */
    while (true) {

        /* Get power values */
        err = adc_oneshot_read(adc1_handle, ADC_CHANNEL_5, &adc_reading);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Fail reading ADC 1 channel 5!");
        } else {
            ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, ADC_CHANNEL_5, adc_reading);
            if (do_calibration1_chan5) {
                err = adc_cali_raw_to_voltage(adc1_cali_chan5_handle, adc_reading, &adc_voltage);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Fail converting raw data to voltage at ADC%d Channel[%d]! Error: %s", 
                        ADC_UNIT_1 + 1, ADC_CHANNEL_5, esp_err_to_name(err));
                } else {
                    ESP_LOGI(TAG, "ADC%d Channel[%d] Cali Voltage: %d mV", ADC_UNIT_1 + 1, ADC_CHANNEL_5, adc_voltage);                    
                    if (adc_voltage <= 270) {
                        atl_telemetry.wind_direction = 315;
                    } else if (adc_voltage <= 320) {
                        atl_telemetry.wind_direction = 270;
                    } else if (adc_voltage <= 380) {
                        atl_telemetry.wind_direction = 225;
                    } else if (adc_voltage <= 450) {
                        atl_telemetry.wind_direction = 180;
                    } else if (adc_voltage <= 570) {
                        atl_telemetry.wind_direction = 135;
                    } else if (adc_voltage <= 750) {
                        atl_telemetry.wind_direction = 90;
                    } else if (adc_voltage <= 1250) {
                        atl_telemetry.wind_direction = 45;
                    } else {
                        atl_telemetry.wind_direction = 0;
                    }
                    ESP_LOGI(TAG, "Wind direction: %u", atl_telemetry.wind_direction);
                }
            }
        }
        
        /* Get telemetry configuration */
        if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
            sampling_period = atl_config.telemetry.wind_sock.sampling_period * 1000;
            xSemaphoreGive(atl_config_mutex);
        } else {
            sampling_period = CONFIG_ATL_WIND_SOCK_SAMPLING_PERIOD * 1000;
            ESP_LOGE(TAG, "[%s:%d] Error getting windsock sampling period! Semaphore fail!", __func__, __LINE__);
        }

        /* Toogle period */
        vTaskDelay(pdMS_TO_TICKS(sampling_period));
    }   
}

/**
 * @fn atl_light_task(void *args)
 * @brief Light task
 * @param [in] args - Pointer to task arguments 
*/
static void atl_light_task(void *args) {
    // esp_err_t err = ESP_OK;
    // bh1750_handle_t bh1750_handle = NULL;
    // uint32_t sampling_period = 0;
    // float light_intensity = 0.0f;

    // /* Initialize BH1750 */
    // bh1750_handle = bh1750_create(ATL_I2C_MASTER_NUM, CONFIG_ATL_LIGHT_I2C_ADDR);
    // if (bh1750_handle == NULL) {
        ESP_LOGE(TAG, "[%s:%d] Error creating BH1750 sensor!", __func__, __LINE__);
        vTaskSuspend(NULL);
    // } else {
    //     /* Define sensor at Continuous Mode High Resolution */
    //     if (bh1750_set_measure_mode(&bh1750_handle, BH1750_CONTINUE_1LX_RES) != ESP_OK) {
    //         ESP_LOGE(TAG, "[%s:%d] Error setting BH1750 sensor mode!", __func__, __LINE__);
    //         vTaskSuspend(NULL);
    //     }
    //     /* Wait for the maximum waiting time for the first measurement of BH1750 */
    //     vTaskDelay(pdMS_TO_TICKS(200));
    // }

    // /* Task looping */
    // while (true) {

    //     /* Get Light values */
    //     if (bh1750_get_data(&bh1750_handle, &light_intensity) != ESP_OK) {
    //         ESP_LOGE(TAG, "[%s:%d] Error reading BH1750 sensor!", __func__, __LINE__);
    //     } else {
    //         ESP_LOGI(TAG, "[%s] Light intensity: %.2f lux", __func__, light_intensity);
            
    //         /* Set telemetry data */
    //         if (xSemaphoreTake(atl_telemetry_data_mutex, portMAX_DELAY) == pdTRUE) {
    //             atl_telemetry.light_intensity = light_intensity;
    //             xSemaphoreGive(atl_telemetry_data_mutex);
    //         } else {
    //             ESP_LOGW(TAG, "[%s:%d] Error setting telemetry data! Semaphore fail!", __func__, __LINE__);
    //         }
    //     }

    //     /* Get telemetry configuration */
    //     if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
    //         sampling_period = atl_config.telemetry.light.sampling_period * 1000;
    //         xSemaphoreGive(atl_config_mutex);
    //     } else {
    //         sampling_period = CONFIG_ATL_LIGHT_SAMPLING_PERIOD * 1000;
    //         ESP_LOGE(TAG, "[%s:%d] Error getting Light sampling period! Semaphore fail!", __func__, __LINE__);
    //     }

    //     /* Toogle period */
    //     vTaskDelay(pdMS_TO_TICKS(sampling_period));
    // }  
}

/**
 * @fn atl_soil_task(void *args)
 * @brief Soil task
 * @param [in] args - Pointer to task arguments 
*/
static void atl_soil_task(void *args) {
    esp_err_t err = ESP_OK;
    float value = 0;
    const mb_parameter_descriptor_t* param_descriptor = NULL;

    /* Task looping */
    while (true) {

        /* Read all found CIDs from slave(s) */
        for (uint16_t cid = 0; (err != ESP_ERR_NOT_FOUND) && cid < atl_modbus_get_num_device_params(); cid++) {
            
            /* If the CID value is out of range, run retries */
            for (uint16_t retries = 0; retries < ATL_MB_RS485_MASTER_MAX_RETRIES; retries++) {
                
                /* Get data from parameters description table and use this information to fill the characteristics */
                /* description table and having all required fields in just one table. */
                err = mbc_master_get_cid_info(mb_rs485_master_handler, cid, &param_descriptor);
                if ((err != ESP_ERR_NOT_FOUND) && (param_descriptor != NULL)) {
                
                    void* temp_data_ptr = atl_modbus_master_rs485_get_param_data(param_descriptor);
                    assert(temp_data_ptr);
                    uint8_t type = 0;
                    if ((param_descriptor->param_type == PARAM_TYPE_ASCII) /*&& (param_descriptor->cid == CID_HOLD_TEST_REG)*/) {
                    
                        /* Check for long array of registers of type PARAM_TYPE_ASCII */
                        err = mbc_master_get_parameter(mb_rs485_master_handler, cid, (uint8_t*)temp_data_ptr, &type);
                        if (err == ESP_OK) {
                            ESP_LOGI(TAG, "CID #%u %s (%s) value = (0x%" PRIx32 ") read successful.", param_descriptor->cid, param_descriptor->param_key,
                                param_descriptor->param_units, *(uint32_t*)temp_data_ptr);
                            
                            /* Initialize data of test array and write to slave */
                            if (*(uint32_t*)temp_data_ptr != 0xAAAAAAAA) {
                                memset((void*)temp_data_ptr, 0xAA, param_descriptor->param_size);
                                *(uint32_t*)temp_data_ptr = 0xAAAAAAAA;
                                err = mbc_master_set_parameter(mb_rs485_master_handler, cid, (uint8_t*)temp_data_ptr, &type);
                                if (err == ESP_OK) {
                                    ESP_LOGI(TAG, "CID #%u %s (%s) value = (0x%" PRIx32 "), write successful.", param_descriptor->cid, param_descriptor->param_key,
                                        param_descriptor->param_units, *(uint32_t*)temp_data_ptr);
                                } else {
                                    ESP_LOGE(TAG, "CID #%u (%s) write fail, err = 0x%x (%s).", param_descriptor->cid, param_descriptor->param_key,
                                        (int)err, (char*)esp_err_to_name(err));
                                }
                            }
                        } else {
                            ESP_LOGE(TAG, "CID #%u (%s) read fail, err = 0x%x (%s).", param_descriptor->cid, param_descriptor->param_key,
                                (int)err, (char*)esp_err_to_name(err));
                        }
                    } else {
                        /* Get CID data from Modbus-RTU slave device */
                        err = mbc_master_get_parameter(mb_rs485_master_handler, cid, (uint8_t*)temp_data_ptr, &type);
                        if (err == ESP_OK) {
                            
                            /* Read HOLDING REGISTER (code 0x03 / 16 bits) or INPUT REGISTER (code 0x04 / 16 bits)*/
                            if ((param_descriptor->mb_param_type == MB_PARAM_HOLDING) || (param_descriptor->mb_param_type == MB_PARAM_INPUT)) {
                                value = (float)*(uint32_t*)temp_data_ptr;
                                if (param_descriptor->cid == CID_HOLD_HUMIDITY || param_descriptor->cid == CID_HOLD_TEMPERATURE || param_descriptor->cid == CID_HOLD_PH) {
                                    value /= 10;
                                }
                                ESP_LOGI(TAG, "CID #%u %s (%s) value = %.1f (0x%.8" PRIx32 ") read successful.", param_descriptor->cid, param_descriptor->param_key,
                                    param_descriptor->param_units, value, *(uint32_t*)temp_data_ptr);
                                
                                /* Check if readed values is into the range */
                                if (((value <= param_descriptor->param_opts.max) && (value >= param_descriptor->param_opts.min))) {
                                    
                                    /* Update telemetry data */
                                    switch (param_descriptor->cid) {
                                        case CID_HOLD_HUMIDITY:
                                            atl_telemetry.soil_humidity = value;
                                            break;

                                        case CID_HOLD_TEMPERATURE:
                                            atl_telemetry.soil_temperature = value;
                                            break;
                                        
                                        case CID_HOLD_CONDUCTIVITY:
                                            atl_telemetry.soil_ec = (uint16_t)value;
                                            break;

                                        case CID_HOLD_PH:
                                            atl_telemetry.soil_ph = value;
                                            break;

                                        case CID_HOLD_NITROGEN:
                                            atl_telemetry.soil_nitrogen = (uint16_t)value;
                                            break;

                                        case CID_HOLD_PHOSPHORUS:
                                            atl_telemetry.soil_phosphorus = (uint16_t)value;
                                            break;

                                        case CID_HOLD_POTASSIUM:
                                            atl_telemetry.soil_potassium = (uint16_t)value;
                                            break;
                                        
                                        case CID_HOLD_SALINITY:
                                            atl_telemetry.soil_salinity = (uint16_t)value;
                                            break;

                                        case CID_HOLD_TDS:
                                            atl_telemetry.soil_tds = (uint16_t)value;
                                            break;

                                        default:
                                            break;
                                    }
                                    
                                    /* Clear data structure */
                                    memset((void*)temp_data_ptr, 0, param_descriptor->param_size);

                                    /* Exit from retry loop */
                                    break;
                                }
                            } else {
                                uint8_t state = *(uint8_t*)temp_data_ptr;
                                const char* rw_str = (state & param_descriptor->param_opts.opt1) ? "ON" : "OFF";
                                if ((state & param_descriptor->param_opts.opt2) == param_descriptor->param_opts.opt2) {
                                    ESP_LOGI(TAG, "CID #%u %s (%s) value = %s (0x%" PRIx8 ") read successful.", param_descriptor->cid, param_descriptor->param_key,
                                        param_descriptor->param_units, (const char*)rw_str, *(uint8_t*)temp_data_ptr);
                                } else {
                                    ESP_LOGE(TAG, "CID #%u %s (%s) value = %s (0x%" PRIx8 "), unexpected value.", param_descriptor->cid, param_descriptor->param_key,
                                        param_descriptor->param_units, (const char*)rw_str, *(uint8_t*)temp_data_ptr);
                                    // retry = true;
                                    break;
                                }
                                if (state & param_descriptor->param_opts.opt1) {
                                    // retry = true;
                                    break;
                                }
                            }
                        } else {
                            ESP_LOGE(TAG, "CID #%u (%s) read fail, err = 0x%x (%s).", param_descriptor->cid, param_descriptor->param_key,
                                (int)err, (char*)esp_err_to_name(err));
                        }
                    }
                }

                /* Time between Modbus-RTU retries */
                vTaskDelay(pdMS_TO_TICKS(ATL_MB_RS485_RETRIES_TIMEOUT_MS));
            }

            /* Time between Modbus-RTU polls */
            vTaskDelay(pdMS_TO_TICKS(ATL_MB_RS485_POLL_TIMEOUT_MS));
        }

        /* Toogle period */
        vTaskDelay(pdMS_TO_TICKS(atl_config.telemetry.soil.sampling_period * 1000));
    }  
}

/**
 * @fn atl_telemetry_task(void *args)
 * @brief Telemetry task
 * @param [in] args - Pointer to task arguments 
*/
static void atl_telemetry_task(void *args) {
    uint32_t send_period = 0;
    atl_data_telemetry_t telemetry_data;
    cJSON* root = NULL;
    char json_item_value[100] = "";

    /* Task looping */
    while (true) {

        /* Get telemetry send period configuration */
        if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
            send_period = atl_config.telemetry.send_period * 1000;
            xSemaphoreGive(atl_config_mutex);
        } else {
            send_period = CONFIG_ATL_SEND_PERIOD * 1000;
            ESP_LOGE(TAG, "[%s:%d] Error getting telemetry send period! Semaphore fail!", __func__, __LINE__);
        }

        /* Toogle period */
        vTaskDelay(pdMS_TO_TICKS(send_period));

        /* Create JSON message */
        root = cJSON_CreateObject();
        memset(json_item_value, 0, sizeof(json_item_value));
        memset(&telemetry_data, 0, sizeof(telemetry_data));

        /* Get telemetry data */
        if (atl_telemetry_get_data(&telemetry_data) == ESP_OK) {

            /* Create power telemetry data */
            if (atl_config.telemetry.power.enabled) {
                sprintf(json_item_value, "%.2f", telemetry_data.power_voltage);
                cJSON_AddStringToObject(root, "power_voltage", json_item_value);
            }

            /* Create air telemetry data */
            if (atl_config.telemetry.dht.enabled || atl_config.telemetry.bme280.enabled) {
                sprintf(json_item_value, "%.2f", telemetry_data.air_temperature);
                cJSON_AddStringToObject(root, "air_temperature", json_item_value);
                sprintf(json_item_value, "%.2f", telemetry_data.air_humidity);
                cJSON_AddStringToObject(root, "air_humidity", json_item_value);
            }            
            if (atl_config.telemetry.bme280.enabled) {
                sprintf(json_item_value, "%.2f", telemetry_data.air_pressure);
                cJSON_AddStringToObject(root, "air_pressure", json_item_value);
            }

            /* Create UV telemetry data*/
            if (atl_config.telemetry.uv.enabled) {
                sprintf(json_item_value, "%u", telemetry_data.uv_index);
                cJSON_AddStringToObject(root, "uv_index", json_item_value);
            }

            /* Create light telemetry data */
            if (atl_config.telemetry.light.enabled) {
                sprintf(json_item_value, "%.2f", telemetry_data.light_intensity);
                cJSON_AddStringToObject(root, "light_intensity", json_item_value);
            }
            
            /* Create soil telemetry data */
            if (atl_config.telemetry.soil.enabled) {
                sprintf(json_item_value, "%.1f", telemetry_data.soil_temperature);
                cJSON_AddStringToObject(root, "soil_temperature", json_item_value);
                sprintf(json_item_value, "%.1f", telemetry_data.soil_humidity);
                cJSON_AddStringToObject(root, "soil_humidity", json_item_value);
                sprintf(json_item_value, "%hu", telemetry_data.soil_ec);
                cJSON_AddStringToObject(root, "soil_ec", json_item_value);
                sprintf(json_item_value, "%.1f", telemetry_data.soil_ph);
                cJSON_AddStringToObject(root, "soil_ph", json_item_value);
                sprintf(json_item_value, "%hu", telemetry_data.soil_nitrogen);
                cJSON_AddStringToObject(root, "soil_nitrogen", json_item_value);
                sprintf(json_item_value, "%hu", telemetry_data.soil_phosphorus);
                cJSON_AddStringToObject(root, "soil_phosphorus", json_item_value);
                sprintf(json_item_value, "%hu", telemetry_data.soil_potassium);
                cJSON_AddStringToObject(root, "soil_potassium", json_item_value);
                sprintf(json_item_value, "%hu", telemetry_data.soil_salinity);
                cJSON_AddStringToObject(root, "soil_salinity", json_item_value);
                sprintf(json_item_value, "%hu", telemetry_data.soil_tds);
                cJSON_AddStringToObject(root, "soil_tds", json_item_value);
            }
            
            /* Create pluviometer telemetry data */
            if (atl_config.telemetry.pluviometer.enabled) {
                sprintf(json_item_value, "%.2f", telemetry_data.pluviometer);
                cJSON_AddStringToObject(root, "rain_gauge", json_item_value);
            }
            
            /* Create anemometer telemetry data */
            if (atl_config.telemetry.anemometer.enabled) {
                sprintf(json_item_value, "%.2f", telemetry_data.anemometer);
                cJSON_AddStringToObject(root, "wind_speed", json_item_value);
            }

            /* Create windsock telemetry data */
            if (atl_config.telemetry.wind_sock.enabled) {
                sprintf(json_item_value, "%hu", telemetry_data.wind_direction);
                cJSON_AddStringToObject(root, "wind_direction", json_item_value);
            }

            /* Create ADC telemetry data */            
            if (atl_config.telemetry.adc[0].mode != ATL_ADC_DISABLED) {
                sprintf(json_item_value, "%.2f", telemetry_data.adc[0].avg_sample);
                cJSON_AddStringToObject(root, "adc0_avg", json_item_value);
            }
            if (atl_config.telemetry.adc[1].mode != ATL_ADC_DISABLED) {
                sprintf(json_item_value, "%.2f", telemetry_data.adc[1].avg_sample);
                cJSON_AddStringToObject(root, "adc1_avg", json_item_value);
            }
            if (atl_config.telemetry.adc[2].mode != ATL_ADC_DISABLED) {
                sprintf(json_item_value, "%.2f", telemetry_data.adc[2].avg_sample);
                cJSON_AddStringToObject(root, "adc2_avg", json_item_value);
            }
            if (atl_config.telemetry.adc[3].mode != ATL_ADC_DISABLED) {
                sprintf(json_item_value, "%.2f", telemetry_data.adc[3].avg_sample);
                cJSON_AddStringToObject(root, "adc3_avg", json_item_value);
            }

            /* Send telemetry data to MQTT broker */
            if (atl_mqtt_send_telemetry(root) == ESP_OK) {
                ESP_LOGI(TAG, "Telemetry data sent!");
            } else {
                ESP_LOGW(TAG, "Fail sending telemetry data!");
            }
        } else {
            ESP_LOGW(TAG, "Fail getting telemetry data!");
        }

        /* Free memory */
        cJSON_Delete(root);
    }  
}

/**
 * @fn atl_telemetry_init(void)
 * @brief Initialize telemetry structure and sensor tasks.
 * @return esp_err_t - If ERR_OK success. 
 */
esp_err_t atl_telemetry_init(void) {
    esp_err_t err = ESP_OK;

    /* Creates data semaphore (mutex) */
    atl_telemetry_data_mutex = xSemaphoreCreateMutex();
    if (atl_telemetry_data_mutex == NULL) {
        ESP_LOGE(TAG, "[%s:%d] Error creating telemetry data semaphore!", __func__, __LINE__);
        return ESP_FAIL;
    }

    /* Reset all telemetry values */
    memset(&atl_telemetry, 0, sizeof(atl_data_telemetry_t));

    /* Initialize I2C interface and all peripherals */
    err = atl_i2c_master_init(); 
    if (err == ESP_OK) {
        
        /* Check if BME280 is enabled */
        if (atl_config.telemetry.bme280.enabled) {
        
            /* Detect if BME280 sensor is connected at I2C bus */
            if (atl_i2c_device_exists(CONFIG_ATL_BME280_I2C_ADDR) == ESP_OK) {
                
                /* Create BME280 task */
                if (xTaskCreatePinnedToCore(atl_bme280_task, "atl_bme280_task", 4096, NULL, 10, &atl_bme280_handle, 1)) {
                    ESP_LOGI(TAG, "BME280 sensor task created!");
                } else {
                    ESP_LOGE(TAG, "Error creating BME280 sensor task!");
                    return ESP_FAIL;
                }
            } else {
                ESP_LOGE(TAG, "[%s:%d] BME280 sensor not found!", __func__, __LINE__);
            }
        }

        /* Check if BH1750 is enabled */
        if (atl_config.telemetry.light.enabled) {

            /* Detect if BH1750 sensor is connected at I2C bus */
            if (atl_i2c_device_exists(CONFIG_ATL_LIGHT_I2C_ADDR) == ESP_OK) {            

                /* Create BH1750 task */
                if (xTaskCreatePinnedToCore(atl_light_task, "atl_light_task", 4096, NULL, 10, &atl_light_handle, 1)) {
                    ESP_LOGI(TAG, "Light sensor task created!");
                } else {
                    ESP_LOGE(TAG, "Error creating light sensor task!");
                    return ESP_FAIL;
                }
            } else {
                ESP_LOGE(TAG, "[%s:%d] BH1750 sensor not found!", __func__, __LINE__);            
            }
        } 

        /* Check if some ADC port is enabled */
        if ((atl_config.telemetry.adc[0].mode != ATL_ADC_DISABLED) || (atl_config.telemetry.adc[1].mode != ATL_ADC_DISABLED)
            || (atl_config.telemetry.adc[2].mode != ATL_ADC_DISABLED) || (atl_config.telemetry.adc[3].mode != ATL_ADC_DISABLED)) {
            
            /* Detect if ADS1115 sensor is connected at I2C bus */
            if (atl_i2c_device_exists(CONFIG_ATL_ADC_I2C_ADDR) == ESP_OK) {
            
                /* Initialzie ADC input service */
                atl_adc_init();
            
            } else {
                ESP_LOGE(TAG, "[%s:%d] ADS1115 sensor not found!", __func__, __LINE__);
            }
        }          
    
    } else {
        atl_led_set_color(255, 69, 0);
        ESP_LOGE(TAG, "Fail initializing I2C master: %s", esp_err_to_name(err));
    }

    /* Create ADC unit configuration */
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    err = adc_oneshot_new_unit(&init_config1, &adc1_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error %d creating ADC unit 1! Error: %s", err, esp_err_to_name(err));
        return err;
    }

    /* Create ADC channel configuration */
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    err = adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_3, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error %d configuring ADC 1 channel 3! Error: %s", err, esp_err_to_name(err));
        return err;
    }
    err = adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_4, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error %d configuring ADC 1 channel 4! Error: %s", err, esp_err_to_name(err));
        return err;
    }

    /* Create ADCs calibration */
    do_calibration1_chan3 = adc_calibration_init(ADC_UNIT_1, ADC_CHANNEL_3, ADC_ATTEN_DB_12, &adc1_cali_chan3_handle);
    do_calibration1_chan4 = adc_calibration_init(ADC_UNIT_1, ADC_CHANNEL_4, ADC_ATTEN_DB_12, &adc1_cali_chan4_handle);

    /* Create PWR task */
    if (atl_config.telemetry.power.enabled) {
        if (xTaskCreatePinnedToCore(atl_pwr_task, "atl_pwr_task", 4096, NULL, 10, &atl_pwr_handle, 1)) {
            ESP_LOGI(TAG, "Power sensor task created!");
        } else {
            ESP_LOGE(TAG, "Error creating power sensor task!");
            return ESP_FAIL;
        }
    }

    /* Create DHT task */
    if (atl_config.telemetry.dht.enabled) {
        if (xTaskCreatePinnedToCore(atl_dht_task, "atl_dht_task", 4096, NULL, 10, &atl_dht_handle, 1)) {
            ESP_LOGI(TAG, "DHT sensor task created!");
        } else {
            ESP_LOGE(TAG, "Error creating DHT sensor task!");
            return ESP_FAIL;
        }
    }

    /* Create UV task */
    if (atl_config.telemetry.uv.enabled) {
        if (xTaskCreatePinnedToCore(atl_uv_task, "atl_uv_task", 4096, NULL, 10, &atl_uv_handle, 1)) {
            ESP_LOGI(TAG, "UV sensor task created!");
        } else {
            ESP_LOGE(TAG, "Error creating UV sensor task!");
            return ESP_FAIL;
        }
    }    

    /* Create Anemometer task */
    if (atl_config.telemetry.anemometer.enabled) {
        if (xTaskCreatePinnedToCore(atl_anem_task, "atl_anem_task", 4096, NULL, 10, &atl_anem_handle, 1)) {
            ESP_LOGI(TAG, "Anemometer task created!");
        } else {
            ESP_LOGE(TAG, "Error creating anemometer task!");
            return ESP_FAIL;
        }
    }  

    /* Create Pluviometer task */
    if (atl_config.telemetry.pluviometer.enabled) {
        if (xTaskCreatePinnedToCore(atl_pluv_task, "atl_pluv_task", 4096, NULL, 10, &atl_pluv_handle, 1)) {
            ESP_LOGI(TAG, "Pluviometer task created!");
        } else {
            ESP_LOGE(TAG, "Error creating pluviometer task!");
            return ESP_FAIL;
        }
    }  

    /* Create Windsock task */
    if (atl_config.telemetry.wind_sock.enabled) {
        if (xTaskCreatePinnedToCore(atl_ws_task, "atl_ws_task", 4096, NULL, 10, &atl_ws_handle, 1)) {
            ESP_LOGI(TAG, "Windsock task created!");
        } else {
            ESP_LOGE(TAG, "Error creating windsock task!");
            return ESP_FAIL;
        }
    }  

    /* Create soil sensor task */
    if (atl_config.telemetry.soil.enabled) {
        ESP_LOGI(TAG, "Starting Modbus at RS-485 interface!");
        if (atl_modbus_master_rs485_init() == ESP_OK) {
            ESP_LOGI(TAG, "Modbus Master RS-485 initialized!");
            if (xTaskCreatePinnedToCore(atl_soil_task, "atl_soil_task", 4096, NULL, 10, &atl_soil_handle, 1)) {
                ESP_LOGI(TAG, "Soil sensor task created!");
            } else {
                ESP_LOGE(TAG, "Error creating soil sensor task!");
                return ESP_FAIL;
            }
        } else {
            ESP_LOGE(TAG, "Error initializing Modbus Master at RS-485!");
            return ESP_FAIL;
        }
    }

    /* Create communication task */
    if (xTaskCreatePinnedToCore(atl_telemetry_task, "atl_telemetry_task", 4096, NULL, 10, &atl_telemetry_handle, 1)) {
        ESP_LOGI(TAG, "Telemetry task created!");
    } else {
        ESP_LOGE(TAG, "Error creating DHT sensor task!");
        return ESP_FAIL;
    }
    
    return err;
}

/**
 * @fn atl_telemetry_get_config(void)
 * @brief Get telemetry configuration.
 * @return esp_err_t - If ERR_OK success. 
 */
esp_err_t atl_telemetry_get_config(atl_config_telemetry_t* config_ptr) {

    /* Get telemetry configuration */
    if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
        *config_ptr = atl_config.telemetry;
        xSemaphoreGive(atl_config_mutex);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "[%s:%d] Error getting telemetry configuration! Semaphore fail!", __func__, __LINE__);
        return ESP_FAIL;
    }
}

/**
 * @fn atl_telemetry_set_config(void)
 * @brief Set telemetry configuration.
 * @return esp_err_t - If ERR_OK success. 
 */
esp_err_t atl_telemetry_set_config(atl_config_telemetry_t* config_ptr) {
    
    /* Set telemetry configuration */
    if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
        atl_config.telemetry = *config_ptr;
        if (atl_config.telemetry.dht.enabled) {
            vTaskResume(atl_dht_handle);
        } else {
            vTaskSuspend(atl_dht_handle);
        }
        xSemaphoreGive(atl_config_mutex);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "[%s:%d] Error setting telemetry configuration! Semaphore fail!", __func__, __LINE__);
        return ESP_FAIL;
    }
}

/**
 * @fn atl_telemetry_get_data(void)
 * @brief Get telemetry data.
 * @return esp_err_t - If ERR_OK success. 
 */
esp_err_t atl_telemetry_get_data(atl_data_telemetry_t* config_ptr) {
    
        /* Get telemetry data */
        if (xSemaphoreTake(atl_telemetry_data_mutex, portMAX_DELAY) == pdTRUE) {
            *config_ptr = atl_telemetry;
            xSemaphoreGive(atl_telemetry_data_mutex);
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "[%s:%d] Error getting telemetry data! Semaphore fail!", __func__, __LINE__);
            return ESP_FAIL;
        }
}

/**
 * @brief Set ADC telemetry data.
 * @param channel - ADC channel
 * @param values - struct atl_data_adc_telemetry_t
 * @return esp_err_t 
 */
esp_err_t atl_telemetry_set_adc_data(uint8_t channel, atl_data_adc_telemetry_t values) {    
    esp_err_t err = ESP_OK;

    /* Take semaphore */
    if (!xSemaphoreTake(atl_telemetry_data_mutex, portMAX_DELAY)) {
        ESP_LOGW(TAG, "Timeout taking mutex!");
        err = ESP_FAIL;
    }
    
    /* Set ADC values at specifi channel */
    atl_telemetry.adc[channel] = values;

    /* Give semaphore */
    if (!xSemaphoreGive(atl_telemetry_data_mutex)) {
        ESP_LOGW(TAG, "Fail giving mutex!");
        err = ESP_FAIL;
    }

    return err;
}