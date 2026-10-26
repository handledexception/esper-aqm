#include "button.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "button"

#define BUTTON_TASK_PRIORITY 2
#define BUTTON_POLL_MS       10
#define BUTTON_MAX           8

typedef struct {
    button_t cfg;
    bool     raw_pressed;       // last raw reading
    uint32_t raw_changed_ms;    // when the raw reading last changed
    bool     pressed;           // debounced state
    uint32_t pressed_at_ms;     // when the debounced press began
    bool     long_fired;        // long press already reported for this hold
    uint32_t repeat_at_ms;      // when the last long press / repeat event fired
    bool     repeating;         // long press fired during this hold, so repeats may follow
} button_state_t;

static button_state_t s_buttons[BUTTON_MAX];
static volatile size_t s_button_count = 0;
static TaskHandle_t s_task = NULL;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool read_pressed(const button_t *b)
{
    int level = gpio_get_level(b->pin);
    return b->active_low ? (level == 0) : (level == 1);
}

static void emit(button_state_t *b, button_event_t ev)
{
    if (b->cfg.on_event) {
        b->cfg.on_event(b->cfg.pin, ev);
    }
}

static void button_task(void *arg)
{
    while (1) {
        uint32_t now = now_ms();
        size_t count = s_button_count;

        for (size_t i = 0; i < count; i++) {
            button_state_t *b = &s_buttons[i];
            bool raw = read_pressed(&b->cfg);

            if (raw != b->raw_pressed) {
                b->raw_pressed = raw;
                b->raw_changed_ms = now;
            } else if (raw != b->pressed && now - b->raw_changed_ms >= b->cfg.debounce_ms) {
                // Debounced edge.
                b->pressed = raw;
                if (raw) {
                    b->pressed_at_ms = now;
                    b->long_fired = false;
                    b->repeating = false;
                } else if (!b->long_fired) {
                    emit(b, BUTTON_EVENT_SHORT_PRESS);
                }
            }

            if (b->pressed && !b->long_fired && b->cfg.long_press_ms > 0 &&
                now - b->pressed_at_ms >= b->cfg.long_press_ms) {
                b->long_fired = true;
                b->repeating = true;
                b->repeat_at_ms = now;
                emit(b, BUTTON_EVENT_LONG_PRESS);
            } else if (b->pressed && b->repeating && b->cfg.repeat_ms > 0 &&
                       now - b->repeat_at_ms >= b->cfg.repeat_ms) {
                b->repeat_at_ms = now;
                emit(b, BUTTON_EVENT_REPEAT);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}

void button_driver_init(void)
{
    if (s_task) {
        ESP_LOGW(TAG, "button_driver_init already called");
        return;
    }
    xTaskCreate(button_task, "button_task", 3072, NULL, BUTTON_TASK_PRIORITY, &s_task);
}

esp_err_t button_register(const button_t *config)
{
    if (s_button_count >= BUTTON_MAX) return ESP_ERR_NO_MEM;

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << config->pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = config->active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = config->active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) return err;

    button_state_t *b = &s_buttons[s_button_count];
    b->cfg = *config;
    // Start from the current level so a button held at boot doesn't fire.
    b->raw_pressed = b->pressed = read_pressed(config);
    b->raw_changed_ms = now_ms();
    b->long_fired = true;
    s_button_count++;  // publish only after the entry is fully set up

    ESP_LOGI(TAG, "Registered button on GPIO %d", config->pin);
    return ESP_OK;
}
