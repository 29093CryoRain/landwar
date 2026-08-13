#!/usr/bin/env python3
# gen_army_sheet.py — 从 data/army.png 生成两张**彩色**兵表（不再灰度化！）：
#   data/army_base.png   （32×256，1 列 × 8 行）：源图列 0（势力1 红）原样 —— 基础版，
#                          运行时用色相旋转 tint 到各势力色（精确复现原图"色相处理"）。
#   data/army_special.png（32×256，1 列 × 8 行）：签名兵种特殊版（青先锋/蓝开拓/绿激光/
#                          橙爆炸/紫地雷），原样彩色，由所属势力直接使用（不再 tint）。
# 背景：army.png 各势力列是同一基础版的**色相旋转**（max/min 通道恒定），灰度 luma 会压暗且
# 破坏跨列亮度一致 → 改用 hue 旋转 tint；特殊版是独立绘制，保持原色。
# 改贴图只改本脚本 + army.png。
from PIL import Image

SRC = "data/army.png"
BASE = "data/army_base.png"
SPECIAL = "data/army_special.png"
CELL = 32
ROWS = 8
# 行 → 源图列（特殊版来源）。行：0 normal, 1 vanguard, 2 pioneer, 3 laser, 4 bomb, 5 mine,
# 6 火花, 7 光束。
SPECIAL_SRC_COL = {1: 2, 2: 3, 3: 4, 4: 5, 5: 6}


def cell(img, col, row):
    return img.crop((col * CELL, row * CELL, col * CELL + CELL, row * CELL + CELL))


def main():
    src = Image.open(SRC).convert("RGBA")
    base = Image.new("RGBA", (CELL, ROWS * CELL), (0, 0, 0, 0))
    special = Image.new("RGBA", (CELL, ROWS * CELL), (0, 0, 0, 0))
    for row in range(ROWS):
        base.paste(cell(src, 0, row), (0, row * CELL))  # 基础 = 源列 0（红）
        if row in SPECIAL_SRC_COL:
            special.paste(cell(src, SPECIAL_SRC_COL[row], row), (0, row * CELL))
        # else：该行无特殊版 → 留空
    base.save(BASE)
    special.save(SPECIAL)
    print(f"wrote {BASE} ({CELL}x{ROWS * CELL}) + {SPECIAL} ({CELL}x{ROWS * CELL})")


if __name__ == "__main__":
    main()
