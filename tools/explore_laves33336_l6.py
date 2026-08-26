#!/usr/bin/env python3
"""explore_laves33336_l6.py — L6 花瓣与 L3 两半。"""
import json, math, sys
sys.path.insert(0, 'tools')
from derive_city_shapes import *

tab = load_spec('laves_33336', 'data/tiling_specs_laves.json')
edges = build_edges(tab)
B = len(tab['cells'])
wx, wy, hx, hy = tab['wx'], tab['wy'], tab['hx'], tab['hy']

def cell_center_abs(cc):
    b, r, c = cc
    cell = tab['cells'][b]
    return (cell['cx'] + c*wx + r*hx, cell['cy'] + c*wy + r*hy)

# 找 6-valent 顶点 (6 个五边形共顶点)
vm = defaultdict(list)
for b in range(B):
    for r in range(-3, 4):
        for c in range(-3, 4):
            ox = c*wx + r*hx; oy = c*wy + r*hy
            for v in tab['cells'][b]['v']:
                key = (round((v[0]+ox)*1e4), round((v[1]+oy)*1e4))
                vm[key].append((b, r, c))

six = {}
for key, cells in vm.items():
    if len(cells) == 6:
        six[key] = cells
print('6-valent vertices:', len(six))
# 打印一个
for key, cells in list(six.items())[:4]:
    p = (key[0]/1e4, key[1]/1e4)
    print('V(%s) cells=%s' % ([round(x,3) for x in p], [(cc[0],cc[1],cc[2]) for cc in cells]))
    # 各格中心相对顶点的角度
    for cc in sorted(cells, key=lambda cc: math.atan2(cell_center_abs(cc)[1]-p[1], cell_center_abs(cc)[0]-p[0])):
        cx0, cy0 = cell_center_abs(cc)
        print('   b=%d ang=%.1f' % (cc[0], math.degrees(math.atan2(cy0-p[1], cx0-p[0]))))
    # 上下分割: y > vy 为上半, y < vy 为下半
    vy = p[1]
    upper = [cc for cc in cells if cell_center_abs(cc)[1] > vy]
    lower = [cc for cc in cells if cell_center_abs(cc)[1] < vy]
    print('   upper:', [(cc[0],cc[1],cc[2]) for cc in upper])
    print('   lower:', [(cc[0],cc[1],cc[2]) for cc in lower])
