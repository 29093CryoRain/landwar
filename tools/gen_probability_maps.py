#!/usr/bin/env python3
"""gen_probability_maps.py — 把旧编码地图等效转换为"地形基图"（RGB 概率权重）编码。

旧编码（P5 亮度带，见 gen_golden_map.py 旧版）：
  - 全黑 (0,0,0) → 海；否则 → 陆，brightness = (b+g+r)/3。
  - 山：brightness∈[1,89]（确定性）；城概率：brightness>99 → pow(1.040909, b-100)/500。

新编码（地形基图，src/world/Map.cpp 解析）：
  - 任一分量 < 32 → 海（确定性）；否则 → 陆。
  - 山概率 = ramp(R) = clamp((R-128)/127,0,1)；城概率 = ramp(G)；B 闲置。
  - 每陆格独立掷骰：先 rng.chance(ramp(G)) 定城、后 rng.chance(ramp(R)) 定山。

等效映射（概率等价保留）：
  海 (0,0,0)                  → (0,0,0)
  陆 brightness∈[1,89]（旧山）→ (255,128,128)   # 100% 山、g=128 无城 → 逐格一致
  陆 brightness∈[90,99]       → (128,128,128)   # 纯平原（r=g=128 → ramp=0）
  陆 brightness≥100           → (128, round(128+127*chance), 128)  # 城概率=旧公式 → 统计等价

用法（项目根 new_project_landwar/ 下执行）：
    python tools/gen_probability_maps.py            # 转换 6 张图（5 基础图 + map_mountain）
    python tools/gen_probability_maps.py --plain    # 只转海陆、把城概率归零（供完全重绘）
    python tools/gen_probability_maps.py --dir data/legacy_old_encoding/  # 从备份目录转换
原图先备份到 data/legacy_old_encoding/（再覆盖写回）。
"""

import argparse
import math
import os
import shutil

W, H = 105, 95
SEA_BLACK = (0, 0, 0)
PLAIN = (128, 128, 128)
MOUNTAIN = (255, 128, 128)

# 只转旧编码基础图。注意：不含 map_mountains.bmp（用户手绘的新编码基图，勿转换）；
# 也不含已删除的 map_mountain.bmp（旧演示图，如需可先从 legacy_old_encoding/ 恢复再转）。
MAPS = [
    "map_bigIslands.bmp",
    "map_mainland.bmp",
    "map_twolands.bmp",
    "map_desert.bmp",
    "map_lakes.bmp",
]


def old_city_chance(brightness):
    """旧城市概率：brightness<=99 → 0；否则 pow(1.040909, b-100)/500。"""
    if brightness <= 99:
        return 0.0
    return (1.040909 ** (brightness - 100)) / 500.0


def read_bmp(path):
    with open(path, "rb") as f:
        data = f.read()
    assert len(data) >= 54, f"{path}: header truncated"
    rowsize = (W * 3 + 3) // 4 * 4  # 4 字节对齐行填充
    pixels = []  # row-major，自底向上（j 行 = 图像底部行）
    for j in range(H):
        base = 54 + j * rowsize
        for i in range(W):
            b, g, r = data[base + i * 3: base + i * 3 + 3]
            pixels.append((r, g, b))
    return pixels


def convert(old, plain_only):
    """旧像素 → 新像素（概率等价保留）。"""
    r, g, b = old
    if r == 0 and g == 0 and b == 0:
        return SEA_BLACK  # 海
    brightness = (r + g + b) // 3
    if 1 <= brightness <= 89:
        return MOUNTAIN  # 旧山（确定性 → 100%）
    if plain_only:
        return PLAIN  # 只转海陆、城概率归零
    chance = min(1.0, old_city_chance(brightness))
    gc = min(255, round(128 + 127 * chance)) if brightness >= 100 else 128
    return (128, gc, 128)


def write_bmp(path, new_pixels):
    """写 24bit BMP：54 头 + 每行 4 字节对齐填充 + BGR + 自底向上。"""
    rowsize = (W * 3 + 3) // 4 * 4
    pad = bytes(rowsize - W * 3)
    header = bytearray(54)
    header[0:2] = b"BM"
    header[2:6] = (54 + H * rowsize).to_bytes(4, "little")
    header[10:14] = (54).to_bytes(4, "little")
    header[14:18] = (40).to_bytes(4, "little")
    header[18:22] = (W).to_bytes(4, "little", signed=True)
    header[22:26] = (H).to_bytes(4, "little", signed=True)
    header[26:28] = (1).to_bytes(2, "little")
    header[28:30] = (24).to_bytes(2, "little")
    header[34:38] = (W * H * 3).to_bytes(4, "little")  # 原始位数据大小
    body = bytearray()
    for j in range(H):  # 自底向上：j 行写图像底部行
        for i in range(W):
            r, g, b = new_pixels[j * W + i]
            body += bytes((b, g, r))
        body += pad
    with open(path, "wb") as f:
        f.write(bytes(header) + bytes(body))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plain", action="store_true", help="只转海陆、把城概率归零")
    ap.add_argument("--dir", default="data", help="旧图目录（默认 data/）")
    args = ap.parse_args()

    legacy = os.path.join(args.dir, "legacy_old_encoding")
    os.makedirs(legacy, exist_ok=True)
    for name in MAPS:
        src = os.path.join(args.dir, name)
        if not os.path.exists(src):
            print(f"skip {name} (not found)")
            continue
        shutil.copy2(src, os.path.join(legacy, name))  # 备份原图
        old = read_bmp(src)
        new = [convert(p, args.plain) for p in old]
        write_bmp(src, new)
        # 统计：海/陆/山/带城概率的格数
        sea = sum(1 for p in new if p == SEA_BLACK)
        mtn = sum(1 for p in new if p == MOUNTAIN)
        city = sum(1 for p in new if p[1] >= 128 and p != SEA_BLACK)
        print(f"{name}: sea={sea} land={len(new)-sea} mountain={mtn} cityprob={city} -> {src}")
    print("originals backed up to", legacy)


if __name__ == "__main__":
    main()
