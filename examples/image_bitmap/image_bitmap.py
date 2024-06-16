import tft_config
import logo

def main():
    tft = tft_config.config()
    tft.reset()
    tft.init()
    tft.rotation(1)
    # From 0 to logo.WIDTH - 1 are logo.WIDTH pixels in total.
    tft.bitmap(0, 0, logo.WIDTH - 1, logo.HEIGHT - 1, logo.BITMAP)

main()
