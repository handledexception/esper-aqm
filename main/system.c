#include "system.h"
#include "wifi.h"
#include "wifi_credential.h"
#include "settings.h"

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_flash.h"
#include "esp_system.h"

#include <i2cdev.h>
#include <string.h>
#include <inttypes.h>

static const char *TAG = "aqm-system";
#define CHECK_ARG(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)

esp_err_t system_get_info(system_t* sys)
{
    ESP_LOGD(TAG, "system_get_info");
    CHECK_ARG(sys);
    esp_chip_info(&sys->chip_info);
    sys->ver_maj = sys->chip_info.revision / 100;
    sys->ver_min = sys->chip_info.revision % 100;
    sys->min_free_heap_size = esp_get_minimum_free_heap_size();
    sys->idf_ver_str = esp_get_idf_version();
    ESP_ERROR_CHECK(esp_flash_get_size(NULL, &sys->flash_size));
    return ESP_OK;
}

esp_err_t system_print_info(system_t* sys)
{
    CHECK_ARG(sys);
    printf("IDF Version: %s\nChip: %s (Rev: %" PRIu32 ".%" PRIu32 ")\nCores: %d\n%s Flash: %" PRIu32 "MB\nHeap Free: %" PRIi32 "\nWiFi: %s\nBluetooth: %s\nIEEE 802.15.4: %s\nEmbedded PSRAM: %s\n",
            sys->idf_ver_str,
            CONFIG_IDF_TARGET,
            sys->ver_maj, sys->ver_min,
            sys->chip_info.cores,
            (sys->chip_info.features & CHIP_FEATURE_EMB_FLASH ? "Embedded" : "External"),
            sys->flash_size / (1024 * 1024),
            sys->min_free_heap_size,
            (sys->chip_info.features & CHIP_FEATURE_WIFI_BGN ? "Y" : ""),
            (sys->chip_info.features & CHIP_FEATURE_BLE ? "LE" : (sys->chip_info.features & CHIP_FEATURE_BT ? "Classic" : "")),
            (sys->chip_info.features & CHIP_FEATURE_IEEE802154 ? "Y" : "N"),
            (sys->chip_info.features & CHIP_FEATURE_EMB_PSRAM ? "Y" : "N"));
    return ESP_OK;
}

system_t* system_init(void)
{
    ESP_LOGD(TAG, "system_init");

    system_t *sys = malloc(sizeof(system_t));
    memset(sys, 0, sizeof(system_t));

    sys->power_on_time = esp_timer_get_time();

    // initialize NVS flash
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }

    if (err == ESP_OK) {
        err = i2cdev_init();
    }

    if (err == ESP_OK) {
        ESP_LOGD(TAG, "system_init OK");
    } else {
        ESP_LOGE(TAG, "system_init FAIL");
        free(sys);
        sys = NULL;
    }

    return sys;
}

esp_err_t system_wifi_init(system_t* sys)
{
    // A network picked in the on-screen setup wins over the menuconfig one.
    char ssid[WIFI_SSID_MAX_LEN + 1] = "";
    char pass[WIFI_PASS_MAX_LEN + 1] = "";
    if (settings_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass)) == ESP_OK) {
        ESP_LOGI(TAG, "Using saved Wi-Fi network %s", ssid);
    } else {
        strlcpy(ssid, WIFI_SSID, sizeof(ssid));
        strlcpy(pass, WIFI_PASS, sizeof(pass));
    }

    // Returns right away; Wi-Fi connects (and reconnects) in the background.
    // wifi_get_snapshot() reports the connection state and IP.
    sys->wifi = wifi_init(ssid, pass, WIFI_AUTH_OPEN);
    if (sys->wifi == NULL) {
        ESP_LOGE(TAG, "Error initializing WiFi!");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void system_shutdown(system_t* sys)
{
    ESP_LOGD(TAG, "system_shutdown");
    if (sys != NULL) {
        free(sys);
        sys = NULL;
    }
}

int64_t system_get_uptime(system_t* sys)
{
    CHECK_ARG(sys);
    return esp_timer_get_time() - sys->power_on_time;
}
