#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "atl_led.h"
#include "atl_wifi.h"
// #include "atl_webserver.h"
// #include "atl_mqtt.h"
// #include "atl_ota.h"
// #include "atl_telemetry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* External global variable */
extern SemaphoreHandle_t atl_config_mutex;

/**
 * @typedef atl_config_system_t
 * @brief System configuration structure.
 */
typedef struct {
    atl_led_behaviour_e     led_behaviour;      /**< LED behaviour. */
} atl_config_system_t;

/**
 * @typedef atl_config_ota_t
 * @brief OTA configuration structure.
 */
typedef struct {
    // atl_ota_behaviour_e    behaviour; /**< OTA behaviour. */
} atl_config_ota_t;

/**
 * @typedef atl_config_t
 * @brief   ATL configuration structure.
 */
typedef struct {
    atl_config_system_t        system;             /**< ATL system configuration.*/
    atl_config_ota_t           ota;                /**< ATL OTA (Over-the-Air) configuration.*/
    atl_config_wifi_t          wifi;               /**< ATL WiFi configuration.*/
    // atl_config_webserver_t     webserver;          /**< ATL Webserver configuration.*/
    // atl_config_mqtt_client_t   mqtt_client;        /**< ATL MQTT client configuration.*/
    // atl_config_telemetry_t     telemetry;          /**< ATL Telemetry configuration.*/  
} atl_config_t;

/**
 * @fn atl_config_init(void)
 * @brief Initialize configuration from NVS.
 * @details If not possible load configuration file, create a new with default values.
 * @return esp_err_t - If ERR_OK success. 
 */
esp_err_t atl_config_init(void);

/**
 * @fn atl_config_commit_nvs(void)
 * @brief Initialize configuration from NVS.
 * @details If not possible load configuration file, create a new with default values.
 * @return esp_err_t - If ERR_OK success, otherwise fail.
 */
esp_err_t atl_config_commit_nvs(void);

/**
 * @fn atl_config_erase_nvs(void)
 * @brief Erase NVS (Non-Volatile Storage).
 * @details Erase NVS.
 * @return esp_err_t - If ERR_OK success. 
 */
esp_err_t atl_config_erase_nvs(void);

#ifdef __cplusplus
}
#endif