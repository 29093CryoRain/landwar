# -*- coding: utf-8 -*-
"""临时核对：把 city_shapes.json 的 arch_4612 形状偏移叠加到 tiling_specs 块上。"""
import json

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

spec = json.load(open("data/tiling_specs_arch.json", encoding="utf-8"))["arch_4612"]
shapes = json.load(open("data/city_shapes.json", encoding="utf-8"))["arch_4612"]

cells = spec["cells"]
B = len(cells)

fig, axes = plt.subplots(1, len(shapes["shapes"]), figsize=(6 * len(shapes["shapes"]), 6))
axes = [axes] if len(shapes["shapes"]) == 1 else axes
for ax, sh in zip(axes, shapes["shapes"]):
    # 中央块多边形（浅色）
    for i, c in enumerate(cells):
        xs = [v[0] for v in c["v"]] + [c["v"][0][0]]
        ys = [v[1] for v in c["v"]] + [c["v"][0][1]]
        ax.fill(xs, ys, alpha=0.15)
        ax.annotate(str(i), (c["cx"], c["cy"]), ha="center", va="center", fontsize=8)
    # 环绕一圈平移的格中心（context）
    for dc in (-1, 0, 1):
        for dr in (-1, 0, 1):
            if dc == 0 and dr == 0:
                continue
            ox = dc * spec["W"][0] + dr * spec["H"][0]
            oy = dc * spec["W"][1] + dr * spec["H"][1]
            ax.scatter([c["cx"] + ox for c in cells], [c["cy"] + oy for c in cells],
                       s=4, color="gray", alpha=0.4, zorder=1)
    # 形状偏移点（世界坐标，锚在原点）→ 叠加到每个同边数基础格上
    pts_x, pts_y = [p[0] for p in sh["cells"]], [p[1] for p in sh["cells"]]
    anchors = []
    for i, c in enumerate(cells):
        # 锚候选：与形状第一格边数一致（用 spec 的 n 字段近似原 anchorN 语义）
        pass
    # 对块内每个基础格都试画：只画与锚重合落在已知格中心的（解析成功）情况太复杂，
    # 这里直接把形状画在“与其首格重合”的锚上：找使全部偏移命中格中心的平移。
    import itertools
    placed = False
    for i, c in enumerate(cells):
        for dx, dy in [(c["cx"], c["cy"])]:
            hit = 0
            for px, py in zip(pts_x, pts_y):
                wx, wy = dx + px, dy + py
                for cc in cells:
                    for ddc in range(-2, 3):
                        for ddr in range(-2, 3):
                            ox = ddc * spec["W"][0] + ddr * spec["H"][0]
                            oy = ddc * spec["W"][1] + ddr * spec["H"][1]
                            if abs(cc["cx"] + ox - wx) < 1e-6 and abs(cc["cy"] + oy - wy) < 1e-6:
                                hit += 1
                                break
                        else:
                            continue
                        break
                    else:
                        continue
                    break
            if hit == len(pts_x):
                anchors.append(i)
                ax.scatter([dx + px for px in pts_x], [dy + py for py in pts_y],
                           s=60, color="red", zorder=5)
                ax.scatter([dx], [dy], marker="x", color="black", s=100, zorder=6)
                placed = True
                break
        if placed:
            break
    ax.set_title("shape lvl=%s  %d格  anchor=%s%s"
                 % (sh["level"], len(pts_x),
                    anchors[0] if anchors else "?", "" if anchors else " 无完全命中"))
    ax.set_aspect("equal")
plt.tight_layout()
plt.savefig("tools/blocks/arch_4612_shapes_overlay.png", dpi=110)
print("saved tools/blocks/arch_4612_shapes_overlay.png")
