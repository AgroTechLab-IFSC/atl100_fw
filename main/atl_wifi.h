#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/**
 * @enum    atl_wifi_mode_e
 * @brief   ATL wifi mode.
 */
typedef enum atl_wifi_mode {
    ATL_WIFI_DISABLED,
    ATL_WIFI_AP_MODE,
    ATL_WIFI_STA_MODE,    
} atl_wifi_mode_e;

/**
 * @typedef atl_config_wifi_t
 * @brief   ATL wifi configuration.
 */
typedef struct {
    atl_wifi_mode_e    mode;                    /**< WiFi startup mode.*/
    uint8_t             ap_ssid[32];            /**< WiFi AP SSID.*/
    uint8_t             ap_pass[64];            /**< WiFi AP password.*/
    uint8_t             ap_channel;             /**< WiFi AP channel.*/
    uint8_t             ap_max_conn;            /**< WiFi AP maximum STA connections.*/
    uint8_t             sta_ssid[32];           /**< WiFi STA SSID.*/
    uint8_t             sta_pass[64];           /**< WiFi STA password.*/
    uint8_t             sta_channel;            /**< WiFi STA channel.*/
    uint8_t             sta_max_conn_retry;     /**< WiFi maximum connection retry.*/
} atl_config_wifi_t;

/**
 * @brief Get the wifi mode enum
 * @param mode_str 
 * @return Function enum
 */
atl_wifi_mode_e atl_wifi_get_mode(char* mode_str);

/**
 * @brief Get the wifi mode string object
 * @param mode 
 * @return Function enum const* 
 */
const char* atl_wifi_get_mode_str(atl_wifi_mode_e mode);

/**
 * @fn atl_wifi_init_softap(void)
 * @brief Initialize WiFi interface in SoftAP mode.
 * @return esp_err_t - If ERR_OK success, otherwise fail.
 */
esp_err_t atl_wifi_init_softap(void);

/**
 * @fn atl_wifi_init_sta(void)
 * @brief Initialize WiFi interface in STA mode.
 * @return esp_err_t - If ERR_OK success, otherwise fail.
 */
esp_err_t atl_wifi_init_sta(void);

#ifdef __cplusplus
}
#endif