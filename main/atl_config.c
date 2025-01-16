#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <inttypes.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "atl_config.h"
#include "atl_wifi.h"
#include "atl_webserver.h"
#include "atl_mqtt.h"
#include "atl_telemetry.h"

/* Constants */
static const char *TAG = "atl-config";      /**< Module identification. */

/* Global variables */
SemaphoreHandle_t atl_config_mutex;         /**< Configuration semaphore (mutex). */
atl_config_t atl_config;                    /**< Global configuration variable. */  

/**
 * @fn atl_config_create_default(void)
 * @brief Create configuration file with default values.
 * @details Create configuration file with default values. 
 */
static void atl_config_create_default(void) {    
    char ssid[32];
    unsigned char mac[6] = {0};

    /** Creates default SYSTEM configuration **/
    atl_config.system.led_behaviour = ATL_LED_ENABLED_FULL;

    /** Creates default OTA configuration **/
    atl_config.ota.behaviour = ATL_OTA_BEHAVIOUR_VERIFY_NOTIFY;

    /** Creates default WIFI configuration **/
    atl_config.wifi.mode = ATL_WIFI_AP_MODE;
    esp_efuse_mac_get_default(mac);
    sprintf(ssid, "%s%02x%02x%02x", CONFIG_ATL_WIFI_AP_SSID_PREFIX, mac[3], mac[4], mac[5]+1);
    strncpy((char*)&atl_config.wifi.ap_ssid, ssid, sizeof(atl_config.wifi.ap_ssid));
    strncpy((char*)&atl_config.wifi.ap_pass, CONFIG_ATL_WIFI_AP_PASSWORD, sizeof(atl_config.wifi.ap_pass));
    atl_config.wifi.ap_channel = CONFIG_ATL_WIFI_AP_CHANNEL;
    atl_config.wifi.ap_max_conn = CONFIG_ATL_WIFI_AP_MAX_STA_CONN;
    strncpy((char*)&atl_config.wifi.sta_ssid, "AgroTechLab", sizeof(atl_config.wifi.sta_ssid));
    strncpy((char*)&atl_config.wifi.sta_pass, CONFIG_ATL_WIFI_AP_PASSWORD, sizeof(atl_config.wifi.sta_pass));
    atl_config.wifi.sta_channel = CONFIG_ATL_WIFI_AP_CHANNEL;
    atl_config.wifi.sta_max_conn_retry = CONFIG_ATL_WIFI_STA_MAX_CONN_RETRY;    

    /** Creates default WEBSERVER configuration **/  
    atl_config.webserver.mode = ATL_WEBSERVER_HTTP;  
    strncpy((char*)&atl_config.webserver.admin_user, CONFIG_ATL_WEBSERVER_ADMIN_USER, sizeof(atl_config.webserver.admin_user));
    strncpy((char*)&atl_config.webserver.admin_pass, CONFIG_ATL_WEBSERVER_ADMIN_PASS, sizeof(atl_config.webserver.admin_pass));

    /** Creates default MQTT CLIENT configuration **/
    atl_config.mqtt_client.mode = CONFIG_ATL_MQTT_BROKER_MODE;
    strncpy((char*)&atl_config.mqtt_client.broker_address, CONFIG_ATL_MQTT_BROKER_ADDR, sizeof(atl_config.mqtt_client.broker_address));
    atl_config.mqtt_client.broker_port = CONFIG_ATL_MQTT_BROKER_PORT;
    atl_config.mqtt_client.transport = MQTT_TRANSPORT_OVER_TCP;
    atl_config.mqtt_client.disable_cn_check = true;
    strncpy((char*)&atl_config.mqtt_client.user, (char*)&atl_config.wifi.ap_ssid, sizeof(atl_config.mqtt_client.user));
    strncpy((char*)&atl_config.mqtt_client.pass, (char*)&atl_config.wifi.ap_ssid, sizeof(atl_config.mqtt_client.pass));
    atl_config.mqtt_client.qos = CONFIG_ATL_MQTT_QOS;

    /** Creates default TELEMETRY configuration **/
    atl_config.telemetry.send_period = CONFIG_ATL_SEND_PERIOD;
    atl_config.telemetry.power.enabled = CONFIG_ATL_PWR_ENABLED;
    atl_config.telemetry.power.sampling_period = CONFIG_ATL_PWR_SAMPLING_PERIOD;
    atl_config.telemetry.uv.enabled = CONFIG_ATL_UV_ENABLED;
    atl_config.telemetry.uv.sampling_period = CONFIG_ATL_UV_SAMPLING_PERIOD;
    atl_config.telemetry.light.enabled = CONFIG_ATL_LIGHT_ENABLED;
    atl_config.telemetry.light.sampling_period = CONFIG_ATL_LIGHT_SAMPLING_PERIOD;
    for (uint8_t i = 0; i < 4; i++) {
        atl_config.telemetry.adc[i].mode = CONFIG_ATL_ADC_MODE;
        atl_config.telemetry.adc[i].sampling_period = CONFIG_ATL_ADC_SAMPLING_PERIOD;
        atl_config.telemetry.adc[i].sampling_window = CONFIG_ATL_ADC_SAMPLING_PERIOD;
    }
    atl_config.telemetry.dht.enabled = CONFIG_ATL_DHT_ENABLED;
    atl_config.telemetry.dht.sampling_period = CONFIG_ATL_DHT_SAMPLING_PERIOD;
    atl_config.telemetry.bme280.enabled = CONFIG_ATL_BME280_ENABLED;
    atl_config.telemetry.bme280.sampling_period = CONFIG_ATL_BME280_SAMPLING_PERIOD;
    atl_config.telemetry.soil.enabled = CONFIG_ATL_SOIL_ENABLED;
    atl_config.telemetry.soil.sampling_period = CONFIG_ATL_SOIL_SAMPLING_PERIOD;
    atl_config.telemetry.soil.modbus_rtu_addr = CONFIG_ATL_SOIL_MODBUS_ADDR;
    atl_config.telemetry.pluviometer.enabled = CONFIG_ATL_PLUVIOMETER_ENABLED;
    atl_config.telemetry.pluviometer.sampling_period = CONFIG_ATL_PLUVIOMETER_SAMPLING_PERIOD;
    atl_config.telemetry.anemometer.enabled = CONFIG_ATL_ANEMOMETER_ENABLED;
    atl_config.telemetry.anemometer.sampling_period = CONFIG_ATL_ANEMOMETER_SAMPLING_PERIOD;
    atl_config.telemetry.wind_sock.enabled = CONFIG_ATL_WIND_SOCK_ENABLED;
    atl_config.telemetry.wind_sock.sampling_period = CONFIG_ATL_WIND_SOCK_SAMPLING_PERIOD;

    /** Creates default GATEWAY (ATL100 <-> CR300) configuration */
    // atl_config.telemetry.cr300_gw.enabled = CONFIG_ATL_GATEWAY_ENABLED;
    // atl_config.telemetry.cr300_gw.sampling_period = CONFIG_ATL_GATEWAY_SAMPLING_PERIOD;
    // atl_config.telemetry.cr300_gw.modbus_rtu_addr = CONFIG_ATL_GATEWAY_MODBUS_ADDR;
}

/**
 * @fn atl_config_init(void)
 * @brief Initialize configuration from NVS.
 * @details If not possible load configuration file, create a new with default values.
 * @return esp_err_t - If ERR_OK success. 
 */
esp_err_t atl_config_init(void) {
    esp_err_t err;
    nvs_handle_t nvs_handler;
    
    /* Creates configuration semaphore (mutex) */
    atl_config_mutex = xSemaphoreCreateMutex();
    if (atl_config_mutex == NULL) {
        ESP_LOGE(TAG, "[%s:%d] Error creating configuration semaphore!", __func__, __LINE__);
        return ESP_FAIL;
    }

    /* Initialize NVS (Non-Volatile Storage) */
    // nvs_flash_erase();
    ESP_LOGI(TAG, "Starting NVS (Non-Volatile Storage)");
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing and restarting NVS");
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_init());
    }

    /* Open NVS system */
    ESP_LOGI(TAG, "Loading configuration from NVS");
    ESP_LOGI(TAG, "Mounting NVS storage");
    err = nvs_open("nvs", NVS_READWRITE, &nvs_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s:%d] Fail mounting NVS storage", __func__, __LINE__);
        ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));
        return err;
    }

    /* Read the memory size required to configuration file */
    ESP_LOGI(TAG, "Loading configuration file");
    size_t file_size = sizeof(atl_config_t);
    err = nvs_get_blob(nvs_handler, "atl_config", &atl_config, &file_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "[%s:%d] Fail loading configuration file!", __func__, __LINE__);
        goto error_proc;
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "File not found! Creating new file with default values!");
        atl_config_create_default();

        /* Creates atl_config file */
        err = nvs_set_blob(nvs_handler, "atl_config", &atl_config, sizeof(atl_config_t));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "[%s:%d] Fail creating new configuration file!", __func__, __LINE__);
            goto error_proc;
        }

        /* Write atl_config file in NVS */
        err = nvs_commit(nvs_handler);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "[%s:%d] Fail writing new configuration file!", __func__, __LINE__);
            goto error_proc;
        } 
    }

    /* Close NVS */
    ESP_LOGI(TAG, "Unmounting NVS storage");
    nvs_close(nvs_handler);
    return ESP_OK;

    /* Error procedure */
    error_proc:
        ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));
        nvs_close(nvs_handler);
        return err;
}

/**
 * @fn atl_config_commit_nvs(void)
 * @brief Initialize configuration from NVS.
 * @details If not possible load configuration file, create a new with default values.
 * @return esp_err_t - If ERR_OK success, otherwise fail.
 */
esp_err_t atl_config_commit_nvs(void) {
    esp_err_t err;
    nvs_handle_t nvs_handler;
    ESP_LOGI(TAG, "Commiting configuration at NVS");    
    
    /* Open NVS system */
    ESP_LOGD(TAG, "Mounting NVS storage");
    xSemaphoreTake(atl_config_mutex, portMAX_DELAY);
    err = nvs_open("nvs", NVS_READWRITE, &nvs_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s:%d] Fail mounting NVS storage", __func__, __LINE__);
        goto error_proc;
    }

    /* Creates atl_config file */
    err = nvs_set_blob(nvs_handler, "atl_config", &atl_config, sizeof(atl_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s:%d] Fail creating new configuration file!", __func__, __LINE__);
        goto error_proc;
    }

    /* Write atl_config file in NVS */
    err = nvs_commit(nvs_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s:%d] Fail writing new configuration file!", __func__, __LINE__);
        goto error_proc;
    }

    /* Close NVS */
    ESP_LOGD(TAG, "Unmounting NVS storage");
    nvs_close(nvs_handler);
    xSemaphoreGive(atl_config_mutex);
    return ESP_OK;

/* Error procedure */
error_proc:
    ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));
    nvs_close(nvs_handler);
    xSemaphoreGive(atl_config_mutex);
    return err;
}

/**
 * @fn atl_config_erase_nvs(void)
 * @brief Erase NVS (Non-Volatile Storage).
 * @details Erase NVS.
 * @return esp_err_t - If ERR_OK success. 
 */
esp_err_t atl_config_erase_nvs(void) {
    esp_err_t err = ESP_OK;
    nvs_flash_erase();
    return err;
}