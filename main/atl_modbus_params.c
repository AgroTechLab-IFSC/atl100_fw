#include "atl_modbus_params.h"

/* Here are the user defined instances for device parameters packed by 1 byte */
/* These are keep the values that can be accessed from Modbus master */
holding_reg_params_t holding_reg_params = { 0 };
input_reg_params_t input_reg_params = { 0 };
coil_reg_params_t coil_reg_params = { 0 };
discrete_reg_params_t discrete_reg_params = { 0 };