#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <led_strip.h>
#include <sdkconfig.h>
#include "atl_led.h"
#include "atl_config.h"

/* Constants */
static const char *TAG = "atl-led";                 /**< Module identification. */
static const uint16_t led_mutex_timeout = 5000;     /**< LED builtin mutex default timeout. */
const char *atl_led_behaviour_str[] = {             /**< LED behaviour string. */
    "ATL_LED_DISABLED",
    "ATL_LED_ENABLED_FAILS",
    "ATL_LED_ENABLED_COMM_FAILS",
    "ATL_LED_ENABLED_FULL",
};

/* Global variables */
static SemaphoreHandle_t led_mutex;                         /**< LED builtin mutex */
static bool led_builtin_state = false;                      /**< LED builtin enabled */
static led_strip_handle_t led_strip;                        /**< LED builtin handle */
static atl_led_rgb_color_t atl_led_color = {0, 0, 255};     /**< LED builtin color */
TaskHandle_t atl_led_handle = NULL;                         /**< LED builtin task handle */
static uint8_t button_count = 0;                            /**< Button pressed count */
extern bool button_pressed;                                 /**< Button pressed */

/**
 * @fn atl_led_task(void *args)
 * @brief LED builtin task
 * @param [in] args - Pointer to task arguments 
*/
static void atl_led_task(void *args) {

    /* Task looping */
    while (true) {

        /* Check if button was pressed */
        if (button_pressed) {
            button_count++;

            /* Check if is factory reset */
            if (button_count == 10) {
                ESP_LOGW(TAG, ">>> Executing factory reset!");
                atl_led_builtin_blink(10, 100, 255, 69, 0);
                atl_config_erase_nvs();
                esp_restart();
            }

            /* Toogle period */
            vTaskDelay(pdMS_TO_TICKS(250));

        } else {

            /* Toogle period */
            vTaskDelay(pdMS_TO_TICKS(CONFIG_ATL_LED_PERIOD * 1000));

        }

        /* Toogle led builtin */
        atl_led_builtin_toogle();
    }    
}

/**
 * @brief Get the led behaviour string object
 * @param behaviour 
 * @return Function enum const* 
 */
const char* atl_led_get_behaviour_str(atl_led_behaviour_e behaviour) {
    return atl_led_behaviour_str[behaviour];
}

/**
 * @brief Get the led behaviour string object
 * @param behaviour_str 
 * @return Function enum
 */
atl_led_behaviour_e atl_led_get_behaviour(char* behaviour_str) {
    uint8_t i = 0;
    while (atl_led_behaviour_str[i] != NULL) {
        if (strcmp(behaviour_str, atl_led_behaviour_str[i]) == 0) {
            return i;
        } else {
            i++;
        }
    }
    return 255;
}

/**
 * @fn atl_led_builtin_init(void)
 * @brief Initialize led builtin task
*/
void atl_led_builtin_init(void) {
    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "Creating LED builtin task at CPU 1");

    /* Creating led mutex */
    led_mutex = xSemaphoreCreateMutex();
    if (led_mutex == NULL) {
        ESP_LOGW(TAG, "Could not create mutex!");
    }

    /* LED strip general initialization, according to your led board design */
    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_ATL_LED_BUILTIN_GPIO,                  /**< The GPIO that connected to the LED strip's data line */
        .max_leds = 1,                                                  /**< The number of LEDs in the strip */
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,    /**< Pixel format of your LED strip */
        .led_model = LED_MODEL_WS2812,                                  /**< LED strip model */
        .flags.invert_out = false,                                      /**< whether to invert the output signal */
    };

    /* LED strip backend configuration: RMT */
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,        /**< different clock source can lead to different power consumption */
        .resolution_hz = LED_STRIP_RMT_RES_HZ, /**< RMT counter clock frequency */
        .flags.with_dma = false,               /**< DMA feature is available on ESP target like ESP32-S3 */
    };         

    /* Initialize LED builtin */
    err = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    if (err == ESP_OK) {

        ESP_LOGI(TAG, "Created LED strip object with RMT backend");
  
        /* Power off led strip */
        led_strip_clear(led_strip);

        /* Create LED builtin task at CPU 1 */
        xTaskCreatePinnedToCore(atl_led_task, "atl_led_task", 2048, NULL, 10, &atl_led_handle, 1);
        
    } else {
        ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));        
    }    
}

/**
 * @fn atl_led_builtin_toogle(void)
 * @brief Toggle led builtin
*/
void atl_led_builtin_toogle(void) {

    /* Take semaphore */
    if (!xSemaphoreTake(led_mutex, pdMS_TO_TICKS(led_mutex_timeout))) {
        ESP_LOGW(TAG, "Timeout taking mutex!");
    }
    
    /* If the addressable LED is enabled */
    if (led_builtin_state == false) {

        /* Set the LED on */
        ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_set_pixel(led_strip, 0, atl_led_color.red, atl_led_color.green, atl_led_color.blue));

    } else {

        /* Set all LED off */
        ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_clear(led_strip));
    }
    
    /* Refresh the strip to send data */
    ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_refresh(led_strip));
    
    /* Give semaphore */
    if (!xSemaphoreGive(led_mutex)) {
        ESP_LOGW(TAG, "Fail giving mutex!");
    }

    /* Update led state variable */
    led_builtin_state = !led_builtin_state;
}

/**
 * @fn atl_led_builtin_blink(uint8_t times, uint16_t interval, uint8_t red, uint8_t green, uint8_t blue)
 * @brief Blink led builtin
 * @param [in] times blink times
 * @param [in] interval interval between blinks
 * @param [in] red red value (0..255)
 * @param [in] green green value (0..255)
 * @param [in] blue blue value (0..255)
*/
void atl_led_builtin_blink(uint8_t times, uint16_t interval, uint8_t red, uint8_t green, uint8_t blue) {

    /* Take semaphore */
    if (!xSemaphoreTake(led_mutex, pdMS_TO_TICKS(led_mutex_timeout))) {
        ESP_LOGW(TAG, "Timeout taking mutex!");
    }
 
    /* Set all LED off to clear all pixels */
    ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_clear(led_strip));
    ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_refresh(led_strip));

    /* Blink looping */    
    for (uint8_t i = 0; i < times; i++) {
        
        /* Set the LED on */
        ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_set_pixel(led_strip, 0, red, green, blue));
        ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_refresh(led_strip));

        /* Wait ON interval */
        vTaskDelay(pdMS_TO_TICKS(200));

        /* Set all LED off */
        ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_clear(led_strip));
        ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_refresh(led_strip));

        /* Wait OFF interval */
        vTaskDelay(pdMS_TO_TICKS(interval));
    }

    /* Give semaphore */
    if (!xSemaphoreGive(led_mutex)) {
        ESP_LOGW(TAG, "Fail giving mutex!");
    }
}

/**
 * @fn atl_led_set_color(uint8_t red, uint8_t green, uint8_t blue)
 * @brief Set led builtin color
 * @param [in] red red value (0..255)
 * @param [in] green green value (0..255)
 * @param [in] blue blue value (0..255)
*/
void atl_led_set_color(uint8_t red, uint8_t green, uint8_t blue) {

    /* Take semaphore */
    if (!xSemaphoreTake(led_mutex, pdMS_TO_TICKS(led_mutex_timeout))) {
        ESP_LOGW(TAG, "Timeout taking mutex!");
    }

    /* Update LED color */
    atl_led_color.red = red;
    atl_led_color.green = green;
    atl_led_color.blue = blue;

    /* Give semaphore */
    if (!xSemaphoreGive(led_mutex)) {
        ESP_LOGW(TAG, "Fail giving mutex!");
    }
}

/**
 * @fn atl_led_set_enabled(bool status)
 * @brief Enabled/disabled led builtin
 * @param [in] status enabled or disabled led builtin
*/
void atl_led_set_enabled(bool status) {
    /* Take semaphore */
    if (!xSemaphoreTake(led_mutex, pdMS_TO_TICKS(led_mutex_timeout))) {
        ESP_LOGW(TAG, "Timeout taking mutex!");
    }

    led_builtin_state = status;
    if (status == false) {
        /* Power off led strip */
        ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_clear(led_strip));
        ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_refresh(led_strip));
    }

    /* Give semaphore */
    if (!xSemaphoreGive(led_mutex)) {
        ESP_LOGW(TAG, "Fail giving mutex!");
    }
}