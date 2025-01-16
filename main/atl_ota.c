#include <stdint.h>
#include <string.h>
#include "atl_ota.h"

/* Constants */
// static const char *TAG = "atl-ota";             /**< Module identification. */
const char *atl_ota_behaviour_str[] = {         
    "ATL_OTA_BEHAVIOUR_DISABLED",
	"ATL_OTA_BEHAVIOUR_VERIFY_NOTIFY",
	"ATL_OTA_BEHAVIOU_DOWNLOAD",
	"ATL_OTA_BEHAVIOU_DOWNLOAD_REBOOT",
};

/**
 * @brief Get the ota behaviour string object
 * @param behaviour 
 * @return Function enum const* 
 */
const char* atl_ota_get_behaviour_str(atl_ota_behaviour_e behaviour) {
    return atl_ota_behaviour_str[behaviour];
}

/**
 * @brief Get the ota behaviour string object
 * @param behaviour_str 
 * @return Function enum
 */
atl_ota_behaviour_e atl_ota_get_behaviour(char* behaviour_str) {
    uint8_t i = 0;
    while (atl_ota_behaviour_str[i] != NULL) {
        if (strcmp(behaviour_str, atl_ota_behaviour_str[i]) == 0) {
            return i;
        } else {
            i++;
        }
    }
    return 255;
}