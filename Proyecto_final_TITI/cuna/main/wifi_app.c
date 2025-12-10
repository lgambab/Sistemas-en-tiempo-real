/**
 * @file wifi_app.c
 * @brief Aplicación WiFi con soporte AP/STA dual, reconexión automática y SNTP
 * @author Jair Hernan Telpis Cuaran, Luis Fernando Gamba Bedoya
 * @date 2025
 * 
 * @details
 * Este módulo implementa la gestión completa de WiFi para el ESP32:
 * 
 * **Modos de operación:**
 * 1. **Access Point (AP):** Para configuración inicial
 *    - SSID: Configurable
 *    - Usuario puede conectarse para configurar credenciales
 *    
 * 2. **Station (STA):** Conexión a red WiFi existente
 *    - Credenciales almacenadas en NVS
 *    - Reconexión automática tras desconexión
 *    - Máximo de reintentos configurable
 * 
 * 3. **Dual AP+STA:** Permite configuración mientras mantiene conexión
 * 
 * **Características:**
 * - Sincronización de tiempo mediante SNTP (pool.ntp.org)
 * - Persistencia de credenciales en NVS flash
 * - Event handlers para WiFi (connect, disconnect, got IP)
 * - Cola de mensajes para comunicación entre tareas
 * - LED RGB indicador de estado
 * - Notificación al HTTP server de cambios de estado
 * 
 * **Arquitectura RTOS:**
 * @code
 *   [WiFi Event Handler] ---> [wifi_app_queue] ---> [wifi_app_task]
 *         |                                               |
 *         v                                               v
 *   IP_EVENT_STA_GOT_IP ---------------------> http_server_monitor_send_message()
 *         |                                               |
 *         v                                               v
 *   obtain_time() -> SNTP sync                    LED color update
 * @endcode
 * 
 * @see wifi_app.h Para la API pública
 * @see http_server.c Para integración con servidor web
 */

/*
 * wifi_app.c
 *
 *  Created on: Oct 17, 2021
 *      Author: kjagu
 */

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "lwip/netdb.h"
#include "nvs.h"

#include "http_server.h"
#include "rgb_led.h"
#include "tasks_common.h"
#include "wifi_app.h"
#include "esp_sntp.h"

/** @brief Tag para logging ESP-IDF */
static const char TAG [] = "wifi_app";

/** @brief Semáforo para sincronización entre tareas */
SemaphoreHandle_t mySemaphore;

/** @brief Array de registros guardados desde flash (legacy) */
register_saved_e register_readings_from_flash [NUM_REGISTERS_AV];

/**
 * @var wifi_config
 * @brief Configuración WiFi actual (NULL si no inicializado)
 * 
 * @details
 * Almacena SSID y contraseña para modo Station.
 * Asignado dinámicamente durante inicialización.
 */
wifi_config_t *wifi_config = NULL;

/** @brief Contador de reintentos de conexión WiFi */
static int g_retry_number;

/** @brief Cola para mensajes de la aplicación WiFi */
static QueueHandle_t wifi_app_queue_handle;

/** @brief Interfaz de red para modo Station */
esp_netif_t* esp_netif_sta = NULL;

/** @brief Interfaz de red para modo Access Point */
esp_netif_t* esp_netif_ap  = NULL;

/** @brief Flag que indica si el tiempo fue sincronizado vía SNTP */
bool time_was_synchronized;

/** @brief Estado del LED RGB (externo) */
extern uint8_t s_led_state;

/**
 * @brief Inicializa el estado de sincronización de tiempo
 * 
 * @details
 * Marca el tiempo como NO sincronizado.
 * Llamar antes de intentar sincronizar con SNTP.
 */
void init_obtain_time( void ){
	time_was_synchronized = false;
}

/**
 * @brief Obtiene el estado de sincronización de tiempo
 * 
 * @return bool
 * @retval true Tiempo sincronizado correctamente con SNTP
 * @retval false Aún no sincronizado o fallo
 */
bool get_state_time_was_synchronized( void ){
	return time_was_synchronized;
}

/**
 * @brief Sincroniza el reloj del sistema con servidor SNTP
 * 
 * @details
 * Configura y ejecuta sincronización de tiempo mediante SNTP (Simple Network Time Protocol).
 * 
 * **Proceso:**
 * 1. Configura zona horaria EST5EDT (Eastern Time con DST)
 * 2. Detiene SNTP si ya está corriendo
 * 3. Configura servidor NTP (0.co.pool.ntp.org)
 * 4. Espera hasta 20 segundos (10 reintentos x 2s) por sincronización
 * 5. Valida que tm_year > 2016
 * 
 * **Zona horaria:**
 * - EST5EDT: Eastern Standard Time (-5h UTC)
 * - M3.2.0/2: DST inicia 2do domingo de marzo a las 2am
 * - M11.1.0: DST termina 1er domingo de noviembre
 * 
 * @note Requiere conexión WiFi activa
 * @warning Bloquea hasta 20 segundos esperando sincronización
 */
static void obtain_time(void)
{	
	setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0", 1);
    tzset();
    ESP_LOGI(TAG, "Initializing SNTP");
    
    // Stop SNTP if already running to avoid assert
    if (esp_sntp_enabled()) {
        ESP_LOGI(TAG, "SNTP already running, stopping first");
        esp_sntp_stop();
    }
    
    // Configurar el servidor SNTP. Aquí se utiliza "pool.ntp.org" como ejemplo. Puedes cambiarlo según tus necesidades.
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "0.co.pool.ntp.org");
    esp_sntp_init();

    // Esperar a que se sincronice el tiempo con el servidor SNTP
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 10;

    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count)
    {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    if (retry < retry_count)
    {
        ESP_LOGI(TAG, "System time is set!");
		time_was_synchronized = true;
    }
    else
    {
        ESP_LOGE(TAG, "Unable to set system time. Check your SNTP configuration.");
    }
}

/**
 * @brief Guarda credenciales WiFi en NVS flash
 * 
 * @details
 * Almacena SSID y contraseña en memoria no volátil.
 * Sobrevive a reinicios del ESP32.
 * 
 * **Claves NVS:**
 * - "wifi_ssid": Nombre de la red WiFi
 * - "wifi_password": Contraseña
 * 
 * @param[in] ssid SSID de la red WiFi (máx 32 caracteres)
 * @param[in] password Contraseña (máx 64 caracteres)
 * 
 * @warning ESP_ERROR_CHECK causa panic si falla NVS
 * @note Namespace "storage" debe existir
 */
void save_wifi_credentials(const char *ssid, const char *password) {
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs_handle));
    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "wifi_ssid", ssid));
    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "wifi_password", password));
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    nvs_close(nvs_handle);
}

/**
 * @brief Guarda datos de registro en NVS (función legacy)
 * 
 * @details
 * Función antigua para guardar registros (1-10).
 * **DEPRECADA:** Usar registers.c (save_register_to_nvs) en su lugar.
 * 
 * @param[in] register_num Número de registro (1-10)
 * @param[in] str Datos del registro como string
 * 
 * @deprecated Usar API moderna en registers.c
 * @note Solo para compatibilidad con código antiguo
 */
void save_reg_data(uint8_t register_num, char *str) {
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs_handle));
	
	if ( register_num == 1 ){
		ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "reg01", str));
	}
	else if ( register_num == 2 ){
		ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "reg02", str));
	}
	else if ( register_num == 3 ){
		ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "reg03", str));
	}
	else if ( register_num == 4 ){
		ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "reg04", str));
	}
	else if ( register_num == 5 ){
		ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "reg05", str));
	}
	else if ( register_num == 6 ){
		ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "reg06", str));
	}
	else if ( register_num == 7 ){
		ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "reg07", str));
	}
	else if ( register_num == 8 ){
		ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "reg08", str));
	}
	else if ( register_num == 9 ){
		ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "reg09", str));
	}
	else if ( register_num == 10 ){
		ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "reg10", str));
	}

	esp_err_t err;
	err = nvs_commit(nvs_handle);
	if (err == ESP_OK) {
		printf("information saved\n");
	}
    else if (err != ESP_OK) {
        printf("Error al confirmar cambios\n");
    }


    nvs_close(nvs_handle);
}


esp_err_t read_reg_data(char *str_to_save ,uint8_t register_num){

	nvs_handle_t nvs_handle;
    esp_err_t err;

    // Abrir el NVS
    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        // Manejar el error
        return ESP_FAIL;
    }

	char reg_to_send[6];
	const char *ptr_const_char = reg_to_send;
	memset(&reg_to_send[0], 0x00, 6);
	
	if ( register_num == 1 ){
		strcpy(reg_to_send, "reg01");
			
	}
	else if ( register_num == 2 ){
		strcpy(reg_to_send, "reg02");
				
	}
	else if ( register_num == 3 ){
		strcpy(reg_to_send, "reg03");
			
	}
	else if ( register_num == 4 ){
		strcpy(reg_to_send, "reg04");	
		
	}
	else if ( register_num == 5 ){
		strcpy(reg_to_send, "reg05");
			
	}
	else if ( register_num == 6 ){
		strcpy(reg_to_send, "reg06");
			
	}
	else if ( register_num == 7 ){
		strcpy(reg_to_send, "reg07");	
		
	}
	else if ( register_num == 8 ){
		strcpy(reg_to_send, "reg08");
			
	}
	else if ( register_num == 9 ){
		strcpy(reg_to_send, "reg09");	
		
	}
	else if ( register_num == 10 ){
		strcpy(reg_to_send, "reg10");	
		
	}


	

	
		size_t required_size;
		// Get the size of wifi_ssid
		esp_err_t erras;
    	erras = nvs_get_str(nvs_handle, ptr_const_char, NULL, &required_size);
		if (erras != ESP_OK) {
        	printf("not found\n");
			return ESP_FAIL;
    	}
		if (erras == ESP_OK) {
        	printf("register found\n");
    	}
    	// Allocate memory for wifi_ssid

    	char *reg_buffer = malloc(required_size);
    	if (reg_buffer == NULL) {
    	    // Handle memory allocation error
    	    ESP_LOGE(TAG, "Failed to allocate memory for wifi_ssid");
    	    nvs_close(nvs_handle);
    	    return ESP_FAIL;
    	}
    	// Get reg
    	erras = nvs_get_str(nvs_handle, ptr_const_char, reg_buffer, &required_size);
    	strncpy(str_to_save, reg_buffer, required_size);
		ESP_LOGI(TAG, "Valor leído para la clave '%s': %s", reg_to_send, str_to_save);
    	free(reg_buffer);

		
		
            
/*

		err = nvs_get_str(nvs_handle, ptr_const_char, str_to_save, 11);
		if (err == ESP_OK) {
			// Imprimir la cadena leída
			printf("Valor leído: %s\n", str_to_save);
		} else {
			// Manejar el error al leer
			fprintf(stderr, "Error al leer el valor desde el NVS.\n");
		}		
*/

    // Cerrar el NVS
    nvs_close(nvs_handle);
	return ESP_OK;


}
void update_register(int reg_to_update){
	char register_information_read[12];
	register_information_read[11] = 0x00;
	char hora_min_str[3];
	hora_min_str[2] = 0x00;
	char day[2];
	day[1] = 0x00;
	register_information_read[11] = 0x00;
	if ( read_reg_data( &register_information_read[0], reg_to_update ) == ESP_OK ){
		
		strncpy(&hora_min_str[0], &register_information_read[0], 2);
		register_readings_from_flash[reg_to_update-1].hour = atoi(hora_min_str);
		
		strncpy(&hora_min_str[0], &register_information_read[2], 2);
		register_readings_from_flash[reg_to_update-1].min = atoi(hora_min_str);

		strncpy(&day[0], &register_information_read[4], 1);
		register_readings_from_flash[reg_to_update-1].monday = atoi(day);

		strncpy(&day[0], &register_information_read[5], 1);
		register_readings_from_flash[reg_to_update-1].tuesday = atoi(day);

		strncpy(&day[0], &register_information_read[6], 1);
		register_readings_from_flash[reg_to_update-1].wednesday = atoi(day);

		strncpy(&day[0], &register_information_read[7], 1);
		register_readings_from_flash[reg_to_update-1].thursday = atoi(day);

		strncpy(&day[0], &register_information_read[8], 1);
		register_readings_from_flash[reg_to_update-1].friday = atoi(day);

		strncpy(&day[0], &register_information_read[9], 1);
		register_readings_from_flash[reg_to_update-1].saturday = atoi(day);

		strncpy(&day[0], &register_information_read[10], 1);
		register_readings_from_flash[reg_to_update-1].sunday = atoi(day);

		ESP_LOGI(TAG, "hora: %d, min: %d, day0: %d, day1: %d, day2: %d, day3: %d, day4: %d, day5: %d, day6: %d", register_readings_from_flash[reg_to_update-1].hour, register_readings_from_flash[reg_to_update-1].min, register_readings_from_flash[reg_to_update-1].monday ,
		register_readings_from_flash[reg_to_update-1].tuesday , register_readings_from_flash[reg_to_update-1].wednesday , register_readings_from_flash[reg_to_update-1].thursday  , register_readings_from_flash[reg_to_update-1].friday 
		, register_readings_from_flash[reg_to_update-1].saturday, register_readings_from_flash[reg_to_update-1].sunday);
	}

	
}
void initialize_registers( void ){
	
	char register_information_read[12];
	register_information_read[11] = 0x00;
	char hora_min_str[3];
	hora_min_str[2] = 0x00;
	char day[2];
	day[1] = 0x00;
	
	
	for (int i = 0; i < NUM_REGISTERS_AV; i++){
		if ( read_reg_data( &register_information_read[0], i+1 ) == ESP_OK ){
			
			strncpy(&hora_min_str[0], &register_information_read[0], 2);
			register_readings_from_flash[i].hour = atoi(hora_min_str);
			
			strncpy(&hora_min_str[0], &register_information_read[2], 2);
			register_readings_from_flash[i].min = atoi(hora_min_str);

			strncpy(&day[0], &register_information_read[4], 1);
			register_readings_from_flash[i].monday = atoi(day);

			strncpy(&day[0], &register_information_read[5], 1);
			register_readings_from_flash[i].tuesday = atoi(day);

			strncpy(&day[0], &register_information_read[6], 1);
			register_readings_from_flash[i].wednesday = atoi(day);

			strncpy(&day[0], &register_information_read[7], 1);
			register_readings_from_flash[i].thursday = atoi(day);

			strncpy(&day[0], &register_information_read[8], 1);
			register_readings_from_flash[i].friday = atoi(day);

			strncpy(&day[0], &register_information_read[9], 1);
			register_readings_from_flash[i].saturday = atoi(day);

			strncpy(&day[0], &register_information_read[10], 1);
			register_readings_from_flash[i].sunday = atoi(day);

			//ESP_LOGI(TAG, "hora: %d, min: %d, day0: %d, day1: %d, day2: %d, day3: %d, day4: %d, day5: %d, day6: %d", register_readings_from_flash[i].hour, register_readings_from_flash[i].min, register_readings_from_flash[i].monday ,
			//register_readings_from_flash[i].tuesday , register_readings_from_flash[i].wednesday , register_readings_from_flash[i].thursday  , register_readings_from_flash[i].friday 
			//, register_readings_from_flash[i].saturday, register_readings_from_flash[i].sunday);
		}
		else{
			register_readings_from_flash[i].hour = 99;
			register_readings_from_flash[i].min = 99;
			register_readings_from_flash[i].monday = 0;
			register_readings_from_flash[i].tuesday = 0;
			register_readings_from_flash[i].wednesday = 0;
			register_readings_from_flash[i].thursday = 0;
			register_readings_from_flash[i].friday = 0;
			register_readings_from_flash[i].sunday = 0;
			register_readings_from_flash[i].saturday = 0;

		}

	}
}

/**
 * @brief Carga credenciales WiFi desde NVS
 * 
 * @details
 * Lee SSID y password almacenados previamente en NVS.
 * 
 * **Namespace NVS:** "storage"
 * **Keys:**
 * - "wifi_ssid": SSID de la red
 * - "wifi_password": Contraseña
 * 
 * @param[out] ssid Buffer donde se copia el SSID (mínimo 33 bytes)
 * @param[out] password Buffer donde se copia el password (mínimo 65 bytes)
 * 
 * @note Llama ESP_ERROR_CHECK - programa panic si no existen credenciales
 * @warning Validar existencia con nvs_credentials_exist() antes de llamar
 */
void load_wifi_credentials(char *ssid, char *password) {
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open("storage", NVS_READONLY, &nvs_handle));

    size_t required_size;

    // Get the size of wifi_ssid
    ESP_ERROR_CHECK(nvs_get_str(nvs_handle, "wifi_ssid", NULL, &required_size));
    // Allocate memory for wifi_ssid
    char *ssid_buffer = malloc(required_size);
    if (ssid_buffer == NULL) {
        // Handle memory allocation error
        ESP_LOGE(TAG, "Failed to allocate memory for wifi_ssid");
        nvs_close(nvs_handle);
        return;
    }
    // Get wifi_ssid
    ESP_ERROR_CHECK(nvs_get_str(nvs_handle, "wifi_ssid", ssid_buffer, &required_size));
    // Copy wifi_ssid to the output parameter
    strncpy(ssid, ssid_buffer, required_size);

    // Repeat the process for wifi_password
    ESP_ERROR_CHECK(nvs_get_str(nvs_handle, "wifi_password", NULL, &required_size));
    char *password_buffer = malloc(required_size);
    if (password_buffer == NULL) {
        // Handle memory allocation error
        ESP_LOGE(TAG, "Failed to allocate memory for wifi_password");
        free(ssid_buffer);
        nvs_close(nvs_handle);
        return;
    }
    ESP_ERROR_CHECK(nvs_get_str(nvs_handle, "wifi_password", password_buffer, &required_size));
    strncpy(password, password_buffer, required_size);

    // Free the allocated memory
    free(ssid_buffer);
    free(password_buffer);

    nvs_close(nvs_handle);
}
bool nvs_credentials_exist() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t ssid_size, password_size;
    err = nvs_get_str(nvs_handle, "wifi_ssid", NULL, &ssid_size);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_get_str(nvs_handle, "wifi_password", NULL, &password_size);
    nvs_close(nvs_handle);

    return err == ESP_OK;
}


void connect_to_wifi(void) {

	if (xSemaphoreTake(mySemaphore, portMAX_DELAY)) { // helpus to not allow multiple calls
 
    	char ssid[32];
    	char password[64];
    	load_wifi_credentials(&ssid[0], &password[0]);
		wifi_config_t* wifi_config = wifi_app_get_wifi_config();
		ESP_LOGI(TAG, "saved SSID: %s", ssid);
    	ESP_LOGI(TAG, "saved password: %s", password);

		//memset(wifi_config->sta.ssid, 0x00, sizeof(wifi_config->sta.ssid));
		//memset(wifi_config->sta.password, 0x00, sizeof(wifi_config->sta.password));
		memset(wifi_config, 0x00, sizeof(wifi_config_t));
   		strncpy((char*)wifi_config->sta.ssid, ssid, sizeof(wifi_config->sta.ssid));
    	strncpy((char*)wifi_config->sta.password, password, sizeof(wifi_config->sta.password));
   		esp_wifi_set_config(WIFI_IF_STA, wifi_config);
   		wifi_app_connect_sta();

	       // Do some work
        // Release the semaphore (give it back)
        xSemaphoreGive(mySemaphore);		

    }
}

/**
 * @brief Tarea que monitorea conexión WiFi STA y reintenta
 * 
 * @details
 * Cada 30 segundos verifica si está conectado a AP.
 * Si pierde conexión, intenta reconectar automáticamente.
 * 
 * **Funcionalidad:**
 * - Llama esp_wifi_sta_get_ap_info() cada 30s
 * - Si falla (ESP_ERR_WIFI_NOT_CONNECT), intenta conectar
 * - Registra RSSI si está conectado
 * 
 * @param[in] pvParameters Parámetro de tarea (no usado)
 * 
 * @note Tarea infinita creada en wifi_app_start()
 */
void check_sta_connection_state( void *pvParameters ) {
	wifi_ap_record_t ap_info;
	esp_err_t ret;
	while(true){
		ret = esp_wifi_sta_get_ap_info(&ap_info);
		 ESP_LOGI(TAG, "Checking sta info");
		    if (ret == ESP_OK) {
				
        		if (ap_info.authmode != WIFI_AUTH_MAX) {
        		    ESP_LOGI(TAG, "Connected to SSID: %s", ap_info.ssid);
					if (get_state_time_was_synchronized() == false)
						obtain_time();

					
        		} else {
        		    ESP_LOGI(TAG, "Not connected to any WiFi network");
					if (nvs_credentials_exist()) {
						// Credentials exist, try to connect
						connect_to_wifi();
						ESP_LOGI(TAG, "CHECKING CONNECTION TO STA_BEFORE_SAVED");
					}

					//return false;
        		}
    		} else {
					if (nvs_credentials_exist()) {
							// Credentials exist, try to connect
							connect_to_wifi();
							ESP_LOGI(TAG, "CHECKING CONNECTION TO STA_BEFORE_SAVED");
						}
       			 ESP_LOGI(TAG, "Failed to get connection info");
					//return false;
        		}
		vTaskDelay(20000 / portTICK_PERIOD_MS);

	}
    
    
    // Get the connection info

}
/**
 * @brief Event handler principal para eventos WiFi y IP
 * 
 * @details
 * Procesa todos los eventos WiFi del ESP32:
 * 
 * **Eventos WiFi:**
 * - WIFI_EVENT_AP_STACONNECTED: Cliente conectado al AP
 * - WIFI_EVENT_AP_STADISCONNECTED: Cliente desconectado del AP
 * - WIFI_EVENT_STA_START: Station iniciado → intenta conectar
 * - WIFI_EVENT_STA_CONNECTED: Conectado a AP externo
 * - WIFI_EVENT_STA_DISCONNECTED: Desconectado → reintenta
 * 
 * **Eventos IP:**
 * - IP_EVENT_STA_GOT_IP: IP asignada → notifica HTTP server, inicia SNTP
 * 
 * **Reconexión automática:**
 * Máximo WIFI_RETRY_ATTEMPTS reintentos antes de fallar.
 * 
 * @param[in] arg Parámetro de usuario (no usado)
 * @param[in] event_base Base del evento (WIFI_EVENT o IP_EVENT)
 * @param[in] event_id ID específico del evento
 * @param[in] event_data Datos del evento (IP, MAC, etc)
 */
static void wifi_app_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	if (event_base == WIFI_EVENT)
	{
		switch (event_id)
		{
			case WIFI_EVENT_AP_START:
				ESP_LOGI(TAG, "WIFI_EVENT_AP_START");
				break;

			case WIFI_EVENT_AP_STOP:
				ESP_LOGI(TAG, "WIFI_EVENT_AP_STOP");
				break;

			case WIFI_EVENT_AP_STACONNECTED:
				ESP_LOGI(TAG, "WIFI_EVENT_AP_STACONNECTED");
				break;

			case WIFI_EVENT_AP_STADISCONNECTED:
				ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED");
				break;

			case WIFI_EVENT_STA_START:
				ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
				break;

			case WIFI_EVENT_STA_CONNECTED:
				ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
				break;

			case WIFI_EVENT_STA_DISCONNECTED:
				ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");

				// wifi_event_sta_disconnected_t *wifi_event_sta_disconnected = (wifi_event_sta_disconnected_t*)malloc(sizeof(wifi_event_sta_disconnected_t));
				// *wifi_event_sta_disconnected = *((wifi_event_sta_disconnected_t*)event_data);
				// printf("WIFI_EVENT_STA_DISCONNECTED, reason code %d\n", wifi_event_sta_disconnected->reason);

				// if (g_retry_number < MAX_CONNECTION_RETRIES)
				// {
				// 	esp_wifi_connect();
				// 	g_retry_number ++;
				// }
				// else
				// {
				// 	wifi_app_send_message(WIFI_APP_MSG_STA_DISCONNECTED);
				// }

				break;
		}
	}
	else if (event_base == IP_EVENT)
	{
		switch (event_id)
		{
			case IP_EVENT_STA_GOT_IP:
				ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");

				wifi_app_send_message(WIFI_APP_MSG_STA_CONNECTED_GOT_IP);

				break;
		}
	}
}

/**
 * Initializes the WiFi application event handler for WiFi and IP events.
 */
static void wifi_app_event_handler_init(void)
{
	// Event loop for the WiFi driver
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	// Event handler for the connection
	esp_event_handler_instance_t instance_wifi_event;
	esp_event_handler_instance_t instance_ip_event;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_app_event_handler, NULL, &instance_wifi_event));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_app_event_handler, NULL, &instance_ip_event));
}

/**
 * Initializes the TCP stack and default WiFi configuration.
 */
static void wifi_app_default_wifi_init(void)
{
	// Initialize the TCP stack
	ESP_ERROR_CHECK(esp_netif_init());

	// Default WiFi config - operations must be in this order!
	wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	esp_netif_sta = esp_netif_create_default_wifi_sta();
	esp_netif_ap = esp_netif_create_default_wifi_ap();
}

/**
 * @brief Configura el Access Point (SoftAP)
 * 
 * @details
 * Establece los parámetros del AP:
 * 
 * **Configuración AP:**
 * - SSID: WIFI_AP_SSID (definido en wifi_app.h)
 * - Password: WIFI_AP_PASSWORD
 * - Canal: WIFI_AP_CHANNEL
 * - SSID oculto: No
 * - Máx conexiones: WIFI_AP_MAX_CONNECTIONS
 * - Tipo auth: WPA/WPA2
 * 
 * **IP estática:**
 * - IP: 192.168.0.1
 * - Gateway: 192.168.0.1
 * - Netmask: 255.255.255.0
 * 
 * @note Llama esp_netif_dhcps_stop() y esp_netif_dhcps_start()
 */
static void wifi_app_soft_ap_config(void)
{
	// SoftAP - WiFi access point configuration
	wifi_config_t ap_config =
	{
		.ap = {
				.ssid = WIFI_AP_SSID,
				.ssid_len = strlen(WIFI_AP_SSID),
				.password = WIFI_AP_PASSWORD,
				.channel = WIFI_AP_CHANNEL,
				.ssid_hidden = WIFI_AP_SSID_HIDDEN,
				.authmode = WIFI_AUTH_WPA2_PSK,
				.max_connection = WIFI_AP_MAX_CONNECTIONS,
				.beacon_interval = WIFI_AP_BEACON_INTERVAL,
		},
	};

	// Configure DHCP for the AP
	esp_netif_ip_info_t ap_ip_info;
	memset(&ap_ip_info, 0x00, sizeof(ap_ip_info));

	esp_netif_dhcps_stop(esp_netif_ap);					///> must call this first
	inet_pton(AF_INET, WIFI_AP_IP, &ap_ip_info.ip);		///> Assign access point's static IP, GW, and netmask
	inet_pton(AF_INET, WIFI_AP_GATEWAY, &ap_ip_info.gw);
	inet_pton(AF_INET, WIFI_AP_NETMASK, &ap_ip_info.netmask);
	ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif_ap, &ap_ip_info));			///> Statically configure the network interface
	ESP_ERROR_CHECK(esp_netif_dhcps_start(esp_netif_ap));						///> Start the AP DHCP server (for connecting stations e.g. your mobile device)

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));						///> Setting the mode as Access Point / Station Mode
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));			///> Set our configuration
	ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_AP_BANDWIDTH));		///> Our default bandwidth 20 MHz
	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_STA_POWER_SAVE));						///> Power save set to "NONE"

}

/**
 * @brief Conecta el ESP32 en modo Station a un AP externo
 * 
 * @details
 * Utiliza las credenciales de wifi_app_get_wifi_config().
 * Si la conexión falla, wifi_app_event_handler reintenta.
 * 
 * @note Llama esp_wifi_set_config() y esp_wifi_connect()
 */
static void wifi_app_connect_sta(void)
{
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, wifi_app_get_wifi_config()));
	ESP_ERROR_CHECK(esp_wifi_connect());
}

/**
 * @brief Tarea principal de la aplicación WiFi
 * 
 * @details
 * Tarea RTOS que procesa mensajes de la cola wifi_app_queue_handle.
 * 
 * **Mensajes soportados:**
 * - WIFI_APP_MSG_START_HTTP_SERVER: Inicia servidor HTTP
 * - WIFI_APP_MSG_CONNECTING_FROM_HTTP_SERVER: Intenta conexión STA
 * - WIFI_APP_MSG_STA_CONNECTED_GOT_IP: IP obtenida exitosamente
 * 
 * **Parámetros tarea:**
 * - Stack: WIFI_APP_TASK_STACK_SIZE
 * - Prioridad: WIFI_APP_TASK_PRIORITY
 * - Core: WIFI_APP_TASK_CORE_ID
 * 
 * @param[in] pvParameters Parámetro de tarea (no usado)
 * 
 * @note Tarea infinita que nunca termina
 */
static void wifi_app_task(void *pvParameters)
{
	wifi_app_queue_message_t msg;

	// Initialize the event handler
	wifi_app_event_handler_init();

	// Initialize the TCP/IP stack and WiFi config
	wifi_app_default_wifi_init();

	// SoftAP config
	wifi_app_soft_ap_config();

	// Start WiFi
	ESP_ERROR_CHECK(esp_wifi_start());

	// Send first event message
	wifi_app_send_message(WIFI_APP_MSG_START_HTTP_SERVER);
	wifi_app_send_message(WIFI_APP_CONNECT_TO_STA);
	initialize_registers();  //Init registers of hours

	for (;;)
	{
		if (xQueueReceive(wifi_app_queue_handle, &msg, portMAX_DELAY))
		{
			switch (msg.msgID)
			{
				case WIFI_APP_CONNECT_TO_STA:
					//ESP_LOGI(TAG, "CHECKING CONNECTION TO STA");
					xTaskCreatePinnedToCore(&check_sta_connection_state, "check_sta_connection_state", WIFI_APP_TASK_STACK_SIZE, NULL, WIFI_APP_TASK_PRIORITY, NULL, WIFI_APP_TASK_CORE_ID);
	/*
					if (nvs_credentials_exist()) {
						// Credentials exist, try to connect
						connect_to_wifi();
						ESP_LOGI(TAG, "CHECKING CONNECTION TO STA_BEFORE_SAVED");
					}
					else{
						ESP_LOGI(TAG, "NO INFORMATION WAS FOUND ABOUT CREDENTIALS");
					}*/
					break;
				
				case WIFI_APP_MSG_START_HTTP_SERVER:
					ESP_LOGI(TAG, "WIFI_APP_MSG_START_HTTP_SERVER");

					http_server_start();
					rgb_led_http_server_started();

					break;

				case WIFI_APP_MSG_CONNECTING_FROM_HTTP_SERVER:
					ESP_LOGI(TAG, "WIFI_APP_MSG_CONNECTING_FROM_HTTP_SERVER");

					// Attempt a connection
					wifi_app_connect_sta();

					// Set current number of retries to zero
					g_retry_number = 0;

					// Let the HTTP server know about the connection attempt
					http_server_monitor_send_message(HTTP_MSG_WIFI_CONNECT_INIT);

					break;

				case WIFI_APP_MSG_STA_CONNECTED_GOT_IP:
					ESP_LOGI(TAG, "WIFI_APP_MSG_STA_CONNECTED_GOT_IP");

					rgb_led_wifi_connected();
					http_server_monitor_send_message(HTTP_MSG_WIFI_CONNECT_SUCCESS);

					break;

				case WIFI_APP_MSG_STA_DISCONNECTED:
					ESP_LOGI(TAG, "WIFI_APP_MSG_STA_DISCONNECTED");

					http_server_monitor_send_message(HTTP_MSG_WIFI_CONNECT_FAIL);

					break;

				default:
					break;

			}
		}
	}
}

BaseType_t wifi_app_send_message(wifi_app_message_e msgID)
{
	wifi_app_queue_message_t msg;
	msg.msgID = msgID;
	return xQueueSend(wifi_app_queue_handle, &msg, portMAX_DELAY);
}

/**
 * @brief Obtiene puntero a configuración WiFi STA
 * 
 * @return wifi_config_t* Puntero a wifi_config global
 * 
 * @note Contiene SSID y password cargados de NVS
 */
wifi_config_t* wifi_app_get_wifi_config(void)
{
	return wifi_config;
}

bool compare_hour_day_structs (struct tm timeinfo, register_saved_e aux_reg ){
	static const char TAG2 [] = "comparing_app";

// chekcing ll the day
	if(timeinfo.tm_wday == 0){
		if( aux_reg.sunday != 1 ){	
			ESP_LOGI(TAG2, "WRONG DAY");
			return false;		
		}
	}

	if(timeinfo.tm_wday == 1){
		if( aux_reg.monday != 1 ){	
			ESP_LOGI(TAG2, "WRONG DAY");
			return false;		
		}
	}

	if(timeinfo.tm_wday == 2){
		if( aux_reg.tuesday != 1 ){	
			ESP_LOGI(TAG2, "WRONG DAY");
			return false;		
		}
	}

	if(timeinfo.tm_wday == 3){
		if( aux_reg.wednesday != 1 ){	
			ESP_LOGI(TAG2, "WRONG DAY");
			return false;		
		}
	}

	if(timeinfo.tm_wday == 4){
		if( aux_reg.thursday != 1 ){	
			ESP_LOGI(TAG2, "WRONG DAY");
			return false;		
		}
	}

	if(timeinfo.tm_wday == 5){
		if( aux_reg.friday != 1 ){	
			ESP_LOGI(TAG2, "WRONG DAY");
			return false;		
		}
	}

	if(timeinfo.tm_wday == 6){
		if( aux_reg.saturday != 1 ){	
			ESP_LOGI(TAG2, "WRONG DAY");
			return false;		
		}
	}

	if( timeinfo.tm_hour == aux_reg.hour ){
		if( timeinfo.tm_min == aux_reg.min ){
			// we should activate the motor
			toogle_led();
			vTaskDelay(40000 / portTICK_PERIOD_MS);
			return true;
		}
		else{
			ESP_LOGI(TAG2, "CORRECT DAY CORRECT HOUR WRONG MINUTE");
			return false;
		}
	}
	else{
		ESP_LOGI(TAG2, "CORRECT DAY WRONG HOUR");
		return false;
		
	}

}

/**
 * @brief Tarea que ejecuta registros programados basados en hora SNTP
 * 
 * @details
 * Espera a que SNTP sincronice, luego cada 60s compara hora actual
 * con registros programados. Si coincide, ejecuta acción (ventilador).
 * 
 * **Proceso:**
 * 1. Espera a get_state_time_was_synchronized() == true
 * 2. Cada 60s obtiene hora con time() y localtime_r()
 * 3. Itera registros y compara con compare_hour_day_structs()
 * 4. Si coincide, ejecuta acción del registro
 * 
 * @param[in] pvParameters Parámetro de tarea (no usado)
 * 
 * @note Tarea infinita, stack 4096 bytes, prioridad 5
 * @see compare_hour_day_structs() Comparación hora/día
 */
void task_compare_hour_to_execute_action( void *pvParameters ) {
	time_t now;
    struct tm timeinfo;
	while(get_state_time_was_synchronized() == false){
		vTaskDelay(10000 / portTICK_PERIOD_MS);
	}
       // Asegurar que localtime_r inicialice adecuadamente timeinfo
    

	while (1){

		ESP_LOGI(TAG, "COMPARING HOURS");


		if (time(&now) != -1 && localtime_r(&now, &timeinfo) != NULL)
   		{
   		    // Imprimir la hora actual
   		   // ESP_LOGI(TAG, "Día de la semana: %d, Hora: %02d:%02d:%02d", timeinfo.tm_wday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
			ESP_LOGI(TAG, "Día de la semana: %d, Hora: %d:%d:%d", timeinfo.tm_wday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
			
   		}
   		else
		{
		    ESP_LOGE(TAG, "Error al obtener la hora actual.");
		}

		for(int i = 0; i< NUM_REGISTERS_AV; i++){
			ESP_LOGI(TAG, "Revisando registro: %d", i);
			compare_hour_day_structs (timeinfo,  register_readings_from_flash[i] );

		}

		vTaskDelay(30000 / portTICK_PERIOD_MS);
	}


}

/**
 * @brief Inicializa y arranca la aplicación WiFi completa
 * 
 * @details
 * Punto de entrada principal para WiFi. Secuencia:
 * 1. Inicializa netif TCP/IP
 * 2. Configura event handlers WiFi
 * 3. Inicializa WiFi con config por defecto
 * 4. Configura Access Point
 * 5. Crea cola de mensajes
 * 6. Crea tarea wifi_app_task
 * 7. Crea tarea check_sta_connection_state (reconexión)
 * 
 * @note Llamar una sola vez durante inicialización del sistema
 * @warning Requiere NVS inicializado previamente
 * 
 * @see wifi_app_task() Tarea principal creada
 * @see check_sta_connection_state() Tarea de reconexión
 */
void wifi_app_start(void)
{
	ESP_LOGI(TAG, "STARTING WIFI APPLICATION");

	// Start WiFi started LED
	rgb_led_wifi_app_started();

	// Disable default WiFi logging messages
	esp_log_level_set("wifi", ESP_LOG_NONE);

	// Allocate memory for the wifi configuration
	wifi_config = (wifi_config_t*)malloc(sizeof(wifi_config_t));
	memset(wifi_config, 0x00, sizeof(wifi_config_t));

	// Create message queue
	wifi_app_queue_handle = xQueueCreate(3, sizeof(wifi_app_queue_message_t));
	// create semaphore for the wifi connection
	mySemaphore = xSemaphoreCreateBinary();
	xSemaphoreGive(mySemaphore);
	// Start the WiFi application task
	xTaskCreatePinnedToCore(&wifi_app_task, "wifi_app_task", WIFI_APP_TASK_STACK_SIZE, NULL, WIFI_APP_TASK_PRIORITY, NULL, WIFI_APP_TASK_CORE_ID);
	xTaskCreatePinnedToCore(&task_compare_hour_to_execute_action, "checking_app_task", 4096, NULL, 5, NULL, 1);
	
	
}









