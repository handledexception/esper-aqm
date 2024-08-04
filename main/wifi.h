#pragma once

#include "esp_event.h"
#include "esp_netif_types.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wifi_s {
    char ssid[32];      // max ssid length is 32 chars
    char pass[64];      // max pw length is 16 chars for WEP, 63 chars for WPA2
    char ip_str[16];
    wifi_auth_mode_t auth_mode;
    esp_netif_ip_info_t ip;
    bool connected;
} wifi_t;

// esp32 stuff
void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
void wifi_init_sta(wifi_t *wifi);
void wifi_scan(void);

// our stuff
wifi_t* wifi_init(const char *ssid, const char *pass, wifi_auth_mode_t auth_mode);
void wifi_free(wifi_t *wifi);

#ifdef __cplusplus
}
#endif
