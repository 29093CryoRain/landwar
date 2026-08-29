#!/usr/bin/env python3
"""explore_arch33434_l5l12.py — 推导 arch_33434 L5/L12 形状。"""
import json, math, sys
sys.path.insert(0, 'tools')
from derive_city_shapes import *

tab = load_spec('arch_33434', 'data/tiling_specs_arch.json')
edges = build_edges(tab)
B = len(tab['cells'])
wx, hy = tab['wx'], tab['hy']

vm_all = {}
def vertex_cells(p):
    key = (round(p[0]*1e4), round(p[1]*1e4))
    if key in vm_all: return vm_all[key]
    out = []
    for b in range(B):
        for r in range(-2, 3):
            for c in range(-2, 3):
                ox = c*wx + r*tab['hx']; oy = c*tab['wy'] + r*tab['hy']
                for v in tab['cells'][b]['v']:
                    if same((v[0]+ox, v[1]+oy), p):
                        out.append((b, r, c))
    vm_all[key] = out
    return out

def cell_center_abs(cc):
    b, r, c = cc
    cell = tab['cells'][b]
    return (cell['cx'] + c*wx + r*tab['hx'], cell['cy'] + c*tab['wy'] + r*tab['hy'])

# ---- L5: 顶点周围 5 格, 左右对称, 上下不对称 ----
print('== L5 候选（左右对称）==')
seen = set()
l5_verts = []
for b in range(B):
    for k, p in enumerate(tab['cells'][b]['v']):
        key = (round(p[0]*1e4), round(p[1]*1e4))
        if key in seen: continue
        seen.add(key)
        vc = vertex_cells(p)
        if len(vc) != 5: continue
        # 左右对称：反射 x -> 2*vx - x 后仍是同一格集合
        def refl(cc):
            b0, r0, c0 = cc
            cx0, cy0 = cell_center_abs(cc)
            # 求对称点 (2*vx - cx0, cy0) 属于哪个格
            q = (2*p[0] - cx0, cy0)
            return wrap_center(tab, q)
        ok = True
        for cc in vc:
            rcc = refl(cc)
            if rcc not in [cell_key(*x) for x in vc]:
                ok = False; break
        if ok:
            l5_verts.append((p, vc))
            print('V(%s) cells=%s' % ([round(x,3) for x in p], [(cc[0],cc[1],cc[2]) for cc in vc]))
print('count:', len(l5_verts))

# ---- L12: 两个有公共边的三角, + 所有与两者有公共顶点的格 ----
print('== L12 ==')
# 找所有边邻三角对
pairs = set()
for b in range(B):
    if tab['cells'][b]['n'] != 3: continue
    for k, e in enumerate(edges[b]):
        if e and tab['cells'][e[0]]['n'] == 3:
            nb = e[0]
            pair = tuple(sorted([(b, 0, 0), (nb, e[1], e[2])], key=lambda cc: (cc[0], cc[1], cc[2])))
            pairs.add(pair)
print('triangle edge pairs:', len(pairs))
seen = set()
for pair in sorted(pairs):
    (b1, r1, c1), (b2, r2, c2) = pair
    # 两三角的顶点集合
    verts = set()
    for (bb, rr, cc) in [(b1, r1, c1), (b2, r2, c2)]:
        cell = tab['cells'][bb]
        for v in cell['v']:
            verts.add((round((v[0] + cc*wx + rr*tab['hx'])*1e4), round((v[1] + cc*tab['wy'] + rr*tab['hy'])*1e4)))
    # 所有与这些顶点有公共顶点的格
    cells_set = set()
    for p in verts:
        for cc in vertex_cells((p[0]/1e4, p[1]/1e4)):
            cells_set.add(cell_key(*cc))
    if len(cells_set) != 12: continue
    types = sorted(tab['cells'][cc[0]]['n'] for cc in cells_set)
    if types != [3]*8 + [4]*4: continue
    # 左右对称 且 上下对称
    def sym_ok(cset, vx, vy):
        for cc in cset:
            cx0, cy0 = cell_center_abs(cc)
            q = (2*vx - cx0, 2*vy - cy0)
            rcc = wrap_center(tab, q)
            if cell_key(*rcc) not in cset: return False
        return True
    # 形状中心：两三角公共边的中点
    c1a = cell_center_abs((b1, r1, c1)); c2a = cell_center_abs((b2, r2, c2))
    mx, my = (c1a[0]+c2a[0])/2, (c1a[1]+c2a[1])/2
    if sym_ok(cells_set, mx, my):
        key2 = frozenset(cells_set)
        if key2 in seen: continue
        seen.add(key2)
        print('L12 shape cells=%s center=(%.3f,%.3f)' % (sorted(cells_set), mx, my))
print('total unique L12:', len(seen))
