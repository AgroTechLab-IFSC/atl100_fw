#pragma once
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HTTPD_401   "401 UNAUTHORIZED"          /**< HTTP 401 Unauthorized. */

/**
 * @enum    atl_webserver_mode_e
 * @brief   ATL Webserver mode.
 */
typedef enum atl_webserver_mode {
    ATL_WEBSERVER_DISABLED,
    ATL_WEBSERVER_HTTP,
    ATL_WEBSERVER_HTTPS,    
} atl_webserver_mode_e;

/**
 * @typedef atl_config_webserver_t
 * @brief   ATL webserver configuration.
 */
typedef struct {
    atl_webserver_mode_e    mode;           /**< Webserver mode. */
    uint8_t                 admin_user[32]; /**< Webserver administration user.*/
    uint8_t                 admin_pass[64]; /**< Webserver administration pass.*/
} atl_config_webserver_t;

/**
 * @fn atl_webserver_init(void)
 * @brief Initialize Webserver.
 * @return err ESP_OK if success.
 */
esp_err_t atl_webserver_init(void);

/**
 * @brief Get the WEBSERVER mode string object
 * @param mode 
 * @return Function enum const* 
 */
const char* atl_webserver_get_mode_str(atl_webserver_mode_e mode);

#ifdef __cplusplus
}
#endif