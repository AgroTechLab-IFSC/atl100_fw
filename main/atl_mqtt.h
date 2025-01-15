#pragma once

#include <mqtt_client.h>
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum    atl_mqtt_mode_e
 * @brief   ATL MQTT mode.
 */
typedef enum atl_mqtt_mode {
    ATL_MQTT_DISABLED,
    ATL_MQTT_AGROTECHLAB_CLOUD,
    ATL_MQTT_THIRD,    
} atl_mqtt_mode_e;

/**
 * @enum    atl_mqtt_qos_e
 * @brief   ATL MQTT QoS level.
 */
typedef enum atl_mqtt_qos {
    ATL_MQTT_QOS0,
    ATL_MQTT_QOS1,
    ATL_MQTT_QOS2,
} atl_mqtt_qos_e;

/**
 * @typedef atl_config_mqtt_client_t
 * @brief   ATL MQTT client configuration.
 */
typedef struct {
    atl_mqtt_mode_e         mode;               /**< MQTT mode.*/
    uint8_t                 broker_address[64]; /**< MQTT broker address.*/
    uint16_t                broker_port;        /**< MQTT broker port.*/
    esp_mqtt_transport_t    transport;          /**< MQTT transport protocol.*/         
    bool                    disable_cn_check;   /**< Skip certificate Common Name check (for self-signed certificates).*/
    uint8_t                 user[32];           /**< MQTT username.*/
    uint8_t                 pass[64];           /**< MQTT password.*/
    atl_mqtt_qos_e          qos;                /**< MQTT QoS level.*/
} atl_config_mqtt_client_t;

void atl_mqtt_init(void);

/**
 * @brief Get the MQTT mode string object
 * @param mode 
 * @return Function enum const* 
 */
const char* atl_mqtt_get_mode_str(atl_mqtt_mode_e mode);

/**
 * @brief Get the MQTT mode enum
 * @param mode_str 
 * @return Function enum
 */
atl_mqtt_mode_e atl_mqtt_get_mode(char* mode_str);

/**
 * @brief Get the MQTT transport string object
 * @param transport 
 * @return Function enum const* 
 */
const char* atl_mqtt_get_transport_str(esp_mqtt_transport_t transport);

/**
 * @brief Get the MQTT transport enum
 * @param transport_str 
 * @return Function enum
 */
esp_mqtt_transport_t atl_mqtt_get_transport(char* transport_str);

/**
 * @brief Send telemetry data to MQTT broker
 * @param json - pointer to JSON object 
 * @return esp_err_t - If ERR_OK success.
 */
esp_err_t atl_mqtt_send_telemetry(cJSON *json);

/**
 * @brief Restart MQTT client
 * @return esp_err_t 
 */
esp_err_t atl_mqtt_client_restart(void);

#ifdef __cplusplus
}
#endif