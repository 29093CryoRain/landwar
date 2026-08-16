#!/usr/bin/env python3
"""分析 Laves 密铺各基础格朝向，确定"锚定方向"所需的基础格位掩码。

需求（开发思路.txt）：
- Laves3464 L1: "一个筝形,但只能是左右对称的" → 锚格须左右对称
- Laves3636 L1: "一个长对角线是水平的, 短对角线是竖直的菱形" → 锚格长对角线水平
- Laves31212 L2: "两个等腰三角形底边为公共边组成的菱形"（底边水平）→ 锚格底边水平
- Laves4612 L2: "两个三角形斜边为公共边, 斜边需为竖直方向" → 锚格斜边竖直

方法：对每个基础格 b，检查其形状是否满足方向条件（相对运行时坐标）。
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
    for name in ['laves_3464', 'laves_3636', 'laves_31212', 'laves_4612']:
        tab, cells = load(name, 'data/tiling_specs_laves.json')
        print('==== %s (B=%d) ====' % (name, len(cells)))
        for b in range(len(cells)):
            c = cells[b]
            n = c['n']
            # 顶点相对中心
            verts = [(p[0] - c['cx'], p[1] - c['cy']) for p in c['v']]
            # 边（方向、长度）
            edges = []
            for k in range(n):
                A = verts[k]
                Bv = verts[(k + 1) % n]
                dx, dy = Bv[0] - A[0], Bv[1] - A[1]
                ln = math.sqrt(dx * dx + dy * dy)
                ang = math.degrees(math.atan2(dy, dx)) % 180.0
                if ang > 90.0:
                    ang -= 180.0
                edges.append((round(ln, 4), round(ang, 1)))
            if n == 4:  # 筝形/菱形
                # 对角线
                d1 = ((verts[0][0] - verts[2][0]) ** 2 + (verts[0][1] - verts[2][1]) ** 2) ** 0.5
                d2 = ((verts[1][0] - verts[3][0]) ** 2 + (verts[1][1] - verts[3][1]) ** 2) ** 0.5
                d1ang = math.degrees(math.atan2(verts[2][1] - verts[0][1], verts[2][0] - verts[0][0])) % 180
                if d1ang > 90: d1ang -= 180
                d2ang = math.degrees(math.atan2(verts[3][1] - verts[1][1], verts[3][0] - verts[1][0])) % 180
                if d2ang > 90: d2ang -= 180
                print('  b=%d 边=%s 对角线=(%.4f@%.0f°, %.4f@%.0f°)' %
                      (b, edges, d1, d1ang, d2, d2ang))
            else:
                print('  b=%d 边=%s' % (b, edges))


if __name__ == '__main__':
    main()
