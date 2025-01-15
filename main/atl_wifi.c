#include <string.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include "atl_config.h"
#include "atl_wifi.h"

/* Constants */
static const char *TAG = "atl-wifi";        /**< Module identification. */
const char *atl_wifi_mode_str[] = {         /**< WiFi mode string. */
    "ATL_WIFI_DISABLED",
    "ATL_WIFI_AP_MODE",
    "ATL_WIFI_STA_MODE",
};

/* Global variables */
EventGroupHandle_t s_wifi_event_group;      /* FreeRTOS event group to signal when we are connected */
volatile uint8_t conn_retry = 0;            /* Connection retry counter */

/* Global external variables */
extern atl_config_t atl_config;             /**< Global configuration variable. */

/**
 * @brief Get the wifi mode enum
 * @param mode_str 
 * @return Function enum
 */
atl_wifi_mode_e atl_wifi_get_mode(char* mode_str) {
    uint8_t i = 0;
    while (atl_wifi_mode_str[i] != NULL) {
        if (strcmp(mode_str, atl_wifi_mode_str[i]) == 0) {
            return i;
        } else {
            i++;
        }
    }
    return 255;
}

/**
 * @brief Get the wifi mode string object
 * @param mode 
 * @return Function enum const* 
 */
const char* atl_wifi_get_mode_str(atl_wifi_mode_e mode) {
    return atl_wifi_mode_str[mode];
}

/**
 * @fn atl_wifi_event_handler(void* handler_args, esp_event_base_t event_base, int32_t event_id, void* event_data)
 * @brief Event handler registered to receive WiFi events.
 * @details This function is called by the WiFi client event loop.
 * @param[in] handler_args - User data registered to the event.
 * @param[out] event_base - Event base for the handler (always WiFi Base).
 * @param[out] event_id - The id for the received event.
 * @param[out] event_data - The data for the event, esp_mqtt_event_handle_t.
 */
void atl_wifi_event_handler(void* handler_args, esp_event_base_t event_base, int32_t event_id, void* event_data) {

    /* Check if WiFi interface was started and then connect to AP */
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 
    
    /* Check if station was connected */
    else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t* event = (wifi_event_sta_connected_t*) event_data;
        conn_retry = 0;
        ESP_LOGI(TAG, "Connected at %s ("MACSTR")", event->ssid, MAC2STR(event->bssid));
    }

    /* Check if station was disconnected */
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGI(TAG, "Disconnected from %s ("MACSTR") reason: %d", event->ssid, MAC2STR(event->bssid), event->reason);
        if (conn_retry < atl_config.wifi.sta_max_conn_retry) {
            conn_retry++;
            ESP_LOGW(TAG, "Retry %d/%d to connect to the AP", conn_retry, atl_config.wifi.sta_max_conn_retry);
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG,"Connect to the AP fail");  
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);            
        }
    }

    /* Check if station got IP address */
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        conn_retry = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }    

    /* Check if some station connects to AP */
    else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
    } 

    /* Check if some station disconnects from AP */
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

/**
 * @fn atl_wifi_init_softap(void)
 * @brief Initialize WiFi interface in SoftAP mode.
 * @details Initialize WiFi interface in SoftAP mode.
 */
esp_err_t atl_wifi_init_softap(void) {
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "Starting ATL100 in AP mode!");

    /* Initialize loopback interface */
    err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail initializing WiFi loopback interface!");
        goto error_proc;
    }

    /* Initialize event loop */
    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail creating WiFi event loop!");
        goto error_proc;
    }

    /* Initialize default WiFi AP */
    esp_netif_create_default_wifi_ap();

    /* Get default WiFi station configuration */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    /* Initialize WiFi interface with default configuration */
    err = esp_wifi_init(&cfg); 
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail initializing WiFi with default configuration!");
        goto error_proc;
    }

    /* Register event handlers */ 
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &atl_wifi_event_handler, NULL, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail registering WiFi event handler!");
        goto error_proc;
    }
    
    /* Allocate memory to local copy of WIFI configuration */
    atl_config_wifi_t* config_wifi_local = calloc(1, sizeof(atl_config_wifi_t));
    if (config_wifi_local == NULL) {
        ESP_LOGE(TAG, "Fail allocating memory for WiFi configuration!");
        err = ESP_ERR_NO_MEM;
        goto error_proc;
    }

    /* Make a local copy of WIFI configuration */
    if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(config_wifi_local, &atl_config.wifi, sizeof(atl_config_wifi_t));
        
        /* Release cofiguration mutex */
        xSemaphoreGive(atl_config_mutex);
    }
    else {
        ESP_LOGE(TAG, "Fail to get configuration mutex!");
        err = ESP_ERR_TIMEOUT;
        free(config_wifi_local);
        goto error_proc;
    }

    /* Setup softAP WiFi config */
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    wifi_config.ap.ssid_len = strlen((const char *)config_wifi_local->ap_ssid);
    snprintf((char*)&wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid), "%s", config_wifi_local->ap_ssid);
    snprintf((char*)&wifi_config.ap.password, sizeof(wifi_config.ap.password), "%s", config_wifi_local->ap_pass);
    wifi_config.ap.channel = config_wifi_local->ap_channel;
    wifi_config.ap.max_connection = config_wifi_local->ap_max_conn;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_WPA3_PSK;
    wifi_config.ap.pmf_cfg.required = false;
    
    /* If no password was defined, network will be OPEN */
    if (strlen((const char *)wifi_config.ap.password) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    /* Setup WiFi to Access Point Mode */
    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail setting WiFi to Access Point mode!");
        goto error_proc;
    }

    /* Apply custom configuration */
    err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail applying custom configuration to WiFi interface!");
        goto error_proc;
    }

    /* Start WiFi interface */
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail starting WiFi interface!");
        goto error_proc;
    }

    /* Finish WiFi AP MODE initialization */
    ESP_LOGI(TAG, "WiFi (Access Point) started!");
    free(config_wifi_local);
    return err;

/* Error procedure */
error_proc:
    atl_led_set_color(255, 0, 0);
    ESP_LOGE(TAG, "Error: %d = %s", err, esp_err_to_name(err));        
    return err;
}

/**
 * @fn atl_wifi_init_sta(void)
 * @brief Initialize WiFi interface in STA mode.
 * @details Initialize WiFi interface in STA mode.
 */
esp_err_t atl_wifi_init_sta(void) {
    esp_err_t err = ESP_OK;
    s_wifi_event_group = xEventGroupCreate();
    ESP_LOGI(TAG, "Starting ATL100 in STA mode!");

    /* Initialize loopback interface */
    err = esp_netif_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail starting WiFi network interface!");
        goto error_proc;
    }

    /* Initialize event loop */
    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail creating WiFi event loop!");
        goto error_proc;
    }

    /* Initialize default WiFi station */
    esp_netif_create_default_wifi_sta();

    /* Get default WiFi station configuration */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    /* Initialize WiFi interface with default configuration */
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail initializing WiFi with default configuration!");
        goto error_proc;
    }
    
    /* Register event handlers to WiFi */
    esp_event_handler_instance_t instance_any_id;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &atl_wifi_event_handler, NULL, &instance_any_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail registering WiFi event handler!");
        goto error_proc;
    }

    /* Register event handlers to IP */
    esp_event_handler_instance_t instance_got_ip;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &atl_wifi_event_handler, NULL, &instance_got_ip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail registering IP event handler!");
        goto error_proc;
    }
    
    /* Allocate memory to local copy of WIFI configuration */
    atl_config_wifi_t* config_wifi_local = calloc(1, sizeof(atl_config_wifi_t));
    if (config_wifi_local == NULL) {
        ESP_LOGE(TAG, "Fail allocating memory for WiFi configuration!");
        err = ESP_ERR_NO_MEM;
        goto error_proc;
    }

    /* Make a local copy of WIFI configuration */
    if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(config_wifi_local, &atl_config.wifi, sizeof(atl_config_wifi_t));
        
        /* Release cofiguration mutex */
        xSemaphoreGive(atl_config_mutex);
    }
    else {
        ESP_LOGE(TAG, "Fail to get configuration mutex!");
        err = ESP_ERR_TIMEOUT;
        free(config_wifi_local);
        goto error_proc;
    }

    /* Setup softAP WiFi config */
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    wifi_config.sta.channel = config_wifi_local->sta_channel;
    snprintf((char*)&wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", config_wifi_local->sta_ssid);
    snprintf((char*)&wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", config_wifi_local->sta_pass);

    /* Setup WiFi to Station Mode */
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail setting WiFi to Station mode!");
        goto error_proc;
    }

    /* Apply custom configuration */
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail applying custom configuration to WiFi interface!");
        goto error_proc;
    }

    /* Start WiFi interface */
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fail starting WiFi interface!");
        goto error_proc;
    }
       
    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
    
    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to SSID %s with password %s", wifi_config.sta.ssid, wifi_config.sta.password);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect SSID %s with password %s", wifi_config.sta.ssid, wifi_config.sta.password);
        err = ESP_ERR_WIFI_BASE;
        goto error_proc;
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        err = ESP_ERR_INVALID_STATE;
        goto error_proc;
    }

    /* Finish WiFi STA MODE initialization */
    ESP_LOGI(TAG, "WiFi (Station) started!");
    free(config_wifi_local);
    return err;
 
/* Error procedure */
error_proc:
    atl_led_set_color(255, 0, 0);
    ESP_LOGE(TAG, "Error: %d = %s", err, esp_err_to_name(err));        
    return err;
}