#include <math.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include "atl_config.h"
#include "atl_wifi.h"
#include "atl_mqtt.h"
// #include "atl_ota.h"

/* Constants */
static const char *TAG = "atl-mqtt";                    /**< Module identification. */
// extern const unsigned char controlle_cloud_cert_start[] asm("_binary_controlle_cloud_pem_start");
// extern const unsigned char controlle_cloud_cert_end[] asm("_binary_controlle_cloud_pem_end");

const char *atl_mqtt_mode_str[] = {                       
    "ATL_MQTT_DISABLED",
    "ATL_MQTT_AGROTECHLAB_CLOUD",
    "ATL_MQTT_THIRD",
};

const char *atl_mqtt_transport_str[] = {                 
    "MQTT_TRANSPORT_UNKNOWN",
    "MQTT_TRANSPORT_OVER_TCP",
    "MQTT_TRANSPORT_OVER_SSL",
    "MQTT_TRANSPORT_OVER_WS",
    "MQTT_TRANSPORT_OVER_WSS",
};

/* Global external variables */
extern atl_config_t atl_config;                             /**< Global configuration variable. */

/* Global variables */
TaskHandle_t x200_analog_input_mqtt_handle = NULL;          /**< Task handler for ATL200 analog input MQTT. */
esp_mqtt_client_handle_t client;                            /**< MQTT client handler. */

// static void log_error_if_nonzero(const char *message, int error_code) {
//     if (error_code != 0) {
//         ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
//     }
// }

static esp_mqtt5_user_property_item_t atl_user_property_arr[] = {
        {"board", "ATL100"},
        // {"u", "user"},
        // {"p", "password"}
    };

#define USE_PROPERTY_ARR_SIZE   sizeof(atl_user_property_arr)/sizeof(esp_mqtt5_user_property_item_t)

static esp_mqtt5_publish_property_config_t publish_property = {
    .payload_format_indicator = 1,
    .message_expiry_interval = 1000,
    // .topic_alias = 0,
    // .response_topic = "/topic/test/response",
    // .correlation_data = "123456",
    // .correlation_data_len = 6,
};

static esp_mqtt5_subscribe_property_config_t subscribe_property = {
    // .subscribe_id = 25555,
    // .no_local_flag = false,
    // .retain_as_published_flag = false,
    .retain_handle = 0,
    //.is_share_subscribe = true,
    //.share_name = "group1",
};

// static esp_mqtt5_subscribe_property_config_t subscribe1_property = {
//     // .subscribe_id = 25555,
//     // .no_local_flag = true,
//     // .retain_as_published_flag = false,
//     .retain_handle = 0,
// };

// static esp_mqtt5_unsubscribe_property_config_t unsubscribe_property = {
//     .is_share_subscribe = true,
//     // .share_name = "group1",
// };

// static esp_mqtt5_disconnect_property_config_t disconnect_property = {
//     .session_expiry_interval = 60,
//     .disconnect_reason = 0,
// };

// static void print_user_property(mqtt5_user_property_handle_t user_property) {
//     if (user_property) {
//         uint8_t count = esp_mqtt5_client_get_user_property_count(user_property);
//         if (count) {
//             esp_mqtt5_user_property_item_t *item = malloc(count * sizeof(esp_mqtt5_user_property_item_t));
//             if (esp_mqtt5_client_get_user_property(user_property, item, &count) == ESP_OK) {
//                 for (int i = 0; i < count; i ++) {
//                     esp_mqtt5_user_property_item_t *t = &item[i];
//                     ESP_LOGI(TAG, "key is %s, value is %s", t->key, t->value);
//                     free((char *)t->key);
//                     free((char *)t->value);
//                 }
//             }
//             free(item);
//         }
//     }
// }

/**
 * @brief Get the MQTT mode string object
 * @param mode 
 * @return Function enum const* 
 */
const char* atl_mqtt_get_mode_str(atl_mqtt_mode_e mode) {
    return atl_mqtt_mode_str[mode];
}

/**
 * @brief Get the MQTT mode enum
 * @param mode_str 
 * @return Function enum
 */
atl_mqtt_mode_e atl_mqtt_get_mode(char* mode_str) {
    uint8_t i = 0;
    while (atl_mqtt_mode_str[i] != NULL) {
        if (strcmp(mode_str, atl_mqtt_mode_str[i]) == 0) {
            return i;
        } else {
            i++;
        }
    }
    return 255;
}

/**
 * @brief Get the MQTT transport string object
 * @param transport 
 * @return Function enum const* 
 */
const char* atl_mqtt_get_transport_str(esp_mqtt_transport_t transport) {
    return atl_mqtt_transport_str[transport];
}

/**
 * @brief Get the MQTT transport enum
 * @param transport_str 
 * @return Function enum
 */
esp_mqtt_transport_t atl_mqtt_get_transport(char* transport_str) {
    uint8_t i = 0;
    while (atl_mqtt_transport_str[i] != NULL) {
        if (strcmp(transport_str, atl_mqtt_transport_str[i]) == 0) {
            return i;
        } else {
            i++;
        }
    }
    return 255;
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void atl_mqtt5_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    static int msg_id = 0;
    static int request_id = 0;
    static uint32_t chunk_size = 4096;
    static uint32_t chunk_count = 0;
    static uint32_t chunk_current = 0;
    cJSON *root;
    static const esp_partition_t *update_partition = NULL;
    static esp_ota_handle_t update_handle = 0 ;
    esp_err_t err;

    ESP_LOGD(TAG, "free heap size is %" PRIu32 ", minimum %" PRIu32, esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            //print_user_property(event->property->user_property);    

            /* If ATL200 is connected at AgroTechLab Cloud */
            if (atl_config.mqtt_client.mode == ATL_MQTT_AGROTECHLAB_CLOUD) {
            
                /* Subscribe to ThingsBoard topic SHARED ATTRIBUTES */
                esp_mqtt5_client_set_user_property(&subscribe_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                esp_mqtt5_client_set_subscribe_property(client, &subscribe_property);
                msg_id = esp_mqtt_client_subscribe(client, "v1/devices/me/attributes", 1);
                esp_mqtt5_client_delete_user_property(subscribe_property.user_property);
                subscribe_property.user_property = NULL;
                ESP_LOGI(TAG, "Sending subscribe to [v1/devices/me/attributes], msg_id=%d", msg_id);

                /* Subscribe to ThingsBoard topic SHARED and CLIENT ATTRIBUTES RESPONSE */
                esp_mqtt5_client_set_user_property(&subscribe_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                esp_mqtt5_client_set_subscribe_property(client, &subscribe_property);
                msg_id = esp_mqtt_client_subscribe(client, "v1/devices/me/attributes/response/+", 1);
                esp_mqtt5_client_delete_user_property(subscribe_property.user_property);
                subscribe_property.user_property = NULL;
                ESP_LOGI(TAG, "Sending subscribe to [v1/devices/me/attributes/response/+], msg_id=%d", msg_id);

                /* Subscribe to ThingsBoard topic FIRMWARE ATTRIBUTES RESPONSE */
                // if (atl_config.ota.behaviour != ATL_OTA_BEHAVIOUR_DISABLED) {                
                //     esp_mqtt5_client_set_user_property(&subscribe_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                //     esp_mqtt5_client_set_subscribe_property(client, &subscribe_property);
                //     msg_id = esp_mqtt_client_subscribe(client, "v2/fw/response/+/chunk/+", 1);
                //     esp_mqtt5_client_delete_user_property(subscribe_property.user_property);
                //     subscribe_property.user_property = NULL;
                //     ESP_LOGI(TAG, "Sending subscribe to [v2/fw/response/+/chunk/+], msg_id=%d", msg_id);
                // }

                /* Send current firmware version to ThingsBoard */
                esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                esp_mqtt5_client_set_publish_property(client, &publish_property);
                root = cJSON_CreateObject();
                esp_app_desc_t app_info;
                const esp_partition_t *partition_info_ptr;
                partition_info_ptr = esp_ota_get_running_partition();
                esp_ota_get_partition_description(partition_info_ptr, &app_info);
                cJSON_AddStringToObject(root, "current_fw_title", app_info.project_name);
                cJSON_AddStringToObject(root, "current_fw_version", app_info.version);
                msg_id = esp_mqtt_client_publish(client, "v1/devices/me/telemetry", cJSON_Print(root), 0, 1, 0);
                esp_mqtt5_client_delete_user_property(publish_property.user_property);
                publish_property.user_property = NULL;
                ESP_LOGI(TAG, "Sending firmware version to [v1/devices/me/telemetry], msg_id=%d", msg_id);
                cJSON_Delete(root);

                /* Send current WiFi configuration (updated by shared attributes) to ThingsBoard */
                esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                esp_mqtt5_client_set_publish_property(client, &publish_property);
                root = cJSON_CreateObject();                                                
                char json_item_value[64] = "";                
                cJSON_AddStringToObject(root, "wifi.startup_mode", atl_wifi_get_mode_str(atl_config.wifi.mode));
                sprintf(json_item_value, "%s", atl_config.wifi.sta_ssid);
                cJSON_AddStringToObject(root, "wifi.sta_ssid", json_item_value);
                memset(&json_item_value, 0, sizeof(json_item_value));
                sprintf(json_item_value, "%s", atl_config.wifi.sta_pass);
                cJSON_AddStringToObject(root, "wifi.sta_pass", json_item_value);
                msg_id = esp_mqtt_client_publish(client, "v1/devices/me/attributes", cJSON_Print(root), 0, 1, 0);
                esp_mqtt5_client_delete_user_property(publish_property.user_property);
                publish_property.user_property = NULL;
                ESP_LOGI(TAG, "Sending WiFi configuration to [v1/devices/me/attributes], msg_id=%d", msg_id);
                cJSON_Delete(root);

                /* Send current WebServer configuration (updated by shared attributes) to ThingsBoard */
                esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                esp_mqtt5_client_set_publish_property(client, &publish_property);
                root = cJSON_CreateObject();                                                             
                cJSON_AddStringToObject(root, "webserver.startup_mode", atl_webserver_get_mode_str(atl_config.webserver.mode));
                memset(&json_item_value, 0, sizeof(json_item_value));  
                sprintf(json_item_value, "%s", atl_config.webserver.admin_user);
                cJSON_AddStringToObject(root, "webserver.admin_user", json_item_value);
                memset(&json_item_value, 0, sizeof(json_item_value));
                sprintf(json_item_value, "%s", atl_config.webserver.admin_pass);
                cJSON_AddStringToObject(root, "webserver.admin_pass", json_item_value);
                msg_id = esp_mqtt_client_publish(client, "v1/devices/me/attributes", cJSON_Print(root), 0, 1, 0);
                esp_mqtt5_client_delete_user_property(publish_property.user_property);
                publish_property.user_property = NULL;
                ESP_LOGI(TAG, "Sending Webserver configuration to [v1/devices/me/attributes], msg_id=%d", msg_id);
                cJSON_Delete(root);

                /* Send current MQTT configuration (updated by shared attributes) to ThingsBoard */
                esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                esp_mqtt5_client_set_publish_property(client, &publish_property);
                root = cJSON_CreateObject();                                                
                memset(&json_item_value, 0, sizeof(json_item_value));               
                cJSON_AddStringToObject(root, "mqtt_client.mode", atl_mqtt_get_mode_str(atl_config.mqtt_client.mode));
                cJSON_AddStringToObject(root, "mqtt_client.broker_address", (const char*)&atl_config.mqtt_client.broker_address);
                cJSON_AddNumberToObject(root, "mqtt_client.broker_port", atl_config.mqtt_client.broker_port);
                cJSON_AddStringToObject(root, "mqtt_client.transport", atl_mqtt_get_transport_str(atl_config.mqtt_client.transport));
                cJSON_AddBoolToObject(root, "mqtt_client.disable_cn_check", atl_config.mqtt_client.disable_cn_check);
                sprintf(json_item_value, "%s", atl_config.mqtt_client.user);
                cJSON_AddStringToObject(root, "mqtt_client.user", json_item_value);
                memset(&json_item_value, 0, sizeof(json_item_value));
                sprintf(json_item_value, "%s", atl_config.mqtt_client.pass);
                cJSON_AddStringToObject(root, "mqtt_client.pass", json_item_value);
                cJSON_AddNumberToObject(root, "mqtt_client.qos", atl_config.mqtt_client.qos);                      
                msg_id = esp_mqtt_client_publish(client, "v1/devices/me/attributes", cJSON_Print(root), 0, 1, 0);
                esp_mqtt5_client_delete_user_property(publish_property.user_property);
                publish_property.user_property = NULL;
                ESP_LOGI(TAG, "Sending MQTT configuration to [v1/devices/me/attributes], msg_id=%d", msg_id);
                cJSON_Delete(root);                            

                /* Send current Firmware Update configuration (updated by shared attributes) to ThingsBoard */
                esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                esp_mqtt5_client_set_publish_property(client, &publish_property);
                root = cJSON_CreateObject();                                                
                memset(&json_item_value, 0, sizeof(json_item_value));               
                // cJSON_AddStringToObject(root, "ota.behaviour", atl_ota_get_behaviour_str(atl_config.ota.behaviour));                
                msg_id = esp_mqtt_client_publish(client, "v1/devices/me/attributes", cJSON_Print(root), 0, 1, 0);
                esp_mqtt5_client_delete_user_property(publish_property.user_property);
                publish_property.user_property = NULL;
                ESP_LOGI(TAG, "Sending OTA configuration to [v1/devices/me/attributes], msg_id=%d", msg_id);
                cJSON_Delete(root);

                /* Send current status (client attributes) to ThingsBoard */
                esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                esp_mqtt5_client_set_publish_property(client, &publish_property);
                root = cJSON_CreateObject();                                                
                memset(&json_item_value, 0, sizeof(json_item_value));
                sprintf(json_item_value, "%s %s", app_info.date, app_info.time);
                cJSON_AddStringToObject(root, "fw_build", json_item_value);
                const esp_partition_pos_t running_pos  = {
                    .offset = partition_info_ptr->address,
                    .size = partition_info_ptr->size,
                };
                esp_image_metadata_t data;
                data.start_addr = running_pos.offset;
                esp_image_verify(ESP_IMAGE_VERIFY, &running_pos, &data);
                memset(&json_item_value, 0, sizeof(json_item_value));
                sprintf(json_item_value, "%ld", data.image_len);
                cJSON_AddStringToObject(root, "fw_size", json_item_value);
                cJSON_AddStringToObject(root, "fw_sdk_version", app_info.idf_ver);
                cJSON_AddStringToObject(root, "fw_running_partition_name", partition_info_ptr->label);
                memset(&json_item_value, 0, sizeof(json_item_value));
                sprintf(json_item_value, "%ld", partition_info_ptr->size);
                cJSON_AddStringToObject(root, "fw_running_partition_size", json_item_value);
                uint8_t mac_addr[6] = {0};
                esp_efuse_mac_get_default((uint8_t*)&mac_addr);
                if (atl_config.wifi.mode == ATL_WIFI_AP_MODE) {
                    mac_addr[5]++;
                } 
                sprintf(json_item_value, "%02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
                cJSON_AddStringToObject(root, "wifi_mac_addr", json_item_value);
                esp_reset_reason_t reset_reason;
                reset_reason = esp_reset_reason();
                if (reset_reason == ESP_RST_UNKNOWN) {
                    cJSON_AddStringToObject(root, "last_reboot_reason", "Reset reason can not be determined");
                } else if (reset_reason == ESP_RST_POWERON) {                    
                    cJSON_AddStringToObject(root, "last_reboot_reason", "Reset due to power-on event");
                } else if (reset_reason == ESP_RST_EXT) {
                    cJSON_AddStringToObject(root, "last_reboot_reason", "Reset by external pin");
                } else if (reset_reason == ESP_RST_SW) {
                    cJSON_AddStringToObject(root, "last_reboot_reason", "Software reset");
                } else if (reset_reason == ESP_RST_PANIC) {
                    cJSON_AddStringToObject(root, "last_reboot_reason", "Software reset due to exception/panic");
                } else if (reset_reason == ESP_RST_INT_WDT) {
                    cJSON_AddStringToObject(root, "last_reboot_reason", "Reset (software or hardware) due to interrupt watchdog");
                } else if (reset_reason == ESP_RST_TASK_WDT) {
                    cJSON_AddStringToObject(root, "last_reboot_reason", "Reset due to task watchdog");
                } else if (reset_reason == ESP_RST_WDT) {
                    cJSON_AddStringToObject(root, "last_reboot_reason", "Reset due to other watchdogs");
                } else if (reset_reason == ESP_RST_DEEPSLEEP) {
                    cJSON_AddStringToObject(root, "last_reboot_reason", "Reset after exiting deep sleep mode");
                } else if (reset_reason == ESP_RST_BROWNOUT) {
                    cJSON_AddStringToObject(root, "last_reboot_reason", "Brownout reset (software or hardware)");
                } else if (reset_reason == ESP_RST_SDIO) {
                    cJSON_AddStringToObject(root, "last_reboot_reason", "Reset over SDIO");
                }     
                cJSON_AddStringToObject(root, "system.led_behaviour", atl_led_get_behaviour_str(atl_config.system.led_behaviour));
                msg_id = esp_mqtt_client_publish(client, "v1/devices/me/attributes", cJSON_Print(root), 0, 1, 0);
                esp_mqtt5_client_delete_user_property(publish_property.user_property);
                publish_property.user_property = NULL;
                ESP_LOGI(TAG, "Sending device status to [v1/devices/me/attributes], msg_id=%d", msg_id);
                cJSON_Delete(root);

                /* Request from ThingsBoard the firmware info */
                // if (atl_config.ota.behaviour != ATL_OTA_BEHAVIOUR_DISABLED) {
                //     esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                //     esp_mqtt5_client_set_publish_property(client, &publish_property);
	            //     root = cJSON_CreateObject();
                //     cJSON_AddStringToObject(root, "sharedKeys", "fw_checksum,fw_checksum_algorithm,fw_size,fw_title,fw_version");
                //     request_id = msg_id + 1;
                //     char fw_topic[60] = {};
                //     sprintf(fw_topic, "v1/devices/me/attributes/request/%d", request_id);          
                //     msg_id = esp_mqtt_client_publish(client, fw_topic, cJSON_Print(root), 0, 1, 0);
                //     esp_mqtt5_client_delete_user_property(publish_property.user_property);
                //     publish_property.user_property = NULL;
                //     ESP_LOGI(TAG, "Requesting firmware information to [%s], msg_id=%d", fw_topic, msg_id);
                //     cJSON_Delete(root);
                // }
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");            
            break;
        case MQTT_EVENT_SUBSCRIBED:            
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED [msg_id=%d]", event->msg_id);                        
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGW(TAG, "MQTT_EVENT_UNSUBSCRIBED [msg_id=%d]", event->msg_id);            
            break;        
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED [msg_id=%d]", event->msg_id);            
            break;        
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA from [%.*s],  msg_id=%d", event->topic_len, event->topic, event->msg_id);
            
            // TODO: Check some unexpected behaviours
            if (event->topic == NULL) {
                ESP_LOGW(TAG, "MQTT_EVENT_DATA [null topic]");
                break;
            } else if (event->data_len == 0) {
                ESP_LOGW(TAG, "MQTT_EVENT_DATA [message empty]");
                break;
            }

            //print_user_property(event->property->user_property);
            // ESP_LOGI(TAG, "payload_format_indicator is %d", event->property->payload_format_indicator);
            // ESP_LOGI(TAG, "response_topic is %.*s", event->property->response_topic_len, event->property->response_topic);
            // ESP_LOGI(TAG, "correlation_data is %.*s", event->property->correlation_data_len, event->property->correlation_data);
            // ESP_LOGI(TAG, "content_type is %.*s", event->property->content_type_len, event->property->content_type);
            // ESP_LOGI(TAG, "topic: %.*s", event->topic_len, event->topic);
            // ESP_LOGI(TAG, "data: %.*s", event->data_len, event->data); 

            /*** Parse message in accordance with MQTT topic ***/
            /* Attributes updated from server */
            if (strncmp(event->topic, "v1/devices/me/attributes", event->topic_len) == 0) {
                ESP_LOGI(TAG, "Configuration updated from server: %.*s", event->data_len, event->data);
                
                /* Parse JSON message */
                root = NULL;                
                root = cJSON_ParseWithLength(event->data, event->data_len);
                if (root == NULL) {                    
                    ESP_LOGW(TAG, "Invalid JSON message!");
                    break;
                }                            

                /* Checking MQTT Client configuration */                
                if (cJSON_HasObjectItem(root, "mqtt_client.mode") == true) {                    
                    cJSON *key = cJSON_GetObjectItem(root, "mqtt_client.mode");
                    if (cJSON_GetNumberValue(key) == ATL_MQTT_DISABLED) {
                        atl_config.mqtt_client.mode = ATL_MQTT_DISABLED;
                    } else if (cJSON_GetNumberValue(key) == ATL_MQTT_AGROTECHLAB_CLOUD) {
                        atl_config.mqtt_client.mode = ATL_MQTT_AGROTECHLAB_CLOUD;
                    } else if (cJSON_GetNumberValue(key) == ATL_MQTT_AGROTECHLAB_CLOUD) {
                        atl_config.mqtt_client.mode = ATL_MQTT_AGROTECHLAB_CLOUD;
                    } else {
                        ESP_LOGW(TAG, "Unknown value [mqtt_client.mode:%d]", (uint16_t)cJSON_GetNumberValue(key));
                    }
                }
                if (cJSON_HasObjectItem(root, "mqtt_client.broker_address") == true) {
                    cJSON *key = cJSON_GetObjectItem(root, "mqtt_client.broker_address");
                    strncpy((char*)&atl_config.mqtt_client.broker_address, (char*)cJSON_GetStringValue(key), sizeof(atl_config.mqtt_client.broker_address));
                }
                if (cJSON_HasObjectItem(root, "mqtt_client.broker_port") == true) {
                    cJSON *key = cJSON_GetObjectItem(root, "mqtt_client.broker_port");
                    atl_config.mqtt_client.broker_port = (uint16_t)cJSON_GetNumberValue(key);
                }
                if (cJSON_HasObjectItem(root, "mqtt_client.transport") == true) {                    
                    cJSON *key = cJSON_GetObjectItem(root, "mqtt_client.transport");
                    if (cJSON_GetNumberValue(key) == MQTT_TRANSPORT_OVER_TCP) {
                        atl_config.mqtt_client.transport = MQTT_TRANSPORT_OVER_TCP;
                    } else if (cJSON_GetNumberValue(key) == MQTT_TRANSPORT_OVER_SSL) {
                        atl_config.mqtt_client.transport = MQTT_TRANSPORT_OVER_SSL;
                    } else {
                        ESP_LOGW(TAG, "Unknown value [mqtt_client.transport:%d]", (uint16_t)cJSON_GetNumberValue(key));
                    }
                }
                if (cJSON_HasObjectItem(root, "mqtt_client.disable_cn_check") == true) {                    
                    if (cJSON_IsTrue(cJSON_GetObjectItem(root, "mqtt_client.disable_cn_check"))) {
                        atl_config.mqtt_client.disable_cn_check = true;
                    } else {
                        atl_config.mqtt_client.disable_cn_check = false;
                    }                    
                }
                if (cJSON_HasObjectItem(root, "mqtt_client.user") == true) {
                    cJSON *key = cJSON_GetObjectItem(root, "mqtt_client.user");
                    strncpy((char*)&atl_config.mqtt_client.user, (char*)cJSON_GetStringValue(key), sizeof(atl_config.mqtt_client.user));
                }
                if (cJSON_HasObjectItem(root, "mqtt_client.pass") == true) {
                    cJSON *key = cJSON_GetObjectItem(root, "mqtt_client.pass");                    
                    strncpy((char*)&atl_config.mqtt_client.pass, (char*)cJSON_GetStringValue(key), sizeof(atl_config.mqtt_client.pass));
                }
                if (cJSON_HasObjectItem(root, "mqtt_client.qos") == true) {                    
                    cJSON *key = cJSON_GetObjectItem(root, "mqtt_client.qos");
                    if (cJSON_GetNumberValue(key) == ATL_MQTT_QOS0) {
                        atl_config.mqtt_client.qos = ATL_MQTT_QOS0;
                    } else if (cJSON_GetNumberValue(key) == ATL_MQTT_QOS1) {
                        atl_config.mqtt_client.qos = ATL_MQTT_QOS1;
                    } else if (cJSON_GetNumberValue(key) == ATL_MQTT_QOS2) {
                        atl_config.mqtt_client.qos = ATL_MQTT_QOS2;
                    } else {
                        ESP_LOGW(TAG, "Unknown value [mqtt_client.qos:%d]", (uint16_t)cJSON_GetNumberValue(key));
                    }
                }                                

                /* Checking WiFi configuration */                
                if (cJSON_HasObjectItem(root, "wifi.startup_mode") == true) {                    
                    // cJSON *key = cJSON_GetObjectItem(root, "wifi.startup_mode");
                    // if (cJSON_GetNumberValue(key) == X200_WIFI_DISABLED) {
                    //     x200_config.wifi.startup_mode = X200_WIFI_DISABLED;
                    // } else if (cJSON_GetNumberValue(key) == X200_AP_MODE) {
                    //     x200_config.wifi.startup_mode = X200_AP_MODE;
                    // } else if (cJSON_GetNumberValue(key) == X200_STA_MODE) {
                    //     x200_config.wifi.startup_mode = X200_STA_MODE;
                    // } else {
                    //     ESP_LOGW(TAG, "Unknown value [wifi.startup_mode:%d]", (uint16_t)cJSON_GetNumberValue(key));
                    // }
                }
                if (cJSON_HasObjectItem(root, "wifi.sta_ssid") == true) {
                    cJSON *key = cJSON_GetObjectItem(root, "wifi.sta_ssid");
                    strncpy((char*)&atl_config.wifi.sta_ssid, (char*)cJSON_GetStringValue(key), sizeof(atl_config.wifi.sta_ssid));
                }
                if (cJSON_HasObjectItem(root, "wifi.sta_pass") == true) {
                    cJSON *key = cJSON_GetObjectItem(root, "wifi.sta_pass");
                    strncpy((char*)&atl_config.wifi.sta_pass, (char*)cJSON_GetStringValue(key), sizeof(atl_config.wifi.sta_pass));
                }                
                
                /* Checking Fimware Update configuration */                
                // if (cJSON_HasObjectItem(root, "ota.behaviour") == true) {                    
                //     cJSON *key = cJSON_GetObjectItem(root, "ota.behaviour");
                //     if (cJSON_GetNumberValue(key) == ATL_OTA_BEHAVIOUR_DISABLED) {
                //         atl_config.ota.behaviour = ATL_OTA_BEHAVIOUR_DISABLED;
                //     } else if (cJSON_GetNumberValue(key) == ATL_OTA_BEHAVIOUR_VERIFY_NOTIFY) {
                //         atl_config.ota.behaviour = ATL_OTA_BEHAVIOUR_VERIFY_NOTIFY;
                //     } else if (cJSON_GetNumberValue(key) == ATL_OTA_BEHAVIOU_DOWNLOAD) {
                //         atl_config.ota.behaviour = ATL_OTA_BEHAVIOU_DOWNLOAD;
                //     } else if (cJSON_GetNumberValue(key) == ATL_OTA_BEHAVIOU_DOWNLOAD_REBOOT) {
                //         atl_config.ota.behaviour = ATL_OTA_BEHAVIOU_DOWNLOAD_REBOOT;
                //     } else {
                //         ESP_LOGW(TAG, "Unknown value [ota.behaviour:%d]", (uint16_t)cJSON_GetNumberValue(key));
                //     }
                // }                

                /* Release memory */                
                cJSON_Delete(root);
                
                /* Commit configuration to NVS */
                atl_config_commit_nvs();    

                /* Restart ATL100 device */
                // ESP_LOGW(TAG, ">>> Rebooting ATL200!");
                // atl_led_builtin_blink(10, 100, 255, 69, 0);
                // esp_restart();               
            } 
            
            /* Response of previous request attributes */
            else if (strstr(event->topic, "v1/devices/me/attributes/response/") != NULL) {         
                
                /* Check if it is a response of previous request_id */
                char check_event_topic[60] = {};
                sprintf(check_event_topic, "v1/devices/me/attributes/response/%d", request_id);
                if (strncmp(event->topic, check_event_topic, event->topic_len) != 0) {
                    ESP_LOGW(TAG, "Received response not requested or out of order!");
                    break;
                }

                /* Check if it is an empty message */
                sprintf(check_event_topic, "v1/devices/me/attributes/response/%d", request_id);
                if ((strncmp(event->data, "{}", event->data_len) == 0) || (strncmp(event->data, "[]", event->data_len) == 0)) {
                    ESP_LOGW(TAG, "Received an empty JSON message: %.*s", event->data_len, event->data);
                    break;
                }

                /* Parse JSON message */
                root = NULL;            
                root = cJSON_ParseWithLength(event->data, event->data_len);
                if (root == NULL) {
                    //const char *error_ptr = cJSON_GetErrorPtr();
                    ESP_LOGW(TAG, "Invalid JSON message!");
                    break;
                }
                cJSON *shared = cJSON_GetObjectItem(root, "shared");
                cJSON *fw_version = cJSON_GetObjectItem(shared, "fw_version");
                cJSON *fw_title = cJSON_GetObjectItem(shared, "fw_title");
                // cJSON *fw_checksum = cJSON_GetObjectItem(shared, "fw_checksum");
                // cJSON *fw_checksum_algorithm = cJSON_GetObjectItem(shared, "fw_checksum_algorithm");
                cJSON *fw_size = cJSON_GetObjectItem(shared, "fw_size");

                esp_app_desc_t app_info;
                const esp_partition_t *partition_info_ptr;
                partition_info_ptr = esp_ota_get_running_partition();
                esp_ota_get_partition_description(partition_info_ptr, &app_info);
                
                // Check if running firmware is updated
                if ((strcmp(cJSON_GetStringValue(fw_title), app_info.project_name) == 0) && (strcmp(cJSON_GetStringValue(fw_version), app_info.version) == 0)) {
                    esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                    esp_mqtt5_client_set_publish_property(client, &publish_property);
                    cJSON *response;
                    response = cJSON_CreateObject();
                    cJSON_AddStringToObject(response, "fw_state", "UPDATED");
                    msg_id = esp_mqtt_client_publish(client, "v1/devices/me/telemetry", cJSON_Print(response), 0, 1, 0);
                    esp_mqtt5_client_delete_user_property(publish_property.user_property);
                    publish_property.user_property = NULL;
                    ESP_LOGI(TAG, "Sent publish successful to [v1/devices/me/telemetry], msg_id=%d", msg_id);
                    cJSON_Delete(response);
                }
                
                // Check if running firmware is out of date
                else if ((strcmp(cJSON_GetStringValue(fw_title), app_info.project_name) == 0) && (strcmp(cJSON_GetStringValue(fw_version), app_info.version) != 0)) {
                    ESP_LOGW(TAG, "Current firmware is out of date! Current: %s - Server: %s", app_info.version, cJSON_GetStringValue(fw_version));

                    // Set device to DOWNLOADING state
                    esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                    esp_mqtt5_client_set_publish_property(client, &publish_property);
                    cJSON *response;
                    response = cJSON_CreateObject();
                    cJSON_AddStringToObject(response, "fw_state", "DOWNLOADING");
                    msg_id = esp_mqtt_client_publish(client, "v1/devices/me/telemetry", cJSON_Print(response), 0, 1, 0);
                    esp_mqtt5_client_delete_user_property(publish_property.user_property);
                    publish_property.user_property = NULL;
                    ESP_LOGI(TAG, "Sent publish successful to [v1/devices/me/telemetry], msg_id=%d", msg_id);
                    cJSON_Delete(response);

                    // Get the first chunk of new firmware from server
                    chunk_count = ceil(cJSON_GetNumberValue(fw_size)/chunk_size);
                    ESP_LOGW(TAG, "Downloading firmware %s from server!", cJSON_GetStringValue(fw_version));
                    ESP_LOGW(TAG, "Total size: %lu bytes (Chunk size: %lu bytes - Total chunks: %lu)", (uint32_t)cJSON_GetNumberValue(fw_size), chunk_size, chunk_count);
                    esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                    esp_mqtt5_client_set_publish_property(client, &publish_property);
                    char fw_topic[60] = {};
                    char fw_chunk_size[10] = {};
                    request_id = msg_id + 1;
                    sprintf(fw_topic, "v2/fw/request/%d/chunk/%lu", request_id, chunk_current);
                    sprintf(fw_chunk_size, "%lu", chunk_size);
                    msg_id = esp_mqtt_client_publish(client, fw_topic, fw_chunk_size, 0, 1, 0);
                    esp_mqtt5_client_delete_user_property(publish_property.user_property);
                    publish_property.user_property = NULL;
                    ESP_LOGI(TAG, "Sent publish successful to [%s], msg_id=%d", fw_topic, msg_id);
                } else {
                    ESP_LOGW(TAG, "Unknown firmware version!");
                    ESP_LOGW(TAG, "Server response: %.*s", event->data_len, event->data);
                }
                cJSON_Delete(root);
            }

            /* Check if is a firmware chunk data */
            else if (strstr(event->topic, "v2/fw/response/") != NULL) {
                
                /* Check if it is a response of previous request_id */
                char check_event_topic[60] = {};
                sprintf(check_event_topic, "v2/fw/response/%d/chunk/", request_id);
                if (strncmp(event->topic, check_event_topic, event->topic_len) != 0) {
                    ESP_LOGW(TAG, "Firmware chunk not requested or out of order!");
                    break;
                }
                
                ESP_LOGI(TAG, "Chunk %lu/%lu received! (Size: %d bytes)", chunk_current + 1, chunk_count, event->data_len);
                
                /* If it is the first chunk of new firmware, prepare new OTA partition */
                if (chunk_current == 0) {
                    
                    /* Check if boot configured partition is the same of running partition */
                    const esp_partition_t *boot_partition = esp_ota_get_boot_partition();
                    const esp_partition_t *running_partition = esp_ota_get_running_partition();
                    if ((boot_partition != NULL) && (running_partition != NULL)) {
                        if (boot_partition != running_partition) {
                            ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08lx, but running from offset 0x%08lx",
                                boot_partition->address, running_partition->address);
                            ESP_LOGW(TAG, "This can happen if either the OTA boot data or preferred boot image become corrupted somehow!");
                        }
                    } else {
                        ESP_LOGE(TAG, "Fail getting boot/running partition! Boot: %s Running: %s", boot_partition->label, running_partition->label);
                    }
                    
                    /* Get pointer to next partition to write new firmware */
                    update_partition = esp_ota_get_next_update_partition(NULL);
                    if (update_partition == NULL) {
                        ESP_LOGE(TAG, "Fail getting update partition!");
                        // goto fw_update_failed;
                    } 

                    /* Erase new partition to write new firmware */
                    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "OTA begin failed! Error: (%d) %s", err, esp_err_to_name(err));
                        esp_ota_abort(update_handle);
                        
                        // Set ThingsBoard to FAILED status
                        esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                        esp_mqtt5_client_set_publish_property(client, &publish_property);
                        cJSON *response;
                        response = cJSON_CreateObject();
                        cJSON_AddStringToObject(response, "fw_state", "FAILED");
                        msg_id = esp_mqtt_client_publish(client, "v1/devices/me/telemetry", cJSON_Print(response), 0, 1, 0);
                        esp_mqtt5_client_delete_user_property(publish_property.user_property);
                        publish_property.user_property = NULL;
                        ESP_LOGI(TAG, "Sent publish successful to [v1/devices/me/telemetry], msg_id=%d", msg_id);
                        cJSON_Delete(response);
                    }
                    ESP_LOGI(TAG, "OTA begin succeeded!");
                }
                
                /* Write the first chunk at next partition */
                err = esp_ota_write(update_handle, (const void*)event->data, event->data_len);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Fail writing chunk %lu/%lu! Error: (%d) %s", chunk_current, chunk_count, err, esp_err_to_name(err));
                    esp_ota_abort(update_handle);

                    /* Set ThingsBoard firmware update to FAILED status */
                    esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                    esp_mqtt5_client_set_publish_property(client, &publish_property);
                    cJSON *response;
                    response = cJSON_CreateObject();
                    cJSON_AddStringToObject(response, "fw_state", "FAILED");
                    msg_id = esp_mqtt_client_publish(client, "v1/devices/me/telemetry", cJSON_Print(response), 0, 1, 0);
                    esp_mqtt5_client_delete_user_property(publish_property.user_property);
                    publish_property.user_property = NULL;
                    ESP_LOGI(TAG, "Sent publish successful to [v1/devices/me/telemetry], msg_id=%d", msg_id);
                    cJSON_Delete(response);                     
                }

                /* Update chunk count */
                chunk_current++;

                /* If not all new firmware chunks was received, request next chunk */
                if (chunk_current < chunk_count) {
                    esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                    esp_mqtt5_client_set_publish_property(client, &publish_property);
                    char fw_topic[60] = {};
                    char fw_chunk_size[10] = {};
                    request_id = msg_id + 1;
                    sprintf(fw_topic, "v2/fw/request/%d/chunk/%lu", request_id, chunk_current);
                    sprintf(fw_chunk_size, "%lu", chunk_size);
                    msg_id = esp_mqtt_client_publish(client, fw_topic, fw_chunk_size, 0, 1, 0);
                    esp_mqtt5_client_delete_user_property(publish_property.user_property);
                    publish_property.user_property = NULL;
                    ESP_LOGI(TAG, "Sent publish successful to [%s], msg_id=%d", fw_topic, msg_id);
                
                } 
                
                /* If all new firmware chunks was received, update status and partition boot order */
                else if (chunk_current == chunk_count) {

                    /* Set device to DOWNLOADED state */
                    esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                    esp_mqtt5_client_set_publish_property(client, &publish_property);
                    cJSON *response;
                    response = cJSON_CreateObject();
                    cJSON_AddStringToObject(response, "fw_state", "DOWNLOADED");
                    msg_id = esp_mqtt_client_publish(client, "v1/devices/me/telemetry", cJSON_Print(response), 0, 1, 0);
                    esp_mqtt5_client_delete_user_property(publish_property.user_property);
                    publish_property.user_property = NULL;
                    ESP_LOGI(TAG, "Sent publish successful to [v1/devices/me/telemetry], msg_id=%d", msg_id);
                    cJSON_Delete(response);

                    /* Finalize partition write and verify checksum of new firmware */
                    err = esp_ota_end(update_handle);
                    if (err != ESP_OK) {
                        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
                            ESP_LOGE(TAG, "Image validation failed, image is corrupted!");
                        } else {
                            ESP_LOGE(TAG, "OTA end failed! Error: (%d) %s", err, esp_err_to_name(err));
                        }

                        /* Set ThingsBoard firmware update to FAILED status */
                        esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                        esp_mqtt5_client_set_publish_property(client, &publish_property);
                        cJSON *response;
                        response = cJSON_CreateObject();
                        cJSON_AddStringToObject(response, "fw_state", "FAILED");
                        msg_id = esp_mqtt_client_publish(client, "v1/devices/me/telemetry", cJSON_Print(response), 0, 1, 0);
                        esp_mqtt5_client_delete_user_property(publish_property.user_property);
                        publish_property.user_property = NULL;
                        ESP_LOGI(TAG, "Sent publish successful to [v1/devices/me/telemetry], msg_id=%d", msg_id);
                        cJSON_Delete(response);
                    } else {

                        /* Get new partition and verify checksum of firmware writed at new partition */
                        esp_app_desc_t new_app_info;
                        err = esp_ota_get_partition_description(update_partition, &new_app_info);
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "Fail getting update app info! Error: (%d) %s", err, esp_err_to_name(err));
                        } else {
                            uint8_t sha_256[32] = { 0 };  /* 32 bytes for SHA-256 digest length */
                            err = esp_partition_get_sha256(update_partition, sha_256);
                            if (err != ESP_OK) {
                                ESP_LOGE(TAG, "Fail getting update SHA-256! Error: (%d) %s", err, esp_err_to_name(err));
                            } else {
                                char hash_print[32 * 2 + 1];
                                memset(&hash_print, 0, sizeof(hash_print));
                                for (uint8_t i = 0; i < 32; ++i) {
                                    sprintf(&hash_print[i * 2], "%02x", sha_256[i]);
                                }
                                ESP_LOGI(TAG, "New firmware (%s at 0x%08lx - SHA256: %s)", update_partition->label, update_partition->address, hash_print);

                                /* If SHA256 checked, notify ThingsBoard that firmware was VERIFIED */
                                esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                                esp_mqtt5_client_set_publish_property(client, &publish_property);
                                cJSON *response;
                                response = cJSON_CreateObject();
                                cJSON_AddStringToObject(response, "fw_state", "VERIFIED");
                                msg_id = esp_mqtt_client_publish(client, "v1/devices/me/telemetry", cJSON_Print(response), 0, 1, 0);
                                esp_mqtt5_client_delete_user_property(publish_property.user_property);
                                publish_property.user_property = NULL;
                                ESP_LOGI(TAG, "Sent publish successful to [v1/devices/me/telemetry], msg_id=%d", msg_id);
                                cJSON_Delete(response);

                                /* Notify ThingsBoard that X200 will apply new firmware */
                                esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                                esp_mqtt5_client_set_publish_property(client, &publish_property);
                                //cJSON *response;
                                response = cJSON_CreateObject();
                                cJSON_AddStringToObject(response, "fw_state", "UPDATING");
                                msg_id = esp_mqtt_client_publish(client, "v1/devices/me/telemetry", cJSON_Print(response), 0, 1, 0);
                                esp_mqtt5_client_delete_user_property(publish_property.user_property);
                                publish_property.user_property = NULL;
                                ESP_LOGI(TAG, "Sent publish successful to [v1/devices/me/telemetry], msg_id=%d", msg_id);
                                cJSON_Delete(response);

                                /* Update boot partition in bootloader */
                                err = esp_ota_set_boot_partition(update_partition);
                                if (err != ESP_OK) {
                                    ESP_LOGE(TAG, "OTA set boot partition failed! Error: (%d) %s", err, esp_err_to_name(err));
                                    
                                    /* Set ThingsBoard firmware update to FAILED status */
                                    esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
                                    esp_mqtt5_client_set_publish_property(client, &publish_property);
                                    cJSON *response;
                                    response = cJSON_CreateObject();
                                    cJSON_AddStringToObject(response, "fw_state", "FAILED");
                                    msg_id = esp_mqtt_client_publish(client, "v1/devices/me/telemetry", cJSON_Print(response), 0, 1, 0);
                                    esp_mqtt5_client_delete_user_property(publish_property.user_property);
                                    publish_property.user_property = NULL;
                                    ESP_LOGI(TAG, "Sent publish successful to [v1/devices/me/telemetry], msg_id=%d", msg_id);
                                    cJSON_Delete(response);

                                } else {
                                    /* Restart ATL100 */
                                    ESP_LOGW(TAG, "Rebooting ATL100!");
                                    esp_restart();
                                }
                            }
                        }
                    }
                }
            } 
            break;
        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");            
            break;
        case MQTT_EVENT_DELETED:
            ESP_LOGW(TAG, "MQTT_EVENT_DELETED [msg_id=%d]", event->msg_id);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");                        
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                //ESP_LOGE(TAG, "Error: %s", strerror(event->error_handle->esp_transport_sock_errno));
                ESP_LOGE(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGE(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
                ESP_LOGE(TAG, "Last captured errno : %d (%s)",  event->error_handle->esp_transport_sock_errno,
                     strerror(event->error_handle->esp_transport_sock_errno));
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGE(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_SUBSCRIBE_FAILED) {
                ESP_LOGE(TAG, "Subscribed error: 0x%x", event->error_handle->connect_return_code);
            } else {
                ESP_LOGE(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
            }
            break;
        default:
            ESP_LOGW(TAG, "MQTT_EVENT_UNKNOWN [event_id=%d]", event->event_id);
            break;
    }
}

void atl_mqtt_init(void) {
    
    /* MQTT5 connection property */
    esp_mqtt5_connection_property_config_t connect_property = {
        .session_expiry_interval = 10,
        .maximum_packet_size = 1024,
        .receive_maximum = 65535,
        // .topic_alias_maximum = 2,
        // .request_resp_info = true,
        // .request_problem_info = true,
        // .will_delay_interval = 10,
        // .payload_format_indicator = true,
        // .message_expiry_interval = 10,
        // .response_topic = "/test/response",
        // .correlation_data = "123456",
        // .correlation_data_len = 6,
    };

    /* Make a local copy of MQTT client configuration */
    atl_config_mqtt_client_t config_mqtt_client;
    memset(&config_mqtt_client, 0, sizeof(atl_config_mqtt_client_t));
    if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(&config_mqtt_client, &atl_config.mqtt_client, sizeof(atl_config_mqtt_client_t));
        xSemaphoreGive(atl_config_mutex);
    }
    else {
        ESP_LOGW(TAG, "[%s:%d] Fail to get configuration mutex!", __func__, __LINE__);
    }

    /* MQTT5 configuration */
    esp_mqtt_client_config_t mqtt5_cfg = {
        .broker.address.hostname = (const char*)&config_mqtt_client.broker_address,
        .broker.address.port = config_mqtt_client.broker_port,
        .broker.address.transport = config_mqtt_client.transport,
        // .broker.verification.certificate = (const char *)mqtt_cert_start,
        // .broker.verification.certificate_len = mqtt_cert_end - mqtt_cert_start,
        // .broker.verification.skip_cert_common_name_check = mqtt_client_config.disable_cn_check,
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
        .network.disable_auto_reconnect = true,
        .credentials.username = (const char*)&config_mqtt_client.user,
        .credentials.authentication.password = (const char*)&config_mqtt_client.pass,
        //.session.last_will.topic = "/topic/will",
        //.session.last_will.msg = "i will leave",
        //.session.last_will.msg_len = 12,
        //.session.last_will.qos = 1,
        //.session.last_will.retain = true,
    };

    if (mqtt5_cfg.broker.address.transport == MQTT_TRANSPORT_OVER_TCP) {
        ESP_LOGI(TAG, "Starting MQTT client [mqtt://%s:%lu]", mqtt5_cfg.broker.address.hostname, mqtt5_cfg.broker.address.port);
    } else if (mqtt5_cfg.broker.address.transport == MQTT_TRANSPORT_OVER_SSL) {
        ESP_LOGI(TAG, "Starting MQTT client [mqtts://%s:%lu]", mqtt5_cfg.broker.address.hostname, mqtt5_cfg.broker.address.port);
        // mqtt5_cfg.broker.verification.certificate = (const char *)controlle_cloud_cert_start;
        // mqtt5_cfg.broker.verification.certificate_len = controlle_cloud_cert_end - controlle_cloud_cert_start;
        mqtt5_cfg.broker.verification.skip_cert_common_name_check = atl_config.mqtt_client.disable_cn_check;
    } else if (mqtt5_cfg.broker.address.transport == MQTT_TRANSPORT_OVER_WS) {
        ESP_LOGI(TAG, "Starting MQTT client [ws://%s:%lu]", mqtt5_cfg.broker.address.hostname, mqtt5_cfg.broker.address.port);
    } else if (mqtt5_cfg.broker.address.transport == MQTT_TRANSPORT_OVER_WSS) {
        ESP_LOGI(TAG, "Starting MQTT client [wss://%s:%lu]", mqtt5_cfg.broker.address.hostname, mqtt5_cfg.broker.address.port);
    }
    
    /* Creates client handle */
    client = esp_mqtt_client_init(&mqtt5_cfg);

    /* Set connection properties and user properties */
    esp_mqtt5_client_set_user_property(&connect_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
    esp_mqtt5_client_set_user_property(&connect_property.will_user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
    esp_mqtt5_client_set_connect_property(client, &connect_property);

    /* If you call esp_mqtt5_client_set_user_property to set user properties, DO NOT forget to delete them.
     * esp_mqtt5_client_set_connect_property will malloc buffer to store the user_property and you can delete it after
     */
    esp_mqtt5_client_delete_user_property(connect_property.user_property);
    esp_mqtt5_client_delete_user_property(connect_property.will_user_property);

    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, atl_mqtt5_event_handler, NULL);
    esp_mqtt_client_start(client);
}

/**
 * @brief Send telemetry data to MQTT broker
 * @param json - pointer to JSON object 
 * @return esp_err_t - If ERR_OK success.
 */
esp_err_t atl_mqtt_send_telemetry(cJSON *json) {
    esp_err_t err = ESP_OK;
    int msg_id = -1;

    /* Check if MQTT client is connected */
    if (client == NULL) {
        ESP_LOGW(TAG, "MQTT client not initialized!");
        return ESP_FAIL;
    }
    // if (client->state != MQTT_STATE_CONNECTED) {
    //     ESP_LOGW(TAG, "MQTT client not connected!");
    //     return ESP_FAIL;
    // }

    /* Check if JSON message is valid */
    if (json == NULL) {
        ESP_LOGW(TAG, "Invalid JSON message!");
        return ESP_FAIL;
    }

    /* Set user properties */
    esp_mqtt5_client_set_user_property(&publish_property.user_property, atl_user_property_arr, USE_PROPERTY_ARR_SIZE);
    esp_mqtt5_client_set_publish_property(client, &publish_property);

    /* Publish telemetry message */
    msg_id = esp_mqtt_client_publish(client, "v1/devices/me/telemetry", cJSON_Print(json), 0, 1, 0);
    ESP_LOGI(TAG, "Sending telemetry to [v1/devices/me/telemetry], msg_id=%d", msg_id);

    /* Delete user properties */
    esp_mqtt5_client_delete_user_property(publish_property.user_property);
    publish_property.user_property = NULL;

    return err;
}

/**
 * @brief Restart MQTT client
 * @return esp_err_t 
 */
esp_err_t atl_mqtt_client_restart(void) {
    esp_err_t err = ESP_OK;

    /* Stop MQTT client */
    err = esp_mqtt_client_stop(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Fail to stop MQTT client!");
    }

    /* Destroy MQTT client */
    err = esp_mqtt_client_destroy(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Fail to destroy MQTT client!");
    }

    /* Start new instance of MQTT client */
    atl_mqtt_init();
    atl_led_builtin_blink(6, 100, 0, 255, 0);

    return err;
}