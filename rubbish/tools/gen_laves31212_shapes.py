#!/usr/bin/env python3
"""构造 Laves31212 全部城市形状并验证。

几何事实（已确认）：
- 锚格 b=0 顶角（rel=(2.6321,0) 下方）= 3-valent：3 三角形共点 → L3 等边三角形
- 锚格 b=0 底边端点 = 12-valent：12 三角形共点 → L12 雪花
- 6-valent 顶点（6 三角形共点）→ L6 大六边形；但 b=0 不在 6-valent 顶点，
  因此 L6 锚定在 6-valent 顶点处的某个基础格。

文档形状（Laves31212）：
- L2: 两等腰三角形底边公共 = 菱形（= Laves3636 L1）
- L3: 三三角形顶角共点 = 等边三角形（正反两种）
- L6: 像 Laves3636 L3 的大六边形（正反两种）
- L6: 两个 L3 一正一反拼菱形（公共边水平）
- L12: 像 Laves3636 L6 的大雪花

实现：以锚格为原点，用"顶点共点"和"边邻接"枚举形状格集合，输出 Config 偏移。
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


def build_neighbors(tab, cells):
    B = len(cells)
    edges = []
    for b in range(B):
        cell = cells[b]
        n = cell['n']
        be = []
        for k in range(n):
            A = cell['v'][k]
            Bv = cell['v'][(k + 1) % n]
            found = False
            for dr in range(-2, 3):
                for dc in range(-2, 3):
                    ox = dc * tab['wx'] + dr * tab['hx']
                    oy = dr * tab['hy']
                    for nb in range(B):
                        ocell = cells[nb]
                        for kk in range(ocell['n']):
                            n0 = [ocell['v'][kk][0] + ox, ocell['v'][kk][1] + oy]
                            n1 = [ocell['v'][(kk + 1) % ocell['n']][0] + ox,
                                  ocell['v'][(kk + 1) % ocell['n']][1] + oy]
                            if (n0[0] - Bv[0]) ** 2 + (n0[1] - Bv[1]) ** 2 <= TOL * TOL and \
                               (n1[0] - A[0]) ** 2 + (n1[1] - A[1]) ** 2 <= TOL * TOL:
                                be.append((nb, dr, dc))
                                found = True
                                break
                        if found:
                            break
                    if found:
                        break
                if found:
                    break
            if not found:
                be.append(None)
        edges.append(be)
    return edges


def main():
    tab, cells = load('laves_31212', 'data/tiling_specs_laves.json')
    edges = build_neighbors(tab, cells)
    B = len(cells)

    # 顶点图
    vert_map = defaultdict(list)
    for r in range(-5, 6):
        for c in range(-5, 6):
            for b in range(B):
                ox = c * tab['wx'] + r * tab['hx']
                oy = r * tab['hy']
                for p in cells[b]['v']:
                    key = (round((p[0] + ox) * 1e4), round((p[1] + oy) * 1e4))
                    vert_map[key].append((b, r, c))

    def cell_center(cc):
        b, r, c = cc
        return (cells[b]['cx'] + c * tab['wx'] + r * tab['hx'],
                cells[b]['cy'] + r * tab['hy'])

    def rel(cc, anchor_center):
        cx0, cy0 = cell_center(cc)
        return (cx0 - anchor_center[0], cy0 - anchor_center[1])

    def neighbors(cc):
        b, r, c = cc
        out = []
        for e in edges[b]:
            if e is None:
                continue
            nb, dr, dc = e
            out.append((nb, r + dr, c + dc))
        return out

    def to_config(runtime_offsets):
        out = []
        for rx, ry in runtime_offsets:
            out.append((round(ry, 12), round(-rx, 12)))
        seen = set()
        result = []
        for o in out:
            if o in seen:
                continue
            seen.add(o)
            result.append(o)
        return result

    def fmt(offsets):
        return '{' + ', '.join('{%.12f, %.12f, 0}' % o for o in offsets) + '}'

    # ---- L2: 锚 + 底边对侧（共享底边的格） ----
    anchor_center = cell_center((0, 0, 0))
    l2 = [(0, 0, 0)]
    # 底边 = 锚格水平边（y 相同的两个端点）；共享该边的格 = 边邻接中的对应格
    # 锚格 b=0 的底边对侧格 = 共享底边的邻格。从边邻接看，b=0 的 3 个邻格中，
    # 哪个与锚共享"水平底边"？锚底边 = (3.9482,0.7598)-(1.3161,0.7598)。
    # 检查各邻格的边：b=3 的多边形含 (3.9482,0.7598),(1.3161,0.7598) → 共享底边 ✓
    l2.append((3, 0, 0))
    print('L2 cells:', sorted(l2))
    print('  runtime:', [tuple(round(x, 4) for x in rel(cc, anchor_center)) for cc in l2])
    print('  config:', fmt(to_config([rel(cc, anchor_center) for cc in l2])))

    # ---- L3: 锚顶角（3-valent）周围 3 三角形 ----
    bottom_vert = min(cells[0]['v'], key=lambda p: p[1])
    apex_key = (round(bottom_vert[0] * 1e4), round(bottom_vert[1] * 1e4))
    l3 = sorted(vert_map.get(apex_key, []), key=lambda cc: (cc[1], cc[2], cc[0]))
    print('L3 cells (顶角 3-valent):', l3)
    print('  runtime:', [tuple(round(x, 4) for x in rel(cc, anchor_center)) for cc in l3])
    print('  config:', fmt(to_config([rel(cc, anchor_center) for cc in l3])))

    # ---- L6 六边形（正反两种）：6-valent 顶点周围 6 三角形 ----
    # 找所有 6-valent 顶点，输出周围 6 格（含锚格的可能）
    six = []
    for key, shared in vert_map.items():
        if len(shared) == 6:
            wx, wy = key[0] / 1e4, key[1] / 1e4
            # 与锚格距离
            d = (wx - anchor_center[0]) ** 2 + (wy - anchor_center[1]) ** 2
            six.append((d, wx, wy, shared))
    six.sort()
    print('\n最近的 6-valent 顶点:')
    for d, wx, wy, shared in six[:3]:
        print('  rel=(%.4f,%.4f) 周围 6 格:' % (wx - anchor_center[0], wy - anchor_center[1]))
        for cc in shared:
            print('    %s rel=(%.4f, %.4f)' % (cc, *tuple(round(x, 4) for x in rel(cc, anchor_center))))

    # ---- L12 雪花：12-valent 顶点（锚底边端点）周围 12 三角形 ----
    # 锚格 b=0 底边右端点
    right_end = max(cells[0]['v'], key=lambda p: (p[0], p[1]))
    rkey = (round(right_end[0] * 1e4), round(right_end[1] * 1e4))
    l12 = sorted(vert_map.get(rkey, []), key=lambda cc: (cc[1], cc[2], cc[0]))
    print('\nL12 cells (底边端点 12-valent):', len(l12))
    print('  runtime:', [tuple(round(x, 4) for x in rel(cc, anchor_center)) for cc in l12])
    print('  config:', fmt(to_config([rel(cc, anchor_center) for cc in l12])))


if __name__ == '__main__':
    main()
