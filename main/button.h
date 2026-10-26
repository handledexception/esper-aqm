#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUTTON_EVENT_SHORT_PRESS,  // pressed and released before long_press_ms
    BUTTON_EVENT_LONG_PRESS,   // held for long_press_ms (fires once, while still held)
    BUTTON_EVENT_REPEAT,       // still held: every repeat_ms after the long press
} button_event_t;

typedef void (*button_callback_t)(gpio_num_t pin, button_event_t event);

/**
 * Per-button configuration.
 */
typedef struct {
    gpio_num_t pin;
    bool active_low;           // true = pressed when low (typical)
    uint32_t debounce_ms;      // input must be stable this long to count (e.g. 30)
    uint32_t long_press_ms;    // hold time for BUTTON_EVENT_LONG_PRESS; 0 disables long press
    uint32_t repeat_ms;        // BUTTON_EVENT_REPEAT interval while held; 0 disables repeat
    button_callback_t on_event;
} button_t;

/**
 * Initializes the button driver (only called once).
 * Starts a small task that polls registered buttons every 10 ms.
 * Callbacks run in that task, so keep them short (e.g. post to a queue).
 */
void button_driver_init(void);

/**
 * Register a button with a callback.
 */
esp_err_t button_register(const button_t *config);

#ifdef __cplusplus
}
#endif
