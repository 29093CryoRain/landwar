#!/usr/bin/env python3
"""explore_laves33336.py — laves_33336 五边形几何与 L1/L3/L6 形状。"""
import json, math, sys
sys.path.insert(0, 'tools')
from derive_city_shapes import *

tab = load_spec('laves_33336', 'data/tiling_specs_laves.json')
edges = build_edges(tab)
B = len(tab['cells'])
wx, wy, hx, hy = tab['wx'], tab['wy'], tab['hx'], tab['hy']

def edge_lens(b):
    cell = tab['cells'][b]
    out = []
    for k in range(cell['n']):
        p, q = cell['v'][k], cell['v'][(k+1) % cell['n']]
        out.append((k, math.hypot(q[0]-p[0], q[1]-p[1])))
    return out

# 分类: 长边 (b) 与短边 (a)
print('== 边分类 ==')
for b in range(B):
    lens = edge_lens(b)
    ls = sorted(set(round(l, 4) for _, l in lens))
    print('b=%d lens=%s' % (b, ls))

# 长边角顶点 (两长边公共端点) 与对称轴方向
print()
print('== 对称轴方向 ==')
for b in range(B):
    cell = tab['cells'][b]
    lens = [math.hypot(cell['v'][(k+1)%5][0]-cell['v'][k][0], cell['v'][(k+1)%5][1]-cell['v'][k][1]) for k in range(5)]
    long_k = [k for k, l in enumerate(lens) if l > 0.7]
    # 长边角顶点 = long_k 两边的公共顶点 = 长边 [k1,k2] 的公共端点
    k1, k2 = long_k
    # 边 k 从 v[k] 到 v[k+1]
    # 两长边公共顶点
    vk1_end = (k1 + 1) % 5  # 边 k1 的终点
    if vk1_end == k2:  # 边 k1 终点 = 边 k2 起点
        corner = k2
        d1 = (cell['v'][corner][0]-cell['v'][(corner+1)%5][0], cell['v'][corner][1]-cell['v'][(corner+1)%5][1])  # 入边方向
    else:
        corner = vk1_end
        d1 = (cell['v'][corner][0]-cell['v'][(corner-1)%5][0], cell['v'][corner][1]-cell['v'][(corner-1)%5][1])
    # 角平分线方向: 两条长边方向的单位向量之和
    p = cell['v'][corner]
    q1 = cell['v'][(corner+1) % 5]; q2 = cell['v'][(corner-1) % 5]
    u1 = (q1[0]-p[0], q1[1]-p[1]); u2 = (q2[0]-p[0], q2[1]-p[1])
    l1 = math.hypot(*u1); l2 = math.hypot(*u2)
    u1 = (u1[0]/l1, u1[1]/l1); u2 = (u2[0]/l2, u2[1]/l2)
    axis = (u1[0]+u2[0], u1[1]+u2[1])
    axis_ang = math.degrees(math.atan2(axis[1], axis[0]))
    # 长边是否水平/竖直
    def horiz_or_vert(k):
        a = cell['v'][k]; b_ = cell['v'][(k+1)%5]
        dx, dy = abs(a[0]-b_[0]), abs(a[1]-b_[1])
        return ('H' if dy < 1e-4 else 'V' if dx < 1e-4 else '')
    long_orient = [horiz_or_vert(k) for k in long_k]
    print('b=%d long_k=%s corner=v%d axis=%.1f deg long_orient=%s' % (b, long_k, corner, axis_ang, long_orient))
