#!/usr/bin/env python3
"""explore_arch33336.py — 推导 arch_33336 城市形状并验证。"""
import json, math, sys
sys.path.insert(0, 'tools')
from derive_city_shapes import *

tab = load_spec('arch_33336', 'data/tiling_specs_arch.json')
edges = build_edges(tab)
B = len(tab['cells'])

def tri_orient(b):
    v = tab['cells'][b]['v']
    for k in range(3):
        p, q = v[k], v[(k+1)%3]
        if abs(p[1]-q[1]) < 1e-4:
            apex = v[(k+2)%3]
            midy = (p[1]+q[1])/2
            return 'UP' if apex[1] > midy else 'DOWN'
    return '?'

# 每三角的水平边邻
def horiz_neighbor(b):
    v = tab['cells'][b]['v']
    for k in range(3):
        p, q = v[k], v[(k+1)%3]
        if abs(p[1]-q[1]) < 1e-4:
            e = edges[b][k]
            return e
    return None

print('== L1: 水平边邻三角 ==')
l1_mask = 0
for b in range(B):
    if tab['cells'][b]['n'] != 3: continue
    e = horiz_neighbor(b)
    if e is not None and tab['cells'][e[0]]['n'] == 3:
        l1_mask |= (1 << b)
        o = tri_orient(b); no = tri_orient(e[0])
        if o == no: print('  !! same orientation pair b=%d(%s) nb=%d(%s)' % (b, o, e[0], no))
print('l1_mask = 0x%X (bases: %s)' % (l1_mask, [i for i in range(B) if l1_mask & (1 << i)]))

# L3: 大三角形 (4 小三角: 中心 + 3 边邻全三角)
print('== L3: 4 三角大三角形 ==')
l3_bases = []
for b in range(B):
    if tab['cells'][b]['n'] != 3: continue
    nbs = [e[0] for e in edges[b] if e and tab['cells'][e[0]]['n'] == 3]
    if len(nbs) == 3:
        l3_bases.append(b)
print('center bases:', l3_bases)

# L5/L9: 六边形
print('== L5/L9: 六边形 ==')
vm_all = {}
def vertex_cells(p):
    key = (round(p[0]*1e4), round(p[1]*1e4))
    if key in vm_all: return vm_all[key]
    out = []
    for b in range(B):
        for r in range(-3, 4):
            for c in range(-3, 4):
                ox = c*tab['wx'] + r*tab['hx']; oy = c*tab['wy'] + r*tab['hy']
                for v in tab['cells'][b]['v']:
                    if same((v[0]+ox, v[1]+oy), p):
                        out.append((b, r, c))
    vm_all[key] = out
    return out

for b in range(B):
    if tab['cells'][b]['n'] != 6: continue
    cell = tab['cells'][b]
    v = cell['v']
    print('hex b=%d center=(%.4f,%.4f)' % (b, cell['cx'], cell['cy']))
    # 六边形顶点按 x 排序：最左/最右
    xs = sorted(range(6), key=lambda k: v[k][0])
    left = v[xs[0]]; right = v[xs[-1]]
    lc = vertex_cells(left); rc = vertex_cells(right)
    lt = [cc for cc in lc if tab['cells'][cc[0]]['n'] == 3]
    rt = [cc for cc in rc if tab['cells'][cc[0]]['n'] == 3]
    print('  left vertex (%s) tris: %s' % ([round(x,3) for x in left], [(cc[0], cc[1], cc[2]) for cc in lt]))
    print('  right vertex (%s) tris: %s' % ([round(x,3) for x in right], [(cc[0], cc[1], cc[2]) for cc in rt]))
    # 上下三角 (水平边邻)
    for k in range(6):
        p, q = v[k], v[(k+1)%6]
        if abs(p[1]-q[1]) < 1e-4:
            e = edges[b][k]
            if e and tab['cells'][e[0]]['n'] == 3:
                print('  %s edge (y=%.3f) neighbor: base %d (%s)' % ('top' if p[1] > cell['cy'] else 'bottom', p[1], e[0], tri_orient(e[0])))

# 平移周期 W/H 语义检查: hex 16 的顶点桶内三角数
print('== 顶点 valence ==')
for b in [16]:
    for k, p in enumerate(tab['cells'][b]['v']):
        vc = vertex_cells(p)
        print('  hex16 v%d (%s) cells=%d types=%s' % (k, [round(x,3) for x in p], len(vc), sorted(set(tab['cells'][cc[0]]['n'] for cc in vc))))
