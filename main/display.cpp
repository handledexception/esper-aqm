#include "display.h"
#include "button.h"
#include "settings.h"
#include "wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <TFT_eSPI.h>
#include <stdio.h>
#include <string.h>

#define TAG "aqm-display"

#define DISPLAY_TASK_CORE     1
#define DISPLAY_TASK_PRIORITY 2
#define DISPLAY_TASK_STACK    6144
#define DISPLAY_REFRESH_MS    100  // also how long a button press waits to be drawn, at most
#define UI_EVENT_QUEUE_LEN    16

// T-Display-S3 board pins.
#define PIN_LCD_BACKLIGHT GPIO_NUM_38
#define PIN_LCD_POWER     GPIO_NUM_15  // powers the LCD when running from battery
#define PIN_KEY_BUTTON    GPIO_NUM_14
#define PIN_BOOT_BUTTON   GPIO_NUM_0

#define BUTTON_DEBOUNCE_MS   30
#define BUTTON_LONG_PRESS_MS 600
#define BUTTON_REPEAT_MS     120  // held button: next character / delete this often

#define MENU_TIMEOUT_US      (60LL * 1000 * 1000)      // menu and network list close when idle
#define PASSWORD_TIMEOUT_US  (5LL * 60 * 1000 * 1000)
#define CONNECTED_RETURN_US  (4LL * 1000 * 1000)       // show "Connected" this long
#define SCAN_RETRY_US        (500LL * 1000)
#define SCAN_MAX_TRIES       20

// Landscape (rotation 1) panel size.
#define SCREEN_W 320
#define SCREEN_H 170
#define HEADER_RULE_Y 31
#define FOOTER_RULE_Y (SCREEN_H - 24)

#define SEN5X_INDEX_INVALID 0x7fff  // VOC/NOx index while the sensor warms up

#define COLOR_BG      TFT_BLACK
#define COLOR_TEXT    TFT_WHITE
#define COLOR_DIM     0x7BEF  // mid grey
#define COLOR_FAINT   0x4208  // dark grey text
#define COLOR_RULE    0x2945  // dark grey
#define COLOR_TILE    0x18E3  // very dark grey
#define COLOR_SELECT  0x1A6F  // dark blue
#define COLOR_OK      0x07E0  // green
#define COLOR_WARN    0xFD20  // amber
#define COLOR_IDLE    0x4208  // grey

// EPA AQI band colors.
#define COLOR_AQI_GOOD      0x0720  // green   (0, 228, 0)
#define COLOR_AQI_MODERATE  0xFFE0  // yellow  (255, 255, 0)
#define COLOR_AQI_USG       0xFBE0  // orange  (255, 126, 0)
#define COLOR_AQI_UNHEALTHY 0xF800  // red     (255, 0, 0)
#define COLOR_AQI_VERY      0x89F2  // purple  (143, 63, 151)
#define COLOR_AQI_HAZARDOUS 0x7804  // maroon  (126, 0, 35)

typedef struct {
    int max;            // highest AQI in this band
    const char *name;
    uint16_t bg;
    uint16_t fg;
} aqi_band_t;

static const aqi_band_t k_aqi_bands[] = {
    {  50, "Good",           COLOR_AQI_GOOD,      TFT_BLACK },
    { 100, "Moderate",       COLOR_AQI_MODERATE,  TFT_BLACK },
    { 150, "Unhealthy (SG)", COLOR_AQI_USG,       TFT_BLACK },  // for sensitive groups
    { 200, "Unhealthy",      COLOR_AQI_UNHEALTHY, TFT_WHITE },
    { 300, "Very Unhealthy", COLOR_AQI_VERY,      TFT_WHITE },
    { INT32_MAX, "Hazardous", COLOR_AQI_HAZARDOUS, TFT_WHITE },
};

// ---------------------------------------------------------------------------
// UI state
// ---------------------------------------------------------------------------
//
// Buttons on every screen except the password entry:
//   BOOT short  next item (dashboard: open the menu)
//   KEY  short  select    (dashboard: switch F / C)
//   BOOT hold   back
// Password entry:
//   BOOT short  next character (hold to scroll fast)
//   KEY  short  type it, or run the selected action (Done, Cancel, ...)
//   KEY  hold   delete the last character (keep holding to delete more)

typedef enum { UI_BTN_KEY, UI_BTN_BOOT } ui_button_t;

typedef struct {
    ui_button_t    button;
    button_event_t event;
} ui_event_t;

typedef enum {
    SCREEN_DASHBOARD,
    SCREEN_MENU,
    SCREEN_SCAN,      // list of Wi-Fi networks
    SCREEN_PASSWORD,
    SCREEN_CONNECT,   // connecting / connected / failed
} screen_t;

typedef enum { MENU_WIFI, MENU_UNITS, MENU_TEMP_SOURCE, MENU_BACK, MENU_COUNT } menu_item_t;

typedef enum { CHARSET_LOWER, CHARSET_UPPER, CHARSET_DIGITS, CHARSET_SYMBOLS, CHARSET_COUNT } charset_t;

static const char *const k_charsets[CHARSET_COUNT] = {
    "abcdefghijklmnopqrstuvwxyz",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    "0123456789",
    " !@#$%^&*()-_=+[]{};:'\",.<>/?\\|`~",
};
static const char *const k_charset_names[CHARSET_COUNT] = { "abc", "ABC", "123", "#+=" };

// Actions that follow the characters in the picker ring.
typedef enum { PICK_NEXT_SET, PICK_DELETE, PICK_DONE, PICK_CANCEL, PICK_ACTION_COUNT } pick_action_t;

typedef struct {
    QueueHandle_t events;
    screen_t screen;
    int64_t last_input_us;
    int menu_sel;
    int menu_top;            // first visible menu row

    // Network list.
    wifi_ap_t aps[WIFI_SCAN_MAX];
    int ap_count;
    bool scan_pending;       // asked for a scan that hasn't started yet
    int scan_tries;
    int64_t scan_retry_at_us;
    bool was_scanning;
    int list_sel;
    int list_top;

    // Network being set up.
    char ssid[WIFI_SSID_MAX_LEN + 1];
    bool secure;
    char pass[WIFI_PASS_MAX_LEN + 1];
    int pass_len;
    int charset;
    int pick;                // position in the picker ring
    int64_t connected_at_us; // when the connect screen first saw success
} ui_t;

static ui_t s_ui;

static TFT_eSPI tft = TFT_eSPI();
static TFT_eSprite spr = TFT_eSprite(&tft);

// Latest sensor readings from the main loop.
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static display_state_t s_state;     // guarded by s_lock
static bool s_has_state = false;    // guarded by s_lock
static TaskHandle_t s_task = NULL;

static const aqi_band_t *aqi_band(int aqi)
{
    for (const aqi_band_t &b : k_aqi_bands) {
        if (aqi <= b.max) return &b;
    }
    return &k_aqi_bands[0];
}

static void on_button(gpio_num_t pin, button_event_t event)
{
    ui_event_t e = {
        .button = pin == PIN_KEY_BUTTON ? UI_BTN_KEY : UI_BTN_BOOT,
        .event = event,
    };
    xQueueSend(s_ui.events, &e, 0);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

static void toggle_temperature_unit(void)
{
    settings_set_fahrenheit(!settings_get_fahrenheit());
}

static void next_temperature_source(void)
{
    settings_set_temp_source((temp_source_t)((settings_get_temp_source() + 1) % TEMP_SOURCE_COUNT));
}

// Picks the temperature to show from the Temp sensor setting.
// Returns false if that sensor has no reading.
static bool resolve_temperature(const display_state_t *st, bool has_state, float *temp_c, const char **sensor)
{
    bool use_mcp;
    switch (settings_get_temp_source()) {
    case TEMP_SOURCE_MCP9808: use_mcp = true; break;
    case TEMP_SOURCE_SEN5X:   use_mcp = false; break;
    default:                  use_mcp = st->has_mcp9808; break;
    }
    *sensor = use_mcp ? "MCP9808" : "SEN5x";
    if (use_mcp) {
        *temp_c = st->sensors.temperature_mcp9808;
        return has_state && st->has_mcp9808;
    }
    *temp_c = st->sensors.ambient_temperature;
    return has_state && st->sen5x_ok;
}

static void start_scan(void)
{
    s_ui.ap_count = 0;
    s_ui.list_sel = 0;
    s_ui.list_top = 0;
    s_ui.scan_pending = true;
    s_ui.scan_tries = 0;
    s_ui.scan_retry_at_us = 0;
    s_ui.was_scanning = true;
}

static void open_wifi_setup(void)
{
    wifi_setup_begin();
    start_scan();
    s_ui.screen = SCREEN_SCAN;
}

// Leaves the Wi-Fi screens; Wi-Fi goes back to the saved network unless a new one connected.
static void close_wifi_setup(screen_t next)
{
    wifi_setup_end();
    s_ui.screen = next;
}

static void start_connect(void)
{
    wifi_try_connect(s_ui.ssid, s_ui.secure ? s_ui.pass : "");
    s_ui.connected_at_us = 0;
    s_ui.screen = SCREEN_CONNECT;
}

static void choose_network(const wifi_ap_t *ap)
{
    strlcpy(s_ui.ssid, ap->ssid, sizeof(s_ui.ssid));
    s_ui.secure = ap->secure;
    if (!ap->secure) {
        start_connect();
        return;
    }
    s_ui.pass[0] = '\0';
    s_ui.pass_len = 0;
    s_ui.charset = CHARSET_LOWER;
    s_ui.pick = 0;
    s_ui.screen = SCREEN_PASSWORD;
}

static int picker_count(void)
{
    return (int)strlen(k_charsets[s_ui.charset]) + PICK_ACTION_COUNT;
}

static void password_delete(void)
{
    if (s_ui.pass_len > 0) {
        s_ui.pass[--s_ui.pass_len] = '\0';
    }
}

static void picker_activate(void)
{
    const char *set = k_charsets[s_ui.charset];
    int n = (int)strlen(set);
    if (s_ui.pick < n) {
        if (s_ui.pass_len < WIFI_PASS_MAX_LEN) {
            s_ui.pass[s_ui.pass_len++] = set[s_ui.pick];
            s_ui.pass[s_ui.pass_len] = '\0';
        }
        return;
    }
    switch (s_ui.pick - n) {
    case PICK_NEXT_SET:
        s_ui.charset = (s_ui.charset + 1) % CHARSET_COUNT;
        s_ui.pick = 0;
        break;
    case PICK_DELETE:
        password_delete();
        break;
    case PICK_DONE:
        if (s_ui.pass_len > 0) start_connect();
        break;
    case PICK_CANCEL:
        s_ui.screen = SCREEN_SCAN;
        break;
    }
}

static bool connect_succeeded(const wifi_t *w)
{
    return w->status == WIFI_STATUS_CONNECTED && strcmp(w->ssid, s_ui.ssid) == 0;
}

static int scan_item_count(void)
{
    return s_ui.ap_count + 2;  // networks, "Scan again", "Back"
}

static void handle_event(const ui_event_t &e)
{
    s_ui.last_input_us = esp_timer_get_time();
    bool key = e.button == UI_BTN_KEY;
    bool is_short = e.event == BUTTON_EVENT_SHORT_PRESS;
    bool is_long = e.event == BUTTON_EVENT_LONG_PRESS;

    switch (s_ui.screen) {
    case SCREEN_DASHBOARD:
        if (key && is_short) {
            toggle_temperature_unit();
        } else if (!key && is_short) {
            s_ui.menu_sel = MENU_WIFI;
            s_ui.menu_top = 0;
            s_ui.screen = SCREEN_MENU;
        }
        break;

    case SCREEN_MENU:
        if (!key && is_short) {
            s_ui.menu_sel = (s_ui.menu_sel + 1) % MENU_COUNT;
        } else if (!key && is_long) {
            s_ui.screen = SCREEN_DASHBOARD;
        } else if (key && is_short) {
            switch (s_ui.menu_sel) {
            case MENU_WIFI:  open_wifi_setup(); break;
            case MENU_UNITS: toggle_temperature_unit(); break;
            case MENU_TEMP_SOURCE: next_temperature_source(); break;
            case MENU_BACK:  s_ui.screen = SCREEN_DASHBOARD; break;
            }
        }
        break;

    case SCREEN_SCAN: {
        bool scanning = s_ui.scan_pending || wifi_scan_running();
        if (!key && is_short) {
            s_ui.list_sel = (s_ui.list_sel + 1) % scan_item_count();
        } else if (!key && is_long) {
            close_wifi_setup(SCREEN_MENU);
        } else if (key && is_short) {
            if (s_ui.list_sel < s_ui.ap_count) {
                choose_network(&s_ui.aps[s_ui.list_sel]);
            } else if (s_ui.list_sel == s_ui.ap_count) {
                if (!scanning) start_scan();
            } else {
                close_wifi_setup(SCREEN_MENU);
            }
        }
        break;
    }

    case SCREEN_PASSWORD:
        if (!key) {
            // Any BOOT event (short, long, repeat) moves to the next character.
            s_ui.pick = (s_ui.pick + 1) % picker_count();
        } else if (is_short) {
            picker_activate();
        } else {
            password_delete();  // long press and repeats
        }
        break;

    case SCREEN_CONNECT: {
        wifi_t w;
        wifi_get_snapshot(&w);
        if (connect_succeeded(&w)) {
            if (is_short) s_ui.screen = SCREEN_DASHBOARD;
        } else if (w.status == WIFI_STATUS_FAILED) {
            if (key && is_short) {
                if (s_ui.secure) {
                    s_ui.screen = SCREEN_PASSWORD;  // keeps what was typed
                } else {
                    start_connect();
                }
            } else if (!key && is_short) {
                s_ui.screen = SCREEN_SCAN;
            } else if (!key && is_long) {
                close_wifi_setup(SCREEN_DASHBOARD);
            }
        } else if (!key && is_long) {
            close_wifi_setup(SCREEN_DASHBOARD);  // cancel the attempt
        }
        break;
    }
    }
}

// Timeouts, scan progress and other things that happen without a button press.
static void tick(void)
{
    int64_t now = esp_timer_get_time();
    int64_t idle = now - s_ui.last_input_us;

    switch (s_ui.screen) {
    case SCREEN_MENU:
        if (idle > MENU_TIMEOUT_US) s_ui.screen = SCREEN_DASHBOARD;
        break;

    case SCREEN_SCAN:
        if (idle > MENU_TIMEOUT_US) {
            close_wifi_setup(SCREEN_DASHBOARD);
            break;
        }
        if (s_ui.scan_pending && now >= s_ui.scan_retry_at_us) {
            // Starting a scan can fail briefly while a connection attempt winds down.
            if (wifi_scan_start() == ESP_OK || ++s_ui.scan_tries >= SCAN_MAX_TRIES) {
                s_ui.scan_pending = false;
            } else {
                s_ui.scan_retry_at_us = now + SCAN_RETRY_US;
            }
        }
        {
            bool scanning = s_ui.scan_pending || wifi_scan_running();
            if (s_ui.was_scanning && !scanning) {
                s_ui.ap_count = wifi_scan_results(s_ui.aps, WIFI_SCAN_MAX);
                s_ui.list_sel = 0;
                s_ui.list_top = 0;
            }
            s_ui.was_scanning = scanning;
        }
        break;

    case SCREEN_PASSWORD:
        if (idle > PASSWORD_TIMEOUT_US) close_wifi_setup(SCREEN_DASHBOARD);
        break;

    case SCREEN_CONNECT: {
        wifi_t w;
        wifi_get_snapshot(&w);
        if (connect_succeeded(&w)) {
            if (s_ui.connected_at_us == 0) {
                s_ui.connected_at_us = now;
            } else if (now - s_ui.connected_at_us > CONNECTED_RETURN_US) {
                s_ui.screen = SCREEN_DASHBOARD;
            }
        } else if (w.status == WIFI_STATUS_FAILED && idle > MENU_TIMEOUT_US) {
            // Nobody's there: go back to the saved network.
            close_wifi_setup(SCREEN_DASHBOARD);
        }
        break;
    }

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

// Draws text with the current datum, shortened with ".." to fit max_w pixels.
static void draw_text_fit(const char *text, int x, int y, int font, int max_w)
{
    if (spr.textWidth(text, font) <= max_w) {
        spr.drawString(text, x, y, font);
        return;
    }
    char buf[72];
    int len = (int)strlen(text);
    if (len > (int)sizeof(buf) - 3) len = sizeof(buf) - 3;
    while (len > 0) {
        memcpy(buf, text, len);
        strcpy(buf + len, "..");
        if (spr.textWidth(buf, font) <= max_w) break;
        len--;
    }
    spr.drawString(buf, x, y, font);
}

// Filled rounded label, vertically centered on cy. Returns its width.
static int draw_pill(int x, int cy, const char *text, uint16_t bg, uint16_t fg, bool right_align)
{
    const int h = 20;
    int w = spr.textWidth(text, 2) + 14;
    if (right_align) x -= w;
    spr.fillRoundRect(x, cy - h / 2, w, h, 6, bg);
    spr.setTextColor(fg);
    spr.setTextDatum(MC_DATUM);
    spr.drawString(text, x + w / 2, cy + 1, 2);
    return w;
}

static void draw_header(const char *title)
{
    spr.setTextColor(COLOR_TEXT);
    spr.setTextDatum(ML_DATUM);
    spr.drawString(title, 8, 16, 4);
    spr.drawFastHLine(0, HEADER_RULE_Y, SCREEN_W, COLOR_RULE);
}

static void draw_footer_hint(const char *hint)
{
    spr.drawFastHLine(0, FOOTER_RULE_Y, SCREEN_W, COLOR_RULE);
    spr.setTextColor(COLOR_DIM);
    spr.setTextDatum(MC_DATUM);
    spr.drawString(hint, SCREEN_W / 2, SCREEN_H - 11, 2);
}

static void draw_scroll_arrows(int top, int bottom, bool up, bool down)
{
    if (up) spr.fillTriangle(SCREEN_W - 7, top + 2, SCREEN_W - 11, top + 8, SCREEN_W - 3, top + 8, COLOR_DIM);
    if (down) spr.fillTriangle(SCREEN_W - 7, bottom - 2, SCREEN_W - 11, bottom - 8, SCREEN_W - 3, bottom - 8, COLOR_DIM);
}

// Keeps the selected row on screen.
static void scroll_to(int *top, int sel, int count, int visible)
{
    if (*top > count - visible) *top = count > visible ? count - visible : 0;
    if (sel < *top) *top = sel;
    else if (sel >= *top + visible) *top = sel - visible + 1;
}

static void draw_signal_bars(int x, int bottom, int rssi, uint16_t color)
{
    int level = rssi > -55 ? 4 : rssi > -67 ? 3 : rssi > -78 ? 2 : 1;
    for (int i = 0; i < 4; i++) {
        int h = 4 + i * 3;
        spr.fillRect(x + i * 5, bottom - h, 3, h, i < level ? color : COLOR_FAINT);
    }
}

static void draw_lock(int x, int y, uint16_t color)
{
    spr.drawRoundRect(x + 2, y, 6, 8, 2, color);  // shackle
    spr.fillRoundRect(x, y + 5, 10, 8, 1, color); // body
}

static const char *wifi_reason_text(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return "Check the password";
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "Network not found";
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_CONNECTION_FAIL:
        return "The network refused the connection";
    default:
        return NULL;
    }
}

// ---------------------------------------------------------------------------
// Dashboard
// ---------------------------------------------------------------------------

static void draw_aqi_panel(int x, int y, int w, int h, int aqi)
{
    const aqi_band_t *band = aqi >= 0 ? aqi_band(aqi) : NULL;
    uint16_t bg = band ? band->bg : COLOR_TILE;
    uint16_t fg = band ? band->fg : COLOR_DIM;
    int cx = x + w / 2;

    spr.fillRoundRect(x, y, w, h, 8, bg);
    spr.setTextColor(fg);
    spr.setTextDatum(MC_DATUM);
    spr.drawString("AQI", cx, y + 14, 2);

    char num[8];
    if (aqi >= 0) {
        snprintf(num, sizeof(num), "%d", aqi > 999 ? 999 : aqi);
    } else {
        snprintf(num, sizeof(num), "--");
    }
    spr.drawString(num, cx, y + h / 2 + 2, 6);
    spr.drawString(band ? band->name : "No data", cx, y + h - 16, 2);
}

// Small tile: label on top, value in a large font with its unit after it.
static void draw_tile(int x, int y, int w, int h, const char *label,
                      const char *value, const char *unit, bool degree)
{
    spr.fillRoundRect(x, y, w, h, 5, COLOR_TILE);

    spr.setTextColor(COLOR_DIM);
    spr.setTextDatum(TL_DATUM);
    spr.drawString(label, x + 6, y + 4, 1);

    spr.setTextColor(COLOR_TEXT);
    spr.drawString(value, x + 6, y + 12, 4);

    int ux = x + 6 + spr.textWidth(value, 4) + 3;
    int uy = y + 25;
    if (degree) {
        spr.drawCircle(ux + 1, uy + 1, 1, COLOR_DIM);  // fonts here have no degree sign
        ux += 5;
    }
    spr.setTextColor(COLOR_DIM);
    spr.drawString(unit, ux, uy, 1);
}

static void format_concentration(char *buf, size_t len, float ugm3)
{
    // Drop the decimal at 100+ so the value and unit fit the tile.
    snprintf(buf, len, ugm3 < 100.0f ? "%.1f" : "%.0f", ugm3);
}

static void format_index(char *buf, size_t len, int16_t index)
{
    if (index == SEN5X_INDEX_INVALID) {
        snprintf(buf, len, "--");
    } else {
        snprintf(buf, len, "%d", index / 10);
    }
}

static void format_wifi_status(char *buf, size_t len, const wifi_t *w)
{
    switch (w->status) {
    case WIFI_STATUS_CONNECTED:      snprintf(buf, len, "%s", w->ip_str); break;
    case WIFI_STATUS_CONNECTING:     snprintf(buf, len, "Connecting to %s", w->ssid); break;
    case WIFI_STATUS_RETRY_WAIT:     snprintf(buf, len, "%s unavailable", w->ssid); break;
    case WIFI_STATUS_NOT_CONFIGURED: snprintf(buf, len, "Wi-Fi not set up"); break;
    default:                         snprintf(buf, len, "Wi-Fi setup"); break;
    }
}

static void render_dashboard(const display_state_t *st, bool has_state)
{
    const struct sensor_data &d = st->sensors;

    // Header: title and sensor status.
    spr.setTextColor(COLOR_TEXT);
    spr.setTextDatum(ML_DATUM);
    spr.drawString("esper-aqm", 8, 16, 4);
    if (!has_state) {
        draw_pill(SCREEN_W - 8, 15, "STARTING", COLOR_IDLE, COLOR_TEXT, true);
    } else if (st->sen5x_ok) {
        draw_pill(SCREEN_W - 8, 15, "LIVE", COLOR_OK, TFT_BLACK, true);
    } else {
        draw_pill(SCREEN_W - 8, 15, "SENSOR ERROR", COLOR_WARN, TFT_BLACK, true);
    }
    spr.drawFastHLine(0, HEADER_RULE_Y, SCREEN_W, COLOR_RULE);

    // Left: AQI in its EPA band color.
    const int top = 36;
    draw_aqi_panel(6, top, 118, 106, has_state ? st->aqi : -1);

    // Right: 2 x 3 grid of readings.
    const int grid_x = 130;
    const int tile_w = (SCREEN_W - 6 - grid_x - 4) / 2;
    const int tile_h = 34;
    const int step_y = 36;
    const int col2_x = grid_x + tile_w + 4;

    char v[16];
    bool have = has_state && st->sen5x_ok;

    if (have) format_concentration(v, sizeof(v), d.mass_concentration_pm2p5); else strcpy(v, "--");
    draw_tile(grid_x, top, tile_w, tile_h, "PM2.5", v, "ug/m3", false);
    if (have) format_concentration(v, sizeof(v), d.mass_concentration_pm10p0); else strcpy(v, "--");
    draw_tile(col2_x, top, tile_w, tile_h, "PM10", v, "ug/m3", false);

    float temp_c;
    const char *temp_sensor;
    if (resolve_temperature(st, has_state, &temp_c, &temp_sensor)) {
        snprintf(v, sizeof(v), "%.1f", settings_temp_from_c(temp_c));
    } else {
        strcpy(v, "--");
    }
    char temp_label[16];
    snprintf(temp_label, sizeof(temp_label), "TEMP %s", temp_sensor);
    draw_tile(grid_x, top + step_y, tile_w, tile_h, temp_label, v, settings_temp_unit(), true);
    if (have) snprintf(v, sizeof(v), "%.0f", d.ambient_humidity); else strcpy(v, "--");
    draw_tile(col2_x, top + step_y, tile_w, tile_h, "HUMIDITY", v, "%", false);

    if (have) format_index(v, sizeof(v), d.voc_index); else strcpy(v, "--");
    draw_tile(grid_x, top + 2 * step_y, tile_w, tile_h, "VOC INDEX", v, "", false);
    if (have) format_index(v, sizeof(v), d.nox_index); else strcpy(v, "--");
    draw_tile(col2_x, top + 2 * step_y, tile_w, tile_h, "NOX INDEX", v, "", false);

    // Footer: Wi-Fi on the left, button hints on the right.
    spr.drawFastHLine(0, FOOTER_RULE_Y, SCREEN_W, COLOR_RULE);
    char net[64] = "Starting...";
    wifi_t w;
    if (wifi_get_snapshot(&w)) format_wifi_status(net, sizeof(net), &w);
    spr.setTextColor(COLOR_DIM);
    spr.setTextDatum(ML_DATUM);
    draw_text_fit(net, 8, SCREEN_H - 11, 2, 180);
    spr.setTextDatum(MR_DATUM);
    spr.drawString("BOOT menu  KEY F/C", SCREEN_W - 8, SCREEN_H - 11, 2);
}

// ---------------------------------------------------------------------------
// Menu and Wi-Fi screens
// ---------------------------------------------------------------------------

static void render_menu(const display_state_t *st)
{
    draw_header("Settings");

    const int top = 36;
    const int row_h = 27;
    const int visible = 4;
    wifi_t w = {};
    bool have_wifi = wifi_get_snapshot(&w);
    scroll_to(&s_ui.menu_top, s_ui.menu_sel, MENU_COUNT, visible);

    for (int i = s_ui.menu_top; i < MENU_COUNT && i < s_ui.menu_top + visible; i++) {
        int y = top + (i - s_ui.menu_top) * row_h;
        int cy = y + (row_h - 4) / 2 + 1;
        if (i == s_ui.menu_sel) {
            spr.fillRoundRect(4, y, SCREEN_W - 8, row_h - 4, 5, COLOR_SELECT);
        }

        const char *label = "";
        const char *value = "";
        uint16_t value_fg = COLOR_TEXT;
        switch (i) {
        case MENU_WIFI:
            label = "Wi-Fi";
            if (!have_wifi || w.ssid[0] == '\0') {
                value = "Not set up";
                value_fg = COLOR_DIM;
            } else {
                value = w.ssid;
                value_fg = w.connected ? COLOR_TEXT : COLOR_WARN;
            }
            break;
        case MENU_UNITS:
            label = "Temp unit";
            value = settings_get_fahrenheit() ? "Fahrenheit" : "Celsius";
            break;
        case MENU_TEMP_SOURCE:
            label = "Temp sensor";
            switch (settings_get_temp_source()) {
            case TEMP_SOURCE_MCP9808:
                value = st->has_mcp9808 ? "MCP9808" : "MCP9808 (not found)";
                if (!st->has_mcp9808) value_fg = COLOR_WARN;
                break;
            case TEMP_SOURCE_SEN5X:
                value = "SEN5x";
                break;
            default:
                value = st->has_mcp9808 ? "Auto (MCP9808)" : "Auto (SEN5x)";
                break;
            }
            break;
        case MENU_BACK:
            label = "Back";
            break;
        }

        spr.setTextColor(COLOR_TEXT);
        spr.setTextDatum(ML_DATUM);
        spr.drawString(label, 14, cy, 4);
        spr.setTextColor(value_fg);
        spr.setTextDatum(MR_DATUM);
        draw_text_fit(value, SCREEN_W - 14, cy, 2, 140);
    }

    draw_scroll_arrows(top, top + visible * row_h, s_ui.menu_top > 0, s_ui.menu_top + visible < MENU_COUNT);
    draw_footer_hint("BOOT next   KEY select   hold BOOT back");
}

static void render_scan(void)
{
    bool scanning = s_ui.scan_pending || wifi_scan_running();
    draw_header("Wi-Fi networks");
    if (scanning) {
        draw_pill(SCREEN_W - 8, 15, "SCANNING", COLOR_IDLE, COLOR_TEXT, true);
    }

    const int top = 36;
    const int row_h = 22;
    const int visible = 5;
    int count = scan_item_count();
    scroll_to(&s_ui.list_top, s_ui.list_sel, count, visible);

    for (int k = s_ui.list_top; k < count && k < s_ui.list_top + visible; k++) {
        int y = top + (k - s_ui.list_top) * row_h;
        int cy = y + row_h / 2;
        bool selected = k == s_ui.list_sel;
        if (selected) {
            spr.fillRoundRect(4, y, SCREEN_W - 16, row_h - 2, 4, COLOR_SELECT);
        }

        spr.setTextDatum(ML_DATUM);
        if (k < s_ui.ap_count) {
            const wifi_ap_t &ap = s_ui.aps[k];
            spr.setTextColor(COLOR_TEXT);
            draw_text_fit(ap.ssid, 12, cy, 2, 230);
            if (ap.secure) draw_lock(258, cy - 7, selected ? COLOR_TEXT : COLOR_DIM);
            draw_signal_bars(276, cy + 6, ap.rssi, selected ? COLOR_TEXT : COLOR_DIM);
        } else if (k == s_ui.ap_count) {
            spr.setTextColor(scanning ? COLOR_DIM : COLOR_TEXT);
            spr.drawString(scanning ? "Scanning..." : "Scan again", 12, cy, 2);
        } else {
            spr.setTextColor(COLOR_TEXT);
            spr.drawString("Back", 12, cy, 2);
        }
    }

    if (!scanning && s_ui.ap_count == 0) {
        spr.setTextColor(COLOR_DIM);
        spr.setTextDatum(ML_DATUM);
        spr.drawString("No networks found", 12, top + 3 * row_h, 2);
    }
    draw_scroll_arrows(top, top + visible * row_h, s_ui.list_top > 0, s_ui.list_top + visible < count);
    draw_footer_hint("BOOT next   KEY select   hold BOOT back");
}

// Label for picker position i: long form for the selected box, short for neighbors.
static const char *picker_label(int i, bool selected, int *font)
{
    static char ch[2];
    const char *set = k_charsets[s_ui.charset];
    int n = (int)strlen(set);
    *font = 2;
    if (i < n) {
        if (set[i] == ' ') return selected ? "space" : "spc";
        ch[0] = set[i];
        ch[1] = '\0';
        *font = 4;
        return ch;
    }
    switch (i - n) {
    case PICK_NEXT_SET: return k_charset_names[(s_ui.charset + 1) % CHARSET_COUNT];
    case PICK_DELETE:   return selected ? "Delete" : "DEL";
    case PICK_DONE:     return selected ? "Done" : "OK";
    default:            return selected ? "Cancel" : "ESC";
    }
}

static void render_password(void)
{
    // Header: title and the network name.
    spr.setTextColor(COLOR_TEXT);
    spr.setTextDatum(ML_DATUM);
    spr.drawString("Password", 8, 16, 4);
    spr.setTextColor(COLOR_DIM);
    spr.setTextDatum(MR_DATUM);
    draw_text_fit(s_ui.ssid, SCREEN_W - 8, 16, 2, 170);
    spr.drawFastHLine(0, HEADER_RULE_Y, SCREEN_W, COLOR_RULE);

    // Entry field: shows the end of the password if it's too long to fit.
    const int fx = 8, fy = 38, fw = SCREEN_W - 16, fh = 34;
    const int text_max = fw - 28;
    spr.fillRoundRect(fx, fy, fw, fh, 5, COLOR_TILE);
    const char *shown = s_ui.pass;
    while (*shown && spr.textWidth(shown, 4) > text_max) shown++;
    int tx = fx + 10;
    int cy = fy + fh / 2;
    spr.setTextDatum(ML_DATUM);
    if (shown != s_ui.pass) {
        spr.setTextColor(COLOR_DIM);
        spr.drawString("<", tx, cy, 2);
        tx += 10;
    }
    int text_end = tx;
    if (s_ui.pass_len > 0) {
        spr.setTextColor(COLOR_TEXT);
        spr.drawString(shown, tx, cy + 1, 4);
        text_end = tx + spr.textWidth(shown, 4);
    } else {
        spr.setTextColor(COLOR_FAINT);
        spr.drawString("Enter the Wi-Fi password", tx + 4, cy, 2);
    }
    if ((esp_timer_get_time() / 500000) % 2 == 0) {
        spr.fillRect(text_end + 1, cy - 11, 2, 22, COLOR_TEXT);  // cursor
    }

    // Character picker: selected entry in the middle, neighbors on both sides.
    const int pcy = 98;
    int n = picker_count();
    int font;
    const char *label = picker_label(s_ui.pick, true, &font);
    int bw = spr.textWidth(label, font) + 22;
    if (bw < 44) bw = 44;
    int bx = (SCREEN_W - bw) / 2;
    spr.fillRoundRect(bx, pcy - 20, bw, 40, 6, COLOR_SELECT);
    spr.setTextColor(COLOR_TEXT);
    spr.setTextDatum(MC_DATUM);
    spr.drawString(label, SCREEN_W / 2, pcy + 1, font);

    for (int side = -1; side <= 1; side += 2) {
        int edge = side > 0 ? bx + bw + 4 : bx - 4;
        for (int k = 1; k < n; k++) {
            int i = ((s_ui.pick + side * k) % n + n) % n;
            const char *nl = picker_label(i, false, &font);
            int w = spr.textWidth(nl, font) + 10;
            int cx = side > 0 ? edge + w / 2 : edge - w / 2;
            if (cx - w / 2 < 0 || cx + w / 2 > SCREEN_W) break;
            spr.setTextColor(k <= 2 ? COLOR_DIM : COLOR_FAINT);
            spr.drawString(nl, cx, pcy + 1, font);
            edge += side * w;
        }
    }

    // Character set tabs.
    const int tab_w = 56;
    int tab_x = (SCREEN_W - CHARSET_COUNT * tab_w) / 2;
    for (int c = 0; c < CHARSET_COUNT; c++) {
        int cx = tab_x + c * tab_w + tab_w / 2;
        if (c == s_ui.charset) {
            spr.fillRoundRect(cx - 22, 125, 44, 16, 5, COLOR_TILE);
            spr.setTextColor(COLOR_TEXT);
        } else {
            spr.setTextColor(COLOR_FAINT);
        }
        spr.drawString(k_charset_names[c], cx, 133, 2);
    }

    draw_footer_hint("BOOT next   KEY enter   hold KEY delete");
}

static void render_connect(void)
{
    wifi_t w = {};
    wifi_get_snapshot(&w);
    draw_header("Wi-Fi");

    spr.setTextColor(COLOR_TEXT);
    spr.setTextDatum(MC_DATUM);
    draw_text_fit(s_ui.ssid, SCREEN_W / 2, 56, 4, SCREEN_W - 16);

    char detail[64] = "";
    const char *hint;
    if (connect_succeeded(&w)) {
        spr.setTextColor(COLOR_OK);
        spr.drawString("Connected", SCREEN_W / 2, 92, 4);
        snprintf(detail, sizeof(detail), "IP address %s", w.ip_str);
        hint = "Press any button";
    } else if (w.status == WIFI_STATUS_FAILED) {
        spr.setTextColor(COLOR_WARN);
        spr.drawString("Couldn't connect", SCREEN_W / 2, 92, 4);
        const char *why = wifi_reason_text(w.last_reason);
        if (why) {
            snprintf(detail, sizeof(detail), "%s", why);
        } else {
            snprintf(detail, sizeof(detail), "Wi-Fi error %d", w.last_reason);
        }
        hint = s_ui.secure ? "KEY retype   BOOT list   hold BOOT exit"
                           : "KEY retry   BOOT list   hold BOOT exit";
    } else {
        static const char *const dots[] = { "Connecting", "Connecting.", "Connecting..", "Connecting..." };
        spr.setTextColor(COLOR_TEXT);
        spr.drawString(dots[(esp_timer_get_time() / 400000) % 4], SCREEN_W / 2, 92, 4);
        snprintf(detail, sizeof(detail), "This can take a few seconds");
        hint = "hold BOOT cancel";
    }
    spr.setTextColor(COLOR_DIM);
    spr.drawString(detail, SCREEN_W / 2, 124, 2);
    draw_footer_hint(hint);
}

// ---------------------------------------------------------------------------
// Task
// ---------------------------------------------------------------------------

static void display_task(void *arg)
{
    ESP_LOGI(TAG, "display_task running on core %d", xPortGetCoreID());
    display_state_t st;

    while (1) {
        // Wake on a button press or every DISPLAY_REFRESH_MS.
        ui_event_t e;
        if (xQueueReceive(s_ui.events, &e, pdMS_TO_TICKS(DISPLAY_REFRESH_MS)) == pdTRUE) {
            handle_event(e);
            while (xQueueReceive(s_ui.events, &e, 0) == pdTRUE) {
                handle_event(e);
            }
        }
        tick();

        taskENTER_CRITICAL(&s_lock);
        bool has_state = s_has_state;
        st = s_state;
        taskEXIT_CRITICAL(&s_lock);

        spr.fillSprite(COLOR_BG);
        switch (s_ui.screen) {
        case SCREEN_DASHBOARD: render_dashboard(&st, has_state); break;
        case SCREEN_MENU:      render_menu(&st); break;
        case SCREEN_SCAN:      render_scan(); break;
        case SCREEN_PASSWORD:  render_password(); break;
        case SCREEN_CONNECT:   render_connect(); break;
        }
        spr.pushSprite(0, 0);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t display_start(void)
{
    ESP_RETURN_ON_FALSE(!s_task, ESP_ERR_INVALID_STATE, TAG, "already started");

    s_ui.events = xQueueCreate(UI_EVENT_QUEUE_LEN, sizeof(ui_event_t));
    ESP_RETURN_ON_FALSE(s_ui.events, ESP_ERR_NO_MEM, TAG, "queue");
    s_ui.screen = SCREEN_DASHBOARD;

    // TFT_eSPI doesn't set these pins up as outputs in this ESP-IDF build,
    // so turn the panel power and backlight on directly.
    gpio_config_t lcd_pins = {
        .pin_bit_mask = (1ULL << PIN_LCD_BACKLIGHT) | (1ULL << PIN_LCD_POWER),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&lcd_pins), TAG, "lcd pins");
    gpio_set_level(PIN_LCD_POWER, 1);

    tft.init();
    gpio_set_level(PIN_LCD_BACKLIGHT, 1);
    tft.setRotation(1);
    tft.fillScreen(COLOR_BG);
    if (!spr.createSprite(SCREEN_W, SCREEN_H)) {
        tft.fillScreen(TFT_RED);
        ESP_LOGE(TAG, "sprite alloc failed");
        return ESP_ERR_NO_MEM;
    }
    spr.setSwapBytes(true);

    button_driver_init();
    const gpio_num_t pins[2] = { PIN_KEY_BUTTON, PIN_BOOT_BUTTON };
    for (gpio_num_t pin : pins) {
        button_t b = {
            .pin = pin,
            .active_low = true,
            .debounce_ms = BUTTON_DEBOUNCE_MS,
            .long_press_ms = BUTTON_LONG_PRESS_MS,
            .repeat_ms = BUTTON_REPEAT_MS,
            .on_event = on_button,
        };
        ESP_RETURN_ON_ERROR(button_register(&b), TAG, "button_register");
    }

    BaseType_t ok = xTaskCreatePinnedToCore(display_task, "display_task", DISPLAY_TASK_STACK, NULL,
                                            DISPLAY_TASK_PRIORITY, &s_task, DISPLAY_TASK_CORE);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}

void display_update(const display_state_t *state)
{
    taskENTER_CRITICAL(&s_lock);
    s_state = *state;
    s_has_state = true;
    taskEXIT_CRITICAL(&s_lock);
}
