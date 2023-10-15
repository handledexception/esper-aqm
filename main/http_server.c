#include "http_server.h"
#include "sensor_data.h"
#include "system.h"

#include "esp_log.h"
#include "esp_system.h"

#include "cJSON.h"

#include <string.h>
#include <fcntl.h>

static const char* TAG = "aqm-http-server";

#define USEC_TO_SEC(usec) (double)usec / 1000000.0
#define REST_CHECK(a, str, goto_tag, ...)                                              \
    do                                                                                 \
    {                                                                                  \
        if (!(a))                                                                      \
        {                                                                              \
            ESP_LOGE(TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            goto goto_tag;                                                             \
        }                                                                              \
    } while (0)

static const char* get_cpu_model_string(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32:
        return "ESP32";
    case CHIP_ESP32S2:
        return "ESP32-S2";
    case CHIP_ESP32S3:
        return "ESP32-S3";
    case CHIP_ESP32C3:
        return "ESP32-C3";
    case CHIP_ESP32H2:
        return "ESP32-H2";
    }
    return "";
}

static esp_err_t get_system_info_handler(httpd_req_t* req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON* root = cJSON_CreateObject();
    rest_server_context_t* rest_server = (rest_server_context_t*)req->user_ctx;
    if (rest_server != NULL && rest_server->sys != NULL) {
        cJSON_AddNumberToObject(root, "power_on_time", rest_server->sys->power_on_time);
        cJSON_AddNumberToObject(root, "uptime", USEC_TO_SEC(system_get_uptime(rest_server->sys)));
        cJSON_AddNumberToObject(root, "flash_size", rest_server->sys->flash_size);
        cJSON_AddNumberToObject(root, "ver_maj", rest_server->sys->ver_maj);
        cJSON_AddNumberToObject(root, "ver_min", rest_server->sys->ver_min);
        cJSON_AddNumberToObject(root, "min_free_heap_size", rest_server->sys->min_free_heap_size);
        cJSON_AddStringToObject(root, "idf_version", rest_server->sys->idf_ver_str);
        cJSON_AddNumberToObject(root, "num_cpu_cores", rest_server->sys->chip_info.cores);
        cJSON_AddNumberToObject(root, "cpu_revision", rest_server->sys->chip_info.revision);
        cJSON_AddNumberToObject(root, "cpu_full_revision", rest_server->sys->chip_info.full_revision);
        cJSON_AddStringToObject(root, "cpu_model", get_cpu_model_string(rest_server->sys->chip_info.model));
    }
    const char* json = cJSON_Print(root);
    httpd_resp_sendstr(req, json);
    free((void*)json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t get_sensor_data_handler(httpd_req_t* req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON* root = cJSON_CreateObject();
    rest_server_context_t* rest_server = (rest_server_context_t*)req->user_ctx;
    if (rest_server != NULL && rest_server->sensors != NULL) {
        cJSON_AddNumberToObject(root, "temperature_mcp9808", rest_server->sensors->temperature_mcp9808);
        cJSON_AddNumberToObject(root, "mass_concentration_pm1p0", rest_server->sensors->mass_concentration_pm1p0);
        cJSON_AddNumberToObject(root, "mass_concentration_pm2p5", rest_server->sensors->mass_concentration_pm2p5);
        cJSON_AddNumberToObject(root, "mass_concentration_pm4p0", rest_server->sensors->mass_concentration_pm4p0);
        cJSON_AddNumberToObject(root, "mass_concentration_pm10p0", rest_server->sensors->mass_concentration_pm10p0);
        cJSON_AddNumberToObject(root, "ambient_humidity", rest_server->sensors->ambient_humidity);
        cJSON_AddNumberToObject(root, "ambient_temperature", rest_server->sensors->ambient_temperature);
        cJSON_AddNumberToObject(root, "voc_index", rest_server->sensors->voc_index);
        cJSON_AddNumberToObject(root, "nox_index", rest_server->sensors->nox_index);
    }
    const char* json = cJSON_Print(root);
    httpd_resp_sendstr(req, json);
    free((void*)json);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t http_server_start(const char* base_path, rest_server_context_t* rest_ctx)
{
    REST_CHECK(rest_ctx, "REST context is NULL", err);
    REST_CHECK(base_path, "Invalid http server base path", err);
    strlcpy(rest_ctx->base_path, base_path, sizeof(rest_ctx->base_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP server...");
    REST_CHECK(httpd_start(&server, &config) == ESP_OK, "Error starting HTTP server", err);

    httpd_uri_t get_sensor_data_uri = {
        .uri = "/api/v1/sensor",
        .method = HTTP_GET,
        .handler = get_sensor_data_handler,
        .user_ctx = rest_ctx
    };
    httpd_register_uri_handler(server, &get_sensor_data_uri);

    httpd_uri_t get_system_info_uri = {
        .uri = "/api/v1/system",
        .method = HTTP_GET,
        .handler = get_system_info_handler,
        .user_ctx = rest_ctx
    };
    httpd_register_uri_handler(server, &get_system_info_uri);

    return ESP_OK;

err:
    return ESP_FAIL;
}

esp_err_t http_server_stop()
{
    return ESP_OK;
}

esp_err_t http_get_handler(httpd_req_t* req)
{
    return ESP_OK;
}