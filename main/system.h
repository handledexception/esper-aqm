#pragma once

#include "esp_err.h"
#include "esp_chip_info.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wifi_s wifi_t;

typedef struct system_s {
    int64_t power_on_time;
    uint32_t flash_size;
    uint32_t ver_maj;
    uint32_t ver_min;
    int32_t min_free_heap_size;
    esp_chip_info_t chip_info;
    const char* idf_ver_str;
    wifi_t *wifi;
} system_t;

system_t* system_init(void);
esp_err_t system_wifi_init(system_t* sys);
void system_shutdown(system_t* sys);
esp_err_t system_get_info(system_t* sys);
esp_err_t system_print_info(system_t* sys);
int64_t system_get_uptime(system_t* sys);

#ifdef __cplusplus
}
#endif
