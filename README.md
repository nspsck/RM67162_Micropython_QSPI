RM67162 Driver for MicroPython
------------------------------

Contents:

- [RM67162 Driver for MicroPython](#rm67162-driver-for-microPython)
- [Introduction](#introduction)
- [Features](#features)
- [Documentation](#documentation)
- [How to build](#build)

## Introduction
This is the successor of the previous [lcd_binding_micropython](https://github.com/nspsck/lcd_binding_micropython). 
It is reconstructed to be more straightforward to develop on, and this allows me to test the changes before committing.

This driver is based on [esp_lcd](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/lcd.html).

Available functions: `fill, fill_rect, rect, fill_cirlce, cirlce, pixel, vline, hline, colorRGB, bitmap, brightness, line, text` ect.
For full details please visit [the documentation](#documentation).

All fonts are created by russhughes.

The firmware is provided each time when I update this repo. 

To-DO:
- png support

## Features

The following display driver ICs are supported:
- Support for RM67162 displays

Supported boards：
- [LILYGO T-DisplayS3-AMOLED](https://github.com/Xinyuan-LilyGO/T-Display-S3-AMOLED)

| Driver IC | Hardware SPI     | Software SPI     | Hardware QSPI    | I8080            | DPI(RGB)         |
| --------- | ---------------- | ---------------- | ---------------- | ---------------- | ---------------- |
| ESP32-S3  | ![alt text][2]   | ![alt text][2]   | ![alt text][1]   | ![alt text][2]   | ![alt text][2]   |

[1]: https://camo.githubusercontent.com/bd5f5f82b920744ff961517942e99a46699fee58737cd9b31bf56e5ca41b781b/68747470733a2f2f696d672e736869656c64732e696f2f62616467652f2d737570706f727465642d677265656e
[2]: https://img.shields.io/badge/-not%20support-lightgrey
[3]: https://img.shields.io/badge/-untested-red
[4]: https://img.shields.io/badge/-todo-blue

## Documentation
In general, the screen starts at 0, and goes to 535 x 239, that's a total resolution of 536 x 240. All drawing functions should be called with this in mind.

- `rm67162.COLOR`

  This returns a predifined color that can be directly used for drawing. Available options are: BLACK, BLUE, RED, GREEN, CYAN, MAGENTA, YELLOW, WHITE

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

  Turn on backlight, this is equal to `brightness(100)`.

- `backlight_off()`

  Turn off backlight, this is equal to `brightness(0)`.

- `invert_color()`

  Invert the display color.

- `height()`

  Returns the height of the display.

- `width()`

  Returns the width of the display.

- `colorRGB(r, g, b)`

  Call this function to get the rgb color for the drawing.

- `pixel(x, y, color)`

  Draw a single pixel at the postion (x, y) with color.

- `hline(x, y, l, color)`

  Draw a horizontal line starting at the postion (x, y) with color and length l. 

- `vline(x, y, l, color)`

  Draw a vertical line starting at the postion (x, y) with color and length l.

  - `line(x0, y0, x1, y1, color)`

  Draw a line (not anti-aliazed) from (x0, y0) to (x1, y1) with color.

- `fill(color)`

  Fill the entire screen with the color.

- `fill_rect(x, y, w, h, color)`

  Draw a rectangle starting from (x, y) with the width w and height h and fill it with the color.

- `rect(x, y, w, h, color)`

  Draw a rectangle starting from (x, y) with the width w and height h of the color.

- `fill_circle(x, y, r, color)`

  Draw a circle with the middle point (x, y) with the radius r and fill it with the color.

- `circle(x, y, r, color)`

  Draw a circle with the middle point (x, y) with the radius r of the color.

- `bitmap(x0, y0, x1, y1, buf)`

  Bitmap the content of a bytearray buf filled with color565 values starting from (x0, y0) to (x1, y1). Currently, the user is resposible for the provided buf content.

- `text(font, text, x, y, fg_color, bg_color)`

  Write text using bitmap fonts starting at (x, y) using foreground color `fg_color` and background color `bg_color`.


## Related Repositories

- [framebuf-plus](https://github.com/lbuque/framebuf-plus)


## Build
This is only for reference. Since esp-idf v5.0.2, you must state the full path to the cmake file in order for the builder to find it.
```Shell
cd ~
git clone https://github.com/nspsck/RM67162_Micropython_QSPI.git

# to the micropython directory
cd micropython/port/esp32
make BOARD=GENERIC_S3_SPIRAM_OCT USER_C_MODULES=~/RM67162_Micropython_QSPI/micropython.cmake
```
You may also want to modify the `sdkconfig` before building in case to get the 16MB storage.
```Shell
cd micropython/port/esp32
# use the editor you prefer
vim boards/GENERIC_S3_SPIRAM_OCT/sdkconfig.board 
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
If the esp_lcd related functions are missing, do following:
```Shell
cd micropython/port/esp32
# use the editor you prefer
vim esp32_common.cmake
```
Jump to line 105, or where ever `APPEND IDF_COMPONENTS` is located, add `esp_lcd` to the list should fixe this.
