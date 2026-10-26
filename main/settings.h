#pragma once

// Settings kept in NVS flash across reboots. nvs_flash_init() must run first
// (system_init() does this).

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Wi-Fi network chosen in the setup menu. ESP_ERR_NVS_NOT_FOUND if none saved.
esp_err_t settings_get_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
esp_err_t settings_set_wifi(const char *ssid, const char *pass);

// Temperature unit used everywhere readings are shown (TFT, ASCII LCD, logs).
// Loaded from NVS on first use and cached, so it's cheap to call from any task.
// Defaults to Fahrenheit.
bool settings_get_fahrenheit(void);
// Updates the cached unit and saves it to NVS. Don't call from an ISR.
esp_err_t settings_set_fahrenheit(bool fahrenheit);

// Converts a Celsius reading to the current unit.
float settings_temp_from_c(float celsius);
// "F" or "C" for the current unit.
const char *settings_temp_unit(void);

// Which sensor's temperature the TFT shows. Cached like the unit.
// Defaults to TEMP_SOURCE_SEN5X.
typedef enum {
    TEMP_SOURCE_AUTO,     // MCP9808 if it's connected, otherwise the SEN5x
    TEMP_SOURCE_MCP9808,
    TEMP_SOURCE_SEN5X,
    TEMP_SOURCE_COUNT
} temp_source_t;

temp_source_t settings_get_temp_source(void);
esp_err_t settings_set_temp_source(temp_source_t source);

#ifdef __cplusplus
}
#endif
