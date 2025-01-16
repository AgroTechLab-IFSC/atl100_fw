#pragma once
#include <esp_err.h>
#include <mbcontroller.h>
#include "atl_modbus_params.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATL_MB_RS485_PORT_NUM               UART_NUM_1      /**< RS-485 UART port. */
#define ATL_MB_RS485_DEV_SPEED              4800            /**< RS-485 baudrate. */
#define ATL_MB_RS485_DEVICE_ADDR1           1               /**< Only one slave device used (soil sensor). */
#define CONFIG_MB_COMM_MODE_RTU             true            /**< RS-485 RTU mode enabled. */
#define ATL_MB_RS485_MASTER_MAX_RETRIES     5               /**< Maximum read retries. */
#define ATL_MB_RS485_RETRIES_TIMEOUT_MS     500             /**< Timeout between retries. */
#define ATL_MB_RS485_POLL_TIMEOUT_MS        250             /**< Timeout between polls. */

/* The macro to get offset for parameter in the appropriate structure */
#define HOLD_OFFSET(field) ((uint16_t)(offsetof(holding_reg_params_t, field) + 1))
#define INPUT_OFFSET(field) ((uint16_t)(offsetof(input_reg_params_t, field) + 1))
#define COIL_OFFSET(field) ((uint16_t)(offsetof(coil_reg_params_t, field) + 1))
#define DISCR_OFFSET(field) ((uint16_t)(offsetof(discrete_reg_params_t, field) + 1))

/* The macro to get fielname */
#define ATL_MB_STR(fieldname) ((const char*)( fieldname ))

/* The macro options can be used as bit masks or parameter limits */
#define ATL_MB_OPTS(min_val, max_val, step_val) { .opt1 = min_val, .opt2 = max_val, .opt3 = step_val }

/**
 * @enum CID enumeration
 * @brief Enumeration of all supported CIDs for device (used in parameter definition table)
 */
enum {
    CID_HOLD_HUMIDITY = 0,
    CID_HOLD_TEMPERATURE,
    CID_HOLD_CONDUCTIVITY,
    CID_HOLD_PH,
    CID_HOLD_NITROGEN,
    CID_HOLD_PHOSPHORUS,
    CID_HOLD_POTASSIUM,
    CID_HOLD_SALINITY,
    CID_HOLD_TDS,
    CID_HOLD_BATT_VOLTAGE,
    CID_HOLD_PTEMP,
};

/**
 * @brief Get the number of parameters in the table.
 * @return uint16_t 
 */
uint16_t atl_modbus_get_num_device_params(void);

/**
 * @brief The function to get pointer to parameter storage (instance) according to parameter description table.
 * @param param_descriptor 
 * @return void* 
 */
void* atl_modbus_master_rs485_get_param_data(const mb_parameter_descriptor_t* param_descriptor);

/**
 * @brief Initialize Modbus Master stack at RS-485 interface
 * @return esp_err_t 
 */
esp_err_t atl_modbus_master_rs485_init(void);

/**
 * @brief Initialize Modbus Master stack at RS-485 interface
 * @return esp_err_t 
 */
bool atl_modbus_master_rs485_initialized(void);

#ifdef __cplusplus
}
#endif