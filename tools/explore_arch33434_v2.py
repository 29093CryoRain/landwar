#!/usr/bin/env python3
"""explore_arch33434_v2.py — 用严格包含判定重做 arch_33434 L5/L12。"""
import json, math, sys
sys.path.insert(0, 'tools')
from derive_city_shapes import *

tab = load_spec('arch_33434', 'data/tiling_specs_arch.json')
edges = build_edges(tab)
B = len(tab['cells'])
wx, hy = tab['wx'], tab['hy']

def cell_poly_abs(cc):
    b, r, c = cc
    cell = tab['cells'][b]
    ox = c*wx + r*tab['hx']; oy = c*tab['wy'] + r*tab['hy']
    return [(v[0]+ox, v[1]+oy) for v in cell['v']]

def point_in_poly(poly, p):
    # 凸多边形包含 (含边)
    n = len(poly)
    sign = 0
    for i in range(n):
        a = poly[i]; b = poly[(i+1) % n]
        cross = (b[0]-a[0])*(p[1]-a[1]) - (b[1]-a[1])*(p[0]-a[0])
        if abs(cross) <= 1e-6: continue
        s = 1 if cross > 0 else -1
        if sign == 0: sign = s
        elif sign != s: return False
    return True

def find_cell(p):
    """找包含点 p 的格 (b,r,c)。"""
    best = None
    for b in range(B):
        for r in range(-2, 3):
            for c in range(-2, 3):
                if point_in_poly(cell_poly_abs((b, r, c)), p):
                    return (b, r, c)
    return best

def cell_center_abs(cc):
    b, r, c = cc
    cell = tab['cells'][b]
    return (cell['cx'] + c*wx + r*tab['hx'], cell['cy'] + c*tab['wy'] + r*tab['hy'])

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

# ---- L5：顶点周围 5 格, 左右对称 (严格) ----
print('== L5 严格左右对称 ==')
seen = set()
l5 = []
for b in range(B):
    for k, p in enumerate(tab['cells'][b]['v']):
        key = (round(p[0]*1e4), round(p[1]*1e4))
        if key in seen: continue
        seen.add(key)
        vc = vertex_cells(p)
        if len(vc) != 5: continue
        vset = set(cell_key(*cc) for cc in vc)
        # 左右对称: 每个格中心反射 (x->2vx-x) 后落入的格仍属于集合
        ok = True
        for cc in vc:
            cx0, cy0 = cell_center_abs(cc)
            rcc = find_cell((2*p[0]-cx0, cy0))
            if rcc is None or cell_key(*rcc) not in vset:
                ok = False; break
        # 上下不对称
        up_ok = True
        for cc in vc:
            cx0, cy0 = cell_center_abs(cc)
            rcc = find_cell((cx0, 2*p[1]-cy0))
            if rcc is None or cell_key(*rcc) not in vset:
                up_ok = False; break
        if ok and not up_ok:
            l5.append((p, vc))
            print('L5 V(%s) cells=%s' % ([round(x,3) for x in p], [(cc[0],cc[1],cc[2]) for cc in vc]))
print('count:', len(l5))

# 每个 L5 形状: 找锚点 (三角 or 方?) — 打印各格的相对角位置
def angles_from(cc, p):
    cx0, cy0 = cell_center_abs(cc)
    return math.atan2(cy0 - p[1], cx0 - p[0])

print()
print('== L5 形状详情 (相对顶点角) ==')
for p, vc in l5:
    print('V(%s):' % [round(x,3) for x in p])
    for cc in sorted(vc, key=lambda cc: angles_from(cc, p)):
        print('   b=%d type=%d at ang=%.1f deg' % (cc[0], tab['cells'][cc[0]]['n'], math.degrees(angles_from(cc, p))))

# ---- L12 ----
print()
print('== L12 ==')
pairs = set()
for b in range(B):
    if tab['cells'][b]['n'] != 3: continue
    for k, e in enumerate(edges[b]):
        if e and tab['cells'][e[0]]['n'] == 3:
            nb = e[0]
            pair = tuple(sorted([(b, 0, 0), (nb, e[1], e[2])], key=lambda cc: (cc[0], cc[1], cc[2])))
            pairs.add(pair)
seen12 = set()
for pair in sorted(pairs):
    (b1, r1, c1), (b2, r2, c2) = pair
    verts = set()
    for (bb, rr, cc) in [(b1, r1, c1), (b2, r2, c2)]:
        cell = tab['cells'][bb]
        for v in cell['v']:
            verts.add((v[0] + cc*wx + rr*tab['hx'], v[1] + cc*tab['wy'] + rr*tab['hy']))
    cells_set = set()
    for p in verts:
        for cc in vertex_cells(p):
            cells_set.add(cell_key(*cc))
    if len(cells_set) != 12: continue
    types = sorted(tab['cells'][cc[0]]['n'] for cc in cells_set)
    if types != [3]*8 + [4]*4: continue
    c1a = cell_center_abs((b1, r1, c1)); c2a = cell_center_abs((b2, r2, c2))
    mx, my = (c1a[0]+c2a[0])/2, (c1a[1]+c2a[1])/2
    def sym_ok(cset, cx_, cy_):
        for cc in cset:
            cx0, cy0 = cell_center_abs(cc)
            rcc = find_cell((2*cx_-cx0, 2*cy_-cy0))
            if rcc is None or cell_key(*rcc) not in cset: return False
        return True
    if sym_ok(cells_set, mx, my):
        key2 = frozenset(cells_set)
        if key2 in seen12: continue
        seen12.add(key2)
        print('L12 pair %s -> %s' % (pair, sorted(cells_set)))
        print('   pair tri types: b1=%d b2=%d, center=(%.3f,%.3f)' % (b1, b2, mx, my))
print('unique L12:', len(seen12))
