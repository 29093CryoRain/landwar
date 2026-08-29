#!/usr/bin/env python3
"""explore_laves33434.py — laves_33434 几何与 L1/L4/L12 形状。"""
import json, math, sys
sys.path.insert(0, 'tools')
from derive_city_shapes import *

tab = load_spec('laves_33434', 'data/tiling_specs_laves.json')
edges = build_edges(tab)
B = len(tab['cells'])
wx, wy, hx, hy = tab['wx'], tab['wy'], tab['hx'], tab['hy']

def cell_center_abs(cc):
    b, r, c = cc
    cell = tab['cells'][b]
    return (cell['cx'] + c*wx + r*hx, cell['cy'] + c*wy + r*hy)

# 短边 (a) 位置与朝向
print('== 短边朝向与左右对称 ==')
for b in range(B):
    cell = tab['cells'][b]
    lens = [math.hypot(cell['v'][(k+1)%5][0]-cell['v'][k][0], cell['v'][(k+1)%5][1]-cell['v'][k][1]) for k in range(5)]
    short_k = [k for k, l in enumerate(lens) if l < 0.7]
    for k in short_k:
        a = cell['v'][k]; bb = cell['v'][(k+1)%5]
        dx, dy = a[0]-bb[0], a[1]-bb[1]
        orient = 'H' if abs(dy) < 1e-4 else 'V' if abs(dx) < 1e-4 else '?'
    # 左右对称: 竖直对称轴 (短边水平) => 短边水平
    print('b=%d short_k=%s orient=%s' % (b, short_k, orient))

# L4: 短边两端点附近的所有五边形 (共 4 个)
print()
print('== L4: 短边周围 4 五边形 ==')
vm = defaultdict(list)
for b in range(B):
    for r in range(-2, 3):
        for c in range(-2, 3):
            ox = c*wx + r*hx; oy = c*wy + r*hy
            for v in tab['cells'][b]['v']:
                key = (round((v[0]+ox)*1e4), round((v[1]+oy)*1e4))
                vm[key].append((b, r, c))

def vert_cells(p):
    return vm.get((round(p[0]*1e4), round(p[1]*1e4)), [])

seen4 = set()
for b in range(B):
    cell = tab['cells'][b]
    lens = [math.hypot(cell['v'][(k+1)%5][0]-cell['v'][k][0], cell['v'][(k+1)%5][1]-cell['v'][k][1]) for k in range(5)]
    short_k = [k for k, l in enumerate(lens) if l < 0.7]
    for k in short_k:
        p = cell['v'][k]; q = cell['v'][(k+1)%5]
        cells_p = vert_cells(p); cells_q = vert_cells(q)
        union = uniq(list(cells_p) + list(cells_q))
        if len(union) != 4: 
            print('  b=%d short edge k=%d -> %d cells (not 4)' % (b, k, len(union)))
            continue
        key4 = frozenset(cell_key(*cc) for cc in union)
        if key4 in seen4: continue
        seen4.add(key4)
        a = cell['v'][k]; bb = cell['v'][(k+1)%5]
        dx, dy = a[0]-bb[0], a[1]-bb[1]
        orient = 'H' if abs(dy) < 1e-4 else 'V' if abs(dx) < 1e-4 else '?'
        print('  L4 (short edge %s, anchor base %d): cells=%s' % (orient, b, [(cc[0],cc[1],cc[2]) for cc in union]))
print('unique L4:', len(seen4))
