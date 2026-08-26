#!/usr/bin/env python3
"""verify_snub_shapes.py — 对 4 种扭棱密铺城市形状做严格几何验证（方向锚定）。

在多个锚格处解析形状（模拟运行时管线），核对：
  1. 全部解析且格数 = 形状格数；
  2. 解析出的格中心相对锚格中心的偏移模式 = 期望模式（经锚格朝向变换）；
  3. 方向规范（左右/上下对称、公共边朝向等）。
"""
import json, math, sys
sys.path.insert(0, 'tools')
from gen_snub_city_shapes import (derive_arch33336, derive_arch33434,
                                  derive_laves33336, derive_laves33434)
from derive_city_shapes import *

def cell_center_abs(tab, cc):
    b, r, c = cc
    cell = tab['cells'][b]
    return (cell['cx'] + c * tab['wx'] + r * tab['hx'],
            cell['cy'] + c * tab['wy'] + r * tab['hy'])

def point_in_convex(poly, p, prec=1e-6):
    n = len(poly)
    sign = 0
    for i in range(n):
        a = poly[i]; b = poly[(i + 1) % n]
        cross = (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])
        if abs(cross) <= prec:
            continue
        s = 1 if cross > 0 else -1
        if sign == 0:
            sign = s
        elif sign != s:
            return False
    return True

def world_to_cell(tab, p):
    B = len(tab['cells'])
    wx, wy, hx, hy = tab['wx'], tab['wy'], tab['hx'], tab['hy']
    det = wx * hy - wy * hx
    for b in range(B):
        cell = tab['cells'][b]
        px, py = p[0] - cell['cx'], p[1] - cell['cy']
        if abs(det) < 1e-12:
            continue
        c0f = (px * hy - hx * py) / det
        r0f = (wx * py - wy * px) / det
        c0 = int(round(c0f)); r0 = int(round(r0f))
        for dr in (-1, 0, 1):
            r = r0 + dr
            for dc in (-1, 0, 1):
                c = c0 + dc
                ox = c * wx + r * hx
                oy = c * wy + r * hy
                poly = [(v[0] + ox, v[1] + oy) for v in cell['v']]
                if point_in_convex(poly, p):
                    return (b, r, c)
    return None

def resolve(tab, edges, tr, refb, anchor, cells_spec):
    """模拟 Map.cpp：形状格世界位置 = 锚中心 + R(aAng)∘F(aRefl)∘F(rRefl)∘R(−rAng)·(c_i − c_ref)。
    cells_spec[0] 为锚格（存储偏移恒 (0,0)）。"""
    B = len(tab['cells'])
    wx, wy, hx, hy = tab['wx'], tab['wy'], tab['hx'], tab['hy']
    a_ang, a_refl = tr[anchor[0]]
    r_ang, r_refl = tr[refb]
    flip = a_refl != r_refl  # F·F = 恒等；恰一镜像 → 中间夹一个 y 翻转
    # 合成：R(aAng)∘F^aRefl∘F^rRefl∘R(−rAng)。
    # 无翻转：R(aAng − rAng)；有翻转：R(aAng)∘F∘R(−rAng) = R(aAng + rAng)∘F。
    ang = a_ang + r_ang if flip else a_ang - r_ang
    ax, ay = cell_center_abs(tab, anchor)
    refc = cell_center_abs(tab, (refb, 0, 0))
    out = []
    for cc in cells_spec:
        if cc == cells_spec[0]:
            # 锚格自身偏移 (0,0) → 解析到锚格
            out.append(world_to_cell(tab, (ax, ay)))
            continue
        cx0, cy0 = cell_center_abs(tab, cc)
        dx, dy = cx0 - refc[0], cy0 - refc[1]
        if flip:
            dy = -dy
        # 旋转 (锚帧 − 参考帧)
        c, s = math.cos(ang), math.sin(ang)
        rx, ry = dx * c - dy * s, dx * s + dy * c
        cell = world_to_cell(tab, (ax + rx, ay + ry))
        out.append(cell)
    return out

def rot_points(points, ang, reflect=False):
    c, s = math.cos(ang), math.sin(ang)
    out = []
    for x, y in points:
        if reflect:
            y = -y
        out.append((round(x * c - y * s, 6), round(x * s + y * c, 6)))
    return out

def pattern_of(tab, cc_list):
    """形状格中心相对首格（锚）中心的模式（世界偏移）。"""
    ax, ay = cell_center_abs(tab, cc_list[0])
    return sorted((round(cx - ax, 5), round(cy - ay, 5)) for cx, cy in
                  (cell_center_abs(tab, cc) for cc in cc_list[1:]))

def check(name, tab, edges, cells_spec, mask, anchor_bases, expect_pattern=None,
          expect_sym=None):
    """在 anchor_bases 的多个锚格处解析，核对模式与方向规范。"""
    B = len(tab['cells'])
    all_ok = True
    tr, first = compute_anchor_transforms(tab, edges)
    # 参考格 = mask 最低置位（mask=0 → 该边数第一个基础格）
    anchorN = tab['cells'][cells_spec[0][0]]['n']
    refb = first[anchorN]
    if mask:
        for bb in range(31):
            if mask & (1 << bb):
                refb = bb
                break
    refc = cell_center_abs(tab, (refb, 0, 0))
    # 期望模式（参考帧偏移，锚格为 (0,0)）
    exp_offs = [(0.0, 0.0)]
    for cc in cells_spec[1:]:
        cx0, cy0 = cell_center_abs(tab, cc)
        exp_offs.append((round(cx0 - refc[0], 5), round(cy0 - refc[1], 5)))
    for b in anchor_bases:
        a_ang, a_refl = tr[b]
        r_ang, r_refl = tr[refb]
        flip = a_refl != r_refl
        ang = a_ang + r_ang if flip else a_ang - r_ang
        for (r, c) in [(4, 4), (5, 6), (4, 7)]:
            anchor = (b, r, c)
            got = resolve(tab, edges, tr, refb, anchor, cells_spec)
            if any(cc is None for cc in got):
                print('  !! %s base %d: unresolved' % (name, b))
                all_ok = False
                continue
            # 解析后各格相对锚格中心（世界）
            ax, ay = cell_center_abs(tab, anchor)
            got_pat = sorted((round(cell_center_abs(tab, cc)[0] - ax, 3),
                              round(cell_center_abs(tab, cc)[1] - ay, 3))
                             for cc in got[1:])
            # 期望模式（锚帧）= R(ang)∘F^flip·exp_offs[i]
            c, s = math.cos(ang), math.sin(ang)
            exp_pat = sorted((round(x * c - (y if not flip else -y) * s, 3),
                              round(x * s + (y if not flip else -y) * c, 3))
                             for x, y in exp_offs[1:])
            if got_pat != exp_pat:
                print('  !! %s base %d pattern mismatch\n      got %s\n      exp %s' %
                      (name, b, got_pat, exp_pat))
                all_ok = False
            if expect_sym is not None:
                # 方向规范检查：以形状质心为原点（形状自身对称轴）
                pts_abs = [(cell_center_abs(tab, cc)[0], cell_center_abs(tab, cc)[1])
                           for cc in got]
                mx = sum(p[0] for p in pts_abs) / len(pts_abs)
                my = sum(p[1] for p in pts_abs) / len(pts_abs)
                pts = [(round(p[0] - mx, 3), round(p[1] - my, 3)) for p in pts_abs]
                ok = True
                for fn, desc in expect_sym:
                    if not fn(pts):
                        ok = False
                if not ok:
                    print('  !! %s base %d symmetry fail' % (name, b))
                    all_ok = False
    return all_ok

def sym_left_right(pts, tol=3e-4):
    # 反射 x→-x 后仍为同一集合（容差匹配 spec 噪声）
    s = list(pts)
    for x, y in pts:
        if not any(abs(-x - a) < tol and abs(y - b) < tol for a, b in s):
            return False
    return True

def sym_top_bottom(pts, tol=3e-4):
    s = list(pts)
    for x, y in pts:
        if not any(abs(x - a) < tol and abs(-y - b) < tol for a, b in s):
            return False
    return True

def horiz_edge_pair(pts):
    # 一对格中心 x 相等（竖直堆叠 ⇒ 公共边水平）
    xs = [x for x, _ in pts]
    for i in range(len(xs)):
        for j in range(i + 1, len(xs)):
            if abs(xs[i] - xs[j]) < 1e-4:
                return True
    return False

def verify_shape(tab, edges, mask, cells_spec, tiling_name, shape_label):
    """弱验证：mask 内全部基础格锚点处解析，格数 = 期望且全部解析。"""
    B = len(tab['cells'])
    tr, first = compute_anchor_transforms(tab, edges)
    anchorN = tab['cells'][cells_spec[0][0]]['n']
    refb = first[anchorN]
    if mask:
        for bb in range(31):
            if mask & (1 << bb):
                refb = bb
                break
    ok = True
    for b in range(B):
        if mask != 0 and not (mask & (1 << b)):
            continue
        anchor = (b, 3, 3)
        got = resolve(tab, edges, tr, refb, anchor, cells_spec)
        if any(cc is None for cc in got):
            print('  !! %s %s anchor base %d: unresolved' % (tiling_name, shape_label, b))
            ok = False
            continue
        if len(set(got)) != len(cells_spec):
            print('  !! %s %s anchor base %d: duplicate/overlap cells %s' %
                  (tiling_name, shape_label, b, sorted(got)))
            ok = False
    return ok

def main():
    # ---- arch_33336 ----
    t = derive_arch33336()
    tab = load_spec(t['name'], t['file'])
    edges = build_edges(tab)
    print('== arch_33336 ==')
    shapes = dict((s[0], s) for s in t['shapes'])
    # L1: 两三角公共边水平 → 两格中心 x 相等（竖直堆叠 = 公共边水平）
    ok = check('L1', tab, edges, shapes['L1'][4], shapes['L1'][3],
               [0, 2, 3, 5, 8, 13],
               expect_sym=[(lambda pts: horiz_edge_pair(pts), '公共边水平')])
    print('  L1:', 'OK' if ok else 'FAIL')
    # L3: 大三角 4 格
    ok = check('L3', tab, edges, shapes['L3'][4], shapes['L3'][3], [2, 3, 10, 11])
    print('  L3:', 'OK' if ok else 'FAIL')
    # L5: 六边形 + 上下三角
    ok = check('L5', tab, edges, shapes['L5'][4], shapes['L5'][3], [16, 17])
    print('  L5:', 'OK' if ok else 'FAIL')
    # L9: 左右对称上下对称
    ok = check('L9', tab, edges, shapes['L9'][4], shapes['L9'][3], [16, 17],
               expect_sym=[(sym_left_right, '左右对称'), (sym_top_bottom, '上下对称')])
    print('  L9:', 'OK' if ok else 'FAIL')

    # ---- arch_33434 ----
    t = derive_arch33434()
    tab = load_spec(t['name'], t['file'])
    edges = build_edges(tab)
    print('== arch_33434 ==')
    shapes = dict((s[0], s) for s in t['shapes'])
    ok = check('L5a', tab, edges, shapes['L5a'][4], shapes['L5a'][3], [2, 8],
               expect_sym=[(sym_left_right, '左右对称'),
                           (lambda pts: not sym_top_bottom(pts), '上下不对称')])
    print('  L5a:', 'OK' if ok else 'FAIL')
    ok = check('L5b', tab, edges, shapes['L5b'][4], shapes['L5b'][3], [3, 9],
               expect_sym=[(sym_left_right, '左右对称'),
                           (lambda pts: not sym_top_bottom(pts), '上下不对称')])
    print('  L5b:', 'OK' if ok else 'FAIL')
    ok = check('L12a', tab, edges, shapes['L12a'][4], shapes['L12a'][3], [2, 3, 8, 9],
               expect_sym=[(sym_left_right, '左右对称'), (sym_top_bottom, '上下对称')])
    print('  L12a:', 'OK' if ok else 'FAIL')
    ok = check('L12b', tab, edges, shapes['L12b'][4], shapes['L12b'][3], [0, 1, 6, 7],
               expect_sym=[(sym_left_right, '左右对称'), (sym_top_bottom, '上下对称')])
    print('  L12b:', 'OK' if ok else 'FAIL')

    # ---- laves_33336 ----
    t = derive_laves33336()
    tab = load_spec(t['name'], t['file'])
    edges = build_edges(tab)
    print('== laves_33336 ==')
    shapes = dict((s[0], s) for s in t['shapes'])
    ok = check('L6', tab, edges, shapes['L6'][4], shapes['L6'][3],
               [0, 3, 4, 7, 8, 11])
    print('  L6:', 'OK' if ok else 'FAIL')
    ok = check('L3a', tab, edges, shapes['L3a'][4], shapes['L3a'][3], [0, 7, 11])
    print('  L3a:', 'OK' if ok else 'FAIL')
    ok = check('L3b', tab, edges, shapes['L3b'][4], shapes['L3b'][3], [3, 4, 8])
    print('  L3b:', 'OK' if ok else 'FAIL')

    # ---- laves_33434 ----
    t = derive_laves33434()
    tab = load_spec(t['name'], t['file'])
    edges = build_edges(tab)
    print('== laves_33434 ==')
    shapes = dict((s[0], s) for s in t['shapes'])
    ok = check('L4a', tab, edges, shapes['L4a'][4], shapes['L4a'][3], [0, 1, 2, 3])
    print('  L4a:', 'OK' if ok else 'FAIL')
    ok = check('L4b', tab, edges, shapes['L4b'][4], shapes['L4b'][3], [4, 5, 6, 7])
    print('  L4b:', 'OK' if ok else 'FAIL')
    ok = check('L12a', tab, edges, shapes['L12a'][4], shapes['L12a'][3], [0, 1, 2, 3])
    print('  L12a:', 'OK' if ok else 'FAIL')
    ok = check('L12b', tab, edges, shapes['L12b'][4], shapes['L12b'][3], [4, 5, 6, 7])
    print('  L12b:', 'OK' if ok else 'FAIL')

if __name__ == '__main__':
    main()
