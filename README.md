RM67162 Driver for MicroPython
------------------------------
# Warning:
This project is not stable on MPY v1.21 and above. The issue lies in the qspi implementations. If you run your program the second time, the device will reset. The issue is currently unknown and I do not have enough time to resolve it in the near future. Sorry.

Contents:

- [RM67162 Driver for MicroPython](#rm67162-driver-for-microPython)
- [Introduction](#introduction)
- [Features](#features)
- [Documentation](#documentation) 
- [How to build](#build)

# Newer versio Lilygo AMOLED S3
According to [Issue#2](https://github.com/nspsck/RM67162_Micropython_QSPI/issues/2), apparently, you have to set `IO38` High for the display to work on newer versions. Huge thanks go to [dobodu](https://github.com/dobodu) to bring this up and [lewisxhe](https://github.com/lewisxhe) for providing the solution. 

# SPI Version
There is a SPI version of this firmware provided by [gampam2000](https://github.com/gampam2000/RM67162_Micropyton_SPI). Thank you for sharing your work!

## Introduction
This is the successor of the previous [lcd_binding_micropython](https://github.com/nspsck/lcd_binding_micropython). 
It is reconstructed to be more straightforward to develop on, and this allows me to test the changes before committing.

This driver is based on [esp_lcd](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/lcd.html).

Available functions: `fill, fill_rect, rect, fill_cirlce, cirlce, pixel, vline, hline, colorRGB, bitmap, brightness, line, text, write, write_len` etc.
For full details please visit [the documentation](#documentation).

All fonts are created by russhughes.

The firmware is provided each time when I update this repo. 

To-DO: (This is a lie. :c I hope I can find time to make this happen later in my life..)
- png support

## Features

The following display driver ICs are supported:
- Support for RM67162 displays

Supported boards：
- [LILYGO T-DisplayS3-AMOLED](https://github.com/Xinyuan-LilyGO/T-Display-S3-AMOLED)

| Driver IC | Hardware SPI     | Software SPI     | Hardware QSPI    | I8080            | DPI(RGB)         |
| --------- | ---------------- | ---------------- | ---------------- | ---------------- | ---------------- |
| ESP32-S3  | [supported](https://github.com/gampam2000/RM67162_Micropyton_SPI)  | [supported](https://github.com/gampam2000/RM67162_Micropyton_SPI)   | supported  | no support   | no support   |


## Documentation
In general, the screen starts at 0 and goes to 535 x 239, that's a total resolution of 536 x 240. All drawing functions should be called with this in mind.

- `rm67162.COLOR`

  This returns a predefined color that can be directly used for drawing. Available options are: BLACK, BLUE, RED, GREEN, CYAN, MAGENTA, YELLOW, WHITE

- `init()`

  Must be called to initialize the display.

- `deinit()`

  Deinit the tft object and release the memory used for the framebuffer.

- `reset()`

  Soft reset the display.

- `rotation(value)`

  Rotate the display, value range: 0 - 3.

- `brightness(value)`

  Set the screen brightness, value range: 0 - 100, in percentage.

- `disp_off()`

  Turn off the display.

- `disp_on()`

  Turn on the display.

- `backlight_on()`

  Turn on the backlight, this is equal to `brightness(100)`.

- `backlight_off()`

  Turn off the backlight, this is equal to `brightness(0)`.

- `invert_color()`

  Invert the display color.

- `height()`

  Returns the height of the display.

- `width()`

  Returns the width of the display.

- `colorRGB(r, g, b)`

  Call this function to get the rgb color for the drawing.

- `pixel(x, y, color)`

  Draw a single pixel at the position (x, y) with color.

- `hline(x, y, l, color)`

  Draw a horizontal line starting at the position (x, y) with color and length l. 

- `vline(x, y, l, color)`

  Draw a vertical line starting at the position (x, y) with color and length l.

  - `line(x0, y0, x1, y1, color)`

  Draw a line (not anti-aliased) from (x0, y0) to (x1, y1) with color.

- `fill(color)`

  Fill the entire screen with the color.

- `fill_rect(x, y, w, h, color)`

  Draw a rectangle starting from (x, y) with the width w and height h and fill it with the color.

- `rect(x, y, w, h, color)`

  Draw a rectangle starting from (x, y) with the width w and height h of the color.

  - `fill_bubble_rect(x, y, w, h, color)`

  Draw a rounded text-bubble-like rectangle starting from (x, y) with the width w and height h and fill it with the color.

- `bubble_rect(x, y, w, h, color)`

  Draw a rounded text-bubble-like rectangle starting from (x, y) with the width w and height h of the color.

- `fill_circle(x, y, r, color)`

  Draw a circle with the middle point (x, y) with the radius r and fill it with the color.

- `circle(x, y, r, color)`

  Draw a circle with the middle point (x, y) with the radius r of the color.

- `bitmap(x0, y0, x1, y1, buf)`

  Bitmap the content of a bytearray buf filled with color565 values starting from (x0, y0) to (x1, y1). Currently, the user is responsible for the provided buf content.

- `text(font, text, x, y, fg_color, bg_color)`

  Write text using bitmap fonts starting at (x, y) using foreground color `fg_color` and background color `bg_color`.

- `write(bitmap_font, s, x, y[, fg, bg, background_tuple, fill_flag])`
  Write text to the display using the specified proportional or Monospace bitmap font module with the coordinates as the upper-left corner of the text. The foreground and background colors of the text can be set by the optional arguments `fg` and `bg`, otherwise the foreground color defaults to `WHITE` and the background color defaults to `BLACK`.

  The `font2bitmap` utility creates compatible 1 bit per pixel bitmap modules from Proportional or Monospaced True Type fonts. The character size, foreground, background colors, and characters in the bitmap module may be specified as parameters. Use the -h option for details. If you specify a buffer_size during the display initialization, it must be large enough to hold the widest character (HEIGHT * MAX_WIDTH * 2).

  For more information please visit: [https://github.com/nspsck/st7735s_WeAct_Studio_TFT_port/tree/main](https://github.com/nspsck/st7735s_WeAct_Studio_TFT_port/tree/main)

- `write_len(bitap_font, s)`
  Returns the string's width in pixels if printed in the specified font.

## Related Repositories

- [framebuf-plus](https://github.com/lbuque/framebuf-plus)


## Build
This is only for reference. Since esp-idf v5.0.2, you must state the full path to the cmake file in order for the builder to find it.
```Shell
cd ~
git clone https://github.com/nspsck/RM67162_Micropython_QSPI.git

# to the micropython directory
cd micropython/port/esp32
make BOARD_VARIANT=SPIRAM_OCT  BOARD=ESP32_GENERIC_S3 USER_C_MODULES=~/RM67162_Micropython_QSPI/micropython.cmake
```
You may also want to modify the `sdkconfig` before building in case to get the 16MB storage.
```Shell
cd micropython/port/esp32
# use the editor you prefer
vim boards/ESP32_GENERIC_S3/sdkconfig.board 
```
Change it to:
```Shell
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESPTOOLPY_AFTER_NORESET=y

CONFIG_ESPTOOLPY_FLASHSIZE_4MB=
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions-16MiB.csv"
```
If the esp_lcd related functions are missing, do the following:
```Shell
cd micropython/port/esp32
# use the editor you prefer
vim esp32_common.cmake
```
Jump to line 105, or where ever `APPEND IDF_COMPONENTS` is located, add `esp_lcd` to the list should fix this.
