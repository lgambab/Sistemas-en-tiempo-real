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

#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_sntp.h"

#include "ntc_driver.h"
#include "pir_driver.h"
#include "esp_adc/adc_oneshot.h"

#include "fan_control.h"
#include "registers.h"
#include "sensor_task.h"


// Tag used for ESP serial console messages
static const char TAG[] = "http_server";
static adc_oneshot_unit_handle_t s_ntc_adc_handle = NULL;

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
extern const uint8_t jquery_3_3_1_min_js_start[]	asm("_binary_jquery_3_3_1_min_js_start");
extern const uint8_t jquery_3_3_1_min_js_end[]		asm("_binary_jquery_3_3_1_min_js_end");
extern const uint8_t index_html_start[]				asm("_binary_index_html_start");
extern const uint8_t index_html_end[]				asm("_binary_index_html_end");
extern const uint8_t app_css_start[]				asm("_binary_app_css_start");
extern const uint8_t app_css_end[]					asm("_binary_app_css_end");
extern const uint8_t app_js_start[]					asm("_binary_app_js_start");
extern const uint8_t app_js_end[]					asm("_binary_app_js_end");
extern const uint8_t favicon_ico_start[]			asm("_binary_favicon_ico_start");
extern const uint8_t favicon_ico_end[]				asm("_binary_favicon_ico_end");

uint8_t s_led_state = 0;



void toogle_led( void )
{
	

	s_led_state = !s_led_state;
	gpio_set_level(BLINK_GPIO, s_led_state);

}

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

    // Aplica configuración
    fan_control_set_mode(mode);
    fan_control_set_manual_speed((uint8_t)speed);

    cJSON_Delete(root);

    // Respuesta JSON
    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"mode\":\"%s\",\"speed\":%d}",
             mode_str, speed);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));

    return ESP_OK;
}


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

    // Formato: 2025-11-30
    strftime(date_str, sizeof(date_str), "%Y-%m-%d", &timeinfo);
    // Formato: 14:05:33
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);

    snprintf(timeJSON, sizeof(timeJSON),
             "{\"date\":\"%s\",\"time\":\"%s\"}",
             date_str, time_str);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, timeJSON, strlen(timeJSON));

    return ESP_OK;
}

static esp_err_t http_server_toogle_led_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "/toogle_led.json requested");

	toogle_led();

	// Cerrar la conexion
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, NULL, 0);
    
	return ESP_OK;
}

static esp_err_t http_server_read_register_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/readreg.json requested");

    // Crear el objeto JSON raíz
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON root");
        return ESP_FAIL;
    }

    char reg_buf[32];   // buffer temporal para cada registro

    // Leer registros del 1 al 10 (igual que antes)
    for (int i = 1; i <= 10; i++)
    {
        memset(reg_buf, 0, sizeof(reg_buf));

        esp_err_t r = read_reg_data(reg_buf, i);

        char key[16];
        snprintf(key, sizeof(key), "reg%d", i);

        if (r == ESP_OK) {
            // agregar "regX": "valor"
            cJSON_AddStringToObject(root, key, reg_buf);
        } else {
            // agregar "regX": null
            cJSON_AddNullToObject(root, key);
        }
    }

    // Serializar el JSON
    char *out = cJSON_PrintUnformatted(root);
    if (!out) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Enviar respuesta
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, strlen(out));

    // Limpiar
    free(out);
    cJSON_Delete(root);

    return ESP_OK;
}


// static esp_err_t http_server_read_register_handler(httpd_req_t *req)
// {
// 	ESP_LOGI(TAG, "/readreg.json requested");

// 	char read_regs[255];
// 	char register_information_read_1[12];
// 	register_information_read_1[11] = 0x00;
// 	if (read_reg_data( &register_information_read_1[0], 1 ) != ESP_OK ){
// 		memset(&register_information_read_1[0], '9', 6);
// 	}

// 	char register_information_read_2[12];
// 	register_information_read_2[11] = 0x00;
// 	if (read_reg_data( &register_information_read_2[0], 2 ) != ESP_OK ){
// 		memset(&register_information_read_2[0], '9', 6);
// 	}


// 	char register_information_read_3[12];
// 	register_information_read_3[11] = 0x00;
// 	if (read_reg_data( &register_information_read_3[0], 3 ) != ESP_OK ){
// 		memset(&register_information_read_3[0], '9', 6);
// 	}


// 	char register_information_read_4[12];
// 	register_information_read_4[11] = 0x00;
// 	if (read_reg_data( &register_information_read_4[0], 4 ) != ESP_OK ){
// 		memset(&register_information_read_4[0], '9', 6);
// 	}

// 	char register_information_read_5[12];
// 	register_information_read_5[11] = 0x00;
// 	if (read_reg_data( &register_information_read_5[0], 5 ) != ESP_OK ){
// 		memset(&register_information_read_5[0], '9', 6);
// 	}


// 	char register_information_read_6[12];
// 	register_information_read_6[11] = 0x00;
// 	if (read_reg_data( &register_information_read_6[0], 6 ) != ESP_OK ){
// 		memset(&register_information_read_6[0], '9', 6);
// 	}

// 	char register_information_read_7[12];
// 	register_information_read_7[11] = 0x00;
// 	if (read_reg_data( &register_information_read_7[0], 7 ) != ESP_OK ){
// 		memset(&register_information_read_7[0], '9', 6);
// 	}


// 	char register_information_read_8[12];
// 	register_information_read_8[11] = 0x00;
// 	if (read_reg_data( &register_information_read_8[0], 8 ) != ESP_OK ){
// 		memset(&register_information_read_8[0], '9', 6);
// 	}


// 	char register_information_read_9[12];
// 	register_information_read_9[11] = 0x00;
// 	if (read_reg_data( &register_information_read_9[0], 9 ) != ESP_OK ){
// 		memset(&register_information_read_9[0], '9', 6);
// 	}

// 	char register_information_read_10[12];
// 	register_information_read_10[11] = 0x00;
// 	if (read_reg_data( &register_information_read_10[0], 10 ) != ESP_OK ){
// 		memset(&register_information_read_10[0], '9', 6);
// 	}




// 	// Build JSON response for registers
// 	{
// 		cJSON *root = cJSON_CreateObject();
// 		char reg_buf[32];
// 		for (int i = 1; i <= 10; i++) {
// 			memset(reg_buf, 0, sizeof(reg_buf));
// 			esp_err_t r = read_reg_data(reg_buf, i);
// 			char key[16];
// 			snprintf(key, sizeof(key), "reg%d", i);
// 			if (r == ESP_OK) {
// 				cJSON_AddStringToObject(root, key, reg_buf);
// 			} else {
// 				cJSON_AddNullToObject(root, key);
// 			}
// 		}

// 		char *out = cJSON_PrintUnformatted(root);
// 		httpd_resp_set_type(req, "application/json");
// 		httpd_resp_send(req, out, strlen(out));

// 		free(out);
// 		cJSON_Delete(root);
// 	}

// 	return ESP_OK;

// }


static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
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
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    //ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        //msg_id = esp_mqtt_client_publish(client, "test", "hola", 0, 1, 0);
        //ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "test", 1);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
		
        //msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        //ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        //msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        //ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        //msg_id = esp_mqtt_client_publish(client, "test", "hola", 0, 1, 0);
        //ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
		if (strncmp(event->data, "toggle", event->data_len) == 0){
			printf("toogle LED received");
			toogle_led();
			
		}

		
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://victor:banano@ec2-35-93-50-123.us-west-2.compute.amazonaws.com",
		.session.keepalive = 15,
		.network.reconnect_timeout_ms = 50,
		//.network.refresh_connection_after_ms,
		.task.priority = 5,
		.task.stack_size = 4096,
	};


	

 
#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.broker.address.uri, "FROM_STDIN") == 0) {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128) {
            int c = fgetc(stdin);
            if (c == '\n') {
                line[count] = '\0';
                break;
            } else if (c > 0 && c < 127) {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.broker.address.uri = line;
        printf("Broker url: %s\n", line);
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

static void sensors_init(void)
{
    // ---------- ADC para NTC ----------
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &s_ntc_adc_handle));
    inicializar_ntc(s_ntc_adc_handle);   // tu función del ntc_driver.c

    // ---------- PIR ----------
    // CAMBIA este GPIO por el que realmente tengas conectado el PIR
    pir_init(GPIO_NUM_4, NULL);   // Si no quieres usar cola, NULL está bien
    
    // ---------- Tarea de sensores (RTOS-compliant) ----------
    esp_err_t ret = sensor_task_init(s_ntc_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize sensor task");
    } else {
        ESP_LOGI(TAG, "Sensor task initialized successfully");
    }
}


/**
 * Checks the g_fw_update_status and creates the fw_update_reset timer if g_fw_update_status is true.
 */
static void http_server_fw_update_reset_timer(void)
{
	if (g_fw_update_status == OTA_UPDATE_SUCCESSFUL)
	{
		ESP_LOGI(TAG, "http_server_fw_update_reset_timer: FW updated successful starting FW update reset timer");

		// Give the web page a chance to receive an acknowledge back and initialize the timer
		ESP_ERROR_CHECK(esp_timer_create(&fw_update_reset_args, &fw_update_reset));
		ESP_ERROR_CHECK(esp_timer_start_once(fw_update_reset, 8000000));
	}
	else
	{
		ESP_LOGI(TAG, "http_server_fw_update_reset_timer: FW update unsuccessful");
	}
}

/**
 * HTTP server monitor task used to track events of the HTTP server
 * @param pvParameters parameter which can be passed to the task.
 */
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
					//mqtt_app_start(); dissable MQTT
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

/**
 * Jquery get handler is requested when accessing the web page.
 * @param req HTTP request for which the uri needs to be handled.
 * @return ESP_OK
 */
static esp_err_t http_server_jquery_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "Jquery requested");

	httpd_resp_set_type(req, "application/javascript");
	httpd_resp_send(req, (const char *)jquery_3_3_1_min_js_start, jquery_3_3_1_min_js_end - jquery_3_3_1_min_js_start);

	return ESP_OK;
}

/**
 * Sends the index.html page.
 * @param req HTTP request for which the uri needs to be handled.
 * @return ESP_OK
 */
static esp_err_t http_server_index_html_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "index.html requested");

	httpd_resp_set_type(req, "text/html");
	httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);

	return ESP_OK;
}

/**
 * app.css get handler is requested when accessing the web page.
 * @param req HTTP request for which the uri needs to be handled.
 * @return ESP_OK
 */
static esp_err_t http_server_app_css_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "app.css requested");

	httpd_resp_set_type(req, "text/css");
	httpd_resp_send(req, (const char *)app_css_start, app_css_end - app_css_start);

	return ESP_OK;
}

/**
 * app.js get handler is requested when accessing the web page.
 * @param req HTTP request for which the uri needs to be handled.
 * @return ESP_OK
 */
static esp_err_t http_server_app_js_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "app.js requested");

	httpd_resp_set_type(req, "application/javascript");
	httpd_resp_send(req, (const char *)app_js_start, app_js_end - app_js_start);

	return ESP_OK;
}

/**
 * Sends the .ico (icon) file when accessing the web page.
 * @param req HTTP request for which the uri needs to be handled.
 * @return ESP_OK
 */
static esp_err_t http_server_favicon_ico_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "favicon.ico requested");

	httpd_resp_set_type(req, "image/x-icon");
	httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_end - favicon_ico_start);

	return ESP_OK;
}

/**
 * Receives the .bin file fia the web page and handles the firmware update
 * @param req HTTP request for which the uri needs to be handled.
 * @return ESP_OK, otherwise ESP_FAIL if timeout occurs and the update cannot be started.
 */
esp_err_t http_server_OTA_update_handler(httpd_req_t *req)
{
	esp_ota_handle_t ota_handle;

	char ota_buff[1024];
	int content_length = req->content_len;
	int content_received = 0;
	int recv_len;
	bool is_req_body_started = false;
	bool flash_successful = false;

	const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

	do
	{
		// Read the data for the request
		if ((recv_len = httpd_req_recv(req, ota_buff, MIN(content_length, sizeof(ota_buff)))) < 0)
		{
			// Check if timeout occurred
			if (recv_len == HTTPD_SOCK_ERR_TIMEOUT)
			{
				ESP_LOGI(TAG, "http_server_OTA_update_handler: Socket Timeout");
				continue; ///> Retry receiving if timeout occurred
			}
			ESP_LOGI(TAG, "http_server_OTA_update_handler: OTA other Error %d", recv_len);
			return ESP_FAIL;
		}
		printf("http_server_OTA_update_handler: OTA RX: %d of %d\r", content_received, content_length);

		// Is this the first data we are receiving
		// If so, it will have the information in the header that we need.
		if (!is_req_body_started)
		{
			is_req_body_started = true;

			// Get the location of the .bin file content (remove the web form data)
			char *body_start_p = strstr(ota_buff, "\r\n\r\n") + 4;
			int body_part_len = recv_len - (body_start_p - ota_buff);

			printf("http_server_OTA_update_handler: OTA file size: %d\r\n", content_length);

			esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
			if (err != ESP_OK)
			{
				printf("http_server_OTA_update_handler: Error with OTA begin, cancelling OTA\r\n");
				return ESP_FAIL;
			}
			else
			{
				printf("http_server_OTA_update_handler: Writing to partition subtype %d at offset 0x%lx\r\n", update_partition->subtype, update_partition->address);
			}

			// Write this first part of the data
			esp_ota_write(ota_handle, body_start_p, body_part_len);
			content_received += body_part_len;
		}
		else
		{
			// Write OTA data
			esp_ota_write(ota_handle, ota_buff, recv_len);
			content_received += recv_len;
		}

	} while (recv_len > 0 && content_received < content_length);

	if (esp_ota_end(ota_handle) == ESP_OK)
	{
		// Lets update the partition
		if (esp_ota_set_boot_partition(update_partition) == ESP_OK)
		{
			const esp_partition_t *boot_partition = esp_ota_get_boot_partition();
			ESP_LOGI(TAG, "http_server_OTA_update_handler: Next boot partition subtype %d at offset 0x%lx", boot_partition->subtype, boot_partition->address);
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

	// We won't update the global variables throughout the file, so send the message about the status
	if (flash_successful) { http_server_monitor_send_message(HTTP_MSG_OTA_UPDATE_SUCCESSFUL); } else { http_server_monitor_send_message(HTTP_MSG_OTA_UPDATE_FAILED); }

	return ESP_OK;
}

/**
 * OTA status handler responds with the firmware update status after the OTA update is started
 * and responds with the compile time/date when the page is first requested
 * @param req HTTP request for which the uri needs to be handled
 * @return ESP_OK
 */
esp_err_t http_server_OTA_status_handler(httpd_req_t *req)
{
	char otaJSON[100];

	ESP_LOGI(TAG, "OTAstatus requested");

	sprintf(otaJSON, "{\"ota_update_status\":%d,\"compile_time\":\"%s\",\"compile_date\":\"%s\"}", g_fw_update_status, __TIME__, __DATE__);

	httpd_resp_set_type(req, "application/json");
	httpd_resp_send(req, otaJSON, strlen(otaJSON));

	return ESP_OK;
}


/**
 * regchange.json handler is invoked after the "enviar registro" button is pressed
 * and handles receiving the data entered by the user
 * @param req HTTP request for which the uri needs to be handled.
 * @return ESP_OK
 */


static esp_err_t http_server_register_change_handler(httpd_req_t *req)
{
    size_t header_len;
    char* header_value;
    char* hour_str = NULL;
	char* reg_str = NULL;
    char* min_str = NULL;
	char* days = NULL;
    int content_length;

    ESP_LOGI(TAG, "/regchange.json requested");

    // Get the "Content-Length" header to determine the length of the request body
    header_len = httpd_req_get_hdr_value_len(req, "Content-Length");
    if (header_len <= 0) {
        // Content-Length header not found or invalid
        //httpd_resp_send_err(req, HTTP_STATUS_411_LENGTH_REQUIRED, "Content-Length header is missing or invalid");
        ESP_LOGI(TAG, "Content-Length header is missing or invalid");
        return ESP_FAIL;
    }

    // Allocate memory to store the header value
    header_value = (char*)malloc(header_len + 1);
    if (httpd_req_get_hdr_value_str(req, "Content-Length", header_value, header_len + 1) != ESP_OK) {
        // Failed to get Content-Length header value
        free(header_value);
        //httpd_resp_send_err(req, HTTP_STATUS_BAD_REQUEST, "Failed to get Content-Length header value");
        ESP_LOGI(TAG, "Failed to get Content-Length header value");
        return ESP_FAIL;
    }

    // Convert the Content-Length header value to an integer
    content_length = atoi(header_value);
    free(header_value);

    if (content_length <= 0) {
        // Content length is not a valid positive integer
        //httpd_resp_send_err(req, HTTP_STATUS_BAD_REQUEST, "Invalid Content-Length value");
        ESP_LOGI(TAG, "Invalid Content-Length value");
        return ESP_FAIL;
    }

    // Allocate memory for the data buffer based on the content length
    char* data_buffer = (char*)malloc(content_length + 1);

    // Read the request body into the data buffer
    if (httpd_req_recv(req, data_buffer, content_length) <= 0) {
        // Handle error while receiving data
        free(data_buffer);
        //httpd_resp_send_err(req, HTTP_STATUS_INTERNAL_SERVER_ERROR, "Failed to receive request body");
        ESP_LOGI(TAG, "Failed to receive request body");
        return ESP_FAIL;
    }

    // Null-terminate the data buffer to treat it as a string
    data_buffer[content_length] = '\0';

    // Parse the received JSON data
    cJSON* root = cJSON_Parse(data_buffer);
    free(data_buffer);

    if (root == NULL) {
        // JSON parsing error
        //httpd_resp_send_err(req, HTTP_STATUS_BAD_REQUEST, "Invalid JSON data");
        ESP_LOGI(TAG, "Invalid JSON data");
        return ESP_FAIL;
    }

	cJSON* reg_number_json = cJSON_GetObjectItem(root, "selectedNumber");
    cJSON* hour_json = cJSON_GetObjectItem(root, "hours");
    cJSON* min_json = cJSON_GetObjectItem(root, "minutes");
	cJSON* selectedDays_json = cJSON_GetObjectItem(root, "selectedDays");
	
    if (hour_json == NULL || min_json == NULL || selectedDays_json == NULL|| !cJSON_IsString(hour_json) || !cJSON_IsString(min_json) || !cJSON_IsArray(selectedDays_json)) {
        cJSON_Delete(root);
        // Missing or invalid JSON fields
        //httpd_resp_send_err(req, HTTP_STATUS_BAD_REQUEST, "Missing or invalid JSON data fields");
        ESP_LOGI(TAG, "Missing or invalid JSON data fields");
        return ESP_FAIL;
    }

    // Extract SSID and password from JSON
	reg_str = strdup(reg_number_json->valuestring);
    hour_str = strdup(hour_json->valuestring);
    min_str = strdup(min_json->valuestring);

    

	ESP_LOGI(TAG, "Received reg: %s", reg_str);
    ESP_LOGI(TAG, "Received hour: %s", hour_str);
    ESP_LOGI(TAG, "Received min: %s", min_str);
 
    
	char str_to_save[12];
	//str_to_save[11]=0x00;
	memset(str_to_save, 0x00, 12);
	
	if (cJSON_IsArray(selectedDays_json)) {
    cJSON* day_item;

    // Iterate over each element in the array
	
	
	//strcat(str_to_save, reg_str);
	strcat(str_to_save, hour_str);
	strcat(str_to_save, min_str);
    cJSON_ArrayForEach(day_item, selectedDays_json) {
        // Check if the array element is a string
        if (cJSON_IsString(day_item)) {
            const char* day_str = day_item->valuestring;
			strcat(str_to_save, day_str);
            // Perform actions with the day_str
            printf("Selected Day: %s\n", day_str);
        }
    }
	
	} else {
		printf("SelectedDays is not an array\n");
	}
	printf("%s\n", str_to_save);
	save_reg_data(atoi(reg_str), &str_to_save[0]);
	update_register(atoi(reg_str));
	// Process the selected days array
   // cJSON* day_item;
    //cJSON_ArrayForEach(day_item, selectedDays_json) {
       // if (cJSON_IsString(day_item)) {
         //   const char* day_str = day_item->valuestring;
           // ESP_LOGI(TAG, "Selected Day: %s", day_str);

            // Perform any additional actions based on the selected day
            // ...

            // Release memory when no longer needed
        //}
		
    //}
	//free(day_item);

	//free(selectedDays_json);

	// Send a success response to the client
	// Cerrar la conexion
	free(reg_str);
	free(hour_str);
    free(min_str);
	cJSON_Delete(root);
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, NULL, 0);
	

    return ESP_OK;
}




/**
 * erasereg.json handler is invoked after the "enviar registro" button is pressed
 * and handles receiving the data entered by the user
 * @param req HTTP request for which the uri needs to be handled.
 * @return ESP_OK
 */


static esp_err_t http_server_register_erase_handler(httpd_req_t *req)
{
    size_t header_len;
    char* header_value;
    char* hour_str = NULL;
	char* reg_str = NULL;
    char* min_str = NULL;
	char* days = NULL;
    int content_length;

    ESP_LOGI(TAG, "/regerase.json requested");

    // Get the "Content-Length" header to determine the length of the request body
    header_len = httpd_req_get_hdr_value_len(req, "Content-Length");
    if (header_len <= 0) {
        // Content-Length header not found or invalid
        //httpd_resp_send_err(req, HTTP_STATUS_411_LENGTH_REQUIRED, "Content-Length header is missing or invalid");
        ESP_LOGI(TAG, "Content-Length header is missing or invalid");
        return ESP_FAIL;
    }

    // Allocate memory to store the header value
    header_value = (char*)malloc(header_len + 1);
    if (httpd_req_get_hdr_value_str(req, "Content-Length", header_value, header_len + 1) != ESP_OK) {
        // Failed to get Content-Length header value
        free(header_value);
        //httpd_resp_send_err(req, HTTP_STATUS_BAD_REQUEST, "Failed to get Content-Length header value");
        ESP_LOGI(TAG, "Failed to get Content-Length header value");
        return ESP_FAIL;
    }

    // Convert the Content-Length header value to an integer
    content_length = atoi(header_value);
    free(header_value);

    if (content_length <= 0) {
        // Content length is not a valid positive integer
        //httpd_resp_send_err(req, HTTP_STATUS_BAD_REQUEST, "Invalid Content-Length value");
        ESP_LOGI(TAG, "Invalid Content-Length value");
        return ESP_FAIL;
    }

    // Allocate memory for the data buffer based on the content length
    char* data_buffer = (char*)malloc(content_length + 1);

    // Read the request body into the data buffer
    if (httpd_req_recv(req, data_buffer, content_length) <= 0) {
        // Handle error while receiving data
        free(data_buffer);
        //httpd_resp_send_err(req, HTTP_STATUS_INTERNAL_SERVER_ERROR, "Failed to receive request body");
        ESP_LOGI(TAG, "Failed to receive request body");
        return ESP_FAIL;
    }

    // Null-terminate the data buffer to treat it as a string
    data_buffer[content_length] = '\0';

    // Parse the received JSON data
    cJSON* root = cJSON_Parse(data_buffer);
    free(data_buffer);

    if (root == NULL) {
        // JSON parsing error
        //httpd_resp_send_err(req, HTTP_STATUS_BAD_REQUEST, "Invalid JSON data");
        ESP_LOGI(TAG, "Invalid JSON data");
        return ESP_FAIL;
    }

	cJSON* reg_number_json = cJSON_GetObjectItem(root, "selectedNumber");
	
    if (reg_number_json == NULL || !cJSON_IsString(reg_number_json) ) {
        cJSON_Delete(root);
        // Missing or invalid JSON fields
        //httpd_resp_send_err(req, HTTP_STATUS_BAD_REQUEST, "Missing or invalid JSON data fields");
        ESP_LOGI(TAG, "Missing or invalid JSON data fields");
        return ESP_FAIL;
    }


	reg_str = strdup(reg_number_json->valuestring);
    

	ESP_LOGI(TAG, "received reg: %s", reg_str);

 
    
	char str_to_save[12];
	//str_to_save[11]=0x00;
	memset(str_to_save, 0x00, 12);
	


    // Iterate over each element in the array
	
	
	//strcat(str_to_save, reg_str);
	strcat(str_to_save, "99");
	strcat(str_to_save, "99");
	strcat(str_to_save, "0000000");
    
	printf("%s\n", str_to_save);
	save_reg_data(atoi(reg_str), &str_to_save[0]);
	update_register(atoi(reg_str));
	// Process the selected days array
   // cJSON* day_item;
    //cJSON_ArrayForEach(day_item, selectedDays_json) {
       // if (cJSON_IsString(day_item)) {
         //   const char* day_str = day_item->valuestring;
           // ESP_LOGI(TAG, "Selected Day: %s", day_str);

            // Perform any additional actions based on the selected day
            // ...

            // Release memory when no longer needed
        //}
		
    //}
	//free(day_item);

	//free(selectedDays_json);

	// Send a success response to the client
	// Cerrar la conexion
	free(reg_str);	
	cJSON_Delete(root);
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, NULL, 0);
	

    return ESP_OK;
}






/**
 * wifiConnect.json handler is invoked after the connect button is pressed
 * and handles receiving the SSID and password entered by the user
 * @param req HTTP request for which the uri needs to be handled.
 * @return ESP_OK
 */


static esp_err_t http_server_wifi_connect_json_handler(httpd_req_t *req)
{
    size_t header_len;
    char* header_value;
    char* ssid_str = NULL;
    char* pass_str = NULL;
    int content_length;

    ESP_LOGI(TAG, "/wifiConnect.json requested");

    // Get the "Content-Length" header to determine the length of the request body
    header_len = httpd_req_get_hdr_value_len(req, "Content-Length");
    if (header_len <= 0) {
        // Content-Length header not found or invalid
        //httpd_resp_send_err(req, HTTP_STATUS_411_LENGTH_REQUIRED, "Content-Length header is missing or invalid");
        ESP_LOGI(TAG, "Content-Length header is missing or invalid");
        return ESP_FAIL;
    }

    // Allocate memory to store the header value
    header_value = (char*)malloc(header_len + 1);
    if (httpd_req_get_hdr_value_str(req, "Content-Length", header_value, header_len + 1) != ESP_OK) {
        // Failed to get Content-Length header value
        free(header_value);
        //httpd_resp_send_err(req, HTTP_STATUS_BAD_REQUEST, "Failed to get Content-Length header value");
        ESP_LOGI(TAG, "Failed to get Content-Length header value");
        return ESP_FAIL;
    }

    // Convert the Content-Length header value to an integer
    content_length = atoi(header_value);
    free(header_value);

    if (content_length <= 0) {
        // Content length is not a valid positive integer
        //httpd_resp_send_err(req, HTTP_STATUS_BAD_REQUEST, "Invalid Content-Length value");
        ESP_LOGI(TAG, "Invalid Content-Length value");
        return ESP_FAIL;
    }

    // Allocate memory for the data buffer based on the content length
    char* data_buffer = (char*)malloc(content_length + 1);

    // Read the request body into the data buffer
    if (httpd_req_recv(req, data_buffer, content_length) <= 0) {
        // Handle error while receiving data
        free(data_buffer);
        //httpd_resp_send_err(req, HTTP_STATUS_INTERNAL_SERVER_ERROR, "Failed to receive request body");
        ESP_LOGI(TAG, "Failed to receive request body");
        return ESP_FAIL;
    }

    // Null-terminate the data buffer to treat it as a string
    data_buffer[content_length] = '\0';

    // Parse the received JSON data
    cJSON* root = cJSON_Parse(data_buffer);
    free(data_buffer);

    if (root == NULL) {
        // JSON parsing error
        //httpd_resp_send_err(req, HTTP_STATUS_BAD_REQUEST, "Invalid JSON data");
        ESP_LOGI(TAG, "Invalid JSON data");
        return ESP_FAIL;
    }

    cJSON* ssid_json = cJSON_GetObjectItem(root, "selectedSSID");
    cJSON* pwd_json = cJSON_GetObjectItem(root, "pwd");

    if (ssid_json == NULL || pwd_json == NULL || !cJSON_IsString(ssid_json) || !cJSON_IsString(pwd_json)) {
        cJSON_Delete(root);
        // Missing or invalid JSON fields
        //httpd_resp_send_err(req, HTTP_STATUS_BAD_REQUEST, "Missing or invalid JSON data fields");
        ESP_LOGI(TAG, "Missing or invalid JSON data fields");
        return ESP_FAIL;
    }

    // Extract SSID and password from JSON
    ssid_str = strdup(ssid_json->valuestring);
    pass_str = strdup(pwd_json->valuestring);

    cJSON_Delete(root);

    // Now, you have the SSID and password in ssid_str and pass_str
    ESP_LOGI(TAG, "Received SSID: %s", ssid_str);
    ESP_LOGI(TAG, "Received Password: %s", pass_str);

    // Update the Wifi networks configuration and let the wifi application know
    wifi_config_t* wifi_config = wifi_app_get_wifi_config();
	memset(wifi_config, 0x00, sizeof(wifi_config_t));
    //memset(wifi_config->sta.ssid, 0x00, sizeof(wifi_config->sta.ssid));
	//memset(wifi_config->sta.password, 0x00, sizeof(wifi_config->sta.password));
    memcpy(wifi_config->sta.ssid, ssid_str, strlen(ssid_str));
    memcpy(wifi_config->sta.password, pass_str, strlen(pass_str));
	save_wifi_credentials(ssid_str, pass_str);
	esp_wifi_disconnect();
    //wifi_app_send_message(WIFI_APP_MSG_CONNECTING_FROM_HTTP_SERVER); // if doesn't work this need to be checked
	connect_to_wifi();


    free(ssid_str);
    free(pass_str);

    return ESP_OK;
}


/**
 * wifiConnectStatus handler updates the connection status for the web page.
 * @param req HTTP request for which the uri needs to be handled.
 * @return ESP_OK
 */
static esp_err_t http_server_wifi_connect_status_json_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "/wifiConnectStatus requested");

	char statusJSON[100];

	sprintf(statusJSON, "{\"wifi_connect_status\":%d}", g_wifi_connect_status);

	httpd_resp_set_type(req, "application/json");
	httpd_resp_send(req, statusJSON, strlen(statusJSON));

	return ESP_OK;
}

static esp_err_t http_server_get_dht_sensor_readings_json_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/dhtSensor.json requested (NTC + PIR)");

    // Leer desde tarea de sensores (thread-safe)
    sensor_data_t sensor_data;
    esp_err_t ret = sensor_task_get_data(&sensor_data);
    
    float temp_c = (ret == ESP_OK && sensor_data.temperature_valid) 
                   ? sensor_data.temperature_celsius : -999.0f;
    bool motion = pir_is_motion_active();

    char json[128];

    // Si la NTC devuelve error (-999), mandamos null
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




/**
 * Sets up the default httpd server configuration.
 * @return http server instance handle if successful, NULL otherwise.
 */
static httpd_handle_t http_server_configure(void)
{
	// Generate the default configuration
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	// Create the message queue
	http_server_monitor_queue_handle = xQueueCreate(3, sizeof(http_server_queue_message_t));

	// Create HTTP server monitor task
	xTaskCreatePinnedToCore(&http_server_monitor, "http_server_monitor", HTTP_SERVER_MONITOR_STACK_SIZE, NULL, HTTP_SERVER_MONITOR_PRIORITY, &task_http_server_monitor, HTTP_SERVER_MONITOR_CORE_ID);

	

	// The core that the HTTP server will run on
	config.core_id = HTTP_SERVER_TASK_CORE_ID;

	// Adjust the default priority to 1 less than the wifi application task
	config.task_priority = HTTP_SERVER_TASK_PRIORITY;

	// Bump up the stack size (default is 4096)
	config.stack_size = HTTP_SERVER_TASK_STACK_SIZE;

	// Increase uri handlers
	config.max_uri_handlers = 20;

	// Increase the timeout limits
	config.recv_wait_timeout = 10;
	config.send_wait_timeout = 10;

	ESP_LOGI(TAG,
			"http_server_configure: Starting server on port: '%d' with task priority: '%d'",
			config.server_port,
			config.task_priority);

	// Start the httpd server
	if (httpd_start(&http_server_handle, &config) == ESP_OK)
	{
		ESP_LOGI(TAG, "http_server_configure: Registering URI handlers");

		//
		httpd_uri_t fan_control_uri = {
			.uri     = "/fanControl.json",
			.method  = HTTP_POST,
			.handler = http_server_fan_control_handler,
			.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &fan_control_uri);


		// register query handler
		httpd_uri_t jquery_js = {
				.uri = "/jquery-3.3.1.min.js",
				.method = HTTP_GET,
				.handler = http_server_jquery_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &jquery_js);

		// register index.html handler
		httpd_uri_t index_html = {
				.uri = "/",
				.method = HTTP_GET,
				.handler = http_server_index_html_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &index_html);

		// register app.css handler
		httpd_uri_t app_css = {
				.uri = "/app.css",
				.method = HTTP_GET,
				.handler = http_server_app_css_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &app_css);

		// register app.js handler
		httpd_uri_t app_js = {
				.uri = "/app.js",
				.method = HTTP_GET,
				.handler = http_server_app_js_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &app_js);

		// register favicon.ico handler
		httpd_uri_t favicon_ico = {
				.uri = "/favicon.ico",
				.method = HTTP_GET,
				.handler = http_server_favicon_ico_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &favicon_ico);

		httpd_uri_t toogle_led = {
				.uri = "/toogle_led.json",
				.method = HTTP_POST,
				.handler = http_server_toogle_led_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &toogle_led );


		// register wifiConnect.json handler
		httpd_uri_t wifi_connect_json = {
				.uri = "/wifiConnect.json",
				.method = HTTP_POST,
				.handler = http_server_wifi_connect_json_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &wifi_connect_json);
		

		// register OTAupdate handler
		httpd_uri_t OTA_update = {
				.uri = "/OTAupdate",
				.method = HTTP_POST,
				.handler = http_server_OTA_update_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &OTA_update);

		// register OTAstatus handler
		httpd_uri_t OTA_status = {
				.uri = "/OTAstatus",
				.method = HTTP_POST,
				.handler = http_server_OTA_status_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &OTA_status);


		// register OTAstatus handler
		httpd_uri_t register_change = {
				.uri = "/regchange.json",
				.method = HTTP_POST,
				.handler = http_server_register_change_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &register_change);

		// register erase handler
		httpd_uri_t register_erase = {
				.uri = "/regerase.json",
				.method = HTTP_POST,
				.handler = http_server_register_erase_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &register_erase);
		

		//register dhtSensor.json handler
		
		httpd_uri_t dht_sensor_json = {
				.uri = "/dhtSensor.json",
				.method = HTTP_GET,
				.handler = http_server_get_dht_sensor_readings_json_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &dht_sensor_json);
		



		

		// register wifiConnectStatus.json handler
		httpd_uri_t wifi_connect_status_json = {
				.uri = "/wifiConnectStatus",
				.method = HTTP_POST,
				.handler = http_server_wifi_connect_status_json_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &wifi_connect_status_json);

		httpd_uri_t read_range_uri = {
				.uri = "/readreg.json",
				.method = HTTP_GET,
				.handler = http_server_read_register_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &read_range_uri );

		// register registers API endpoints (from registers.c)
		register_registers_endpoints(http_server_handle);


		// register time.json handler
        httpd_uri_t time_json = {
                .uri = "/time.json",
                .method = HTTP_GET,
                .handler = http_server_time_json_handler,
                .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &time_json);





		

		return http_server_handle;
	}

	return NULL;
}

void http_server_start(void)
{
	if (http_server_handle == NULL)
	{
		sensors_init();
		//fan_control_init();
		http_server_handle = http_server_configure();
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

BaseType_t http_server_monitor_send_message(http_server_message_e msgID)
{
	http_server_queue_message_t msg;
	msg.msgID = msgID;
	return xQueueSend(http_server_monitor_queue_handle, &msg, portMAX_DELAY);
}

void http_server_fw_update_reset_callback(void *arg)
{
	ESP_LOGI(TAG, "http_server_fw_update_reset_callback: Timer timed-out, restarting the device");
	esp_restart();
}



