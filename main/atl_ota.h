#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @typedef atl_ota_behaviour_e
 * @brief OTA behaviour.
 */
typedef enum {	
    ATL_OTA_BEHAVIOUR_DISABLED,
	ATL_OTA_BEHAVIOUR_VERIFY_NOTIFY,
	ATL_OTA_BEHAVIOU_DOWNLOAD,
	ATL_OTA_BEHAVIOU_DOWNLOAD_REBOOT,    
} atl_ota_behaviour_e;

/**
 * @brief Get the ota behaviour string object
 * @param behaviour 
 * @return Function enum const* 
 */
const char* atl_ota_get_behaviour_str(atl_ota_behaviour_e behaviour);

/**
 * @brief Get the ota behaviour string object
 * @param behaviour_str 
 * @return Function enum
 */
atl_ota_behaviour_e atl_ota_get_behaviour(char* behaviour_str);

#ifdef __cplusplus
}
#endif