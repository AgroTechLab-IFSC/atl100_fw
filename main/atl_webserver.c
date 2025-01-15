#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <esp_https_server.h>
#include <esp_tls_crypto.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <esp_mac.h>
#include <cJSON.h>
#include "atl_config.h"
#include "atl_led.h"
// #include "atl_mqtt.h"
#include "atl_webserver.h"
// #include "atl_telemetry.h"

/* Constants */
static const char *TAG = "atl-webserver";                               /**< Module identification. */
extern const char favicon_start[] asm("_binary_favicon_ico_start");     /**< Favicon file start. */
extern const char favicon_end[] asm("_binary_favicon_ico_end");         /**< Favicon file end. */
extern const char css_start[] asm("_binary_atl_css_start");             /**< CSS file start. */
extern const char css_end[] asm("_binary_atl_css_end");                 /**< CSS file end. */
extern const char js_start[] asm("_binary_atl_js_start");               /**< JS file start. */
extern const char js_end[] asm("_binary_atl_js_end");                   /**< JS file end. */
extern const char header_start[] asm("_binary_header_html_start");      /**< Header file start. */
extern const char header_end[] asm("_binary_header_html_end");          /**< Header file end. */
extern const char footer_start[] asm("_binary_footer_html_start");      /**< Footer file start. */
extern const char footer_end[] asm("_binary_footer_html_end");          /**< Footer file end. */
// extern const unsigned char servercert_start[] asm("_binary_cacert_pem_start");
// extern const unsigned char servercert_end[] asm("_binary_cacert_pem_end");
// extern const unsigned char prvtkey_pem_start[] asm("_binary_prvtkey_pem_start");
// extern const unsigned char prvtkey_pem_end[] asm("_binary_prvtkey_pem_end");
const char *atl_webserver_mode_str[] = {                                /**< Webserver mode string. */
    "ATL_WEBSERVER_DISABLED",
    "ATL_WEBSERVER_HTTP",
    "ATL_WEBSERVER_HTTPS",
};

/* External global variables */
extern atl_config_t atl_config;             /**< Global configuration variable. */
extern TaskHandle_t atl_dht_handle;         /**< DHT task handle */
extern TaskHandle_t atl_telemetry_handle;   /**< Telemetry task handle */

/**
 * @fn hex2int(char ch)
 * @brief Convert hexadecimal charactere to integer
 * @param[in] ch - character to be converted
 * @return -1 - invalid
 */
static int hex2int(char ch) {
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    return -1;
}

/**
 * @fn urldecode(char *src)
 * @brief Decode message from URL format
 * @param[in] src - message to be decoded
 * @return ESP error code
 */
static esp_err_t urldecode(char* src) {
    
    /* Check if not has % in source */
    if (strchr(src, '%') == NULL) {
        return ESP_OK;
    }

    /* Create auxiliary string */
    char* aux = malloc(strlen(src) + 1);
    if (aux == NULL) {
        return ESP_ERR_NO_MEM;
    }

    char *p, *q;
    for (p = src, q = aux; *p; p++) {
        if (*p == '%') {
            if (*(p + 1) && *(p + 2)) {
                /* Convert the two next hex char to one char */
                *q++ = hex2int(*(p + 1)) * 16 + hex2int(*(p + 2));
                p += 2;
            } else {
                /* Invalid sequence */
                return ESP_FAIL;
            }
        } else if (*p == '+') {
            /* Replaces '+' by space */
            *q++ = ' ';
        } else {
            /* Copy char directly */
            *q++ = *p;
        }
    }
    /* Finish the string */
    *q = '\0';

    /* Replace aux to source */
    memset(src, 0, strlen(src));
    memcpy(src, aux, strlen(aux));
    free(aux);
    return ESP_OK;
}

/**
 * @fn favicon_get_handler(httpd_req_t *req)
 * @brief GET handler for FAVICON file
 * @details HTTP GET Handler for FAVICON file
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t favicon_get_handler(httpd_req_t *req) {
    const uint32_t favicon_len = favicon_end - favicon_start;

    /* Reply favicon.ico */
    ESP_LOGD(TAG, "Sending favicon.ico");
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, favicon_start, favicon_len);

    return ESP_OK;
}

/**
 * @brief HTTP GET Handler for FAVICON file
 */
static const httpd_uri_t favicon = {
    .uri = "/favicon.ico",
    .method = HTTP_GET,
    .handler = favicon_get_handler
};

/**
 * @fn css_get_handler(httpd_req_t *req)
 * @brief GET handler for CSS file
 * @details HTTP GET Handler for CSS file
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t css_get_handler(httpd_req_t *req) {
    const uint32_t css_len = css_end - css_start;

    /* Reply CSS */
    ESP_LOGD(TAG, "Sending atl.css");
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, css_start, css_len);

    return ESP_OK;
}

/**
 * @brief HTTP GET Handler for CSS file
 */
static const httpd_uri_t css = {
    .uri = "/atl.css",
    .method = HTTP_GET,
    .handler = css_get_handler
};

/**
 * @fn js_get_handler(httpd_req_t *req)
 * @brief GET handler for JS file
 * @details HTTP GET Handler for JS file
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t js_get_handler(httpd_req_t *req) {
    const uint32_t js_len = js_end - js_start;

    /* Reply JavaScript */
    ESP_LOGD(TAG, "Sending atl.js");
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, js_start, js_len);

    return ESP_OK;
}

/**
 * @brief HTTP GET Handler for JS file
 */
static const httpd_uri_t js = {
    .uri = "/atl.js",
    .method = HTTP_GET,
    .handler = js_get_handler
};

/**
 * @fn http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
 * @brief 404 error handler
 * @details HTTP Error (404) Handler - Redirects all requests to the root page
 * @param[in] req - request
 * @param[in] err - HTTP error code
 * @return ESP error code
 */
static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err) {
    
    /* Set status */
    httpd_resp_set_status(req, "302 Temporary Redirect");

    /* Redirect to the root directory */
    httpd_resp_set_hdr(req, "Location", "/index.html");

    /* iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient. */
    httpd_resp_send(req, "Redirect to the home portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGW(TAG, "Redirecting request to root page!");
    return ESP_OK;
}

/**
 * @fn home_get_handler(httpd_req_t *req)
 * @brief GET handler for home webpage
 * @details HTTP GET Handler for home webpage
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t home_get_handler(httpd_req_t *req) {
    ESP_LOGD(TAG, "Sending index.html");

    /* Set response status, type and header */
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");

    /* Send header chunck */
    const size_t header_size = (header_end - header_start);
    httpd_resp_send_chunk(req, (const char *)header_start, header_size);

    /* Send article chunks */
    httpd_resp_sendstr_chunk(req, "<p style=\"text-align:center\">Welcome to ATL-100, a multiparametric telemetry device developed by ");
    httpd_resp_sendstr_chunk(req, "<a href=\"https://agrotechlab.lages.ifsc.edu.br\" target=\"_blank\">AgroTechLab</a>.</p>");

    /* Send footer chunck */
    const size_t footer_size = (footer_end - footer_start);
    httpd_resp_send_chunk(req, (const char *)footer_start, footer_size);

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

/**
 * @brief HTTP GET Handler for /index.html
 */
static const httpd_uri_t home_get = {
    .uri = "/index.html",
    .method = HTTP_GET,
    .handler = home_get_handler
};

/**
 * @fn conf_webserver_get_handler(httpd_req_t *req)
 * @brief GET handler for HTTP server configuration webpage
 * @details HTTP GET handler for HTTP server configuration webpage
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t conf_webserver_get_handler(httpd_req_t *req) {
    esp_err_t err = ESP_OK;
    char resp_val[65];
    ESP_LOGD(TAG, "Sending conf_webserver.html");

    /* Set response status, type and header */
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");

    /* Send header chunck */
    const size_t header_size = (header_end - header_start);
    httpd_resp_send_chunk(req, (const char *)header_start, header_size);

    /* Allocate memory to local copy of HTTP SERVER configuration */
    atl_config_webserver_t* config_webserver_local = calloc(1, sizeof(atl_config_webserver_t));
    if (config_webserver_local == NULL) {
        httpd_resp_send_500(req);
        ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for webserver GET!", sizeof(atl_config_webserver_t));
        err = ESP_ERR_NO_MEM;
        goto error_proc;
    }

    /* Make a local copy of MQTT CLIENT configuration */
    if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(config_webserver_local, &atl_config.webserver, sizeof(atl_config_webserver_t));
        
        /* Release cofiguration mutex */
        xSemaphoreGive(atl_config_mutex);
    }
    else {
        httpd_resp_send_408(req);
        ESP_LOGE(TAG, "Fail to get webserver configuration mutex!");
        err = ESP_ERR_TIMEOUT;
        free(config_webserver_local);
        goto error_proc;
    }

    /* Send article chunks */    
    httpd_resp_sendstr_chunk(req, "<form action=\"conf_webserver_post.html\" method=\"post\"><div class=\"row\"> \
                                      <table><tr><th>Parameter</th><th>Value</th></tr> \
                                      <tr><td>Webserver Mode</td><td><select name=\"webserver_mode\" id=\"webserver_mode\">");
    if (config_webserver_local->mode == ATL_WEBSERVER_DISABLED) {
        httpd_resp_sendstr_chunk(req, "<option selected value=\"ATL_WEBSERVER_DISABLED\">Webserver Disabled</option> \
                                       <option value=\"ATL_WEBSERVER_HTTP\">Webserver HTTP</option> \
                                       <option value=\"ATL_WEBSERVER_HTTPS\">Webserver HTTPS</option> \
                                       </select></td></tr>");
    } else if (config_webserver_local->mode == ATL_WEBSERVER_HTTP) {
        httpd_resp_sendstr_chunk(req, "<option value=\"ATL_WEBSERVER_DISABLED\">Webserver Disabled</option> \
                                       <option selected value=\"ATL_WEBSERVER_HTTP\">Webserver HTTP</option> \
                                       <option value=\"ATL_WEBSERVER_HTTPS\">Webserver HTTPS</option> \
                                       </select></td></tr>");
    } else if (config_webserver_local->mode == ATL_WEBSERVER_HTTPS) {
        httpd_resp_sendstr_chunk(req, "<option value=\"ATL_WEBSERVER_DISABLED\">Webserver Disabled</option> \
                                       <option value=\"ATL_WEBSERVER_HTTP\">Webserver HTTP</option> \
                                       <option selected value=\"ATL_WEBSERVER_HTTPS\">Webserver HTTPS</option> \
                                       </select></td></tr>");
    }    
    httpd_resp_sendstr_chunk(req, "<tr><td>Username</td><td><input type=\"text\" id=\"webserver_username\" name=\"webserver_username\" value=\"");
    sprintf(resp_val, "%s", config_webserver_local->admin_user);
    httpd_resp_sendstr_chunk(req, resp_val);
    httpd_resp_sendstr_chunk(req, "\"></td></tr><tr><td>Password</td><td><input type=\"password\" id=\"webserver_pass\" name=\"webserver_pass\" value=\"");
    sprintf(resp_val, "%s", config_webserver_local->admin_pass);
    httpd_resp_sendstr_chunk(req, resp_val);
    httpd_resp_sendstr_chunk(req, "\"></table><br><div class=\"reboot-msg\" id=\"delayMsg\"></div>");    

    /* Send button chunks */    
    httpd_resp_sendstr_chunk(req, "<br><input class=\"btn_generic\" name=\"btn_save_reboot\" type=\"submit\" \
                                    onclick=\"delayRedirect()\" value=\"Save & Reboot\"></div></form>");     

    /* Send footer chunck */
    const size_t footer_size = (footer_end - footer_start);
    httpd_resp_send_chunk(req, (const char *)footer_start, footer_size);

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);
    free(config_webserver_local);
    return ESP_OK;

/* Error procedure */
error_proc:
    atl_led_builtin_blink(6, 100, 255, 0, 0);
    ESP_LOGE(TAG, "Error: %d = %s", err, esp_err_to_name(err));
    return err;
}

/**
 * @brief HTTP GET Handler for HTTP server configuration webpage
 */
static const httpd_uri_t conf_webserver_get = {
    .uri = "/conf_webserver.html",
    .method = HTTP_GET,
    .handler = conf_webserver_get_handler
};

/**
 * @fn conf_mqtt_update_handler(httpd_req_t *req)
 * @brief POST handler for HTTP Server configuration webpage
 * @details HTTP POST handler for HTTP Server configuration webpage
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t conf_webserver_post_handler(httpd_req_t *req) {
    esp_err_t err = ESP_OK;
    ESP_LOGD(TAG, "Processing POST conf_webserver_post");

    /* Allocate memory to process request */
    int    ret;
    size_t off = 0;
    char*  buf = calloc(1, req->content_len + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for webserver POST!", req->content_len + 1);
        err = ESP_ERR_NO_MEM;
        goto error_proc;
    }

    /* Receive all data */
    while (off < req->content_len) {
        /* Read data received in the request */
        ret = httpd_req_recv(req, buf + off, req->content_len - off);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            free(buf);
            err = ESP_ERR_TIMEOUT;
            goto error_proc;
        }
        off += ret;
    }
    buf[off] = '\0';

    /* Allocate memory to local copy of WEBSERVER configuration */
    atl_config_webserver_t* config_webserver_local = calloc(1, sizeof(atl_config_webserver_t));
    if (config_webserver_local == NULL) {
        httpd_resp_send_500(req);
        ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for webserver configuration POST!", sizeof(atl_config_webserver_t));
        err = ESP_ERR_NO_MEM;
        goto error_proc;
    }

    /* Make a local copy of WEBSERVER configuration */
    if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(config_webserver_local, &atl_config.webserver, sizeof(atl_config_webserver_t));
        
        /* Release cofiguration mutex */
        xSemaphoreGive(atl_config_mutex);
    }
    else {
        httpd_resp_send_408(req);
        ESP_LOGE(TAG, "Fail to get mqtt client configuration mutex!");
        err = ESP_ERR_TIMEOUT;
        free(config_webserver_local);
        goto error_proc;
    }

    /* Search for custom header field */
    char* token;
    char* key;
    char* value;
    int token_len, value_len;
    token = strtok(buf, "&");
    while (token) {
        token_len = strlen(token);
        value = strstr(token, "=") + 1;
        value_len = strlen(value);
        key = calloc(1, (token_len - value_len));
        if (!key) {
            httpd_resp_send_500(req);
            ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for webserver payload POST!", (token_len - value_len));
            err = ESP_ERR_NO_MEM;
            goto error_proc;
        }
        memcpy(key, token, (token_len - value_len - 1));
        if (strcmp(key, "webserver_mode") == 0) {
            if (strcmp(value, "ATL_WEBSERVER_DISABLED") == 0) {
                config_webserver_local->mode = ATL_WEBSERVER_DISABLED;
            } else if (strcmp(value, "ATL_WEBSERVER_HTTP") == 0) {
                config_webserver_local->mode = ATL_WEBSERVER_HTTP;
            } else if (strcmp(value, "ATL_WEBSERVER_HTTPS") == 0) {
                config_webserver_local->mode = ATL_WEBSERVER_HTTPS;
            }            
            ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
        } else if (strcmp(key, "webserver_username") == 0) {
            strncpy((char*)&config_webserver_local->admin_user, value, sizeof(config_webserver_local->admin_user));
            ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
        } else if (strcmp(key, "webserver_pass") == 0) {
            strncpy((char*)&config_webserver_local->admin_pass, value, sizeof(config_webserver_local->admin_pass));
            ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
        }
        free(key);
        token = strtok(NULL, "&");
    }
    
    /* Apply local copy of HTTP SERVER configuration to actual main configuration */
    if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(&atl_config.webserver, config_webserver_local, sizeof(atl_config_webserver_t));
        
        /* Release cofiguration mutex */
        xSemaphoreGive(atl_config_mutex);
    }
    else {
        httpd_resp_send_500(req);    
        ESP_LOGE(TAG, "Fail to get webserver configuration mutex!");
        err = ESP_ERR_TIMEOUT;
        free(config_webserver_local);
        goto error_proc;
    }

    /* Commit configuration to NVS */
    err = atl_config_commit_nvs();
    if (err == ESP_OK) {
        free(config_webserver_local);
        atl_led_builtin_blink(6, 100, 255, 69, 0);
        return err;
    } else {
        httpd_resp_send_500(req);
        ESP_LOGE(TAG, "Fail to commit webserver configuration to NVS!");
        free(config_webserver_local);
        goto error_proc;
    }    

/* Error procedure */
error_proc:
    atl_led_builtin_blink(6, 100, 255, 0, 0);
    ESP_LOGE(TAG, "Error: %d = %s", err, esp_err_to_name(err));
    return err;
}

/**
 * @brief HTTP POST Handler for Webserver configuration webpage
 */
static const httpd_uri_t conf_webserver_post = {
    .uri = "/conf_webserver_post.html",
    .method = HTTP_POST,
    .handler = conf_webserver_post_handler
};

/**
 * @fn conf_mqtt_get_handler(httpd_req_t *req)
 * @brief GET handler for MQTT Client configuration webpage
 * @details HTTP GET handler for MQTT Client configuration webpage
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t conf_mqtt_get_handler(httpd_req_t *req) {
    esp_err_t err = ESP_OK;
//     char resp_val[65];
//     ESP_LOGD(TAG, "Sending conf_mqtt.html");

//     /* Set response status, type and header */
//     httpd_resp_set_status(req, HTTPD_200);
//     httpd_resp_set_type(req, "text/html");
//     httpd_resp_set_hdr(req, "Connection", "keep-alive");

//     /* Send header chunck */
//     const size_t header_size = (header_end - header_start);
//     httpd_resp_send_chunk(req, (const char *)header_start, header_size);

//     /* Allocate memory to local copy of MQTT CLIENT configuration */
//     atl_config_mqtt_client_t* config_mqtt_client_local = calloc(1, sizeof(atl_config_mqtt_client_t));
//     if (config_mqtt_client_local == NULL) {
//         httpd_resp_send_500(req);
//         ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for mqtt client GET!", sizeof(atl_config_mqtt_client_t));
//         err = ESP_ERR_NO_MEM;
//         goto error_proc;
//     }

//     /* Make a local copy of MQTT CLIENT configuration */
//     if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
//         memcpy(config_mqtt_client_local, &atl_config.mqtt_client, sizeof(atl_config_mqtt_client_t));
        
//         /* Release cofiguration mutex */
//         xSemaphoreGive(atl_config_mutex);
//     }
//     else {
//         httpd_resp_send_408(req);
//         ESP_LOGE(TAG, "Fail to get mqtt client configuration mutex!");
//         err = ESP_ERR_TIMEOUT;
//         free(config_mqtt_client_local);
//         goto error_proc;
//     }

//     /* Send article chunks */    
/*    httpd_resp_sendstr_chunk(req, "<form action=\"conf_mqtt_post.html\" method=\"post\"><div class=\"row\"> \
                                      <table><tr><th>Parameter</th><th>Value</th></tr> \
                                      <tr><td>MQTT Mode</td><td><select name=\"mqtt_mode\" id=\"mqtt_mode\">");
    if (config_mqtt_client_local->mode == ATL_MQTT_DISABLED) {
        httpd_resp_sendstr_chunk(req, "<option selected value=\"ATL_MQTT_DISABLED\">MQTT Client Disabled</option> \
                                       <option value=\"ATL_MQTT_AGROTECHLAB_CLOUD\">AgroTechLab Cloud</option> \
                                       <option value=\"ATL_MQTT_THIRD\">Third Server</option> \
                                       </select></td></tr>");
    } else if (config_mqtt_client_local->mode == ATL_MQTT_AGROTECHLAB_CLOUD) {
        httpd_resp_sendstr_chunk(req, "<option value=\"ATL_MQTT_DISABLED\">MQTT Client Disabled</option> \
                                       <option selected value=\"ATL_MQTT_AGROTECHLAB_CLOUD\">AgroTechLab Cloud</option> \
                                       <option value=\"ATL_MQTT_THIRD\">Third Server</option> \
                                       </select></td></tr>");
    } else if (config_mqtt_client_local->mode == ATL_MQTT_THIRD) {
        httpd_resp_sendstr_chunk(req, "<option value=\"ATL_MQTT_DISABLED\">MQTT Client Disabled</option> \
                                       <option value=\"ATL_MQTT_AGROTECHLAB_CLOUD\">AgroTechLab Cloud</option> \
                                       <option selected value=\"ATL_MQTT_THIRD\">Third Server</option> \
                                       </select></td></tr>");
    }    */
//     httpd_resp_sendstr_chunk(req, "<tr><td>MQTT Server Address</td><td><input type=\"text\" id=\"mqtt_srv_addr\" name=\"mqtt_srv_addr\" value=\"");
//     sprintf(resp_val, "%s", config_mqtt_client_local->broker_address);
//     httpd_resp_sendstr_chunk(req, resp_val);
//     httpd_resp_sendstr_chunk(req, "\"></td></tr><tr><td>MQTT Server Port</td><td><input type=\"number\" id=\"mqtt_srv_port\" name=\"mqtt_srv_port\" value=\"");
//     sprintf(resp_val, "%d", config_mqtt_client_local->broker_port);
//     httpd_resp_sendstr_chunk(req, resp_val);
//     httpd_resp_sendstr_chunk(req, "\"></td></tr><tr><td>Transport</td><td><select name=\"mqtt_transport\" id=\"mqtt_transport\">");
 /*   if (config_mqtt_client_local->transport == MQTT_TRANSPORT_OVER_TCP) {
        httpd_resp_sendstr_chunk(req, "<option selected value=\"MQTT_TRANSPORT_OVER_TCP\">MQTT (TCP)</option> \
                                        <option value=\"MQTT_TRANSPORT_OVER_SSL\">MQTTS (TCP+TLS)</option> \
                                       </select></td></tr>");
    } else if (config_mqtt_client_local->transport == MQTT_TRANSPORT_OVER_SSL) {
        httpd_resp_sendstr_chunk(req, "<option value=\"MQTT_TRANSPORT_OVER_TCP\">MQTT (TCP)</option> \
                                       <option selected value=\"MQTT_TRANSPORT_OVER_SSL\">MQTTS (TCP+TLS)</option> \
                                       </select></td></tr>");
    }*/
//     httpd_resp_sendstr_chunk(req, "<tr><td>Disable Common Name (CN) check</td><td><select name=\"mqtt_disable_cn_check\" id=\"mqtt_disable_cn_check\">");
//     if (config_mqtt_client_local->disable_cn_check == true) {
//         httpd_resp_sendstr_chunk(req, "<option selected value=\"true\">true</option><option value=\"false\">false</option></select></td></tr>");
//     } else {
//         httpd_resp_sendstr_chunk(req, "<option value=\"true\">true</option><option selected value=\"false\">false</option></select></td></tr>");
//     }    
//     httpd_resp_sendstr_chunk(req, "<tr><td>Username</td><td><input type=\"text\" id=\"mqtt_username\" name=\"mqtt_username\" value=\"");
//     sprintf(resp_val, "%s", config_mqtt_client_local->user);
//     httpd_resp_sendstr_chunk(req, resp_val);
//     httpd_resp_sendstr_chunk(req, "\"></td></tr><tr><td>Password</td><td><input type=\"password\" id=\"mqtt_pass\" name=\"mqtt_pass\" value=\"");
//     sprintf(resp_val, "%s", config_mqtt_client_local->pass);
//     httpd_resp_sendstr_chunk(req, resp_val);
//     httpd_resp_sendstr_chunk(req, "\"></td></tr><tr><td>QoS</td><td><select name=\"mqtt_qos\" id=\"mqtt_qos\">");
/*    if (config_mqtt_client_local->qos == ATL_MQTT_QOS0) {
        httpd_resp_sendstr_chunk(req, "<option selected value=\"ATL_MQTT_QOS0\">At most once (QoS 0)</option> \
                                       <option value=\"ATL_MQTT_QOS1\">At least once (QoS 1)</option> \
                                       <option value=\"ATL_MQTT_QOS2\">Exactly once (QoS 2)</option> \
                                       </select></td></tr>");
    } else if (config_mqtt_client_local->qos == ATL_MQTT_QOS1) {
        httpd_resp_sendstr_chunk(req, "<option value=\"ATL_MQTT_QOS0\">At most once (QoS 0)</option> \
                                       <option selected value=\"ATL_MQTT_QOS1\">At least once (QoS 1)</option> \
                                       <option value=\"ATL_MQTT_QOS2\">Exactly once (QoS 2)</option> \
                                       </select></td></tr>");
    } else if (config_mqtt_client_local->qos == ATL_MQTT_QOS2) {
        httpd_resp_sendstr_chunk(req, "<option value=\"ATL_MQTT_QOS0\">At most once (QoS 0)</option> \
                                       <option value=\"ATL_MQTT_QOS1\">At least once (QoS 1)</option> \
                                       <option selected value=\"ATL_MQTT_QOS2\">Exactly once (QoS 2)</option> \
                                       </select></td></tr>");
    }
    httpd_resp_sendstr_chunk(req, "</table><br><div class=\"reboot-msg\" id=\"delayMsg\"></div>");    */

    /* Send button chunks */    
 /*   httpd_resp_sendstr_chunk(req, "<br><input class=\"btn_generic\" name=\"btn_save_reboot\" type=\"submit\" \
                                    onclick=\"delayRedirect()\" value=\"Save & Reboot\"></div></form>");     
*/
//     /* Send footer chunck */
//     const size_t footer_size = (footer_end - footer_start);
//     httpd_resp_send_chunk(req, (const char *)footer_start, footer_size);

//     /* Send empty chunk to signal HTTP response completion */
//     httpd_resp_sendstr_chunk(req, NULL);
//     free(config_mqtt_client_local);
//     return ESP_OK;

// /* Error procedure */
// error_proc:
//     atl_led_builtin_blink(6, 100, 255, 0, 0);
//     ESP_LOGE(TAG, "Error: %d = %s", err, esp_err_to_name(err));
    return err;
}

/**
 * @brief HTTP GET Handler for MQTT configuration webpage
 */
static const httpd_uri_t conf_mqtt_get = {
    .uri = "/conf_mqtt.html",
    .method = HTTP_GET,
    .handler = conf_mqtt_get_handler
};

/**
 * @fn conf_mqtt_update_handler(httpd_req_t *req)
 * @brief POST handler for MQTT Client configuration webpage
 * @details HTTP POST handler for MQTT Client configuration webpage
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t conf_mqtt_post_handler(httpd_req_t *req) {
    esp_err_t err = ESP_OK;
//     ESP_LOGD(TAG, "Processing POST conf_mqtt_post");

//     /* Allocate memory to process request */
//     int    ret;
//     size_t off = 0;
//     char*  buf = calloc(1, req->content_len + 1);
//     if (!buf) {
//         httpd_resp_send_500(req);
//         ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for mqtt client POST!", req->content_len + 1);
//         err = ESP_ERR_NO_MEM;
//         goto error_proc;
//     }

//     /* Receive all data */
//     while (off < req->content_len) {
//         /* Read data received in the request */
//         ret = httpd_req_recv(req, buf + off, req->content_len - off);
//         if (ret <= 0) {
//             if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
//                 httpd_resp_send_408(req);
//             }
//             free(buf);
//             err = ESP_ERR_TIMEOUT;
//             goto error_proc;
//         }
//         off += ret;
//     }
//     buf[off] = '\0';

//     /* Allocate memory to local copy of ANALOG INPUT configuration */
//     atl_config_mqtt_client_t* config_mqtt_client_local = calloc(1, sizeof(atl_config_mqtt_client_t));
//     if (config_mqtt_client_local == NULL) {
//         httpd_resp_send_500(req);
//         ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for mqtt client configuration POST!", sizeof(atl_config_mqtt_client_t));
//         err = ESP_ERR_NO_MEM;
//         goto error_proc;
//     }

//     /* Make a local copy of ANALOG INPUT configuration */
//     if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
//         memcpy(config_mqtt_client_local, &atl_config.mqtt_client, sizeof(atl_config_mqtt_client_t));
        
//         /* Release cofiguration mutex */
//         xSemaphoreGive(atl_config_mutex);
//     }
//     else {
//         httpd_resp_send_408(req);
//         ESP_LOGE(TAG, "Fail to get mqtt client configuration mutex!");
//         err = ESP_ERR_TIMEOUT;
//         free(config_mqtt_client_local);
//         goto error_proc;
//     }

//     /* Search for custom header field */
//     char* token;
//     char* key;
//     char* value;
//     int token_len, value_len;
//     token = strtok(buf, "&");
//     while (token) {
//         token_len = strlen(token);
//         value = strstr(token, "=") + 1;
//         value_len = strlen(value);
//         key = calloc(1, (token_len - value_len));
//         if (!key) {
//             httpd_resp_send_500(req);
//             ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for mqtt client payload POST!", (token_len - value_len));
//             err = ESP_ERR_NO_MEM;
//             goto error_proc;
//         }
//         memcpy(key, token, (token_len - value_len - 1));
//         if (strcmp(key, "mqtt_mode") == 0) {
//             if (strcmp(value, "ATL_MQTT_DISABLED") == 0) {
//                 config_mqtt_client_local->mode = ATL_MQTT_DISABLED;
//             } else if (strcmp(value, "ATL_MQTT_AGROTECHLAB_CLOUD") == 0) {
//                 config_mqtt_client_local->mode = ATL_MQTT_AGROTECHLAB_CLOUD;
//             } else if (strcmp(value, "ATL_MQTT_THIRD") == 0) {
//                 config_mqtt_client_local->mode = ATL_MQTT_THIRD;
//             }            
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "mqtt_srv_addr") == 0) {
//             strncpy((char*)&config_mqtt_client_local->broker_address, value, sizeof(config_mqtt_client_local->broker_address));
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "mqtt_srv_port") == 0) {
//             config_mqtt_client_local->broker_port = atoi(value);
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "mqtt_transport") == 0) {
//             if (strcmp(value, "MQTT_TRANSPORT_OVER_TCP") == 0) {
//                 config_mqtt_client_local->transport = MQTT_TRANSPORT_OVER_TCP;
//             } else if (strcmp(value, "MQTT_TRANSPORT_OVER_SSL") == 0) {
//                 config_mqtt_client_local->transport = MQTT_TRANSPORT_OVER_SSL;
//             }            
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "mqtt_disable_cn_check") == 0) {
//             if (strcmp(value, "true") == 0) {
//                 config_mqtt_client_local->disable_cn_check = true;
//             } else if (strcmp(value, "false") == 0) {
//                 config_mqtt_client_local->disable_cn_check = false;
//             }
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "mqtt_username") == 0) {
//             strncpy((char*)&config_mqtt_client_local->user, value, sizeof(config_mqtt_client_local->user));
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "mqtt_pass") == 0) {
//             strncpy((char*)&config_mqtt_client_local->pass, value, sizeof(config_mqtt_client_local->pass));
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "mqtt_qos") == 0) {
//             if (strcmp(value, "ATL_MQTT_QOS0") == 0) {
//                 config_mqtt_client_local->qos = ATL_MQTT_QOS0;
//             } else if (strcmp(value, "ATL_MQTT_QOS1") == 0) {
//                 config_mqtt_client_local->qos = ATL_MQTT_QOS1;
//             } else if (strcmp(value, "ATL_MQTT_QOS2") == 0) {
//                 config_mqtt_client_local->qos = ATL_MQTT_QOS2;
//             }
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         }
//         free(key);
//         token = strtok(NULL, "&");
//     }
    
//     /* Apply local copy of MQTT CLIENT configuration to actual main configuration */
//     if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
//         memcpy(&atl_config.mqtt_client, config_mqtt_client_local, sizeof(atl_config_mqtt_client_t));
        
//         /* Release cofiguration mutex */
//         xSemaphoreGive(atl_config_mutex);
//     }
//     else {
//         httpd_resp_send_500(req);    
//         ESP_LOGE(TAG, "Fail to get mqtt client configuration mutex!");
//         err = ESP_ERR_TIMEOUT;
//         free(config_mqtt_client_local);
//         goto error_proc;
//     }

//     /* Commit configuration to NVS */
//     err = atl_config_commit_nvs();
//     if (err == ESP_OK) {
//         free(config_mqtt_client_local);
//         atl_led_builtin_blink(6, 100, 255, 69, 0);
//         return err;
//     } else {
//         httpd_resp_send_500(req);
//         ESP_LOGE(TAG, "Fail to commit mqtt client configuration to NVS!");
//         free(config_mqtt_client_local);
//         goto error_proc;
//     }

//     /* Restart MQTT task */    
//     err = atl_mqtt_client_restart();
//     if (err != ESP_OK) {
//         httpd_resp_send_500(req);
//         ESP_LOGE(TAG, "Fail to restart MQTT client task!");
//         goto error_proc;
//     }

// /* Error procedure */
// error_proc:
//     atl_led_builtin_blink(6, 100, 255, 0, 0);
//     ESP_LOGE(TAG, "Error: %d = %s", err, esp_err_to_name(err));
    return err;
}

/**
 * @brief HTTP POST Handler for MQTT configuration webpage
 */
static const httpd_uri_t conf_mqtt_post = {
    .uri = "/conf_mqtt_post.html",
    .method = HTTP_POST,
    .handler = conf_mqtt_post_handler
};

/**
 * @fn conf_telemetry_get_handler(httpd_req_t *req)
 * @brief GET handler for telemetry configuration webpage
 * @details HTTP GET handler for telemetry configuration webpage
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t conf_telemetry_get_handler(httpd_req_t *req) {
    esp_err_t err = ESP_OK;
//     char resp_val[65];
//     ESP_LOGD(TAG, "Sending conf_telemetry.html");

//     /* Set response status, type and header */
//     httpd_resp_set_status(req, HTTPD_200);
//     httpd_resp_set_type(req, "text/html");
//     httpd_resp_set_hdr(req, "Connection", "keep-alive");

//     /* Send header chunck */
//     const size_t header_size = (header_end - header_start);
//     httpd_resp_send_chunk(req, (const char *)header_start, header_size);

//     /* Allocate memory to local copy of MQTT CLIENT configuration */
//     atl_config_telemetry_t* config_telemetry_local = calloc(1, sizeof(atl_config_telemetry_t));
//     if (config_telemetry_local == NULL) {
//         httpd_resp_send_500(req);
//         ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for telemetry GET!", sizeof(atl_config_telemetry_t));
//         err = ESP_ERR_NO_MEM;
//         goto error_proc;
//     }

//     /* Make a local copy of MQTT CLIENT configuration */
//     if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
//         memcpy(config_telemetry_local, &atl_config.telemetry, sizeof(atl_config_telemetry_t));
        
//         /* Release cofiguration mutex */
//         xSemaphoreGive(atl_config_mutex);
//     }
//     else {
//         httpd_resp_send_408(req);
//         ESP_LOGE(TAG, "Fail to get telemetry configuration mutex!");
//         err = ESP_ERR_TIMEOUT;
//         free(config_telemetry_local);
//         goto error_proc;
//     }  

//     /* Send article chunks */    
/*    httpd_resp_sendstr_chunk(req, "<form action=\"conf_telemetry_post.html\" method=\"post\"><div class=\"row\"> \
                                      <table><tr><th>Parameter</th><th>Value</th></tr> \
                                      <tr><td>Telemetry Send Period (in seconds)</td><td><input type=\"number\" id=\"telemetry_send_period\" name=\"telemetry_send_period\" value=\"");
    sprintf(resp_val, "%lu", config_telemetry_local->send_period);
    httpd_resp_sendstr_chunk(req, resp_val);
    httpd_resp_sendstr_chunk(req, "\"></td></tr><tr><td>Power Sensor Enabled</td><td><select name=\"power_enabled\" id=\"power_enabled\">");
    if (config_telemetry_local->power.enabled == true) {
        httpd_resp_sendstr_chunk(req, "<option selected value=\"true\">Enabled</option> \
                                       <option value=\"false\">Disabled</option> \
                                       </select></td></tr>");
    } else {
        httpd_resp_sendstr_chunk(req, "<option value=\"true\">Enabled</option> \
                                       <option selected value=\"false\">Disabled</option> \
                                       </select></td></tr>");
    }*/
//     httpd_resp_sendstr_chunk(req, "<tr><td>Power Sampling Period (in seconds)</td><td><input type=\"number\" id=\"power_sampling_period\" name=\"power_sampling_period\" value=\"");
//     sprintf(resp_val, "%lu", config_telemetry_local->power.sampling_period);
//     httpd_resp_sendstr_chunk(req, resp_val);
//     httpd_resp_sendstr_chunk(req, "\"></td></tr><tr><td>DHT Enabled</td><td><select name=\"dht_enabled\" id=\"dht_enabled\">");
/*    if (config_telemetry_local->dht.enabled == true) {
        httpd_resp_sendstr_chunk(req, "<option selected value=\"true\">Enabled</option> \
                                       <option value=\"false\">Disabled</option> \
                                       </select></td></tr>");
    } else {
        httpd_resp_sendstr_chunk(req, "<option value=\"true\">Enabled</option> \
                                       <option selected value=\"false\">Disabled</option> \
                                       </select></td></tr>");
    } */
//     httpd_resp_sendstr_chunk(req, "<tr><td>DHT Sampling Period (in seconds)</td><td><input type=\"number\" id=\"dht_sampling_period\" name=\"dht_sampling_period\" value=\"");
//     sprintf(resp_val, "%lu", config_telemetry_local->dht.sampling_period);
//     httpd_resp_sendstr_chunk(req, resp_val);
//     httpd_resp_sendstr_chunk(req, "\"></td></tr><tr><td>BME280 Enabled</td><td><select name=\"bme280_enabled\" id=\"bme280_enabled\">");
 /*   if (config_telemetry_local->bme280.enabled == true) {
        httpd_resp_sendstr_chunk(req, "<option selected value=\"true\">Enabled</option> \
                                       <option value=\"false\">Disabled</option> \
                                       </select></td></tr>");
    } else {
        httpd_resp_sendstr_chunk(req, "<option value=\"true\">Enabled</option> \
                                       <option selected value=\"false\">Disabled</option> \
                                       </select></td></tr>");
    } */
//     httpd_resp_sendstr_chunk(req, "<tr><td>BME280 Sampling Period (in seconds)</td><td><input type=\"number\" id=\"bme280_sampling_period\" name=\"bme280_sampling_period\" value=\"");
//     sprintf(resp_val, "%lu", config_telemetry_local->bme280.sampling_period);
//     httpd_resp_sendstr_chunk(req, resp_val);
//     httpd_resp_sendstr_chunk(req, "\"></td></tr><tr><td>Soil Sensor Enabled</td><td><select name=\"soil_enabled\" id=\"soil_enabled\">");
/*    if (config_telemetry_local->soil.enabled == true) {
        httpd_resp_sendstr_chunk(req, "<option selected value=\"true\">Enabled</option> \
                                       <option value=\"false\">Disabled</option> \
                                       </select></td></tr>");
    } else {
        httpd_resp_sendstr_chunk(req, "<option value=\"true\">Enabled</option> \
                                       <option selected value=\"false\">Disabled</option> \
                                       </select></td></tr>");
    } */
//     httpd_resp_sendstr_chunk(req, "<tr><td>Soil Sensor Sampling Period (in seconds)</td><td><input type=\"number\" id=\"soil_sampling_period\" name=\"soil_sampling_period\" value=\"");
//     sprintf(resp_val, "%lu", config_telemetry_local->soil.sampling_period);
//     httpd_resp_sendstr_chunk(req, resp_val);
//     httpd_resp_sendstr_chunk(req, "\"></td></tr><tr><td>Pluviometer Enabled</td><td><select name=\"pluviometer_enabled\" id=\"pluviometer_enabled\">");
 /*   if (config_telemetry_local->pluviometer.enabled == true) {
        httpd_resp_sendstr_chunk(req, "<option selected value=\"true\">Enabled</option> \
                                       <option value=\"false\">Disabled</option> \
                                       </select></td></tr>");
    } else {
        httpd_resp_sendstr_chunk(req, "<option value=\"true\">Enabled</option> \
                                       <option selected value=\"false\">Disabled</option> \
                                       </select></td></tr>");
    } */
//     httpd_resp_sendstr_chunk(req, "<tr><td>Pluviometer Sampling Period (in seconds)</td><td><input type=\"number\" id=\"pluviometer_sampling_period\" name=\"pluviometer_sampling_period\" value=\"");
//     sprintf(resp_val, "%lu", config_telemetry_local->pluviometer.sampling_period);
//     httpd_resp_sendstr_chunk(req, resp_val);
//     httpd_resp_sendstr_chunk(req, "\"></td></tr><tr><td>Anemometer Enabled</td><td><select name=\"anemometer_enabled\" id=\"anemometer_enabled\">");
 /*   if (config_telemetry_local->anemometer.enabled == true) {
        httpd_resp_sendstr_chunk(req, "<option selected value=\"true\">Enabled</option> \
                                       <option value=\"false\">Disabled</option> \
                                       </select></td></tr>");
    } else {
        httpd_resp_sendstr_chunk(req, "<option value=\"true\">Enabled</option> \
                                       <option selected value=\"false\">Disabled</option> \
                                       </select></td></tr>");
    } */
//     httpd_resp_sendstr_chunk(req, "<tr><td>Anemometer Sampling Period (in seconds)</td><td><input type=\"number\" id=\"anemometer_sampling_period\" name=\"anemometer_sampling_period\" value=\"");
//     sprintf(resp_val, "%lu", config_telemetry_local->anemometer.sampling_period);
//     httpd_resp_sendstr_chunk(req, resp_val);
//     httpd_resp_sendstr_chunk(req, "\"></td></tr><tr><td>Wind Sock Enabled</td><td><select name=\"wind_sock_enabled\" id=\"wind_sock_enabled\">");
 /*   if (config_telemetry_local->wind_sock.enabled == true) {
        httpd_resp_sendstr_chunk(req, "<option selected value=\"true\">Enabled</option> \
                                       <option value=\"false\">Disabled</option> \
                                       </select></td></tr>");
    } else {
        httpd_resp_sendstr_chunk(req, "<option value=\"true\">Enabled</option> \
                                       <option selected value=\"false\">Disabled</option> \
                                       </select></td></tr>");
    } */
//     httpd_resp_sendstr_chunk(req, "<tr><td>Wind Sock Sampling Period (in seconds)</td><td><input type=\"number\" id=\"wind_sock_sampling_period\" name=\"wind_sock_sampling_period\" value=\"");
//     sprintf(resp_val, "%lu", config_telemetry_local->wind_sock.sampling_period);
//     httpd_resp_sendstr_chunk(req, resp_val);
    
//     httpd_resp_sendstr_chunk(req, "\"></td></tr><tr><td>CR300 Gateway</td><td><select name=\"cr300_gw_enabled\" id=\"cr300_gw_enabled\">");
/*    if (config_telemetry_local->wind_sock.enabled == true) {
        httpd_resp_sendstr_chunk(req, "<option selected value=\"true\">Enabled</option> \
                                       <option value=\"false\">Disabled</option> \
                                       </select></td></tr>");
    } else {
        httpd_resp_sendstr_chunk(req, "<option value=\"true\">Enabled</option> \
                                       <option selected value=\"false\">Disabled</option> \
                                       </select></td></tr>");
    } */
//     httpd_resp_sendstr_chunk(req, "<tr><td>Wind Sock Sampling Period (in seconds)</td><td><input type=\"number\" id=\"wind_sock_sampling_period\" name=\"wind_sock_sampling_period\" value=\"");
//     sprintf(resp_val, "%lu", config_telemetry_local->wind_sock.sampling_period);
//     httpd_resp_sendstr_chunk(req, resp_val);

//     httpd_resp_sendstr_chunk(req, "\"></td></tr>");
//     httpd_resp_sendstr_chunk(req, "</table><br><div class=\"reboot-msg\" id=\"delayMsg\"></div>");    

//     /* Send button chunks */    
 /*   httpd_resp_sendstr_chunk(req, "<br><input class=\"btn_generic\" name=\"btn_save_apply\" type=\"submit\" \
                                    onclick=\"delayRedirect()\" value=\"Save & Apply\"></div></form>");     
  */
//     /* Send footer chunck */
//     const size_t footer_size = (footer_end - footer_start);
//     httpd_resp_send_chunk(req, (const char *)footer_start, footer_size);

//     /* Send empty chunk to signal HTTP response completion */
//     httpd_resp_sendstr_chunk(req, NULL);
//     free(config_telemetry_local);
//     return ESP_OK;

// /* Error procedure */
// error_proc:
//     atl_led_builtin_blink(6, 100, 255, 0, 0);
//     ESP_LOGE(TAG, "Error: %d = %s", err, esp_err_to_name(err));
    return err;
}

/**
 * @brief HTTP GET Handler for telemetry configuration webpage
 */
static const httpd_uri_t conf_telemetry_get = {
    .uri = "/conf_telemetry.html",
    .method = HTTP_GET,
    .handler = conf_telemetry_get_handler
};

/**
 * @fn conf_telemetry_post_handler(httpd_req_t *req)
 * @brief POST handler for telemetry configuration webpage
 * @details HTTP POST handler for telemetry configuration webpage
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t conf_telemetry_post_handler(httpd_req_t *req) {
    esp_err_t err = ESP_OK;
//     ESP_LOGD(TAG, "Processing POST conf_telemetry_post");

//     /* Allocate memory to process request */
//     int    ret;
//     size_t off = 0;
//     char*  buf = calloc(1, req->content_len + 1);
//     if (!buf) {
//         httpd_resp_send_500(req);
//         ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for telemetry POST!", req->content_len + 1);
//         err = ESP_ERR_NO_MEM;
//         goto error_proc;
//     }

//     /* Receive all data */
//     while (off < req->content_len) {
//         /* Read data received in the request */
//         ret = httpd_req_recv(req, buf + off, req->content_len - off);
//         if (ret <= 0) {
//             if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
//                 httpd_resp_send_408(req);
//             }
//             free(buf);
//             err = ESP_ERR_TIMEOUT;
//             goto error_proc;
//         }
//         off += ret;
//     }
//     buf[off] = '\0';

//     /* Allocate memory to local copy of telemetry configuration */
//     atl_config_telemetry_t* config_telemetry_local = calloc(1, sizeof(atl_config_telemetry_t));
//     if (config_telemetry_local == NULL) {
//         httpd_resp_send_500(req);
//         ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for telemetry configuration POST!", sizeof(atl_config_telemetry_t));
//         err = ESP_ERR_NO_MEM;
//         goto error_proc;
//     }

//     /* Make a local copy of telemetry configuration */
//     if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
//         memcpy(config_telemetry_local, &atl_config.telemetry, sizeof(atl_config_telemetry_t));
        
//         /* Release cofiguration mutex */
//         xSemaphoreGive(atl_config_mutex);
//     }
//     else {
//         httpd_resp_send_408(req);
//         ESP_LOGE(TAG, "Fail to get telemetry configuration mutex!");
//         err = ESP_ERR_TIMEOUT;
//         free(config_telemetry_local);
//         goto error_proc;
//     }

//     /* Search for custom header field */
//     char* token;
//     char* key;
//     char* value;
//     int token_len, value_len;
//     token = strtok(buf, "&");
//     while (token) {
//         token_len = strlen(token);
//         value = strstr(token, "=") + 1;
//         value_len = strlen(value);
//         key = calloc(1, (token_len - value_len));
//         if (!key) {
//             httpd_resp_send_500(req);
//             ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for telemetry payload POST!", (token_len - value_len));
//             err = ESP_ERR_NO_MEM;
//             goto error_proc;
//         }
//         memcpy(key, token, (token_len - value_len - 1));
//         if (strcmp(key, "telemetry_send_period") == 0) {
//             config_telemetry_local->send_period = atoi(value);
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "power_enabled") == 0) {
//             if (strcmp(value, "true") == 0) {
//                 config_telemetry_local->power.enabled = true;
//             } else if (strcmp(value, "false") == 0) {
//                 config_telemetry_local->power.enabled = false;
//             }            
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "power_sampling_period") == 0) {
//             config_telemetry_local->power.sampling_period = atoi(value);
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "dht_enabled") == 0) {
//             if (strcmp(value, "true") == 0) {
//                 config_telemetry_local->dht.enabled = true;
//             } else if (strcmp(value, "false") == 0) {
//                 config_telemetry_local->dht.enabled = false;
//             }            
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "dht_sampling_period") == 0) {
//             config_telemetry_local->dht.sampling_period = atoi(value);
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "bme280_enabled") == 0) {
//             if (strcmp(value, "true") == 0) {
//                 config_telemetry_local->bme280.enabled = true;
//             } else if (strcmp(value, "false") == 0) {
//                 config_telemetry_local->bme280.enabled = false;
//             }            
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "bme280_sampling_period") == 0) {
//             config_telemetry_local->bme280.sampling_period = atoi(value);
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "soil_enabled") == 0) {
//             if (strcmp(value, "true") == 0) {
//                 config_telemetry_local->soil.enabled = true;
//             } else if (strcmp(value, "false") == 0) {
//                 config_telemetry_local->soil.enabled = false;
//             }            
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "soil_sampling_period") == 0) {
//             config_telemetry_local->soil.sampling_period = atoi(value);
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "pluviometer_enabled") == 0) {
//             if (strcmp(value, "true") == 0) {
//                 config_telemetry_local->pluviometer.enabled = true;
//             } else if (strcmp(value, "false") == 0) {
//                 config_telemetry_local->pluviometer.enabled = false;
//             }            
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "pluviometer_sampling_period") == 0) {
//             config_telemetry_local->pluviometer.sampling_period = atoi(value);
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "anemometer_enabled") == 0) {
//             if (strcmp(value, "true") == 0) {
//                 config_telemetry_local->anemometer.enabled = true;
//             } else if (strcmp(value, "false") == 0) {
//                 config_telemetry_local->anemometer.enabled = false;
//             }            
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "anemometer_sampling_period") == 0) {
//             config_telemetry_local->anemometer.sampling_period = atoi(value);
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "wind_sock_enabled") == 0) {
//             if (strcmp(value, "true") == 0) {
//                 config_telemetry_local->wind_sock.enabled = true;
//             } else if (strcmp(value, "false") == 0) {
//                 config_telemetry_local->wind_sock.enabled = false;
//             }            
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         } else if (strcmp(key, "wind_sock_sampling_period") == 0) {
//             config_telemetry_local->wind_sock.sampling_period = atoi(value);
//             ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
//         }
//         free(key);
//         token = strtok(NULL, "&");
//     }
    
//     /* Apply local copy of telemetry configuration to actual main configuration */
//     if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
//         memcpy(&atl_config.telemetry, config_telemetry_local, sizeof(atl_config_telemetry_t));
        
//         // /* Update telemetry DHT sensor task run mode */
//         // if (atl_config.telemetry.dht.enabled == true) {
//         //     vTaskResume(atl_dht_handle);
//         // } else {
//         //     vTaskSuspend(atl_dht_handle);
//         // }

//         // /* If any sensor task is enabled, telemetry send task must be enabled too */
//         // if (atl_config.telemetry.dht.enabled == true) {
//         //     vTaskResume(atl_telemetry_handle);
//         // } else {
//         //     vTaskSuspend(atl_telemetry_handle);
//         // }

//         /* Release cofiguration mutex */
//         xSemaphoreGive(atl_config_mutex);
//     }
//     else {
//         httpd_resp_send_500(req);    
//         ESP_LOGE(TAG, "Fail to get telemetry configuration mutex!");
//         err = ESP_ERR_TIMEOUT;
//         free(config_telemetry_local);
//         goto error_proc;
//     }

//     /* Commit configuration to NVS */
//     err = atl_config_commit_nvs();
//     if (err == ESP_OK) {
//         free(config_telemetry_local);
//         atl_led_builtin_blink(6, 100, 255, 69, 0);
//         return err;
//     } else {
//         httpd_resp_send_500(req);
//         ESP_LOGE(TAG, "Fail to commit telemetry configuration to NVS!");
//         free(config_telemetry_local);
//         goto error_proc;
//     }    

// /* Error procedure */
// error_proc:
//     atl_led_builtin_blink(6, 100, 255, 0, 0);
//     ESP_LOGE(TAG, "Error: %d = %s", err, esp_err_to_name(err));
    return err;
}

/**
 * @brief HTTP POST Handler for telemetry configuration webpage
 */
static const httpd_uri_t conf_telemetry_post = {
    .uri = "/conf_telemetry_post.html",
    .method = HTTP_POST,
    .handler = conf_telemetry_post_handler
};

/**
 * @fn conf_wifi_get_handler(httpd_req_t *req)
 * @brief GET handler for WiFi configuration webpage
 * @details HTTP GET handler for WiFi configuration webpage
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t conf_wifi_get_handler(httpd_req_t *req) {
    esp_err_t err = ESP_OK;
    char resp_val[65];
    ESP_LOGD(TAG, "Sending conf_wifi.html");

    /* Set response status, type and header */
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");

    /* Send header chunck */
    const size_t header_size = (header_end - header_start);
    httpd_resp_send_chunk(req, (const char *)header_start, header_size);

    /* Allocate memory to local copy of WIFI configuration */
    atl_config_wifi_t* config_wifi_local = calloc(1, sizeof(atl_config_wifi_t));
    if (config_wifi_local == NULL) {
        httpd_resp_send_500(req);
        ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for wifi GET!", sizeof(atl_config_wifi_t));
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
        httpd_resp_send_408(req);
        ESP_LOGE(TAG, "Fail to get wifi configuration mutex!");
        err = ESP_ERR_TIMEOUT;
        free(config_wifi_local);
        goto error_proc;
    }

    /* Send article chunks */    
    httpd_resp_sendstr_chunk(req, "<form action=\"conf_wifi_post.html\" method=\"post\"> \
                                      <div class=\"row\"> \
                                      <table><tr><th>Parameter</th><th>Value</th></tr> \
                                      <tr><td>MAC Address</td><td>");
    uint8_t mac_addr[6] = {0};
    esp_efuse_mac_get_default((uint8_t*)&mac_addr);
    if (config_wifi_local->mode == ATL_WIFI_AP_MODE) {
        mac_addr[5]++;
    } 
    sprintf(resp_val, "%02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    httpd_resp_sendstr_chunk(req, resp_val);
    httpd_resp_sendstr_chunk(req,"</td></tr><tr><td>WiFi mode</td><td><select name=\"wifi_mode\" id=\"wifi_mode\">");
    if (config_wifi_local->mode == ATL_WIFI_AP_MODE) {
        httpd_resp_sendstr_chunk(req, "<option selected value=\"AP_MODE\">Access Point</option> \
                                       <option value=\"STA_MODE\">Station</option> \
                                       </select></td></tr>");
    } else if (config_wifi_local->mode == ATL_WIFI_STA_MODE) {
        httpd_resp_sendstr_chunk(req, "<option value=\"AP_MODE\">Access Point</option> \
                                       <option selected value=\"STA_MODE\">Station</option> \
                                       </select></td></tr>");
    }
    
    /* Process station BSSID name */
    httpd_resp_sendstr_chunk(req, "<tr><td><label for=\"bssid\">Network (BSSID):</label></td> \
                                    <td><input type=\"text\" id=\"bssid\" name=\"bssid\" value=\"");
    sprintf(resp_val, "%s", config_wifi_local->sta_ssid);
    httpd_resp_sendstr_chunk(req, resp_val);
    httpd_resp_sendstr_chunk(req, "\"></td></tr>");

    /* Process station BSSID password */
    httpd_resp_sendstr_chunk(req, "<tr><td><label for=\"pass\">Password:</label></td> \
                                   <td><input type=\"password\" id=\"pass\" name=\"pass\" value=\"");       
    sprintf(resp_val, "%s", config_wifi_local->sta_pass);
    httpd_resp_sendstr_chunk(req, resp_val);
    httpd_resp_sendstr_chunk(req, "\"></td></tr></table><br><div class=\"reboot-msg\" id=\"delayMsg\"></div>");

    /* Send button chunks */    
    httpd_resp_sendstr_chunk(req, "<br><input class=\"btn_generic\" name=\"btn_save_reboot\" type=\"submit\" \
                                    onclick=\"delayRedirect()\" value=\"Save & Reboot\"></div></form>");     

    /* Send footer chunck */
    const size_t footer_size = (footer_end - footer_start);
    httpd_resp_send_chunk(req, (const char *)footer_start, footer_size);

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);
    free(config_wifi_local);
    return ESP_OK;

/* Error procedure */
error_proc:
    httpd_resp_send_500(req);
    atl_led_builtin_blink(6, 100, 255, 0, 0);
    ESP_LOGE(TAG, "Error: %d = %s", err, esp_err_to_name(err));
    return err;
}

/**
 * @brief HTTP GET Handler for WIFI configuration webpage
 */
static const httpd_uri_t conf_wifi_get = {
    .uri = "/conf_wifi.html",
    .method = HTTP_GET,
    .handler = conf_wifi_get_handler
};

/**
 * @fn conf_wifi_post_handler(httpd_req_t *req)
 * @brief POST handler for WiFi configuration webpage
 * @details HTTP POST handler for WiFi configuration webpage
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t conf_wifi_post_handler(httpd_req_t *req) {
    esp_err_t err = ESP_OK;
    ESP_LOGD(TAG, "Processing POST conf_wifi_post");

    /* Allocate memory to process request */
    int    ret;
    size_t off = 0;
    char*  buf = calloc(1, req->content_len + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for wifi POST!", req->content_len + 1);
        err = ESP_ERR_NO_MEM;
        goto error_proc;
    }

    /* Receive all data */
    while (off < req->content_len) {
        /* Read data received in the request */
        ret = httpd_req_recv(req, buf + off, req->content_len - off);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            free(buf);
            err = ESP_ERR_TIMEOUT;
            goto error_proc;
        }
        off += ret;
    }
    buf[off] = '\0';

    /* Allocate memory to local copy of ANALOG INPUT configuration */
    atl_config_wifi_t* config_wifi_local = calloc(1, sizeof(atl_config_wifi_t));
    if (config_wifi_local == NULL) {
        httpd_resp_send_500(req);
        ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for wifi configuration POST!", sizeof(atl_config_wifi_t));
        err = ESP_ERR_NO_MEM;
        goto error_proc;
    }

    /* Make a local copy of ANALOG INPUT configuration */
    if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(config_wifi_local, &atl_config.wifi, sizeof(atl_config_wifi_t));
        
        /* Release cofiguration mutex */
        xSemaphoreGive(atl_config_mutex);
    }
    else {
        httpd_resp_send_408(req);
        ESP_LOGE(TAG, "Fail to get wifi configuration mutex!");
        err = ESP_ERR_TIMEOUT;
        free(config_wifi_local);
        goto error_proc;
    }

    /* Search for custom header field */    
    char* token;
    char* key;
    char* value;
    int token_len, value_len;
    token = strtok(buf, "&");
    while (token) {
        token_len = strlen(token);
        value = strstr(token, "=") + 1;
        value_len = strlen(value);
        key = calloc(1, (token_len - value_len));
        if (!key) {
            httpd_resp_send_500(req);
            ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for wifi payload POST!", (token_len - value_len));
            err = ESP_ERR_NO_MEM;
            goto error_proc;
        }
        memcpy(key, token, (token_len - value_len - 1));
        if (strcmp(key, "wifi_mode") == 0) {
            if (strcmp(value, "AP_MODE") == 0) {
                config_wifi_local->mode = ATL_WIFI_AP_MODE;
            } else if (strcmp(value, "STA_MODE") == 0) {
                config_wifi_local->mode = ATL_WIFI_STA_MODE;
            } else if (strcmp(value, "WIFI_DISABLED") == 0) {
                config_wifi_local->mode = ATL_WIFI_DISABLED;
            }
            ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
        } else if (strcmp(key, "bssid") == 0) {
            strncpy((char*)&config_wifi_local->sta_ssid, value, sizeof(config_wifi_local->sta_ssid));
            ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
        } else if (strcmp(key, "pass") == 0) {            
            urldecode(value);
            strncpy((char*)&config_wifi_local->sta_pass, value, sizeof(config_wifi_local->sta_pass));
            ESP_LOGI(TAG, "Updating [%s:%s]", key,  value);
        }
        free(key);
        token = strtok(NULL, "&");
    }

    /* Apply local copy of WIFI configuration to actual main configuration */
    if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(&atl_config.wifi, config_wifi_local, sizeof(atl_config_wifi_t));
        
        /* Release cofiguration mutex */
        xSemaphoreGive(atl_config_mutex);
    }
    else {
        httpd_resp_send_408(req);
        ESP_LOGE(TAG, "Fail to get wifi configuration mutex!");
        err = ESP_ERR_TIMEOUT;
        free(config_wifi_local);
        goto error_proc;
    }

    /* Commit configuration to NVS */
    err = atl_config_commit_nvs();
    if (err == ESP_OK) {
        free(config_wifi_local);
        atl_led_builtin_blink(10, 100, 255, 69, 0);
        ESP_LOGW(TAG, "Restarting ATL100!");
        esp_restart();
        return err;
    } else {
        httpd_resp_send_500(req);
        ESP_LOGE(TAG, "Fail to commit wifi configuration to NVS!");
        free(config_wifi_local);
        goto error_proc;
    }

/* Error procedure */
error_proc:
    atl_led_builtin_blink(6, 100, 255, 0, 0);
    ESP_LOGE(TAG, "Error: %d = %s", err, esp_err_to_name(err));
    return err;
}

/**
 * @brief HTTP POST Handler for WIFI configuration webpage
 */
static const httpd_uri_t conf_wifi_post = {
    .uri = "/conf_wifi_post.html",
    .method = HTTP_POST,
    .handler = conf_wifi_post_handler
};

/**
 * @fn conf_configuration_get_handler(httpd_req_t *req)
 * @brief GET handler
 * @details HTTP GET Handler
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t conf_configuration_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Sending conf_cofiguration.html");

    /* Set response status, type and header */
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");

    /* Send header chunck */
    const size_t header_size = (header_end - header_start);
    httpd_resp_send_chunk(req, (const char *)header_start, header_size);    

    /* Send download session chunks */
    httpd_resp_sendstr_chunk(req, "<div class=\"row\" style=\"border: 1px solid #223904\"> \
                        <p>Download ATL200 configuration file (JSON)</p><input class=\"btn_generic\" name=\"btn_get_conf\" \
                        onclick=\"getConfJSONFile()\" value=\"Download\"></div><br><br>");

    /* Send upload session chunks */
    httpd_resp_sendstr_chunk(req, "<div class=\"row\" style=\"border: 1px solid #223904\"> \
                        <p>Upload ATL200 configuration file (JSON)</p><form action=\"/api/v1/system/set/conf\" method=\"post\" enctype=\"multipart/form-data\"> \
                        <input id=\"file\" name=\"file\" type=\"file\" accept=\".json\" />");
    
    /* Send button chunks */
    httpd_resp_sendstr_chunk(req, "<br><input class=\"btn_generic\" name=\"btn_set_conf\" type=\"submit\" \
                                    onclick=\"uploadFile()\" value=\"Upload\"></form></div>");
    
    /* Send footer chunck */
    const size_t footer_size = (footer_end - footer_start);
    httpd_resp_send_chunk(req, (const char *)footer_start, footer_size);

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

/**
 * @brief HTTP GET Handler for configuration webpage
 */
static const httpd_uri_t conf_configuration_get = {
    .uri = "/conf_configuration.html",
    .method = HTTP_GET,
    .handler = conf_configuration_get_handler
};

/**
 * @fn api_v1_system_get_conf_handler(httpd_req_t *req)
 * @brief GET handler
 * @details HTTP GET Handler
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t api_v1_system_get_conf_handler(httpd_req_t *req) {
    esp_err_t err = ESP_OK;
    ESP_LOGD(TAG, "Processing /api/v1/system/get/conf");

    /* Set response status, type and header */
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");

    /* Allocate memory to local copy of X200 configuration */
    atl_config_t* config_local = calloc(1, sizeof(atl_config_t));
    if (config_local == NULL) {
        httpd_resp_send_500(req);
        ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for /api/v1/system/get/conf!", sizeof(atl_config_t));
        err = ESP_ERR_NO_MEM;
        goto error_proc;
    }

    /* Make a local copy of ATL200 configuration */
    if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(config_local, &atl_config, sizeof(atl_config_t));
        
        /* Release cofiguration mutex */
        xSemaphoreGive(atl_config_mutex);
    }
    else {
        httpd_resp_send_408(req);
        ESP_LOGE(TAG, "Fail to get ATL200 configuration mutex!");
        err = ESP_ERR_TIMEOUT;
        free(config_local);
        goto error_proc;
    }

    /* Create root JSON object */
    cJSON *root = cJSON_CreateObject();
    esp_app_desc_t app_info;
    const esp_partition_t *partition_info_ptr;
    partition_info_ptr = esp_ota_get_running_partition();
    esp_ota_get_partition_description(partition_info_ptr, &app_info);
    cJSON_AddStringToObject(root, "current_fw_title", app_info.project_name);
    cJSON_AddStringToObject(root, "current_fw_version", app_info.version);
        
    /* Create system JSON object */
    cJSON *root_system = cJSON_CreateObject();
    cJSON_AddStringToObject(root_system, "led_behaviour", atl_led_get_behaviour_str(config_local->system.led_behaviour));
    cJSON_AddItemToObject(root, "system", root_system);

    /* Create ota JSON object */
    cJSON *root_ota = cJSON_CreateObject();
    // cJSON_AddStringToObject(root_ota, "behaviour", atl_ota_get_behaviour_str(config_local->ota.behaviour));
    cJSON_AddItemToObject(root, "ota", root_ota);
        
    /* Create wifi JSON object */
    cJSON *root_wifi = cJSON_CreateObject();
    cJSON_AddStringToObject(root_wifi, "mode", atl_wifi_get_mode_str(config_local->wifi.mode));
    cJSON_AddStringToObject(root_wifi, "ap_ssid", (const char*)&config_local->wifi.ap_ssid);
    cJSON_AddStringToObject(root_wifi, "ap_pass", (const char*)&config_local->wifi.ap_pass);
    cJSON_AddNumberToObject(root_wifi, "ap_channel", config_local->wifi.ap_channel);
    cJSON_AddNumberToObject(root_wifi, "ap_max_conn", config_local->wifi.ap_max_conn);
    cJSON_AddStringToObject(root_wifi, "sta_ssid", (const char*)&config_local->wifi.sta_ssid);        
    cJSON_AddStringToObject(root_wifi, "sta_pass", (const char*)&config_local->wifi.sta_pass);
    cJSON_AddNumberToObject(root_wifi, "sta_channel", config_local->wifi.sta_channel);
    cJSON_AddNumberToObject(root_wifi, "sta_max_conn_retry", config_local->wifi.sta_max_conn_retry);
    cJSON_AddItemToObject(root, "wifi", root_wifi);

    /* Create webserver JSON object */
    cJSON *root_webserver = cJSON_CreateObject();
    cJSON_AddStringToObject(root_webserver, "mode", atl_webserver_get_mode_str(config_local->webserver.mode));
    cJSON_AddStringToObject(root_webserver, "username", (const char*)&config_local->webserver.admin_user);
    cJSON_AddStringToObject(root_webserver, "password", (const char*)&config_local->webserver.admin_pass);        
    cJSON_AddItemToObject(root, "webserver", root_webserver);

    /* Create mqtt client JSON object */
    cJSON *root_mqtt_client = cJSON_CreateObject();
    // cJSON_AddStringToObject(root_mqtt_client, "mode", atl_mqtt_get_mode_str(config_local->mqtt_client.mode));
    // cJSON_AddStringToObject(root_mqtt_client, "broker_address", (const char*)&config_local->mqtt_client.broker_address);
    // cJSON_AddNumberToObject(root_mqtt_client, "broker_port", config_local->mqtt_client.broker_port);        
    // cJSON_AddStringToObject(root_mqtt_client, "transport", atl_mqtt_get_transport_str(config_local->mqtt_client.transport));        
    // cJSON_AddBoolToObject(root_mqtt_client, "disable_cn_check", config_local->mqtt_client.disable_cn_check);
    // cJSON_AddStringToObject(root_mqtt_client, "user", (const char*)&config_local->mqtt_client.user);
    // cJSON_AddStringToObject(root_mqtt_client, "pass", (const char*)&config_local->mqtt_client.pass);
    // cJSON_AddNumberToObject(root_mqtt_client, "qos", config_local->mqtt_client.qos);
    // cJSON_AddItemToObject(root, "mqtt_client", root_mqtt_client);

    /* Put JSON to response message */
    const char *conf_info = cJSON_Print(root);

    /* Sent response */
    httpd_resp_sendstr(req, conf_info);

    /* Free objects */                
    cJSON_Delete(root);
    free((void *)conf_info);
    return ESP_OK;

/* Error procedure */
error_proc:
    atl_led_builtin_blink(6, 100, 255, 0, 0);
    ESP_LOGE(TAG, "Error: %d = %s", err, esp_err_to_name(err));
    return err;
}

/**
 * @brief HTTP GET Handler for configuration webpage
 */
static const httpd_uri_t api_v1_system_get_conf = {
    .uri = "/api/v1/system/get/conf",
    .method = HTTP_GET,
    .handler = api_v1_system_get_conf_handler
};

/**
 * @fn api_v1_system_set_conf_handler(httpd_req_t *req)
 * @brief POST handler
 * @details HTTP POST Handler
 * @param[in] req - request
 * @return ESP error code
*/
static esp_err_t api_v1_system_set_conf_handler(httpd_req_t *req) {    
    esp_err_t err = ESP_OK;
    ESP_LOGI(TAG, "Processing /api/v1/system/set/conf");
    
    /* Allocate memory to process request */
    int    ret;
    size_t off = 0;
    char*  buf = calloc(1, req->content_len + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for /api/v1/system/set/conf!", req->content_len + 1);
        err = ESP_ERR_NO_MEM;
        goto error_proc;
    }

    /* Receive all data */
    while (off < req->content_len) {
        /* Read data received in the request */
        ret = httpd_req_recv(req, buf + off, req->content_len - off);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            free(buf);
            err = ESP_ERR_TIMEOUT;
            goto error_proc;
        }
        off += ret;
    }
    buf[off] = '\0';

    /* Allocate memory to local copy of X200 configuration */
    atl_config_t* config_local = calloc(1, sizeof(atl_config_t));
    if (config_local == NULL) {
        httpd_resp_send_500(req);
        ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for /api/v1/system/set/conf!", sizeof(atl_config_t));
        err = ESP_ERR_NO_MEM;
        goto error_proc;
    }

    /* Make a local copy of ATL200 configuration */
    if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(config_local, &atl_config, sizeof(atl_config_t));
        
        /* Release cofiguration mutex */
        xSemaphoreGive(atl_config_mutex);
    }
    else {
        httpd_resp_send_408(req);
        ESP_LOGE(TAG, "Fail to get ATL100 configuration mutex!");
        err = ESP_ERR_TIMEOUT;
        free(config_local);
        goto error_proc;
    }

    /* Get JSON begin from HTTP */
    char* json_begin = strstr(buf, "{");        

    /* Parse JSON file */
    cJSON *json = cJSON_ParseWithLength(json_begin, strlen(json_begin));
    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "Error before: %s\n", error_ptr);
        }
        cJSON_Delete(json);
        free(buf);
        httpd_resp_send_500(req);
        err = ESP_ERR_INVALID_ARG;
        goto error_proc;
    } else {
        ESP_LOGI(TAG, "Parsing JSON configuration file...");
        
        /* Get SYSTEM parameters */
        cJSON *root = cJSON_GetObjectItem(json, "system");
        if (root != NULL) {
            cJSON *led_behaviour = cJSON_GetObjectItem(root, "led_behaviour");
            if (led_behaviour != NULL) {
                config_local->system.led_behaviour = atl_led_get_behaviour(led_behaviour->valuestring);
            }
        }

        /* Get OTA parameters */
        root = cJSON_GetObjectItem(json, "ota");
        if (root != NULL) {
            cJSON *behaviour = cJSON_GetObjectItem(root, "behaviour");
            if (behaviour != NULL) {
                // config_local->ota.behaviour = atl_ota_get_behaviour(behaviour->valuestring);
            }
        }

        /* Get WIFI parameters */
        root = cJSON_GetObjectItem(json, "wifi");
        if (root != NULL) {
            cJSON *mode = cJSON_GetObjectItem(root, "mode");
            if (mode != NULL) {
                config_local->wifi.mode = atl_wifi_get_mode(mode->valuestring);
            }
            cJSON *ap_ssid = cJSON_GetObjectItem(root, "ap_ssid");
            if (ap_ssid != NULL) {
                strncpy((char*)config_local->wifi.ap_ssid, ap_ssid->valuestring, sizeof(config_local->wifi.ap_ssid));
            }
            cJSON *ap_pass = cJSON_GetObjectItem(root, "ap_pass");
            if (ap_pass != NULL) {
                strncpy((char*)config_local->wifi.ap_pass, ap_pass->valuestring, sizeof(config_local->wifi.ap_pass));
            }
            cJSON *ap_channel = cJSON_GetObjectItem(root, "ap_channel");
            if (ap_channel != NULL) {
                config_local->wifi.ap_channel = ap_channel->valueint;
            }
            cJSON *ap_max_conn = cJSON_GetObjectItem(root, "ap_max_conn");
            if (ap_max_conn != NULL) {
                config_local->wifi.ap_max_conn = ap_max_conn->valueint;
            }
            cJSON *sta_ssid = cJSON_GetObjectItem(root, "sta_ssid");
            if (sta_ssid != NULL) {
                strncpy((char*)config_local->wifi.sta_ssid, sta_ssid->valuestring, sizeof(config_local->wifi.sta_ssid));
            }
            cJSON *sta_pass = cJSON_GetObjectItem(root, "sta_pass");
            if (sta_pass != NULL) {
                strncpy((char*)config_local->wifi.sta_pass, sta_pass->valuestring, sizeof(config_local->wifi.sta_pass));
            }
            cJSON *sta_channel = cJSON_GetObjectItem(root, "sta_channel");
            if (sta_channel != NULL) {
                config_local->wifi.sta_channel = sta_channel->valueint;
            }
            cJSON *sta_max_conn_retry = cJSON_GetObjectItem(root, "sta_max_conn_retry");
            if (sta_max_conn_retry != NULL) {
                config_local->wifi.sta_max_conn_retry = sta_max_conn_retry->valueint;
            }
        }
        root = cJSON_GetObjectItem(json, "webserver");
        if (root != NULL) {
            cJSON *username = cJSON_GetObjectItem(root, "username");
            if (username != NULL) {
                strncpy((char*)config_local->webserver.admin_user, username->valuestring, sizeof(config_local->webserver.admin_user));
            }
            cJSON *password = cJSON_GetObjectItem(root, "password");
            if (password != NULL) {
                strncpy((char*)config_local->webserver.admin_pass, password->valuestring, sizeof(config_local->webserver.admin_pass));
            }
        }
        // root = cJSON_GetObjectItem(json, "mqtt_client");
        // if (root != NULL) {
        //     cJSON *mode = cJSON_GetObjectItem(root, "mode");
        //     if (mode != NULL) {
        //         config_local->mqtt_client.mode = atl_mqtt_get_mode(mode->valuestring);
        //     }
        //     cJSON *broker_address = cJSON_GetObjectItem(root, "broker_address");
        //     if (broker_address != NULL) {
        //         strncpy((char*)config_local->mqtt_client.broker_address, broker_address->valuestring, sizeof(config_local->mqtt_client.broker_address));
        //     }
        //     cJSON *broker_port = cJSON_GetObjectItem(root, "broker_port");
        //     if (broker_port != NULL) {
        //         config_local->mqtt_client.broker_port = broker_port->valueint;
        //     }
        //     cJSON *transport = cJSON_GetObjectItem(root, "transport");
        //     if (transport != NULL) {
        //         config_local->mqtt_client.transport = atl_mqtt_get_transport(transport->valuestring);
        //     }
        //     cJSON *disable_cn_check = cJSON_GetObjectItem(root, "disable_cn_check");
        //     if (disable_cn_check != NULL) {
        //         config_local->mqtt_client.disable_cn_check = disable_cn_check->valueint;
        //     }
        //     cJSON *user = cJSON_GetObjectItem(root, "user");
        //     if (user != NULL) {
        //         strncpy((char*)config_local->mqtt_client.user, user->valuestring, sizeof(config_local->mqtt_client.user));
        //     }
        //     cJSON *pass = cJSON_GetObjectItem(root, "pass");
        //     if (pass != NULL) {
        //         strncpy((char*)config_local->mqtt_client.pass, pass->valuestring, sizeof(config_local->mqtt_client.pass));
        //     }
        //     cJSON *qos = cJSON_GetObjectItem(root, "qos");
        //     if (qos != NULL) {
        //         config_local->mqtt_client.qos = qos->valueint;
        //     }
        // }

        /* Delete JSON object */
        cJSON_Delete(root);
    }
    
    /* Update current configuration */
    if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(&atl_config, config_local, sizeof(atl_config_t));
        xSemaphoreGive(atl_config_mutex);
    }
    else {
        httpd_resp_send_408(req);
        ESP_LOGW(TAG, "Fail to get ATL100 configuration mutex!");
        err = ESP_ERR_TIMEOUT;
        goto error_proc;
    }

    /* Commit configuration to NVS */
    err = atl_config_commit_nvs();
    if (err == ESP_OK) {
        /* Send the HTTP response */
        httpd_resp_set_status(req, "302 Temporary Redirect");
        httpd_resp_set_hdr(req, "Location", "/conf_configuration.html");

        /* iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient. */
        httpd_resp_send(req, "Redirect to the home portal", HTTPD_RESP_USE_STRLEN);

        /* Blink LED */
        atl_led_builtin_blink(6, 100, 255, 69, 0);
        return ESP_OK;
    } else {
        httpd_resp_send_500(req);
        ESP_LOGE(TAG, "Fail to commit ATL100 configuration to NVS!");
        goto error_proc;
    }

/* Error procedure */
error_proc:
    atl_led_builtin_blink(6, 100, 255, 0, 0);
    ESP_LOGE(TAG, "Error: %d = %s", err, esp_err_to_name(err));
    return err;
}

/**
 * @brief HTTP POST Handler for configuration webpage
 */
static const httpd_uri_t api_v1_system_set_conf = {
    .uri = "/api/v1/system/set/conf",
    .method = HTTP_POST,
    .handler = api_v1_system_set_conf_handler
};

/**
 * @fn conf_fw_get_update_handler(httpd_req_t *req)
 * @brief GET handler
 * @details HTTP GET Handler
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t conf_fw_update_get_handler(httpd_req_t *req) {
    esp_err_t err = ESP_OK;
    ESP_LOGD(TAG, "Sending conf_fw_update.html");
    char resp_val[65];

    /* Set response status, type and header */
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");

    /* Send header chunck */
    const size_t header_size = (header_end - header_start);
    httpd_resp_send_chunk(req, (const char *)header_start, header_size);

    /* Send information chunks */
    httpd_resp_sendstr_chunk(req, "<table><tr><th>Parameter</th><th>Value</th></tr><tr><td>Firmware version</td><td>");
    esp_app_desc_t app_info;
    const esp_partition_t *partition_info_ptr;
    partition_info_ptr = esp_ota_get_running_partition();
    if (esp_ota_get_partition_description(partition_info_ptr, &app_info) == ESP_OK) {
        sprintf(resp_val, "%s", app_info.version);
        httpd_resp_sendstr_chunk(req, resp_val);
        httpd_resp_sendstr_chunk(req, "</td></tr><tr><td>Build</td><td>");
        sprintf(resp_val, "%s %s", app_info.date, app_info.time);
        httpd_resp_sendstr_chunk(req, resp_val);
        httpd_resp_sendstr_chunk(req, "</td></tr><tr><td>SDK version</td><td>");
        sprintf(resp_val, "%s", app_info.idf_ver);
        httpd_resp_sendstr_chunk(req, resp_val);
        httpd_resp_sendstr_chunk(req, "</td></tr><tr><td>Running partition name</td><td>");
        sprintf(resp_val, "%s", partition_info_ptr->label);
        httpd_resp_sendstr_chunk(req, resp_val);
        httpd_resp_sendstr_chunk(req, "</td></tr><tr><td>Running partition size</td><td>");
        sprintf(resp_val, "%ld bytes", partition_info_ptr->size);
        httpd_resp_sendstr_chunk(req, resp_val);
    }
    const esp_partition_pos_t running_pos  = {
        .offset = partition_info_ptr->address,
        .size = partition_info_ptr->size,
    };
    esp_image_metadata_t data;
    data.start_addr = running_pos.offset;
    esp_image_verify(ESP_IMAGE_VERIFY, &running_pos, &data);
    httpd_resp_sendstr_chunk(req, "</td></tr><tr><td>Running firmware size</td><td>");
    sprintf(resp_val, "%ld bytes", data.image_len);
    httpd_resp_sendstr_chunk(req, resp_val);    
    httpd_resp_sendstr_chunk(req, "</td></tr></table><br><br>");

    /* Allocate memory to local copy of ATL200 configuration */
    atl_config_ota_t* config_ota_local = calloc(1, sizeof(atl_config_ota_t));
    if (config_ota_local == NULL) {
        httpd_resp_send_500(req);
        ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for ota GET!", sizeof(atl_config_ota_t));
        err = ESP_ERR_NO_MEM;
        goto error_proc;
    }

    /* Make a local copy of ATL200 configuration */
    if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(config_ota_local, &atl_config.ota, sizeof(atl_config_ota_t));
        
        /* Release cofiguration mutex */
        xSemaphoreGive(atl_config_mutex);
    }
    else {
        httpd_resp_send_408(req);
        ESP_LOGE(TAG, "Fail to get ota configuration mutex!");
        err = ESP_ERR_TIMEOUT;
        free(config_ota_local);
        goto error_proc;
    }

    /* Send parameters chunks */
    httpd_resp_sendstr_chunk(req, "<form action=\"conf_fw_update_post.html\" method=\"post\"> \
                                      <div class=\"row\"> \
                                      <table><tr><th>Parameter</th><th>Value</th></tr> \
                                      <tr><td>FW Update Behaviour</td>");
    
    httpd_resp_sendstr_chunk(req,"<td><select name=\"ota_behaviour\" id=\"ota_behaviour\">");
/*    if (config_ota_local->behaviour == ATL_OTA_BEHAVIOUR_DISABLED) {
        httpd_resp_sendstr_chunk(req, "<option selected value=\"ATL_OTA_BEHAVIOUR_DISABLED\">Disabled</option> \
                                       <option value=\"ATL_OTA_BEHAVIOUR_VERIFY_NOTIFY\">Verify & Notify</option> \
                                       <option value=\"ATL_OTA_BEHAVIOU_DOWNLOAD\">Download</option> \
                                       <option value=\"ATL_OTA_BEHAVIOU_DOWNLOAD_REBOOT\">Download & Reboot</option> \
                                       </select></td></tr>");
    } else if (config_ota_local->behaviour == ATL_OTA_BEHAVIOUR_VERIFY_NOTIFY) {
        httpd_resp_sendstr_chunk(req, "<option value=\"ATL_OTA_BEHAVIOUR_DISABLED\">Disabled</option> \
                                       <option selected value=\"ATL_OTA_BEHAVIOUR_VERIFY_NOTIFY\">Verify & Notify</option> \
                                       <option value=\"ATL_OTA_BEHAVIOU_DOWNLOAD\">Download</option> \
                                       <option value=\"ATL_OTA_BEHAVIOU_DOWNLOAD_REBOOT\">Download & Reboot</option> \
                                       </select></td></tr>");
    } else if (config_ota_local->behaviour == ATL_OTA_BEHAVIOU_DOWNLOAD) {
        httpd_resp_sendstr_chunk(req, "<option value=\"ATL_OTA_BEHAVIOUR_DISABLED\">Disabled</option> \
                                       <option value=\"ATL_OTA_BEHAVIOUR_VERIFY_NOTIFY\">Verify & Notify</option> \
                                       <option selected value=\"ATL_OTA_BEHAVIOU_DOWNLOAD\">Download</option> \
                                       <option value=\"ATL_OTA_BEHAVIOU_DOWNLOAD_REBOOT\">Download & Reboot</option> \
                                       </select></td></tr>");
    } else if (config_ota_local->behaviour == ATL_OTA_BEHAVIOU_DOWNLOAD_REBOOT) {
        httpd_resp_sendstr_chunk(req, "<option value=\"ATL_OTA_BEHAVIOUR_DISABLED\">Disabled</option> \
                                       <option value=\"ATL_OTA_BEHAVIOUR_VERIFY_NOTIFY\">Verify & Notify</option> \
                                       <option value=\"ATL_OTA_BEHAVIOU_DOWNLOAD\">Download</option> \
                                       <option selected value=\"ATL_OTA_BEHAVIOU_DOWNLOAD_REBOOT\">Download & Reboot</option> \
                                       </select></td></tr>");
    } */
    httpd_resp_sendstr_chunk(req, "</table><br><div class=\"reboot-msg\" id=\"delayMsg\"></div>");

    /* Send button chunks */    
    httpd_resp_sendstr_chunk(req, "<br><input class=\"btn_generic\" name=\"btn_save_reboot\" type=\"submit\" \
                                    onclick=\"delayRedirect()\" value=\"Save & Reboot\"></div></form>"); 

    /* Send footer chunck */
    const size_t footer_size = (footer_end - footer_start);
    httpd_resp_send_chunk(req, (const char *)footer_start, footer_size);

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);
    free(config_ota_local);
    return ESP_OK;

/* Error procedure */
error_proc:
    atl_led_builtin_blink(6, 100, 255, 0, 0);
    ESP_LOGE(TAG, "Error: %d = %s", err, esp_err_to_name(err));
    return err;
}

/**
 * @brief HTTP GET Handler for firmware webpage
 */
static const httpd_uri_t conf_fw_update_get = {
    .uri = "/conf_fw_update.html",
    .method = HTTP_GET,
    .handler = conf_fw_update_get_handler
};

/**
 * @fn conf_fw_update_post_handler(httpd_req_t *req)
 * @brief POST handler
 * @details HTTP POST Handler
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t conf_fw_update_post_handler(httpd_req_t *req) {
    esp_err_t err = ESP_OK;
    ESP_LOGD(TAG, "Processing POST conf_fw_update_post");

    /* Allocate memory to process request */
    int    ret;
    size_t off = 0;
    char*  buf = calloc(1, req->content_len + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for ota POST!", req->content_len + 1);
        err = ESP_ERR_NO_MEM;
        goto error_proc;
    }

    /* Receive all data */
    while (off < req->content_len) {
        /* Read data received in the request */
        ret = httpd_req_recv(req, buf + off, req->content_len - off);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            free(buf);
            err = ESP_ERR_TIMEOUT;
            goto error_proc;
        }
        off += ret;
    }
    buf[off] = '\0';

    /* Allocate memory to local copy of OTA configuration */
    atl_config_ota_t* config_ota_local = calloc(1, sizeof(atl_config_ota_t));
    if (config_ota_local == NULL) {
        httpd_resp_send_408(req);
        ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for ota configuration POST!", sizeof(atl_config_ota_t));
        err = ESP_ERR_NO_MEM;
        goto error_proc;
    }

    /* Make a local copy of OTA configuration */
    if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(config_ota_local, &atl_config.ota, sizeof(atl_config_ota_t));
        
        /* Release cofiguration mutex */
        xSemaphoreGive(atl_config_mutex);
    }
    else {
        httpd_resp_send_408(req);
        ESP_LOGE(TAG, "Fail to get ota configuration mutex!");
        err = ESP_ERR_TIMEOUT;
        free(config_ota_local);
        goto error_proc;
    }

    /* Search for custom header field */
    char* token;
    char* key;
    char* value;
    int token_len, value_len;
    token = strtok(buf, "&");
    while (token) {
        token_len = strlen(token);
        value = strstr(token, "=") + 1;
        value_len = strlen(value);
        key = calloc(1, (token_len - value_len));
        if (!key) {
            httpd_resp_send_500(req);
            ESP_LOGE(TAG, "Fail allocating memory (%d bytes) for ota payload POST!", (token_len - value_len));
            err = ESP_ERR_NO_MEM;
            goto error_proc;
        }
        memcpy(key, token, (token_len - value_len - 1));
        if (strcmp(key, "ota_behaviour") == 0) {
            // if (strcmp(value, "ATL_OTA_BEHAVIOUR_DISABLED") == 0) {
            //     config_ota_local->behaviour = ATL_OTA_BEHAVIOUR_DISABLED;
            // } else if (strcmp(value, "ATL_OTA_BEHAVIOUR_VERIFY_NOTIFY") == 0) {
            //     config_ota_local->behaviour = ATL_OTA_BEHAVIOUR_VERIFY_NOTIFY;
            // } else if (strcmp(value, "ATL_OTA_BEHAVIOU_DOWNLOAD") == 0) {
            //     config_ota_local->behaviour = ATL_OTA_BEHAVIOU_DOWNLOAD;
            // } else if (strcmp(value, "ATL_OTA_BEHAVIOU_DOWNLOAD_REBOOT") == 0) {
            //     config_ota_local->behaviour = ATL_OTA_BEHAVIOU_DOWNLOAD_REBOOT;
            // }
            // ESP_LOGI(TAG, "Updating [%s:%s]", key, value);
            // ESP_LOGI(TAG, "Updating [%s:%d]", key, config_ota_local->behaviour);
        } 
        free(key);
        token = strtok(NULL, "&");
    }
    
    /* Commit configuration to NVS */
    err = atl_config_commit_nvs();
    if (err == ESP_OK) {
        free(config_ota_local);
        atl_led_builtin_blink(6, 100, 255, 69, 0);
        httpd_resp_set_status(req, "302 Temporary Redirect");
        httpd_resp_set_hdr(req, "Location", "/conf_fw_update.html");
        httpd_resp_send(req, "Redirect to the home portal", HTTPD_RESP_USE_STRLEN);
        return err;
    } else {
        httpd_resp_send_500(req);
        ESP_LOGE(TAG, "Fail to commit OTA configuration to NVS!");
        free(config_ota_local);
        goto error_proc;
    }
    
/* Error procedure */
error_proc:
    atl_led_builtin_blink(6, 100, 255, 0, 0);
    ESP_LOGE(TAG, "Error: %d = %s", err, esp_err_to_name(err));
    return err;
}

/**
 * @brief HTTP POST Handler for firmware webpage
 */
static const httpd_uri_t conf_fw_update_post = {
    .uri = "/conf_fw_update_post.html",
    .method = HTTP_POST,
    .handler = conf_fw_update_post_handler
};

/**
 * @fn conf_reboot_get_handler(httpd_req_t *req)
 * @brief GET handler
 * @details HTTP GET Handler
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t conf_reboot_get_handler(httpd_req_t *req) {
    ESP_LOGD(TAG, "Sending conf_reboot.html");
    char resp_val[65];

    /* Set response status, type and header */
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");

    /* Send header chunck */
    const size_t header_size = (header_end - header_start);
    httpd_resp_send_chunk(req, (const char *)header_start, header_size);

    /* Send article chunks */
    httpd_resp_sendstr_chunk(req, "<form action=\"conf_reboot_post.html\" method=\"post\"> \
        <div class=\"row\"> \
        <table><tr><th>Parameter</th><th>Value</th></tr> \
        <tr><td>Last reboot reason</td><td>");
    esp_reset_reason_t reset_reason;
    reset_reason = esp_reset_reason();
    if (reset_reason == ESP_RST_UNKNOWN) {
        sprintf(resp_val, "Reset reason can not be determined");
    } else if (reset_reason == ESP_RST_POWERON) {
        sprintf(resp_val, "Reset due to power-on event");
    } else if (reset_reason == ESP_RST_EXT) {
        sprintf(resp_val, "Reset by external pin");
    } else if (reset_reason == ESP_RST_SW) {
        sprintf(resp_val, "Software reset");
    } else if (reset_reason == ESP_RST_PANIC) {
        sprintf(resp_val, "Software reset due to exception/panic");
    } else if (reset_reason == ESP_RST_INT_WDT) {
        sprintf(resp_val, "Reset (software or hardware) due to interrupt watchdog");
    } else if (reset_reason == ESP_RST_TASK_WDT) {
        sprintf(resp_val, "Reset due to task watchdog");
    } else if (reset_reason == ESP_RST_WDT) {
        sprintf(resp_val, "Reset due to other watchdogs");
    } else if (reset_reason == ESP_RST_DEEPSLEEP) {
        sprintf(resp_val, "Reset after exiting deep sleep mode");
    } else if (reset_reason == ESP_RST_BROWNOUT) {
        sprintf(resp_val, "Brownout reset (software or hardware)");
    } else if (reset_reason == ESP_RST_SDIO) {
        sprintf(resp_val, "Reset over SDIO");
    }
    httpd_resp_sendstr_chunk(req, resp_val);
    httpd_resp_sendstr_chunk(req, "</td></tr></table><br><input class=\"btn_generic\" name=\"btn_reboot\" type=\"submit\" value=\"Reboot ATL200\"></div></form>");

    /* Send footer chunck */
    const size_t footer_size = (footer_end - footer_start);
    httpd_resp_send_chunk(req, (const char *)footer_start, footer_size);

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

/**
 * @brief HTTP GET Handler for reboot webpage
 */
static const httpd_uri_t conf_reboot_get = {
    .uri = "/conf_reboot.html",
    .method = HTTP_GET,
    .handler = conf_reboot_get_handler
};

/**
 * @fn conf_reboot_post_handler(httpd_req_t *req)
 * @brief POST handler
 * @details HTTP POST Handler
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t conf_reboot_post_handler(httpd_req_t *req) {
    ESP_LOGD(TAG, "Processing POST conf_reboot");

    /* Restart ATL device */
    ESP_LOGW(TAG, ">>> Rebooting ATL100!");
    atl_led_builtin_blink(10, 100, 255, 69, 0);
    esp_restart();

    return ESP_OK;
}

/**
 * @brief HTTP POST Handler for reboot webpage
 */
static const httpd_uri_t conf_reboot_post = {
    .uri = "/conf_reboot_post.html",
    .method = HTTP_POST,
    .handler = conf_reboot_post_handler
};

/**
 * @typedef basic_auth_info_t
 */
typedef struct {
    char *username;
    char *password;
} basic_auth_info_t;

/**
 * @fn httpd_auth_basic(const char *username, const char *password)
 * @brief Basic auth
 * @details Basic auth
 * @param[in] username - username
 * @param[in] password - password
 * @return digest
 */
static char *httpd_auth_basic(const char *username, const char *password) {
    int out;
    char *user_info = NULL;
    char *digest = NULL;
    size_t n = 0;
    asprintf(&user_info, "%s:%s", username, password);
    if (!user_info) {
        ESP_LOGE(TAG, "No enough memory for user information");
        return NULL;
    }
    esp_crypto_base64_encode(NULL, 0, &n, (const unsigned char *)user_info, strlen(user_info));
    
    /* 6: The length of the "Basic " string
     * n: Number of bytes for a base64 encode format
     * 1: Number of bytes for a reserved which be used to fill zero
    */
    digest = calloc(1, 6 + n + 1);
    if (digest) {
        strcpy(digest, "Basic ");
        esp_crypto_base64_encode((unsigned char *)digest + 6, n, (size_t *)&out, (const unsigned char *)user_info, strlen(user_info));
    }
    free(user_info);
    return digest;
}

/**
 * @fn basic_auth_get_handler(httpd_req_t *req)
 * @brief GET handler
 * @details HTTP GET Handler
 * @param[in] req - request
 * @return ESP error code
 */
static esp_err_t basic_auth_get_handler(httpd_req_t *req) {
    esp_err_t err = ESP_OK;
    char *buf = NULL;
    size_t buf_len = 0;
    basic_auth_info_t *basic_auth_info = req->user_ctx;

    buf_len = httpd_req_get_hdr_value_len(req, "Authorization") + 1;
    if (buf_len > 1) {
        buf = calloc(1, buf_len);
        if (!buf) {
            ESP_LOGE(TAG, "No enough memory for basic authorization");
            err = ESP_ERR_NO_MEM;
            goto error_proc;
        }

        if (httpd_req_get_hdr_value_str(req, "Authorization", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header -> Authorization: %s", buf);
        } else {
            ESP_LOGE(TAG, "No auth value received!");
        }

        char *auth_credentials = httpd_auth_basic(basic_auth_info->username, basic_auth_info->password);
        if (!auth_credentials) {
            ESP_LOGE(TAG, "No enough memory for basic authorization credentials");
            free(buf);
            err = ESP_ERR_NO_MEM;
            goto error_proc;
        }

        if (strncmp(auth_credentials, buf, buf_len)) {
            ESP_LOGE(TAG, "Not authenticated!");
            httpd_resp_set_status(req, HTTPD_401);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Connection", "keep-alive");
            httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Hello\"");
            httpd_resp_send(req, NULL, 0);
        } else {
            ESP_LOGI(TAG, "Authenticated!");
            home_get_handler(req);
        }
        free(auth_credentials);
        free(buf);
    } else {
        ESP_LOGE(TAG, "No auth header received!");
        httpd_resp_set_status(req, HTTPD_401);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Connection", "keep-alive");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Hello\"");
        httpd_resp_send(req, NULL, 0);
    }

    return ESP_OK;

/* Error procedure */
error_proc:
    httpd_resp_send_500(req);
    atl_led_builtin_blink(3, 100, 255, 0, 0);
    ESP_LOGE(TAG, "Error: %d = %s", err, esp_err_to_name(err));
    return err;
}

/**
 * @brief HTTP GET Handler for /index.html
 */
static httpd_uri_t basic_auth = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = basic_auth_get_handler,
};

/**
 * @fn httpd_register_basic_auth(httpd_handle_t server)
 * @brief Register basic auth
 * @details Register basic auth
 * @param[in] server - HTTPD server
 */
static void httpd_register_basic_auth(httpd_handle_t server) {
    basic_auth_info_t *basic_auth_info = calloc(1, sizeof(basic_auth_info_t));
    
    /* Get cofiguration mutex to setup webserver config */    
    if (xSemaphoreTake(atl_config_mutex, portMAX_DELAY) == pdTRUE) {
        basic_auth_info->username = calloc(1, sizeof(atl_config.webserver.admin_user));
        snprintf((char*)basic_auth_info->username, sizeof(atl_config.webserver.admin_user), "%s", atl_config.webserver.admin_user);
        basic_auth_info->password = calloc(1, sizeof(atl_config.webserver.admin_pass));
        snprintf((char*)basic_auth_info->password, sizeof(atl_config.webserver.admin_pass), "%s", atl_config.webserver.admin_pass);        

        /* Release cofiguration mutex */
        xSemaphoreGive(atl_config_mutex);
    }
    else {
        ESP_LOGW(TAG, "Fail to get configuration mutex!");
    } 

    basic_auth.user_ctx = basic_auth_info;
    httpd_register_uri_handler(server, &basic_auth);
}

/**
 * @fn print_peer_cert_info(const mbedtls_ssl_context *ssl)
 * @brief Print SSL certificate info
 * @details Print SSL certificate info
*/
static void print_peer_cert_info(const mbedtls_ssl_context *ssl) {
    const mbedtls_x509_crt *cert;
    const size_t buf_size = 1024;
    char *buf = calloc(buf_size, sizeof(char));
    if (buf == NULL) {
        ESP_LOGE(TAG, "Out of memory - Callback execution failed!");
        return;
    }

    /* Logging the peer certificate info */
    cert = mbedtls_ssl_get_peer_cert(ssl);
    if (cert != NULL) {
        mbedtls_x509_crt_info((char *) buf, buf_size - 1, "    ", cert);
        ESP_LOGI(TAG, "Peer certificate info:\n%s", buf);
    } else {
        ESP_LOGW(TAG, "Could not obtain the peer certificate!");
    }

    free(buf);
}

/**
 * @fn https_server_user_callback(esp_https_server_user_cb_arg_t *user_cb)
 * @brief HTTPS server callback
 * @details HTTPS server callback 
*/
static void https_server_user_callback(esp_https_server_user_cb_arg_t *user_cb) {
    mbedtls_ssl_context *ssl_ctx = NULL;
    switch(user_cb->user_cb_state) {
        case HTTPD_SSL_USER_CB_SESS_CREATE:
            ESP_LOGI(TAG, "HTTPS session creation");

            // Logging the socket FD
            int sockfd = -1;
            esp_err_t esp_ret;
            esp_ret = esp_tls_get_conn_sockfd(user_cb->tls, &sockfd);
            if (esp_ret != ESP_OK) {
                ESP_LOGE(TAG, "Error in obtaining the sockfd from tls context");
                break;
            }
            ESP_LOGI(TAG, "Socket FD: %d", sockfd);
            ssl_ctx = (mbedtls_ssl_context *) esp_tls_get_ssl_context(user_cb->tls);
            if (ssl_ctx == NULL) {
                ESP_LOGE(TAG, "Error in obtaining ssl context");
                break;
            }
            // Logging the current ciphersuite
            ESP_LOGI(TAG, "Current Ciphersuite: %s", mbedtls_ssl_get_ciphersuite(ssl_ctx));
            break;

        case HTTPD_SSL_USER_CB_SESS_CLOSE:
            ESP_LOGI(TAG, "HTTPS session close");

            // Logging the peer certificate
            ssl_ctx = (mbedtls_ssl_context *) esp_tls_get_ssl_context(user_cb->tls);
            if (ssl_ctx == NULL) {
                ESP_LOGE(TAG, "Error in obtaining ssl context");
                break;
            }
            print_peer_cert_info(ssl_ctx);
            break;

        default:
            ESP_LOGE(TAG, "HTTPS illegal state!");
            return;
    }
}

/**
 * @fn atl_webserver_init(void)
 * @brief Initialize Webserver.
 * @return err ESP_OK if success.
 */
esp_err_t atl_webserver_init(void) {
    httpd_handle_t server = NULL;
    esp_err_t err = ESP_OK;
    httpd_config_t config_http = HTTPD_DEFAULT_CONFIG();
    httpd_ssl_config_t config_https = HTTPD_SSL_CONFIG_DEFAULT();

    /* Create default webserver initialization */
    if (atl_config.webserver.mode == ATL_WEBSERVER_DISABLED) {
        return err;
    } else if (atl_config.webserver.mode == ATL_WEBSERVER_HTTP) {    
        config_http.max_uri_handlers = 25;
        config_http.max_open_sockets = 7;
        config_http.lru_purge_enable = true;
    } else if (atl_config.webserver.mode == ATL_WEBSERVER_HTTPS) {    
        config_https.httpd.max_uri_handlers = 25;
        config_https.httpd.max_open_sockets = 7;
        config_https.httpd.lru_purge_enable = true;
        config_https.user_cb = https_server_user_callback;
        // config_https.servercert = servercert_start;
        // config_https.servercert_len = servercert_end - servercert_start;
        // config_https.prvtkey_pem = prvtkey_pem_start;
        // config_https.prvtkey_len = prvtkey_pem_end - prvtkey_pem_start;
    }
    
    /* Start the httpd server */
    if (atl_config.webserver.mode == ATL_WEBSERVER_HTTP) {
        err = httpd_start(&server, &config_http);
    } else if (atl_config.webserver.mode == ATL_WEBSERVER_HTTPS) {
        err = httpd_ssl_start(&server, &config_https);
    }
    if (err == ESP_OK) {
        // Set URI handlers
        ESP_LOGD(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &favicon);
        httpd_register_uri_handler(server, &css);
        httpd_register_uri_handler(server, &js);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
        httpd_register_uri_handler(server, &home_get);
        httpd_register_uri_handler(server, &conf_webserver_get);
        httpd_register_uri_handler(server, &conf_webserver_post);
        httpd_register_uri_handler(server, &conf_mqtt_get);
        httpd_register_uri_handler(server, &conf_mqtt_post);  
        httpd_register_uri_handler(server, &conf_telemetry_get);
        httpd_register_uri_handler(server, &conf_telemetry_post);
        httpd_register_uri_handler(server, &conf_wifi_get);
        httpd_register_uri_handler(server, &conf_wifi_post);
        httpd_register_uri_handler(server, &conf_configuration_get);
        httpd_register_uri_handler(server, &api_v1_system_get_conf);
        httpd_register_uri_handler(server, &api_v1_system_set_conf);
        httpd_register_uri_handler(server, &conf_fw_update_get);
        httpd_register_uri_handler(server, &conf_fw_update_post);
        httpd_register_uri_handler(server, &conf_reboot_get);
        httpd_register_uri_handler(server, &conf_reboot_post);
        httpd_register_basic_auth(server);
    } else {        
        ESP_LOGE(TAG, "Fail starting webserver!");
        atl_led_set_color(255, 0, 0);
        ESP_LOGE(TAG, "Error: %d = %s", err, esp_err_to_name(err));        
    }
    return err;
}

/**
 * @brief Get the WEBSERVER mode string object
 * @param mode 
 * @return Function enum const* 
 */
const char* atl_webserver_get_mode_str(atl_webserver_mode_e mode) {
    return atl_webserver_mode_str[mode];
}

/**
 * @brief Get the WEBSERVER mode enum
 * @param mode_str 
 * @return Function enum
 */
atl_webserver_mode_e atl_webserver_get_mode(char* mode_str) {
    uint8_t i = 0;
    while (atl_webserver_mode_str[i] != NULL) {
        if (strcmp(mode_str, atl_webserver_mode_str[i]) == 0) {
            return i;
        } else {
            i++;
        }
    }
    return 255;
}