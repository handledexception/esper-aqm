#include "lcd_ascii.h"
#include "utils.h"

#include "FreeRTOS/FreeRTOS.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static const char* TAG = "aqm-lcd-ascii";

#define I2C_FREQ_HZ 100000
#define CHECK_ARG(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)

static esp_err_t write_8_nolock(i2c_dev_t *dev, uint8_t val)
{
   return i2c_dev_write(dev, NULL, 0, &val, 1);
}
static esp_err_t write_8(i2c_dev_t *dev, uint8_t val)
{
    I2C_DEV_TAKE_MUTEX(dev);
    I2C_DEV_CHECK(dev, write_8_nolock(dev, val));
    I2C_DEV_GIVE_MUTEX(dev);
    return ESP_OK;
}

static esp_err_t write_data_nolock(i2c_dev_t *dev, uint8_t* data, size_t size)
{
   return i2c_dev_write(dev, NULL, 0, data, size);
}
static esp_err_t write_data(i2c_dev_t *dev, uint8_t* data, size_t size)
{
    I2C_DEV_TAKE_MUTEX(dev);
    I2C_DEV_CHECK(dev, write_data_nolock(dev, data, size));
    I2C_DEV_GIVE_MUTEX(dev);
    return ESP_OK;
}

lcd_ascii_t* lcd_init(uint8_t addr, i2c_port_t port, i2c_config_t* i2c, int rows, int cols, enum lcd_char_size dots)
{
    ESP_LOGI(TAG, "Initializing ASCII LCD...");

    lcd_ascii_t* lcd = malloc(sizeof(lcd_ascii_t));
    memset(&lcd->dev, 0, sizeof(i2c_dev_t));

    lcd->dev.port = port;
    lcd->dev.addr = addr;
    lcd->dev.cfg.sda_io_num = i2c->sda_io_num;
    lcd->dev.cfg.scl_io_num = i2c->scl_io_num;
    lcd->dev.cfg.sda_pullup_en = i2c->sda_pullup_en;
    lcd->dev.cfg.scl_pullup_en = i2c->scl_pullup_en;
#if HELPER_TARGET_IS_ESP32
    lcd->dev.cfg.master.clk_speed = I2C_FREQ_HZ;
#endif
    ESP_ERROR_CHECK(i2c_dev_create_mutex(&lcd->dev));

    lcd->num_rows = rows;
    lcd->num_cols = cols;

    lcd->backlight = LCD_BACKLIGHT_OFF;
    lcd->display_func = LCD_4BITMODE | LCD_1LINE | LCD_5x8DOTS;
    if (rows > 1)
        lcd->display_func |= LCD_2LINE;
    if (dots == LCD_CHAR_SIZE_BIG && rows == 1)
        lcd->display_func |= LCD_5x10DOTS;
    sleep_msec(50); // need at least 40ms after power rises above 2.7V before sending commands

    lcd_write_data(lcd, lcd->backlight);
    lcd->backlight = LCD_BACKLIGHT_ON;
    sleep_msec(1000);
    lcd_display(lcd, LCD_DISPLAY_ON);

    // start in 8bit mode, try to set 4bit mode
    // lcd_send(lcd, 0b00110000, 0);
    // lcd_send(lcd, 0b00000010, 0);
    // lcd_send(lcd, 0b00001100, 0);
    // lcd_send(lcd, 0b00000001, 0);

    lcd_write_nibble(lcd, 0x03 << 4); // 1st try
    sleep_usec(4500);
    lcd_write_nibble(lcd, 0x03 << 4); // 2nd try
    sleep_usec(4500);
    lcd_write_nibble(lcd, 0x03 << 4); // 3rd try
    sleep_usec(150);
    lcd_write_nibble(lcd, 0x02 << 4); // set 4bit interface

    // set num lines, font size, etc.
    lcd_command(lcd, LCD_FUNCTIONSET | lcd->display_func);
    lcd_clear(lcd);

    lcd->display_ctrl = LCD_DISPLAY_ON | LCD_CURSOR_OFF | LCD_BLINKOFF;
    lcd_display_control(lcd, lcd->display_ctrl);
    lcd_clear(lcd);

    lcd->display_mode = LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT;
    lcd_command(lcd, LCD_ENTRYMODESET | lcd->display_mode);

    lcd_home(lcd);

    return lcd;
}

void lcd_free(lcd_ascii_t* lcd)
{
    if (lcd != NULL) {
        i2c_dev_delete_mutex(&lcd->dev);
        free(lcd);
        lcd = NULL;
    }
}

esp_err_t lcd_clear(lcd_ascii_t* lcd)
{
    CHECK_ARG(lcd);
    esp_err_t err = lcd_command(lcd, LCD_CLEARDISPLAY);
    sleep_usec(2000);
    return err;
}
esp_err_t lcd_home(lcd_ascii_t* lcd)
{
    CHECK_ARG(lcd);
    esp_err_t err = lcd_command(lcd, LCD_RETURNHOME);
    sleep_usec(2000);
    return err;
}
esp_err_t lcd_printf(lcd_ascii_t* lcd, const char* fmt, ...)
{
    CHECK_ARG(lcd);
    va_list args;
    va_start(args, fmt);
    char buf[16];
    memset(&buf[0], 0, 16);
    sprintf(&buf[0], fmt, args);
    va_end(args);
    int len = strlen(&buf[0]);
    if (len > lcd->num_cols)
        len = lcd->num_cols;
    if (len > 16)
        len = 16;

    ESP_LOGD(TAG, "LCD Text: %s (%d chars)", &buf[0], len);
    for (int i = 0; i < len; i++)
        ESP_ERROR_CHECK(lcd_send(lcd, buf[i], REG_SELECT_BIT));

    return ESP_OK;
}
esp_err_t lcd_display_control(lcd_ascii_t* lcd, uint8_t ctrl)
{
    CHECK_ARG(lcd);
    return lcd_command(lcd, LCD_DISPLAYCONTROL | ctrl);
}
esp_err_t lcd_display(lcd_ascii_t* lcd, enum lcd_display_mode mode)
{
    CHECK_ARG(lcd);
    lcd_command(lcd, LCD_DISPLAYCONTROL | (uint8_t)mode);
    return ESP_OK;
}
esp_err_t lcd_backlight(lcd_ascii_t* lcd, enum lcd_backlight_mode mode)
{
    CHECK_ARG(lcd);
    lcd_write_data(lcd, mode);
    return ESP_OK;
}
esp_err_t lcd_cursor(lcd_ascii_t* lcd, enum lcd_cursor_mode mode)
{
    CHECK_ARG(lcd);
    return ESP_OK;
}
esp_err_t lcd_cursor_pos(lcd_ascii_t* lcd, uint8_t col, uint8_t row)
{
    CHECK_ARG(lcd);
    int row_offsets[] = { 0x00, 0x40, 0x14, 0x54 };
    if (row > lcd->num_rows)
        row = lcd->num_rows - 1;
    return lcd_command(lcd, LCD_SETDDRAMADDR | (col + row_offsets[row]));
}
esp_err_t lcd_scroll_display(lcd_ascii_t* lcd, enum lcd_scroll_dir dir)
{
    CHECK_ARG(lcd);
    return ESP_OK;
}
esp_err_t lcd_text_direction(lcd_ascii_t* lcd, enum lcd_text_dir dir)
{
    CHECK_ARG(lcd);
    return ESP_OK;
}
esp_err_t lcd_shift_inc(lcd_ascii_t* lcd)
{
    CHECK_ARG(lcd);
    return ESP_OK;
}
esp_err_t lcd_shift_dec(lcd_ascii_t* lcd)
{
    CHECK_ARG(lcd);
    return ESP_OK;
}
esp_err_t lcd_create_char(lcd_ascii_t* lcd, uint8_t loc, uint8_t chars[8])
{
    CHECK_ARG(lcd);
    return ESP_OK;
}
esp_err_t lcd_command(lcd_ascii_t* lcd, uint8_t cmd)
{
    CHECK_ARG(lcd);
    return lcd_send(lcd, cmd, 0);
}
esp_err_t lcd_send(lcd_ascii_t* lcd, uint8_t val, uint8_t flags)
{
    CHECK_ARG(lcd);
    uint8_t hi_nib = val & 0xf0;
    uint8_t lo_nib = (val << 4) & 0xf0;
    uint8_t arr[4];
    arr[0] = hi_nib|flags|lcd->backlight|ENABLE_BIT;
    arr[1] = hi_nib|flags|lcd->backlight;
    arr[2] = lo_nib|flags|lcd->backlight|ENABLE_BIT;
    arr[3] = lo_nib|flags|lcd->backlight;
    write_data(&lcd->dev, &arr[0], 4);
    // for (int i = 0; i < 4; i++)
    //     lcd_write_data(lcd, arr[i]);
    sleep_msec(5);
    // ESP_ERROR_CHECK(lcd_write_nibble(lcd, hi_nib|mode));
    // ESP_ERROR_CHECK(lcd_write_nibble(lcd, lo_nib|mode));
    return ESP_OK;
}
esp_err_t lcd_write_nibble(lcd_ascii_t* lcd, uint8_t nib)
{
    CHECK_ARG(lcd);
    ESP_ERROR_CHECK(lcd_write_data(lcd, nib));
    ESP_ERROR_CHECK(lcd_pulse_enable(lcd, nib));
    return ESP_OK;
}
esp_err_t lcd_write_data(lcd_ascii_t* lcd, uint8_t val)
{
    CHECK_ARG(lcd);
    ESP_ERROR_CHECK(write_8(&lcd->dev, val | lcd->backlight));
    return ESP_OK;
}
esp_err_t lcd_pulse_enable(lcd_ascii_t* lcd, uint8_t data)
{
    CHECK_ARG(lcd);
    ESP_ERROR_CHECK(lcd_write_data(lcd, data | ENABLE_BIT));
    sleep_usec(1);
    ESP_ERROR_CHECK(lcd_write_data(lcd, data & ~ENABLE_BIT));
    sleep_usec(50);
    return ESP_OK;
}
