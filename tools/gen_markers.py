#!/usr/bin/env python3
# gen_markers.py — 生成玩家城市指示贴图 data/ring.png、data/arrow.png（128×128 白底透明）。
# 复刻 src/render/CityMarkerRenderer.cpp 的 makeRingTexture/makeArrowTexture 逐像素逻辑，
# 输出为可替换的美术资产（tint 管线加载后按势力色着色）。改贴图形状只改本脚本。
import math

from PIL import Image

SIZE = 128


def point_in_triangle(px, py, x0, y0, x1, y1, x2, y2):
    def sign(x, y, ax, ay, bx, by):
        return (x - bx) * (ay - by) - (ax - bx) * (y - by)

    d1 = sign(px, py, x0, y0, x1, y1)
    d2 = sign(px, py, x1, y1, x2, y2)
    d3 = sign(px, py, x2, y2, x0, y0)
    has_neg = (d1 < 0) or (d2 < 0) or (d3 < 0)
    has_pos = (d1 > 0) or (d2 > 0) or (d3 > 0)
    return not (has_neg and has_pos)


def make_ring():
    """虚线缺口环：内半径 42、外半径 52；开 22° 闭 8° 虚线；40° 缺口扇区。"""
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    px = img.load()
    c = SIZE / 2.0
    seg_on = math.radians(22.0)
    seg_off = math.radians(8.0)
    gap = math.radians(40.0)
    for y in range(SIZE):
        for x in range(SIZE):
            dx = x - c
            dy = y - c
            r = math.hypot(dx, dy)
            if r < 42.0 or r > 52.0:
                continue
            a = math.atan2(dy, dx)
            if a < 0:
                a += 2.0 * math.pi
            on = (a % (seg_on + seg_off)) < seg_on
            in_gap = a < gap
            if on and not in_gap:
                px[x, y] = (255, 255, 255, 255)
    return img


def make_arrow():
    """四个箭头（上/下/左/右）指向中心：长度 20、半宽 15、尖端距中心 22。"""
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    px = img.load()
    c = SIZE / 2.0
    k_len, k_half, k_off = 20.0, 15.0, 22.0
    tris = [
        (c - k_half, c - k_off - k_len, c + k_half, c - k_off - k_len, c, c - k_off),
        (c - k_half, c + k_off + k_len, c + k_half, c + k_off + k_len, c, c + k_off),
        (c - k_off - k_len, c - k_half, c - k_off - k_len, c + k_half, c - k_off, c),
        (c + k_off + k_len, c - k_half, c + k_off + k_len, c + k_half, c + k_off, c),
    ]
    for y in range(SIZE):
        for x in range(SIZE):
            for tri in tris:
                if point_in_triangle(x, y, *tri):
                    px[x, y] = (255, 255, 255, 255)
                    break
    return img


def main():
    make_ring().save("data/ring.png")
    make_arrow().save("data/arrow.png")
    print("wrote data/ring.png, data/arrow.png")


if __name__ == "__main__":
    main()
