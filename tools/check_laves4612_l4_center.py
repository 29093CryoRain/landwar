#!/usr/bin/env python3
"""验证 L4 变体1 的正菱形：中心点 (0.3102,-0.1791) 是否 4-valent 直角共点？
若成立，则 L4 变体1 与 L4b 变体2（直角共点）应为同一菱形，格子集合相同。
"""
import json
import math
from collections import defaultdict

TOL = 1e-6

d = json.load(open('data/tiling_specs_laves.json', encoding='utf-8'))
j = d['laves_4612']
old_wx, old_hy = j['W'][0], j['H'][1]
cells = []
for c in j['cells']:
    nc = old_hy - c['cy']
    ny = c['cx']
    v = [[old_hy - p[1], p[0]] for p in c['v']]
    cells.append({'n': c['n'], 'cx': nc, 'cy': ny, 'v': v})
tab = {'wx': old_hy, 'hx': 0.0, 'hy': old_wx}
B = len(cells)

vert_map = defaultdict(list)
for r in range(-8, 9):
    for c in range(-8, 9):
        for b in range(B):
            ox = c * tab['wx'] + r * tab['hx']
            oy = r * tab['hy']
            for p in cells[b]['v']:
                key = (round((p[0] + ox) * 1e4), round((p[1] + oy) * 1e4))
                vert_map[key].append((b, r, c))

ax, ay = cells[0]['cx'], cells[0]['cy']

# L4 变体1 的 4 格（运行时 rel）
l4_rt = [(-0.6204, -0.3582), (0.0, 0.0), (0.6204, 0.0), (1.2408, -0.3582)]

# 找每格在 b=0 邻域的实际格子（含周期平移）
def find_cell(wx, wy):
    best = None
    bd = 1e9
    for dr in range(-3, 4):
        for dc in range(-3, 4):
            for nb in range(B):
                cx0 = cells[nb]['cx'] + dc * tab['wx'] + dr * tab['hx']
                cy0 = cells[nb]['cy'] + dr * tab['hy']
                dd = (ax + wx - cx0) ** 2 + (ay + wy - cy0) ** 2
                if dd < bd:
                    bd = dd
                    best = (nb, dr, dc)
    return best

# 菱形中心 = 4 格中心平均
cxm = sum(x for x, y in l4_rt) / 4.0
cym = sum(y for x, y in l4_rt) / 4.0
print('L4 变体1 菱形中心 rel=(%.4f,%.4f)' % (cxm, cym))

# 检查中心是否为 4-valent 顶点
key = (round((ax + cxm) * 1e4), round((ay + cym) * 1e4))
shared = vert_map.get(key, [])
print('中心顶点价数 =', len(shared))
if shared:
    for cc in shared:
        b, r, c = cc
        print('  共享格 rel=(%.4f,%.4f)' % (cells[b]['cx'] + c*tab['wx'] + r*tab['hx'] - ax,
                                          cells[b]['cy'] + r*tab['hy'] - ay))

# 检查 L4 变体1 每格是否有顶点在中心
for wx, wy in l4_rt:
    nb, dr, dc = find_cell(wx, wy)
    has_center = False
    for p in cells[nb]['v']:
        if abs(p[0] + dc*tab['wx'] + dr*tab['hx'] - (ax + cxm)) < TOL and \
           abs(p[1] + dr*tab['hy'] - (ay + cym)) < TOL:
            has_center = True
    print('  格 rel=(%.4f,%.4f) 含中心顶点: %s' % (wx, wy, has_center))
