#include "wifi.h"
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"

#define WIFI_MAX_RETRY  CONFIG_ESP_MAXIMUM_RETRY
#define WIFI_TRY_MAX_RETRY 3  // attempts for a network picked in the setup menu

// After WIFI_MAX_RETRY quick retries, keep trying at this slower interval.
#define WIFI_SLOW_RETRY_US (30 * 1000 * 1000)

static const char *TAG = "aqm-wifi";

typedef enum {
    MODE_NORMAL,  // stay connected to the saved network
    MODE_PAUSED,  // setup open: don't reconnect
    MODE_TRYING,  // trying the network picked in setup
} wifi_mode_state_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;  // guards *s_wifi and the scan results
static wifi_t *s_wifi = NULL;
static wifi_mode_state_t s_mode = MODE_NORMAL;
static int s_retry_num = 0;
static esp_timer_handle_t s_retry_timer = NULL;

// Network to go back to when setup ends without a new connection.
static char s_saved_ssid[WIFI_SSID_MAX_LEN + 1];
static char s_saved_pass[WIFI_PASS_MAX_LEN + 1];

static bool s_scanning = false;
static wifi_ap_t s_scan[WIFI_SCAN_MAX];
static int s_scan_count = 0;
static wifi_ap_record_t s_scan_records[WIFI_SCAN_MAX];  // only touched in the event task

static void set_status(wifi_status_t status)
{
    taskENTER_CRITICAL(&s_lock);
    s_wifi->status = status;
    taskEXIT_CRITICAL(&s_lock);
}

// Points the station at ssid/pass and records them as the network in use.
static esp_err_t apply_config(const char *ssid, const char *pass)
{
    wifi_config_t cfg = {
        .sta = {
            .threshold.authmode = s_wifi->auth_mode,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));

    taskENTER_CRITICAL(&s_lock);
    strlcpy(s_wifi->ssid, ssid, sizeof(s_wifi->ssid));
    strlcpy(s_wifi->pass, pass, sizeof(s_wifi->pass));
    taskEXIT_CRITICAL(&s_lock);

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_ERR_WIFI_STATE) {
        // Still connecting: stop that attempt and try again.
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));
        err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
    }
    return err;
}

static void connect_now(void)
{
    set_status(WIFI_STATUS_CONNECTING);
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect: %s", esp_err_to_name(err));
    }
}

static void retry_timer_cb(void *arg)
{
    if (s_mode == MODE_NORMAL && !s_wifi->connected) {
        ESP_LOGI(TAG, "Retrying Wi-Fi connection");
        connect_now();
    }
}

static int compare_rssi(const void *a, const void *b)
{
    return ((const wifi_ap_t *)b)->rssi - ((const wifi_ap_t *)a)->rssi;
}

// Keeps the strongest entry per SSID, drops hidden networks, sorts by signal.
static void collect_scan_results(void)
{
    uint16_t n = WIFI_SCAN_MAX;
    if (esp_wifi_scan_get_ap_records(&n, s_scan_records) != ESP_OK) {
        n = 0;
    }

    wifi_ap_t found[WIFI_SCAN_MAX];
    int count = 0;
    for (int i = 0; i < n; i++) {
        const wifi_ap_record_t *r = &s_scan_records[i];
        if (r->ssid[0] == '\0') continue;
        int j = 0;
        while (j < count && strcmp(found[j].ssid, (const char *)r->ssid) != 0) j++;
        if (j == count) {
            strlcpy(found[count].ssid, (const char *)r->ssid, sizeof(found[count].ssid));
            found[count].rssi = r->rssi;
            found[count].secure = r->authmode != WIFI_AUTH_OPEN;
            count++;
        } else if (r->rssi > found[j].rssi) {
            found[j].rssi = r->rssi;
        }
    }
    qsort(found, count, sizeof(found[0]), compare_rssi);

    taskENTER_CRITICAL(&s_lock);
    memcpy(s_scan, found, sizeof(found[0]) * count);
    s_scan_count = count;
    s_scanning = false;
    taskEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "Scan found %d networks", count);
}

static void on_disconnected(const wifi_event_sta_disconnected_t *ev)
{
    taskENTER_CRITICAL(&s_lock);
    s_wifi->connected = false;
    s_wifi->last_reason = ev->reason;
    taskEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "Disconnected from %s (reason %d)", s_wifi->ssid, ev->reason);

    switch (s_mode) {
    case MODE_PAUSED:
        if (s_wifi->status != WIFI_STATUS_FAILED) set_status(WIFI_STATUS_PAUSED);
        break;

    case MODE_TRYING:
        if (s_retry_num < WIFI_TRY_MAX_RETRY) {
            s_retry_num++;
            connect_now();
        } else {
            ESP_LOGW(TAG, "Couldn't connect to %s", s_wifi->ssid);
            s_mode = MODE_PAUSED;
            set_status(WIFI_STATUS_FAILED);
        }
        break;

    case MODE_NORMAL:
        if (s_wifi->ssid[0] == '\0') {
            set_status(WIFI_STATUS_NOT_CONFIGURED);
        } else if (s_retry_num < WIFI_MAX_RETRY) {
            // Never give up: retry quickly a few times, then every WIFI_SLOW_RETRY_US.
            s_retry_num++;
            ESP_LOGI(TAG, "Retry %d of %d", s_retry_num, WIFI_MAX_RETRY);
            connect_now();
        } else {
            ESP_LOGW(TAG, "Wi-Fi unavailable, retrying in %d s", WIFI_SLOW_RETRY_US / 1000000);
            set_status(WIFI_STATUS_RETRY_WAIT);
            esp_timer_stop(s_retry_timer);  // no-op if it isn't running
            esp_timer_start_once(s_retry_timer, WIFI_SLOW_RETRY_US);
        }
        break;
    }
}

static void on_got_ip(const ip_event_got_ip_t *ev)
{
    ESP_LOGI(TAG, "Connected to %s, IP " IPSTR, s_wifi->ssid, IP2STR(&ev->ip_info.ip));
    taskENTER_CRITICAL(&s_lock);
    s_wifi->ip = ev->ip_info;
    snprintf(s_wifi->ip_str, sizeof(s_wifi->ip_str), IPSTR, IP2STR(&ev->ip_info.ip));
    s_wifi->connected = true;
    s_wifi->status = WIFI_STATUS_CONNECTED;
    taskEXIT_CRITICAL(&s_lock);
    s_retry_num = 0;

    if (s_mode == MODE_TRYING) {
        // The network picked in setup works: remember it.
        strlcpy(s_saved_ssid, s_wifi->ssid, sizeof(s_saved_ssid));
        strlcpy(s_saved_pass, s_wifi->pass, sizeof(s_saved_pass));
        esp_err_t err = settings_set_wifi(s_saved_ssid, s_saved_pass);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Couldn't save Wi-Fi settings: %s", esp_err_to_name(err));
        }
        s_mode = MODE_NORMAL;
    }
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_wifi->ssid[0] != '\0') {
            connect_now();
        } else {
            set_status(WIFI_STATUS_NOT_CONFIGURED);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        on_disconnected((const wifi_event_sta_disconnected_t *)event_data);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        collect_scan_results();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        on_got_ip((const ip_event_got_ip_t *)event_data);
    }
}

static void wifi_init_sta(void)
{
    const esp_timer_create_args_t retry_args = {
        .callback = retry_timer_cb,
        .name = "wifi_retry",
    };
    ESP_ERROR_CHECK(esp_timer_create(&retry_args, &s_retry_timer));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    // settings.c stores the network; don't keep a second copy in the driver's flash.
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    // Handlers stay registered so the state stays current and dropped
    // connections are retried.
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(apply_config(s_saved_ssid, s_saved_pass));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Don't wait for the connection: the app keeps running without Wi-Fi.
    if (s_saved_ssid[0] != '\0') {
        ESP_LOGI(TAG, "Connecting to %s in the background", s_saved_ssid);
    } else {
        ESP_LOGI(TAG, "No Wi-Fi network configured");
    }
}

wifi_t* wifi_init(const char *ssid, const char *pass, wifi_auth_mode_t auth_mode)
{
    if (s_wifi != NULL) {
        return s_wifi;
    }
    wifi_t *wifi = calloc(1, sizeof(wifi_t));
    if (wifi == NULL) {
        return NULL;
    }
    wifi->auth_mode = auth_mode;
    wifi->status = WIFI_STATUS_NOT_CONFIGURED;
    strlcpy(s_saved_ssid, ssid ? ssid : "", sizeof(s_saved_ssid));
    strlcpy(s_saved_pass, pass ? pass : "", sizeof(s_saved_pass));
    s_wifi = wifi;
    wifi_init_sta();
    return wifi;
}

void wifi_free(wifi_t *wifi)
{
    // The driver keeps using the state after init, so it's never freed.
    (void)wifi;
}

bool wifi_get_snapshot(wifi_t *out)
{
    if (s_wifi == NULL) {
        return false;
    }
    taskENTER_CRITICAL(&s_lock);
    *out = *s_wifi;
    taskEXIT_CRITICAL(&s_lock);
    return true;
}

void wifi_setup_begin(void)
{
    if (s_wifi == NULL) return;
    s_mode = MODE_PAUSED;
    esp_timer_stop(s_retry_timer);
    if (!s_wifi->connected) {
        // A connection attempt in progress would block scanning.
        esp_wifi_disconnect();
        set_status(s_wifi->ssid[0] ? WIFI_STATUS_PAUSED : WIFI_STATUS_NOT_CONFIGURED);
    }
}

void wifi_setup_end(void)
{
    if (s_wifi == NULL || s_mode == MODE_NORMAL) return;

    if (s_mode == MODE_TRYING) {
        // Setup closed mid-attempt: abandon it.
        s_mode = MODE_PAUSED;
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    bool on_saved = s_wifi->connected && strcmp(s_wifi->ssid, s_saved_ssid) == 0;
    s_mode = MODE_NORMAL;
    s_retry_num = 0;
    if (on_saved) return;

    if (s_wifi->connected) esp_wifi_disconnect();
    apply_config(s_saved_ssid, s_saved_pass);
    if (s_saved_ssid[0] != '\0') {
        connect_now();
    } else {
        set_status(WIFI_STATUS_NOT_CONFIGURED);
    }
}

esp_err_t wifi_scan_start(void)
{
    if (s_wifi == NULL) return ESP_ERR_INVALID_STATE;
    if (s_scanning) return ESP_OK;
    esp_err_t err = esp_wifi_scan_start(NULL, false);
    if (err == ESP_OK) {
        taskENTER_CRITICAL(&s_lock);
        s_scanning = true;
        s_scan_count = 0;
        taskEXIT_CRITICAL(&s_lock);
    } else {
        ESP_LOGW(TAG, "Scan didn't start: %s", esp_err_to_name(err));
    }
    return err;
}

bool wifi_scan_running(void)
{
    return s_scanning;
}

int wifi_scan_results(wifi_ap_t *out, int max)
{
    taskENTER_CRITICAL(&s_lock);
    int n = s_scan_count < max ? s_scan_count : max;
    memcpy(out, s_scan, sizeof(s_scan[0]) * n);
    taskEXIT_CRITICAL(&s_lock);
    return n;
}

void wifi_try_connect(const char *ssid, const char *pass)
{
    if (s_wifi == NULL) return;
    ESP_LOGI(TAG, "Trying %s", ssid);
    esp_timer_stop(s_retry_timer);
    s_retry_num = 0;

    bool was_connected = s_wifi->connected;
    s_mode = MODE_TRYING;
    if (apply_config(ssid, pass) != ESP_OK) {
        s_mode = MODE_PAUSED;
        set_status(WIFI_STATUS_FAILED);
        return;
    }
    set_status(WIFI_STATUS_CONNECTING);
    if (was_connected) {
        // The disconnect event starts the new attempt.
        esp_wifi_disconnect();
    } else {
        connect_now();
    }
}
