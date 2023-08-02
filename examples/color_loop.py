import tft_config
import framebuf
import time

def hsv2rgb(hue, sat, val):
    '''The conversion algorithm comes from https://blog.csdn.net/lly_3485390095/article/details/104570885'''
    C = 0.0
    X = 0.0
    Y = 0.0
    Z = 0.0
    i = 0
    H = float(hue)
    S = sat / 100.0
    V = val / 100.0
    if int(S) == 0:
        return int(V*255), int(V*255), int(V*255)
    else:
        H = H / 60
        i = int(H)
        C = H - i

        X = V * (1 - S)
        Y = V * (1 - S * C)
        Z = V * (1 -S * (1 - C))
        if i == 0:
            return int(V * 255), int(Z * 255), int(X * 255)
        elif i == 1:
            return int(Y * 255), int(V * 255), int(X * 255)
        elif i == 2:
            return int(X * 255), int(V * 255), int(Z * 255)
        elif i == 3:
            return int(X * 255), int(Y * 255), int(V * 255)
        elif i == 4:
            return int(Z * 255), int(X * 255), int(V * 255)
        elif i == 5:
            return int(V * 255), int(X * 255), int(Y * 255)


def hsv_wheel():
    while True:
        for i in range(0, 360):
            yield hsv2rgb(i, 255, 100)

"""
The delay is required to achieve the animation effect, since the
new driver is way too fast at drawing.

"""

def main():
    tft = tft_config.config()
    tft.reset()
    tft.init()
    tft.rotation(1)
    x = tft.width()
    y = tft.height()
    speed = 1
    delay = speed/1000
    buf = bytearray(x * speed * 2)
    fbuf = framebuf.FrameBuffer(buf, x, speed, framebuf.RGB565)
    color = hsv_wheel()
    start_time = time.ticks_ms()
    count = 0
    while True:
        r, g, b = next(color)
        fbuf.fill(tft.colorRGB(r, g, b))
        for j in range(0, y, speed):
            tft.bitmap(0, j, x, j + speed, buf)
            time.sleep(delay)
            count += 1
        if y % speed != 0:
            tft.bitmap(
                0,
                y - y % speed,
                x,
                y,
                buf
            )
            time.sleep(delay)
            count += 1
        if time.ticks_ms() - start_time >= 1000:
            print("Operations per second: %d" % count)
            count = 0
            start_time = time.ticks_ms()

main()
