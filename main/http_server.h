#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (10240)

struct sensor_data;
typedef struct system_s system_t;

typedef struct rest_server_context {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
    struct sensor_data* sensors;
    system_t* sys;
} rest_server_context_t;

esp_err_t http_server_start(const char* base_path, rest_server_context_t* rest_ctx);
esp_err_t http_server_stop();
esp_err_t http_get_handler(httpd_req_t* req);

#ifdef __cplusplus
}
#endif
