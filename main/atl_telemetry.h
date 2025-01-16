#pragma once

#include "atl_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_VREF    1100        /**< Use adc2_vref_to_gpio() to obtain a better estimate. */

/**
 * @typedef atl_telemetry_t
 * @brief Telemetry configuration structure.
 */
typedef struct {
    bool        enabled;            /**< Sensor enabled. */
    uint32_t    sampling_period;    /**< Sampling period. */
} atl_telemetry_t;

/**
 * @typedef atl_adc_telemetry_t
 * @brief ADC telemetry configuration structure.
 */
typedef struct {
    atl_adc_input_mode_e    mode;               /**< Sensor mode. */
    uint32_t                sampling_period;    /**< Sampling period. */
    uint16_t                sampling_window;    /**< Sampling window. */
} atl_adc_telemetry_t;

/**
 * @typedef atl_soil_telemetry_t
 * @brief Soil telemetry configuration structure.
 */
typedef struct {
    bool        enabled;            /**< Sensor enabled. */
    uint32_t    sampling_period;    /**< Sampling period. */
    uint8_t     modbus_rtu_addr;    /**< Modbus-RTU address. */
} atl_soil_telemetry_t;

/**
 * @typedef atl_gw_telemetry_t
 * @brief Gateway telemetry configuration structure.
 */
typedef struct {
    bool        enabled;            /**< Sensor enabled. */
    uint32_t    sampling_period;    /**< Sampling period. */
    uint8_t     modbus_rtu_addr;    /**< Modbus-RTU address. */
} atl_gw_telemetry_t;

/**
 * @typedef atl_config_telemetry_t
 * @brief Telemetry configuration structure.
 */
typedef struct {
    uint32_t                send_period;    /**< Send period (in seconds). */
    atl_telemetry_t         power;          /**< Power telemetry configuration. */
    atl_adc_telemetry_t     adc[4];         /**< Analog telemetry configuration. */
    atl_telemetry_t         dht;            /**< DHT telemetry configuration. */
    atl_telemetry_t         uv;             /**< UV telemetry configuration. */
    atl_telemetry_t         light;          /**< Light telemetry configuration. */
    atl_telemetry_t         bme280;         /**< BME280 telemetry configuration. */
    atl_soil_telemetry_t    soil;           /**< Soil telemetry configuration. */
    atl_telemetry_t         pluviometer;    /**< Pluviometer telemetry configuration. */
    atl_telemetry_t         anemometer;     /**< Anemometer telemetry configuration. */
    atl_telemetry_t         wind_sock;      /**< Wind sock telemetry configuration. */
    atl_gw_telemetry_t      cr300_gw;       /**< Wind sock telemetry configuration. */
} atl_config_telemetry_t;

/**
 * @typedef atl_data_adc_telemetry_t
 * @brief   ATL ADC input telemetry.
 */
typedef struct {    		    	
	float last_sample;  /**< ATL Analog input last sample.*/
	float avg_sample;   /**< ATL Analog input average sample.*/
	float min_sample;   /**< ATL Analog input minimum value sampled.*/
	float max_sample;   /**< ATL Analog input maximum value sampled.*/	
} atl_data_adc_telemetry_t;

/**
 * @typedef atl_data_telemetry_t
 * @brief Telemetry data structure.
 */
typedef struct {
    float power_voltage;                /**< Power voltage (in V). */
    float air_temperature;              /**< Air temperature (in oC). */
    float air_humidity;                 /**< Air humidity (in %). */
    float air_pressure;                 /**< Air pressure (in hPa). */
    uint8_t uv_index;                   /**< UV index. */
    float light_intensity;              /**< Light intensity (in lux). */
    float soil_temperature;             /**< Soil temperature (in oC). */
    float soil_humidity;                /**< Soil humidity (in %). */
    uint16_t soil_ec;                   /**< Soil EC (in us/cm). */
    float soil_ph;                      /**< Soil pH. */
    uint16_t soil_nitrogen;             /**< Soil nitrogen (in ppm). */
    uint16_t soil_phosphorus;           /**< Soil phosphorus (in ppm). */
    uint16_t soil_potassium;            /**< Soil potassium (in ppm). */
    uint16_t soil_salinity;             /**< Soil salinity (in mg/L). */
    uint16_t soil_tds;                  /**< Soil TDS (Total Dissolved Solids) (in mg/L). */
    float pluviometer;                  /**< Pluviometer (in mm). */
    float anemometer;                   /**< Anemometer (in Km/h). */
    uint16_t wind_direction;            /**< Wind direction (in degrees). */
    atl_data_adc_telemetry_t adc[4];    /**< ADC sensor (4 ports). */
} atl_data_telemetry_t;

/**
 * @fn atl_telemetry_init(void)
 * @brief Initialize telemetry structure and sensor tasks.
 * @return esp_err_t - If ERR_OK success. 
 */
esp_err_t atl_telemetry_init(void);

/**
 * @fn atl_telemetry_get_config(void)
 * @brief Get telemetry configuration.
 * @return esp_err_t - If ERR_OK success. 
 */
esp_err_t atl_telemetry_get_config(atl_config_telemetry_t* config_ptr);

/**
 * @fn atl_telemetry_set_config(void)
 * @brief Set telemetry configuration.
 * @return esp_err_t - If ERR_OK success. 
 */
esp_err_t atl_telemetry_set_config(atl_config_telemetry_t* config_ptr);

/**
 * @fn atl_telemetry_get_data(void)
 * @brief Get telemetry data.
 * @return esp_err_t - If ERR_OK success. 
 */
esp_err_t atl_telemetry_get_data(atl_data_telemetry_t* config_ptr);

/**
 * @brief Set ADC telemetry data.
 * @param port 
 * @param values - struct atl_data_adc_telemetry_t
 * @return esp_err_t 
 */
esp_err_t atl_telemetry_set_adc_data(uint8_t port, atl_data_adc_telemetry_t values);

#ifdef __cplusplus
}
#endif