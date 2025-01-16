#include "atl_led.h"
#include "atl_modbus.h"

/* Constants */
static const char *TAG = "atl-modbus";                  /**< Module identification. */

/* Global variables */
static bool modbus_master_rs485_initialized = false;    /**< Modbus Master RS-485 initialized flag */
void* mb_rs485_master_handler = NULL;                   /**< Modbus Master RS-485 handler */

/**
 * @brief Modbus descriptor for NPKPHCTH sensor model
 * @details CID - field in the table (must be unique).
 *          Param Name - Name of the parameter.
 *          Units - Units of the parameter.
 *          Modbus Slave Addr - field defines slave address of the device with correspond parameter.
 *          Modbus Reg Type - Type of Modbus register area (Holding register, Input Register and such).
 *          Reg Start - field defines the start Modbus register number.
 *          Reg Size - defines the number of registers for the characteristic accordingly.
 *          Instance Offset - defines offset in the appropriate parameter structure that will be used as instance to save parameter value.
 *          Data Type - specify value type of CID.
 *          Data Size - specify value size of CID.
 *          Parameter Options - field specifies the options that can be used to process parameter value (limits or masks).
 *          Access Mode - can be used to implement custom options for processing of characteristic (Read/Write restrictions, factory mode values and etc).
 */
const mb_parameter_descriptor_t atl_modbus_device_parameters[] = {
    // { CID, Param Name, Units, Modbus Slave Addr, Modbus Reg Type, Reg Start, Reg Size, Instance Offset, Data Type, Data Size, Parameter Options, Access Mode}
    // { CID_HOLD_HUMIDITY, ATL_MB_STR("CR300_BattV"), ATL_MB_STR("V"), 1, MB_PARAM_HOLDING, 1, 2,
    //         HOLD_OFFSET(holding_data1), PARAM_TYPE_FLOAT, 4, ATL_MB_OPTS( 0, 24, 1 ), PAR_PERMS_READ },
    { CID_HOLD_HUMIDITY, ATL_MB_STR("Humidity"), ATL_MB_STR("%RH"), ATL_MB_RS485_DEVICE_ADDR1, MB_PARAM_HOLDING, 0, 1,
            HOLD_OFFSET(holding_data0), PARAM_TYPE_U16, 2, ATL_MB_OPTS( 0, 100, 0.1 ), PAR_PERMS_READ },
    { CID_HOLD_TEMPERATURE, ATL_MB_STR("Temperature"), ATL_MB_STR("oC"), ATL_MB_RS485_DEVICE_ADDR1, MB_PARAM_HOLDING, 1, 1,
            HOLD_OFFSET(holding_data1), PARAM_TYPE_U16, 2, ATL_MB_OPTS( -40, 80, 0.1 ), PAR_PERMS_READ },
    { CID_HOLD_CONDUCTIVITY, ATL_MB_STR("Conductivity"), ATL_MB_STR("us/cm"), ATL_MB_RS485_DEVICE_ADDR1, MB_PARAM_HOLDING, 2, 1,
            HOLD_OFFSET(holding_data2), PARAM_TYPE_U16, 2, ATL_MB_OPTS( 0, 20000, 1 ), PAR_PERMS_READ },
    { CID_HOLD_PH, ATL_MB_STR("PH"), ATL_MB_STR("PH"), ATL_MB_RS485_DEVICE_ADDR1, MB_PARAM_HOLDING, 3, 1,
            HOLD_OFFSET(holding_data3), PARAM_TYPE_U16, 2, ATL_MB_OPTS( 3, 9, 0.1 ), PAR_PERMS_READ },
    { CID_HOLD_NITROGEN, ATL_MB_STR("Nitrogen"), ATL_MB_STR("mg/kg"), ATL_MB_RS485_DEVICE_ADDR1, MB_PARAM_HOLDING, 4, 1,
            HOLD_OFFSET(holding_data4), PARAM_TYPE_U16, 2, ATL_MB_OPTS( 1, 2999, 1 ), PAR_PERMS_READ_WRITE },
    { CID_HOLD_PHOSPHORUS, ATL_MB_STR("Phosphorus"), ATL_MB_STR("mg/kg"), ATL_MB_RS485_DEVICE_ADDR1, MB_PARAM_HOLDING, 5, 1,
            HOLD_OFFSET(holding_data5), PARAM_TYPE_U16, 2, ATL_MB_OPTS( 1, 2999, 1 ), PAR_PERMS_READ_WRITE },
    { CID_HOLD_POTASSIUM, ATL_MB_STR("Potassium"), ATL_MB_STR("mg/kg"), ATL_MB_RS485_DEVICE_ADDR1, MB_PARAM_HOLDING, 6, 1,
            HOLD_OFFSET(holding_data6), PARAM_TYPE_U16, 2, ATL_MB_OPTS( 1, 2999, 1 ), PAR_PERMS_READ_WRITE },
    { CID_HOLD_SALINITY, ATL_MB_STR("Salinity"), ATL_MB_STR("mg/L"), ATL_MB_RS485_DEVICE_ADDR1, MB_PARAM_HOLDING, 7, 1,
            HOLD_OFFSET(holding_data7), PARAM_TYPE_U16, 2, ATL_MB_OPTS( 1, 2999, 1 ), PAR_PERMS_READ },
    { CID_HOLD_TDS, ATL_MB_STR("TDS"), ATL_MB_STR("mg/L"), ATL_MB_RS485_DEVICE_ADDR1, MB_PARAM_HOLDING, 8, 1,
            HOLD_OFFSET(holding_data8), PARAM_TYPE_U16, 2, ATL_MB_OPTS( 1, 2999, 1 ), PAR_PERMS_READ },
    // { CID_HOLD_BATT_VOLTAGE, ATL_MB_STR("CR300_BattV"), ATL_MB_STR("V"), 2, MB_PARAM_HOLDING, 40001, 2,
    //         HOLD_OFFSET(holding_data9), PARAM_TYPE_FLOAT, 2, ATL_MB_OPTS( 0, 40, 1 ), PAR_PERMS_READ },
    // { CID_HOLD_PTEMP, ATL_MB_STR("CR300_PTemp"), ATL_MB_STR("oC"), 2, MB_PARAM_HOLDING, 2, 2,
    //         HOLD_OFFSET(holding_data10), PARAM_TYPE_FLOAT, 2, ATL_MB_OPTS( -40, 80, 0.1 ), PAR_PERMS_READ },
    // { CID_HOLD_TEMPERATURE, ATL_MB_STR("CR300_AirTemp"), ATL_MB_STR("oC"), 2, MB_PARAM_HOLDING, 4, 2,
    //         HOLD_OFFSET(holding_data11), PARAM_TYPE_FLOAT, 2, ATL_MB_OPTS( -40, 80, 0.1 ), PAR_PERMS_READ },
    // { CID_HOLD_HUMIDITY, ATL_MB_STR("CR300_AirHumid"), ATL_MB_STR("%RH"), 2, MB_PARAM_HOLDING, 6, 2,
    //         HOLD_OFFSET(holding_data12), PARAM_TYPE_FLOAT, 2, ATL_MB_OPTS( 0, 100, 0.1 ), PAR_PERMS_READ },
};

/**
 * @brief Get the number of parameters in the table.
 * @return uint16_t 
 */
uint16_t atl_modbus_get_num_device_params(void) {
    return (sizeof(atl_modbus_device_parameters)/sizeof(atl_modbus_device_parameters[0]));
}
/**
 * @brief The function to get pointer to parameter storage (instance) according to parameter description table.
 * @param param_descriptor 
 * @return void* 
 */
void* atl_modbus_master_rs485_get_param_data(const mb_parameter_descriptor_t* param_descriptor) {
    assert(param_descriptor != NULL);
    void* instance_ptr = NULL;
    if (param_descriptor->param_offset != 0) {
       switch(param_descriptor->mb_param_type)
       {
           case MB_PARAM_HOLDING:
               instance_ptr = ((void*)&holding_reg_params + param_descriptor->param_offset - 1);
               break;
           case MB_PARAM_INPUT:
               instance_ptr = ((void*)&input_reg_params + param_descriptor->param_offset - 1);
               break;
           case MB_PARAM_COIL:
               instance_ptr = ((void*)&coil_reg_params + param_descriptor->param_offset - 1);
               break;
           case MB_PARAM_DISCRETE:
               instance_ptr = ((void*)&discrete_reg_params + param_descriptor->param_offset - 1);
               break;
           default:
               instance_ptr = NULL;
               break;
       }
    } else {
        ESP_LOGE(TAG, "Wrong parameter offset for CID #%u", (unsigned)param_descriptor->cid);
        assert(instance_ptr != NULL);
    }
    return instance_ptr;
}

/**
 * @brief Initialize Modbus Master stack at RS-485 interface
 * @return esp_err_t 
 */
esp_err_t atl_modbus_master_rs485_init(void) {
    esp_err_t err = ESP_OK;

    /* Create Modbus Master configuration */
    mb_communication_info_t comm = {
#if CONFIG_MB_COMM_MODE_ASCII
            .mode = MB_MODE_ASCII,
#elif CONFIG_MB_COMM_MODE_RTU
            .mode = MB_RTU,
#endif
            .ser_opts.port = ATL_MB_RS485_PORT_NUM,
            .ser_opts.baudrate = ATL_MB_RS485_DEV_SPEED,
            .ser_opts.parity = MB_PARITY_NONE,
    };
    

    /* Create Modbus-RTU Master controller */
    err = mbc_master_create_serial(&comm, &mb_rs485_master_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Modbus-RTU master controller initialization fail!");
        goto error_proc;
    }

    /* Setup Modbus Master stack */
    // err = mbc_master_setup((void*)&comm);
    // if (err != ESP_OK) {
    //     ESP_LOGE(TAG, "Modbus master controller setup fail!");
    //     goto error_proc;
    // }
    
    /* Set Modbus UART pin */
    err = uart_set_pin(ATL_MB_RS485_PORT_NUM, CONFIG_ATL_RS485_TXD_GPIO, CONFIG_ATL_RS485_RXD_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Modbus Master UART set pin fail!");
        goto error_proc;
    }
    
    /* Start Modbus Master controller */
    err = mbc_master_start(&mb_rs485_master_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Modbus Master controller start fail!");
        goto error_proc;
    }

    /* Set driver mode to Half Duplex */
    err = uart_set_mode(ATL_MB_RS485_PORT_NUM, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Modbus Master UART set mode fail!");
        goto error_proc;
    }

    /* Set Modbus Master stack descriptor */
    vTaskDelay(5);
    err = mbc_master_set_descriptor(mb_rs485_master_handler, &atl_modbus_device_parameters[0], atl_modbus_get_num_device_params());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Modbus Master set descriptor fail!");
        goto error_proc;
    }
    
    ESP_LOGI(TAG, "Modbus Master initialized at RS-485 interface!");
    modbus_master_rs485_initialized = true;
    return err;

    /* Error procedure */
error_proc:
    atl_led_builtin_blink(6, 100, 255, 0, 0);
    ESP_LOGE(TAG, "Error: %d = %s", err, esp_err_to_name(err));
    return err;
}

/**
 * @brief Initialize Modbus Master stack at RS-485 interface
 * @return esp_err_t 
 */
bool atl_modbus_master_rs485_initialized(void) {
    return modbus_master_rs485_initialized;
}