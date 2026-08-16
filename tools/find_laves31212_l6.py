#!/usr/bin/env python3
"""完整构造 Laves31212 城市形状（按开发思路.txt 语义）。

几何关键（已验证）：
- 锚格 b=0 = 等腰三角形（底边水平在上 = 菱形长对角线，顶角朝下）。
- 顶角（rel=(2.6321,0)，即下方）是 3-valent：3 三角形共点 → L3。
- 底边端点（rel=(3.9482,0.7598)/(1.3161,0.7598)）是 12-valent → L12 围绕它们。
- 6-valent 顶点（6 三角形共点）→ L6 六边形中心。但锚格不在 6-valent 顶点处。

Laves31212 = Laves3636 菱形沿长对角线分割。锚格 b=0 是"菱形上半"：
- L2 = 锚 + 共享底边的另一半 = 完整菱形（= Laves3636 L1）✓ 已有
- L3 = 顶角 3 三角形共点 = 等边三角形
- L6 = 大六边形：中心在 6-valent 顶点。哪个 6-valent 顶点离锚最近？
- L6(菱形) = 两个 L3 拼菱形（公共边水平）
- L12 = 12-valent 顶点周围全部 12 三角形 = 大雪花（中心 = 底边端点）

策略：锚 = 参考格 b=0（等腰三角形）。L3/L6/L12 都以锚为中心构造：
- L3: 顶角处 3 格 ✓
- L12: 底边端点（12-valent）处全部 12 格？但锚不是端点…… 文档 L12 = "像 Laves3636 6级城，大雪花"。
  Laves3636 L6 = 顶点附近全部 6 菱形 = 12 三角形。即 12-valent 顶点周围 12 三角形。
  但以锚为中心时，锚的顶角 3-valent、底角 12-valent。锚格本身 + 底角 12 格？
  
重新读文档 Laves31212：
  2: 菱形（Laves3636 L1）
  3: 三三角形顶角共点 = 等边三角形
  6: 像 Laves3636 L3 的大六边形（正反）
  6: 两个 3 级城一正一反拼菱形（公共边水平）
  12: 像 Laves3636 L6 的大雪花

Laves3636 L3 = 顶点附近 3 菱形 = 六边形。在 Laves31212 中，菱形的顶点 = 等腰三角形的底角（12-valent）？不对。
Laves3636 顶点 = 3 菱形共点（120°×3=360°）。分割后每菱形 2 三角形，顶点处 6 三角形 → 6-valent。
所以 Laves3636 L3（3 菱形）→ Laves31212 的 6-valent 顶点周围 6 三角形 = 六边形 = L6！
而 Laves3636 L6（6 菱形）→ Laves31212 的 12-valent 顶点（12 三角形）= L12 雪花。

锚格 b=0 是等腰三角形。它的顶角是 3-valent（不是 6-valent），底角是 12-valent。
那 6-valent 顶点在哪？锚格的对偶顶点？Laves3636 顶点对应 Laves31212 的 6-valent。
锚格 b=0 的顶角（3-valent）= 原三角形顶角。底角（12-valent）= 原菱形顶点（Laves3636 顶点）。
矛盾：Laves3636 顶点 3 菱形 = 6 三角形，应是 6-valent，但这里底角 12-valent。

啊，我明白了！Laves31212 的三角形不是"菱形沿长对角线分成两半"这么简单。看数据：
- 顶角 3-valent：3 个三角形顶角共点，每角 120°。
- 底角 12-valent：12 个三角形底角共点，每角 30°。
三角形内角和 = 120+30+30 = 180 ✓。所以等腰三角形顶角 120°、底角 30°。
菱形 = 2 三角形 = 120+120+60+60？不对。菱形 4 角 = 2 个顶角(120°)在两端 + 2 个底角合并？

重新理解：Laves31212 等腰三角形 a,a,b（a=2/∜3, b=2∜3）。顶角对边 b。
cos(顶角) = (a²+a²-b²)/(2a²) = (2a²-b²)/(2a²)。a²=4/√3, b²=4√3。
= (8/√3 - 4√3)/(8/√3) = (8-12)/8 = -0.5 → 顶角 120°。底角 30° ✓。

菱形 = 2 个这样的三角形沿底边拼（底边 = 菱形长对角线？）。菱形角 = 30+30=60（两端）和 120+120=240？不对，菱形角 < 180。
两个三角形沿底边 b 拼：底边两端各合并 2 个底角 30° → 菱形两端角 60°；两个顶角 120° 在上下 → 菱形上下角 120°。菱形 = 60/120/60/120 ✓ = Laves3636 菱形！

所以：Laves3636 菱形顶点（120° 角）= Laves31212 的顶角（3-valent，3×120°=360°）！
Laves3636 菱形顶点（60° 角）= Laves31212 的底角（12-valent，12×30°=360°）？

等等，Laves3636 菱形 120° 顶点处有几个菱形？3 个（3×120=360）。Laves31212 顶角 3-valent ✓ 对应。
Laves3636 菱形 60° 顶点处几个菱形？6 个（6×60=360）。Laves31212 底角 12-valent = 12 三角形 = 6 菱形 × 2 ✓！

所以：
- L3（3 三角形顶角共点）= Laves3636 的 120° 顶点（3 菱形）
- L6（6 三角形 6-valent 顶点）= Laves3636 的 60° 顶点（6 菱形）？不对，Laves3636 L3 = 3 菱形。
- L12（12 三角形 12-valent）= Laves3636 的 60° 顶点（6 菱形）→ Laves3636 L6 = 6 菱形 ✓！

所以：
- L6 = 6-valent 顶点周围 6 三角形（= Laves3636 的 120° 顶点的另一半？）不对……

重新理清：Laves3636 顶点两种：
- 120° 顶点：3 菱形共点。Laves3636 L3 = 顶点附近全部 3 菱形 = 这 3 个菱形。
  在 Laves31212 中 = 3 菱形 = 6 三角形，围绕该顶点（该顶点 = Laves31212 顶角 3-valent？）
  不对——Laves31212 顶角 3-valent = 3 三角形共点，不是 6。
  
我搞混了。让我直接看数据：锚格 b=0 顶角 3-valent（3 三角形）。这 3 个三角形 = L3 等边三角形。
锚格底角 12-valent（12 三角形）= L12 雪花。

那 6-valent 顶点呢？分布里有 19 个 6-valent 顶点。它们对应什么？可能是"菱形内部顶点"？
不，6-valent = 6 三角形共点。让我找离锚最近的 6-valent 顶点，看它周围 6 格是否构成六边形。
"""
import json
import math
from collections import defaultdict

TOL = 1e-6


def load(name, file):
    d = json.load(open(file, encoding='utf-8'))
    j = d[name]
    old_wx = j['W'][0]
    old_hy = j['H'][1]
    cells = []
    for c in j['cells']:
        nc = old_hy - c['cy']
        ny = c['cx']
        v = [[old_hy - p[1], p[0]] for p in c['v']]
        cells.append({'n': c['n'], 'cx': nc, 'cy': ny, 'v': v})
    return {'wx': old_hy, 'hx': 0.0, 'hy': old_wx}, cells


def main():
    tab, cells = load('laves_31212', 'data/tiling_specs_laves.json')
    B = len(cells)
    vert_map = defaultdict(list)
    for r in range(-4, 5):
        for c in range(-4, 5):
            for b in range(B):
                ox = c * tab['wx'] + r * tab['hx']
                oy = r * tab['hy']
                for p in cells[b]['v']:
                    key = (round((p[0] + ox) * 1e4), round((p[1] + oy) * 1e4))
                    vert_map[key].append((b, r, c))
    ax, ay = cells[0]['cx'], cells[0]['cy']
    # 找离锚格中心最近的 6-valent 顶点
    best = None
    bestd = 1e18
    for key, shared in vert_map.items():
        if len(shared) != 6:
            continue
        wx, wy = key[0] / 1e4, key[1] / 1e4
        d = (wx - ax) ** 2 + (wy - ay) ** 2
        if d < bestd:
            bestd = d
            best = (wx, wy, shared)
    wx, wy, shared = best
    print('最近 6-valent 顶点 rel=(%.4f, %.4f):' % (wx - ax, wy - ay))
    for cc in shared:
        b, r, c = cc
        cx0 = cells[b]['cx'] + c * tab['wx'] + r * tab['hx']
        cy0 = cells[b]['cy'] + r * tab['hy']
        print('  %s rel=(%.4f, %.4f)' % (cc, cx0 - ax, cy0 - ay))


if __name__ == '__main__':
    main()
