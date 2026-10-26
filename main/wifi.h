#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif_types.h"
#include "esp_wifi_types.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASS_MAX_LEN 63  // WPA2 passphrase limit
#define WIFI_SCAN_MAX     16  // networks kept from one scan

typedef enum {
    WIFI_STATUS_NOT_CONFIGURED,  // no network saved or set in menuconfig
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_RETRY_WAIT,      // network unavailable; waiting to try again
    WIFI_STATUS_PAUSED,          // Wi-Fi setup is open, not reconnecting
    WIFI_STATUS_FAILED,          // a wifi_try_connect() attempt gave up
} wifi_status_t;

typedef struct wifi_s {
    char ssid[WIFI_SSID_MAX_LEN + 1];  // network in use (or being tried)
    char pass[WIFI_PASS_MAX_LEN + 1];
    char ip_str[16];
    wifi_auth_mode_t auth_mode;        // weakest security accepted
    esp_netif_ip_info_t ip;
    bool connected;
    wifi_status_t status;
    uint8_t last_reason;               // wifi_err_reason_t of the last disconnect
} wifi_t;

typedef struct {
    char ssid[WIFI_SSID_MAX_LEN + 1];
    int8_t rssi;   // dBm
    bool secure;   // needs a password
} wifi_ap_t;

// Starts Wi-Fi and connects to ssid in the background (and keeps reconnecting).
// An empty ssid starts Wi-Fi without connecting, so the setup menu can scan.
wifi_t* wifi_init(const char *ssid, const char *pass, wifi_auth_mode_t auth_mode);
void wifi_free(wifi_t *wifi);

// Copies the current state. Safe from any task. Returns false before wifi_init().
bool wifi_get_snapshot(wifi_t *out);

// Wi-Fi setup (used by the on-screen menu):
//   wifi_setup_begin()  stop reconnecting so scans can run
//   wifi_scan_start()   start a scan; results arrive in the background
//   wifi_try_connect()  try a network; saved to flash only if it connects
//   wifi_setup_end()    go back to the saved network if nothing new connected
void wifi_setup_begin(void);
void wifi_setup_end(void);
esp_err_t wifi_scan_start(void);
bool wifi_scan_running(void);
int wifi_scan_results(wifi_ap_t *out, int max);  // strongest first, one entry per SSID
void wifi_try_connect(const char *ssid, const char *pass);

#ifdef __cplusplus
}
#endif
