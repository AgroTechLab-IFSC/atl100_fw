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

    /* Update serial interface output */
    ESP_LOGI(TAG, "Initialization finished!"); 
}