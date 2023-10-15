#include "system.h"
#include "sensor_data.h"
#include "sen5x_i2c.h"
#include "sensirion_i2c_hal.h"
#include "lcd_ascii.h"
#include "mcp9808.h"
#include "http_server.h"
#include "utils.h"

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "rtc.h"
#include "driver/i2c.h"

#include <stdio.h>
#include <string.h>

static constexpr auto TAG = "esper-aqm";
static constexpr auto kAppVersion = "1.0.0";

#define I2C_FREQ 100000
#define I2C_SDA_GPIO_PIN (gpio_num_t)(18)
#define I2C_SCL_GPIO_PIN (gpio_num_t)(17)
#define I2C_ADDR_MCP9808 0x18
#define I2C_ADDR_SEN5X 0x69
#define SENSOR_UPDATE_RATE 1000 // msec

constexpr double usec_to_sec(int64_t usec) {
    return (double)usec / 1000000.0;
}

constexpr double c_to_f(double celsius) {
    return (celsius * 1.8) + 32.0;
}

class esper_aqm {
public:
    esper_aqm(int update_rate_msec);
    ~esper_aqm();
    esp_err_t init();
    void run();

private:
    void read_sensors();
    void i2c_init();
    void i2c_enumerate(i2c_port_t port, i2c_config_t* cfg);

    system_t* _system;
    i2c_dev_t _mcp;
    i2c_dev_t _sen;
    lcd_ascii_t* _lcd;
    sensor_data _data;
    rest_server_context_t* _rest;
    int _update_rate_msec;
};

esper_aqm::esper_aqm(int update_rate_msec)
: _system{nullptr}, _mcp{}, _sen{}, _data{}, _rest{nullptr}, _update_rate_msec{update_rate_msec}
{
    _rest = new rest_server_context_t();
    sensor_data_init(&_data);
    _rest->sensors = &_data;
}

esper_aqm::~esper_aqm()
{
    if (_rest != nullptr) {
        delete _rest;
        _rest = nullptr;
    }
    system_shutdown(_system);
}

esp_err_t esper_aqm::init()
{
    printf("Esper Air Quality Monitor %s\n", kAppVersion);

    _system = system_init();

    ESP_ERROR_CHECK(system_get_info(_system));
    ESP_ERROR_CHECK(system_print_info(_system));

    i2c_init();

    i2c_config_t lcd_i2c;
    lcd_i2c.sda_io_num = (int)I2C_SDA_GPIO_PIN;
    lcd_i2c.scl_io_num = (int)I2C_SCL_GPIO_PIN;
    lcd_i2c.sda_pullup_en = true;
    lcd_i2c.scl_pullup_en = true;
    _lcd = lcd_init(0x27, I2C_NUM_0, &lcd_i2c, 2, 16, LCD_CHAR_SIZE_SMALL);
    lcd_backlight(_lcd, LCD_BACKLIGHT_ON);
    lcd_cursor_pos(_lcd, 0, 0);
    lcd_printf(_lcd, "esper-aqm 1.0.0");
    lcd_cursor_pos(_lcd, 0, 1);
    lcd_printf(_lcd, "initializing...");

    system_wifi_init(_system);

    // MCP9808 Temperature Sensor
    memset(&_mcp, 0, sizeof(i2c_dev_t));
    ESP_ERROR_CHECK(mcp9808_init_desc(&_mcp, I2C_ADDR_MCP9808, I2C_NUM_0, I2C_SDA_GPIO_PIN, I2C_SCL_GPIO_PIN));
    ESP_ERROR_CHECK(mcp9808_init(&_mcp));

    // SEN55 Air Quality Sensor
    ESP_ERROR_CHECK(sensirion_i2c_hal_init(I2C_FREQ,
        I2C_ADDR_SEN5X, I2C_NUM_0,
        I2C_SDA_GPIO_PIN, I2C_SCL_GPIO_PIN,
        true, true));
    ESP_ERROR_CHECK((esp_err_t)sen5x_device_reset());

    unsigned char sen5x_name[32];
    sen5x_get_product_name(&sen5x_name[0], 32);
    unsigned char sen5x_serial[32];
    sen5x_get_serial_number(&sen5x_serial[0], 32);
    uint8_t sen5x_fw_maj = 0;
    uint8_t sen5x_fw_min = 0;
    bool sen5x_fw_debug = false;
    uint8_t sen5x_hw_maj = 0;
    uint8_t sen5x_hw_min = 0;
    uint8_t sen5x_proto_maj = 0;
    uint8_t sen5x_proto_min = 0;
    sen5x_get_version(&sen5x_fw_maj, &sen5x_fw_min, &sen5x_fw_debug, &sen5x_hw_maj, &sen5x_hw_min, &sen5x_proto_maj, &sen5x_proto_min);
    ESP_LOGI(TAG, "Sensirion Device: %s Serial: %s\n", &sen5x_name[0], &sen5x_serial[0]);
    ESP_LOGI(TAG, "Firmware Version: %d.%d (%s) | Hardware Version: %d.%d | Protocol Version: %d.%d",
        sen5x_fw_maj,
        sen5x_fw_min,
        sen5x_fw_debug ? "debug" : "release",
        sen5x_hw_maj,
        sen5x_hw_min,
        sen5x_proto_maj,
        sen5x_proto_min);
    ESP_ERROR_CHECK((esp_err_t)sen5x_start_measurement());

    _rest->sys = _system;
    http_server_start("/", _rest);

    lcd_clear(_lcd);
    lcd_cursor_pos(_lcd, 0, 0);

    return ESP_OK;
}

void esper_aqm::run()
{
    static unsigned int lcd_window = 0;
    static int64_t usec_last = 0;
    while (1) {
        int64_t usec_now = esp_timer_get_time();
        if (usec_now - usec_last >= 5000000) {
            usec_last = usec_now;
            lcd_window = !lcd_window;
            ESP_LOGI(TAG, "lcd_window = %d", lcd_window);
        }

        double duration = usec_to_sec(system_get_uptime(_system));
        ESP_LOGI(TAG, "[%lldusec (+%.3fsec)] Reading sensors...", usec_now, duration);

        read_sensors();

        if (lcd_window == 0) {
            lcd_cursor_pos(_lcd, 0, 0);
            char txt[16];
            memset(&txt[0], ' ', 16);
            sprintf(&txt[0], "MCP: %.2fC     ", _data.temperature_mcp9808);
            lcd_printf(_lcd, &txt[0]);
            lcd_cursor_pos(_lcd, 0, 1);
            memset(&txt[0], ' ', 16);
            sprintf(&txt[0], "SEN: %.2fC     ", _data.ambient_temperature);
            lcd_printf(_lcd, &txt[0]);
        } else if (lcd_window == 1) {
            lcd_cursor_pos(_lcd, 0, 0);
            char txt[16];
            memset(&txt[0], ' ', 16);
            sprintf(&txt[0], "RH: %.2f     ", _data.ambient_humidity);
            lcd_printf(_lcd, &txt[0]);
            lcd_cursor_pos(_lcd, 0, 1);
            memset(&txt[0], ' ', 16);
            sprintf(&txt[0], "PM2.5: %.1f     ", _data.mass_concentration_pm2p5);
            lcd_printf(_lcd, &txt[0]);       
        }
    
        printf("MCP9808 Temp: %.2f °C (%.2f °F)\n", _data.temperature_mcp9808, c_to_f(_data.temperature_mcp9808));

        { // Sensirion readings
            printf("Mass concentration pm1p0: %.1f µg/m³\n",
                _data.mass_concentration_pm1p0);
            printf("Mass concentration pm2p5: %.1f µg/m³\n",
                _data.mass_concentration_pm2p5);
            printf("Mass concentration pm4p0: %.1f µg/m³\n",
                _data.mass_concentration_pm4p0);
            printf("Mass concentration pm10p0: %.1f µg/m³\n",
                _data.mass_concentration_pm10p0);
            printf("Ambient humidity: %.1f %%RH\n",
                _data.ambient_humidity);
            printf("Ambient temperature: %.1f °C (%.1f °F)\n",
                _data.ambient_temperature, c_to_f(_data.ambient_temperature));
            if (_data.voc_index == 0x7fff) {
                printf("Voc index: n/a\n");
            } else {
                printf("Voc index: %.1f\n", _data.voc_index / 10.0f);
            }
            if (_data.nox_index == 0x7fff) {
                printf("Nox index: n/a\n");
            } else {
                printf("Nox index: %.1f\n", _data.nox_index / 10.0f);
            }
        }

        sleep_msec(_update_rate_msec);
    }
}

void esper_aqm::read_sensors()
{
    _data.temperature_mcp9808 = 0.0;
    ESP_ERROR_CHECK(mcp9808_get_temperature(&_mcp, &_data.temperature_mcp9808, NULL, NULL, NULL));

    uint32_t sen5x_status = 0;
    int16_t sen5x_err = sen5x_read_device_status(&sen5x_status);
    if (!sen5x_err && !sen5x_status) {
        uint16_t mass_concentration_pm1p0 = 0;
        uint16_t mass_concentration_pm2p5 = 0;
        uint16_t mass_concentration_pm4p0 = 0;
        uint16_t mass_concentration_pm10p0 = 0;
        int16_t  ambient_humidity = 0;
        int16_t  ambient_temperature = 0;
        sen5x_err = sen5x_read_measured_values(
            &mass_concentration_pm1p0, &mass_concentration_pm2p5,
            &mass_concentration_pm4p0, &mass_concentration_pm10p0,
            &ambient_humidity, &ambient_temperature, &_data.voc_index, &_data.nox_index);
        if (!sen5x_err) {
            _data.mass_concentration_pm1p0 = (float)mass_concentration_pm1p0 / 10.0f;
            _data.mass_concentration_pm2p5 = (float)mass_concentration_pm2p5 / 10.0f;
            _data.mass_concentration_pm4p0 = (float)mass_concentration_pm4p0 / 10.0f;
            _data.mass_concentration_pm10p0 = (float)mass_concentration_pm10p0 / 10.0f;
            _data.ambient_humidity = (float)ambient_humidity / 100.0f;
            _data.ambient_temperature = (float)ambient_temperature / 200.0f;
        }
    } else {
        ESP_LOGE(TAG, "Sensirion device status error! Status: %d Error: %d", sen5x_status, sen5x_err);
    }
}

void esper_aqm::i2c_init()
{
    i2c_config_t config;
    memset(&config, 0, sizeof(i2c_config_t));
    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = I2C_SDA_GPIO_PIN;
    config.scl_io_num = I2C_SCL_GPIO_PIN;
    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    config.master.clk_speed = I2C_FREQ;

    i2c_enumerate(I2C_NUM_0, &config);

    ESP_ERROR_CHECK(gpio_set_direction(I2C_SDA_GPIO_PIN, GPIO_MODE_INPUT_OUTPUT_OD));
    ESP_ERROR_CHECK(gpio_set_direction(I2C_SCL_GPIO_PIN, GPIO_MODE_INPUT_OUTPUT_OD));
    ESP_ERROR_CHECK(gpio_pullup_en(I2C_SDA_GPIO_PIN));
    ESP_ERROR_CHECK(gpio_pullup_en(I2C_SCL_GPIO_PIN));
}

void esper_aqm::i2c_enumerate(i2c_port_t port, i2c_config_t* cfg)
{
    printf("Enumerating I2C devices...\n");
    ESP_ERROR_CHECK(i2c_param_config(port, cfg));
    ESP_ERROR_CHECK(i2c_driver_install(port, cfg->mode, 0, 0, 0));
    for (uint8_t address = 0; address < 127; address++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        ESP_ERROR_CHECK(i2c_master_start(cmd));
        ESP_ERROR_CHECK(i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true));
        ESP_ERROR_CHECK(i2c_master_stop(cmd));
        esp_err_t err = i2c_master_cmd_begin(port, cmd, 10 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);
        if (err == ESP_OK) {
            printf("I2C device found at address 0x%02X\n", address);
        }
    }
    ESP_ERROR_CHECK(i2c_driver_delete(port));
}

extern "C" void app_main(void)
{
    auto aqm = new esper_aqm(SENSOR_UPDATE_RATE);

    ESP_ERROR_CHECK(aqm->init());

    aqm->run();

    printf("Esper AQM restart...\n");
    if (aqm != nullptr) {
        delete aqm;
        aqm = nullptr;
    }

    fflush(stdout);
    esp_restart();
}