#include <stdio.h>
#include <esp_log.h>
#include "atl_config.h"
#include "atl_led.h"
#include "atl_button.h"

/* Constants */
static const char *TAG = "atl-main";        /**< Module identification. */

/* Global external variables */
extern atl_config_t atl_config;             /**< Global configuration variable. */

/**
 * @brief Main application entry point.
 */
void app_main(void) {

    /* Initialize led builtin */
    atl_led_builtin_init();

    /* Initialize button */
    atl_button_init();

    /* Load configuration from NVS */
    atl_config_init();

    /* Check the WiFi startup mode defined by configuration file */
    if (atl_config.wifi.mode != ATL_WIFI_DISABLED) {

        // /* If defines as Access Point mode */
        // if (atl_config.wifi.mode == ATL_WIFI_AP_MODE) {
            
        //     /* Initialize WiFi in AP mode */
        //     atl_wifi_init_softap();

        // } else if (atl_config.wifi.mode == ATL_WIFI_STA_MODE) {
            
        //     /* Initialize WiFi in STA mode */
        //     if (atl_wifi_init_sta() == ESP_OK) {

        //         /* Initialize MQTT client */
        //         if ((atl_config.mqtt_client.mode == ATL_MQTT_AGROTECHLAB_CLOUD) || 
        //             (atl_config.mqtt_client.mode == ATL_MQTT_THIRD)) {
        //                 atl_mqtt_init();
        //         }

        //         /* Initialize telemetry */
        //         atl_telemetry_init();

        //     } else {
        //         ESP_LOGE(TAG, "Fail to initialize WiFi in STA mode!");
        //         ESP_LOGE(TAG, "Waiting for 60 seconds and restarting!");
        //         vTaskDelay(pdMS_TO_TICKS(60000));
        //         esp_restart();
        //     }
        // }

        // /* Initialize webserver (HTTP) */
        // atl_webserver_init();
    }

    /* Update serial interface output */
    ESP_LOGI(TAG, "Initialization finished!"); 
}