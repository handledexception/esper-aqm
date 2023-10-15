#include "utils.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#define NOP() asm volatile ("nop")

void sleep_usec(unsigned int usec)
{
    uint64_t m = (uint64_t)esp_timer_get_time();
    if (usec) {
        uint64_t e = (m + usec);
        if (m > e) { //overflow
            while ((uint64_t)esp_timer_get_time() > e) {
                NOP();
            }
        }
        while ((uint64_t)esp_timer_get_time() < e) {
            NOP();
        }
    }
}

void sleep_msec(unsigned int msec)
{
    vTaskDelay(msec / portTICK_PERIOD_MS);
}
