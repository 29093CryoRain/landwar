#!/usr/bin/env python3
"""derive_laves4612.py — 按 .docs/异种地图开发思路.txt 语义重新推导 laves_4612 城市形状。

与 4 种扭棱密铺同一机制（anchorN + mask + 参考帧偏移），输出 Config.cpp 片段并做
全锚格运行时管线验证（复用 verify_snub_shapes 的 resolve）。
"""
import json
import math
import sys
sys.path.insert(0, 'tools')
from derive_city_shapes import *

tab = load_spec('laves_4612', 'data/tiling_specs_laves.json')
edges = build_edges(tab)
B = len(tab['cells'])
wx, wy, hx, hy = tab['wx'], tab['wy'], tab['hx'], tab['hy']

SHORT = math.sqrt(2.0 / math.sqrt(3.0))   # 短直角边 x ≈ 1.0746
LONG = SHORT * math.sqrt(3.0)             # 长直角边 z ≈ 1.8608
HYP = 2 * SHORT                           # 斜边 y ≈ 2.1491

def edge_class(b, k):
    cell = tab['cells'][b]
    p, q = cell['v'][k], cell['v'][(k + 1) % 3]
    L = math.hypot(q[0] - p[0], q[1] - p[1])
    if abs(L - SHORT) < 1e-3:
        return 'x'
    if abs(L - LONG) < 1e-3:
        return 'z'
    return 'y'

def cabs(cc):
    b, r, c = cc
    cell = tab['cells'][b]
    return (cell['cx'] + c * wx + r * hx, cell['cy'] + c * wy + r * hy)

def vert_cells(p, radius=3):
    out = []
    for b in range(B):
        for r in range(-radius, radius + 1):
            for c in range(-radius, radius + 1):
                ox = c * wx + r * hx
                oy = c * wy + r * hy
                for v in tab['cells'][b]['v']:
                    if same((v[0] + ox, v[1] + oy), p):
                        out.append((b, r, c))
                        break
    return out

def corner(b, cls):
    """边类 cls 的对角顶点（该边两端外的顶点）。"""
    cell = tab['cells'][b]
    k = [k for k in range(3) if edge_class(b, k) == cls][0]
    return cell['v'][(k + 2) % 3]

def edge_neighbors(cc_list):
    """各格三边邻（去重）。"""
    out = set()
    for b, r, c in cc_list:
        for k in range(3):
            e = edges[b][k]
            if e:
                out.add(cell_key(e[0], r + e[1], c + e[2]))
    return out

# ---- 竖直斜边基础格（L2/L4a 锚定）----
vert_bases = []
for b in range(B):
    k = [k for k in range(3) if edge_class(b, k) == 'y'][0]
    p, q = tab['cells'][b]['v'][k], tab['cells'][b]['v'][(k + 1) % 3]
    if abs(p[0] - q[0]) < 1e-4:
        vert_bases.append(b)
vert_mask = 0
for b in vert_bases:
    vert_mask |= (1 << b)
print('竖直斜边基础格:', vert_bases, 'mask=0x%x' % vert_mask)
ref_l2 = min(vert_bases)   # base 1
refc_l2 = cabs((ref_l2, 0, 0))

# ---- L2: 两三角共竖直斜边（筝形）----
k_y = [k for k in range(3) if edge_class(ref_l2, k) == 'y'][0]
partner = edges[ref_l2][k_y]
l2 = [(ref_l2, 0, 0), partner]

# ---- L4a: L2 + 长直角边邻 ----
l4a = list(l2)
for cc in list(l2):
    b, r, c = cc
    for k in range(3):
        if edge_class(b, k) == 'z':
            e = edges[b][k]
            if e:
                ncc = (e[0], r + e[1], c + e[2])
                if ncc not in l4a:
                    l4a.append(ncc)

# ---- L4b: 4 三角直角顶点（90°）共顶点 ----
rv = corner(ref_l2, 'y')  # 直角顶点 = 斜边对角？不：直角顶点在 x 与 z 之间
cell0 = tab['cells'][ref_l2]
k_x = [k for k in range(3) if edge_class(ref_l2, k) == 'x'][0]
k_z = [k for k in range(3) if edge_class(ref_l2, k) == 'z'][0]
x_end = (k_x + 1) % 3
z_end = (k_z + 1) % 3
if x_end == k_z:
    right_v = cell0['v'][k_z]
elif z_end == k_x:
    right_v = cell0['v'][k_x]
else:
    right_v = None
l4b = vert_cells(right_v)

# ---- L6a: 6 三角 60° 角（长直角边对角）共顶点，大等边三角形 ----
# 参考格 = base 0（mask=0 → 该边数首格）
refb6 = 0
refc6 = cabs((refb6, 0, 0))
cell6 = tab['cells'][refb6]
# 60° 角 = 长直角边 z 的对角 = 短边 x 与斜边 y 的公共端点
k_x6 = [k for k in range(3) if edge_class(refb6, k) == 'x'][0]
k_y6 = [k for k in range(3) if edge_class(refb6, k) == 'y'][0]
x_end6 = (k_x6 + 1) % 3
y_end6 = (k_y6 + 1) % 3
if x_end6 == k_y6:
    c60 = cell6['v'][k_y6]
elif y_end6 == k_x6:
    c60 = cell6['v'][k_x6]
else:
    c60 = None
l6a = vert_cells(c60)
print('L6a 60°顶点', c60, 'cells', [(cc[0], cc[1], cc[2]) for cc in l6a], 'count', len(l6a))

# ---- L6b: 1/4 块矩形（6 三角）----
# 块矩形宽 = 长直角边 z (1.8608)、高 = 短直角边 x + 斜边 y (3.2237)；含 6 三角。
# 用旧表 L6 变体2 的格（已核对矩形构造）：base0 邻域
l6b = [(0, 0, 0), (1, 0, 0), (5, 0, 0), (4, 0, 0), (6, 0, 0), (10, 0, 0)]
print('L6b cells:', [(cc[0], cc[1], cc[2]) for cc in l6b])

# ---- L12a: 12 三角 30° 角（短直角边对角）共顶点，大六边形 ----
# 30° 角 = 短直角边 x 的对角 = 长直角边 z 与斜边 y 的公共端点
k_z6 = [k for k in range(3) if edge_class(refb6, k) == 'z'][0]
z_end6 = (k_z6 + 1) % 3
if z_end6 == k_y6:
    c30 = cell6['v'][k_y6]
elif y_end6 == k_z6:
    c30 = cell6['v'][k_z6]
else:
    c30 = None
l12a = vert_cells(c30)
print('L12a 30°顶点', c30, 'cells', [(cc[0], cc[1], cc[2]) for cc in l12a], 'count', len(l12a))

# ---- L12b: L6a + 边邻（12 格）----
l12b = set(cell_key(*cc) for cc in l6a)
nb = edge_neighbors(l6a)
for ncc in nb:
    l12b.add(cell_key(*ncc))
print('L12b count:', len(l12b))
print('L12b cells:', sorted(l12b))

# ---- 输出 Config.cpp 片段 ----
def emit_shape(anchorN, mask, cells_spec, refb):
    refc = cabs((refb, 0, 0))
    stored = []
    for cc in cells_spec:
        if cc == cells_spec[0]:
            stored.append((0.0, 0.0))
            continue
        cx0, cy0 = cabs(cc)
        dx, dy = cx0 - refc[0], cy0 - refc[1]
        stored.append((round(dy, 12), round(-dx, 12)))
    mask_str = '' if mask == 0 else ', 0x%x' % mask
    cells = ', '.join('{%.12f, %.12f, 0}' % s for s in stored)
    return 'shp(%d, {%s}%s)' % (anchorN, cells, mask_str)

print()
print('// Laves4612 重写形状表')
print('s.levels = {2.0, 4.0, 6.0, 12.0};')
shapes_out = [
    ('L2', 3, vert_mask, l2, ref_l2),
    ('L4a', 3, vert_mask, l4a, ref_l2),
    ('L4b', 3, 0, l4b, ref_l2),
    ('L6a', 3, 0, l6a, refb6),
    ('L6b', 3, 0, l6b, refb6),
    ('L12a', 3, 0, l12a, refb6),
    ('L12b', 3, 0, [(cc[0], cc[1], cc[2]) for cc in l6a] +
                   [cc for cc in sorted(l12b - set(cell_key(*x) for x in l6a))], refb6),
]
for label, anchorN, mask, cells_spec, refb in shapes_out:
    print('        %s,  // %s' % (emit_shape(anchorN, mask, cells_spec, refb), label))
print('s.shapeLevelIndex = {0, 1, 1, 2, 2, 3, 3};')

# ---- 验证：全锚格解析 + 方向规范 ----
import verify_snub_shapes as vs
tr, first = compute_anchor_transforms(tab, edges)
print()
print('== verify ==')
for label, anchorN, mask, cells_spec, refb in shapes_out:
    ok = vs.verify_shape(tab, edges, mask, cells_spec, 'laves_4612', label)
    # L2/L4a 斜边竖直检查（mask 内全部锚格）
    if label in ('L2', 'L4a'):
        for b in range(B):
            if not (mask & (1 << b)):
                continue
            a_ang, _ = tr[b]
            ang = a_ang - tr[refb][0]
            c, s = math.cos(ang), math.sin(ang)
            # 找形状中共享斜边的两格（L2 的 cells[0] 与 cells[1]）
            p0 = cabs(cells_spec[1])
            p1 = cabs(cells_spec[2] if len(cells_spec) > 2 else cells_spec[0])
            ax, ay = cabs((b, 0, 0))
            dx0, dy0 = p0[0] - refc_l2[0], p0[1] - refc_l2[1]
            w0 = (dx0 * c - dy0 * s, dx0 * s + dy0 * c)
            w1 = None
            if label == 'L2':
                # 斜边邻 = partner（cells_spec[1]）；检查它与锚格中心同 y
                ok2 = abs(w0[1]) < 1e-3
            else:
                pass
    print('  %s: %s' % (label, 'OK' if ok else 'FAIL'))
