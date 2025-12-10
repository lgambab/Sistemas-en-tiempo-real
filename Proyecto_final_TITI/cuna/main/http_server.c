/*
 * http_server.c
 *
 *  Created on: Oct 20, 2021
 *      Author: kjagu
 */

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "sys/param.h"
#include <stdlib.h>

#include "http_server.h"
#include "tasks_common.h"
#include "wifi_app.h"
#include "cJSON.h"
#include "driver/gpio.h"

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h> 

#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#if 0
// MQTT client removed from this build (functions unused). Enable if needed.
#include "mqtt_client.h"
#endif
#include "esp_sntp.h"

#include "ntc_driver.h"
#include "pir_driver.h"
#include "esp_adc/adc_oneshot.h"

#include "fan_control.h"
#include "registers.h"
#include "sensor_task.h"



static esp_err_t http_server_read_register_handler(httpd_req_t *req);

static esp_err_t http_server_register_erase_handler(httpd_req_t *req);

// Tag used for ESP serial console messages
static const char TAG[] = "http_server";

adc_oneshot_unit_handle_t s_ntc_adc_handle = NULL;

// Wifi connect status
static int g_wifi_connect_status = NONE;

// Firmware update status
static int g_fw_update_status = OTA_UPDATE_PENDING;

// HTTP server task handle
static httpd_handle_t http_server_handle = NULL;

// HTTP server monitor task handle
static TaskHandle_t task_http_server_monitor = NULL;

// Queue handle used to manipulate the main queue of events
static QueueHandle_t http_server_monitor_queue_handle;

/**
 * ESP32 timer configuration passed to esp_timer_create.
 */
const esp_timer_create_args_t fw_update_reset_args = {
    .callback = &http_server_fw_update_reset_callback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "fw_update_reset"
};
esp_timer_handle_t fw_update_reset;

// Embedded files: JQuery, index.html, app.css, app.js and favicon.ico files
extern const uint8_t jquery_3_3_1_min_js_start[] asm("_binary_jquery_3_3_1_min_js_start");
extern const uint8_t jquery_3_3_1_min_js_end[]   asm("_binary_jquery_3_3_1_min_js_end");
extern const uint8_t index_html_start[]          asm("_binary_index_html_start");
extern const uint8_t index_html_end[]            asm("_binary_index_html_end");
extern const uint8_t app_css_start[]             asm("_binary_app_css_start");
extern const uint8_t app_css_end[]               asm("_binary_app_css_end");
extern const uint8_t app_js_start[]              asm("_binary_app_js_start");
extern const uint8_t app_js_end[]                asm("_binary_app_js_end");
extern const uint8_t favicon_ico_start[]         asm("_binary_favicon_ico_start");
extern const uint8_t favicon_ico_end[]           asm("_binary_favicon_ico_end");

uint8_t s_led_state = 0;

// -----------------------------------------------------
// LED TOGGLE
// -----------------------------------------------------
void toogle_led(void)
{
    s_led_state = !s_led_state;
    gpio_set_level(BLINK_GPIO, s_led_state);
}

// -----------------------------------------------------
// FAN CONTROL HANDLER (/fanControl.json)
// -----------------------------------------------------
static esp_err_t http_server_fan_control_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/fanControl.json requested");

    int len = req->content_len;
    if (len <= 0) {
        ESP_LOGI(TAG, "fanControl: content_len <= 0");
        return ESP_FAIL;
    }

    char *buf = malloc(len + 1);
    if (!buf) {
        ESP_LOGI(TAG, "fanControl: malloc failed");
        return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, buf, len);
    if (ret <= 0) {
        free(buf);
        ESP_LOGI(TAG, "fanControl: recv failed");
        return ESP_FAIL;
    }
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);

    if (!root) {
        ESP_LOGI(TAG, "fanControl: invalid JSON");
        return ESP_FAIL;
    }

    cJSON *mode_json  = cJSON_GetObjectItem(root, "mode");
    cJSON *speed_json = cJSON_GetObjectItem(root, "speed");

    if (!cJSON_IsString(mode_json) || !cJSON_IsNumber(speed_json)) {
        cJSON_Delete(root);
        ESP_LOGI(TAG, "fanControl: missing fields");
        return ESP_FAIL;
    }

    const char *mode_str = mode_json->valuestring;
    int speed            = speed_json->valueint;

    fan_mode_t mode = FAN_MODE_MANUAL;
    if (strcmp(mode_str, "auto") == 0) {
        mode = FAN_MODE_AUTO;
    } else if (strcmp(mode_str, "registros") == 0) {
        mode = FAN_MODE_REGISTERS;
    }

    fan_control_set_mode(mode);
    fan_control_set_manual_speed((uint8_t)speed);

    cJSON_Delete(root);

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"mode\":\"%s\",\"speed\":%d}",
             mode_str, speed);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));

    return ESP_OK;
}

// -----------------------------------------------------
// TIME (/time.json)
// -----------------------------------------------------
static esp_err_t http_server_time_json_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/time.json requested");

    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    char date_str[16];
    char time_str[16];
    char timeJSON[128];

    strftime(date_str, sizeof(date_str), "%Y-%m-%d", &timeinfo);
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);

    snprintf(timeJSON, sizeof(timeJSON),
             "{\"date\":\"%s\",\"time\":\"%s\"}",
             date_str, time_str);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, timeJSON, strlen(timeJSON));

    return ESP_OK;
}

// -----------------------------------------------------
// TOGGLE LED (/toogle_led.json)
// -----------------------------------------------------
static esp_err_t http_server_toogle_led_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/toogle_led.json requested");

    toogle_led();

    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, NULL, 0);
    
    return ESP_OK;
}

// -----------------------------------------------------
// LOG ERROR
// -----------------------------------------------------
static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

// MQTT support disabled in this build; re-enable if needed.

// -----------------------------------------------------
// SENSORES: NTC + PIR
// -----------------------------------------------------
static void sensors_init(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &s_ntc_adc_handle));
    inicializar_ntc(s_ntc_adc_handle);
    
    // Inicializar sensor_task para lectura de sensores
    esp_err_t err = sensor_task_init(s_ntc_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize sensor_task: %d", err);
    }

    // TODO: Cambia el GPIO por el que uses para el PIR (actualmente deshabilitado por conflicto con I2C SDA GPIO4)
    // pir_init(GPIO_NUM_XX, NULL);  // Usa un GPIO diferente disponible
}

// -----------------------------------------------------
// FW UPDATE TIMER
// -----------------------------------------------------
static void http_server_fw_update_reset_timer(void)
{
    if (g_fw_update_status == OTA_UPDATE_SUCCESSFUL)
    {
        ESP_LOGI(TAG, "http_server_fw_update_reset_timer: FW updated successful, starting reset timer");
        ESP_ERROR_CHECK(esp_timer_create(&fw_update_reset_args, &fw_update_reset));
        ESP_ERROR_CHECK(esp_timer_start_once(fw_update_reset, 8000000));
    }
    else
    {
        ESP_LOGI(TAG, "http_server_fw_update_reset_timer: FW update unsuccessful");
    }
}

// -----------------------------------------------------
// MONITOR TASK
// -----------------------------------------------------
static void http_server_monitor(void *parameter)
{
    http_server_queue_message_t msg;

    for (;;)
    {
        if (xQueueReceive(http_server_monitor_queue_handle, &msg, portMAX_DELAY))
        {
            switch (msg.msgID)
            {
                case HTTP_MSG_WIFI_CONNECT_INIT:
                    ESP_LOGI(TAG, "HTTP_MSG_WIFI_CONNECT_INIT");
                    g_wifi_connect_status = HTTP_WIFI_STATUS_CONNECTING;
                    break;

                case HTTP_MSG_WIFI_CONNECT_SUCCESS:
                    ESP_LOGI(TAG, "HTTP_MSG_WIFI_CONNECT_SUCCESS");
                    g_wifi_connect_status = HTTP_WIFI_STATUS_CONNECT_SUCCESS;
                    // mqtt_app_start();
                    break;

                case HTTP_MSG_WIFI_CONNECT_FAIL:
                    ESP_LOGI(TAG, "HTTP_MSG_WIFI_CONNECT_FAIL");
                    g_wifi_connect_status = HTTP_WIFI_STATUS_CONNECT_FAILED;
                    break;

                case HTTP_MSG_OTA_UPDATE_SUCCESSFUL:
                    ESP_LOGI(TAG, "HTTP_MSG_OTA_UPDATE_SUCCESSFUL");
                    g_fw_update_status = OTA_UPDATE_SUCCESSFUL;
                    http_server_fw_update_reset_timer();
                    break;

                case HTTP_MSG_OTA_UPDATE_FAILED:
                    ESP_LOGI(TAG, "HTTP_MSG_OTA_UPDATE_FAILED");
                    g_fw_update_status = OTA_UPDATE_FAILED;
                    break;

                default:
                    break;
            }
        }
    }
}

// -----------------------------------------------------
// STATIC FILE HANDLERS
// -----------------------------------------------------
static esp_err_t http_server_jquery_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "jquery requested");

    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req,
                    (const char *)jquery_3_3_1_min_js_start,
                    jquery_3_3_1_min_js_end - jquery_3_3_1_min_js_start);
    return ESP_OK;
}

static esp_err_t http_server_index_html_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "index.html requested");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req,
                    (const char *)index_html_start,
                    index_html_end - index_html_start);
    return ESP_OK;
}

static esp_err_t http_server_app_css_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "app.css requested");

    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req,
                    (const char *)app_css_start,
                    app_css_end - app_css_start);
    return ESP_OK;
}

static esp_err_t http_server_app_js_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "app.js requested");

    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req,
                    (const char *)app_js_start,
                    app_js_end - app_js_start);
    return ESP_OK;
}

static esp_err_t http_server_favicon_ico_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "favicon.ico requested");

    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req,
                    (const char *)favicon_ico_start,
                    favicon_ico_end - favicon_ico_start);
    return ESP_OK;
}

// -----------------------------------------------------
// OTA HANDLERS
// -----------------------------------------------------
esp_err_t http_server_OTA_update_handler(httpd_req_t *req)
{
    esp_ota_handle_t ota_handle;
    char ota_buff[1024];
    int content_length   = req->content_len;
    int content_received = 0;
    int recv_len;
    bool is_req_body_started = false;
    bool flash_successful    = false;

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

    do
    {
        if ((recv_len = httpd_req_recv(req, ota_buff, MIN(content_length, sizeof(ota_buff)))) < 0)
        {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT)
            {
                ESP_LOGI(TAG, "http_server_OTA_update_handler: Socket Timeout");
                continue;
            }
            ESP_LOGI(TAG, "http_server_OTA_update_handler: OTA other Error %d", recv_len);
            return ESP_FAIL;
        }
        printf("http_server_OTA_update_handler: OTA RX: %d of %d\r",
               content_received, content_length);

        if (!is_req_body_started)
        {
            is_req_body_started = true;

            char *body_start_p = strstr(ota_buff, "\r\n\r\n") + 4;
            int body_part_len  = recv_len - (body_start_p - ota_buff);

            printf("http_server_OTA_update_handler: OTA file size: %d\r\n", content_length);

            esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
            if (err != ESP_OK)
            {
                printf("http_server_OTA_update_handler: Error with OTA begin, cancelling\r\n");
                return ESP_FAIL;
            }
            else
            {
                printf("http_server_OTA_update_handler: Writing to partition subtype %d at 0x%lx\r\n",
                       update_partition->subtype, update_partition->address);
            }

            esp_ota_write(ota_handle, body_start_p, body_part_len);
            content_received += body_part_len;
        }
        else
        {
            esp_ota_write(ota_handle, ota_buff, recv_len);
            content_received += recv_len;
        }

    } while (recv_len > 0 && content_received < content_length);

    if (esp_ota_end(ota_handle) == ESP_OK)
    {
        if (esp_ota_set_boot_partition(update_partition) == ESP_OK)
        {
            const esp_partition_t *boot_partition = esp_ota_get_boot_partition();
            ESP_LOGI(TAG,
                     "http_server_OTA_update_handler: Next boot partition subtype %d at 0x%lx",
                     boot_partition->subtype, boot_partition->address);
            flash_successful = true;
        }
        else
        {
            ESP_LOGI(TAG, "http_server_OTA_update_handler: FLASHED ERROR!!!");
        }
    }
    else
    {
        ESP_LOGI(TAG, "http_server_OTA_update_handler: esp_ota_end ERROR!!!");
    }

    if (flash_successful) {
        http_server_monitor_send_message(HTTP_MSG_OTA_UPDATE_SUCCESSFUL);
    } else {
        http_server_monitor_send_message(HTTP_MSG_OTA_UPDATE_FAILED);
    }

    return ESP_OK;
}

esp_err_t http_server_OTA_status_handler(httpd_req_t *req)
{
    char otaJSON[100];

    ESP_LOGI(TAG, "OTAstatus requested");

    sprintf(otaJSON,
            "{\"ota_update_status\":%d,\"compile_time\":\"%s\",\"compile_date\":\"%s\"}",
            g_fw_update_status, __TIME__, __DATE__);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, otaJSON, strlen(otaJSON));

    return ESP_OK;
}

// -----------------------------------------------------
// WiFi CONNECT (/wifiConnect.json)
// -----------------------------------------------------
static esp_err_t http_server_wifi_connect_json_handler(httpd_req_t *req)
{
    size_t header_len;
    char *header_value;
    char *ssid_str = NULL;
    char *pass_str = NULL;
    int content_length;

    ESP_LOGI(TAG, "/wifiConnect.json requested");

    header_len = httpd_req_get_hdr_value_len(req, "Content-Length");
    if (header_len <= 0) {
        ESP_LOGI(TAG, "Content-Length header is missing or invalid");
        return ESP_FAIL;
    }

    header_value = (char *)malloc(header_len + 1);
    if (httpd_req_get_hdr_value_str(req, "Content-Length", header_value, header_len + 1) != ESP_OK) {
        free(header_value);
        ESP_LOGI(TAG, "Failed to get Content-Length header value");
        return ESP_FAIL;
    }

    content_length = atoi(header_value);
    free(header_value);

    if (content_length <= 0) {
        ESP_LOGI(TAG, "Invalid Content-Length value");
        return ESP_FAIL;
    }

    char *data_buffer = (char *)malloc(content_length + 1);
    if (!data_buffer) {
        ESP_LOGI(TAG, "malloc failed");
        return ESP_FAIL;
    }

    if (httpd_req_recv(req, data_buffer, content_length) <= 0) {
        free(data_buffer);
        ESP_LOGI(TAG, "Failed to receive request body");
        return ESP_FAIL;
    }

    data_buffer[content_length] = '\0';

    cJSON *root = cJSON_Parse(data_buffer);
    free(data_buffer);

    if (!root) {
        ESP_LOGI(TAG, "Invalid JSON data");
        return ESP_FAIL;
    }

    cJSON *ssid_json = cJSON_GetObjectItem(root, "selectedSSID");
    cJSON *pwd_json  = cJSON_GetObjectItem(root, "pwd");

    if (!ssid_json || !pwd_json || !cJSON_IsString(ssid_json) || !cJSON_IsString(pwd_json)) {
        cJSON_Delete(root);
        ESP_LOGI(TAG, "Missing or invalid JSON data fields");
        return ESP_FAIL;
    }

    ssid_str = strdup(ssid_json->valuestring);
    pass_str = strdup(pwd_json->valuestring);

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Received SSID: %s", ssid_str);
    ESP_LOGI(TAG, "Received Password: %s", pass_str);

    wifi_config_t *wifi_config = wifi_app_get_wifi_config();
    memset(wifi_config, 0x00, sizeof(wifi_config_t));
    memcpy(wifi_config->sta.ssid,     ssid_str, strlen(ssid_str));
    memcpy(wifi_config->sta.password, pass_str, strlen(pass_str));

    save_wifi_credentials(ssid_str, pass_str);
    esp_wifi_disconnect();
    connect_to_wifi();

    free(ssid_str);
    free(pass_str);

    return ESP_OK;
}

// -----------------------------------------------------
// WiFi STATUS (/wifiConnectStatus)
// -----------------------------------------------------
static esp_err_t http_server_wifi_connect_status_json_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/wifiConnectStatus requested");

    char statusJSON[100];
    sprintf(statusJSON, "{\"wifi_connect_status\":%d}", g_wifi_connect_status);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, statusJSON, strlen(statusJSON));

    return ESP_OK;
}

// -----------------------------------------------------
// DHT/NTC + PIR (/dhtSensor.json)
// -----------------------------------------------------
static esp_err_t http_server_get_dht_sensor_readings_json_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/dhtSensor.json requested (NTC + PIR)");

    float temp_c = leer_temperatura_celsius(s_ntc_adc_handle);
    bool motion  = pir_is_motion_active();

    char json[128];

    if (temp_c < -100.0f) {
        snprintf(json, sizeof(json),
                 "{\"temp\":null,\"pir\":%d}",
                 motion ? 1 : 0);
    } else {
        snprintf(json, sizeof(json),
                 "{\"temp\":%.2f,\"pir\":%d}",
                 temp_c,
                 motion ? 1 : 0);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));

    return ESP_OK;
}


// -----------------------------------------------------
// /regerase.json  (compatibilidad botón "Borrar registro")
// Body JSON: { "selectedNumber": "3" }
// -----------------------------------------------------
static esp_err_t http_server_register_erase_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/regerase.json requested");

    int len = req->content_len;
    if (len <= 0 || len > 128) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid length");
        return ESP_FAIL;
    }

    char *buf = malloc(len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
        return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, buf, len);
    if (ret <= 0) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_FAIL;
    }

    cJSON *reg_number_json = cJSON_GetObjectItem(root, "selectedNumber");
    if (!cJSON_IsString(reg_number_json)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing selectedNumber");
        return ESP_FAIL;
    }

    int id = atoi(reg_number_json->valuestring);
    cJSON_Delete(root);

    if (id <= 0 || id > 10) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid id");
        return ESP_FAIL;
    }

    delete_register_from_nvs(id);
    ESP_LOGI(TAG, "register %d deleted from NVS", id);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", -1);

    return ESP_OK;
}


// -----------------------------------------------------
// HTTP SERVER CONFIG
// -----------------------------------------------------
static httpd_handle_t http_server_configure(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    http_server_monitor_queue_handle =
        xQueueCreate(3, sizeof(http_server_queue_message_t));

    xTaskCreatePinnedToCore(&http_server_monitor,
                            "http_server_monitor",
                            HTTP_SERVER_MONITOR_STACK_SIZE,
                            NULL,
                            HTTP_SERVER_MONITOR_PRIORITY,
                            &task_http_server_monitor,
                            HTTP_SERVER_MONITOR_CORE_ID);

    config.core_id           = HTTP_SERVER_TASK_CORE_ID;
    config.task_priority     = HTTP_SERVER_TASK_PRIORITY;
    config.stack_size        = HTTP_SERVER_TASK_STACK_SIZE;
    config.max_uri_handlers  = 20;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;

    ESP_LOGI(TAG,
             "http_server_configure: Starting server on port: '%d' with priority: '%d'",
             config.server_port,
             config.task_priority);

    if (httpd_start(&http_server_handle, &config) == ESP_OK)
    {
        ESP_LOGI(TAG, "http_server_configure: Registering URI handlers");

        httpd_uri_t fan_control_uri = {
            .uri      = "/fanControl.json",
            .method   = HTTP_POST,
            .handler  = http_server_fan_control_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &fan_control_uri);

        httpd_uri_t jquery_js = {
            .uri      = "/jquery-3.3.1.min.js",
            .method   = HTTP_GET,
            .handler  = http_server_jquery_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &jquery_js);

        httpd_uri_t index_html = {
            .uri      = "/",
            .method   = HTTP_GET,
            .handler  = http_server_index_html_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &index_html);

        httpd_uri_t app_css = {
            .uri      = "/app.css",
            .method   = HTTP_GET,
            .handler  = http_server_app_css_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &app_css);

        httpd_uri_t app_js = {
            .uri      = "/app.js",
            .method   = HTTP_GET,
            .handler  = http_server_app_js_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &app_js);

        httpd_uri_t favicon_ico = {
            .uri      = "/favicon.ico",
            .method   = HTTP_GET,
            .handler  = http_server_favicon_ico_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &favicon_ico);

        httpd_uri_t toogle_led_uri = {
            .uri      = "/toogle_led.json",
            .method   = HTTP_POST,
            .handler  = http_server_toogle_led_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &toogle_led_uri);

        httpd_uri_t wifi_connect_json = {
            .uri      = "/wifiConnect.json",
            .method   = HTTP_POST,
            .handler  = http_server_wifi_connect_json_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &wifi_connect_json);

        httpd_uri_t readreg_uri = {
            .uri      = "/readreg.json",
            .method   = HTTP_GET,
            .handler  = http_server_read_register_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &readreg_uri);
        ESP_LOGI(TAG, "Registered /readreg.json handler");

       
        // Borrar registro (compatibilidad)
        httpd_uri_t register_erase_uri = {
            .uri      = "/regerase.json",
            .method   = HTTP_POST,
            .handler  = http_server_register_erase_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &register_erase_uri);

        httpd_uri_t OTA_update = {
            .uri      = "/OTAupdate",
            .method   = HTTP_POST,
            .handler  = http_server_OTA_update_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &OTA_update);

        httpd_uri_t OTA_status = {
            .uri      = "/OTAstatus",
            .method   = HTTP_POST,
            .handler  = http_server_OTA_status_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &OTA_status);

        httpd_uri_t dht_sensor_json = {
            .uri      = "/dhtSensor.json",
            .method   = HTTP_GET,
            .handler  = http_server_get_dht_sensor_readings_json_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &dht_sensor_json);

        httpd_uri_t wifi_connect_status_json = {
            .uri      = "/wifiConnectStatus",
            .method   = HTTP_POST,
            .handler  = http_server_wifi_connect_status_json_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &wifi_connect_status_json);

        // Endpoints nuevos de registros (en registers.c):
        register_registers_endpoints(http_server_handle);

        httpd_uri_t time_json = {
            .uri      = "/time.json",
            .method   = HTTP_GET,
            .handler  = http_server_time_json_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &time_json);


        

        return http_server_handle;
    }

    return NULL;
}

// -----------------------------------------------------
// /readreg.json  (compatibilidad con front-end viejo)
// Devuelve reg1..reg10 como strings "HHMM" + días ("L","M","X"...)
// -----------------------------------------------------
static esp_err_t http_server_read_register_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/readreg.json requested -> leyendo desde NVS");

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
        return ESP_FAIL;
    }

    for (int i = 1; i <= 10; i++) {
        char key[16];
        snprintf(key, sizeof(key), "reg%d", i);

        char buf[256];
        esp_err_t err = load_register_from_nvs(i, buf, sizeof(buf));
        if (err != ESP_OK) {
            // No hay registro: manda null
            cJSON_AddNullToObject(root, key);
            continue;
        }

        cJSON *reg_json = cJSON_Parse(buf);
        if (!reg_json) {
            cJSON_AddNullToObject(root, key);
            continue;
        }

        cJSON *hour_json   = cJSON_GetObjectItem(reg_json, "hour");
        cJSON *minute_json = cJSON_GetObjectItem(reg_json, "minute");
        cJSON *days_json   = cJSON_GetObjectItem(reg_json, "days");

        if (!cJSON_IsNumber(hour_json) ||
            !cJSON_IsNumber(minute_json) ||
            !cJSON_IsArray(days_json)) {
            cJSON_Delete(reg_json);
            cJSON_AddNullToObject(root, key);
            continue;
        }

        char reg_str[32];
        // "HHMM"
        snprintf(reg_str, sizeof(reg_str), "%02d%02d",
                 hour_json->valueint, minute_json->valueint);

        // Añade letras de días tal como las guardamos en NVS: "L","M","X","J","V","S","D"
        int ndays = cJSON_GetArraySize(days_json);
        for (int j = 0; j < ndays && strlen(reg_str) < sizeof(reg_str) - 1; j++) {
            cJSON *d = cJSON_GetArrayItem(days_json, j);
            if (cJSON_IsString(d) && d->valuestring && d->valuestring[0] != '\0') {
                strncat(reg_str, d->valuestring, 1);   // solo la primera letra
            }
        }

        cJSON_AddStringToObject(root, key, reg_str);
        cJSON_Delete(reg_json);
    }

    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, strlen(out));

    free(out);
    cJSON_Delete(root);

    return ESP_OK;
}

// -----------------------------------------------------
// FAN TASK - Actualización automática del ventilador
// -----------------------------------------------------
static void fan_task(void *arg)
{
    while (1) {
        fan_control_update();
        vTaskDelay(pdMS_TO_TICKS(1000));  // actualiza cada 1s
    }
}

// -----------------------------------------------------
// PUBLIC START/STOP
// -----------------------------------------------------
void http_server_start(void)
{
    if (http_server_handle == NULL)
    {
        sensors_init();
        http_server_handle = http_server_configure();
        
        // Inicializar control del ventilador
        fan_control_init();
        ESP_LOGI(TAG, "Fan control initialized");
        
        // Crear tarea para actualización automática del ventilador
        xTaskCreate(fan_task, "fan_task", 3072, NULL, 5, NULL);
        ESP_LOGI(TAG, "Fan task created");
    }
}

void http_server_stop(void)
{
    if (http_server_handle)
    {
        httpd_stop(http_server_handle);
        ESP_LOGI(TAG, "http_server_stop: stopping HTTP server");
        http_server_handle = NULL;
    }
    if (task_http_server_monitor)
    {
        vTaskDelete(task_http_server_monitor);
        ESP_LOGI(TAG, "http_server_stop: stopping HTTP server monitor");
        task_http_server_monitor = NULL;
    }
}

// -----------------------------------------------------
// QUEUE MESSAGE
// -----------------------------------------------------
BaseType_t http_server_monitor_send_message(http_server_message_e msgID)
{
    http_server_queue_message_t msg;
    msg.msgID = msgID;
    return xQueueSend(http_server_monitor_queue_handle, &msg, portMAX_DELAY);
}

void http_server_fw_update_reset_callback(void *arg)
{
    ESP_LOGI(TAG, "http_server_fw_update_reset_callback: Timer timed-out, restarting device");
    esp_restart();
}
