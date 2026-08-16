#!/usr/bin/env python3
"""分析 Laves31212 顶点价数分布：找 3/6/12-valent 顶点，定位 L3/L6/L12 的中心。"""
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
    # 统计价数分布
    val_counts = defaultdict(int)
    for key, shared in vert_map.items():
        val_counts[len(shared)] += 1
    print('价数分布 (价数 -> 顶点数):', dict(sorted(val_counts.items())))
    # 找锚格 b=0 附近的各价数顶点，输出共享格中心
    def cell_center(cc):
        b, r, c = cc
        return (cells[b]['cx'] + c * tab['wx'] + r * tab['hx'],
                cells[b]['cy'] + r * tab['hy'])
    ax, ay = cell_center((0, 0, 0))
    # 锚格 3 顶点
    for p in cells[0]['v']:
        key = (round(p[0] * 1e4), round(p[1] * 1e4))
        shared = vert_map.get(key, [])
        print('锚格顶点 rel=(%.4f,%.4f) 价数=%d:' % (p[0], p[1], len(shared)))
        for cc in shared:
            cx0, cy0 = cell_center(cc)
            print('   ', cc, 'rel=(%.4f, %.4f)' % (cx0 - ax, cy0 - ay))
    # 找 6-valent 顶点（L6 六边形中心）
    print('\n6-valent 顶点示例（世界坐标 rel 到锚格中心）:')
    for key, shared in vert_map.items():
        if len(shared) == 6:
            wx = key[0] / 1e4
            wy = key[1] / 1e4
            print('  顶点 (%.4f, %.4f) rel=(%.4f, %.4f)' % (wx, wy, wx - ax, wy - ay))
            for cc in shared:
                cx0, cy0 = cell_center(cc)
                print('    ', cc, 'rel=(%.4f, %.4f)' % (cx0 - ax, cy0 - ay))
            break


if __name__ == '__main__':
    main()
