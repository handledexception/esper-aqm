#include "settings.h"

#include "nvs.h"

#define SETTINGS_NAMESPACE "esper-aqm"
#define KEY_WIFI_SSID      "wifi_ssid"
#define KEY_WIFI_PASS      "wifi_pass"
#define KEY_FAHRENHEIT     "temp_f"
#define KEY_TEMP_SOURCE    "temp_src"

esp_err_t settings_get_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    err = nvs_get_str(h, KEY_WIFI_SSID, ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(h, KEY_WIFI_PASS, pass, &pass_len);
    }
    nvs_close(h);
    return err;
}

esp_err_t settings_set_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, KEY_WIFI_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_WIFI_PASS, pass);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

#define DEFAULT_FAHRENHEIT true

// -1 = not loaded yet, otherwise 0 (Celsius) or 1 (Fahrenheit).
static volatile int s_fahrenheit = -1;

bool settings_get_fahrenheit(void)
{
    if (s_fahrenheit < 0) {
        bool value = DEFAULT_FAHRENHEIT;
        nvs_handle_t h;
        if (nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
            uint8_t stored;
            if (nvs_get_u8(h, KEY_FAHRENHEIT, &stored) == ESP_OK) value = stored != 0;
            nvs_close(h);
        }
        s_fahrenheit = value ? 1 : 0;
    }
    return s_fahrenheit != 0;
}

esp_err_t settings_set_fahrenheit(bool fahrenheit)
{
    s_fahrenheit = fahrenheit ? 1 : 0;

    nvs_handle_t h;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, KEY_FAHRENHEIT, fahrenheit ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

float settings_temp_from_c(float celsius)
{
    return settings_get_fahrenheit() ? celsius * 1.8f + 32.0f : celsius;
}

const char *settings_temp_unit(void)
{
    return settings_get_fahrenheit() ? "F" : "C";
}

// -1 = not loaded yet, otherwise a temp_source_t.
static volatile int s_temp_source = -1;

temp_source_t settings_get_temp_source(void)
{
    if (s_temp_source < 0) {
        int value = TEMP_SOURCE_SEN5X;  // default until changed in the menu
        nvs_handle_t h;
        if (nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
            uint8_t stored;
            if (nvs_get_u8(h, KEY_TEMP_SOURCE, &stored) == ESP_OK && stored < TEMP_SOURCE_COUNT) {
                value = stored;
            }
            nvs_close(h);
        }
        s_temp_source = value;
    }
    return (temp_source_t)s_temp_source;
}

esp_err_t settings_set_temp_source(temp_source_t source)
{
    if (source >= TEMP_SOURCE_COUNT) return ESP_ERR_INVALID_ARG;
    s_temp_source = source;

    nvs_handle_t h;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, KEY_TEMP_SOURCE, (uint8_t)source);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
