#pragma once
#include <driver/i2c_master.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ATL_I2C_MASTER_NUM             0        /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define ATL_I2C_MASTER_FREQ_HZ         400000   /*!< I2C master clock frequency */
#define ATL_I2C_MASTER_TX_BUF_DISABLE  0        /*!< I2C master doesn't need buffer */
#define ATL_I2C_MASTER_RX_BUF_DISABLE  0        /*!< I2C master doesn't need buffer */
#define ATL_I2C_MASTER_TIMEOUT_MS      1000     /*!< I2C master timeout */
#define ACK_CHECK_EN 0x1                        /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS 0x0                       /*!< I2C master will not check ack from slave */
#define ACK_VAL 0x0                             /*!< I2C ack value */
#define NACK_VAL 0x1                            /*!< I2C nack value */

/**
 * @brief I2C master initialization
 */
esp_err_t atl_i2c_master_init(void);

/**
 * @brief Dectect I2C devices
*/
uint8_t atl_i2c_detect_devices(void);

/**
 * @brief Check if I2C device is connected
*/
esp_err_t atl_i2c_device_exists(uint8_t i2c_address);

/**
 * @brief Add device at I2C bus
 * @param dev_cfg device configuration
 * @param dev_handle device handle
 * @return esp_err_t 
 */
esp_err_t atl_i2c_master_bus_add_device(i2c_device_config_t *dev_cfg, i2c_master_dev_handle_t *dev_handle);

/**
 * @brief Read data from I2C bus
 * @param[in] dev_handle I2C device handle
 * @param[in] buf_ptr Pointer to data buffer
 * @param[in] buf_size Size of data buffer
 * @return ESP_OK if success, otherwise fail
*/
esp_err_t atl_i2c_receive(i2c_master_dev_handle_t dev_handle, uint8_t* buf_ptr, size_t buf_size);

/**
 * @brief Transmit data at I2C bus
 * @param[in] dev_handle I2C device handle
 * @param[in] buf_ptr Pointer to data buffer
 * @param[in] buf_size Size of data buffer
 * @return ESP_OK if success, otherwise fail
 */
esp_err_t atl_i2c_transmit(i2c_master_dev_handle_t dev_handle, uint8_t* buf_ptr, size_t buf_size);

#ifdef __cplusplus
}
#endif