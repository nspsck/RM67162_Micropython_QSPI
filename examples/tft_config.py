import rm67162
from machine import Pin, SPI

def config():
    hspi = SPI(2, sck=Pin(47), mosi=None, miso=None, polarity=0, phase=0)
    panel = rm67162.QSPIPanel(
        spi=hspi,
        data=(Pin(18), Pin(7), Pin(48), Pin(5)),
        dc=Pin(7),
        cs=Pin(6),
        pclk=80 * 1000 * 1000,
        width=240,
        height=536
    )
    return rm67162.RM67162(panel, reset=Pin(17), bpp=16)


def color565(r, g, b):
    c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)
    return (c >> 8) | (c << 8)
