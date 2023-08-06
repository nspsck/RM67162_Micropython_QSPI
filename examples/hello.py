"""
hello.py

    Writes "Hello!" in random colors at random locations on the display.
"""

import random
import utime
import rm67162
import tft_config
import vga2_bold_16x32 as font

tft = tft_config.config()

def center(text):
    length = 1 if isinstance(text, int) else len(text)
    tft.text(
        font,
        text,
        (tft.width() - length * font.WIDTH) // 2,
        (tft.height() - font.HEIGHT ) // 2,
        rm67162.WHITE,
        rm67162.RED)

def main():
    tft.reset()
    tft.init()
    tft.rotation(1)
    tft.fill(rm67162.RED)
    center(b'\xAEHello\xAF')
    utime.sleep(2)
    tft.fill(rm67162.BLACK)

    while True:
        for rotation in range(4):
            tft.rotation(rotation)
            tft.fill(0)
            col_max = tft.width() - font.WIDTH*6
            row_max = tft.height() - font.HEIGHT

            for _ in range(128):
                tft.text(
                    font,
                    b'Hello!',
                    random.randint(0, col_max),
                    random.randint(0, row_max),
                    tft.colorRGB(
                        random.getrandbits(8),
                        random.getrandbits(8),
                        random.getrandbits(8)),
                    tft.colorRGB(
                        random.getrandbits(8),
                        random.getrandbits(8),
                        random.getrandbits(8)))
                utime.sleep(0.005)


main()
