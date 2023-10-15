#pragma once

#include <i2cdev.h>

#ifdef __cplusplus
extern "C" {
#endif

enum lcd_cursor_mode {
    LCD_CURSOR_OFF = 0x0,
    LCD_CURSOR_BLINK = 0x01,
    LCD_CURSOR_ON = 0x02
};

enum lcd_display_mode {
    LCD_DISPLAY_OFF = 0x0,
    LCD_DISPLAY_ON = 0x04
};

enum lcd_backlight_mode {
    LCD_BACKLIGHT_OFF = 0x0,
    LCD_BACKLIGHT_ON = 0x8
};

enum lcd_scroll_dir {
    LCD_SCROLL_LEFT,
    LCD_SCROLL_RIGHT,
    LCD_SCROLL_AUTO,
    LCD_SCROLL_NO_AUTO
};

enum lcd_text_dir {
    LCD_LEFT_TO_RIGHT,
    LCD_RIGHT_TO_LEFT
};

enum lcd_char_size {
    LCD_CHAR_SIZE_SMALL,
    LCD_CHAR_SIZE_BIG
};

typedef struct lcd_ascii {
    i2c_dev_t dev;
    enum lcd_backlight_mode backlight;
    uint8_t display_func;
    uint8_t display_ctrl;
    uint8_t display_mode;
    int num_rows;
    int num_cols;
} lcd_ascii_t;

// commands
#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME 0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_CURSORSHIFT 0x10
#define LCD_FUNCTIONSET 0x20
#define LCD_SETCGRAMADDR 0x40
#define LCD_SETDDRAMADDR 0x80

// flags for display entry mode
#define LCD_ENTRYRIGHT 0x00
#define LCD_ENTRYLEFT 0x02
#define LCD_ENTRYSHIFTINCREMENT 0x01
#define LCD_ENTRYSHIFTDECREMENT 0x00

// flags for display on/off control
#define LCD_DISPLAYON 0x04
#define LCD_DISPLAYOFF 0x00
#define LCD_CURSORON 0x02
#define LCD_CURSOROFF 0x00
#define LCD_BLINKON 0x01
#define LCD_BLINKOFF 0x00

// flags for display/cursor shift
#define LCD_DISPLAYMOVE 0x08
#define LCD_CURSORMOVE 0x00
#define LCD_MOVERIGHT 0x04
#define LCD_MOVELEFT 0x00

// flags for function set
#define LCD_8BITMODE 0x10
#define LCD_4BITMODE 0x00
#define LCD_2LINE 0x08
#define LCD_1LINE 0x00
#define LCD_5x10DOTS 0x04
#define LCD_5x8DOTS 0x00

// flags for backlight control
#define LCD_BACKLIGHT 0x08
#define LCD_NOBACKLIGHT 0x00

#define ENABLE_BIT     0b00000100
#define READ_WRITE_BIT 0b00000010
#define REG_SELECT_BIT 0b00000001

lcd_ascii_t* lcd_init(uint8_t addr, i2c_port_t port, i2c_config_t* i2c, int rows, int cols, enum lcd_char_size dots);
void lcd_free(lcd_ascii_t* lcd);
esp_err_t lcd_clear(lcd_ascii_t* lcd);
esp_err_t lcd_home(lcd_ascii_t* lcd);
esp_err_t lcd_printf(lcd_ascii_t* lcd, const char* fmt, ...);
esp_err_t lcd_display_control(lcd_ascii_t* lcd, uint8_t ctrl);
esp_err_t lcd_display(lcd_ascii_t* lcd, enum lcd_display_mode mode);
esp_err_t lcd_backlight(lcd_ascii_t* lcd, enum lcd_backlight_mode mode);
esp_err_t lcd_cursor(lcd_ascii_t* lcd, enum lcd_cursor_mode mode);
esp_err_t lcd_cursor_pos(lcd_ascii_t* lcd, uint8_t col, uint8_t row);
esp_err_t lcd_scroll_display(lcd_ascii_t* lcd, enum lcd_scroll_dir dir);
esp_err_t lcd_text_direction(lcd_ascii_t* lcd, enum lcd_text_dir dir);
esp_err_t lcd_shift_inc(lcd_ascii_t* lcd);
esp_err_t lcd_shift_dec(lcd_ascii_t* lcd);
esp_err_t lcd_create_char(lcd_ascii_t* lcd, uint8_t loc, uint8_t chars[8]);
esp_err_t lcd_command(lcd_ascii_t* lcd, uint8_t cmd);
esp_err_t lcd_send(lcd_ascii_t* lcd, uint8_t val, uint8_t flags);
esp_err_t lcd_write_nibble(lcd_ascii_t* lcd, uint8_t nib);
esp_err_t lcd_write_data(lcd_ascii_t* lcd, uint8_t val);
esp_err_t lcd_pulse_enable(lcd_ascii_t* lcd, uint8_t data);
#ifdef __cplusplus
}
#endif
