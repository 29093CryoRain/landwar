#!/usr/bin/env python3
"""explore_laves33434_l12.py — laves_33434 L12 推导。"""
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

def edge_is_long(tab, b, k):
    cell = tab['cells'][b]
    p, q = cell['v'][k], cell['v'][(k+1) % cell['n']]
    return math.hypot(q[0]-p[0], q[1]-p[1]) > 0.7

def b_neighbors(cc):
    """cc 的 b 边 (长边) 邻格 (含方向)。"""
    b, r, c = cc
    out = []
    for k, e in enumerate(edges[b]):
        if e and edge_is_long(tab, b, k):
            nb, dr, dc = e
            out.append((nb, r+dr, c+dc))
    return out

# L4 变体1 (V): 形状 {0,2,4,6} 绕 base 0 的竖直短边
# 参考帧 anchor = base 0 (V 短边); 形状格 = base 0 + 其他 3
# 用 anchor base 0 的短边端点
cell = tab['cells'][0]
lens = [math.hypot(cell['v'][(k+1)%5][0]-cell['v'][k][0], cell['v'][(k+1)%5][1]-cell['v'][k][1]) for k in range(5)]
short_k = [k for k, l in enumerate(lens) if l < 0.7][0]
# L4-V 形状格: 两个端点的点邻并集
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

p0 = cell['v'][short_k]; p1 = cell['v'][(short_k+1)%5]
l4v = uniq(list(vert_cells(p0)) + list(vert_cells(p1)))
print('L4-V cells:', [(cc[0],cc[1],cc[2]) for cc in l4v])

# L12-V: L4 + 长边邻
l12v = set(cell_key(*cc) for cc in l4v)
for cc in l4v:
    for ncc in b_neighbors(cc):
        l12v.add(cell_key(*ncc))
print('L12-V count:', len(l12v), 'cells:', sorted(l12v))

# 同理 L4-H / L12-H (anchor base 4, 水平短边)
cell4 = tab['cells'][4]
lens4 = [math.hypot(cell4['v'][(k+1)%5][0]-cell4['v'][k][0], cell4['v'][(k+1)%5][1]-cell4['v'][k][1]) for k in range(5)]
short_k4 = [k for k, l in enumerate(lens4) if l < 0.7][0]
q0 = cell4['v'][short_k4]; q1 = cell4['v'][(short_k4+1)%5]
l4h = uniq(list(vert_cells(q0)) + list(vert_cells(q1)))
print('L4-H cells:', [(cc[0],cc[1],cc[2]) for cc in l4h])
l12h = set(cell_key(*cc) for cc in l4h)
for cc in l4h:
    for ncc in b_neighbors(cc):
        l12h.add(cell_key(*ncc))
print('L12-H count:', len(l12h), 'cells:', sorted(l12h))

# L1 mask: 左右对称 = 竖直短边? 不 — 左右对称 = 竖直对称轴 = 短边水平 = bases 4,5,6,7
print('L1 mask bases (短边水平):', [b for b in range(B) if [k for k, l in enumerate([math.hypot(tab['cells'][b]['v'][(k+1)%5][0]-tab['cells'][b]['v'][k][0], tab['cells'][b]['v'][(k+1)%5][1]-tab['cells'][b]['v'][k][1]) for k in range(5)]) if l < 0.7][0] in (0, 2) ])
