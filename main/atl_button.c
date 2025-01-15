#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include "atl_led.h"

/* Constants */
static const char *TAG = "atl-button";      /**< Function identification */

/* Global variables */
static QueueHandle_t button_evt_queue;      /**< Button event queue */
bool button_pressed = false;                /**< Button pressed */
TaskHandle_t atl_button_handle = NULL;      /**< Button task handle */

/**
 * @fn button_isr_handler(void *args)
 * @brief Button event handler
 * @param [in] args - Pointer to task arguments
*/
static void IRAM_ATTR button_isr_handler(void *args) {
    uint32_t button_pin = (uint32_t)args;
    xQueueSendFromISR(button_evt_queue, &button_pin, NULL);  
}

/**
 * @fn atl_button_task(void *args)
 * @brief Button task
 * @param [in] args - Pointer to task arguments 
*/
static void atl_button_task(void *args) {
    uint32_t gpio_pin;

    /* Task looping */
    while (true) {

        /* Check for button event */
        if (xQueueReceive(button_evt_queue, &gpio_pin, portMAX_DELAY)) {
            if (gpio_get_level(CONFIG_ATL_BUTTON_GPIO) == 0) {
                button_pressed = true;
                atl_led_set_color(255, 69, 0);                
            } else {
                button_pressed = false;
                atl_led_set_color(0, 0, 255);
            }             
        }        
    }    
}

/**
 * @fn atl_button_init(void)
 * @brief Initialize button task
*/
void atl_button_init(void) {
    uint8_t cpu_core = 0;
    if (strcmp(CONFIG_IDF_TARGET, "esp32") == 0 || strcmp(CONFIG_IDF_TARGET, "esp32s3") == 0 || strcmp(CONFIG_IDF_TARGET, "esp32p4") == 0) {
        ESP_LOGI(TAG, "Creating button task at CPU 1");
        cpu_core = 1;
    } else {
        ESP_LOGI(TAG, "Creating button task");
    }
    
    /* Configure button event */
    gpio_set_direction(CONFIG_ATL_BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_pulldown_en(CONFIG_ATL_BUTTON_GPIO);
    gpio_pullup_dis(CONFIG_ATL_BUTTON_GPIO);
    gpio_set_intr_type(CONFIG_ATL_BUTTON_GPIO, GPIO_INTR_ANYEDGE);
    button_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    /* Create LED builtin task at CPU 1 */
    xTaskCreatePinnedToCore(atl_button_task, "atl_button_task", 2048, NULL, 10, &atl_button_handle, cpu_core);

    /* Install interruption handler at button event */
    gpio_install_isr_service(0);
    gpio_isr_handler_add(CONFIG_ATL_BUTTON_GPIO, button_isr_handler, (void*)CONFIG_ATL_BUTTON_GPIO);
}