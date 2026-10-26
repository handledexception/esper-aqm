#pragma once

// Sensor dashboard on the T-Display-S3's built-in 320x170 TFT.
//
// The main loop calls display_update() after each sensor read. A background
// task redraws the screen from the latest copy.
//
// KEY button (GPIO14): switch temperature between Fahrenheit and Celsius.
// The choice is saved and applies everywhere (see settings_get_fahrenheit).

#include "sensor_data.h"

#include "esp_err.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    struct sensor_data sensors;
    bool sen5x_ok;        // the last SEN5x read succeeded
    bool has_mcp9808;     // show the MCP9808 temperature instead of the SEN5x one
    int aqi;              // EPA AQI, or -1 while there's no reading yet
} display_state_t;

// Initializes the TFT and the KEY button and starts the display task.
esp_err_t display_start(void);

// Hands the display the latest readings. Safe to call from any task.
void display_update(const display_state_t *state);

#ifdef __cplusplus
}
#endif
