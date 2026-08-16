#!/usr/bin/env python3
"""判断 Laves 基础格的对称性/方向要求，输出 anchorBaseMask 位。

- Laves3464 L1: 筝形"左右对称"（关于竖直轴 y 镜像对称）
- Laves3636 L1: 菱形长对角线水平（对角线 1.8612@0°）
- Laves31212 L2: 等腰三角形底边水平（底边=长边 2.6321@0°）
- Laves4612 L2: 三角形斜边竖直（斜边=最长边 2.1491@±90°）
"""
import json
import math

TOL = 1e-6


def load(name, file):
    d = json.load(open(file, encoding='utf-8'))
    j = d[name]
    old_wx = j['W'][0]
    old_hy = j['H'][1]
    cells = []
    for c in j['cells']:
        nc = old_hy - c['cy']
        ny = c['cx']
        v = [[old_hy - p[1], p[0]] for p in c['v']]
        cells.append({'n': c['n'], 'cx': nc, 'cy': ny, 'v': v})
    return cells


def is_mirror_sym_vertical(verts):
    """顶点集（相对中心）是否关于竖直轴（x=0）镜像对称。"""
    # 顶点对匹配：每个顶点 (x,y) 需有镜像 (-x,y) 或顶点恰在轴上 (x≈0)
    pts = [(round(x, 6), round(y, 6)) for x, y in verts]
    remaining = list(pts)
    while remaining:
        x, y = remaining.pop()
        if abs(x) <= 1e-4:
            continue  # 顶点在对称轴上
        mirror = (-x, y)
        for i, p in enumerate(remaining):
            if abs(p[0] - mirror[0]) < 1e-4 and abs(p[1] - mirror[1]) < 1e-4:
                del remaining[i]
                break
        else:
            return False
    return True


def main():
    for name, rule in [('laves_3464', 'sym'), ('laves_3636', 'dhoriz'),
                       ('laves_31212', 'horiz'), ('laves_4612', 'vert')]:
        cells = load(name, 'data/tiling_specs_laves.json')
        mask = 0
        details = []
        for b in range(len(cells)):
            c = cells[b]
            verts = [(p[0] - c['cx'], p[1] - c['cy']) for p in c['v']]
            ok = False
            if rule == 'sym':
                ok = is_mirror_sym_vertical(verts)
            elif rule == 'dhoriz':
                # 菱形：长对角线（1.8612）水平、短对角线（1.0746）竖直
                n = c['n']
                diags = []
                for k in range(n):
                    A = verts[k]
                    opp = verts[(k + 2) % n]
                    dx, dy = opp[0] - A[0], opp[1] - A[1]
                    ang = math.degrees(math.atan2(dy, dx)) % 180.0
                    if ang > 90.0:
                        ang -= 180.0
                    diags.append((round(math.hypot(dx, dy), 4), round(ang, 1)))
                diags.sort(reverse=True)
                long_ang = diags[0][1]
                ok = abs(long_ang) < 1e-6  # 长对角线水平
            else:
                # 边方向
                edge_angs = []
                n = c['n']
                for k in range(n):
                    A = verts[k]
                    Bv = verts[(k + 1) % n]
                    dx, dy = Bv[0] - A[0], Bv[1] - A[1]
                    ln = math.hypot(dx, dy)
                    ang = math.degrees(math.atan2(dy, dx)) % 180.0
                    if ang > 90.0:
                        ang -= 180.0
                    edge_angs.append((round(ln, 4), round(ang, 1)))
                if rule == 'horiz':
                    # 长边（唯一最长）水平 = 方向 0°
                    longest = max(edge_angs, key=lambda e: e[0])
                    ok = abs(longest[1]) < 1e-6
                elif rule == 'vert':
                    longest = max(edge_angs, key=lambda e: e[0])
                    ok = abs(abs(longest[1]) - 90.0) < 1e-6
            if ok:
                mask |= (1 << b)
                details.append(b)
        print('%-14s mask=0x%x 基础格=%s' % (name, mask, details))


if __name__ == '__main__':
    main()
