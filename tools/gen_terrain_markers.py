#!/usr/bin/env python3
"""gen_terrain_markers.py — 生成 P5 山地线条贴图 data/mountain.png。

简笔画（线条）风格，白底透明（tint 管线 Multiply 加载后按势力色着色：山→近黑、
城→势力色略深，见 render/MapRenderer）。与 data/ring.png/arrow.png 同一条管线。
4 倍超采样绘制后 LANCZOS 降采样 → 抗锯齿细线在 15px 格内也清晰。

用法（项目根 new_project_landwar/ 下执行）:
    python tools/gen_terrain_markers.py
"""

from PIL import Image, ImageDraw

SIZE = 128
SS = 4  # 超采样倍率
WHITE = (255, 255, 255, 255)


def make_mountain():
    """两座叠放的三角形山峰 + 雪线 + 山脚基线（线条风格）。"""
    img = Image.new("RGBA", (SIZE * SS, SIZE * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    s = SS
    # 后山（右）：三角形轮廓
    d.line([(50 * s, 104 * s), (80 * s, 42 * s), (106 * s, 104 * s)],
           fill=WHITE, width=11 * s, joint="curve")
    # 前山（左）：三角形轮廓
    d.line([(22 * s, 104 * s), (58 * s, 30 * s), (94 * s, 104 * s)],
           fill=WHITE, width=12 * s, joint="curve")
    # 前山雪线：峰顶下方一小段折线
    d.line([(52 * s, 48 * s), (58 * s, 42 * s), (64 * s, 50 * s)],
           fill=WHITE, width=7 * s)
    # 山脚基线
    d.line([(18 * s, 104 * s), (110 * s, 104 * s)], fill=WHITE, width=8 * s)
    return img.resize((SIZE, SIZE), Image.LANCZOS)


def main():
    make_mountain().save("data/mountain.png")
    print("wrote data/mountain.png")


if __name__ == "__main__":
    main()
