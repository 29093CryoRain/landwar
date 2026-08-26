#!/usr/bin/env python3
"""explore_arch33434.py — 推导 arch_33434 城市形状并验证。"""
import json, math, sys
sys.path.insert(0, 'tools')
from derive_city_shapes import *

tab = load_spec('arch_33434', 'data/tiling_specs_arch.json')
edges = build_edges(tab)
B = len(tab['cells'])

def tri_orient(b):
    v = tab['cells'][b]['v']
    # up/down: 有水平边；left/right: 有竖直边
    for k in range(3):
        p, q = v[k], v[(k+1)%3]
        if abs(p[1]-q[1]) < 1e-4:
            apex = v[(k+2)%3]
            midy = (p[1]+q[1])/2
            return 'UP' if apex[1] > midy else 'DOWN'
    for k in range(3):
        p, q = v[k], v[(k+1)%3]
        if abs(p[0]-q[0]) < 1e-4:
            apex = v[(k+2)%3]
            midx = (p[0]+q[0])/2
            return 'RIGHT' if apex[0] > midx else 'LEFT'
    return '?'

print('== 三角朝向 ==')
for b in range(B):
    if tab['cells'][b]['n'] == 3:
        print('b=%d %s' % (b, tri_orient(b)))

print('== L1 mask: 上下三角 ==')
mask_updown = 0
for b in range(B):
    if tab['cells'][b]['n'] == 3 and tri_orient(b) in ('UP', 'DOWN'):
        mask_updown |= (1 << b)
print('mask = 0x%X bases=%s' % (mask_updown, [i for i in range(B) if mask_updown & (1 << i)]))

print('== L2: 正方形 ==')
for b in range(B):
    if tab['cells'][b]['n'] == 4:
        print('b=%d square center=(%.4f,%.4f)' % (b, tab['cells'][b]['cx'], tab['cells'][b]['cy']))

print('== L5: 顶点周围 5 图形 (3三角+2方) ==')
vm_all = {}
def vertex_cells(p):
    key = (round(p[0]*1e4), round(p[1]*1e4))
    if key in vm_all: return vm_all[key]
    out = []
    for b in range(B):
        for r in range(-2, 3):
            for c in range(-2, 3):
                ox = c*tab['wx'] + r*tab['hx']; oy = c*tab['wy'] + r*tab['hy']
                for v in tab['cells'][b]['v']:
                    if same((v[0]+ox, v[1]+oy), p):
                        out.append((b, r, c))
    vm_all[key] = out
    return out

seen_verts = set()
count = 0
for b in range(B):
    for k, p in enumerate(tab['cells'][b]['v']):
        key = (round(p[0]*1e4), round(p[1]*1e4))
        if key in seen_verts: continue
        seen_verts.add(key)
        vc = vertex_cells(p)
        types = [tab['cells'][cc[0]]['n'] for cc in vc]
        if len(vc) == 5:
            count += 1
            print('V(%s) cells=%s types=%s' % ([round(x,3) for x in p], [(cc[0],cc[1],cc[2]) for cc in vc], sorted(types)))
print('total 5-cell vertices:', count)
