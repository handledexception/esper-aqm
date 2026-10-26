#pragma once

// TFT_eSPI setup for the LilyGo T-Display-S3 (ST7789, 170x320, 8-bit parallel).
// TFT_eSPI.h picks this file up automatically with __has_include(<tft_setup.h>).
// Values match the ltcgen project, which runs on the same board.

#define DISABLE_ALL_LIBRARY_WARNINGS  // e.g. "TOUCH_CS pin not defined"

#define ST7789_DRIVER
#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERSION_ON

#define TFT_WIDTH  170
#define TFT_HEIGHT 320

#define TFT_PARALLEL_8_BIT
#define TFT_D0 39
#define TFT_D1 40
#define TFT_D2 41
#define TFT_D3 42
#define TFT_D4 45
#define TFT_D5 46
#define TFT_D6 47
#define TFT_D7 48
#define TFT_WR 8
#define TFT_RD 9
#define TFT_CS 6
#define TFT_DC 7
#define TFT_RST 5

#define TFT_BL 38
#define TFT_BACKLIGHT_ON 1

#define LOAD_GLCD   // 8 px Adafruit font
#define LOAD_FONT2  // 16 px
#define LOAD_FONT4  // 26 px
#define LOAD_FONT6  // 48 px digits only
#define LOAD_FONT7  // 48 px 7-segment digits only
#define LOAD_FONT8  // 75 px digits only
#define LOAD_GFXFF
#define SMOOTH_FONT
