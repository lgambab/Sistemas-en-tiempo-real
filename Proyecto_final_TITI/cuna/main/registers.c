#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "registers.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"

// Fix for VS Code IntelliSense: some builds generate sdkconfig.h in build/ and
// the editor may not see CONFIG_* macros. Define a safe default only for
// IntelliSense to avoid spurious "identifier undefined" errors.
#if defined(__INTELLISENSE__)
#ifndef CONFIG_LOG_MAXIMUM_LEVEL
#define CONFIG_LOG_MAXIMUM_LEVEL 5
#endif
#endif

static const char *TAG = "REGISTERS";
void init_nvs()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}
esp_err_t save_register_to_nvs(int id, reg_t *reg)
{
    nvs_handle_t nvs;
    char key[16];
    sprintf(key, "reg_%d", id);

    // Convertir a JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "hour", reg->hour);
    cJSON_AddNumberToObject(root, "minute", reg->minute);

    cJSON *days = cJSON_CreateArray();
    for (int i = 0; i < reg->day_count; i++)
        cJSON_AddItemToArray(days, cJSON_CreateString(reg->days[i]));
    cJSON_AddItemToObject(root, "days", days);

    char *json_str = cJSON_Print(root);

    ESP_LOGI(TAG, "save_register_to_nvs: id=%d json=%s", id, json_str);

    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs));
    esp_err_t err = nvs_set_str(nvs, key, json_str);
    nvs_commit(nvs);
    nvs_close(nvs);

    free(json_str);
    cJSON_Delete(root);

    return err;
}
esp_err_t load_register_from_nvs(int id, char *buffer, size_t buffer_len)
{
    nvs_handle_t nvs;
    char key[16];
    sprintf(key, "reg_%d", id);

    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs));

    size_t required_size;
    esp_err_t err = nvs_get_str(nvs, key, NULL, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs);
        return ESP_ERR_NOT_FOUND;
    }

    if (required_size > buffer_len) {
        nvs_close(nvs);
        return ESP_ERR_NVS_INVALID_LENGTH;
    }

    err = nvs_get_str(nvs, key, buffer, &required_size);
    nvs_close(nvs);
    return err;
}
void delete_register_from_nvs(int id)
{
    nvs_handle_t nvs;
    char key[16];
    sprintf(key, "reg_%d", id);

    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs));
    nvs_erase_key(nvs, key);
    nvs_commit(nvs);
    nvs_close(nvs);
}
esp_err_t api_post_register(httpd_req_t *req)
{
    int len = req->content_len;
    if (len <= 0 || len > 4096) {
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

    cJSON *id_json = cJSON_GetObjectItem(root, "register");
    cJSON *hour_json = cJSON_GetObjectItem(root, "hour");
    cJSON *min_json = cJSON_GetObjectItem(root, "minute");
    cJSON *days = cJSON_GetObjectItem(root, "days");

    if (!cJSON_IsNumber(id_json) || !cJSON_IsNumber(hour_json) || !cJSON_IsNumber(min_json) || !cJSON_IsArray(days)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing fields");
        return ESP_FAIL;
    }

    int id = id_json->valueint;
    int hour = hour_json->valueint;
    int minute = min_json->valueint;

    int day_count = cJSON_GetArraySize(days);
    if (day_count > 7) day_count = 7;

    reg_t reg = {0};
    reg.hour = hour;
    reg.minute = minute;
    reg.day_count = day_count;

    for (int i = 0; i < day_count; i++) {
        cJSON *item = cJSON_GetArrayItem(days, i);
        if (cJSON_IsString(item) && item->valuestring) {
            strncpy(reg.days[i], item->valuestring, sizeof(reg.days[i]) - 1);
            reg.days[i][sizeof(reg.days[i]) - 1] = '\0';
        } else {
            reg.days[i][0] = '\0';
        }
    }

    esp_err_t err = save_register_to_nvs(id, &reg);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "nvs error");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "api_post_register: saved id=%d hour=%d minute=%d days=%d", id, hour, minute, day_count);

    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}
esp_err_t api_get_registers(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    char smallbuf[256];
    for (int i = 1; i <= 10; i++) {
        char key[8]; snprintf(key, sizeof(key), "%d", i);

        esp_err_t r = load_register_from_nvs(i, smallbuf, sizeof(smallbuf));
        if (r == ESP_OK) {
            cJSON *reg_json = cJSON_Parse(smallbuf);
            if (reg_json) {
                cJSON_AddItemToObject(root, key, reg_json);
                continue;
            }
        }

        if (r == ESP_ERR_NVS_INVALID_LENGTH) {
            // read with required size
            nvs_handle_t nvs;
            char nkey[16]; snprintf(nkey, sizeof(nkey), "reg_%d", i);
            if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
                size_t required = 0;
                if (nvs_get_str(nvs, nkey, NULL, &required) == ESP_OK && required > 0) {
                    char *dyn = malloc(required);
                    if (dyn) {
                        if (nvs_get_str(nvs, nkey, dyn, &required) == ESP_OK) {
                            cJSON *reg_json = cJSON_Parse(dyn);
                            if (reg_json) cJSON_AddItemToObject(root, key, reg_json);
                        }
                        free(dyn);
                    }
                }
                nvs_close(nvs);
                continue;
            }
        }

        // not found or error
        cJSON_AddNullToObject(root, key);
    }

    char *out = cJSON_PrintUnformatted(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, strlen(out));

    free(out);
    cJSON_Delete(root);
    return ESP_OK;
}
esp_err_t api_delete_register(httpd_req_t *req)
{
    // Try to parse id from URI path: /api/register/{id}
    const char *uri = req->uri; // e.g. "/api/register/3"
    int id = 0;
    if (uri) {
        const char *last = strrchr(uri, '/');
        if (last && *(last + 1) != '\0') {
            id = atoi(last + 1);
        }
    }

    if (id <= 0) {
        // Fallback: try query string (old behavior)
        char query[16] = {0};
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
            id = atoi(query);
        }
    }

    if (id <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid id");
        return ESP_FAIL;
    }

    delete_register_from_nvs(id);

    ESP_LOGI(TAG, "api_delete_register: deleted id=%d", id);

    httpd_resp_sendstr(req, "Deleted");
    return ESP_OK;
}
httpd_uri_t post_register_uri = {
    .uri = "/api/register",
    .method = HTTP_POST,
    .handler = api_post_register
};

httpd_uri_t get_registers_uri = {
    .uri = "/api/registers",
    .method = HTTP_GET,
    .handler = api_get_registers
};

httpd_uri_t delete_register_uri = {
    .uri = "/api/register/*",
    .method = HTTP_DELETE,
    .handler = api_delete_register
};

void register_registers_endpoints(httpd_handle_t server)
{
    ESP_LOGI(TAG, "register_registers_endpoints: registering /api/register endpoints");
    httpd_uri_t post_register_uri = {
        .uri = "/api/register",
        .method = HTTP_POST,
        .handler = api_post_register
    };

    httpd_uri_t get_registers_uri = {
        .uri = "/api/registers",
        .method = HTTP_GET,
        .handler = api_get_registers
    };

    httpd_uri_t delete_register_uri = {
        .uri = "/api/register/*",
        .method = HTTP_DELETE,
        .handler = api_delete_register
    };

    httpd_register_uri_handler(server, &post_register_uri);
    httpd_register_uri_handler(server, &get_registers_uri);
    httpd_register_uri_handler(server, &delete_register_uri);
}
