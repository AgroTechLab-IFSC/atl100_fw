#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_err.h>
#include <esp_log.h>
#include "atl_i2c.h"

/* Constants */
static const char *TAG = "atl-i2c";                     /**< Module identification. */
static const uint16_t i2c_mutex_timeout = 1000;         /**< I2C mutex timeout (in ms). */

/* Global variables */
static SemaphoreHandle_t i2c_mutex;                     /**< I2C mutex */
static i2c_master_bus_handle_t bus_handle;              /**< I2C bus handle */

/**
 * @fn atl_i2c_master_init(void)
 * @brief I2C master initialization
 */
esp_err_t atl_i2c_master_init(void) {
    esp_err_t err = ESP_OK;
    int i2c_master_port = (int)ATL_I2C_MASTER_NUM;
    ESP_LOGI(TAG, "Starting I2C master interface");

    i2c_master_bus_config_t i2c_master_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = i2c_master_port,
        .sda_io_num = CONFIG_ATL_I2C_MASTER_SDA_GPIO,
        .scl_io_num = CONFIG_ATL_I2C_MASTER_SCL_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    /* Creating mutex */
    i2c_mutex = xSemaphoreCreateMutex();
    if (i2c_mutex == NULL) {
        ESP_LOGE(TAG, "Could not create mutex!");
        err = ESP_ERR_NO_MEM;
        return err;
    }

    /* Create bus handle */
    err = i2c_new_master_bus(&i2c_master_config, &bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail creating I2C bus handle!");
        return err;
    }

    return err;
}

/**
 * @brief Dectect I2C devices
*/
uint8_t atl_i2c_detect_devices(void) {
    uint8_t count = 0;
    uint8_t address;
    ESP_LOGI(TAG, "Detecting I2C devices");
    
    /* Take semaphore */
    if (!xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(30000))) {
        ESP_LOGW(TAG, "Timeout taking mutex!");
    }

    /* Scan I2C bus for devices */
    for (int i = 0; i < 128; i += 16) {
        for (int j = 0; j < 16; j++) {
            fflush(stdout);
            address = i + j;
            esp_err_t err = i2c_master_probe(bus_handle, address, 500);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Detected device at address 0x%02x", address);
                count++;
            } else if (err == ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "Timeout detected device at address 0x%02x", address);
            }
        }
    }

    /* Give semaphore */
    if (!xSemaphoreGive(i2c_mutex)) {
        ESP_LOGW(TAG, "Fail giving mutex!");
    }

    return count;
}

/**
 * @brief Check if I2C device is connected
*/
esp_err_t atl_i2c_device_exists(uint8_t i2c_address) {
    
    ESP_LOGI(TAG, "Checking for I2C device at address 0x%02X", i2c_address);

    /* Take semaphore */
    if (!xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(i2c_mutex_timeout))) {
        ESP_LOGW(TAG, "Timeout taking mutex!");
    }

    esp_err_t err = i2c_master_probe(bus_handle, i2c_address, ATL_I2C_MASTER_TIMEOUT_MS);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Detected!");
    } else if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Not detected");
    } else if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "I2C timeout!");
    }

    /* Give semaphore */
    if (!xSemaphoreGive(i2c_mutex)) {
        ESP_LOGW(TAG, "Fail giving mutex!");
    }

    return err;
}

/**
 * @brief Add device at I2C bus
 * @param dev_cfg device configuration
 * @param dev_handle device handle
 * @return esp_err_t 
 */
esp_err_t atl_i2c_master_bus_add_device(i2c_device_config_t *dev_cfg, i2c_master_dev_handle_t *dev_handle) {
    esp_err_t err = ESP_OK;

    /* Take semaphore */
    if (!xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(i2c_mutex_timeout))) {
        ESP_LOGW(TAG, "Timeout taking mutex!");
    }

    /* Add device at I2C bus */   
    err = i2c_master_bus_add_device(bus_handle, dev_cfg, dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail creating I2C device handle!");
    }

    /* Give semaphore */
    if (!xSemaphoreGive(i2c_mutex)) {
        ESP_LOGW(TAG, "Fail giving mutex!");
    }
    
    return err;
}

/**
 * @brief Read data from I2C bus
 * @param[in] dev_handle I2C device handle
 * @param[in] buf_ptr Pointer to data buffer
 * @param[in] buf_size Size of data buffer
 * @return ESP_OK if success, otherwise fail
*/
esp_err_t atl_i2c_receive(i2c_master_dev_handle_t dev_handle, uint8_t* buf_ptr, size_t buf_size) {
    esp_err_t err = ESP_OK;

    /* Take semaphore */
    if (!xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(i2c_mutex_timeout))) {
        ESP_LOGW(TAG, "Timeout taking mutex!");
    }

    /* Read data */
    err = i2c_master_receive(dev_handle, buf_ptr, buf_size, ATL_I2C_MASTER_TIMEOUT_MS);
    if (err == ESP_ERR_INVALID_ARG) {
        ESP_LOGW(TAG, "Invalid argument!");
    } else if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "Timeout!");
    }


    /* Give semaphore */
    if (!xSemaphoreGive(i2c_mutex)) {
        ESP_LOGW(TAG, "Fail giving mutex!");
    }

    return err;
}

/**
 * @brief Transmit data at I2C bus
 * @param[in] dev_handle I2C device handle
 * @param[in] buf_ptr Pointer to data buffer
 * @param[in] buf_size Size of data buffer
 * @return ESP_OK if success, otherwise fail
 */
esp_err_t atl_i2c_transmit(i2c_master_dev_handle_t dev_handle, uint8_t* buf_ptr, size_t buf_size) {
    esp_err_t err = ESP_OK;

    /* Take semaphore */
    if (!xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(i2c_mutex_timeout))) {
        ESP_LOGW(TAG, "Timeout taking mutex!");
    }

    /* Write data */
    err = i2c_master_transmit(dev_handle, buf_ptr, buf_size, ATL_I2C_MASTER_TIMEOUT_MS);
    if (err == ESP_ERR_INVALID_ARG) {
        ESP_LOGW(TAG, "Invalid argument!");
    } else if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "Timeout!");
    }

    /* Give semaphore */
    if (!xSemaphoreGive(i2c_mutex)) {
        ESP_LOGW(TAG, "Fail giving mutex!");
    }

    return err;
}