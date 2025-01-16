#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum    atl_adc_mode_e
 * @brief   ATL ADC input mode.
 */
typedef enum atl_adc_input_mode {
    ATL_ADC_DISABLED,
	ATL_ADC_INPUT_4_20_MA,
	ATL_ADC_INPUT_0_10_V,    
} atl_adc_input_mode_e;

/**
 * @fn atl_adc_init(void)
 * @brief Initialize Analog Input interface.
 * @details Initialize Analog Input interface.
 * @return esp_err_t - If ESP_OK success. 
 */
esp_err_t atl_adc_init(void);

#ifdef __cplusplus
}
#endif