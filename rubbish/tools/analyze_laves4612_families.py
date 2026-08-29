#!/usr/bin/env python3
"""分析 Laves4612 各基础格的朝向族，确定 L4/L4b 菱形"唯一朝向"的 mask。

L2 文档：两个三角形斜边公共、斜边竖直。L4/L4b 基于 L2 的菱形。
运行时斜边竖直 = 斜边方向 ±90°。但三角形有正/反（镜像）两种，需要细分朝向族：
- 用锚格的"参考朝向变换"（相对 b=0）分组：同 transform (ang, reflect) 的 b 属同族。
"""
import json
import math
from collections import defaultdict

TOL = 1e-6

d = json.load(open('data/tiling_specs_laves.json', encoding='utf-8'))
j = d['laves_4612']
old_wx, old_hy = j['W'][0], j['H'][1]
cells = []
for c in j['cells']:
    nc = old_hy - c['cy']
    ny = c['cx']
    v = [[old_hy - p[1], p[0]] for p in c['v']]
    cells.append({'n': c['n'], 'cx': nc, 'cy': ny, 'v': v})
tab = {'wx': old_hy, 'hx': 0.0, 'hy': old_wx}
B = len(cells)

edges = []
for b in range(B):
    be = []
    cell = cells[b]
    for k in range(cell['n']):
        A = cell['v'][k]
        Bv = cell['v'][(k + 1) % cell['n']]
        found = False
        for dr in range(-2, 3):
            for dc in range(-2, 3):
                ox = dc * tab['wx'] + dr * tab['hx']
                oy = dr * tab['hy']
                for nb in range(B):
                    for kk in range(cells[nb]['n']):
                        n0 = [cells[nb]['v'][kk][0] + ox, cells[nb]['v'][kk][1] + oy]
                        n1 = [cells[nb]['v'][(kk + 1) % cells[nb]['n']][0] + ox,
                              cells[nb]['v'][(kk + 1) % cells[nb]['n']][1] + oy]
                        if abs(n0[0] - Bv[0]) < TOL and abs(n0[1] - Bv[1]) < TOL and \
                           abs(n1[0] - A[0]) < TOL and abs(n1[1] - A[1]) < TOL:
                            be.append((nb, dr, dc))
                            found = True
                            break
                    if found:
                        break
                if found:
                    break
            if found:
                break
        if not found:
            be.append(None)
    edges.append(be)


def cc_center(cc):
    b, r, c = cc
    return (cells[b]['cx'] + c * tab['wx'] + r * tab['hx'],
            cells[b]['cy'] + r * tab['hy'])


def offs(bb):
    s = []
    c0x, c0y = cells[bb]['cx'], cells[bb]['cy']
    for e in edges[bb]:
        if e is None:
            continue
        cx0, cy0 = cc_center((e[0], e[1], e[2]))
        s.append((round(cx0 - c0x, 4), round(cy0 - c0y, 4)))
    return sorted(s)


def transform_to_ref(b, refb):
    """邻格偏移集匹配，返回 (rot_deg, mirror) 或 None。"""
    ref = offs(refb)
    mine = offs(b)
    for mirror in [False, True]:
        for rot90 in range(4):
            ang = rot90 * 90.0
            t = []
            for x, y in mine:
                yy = -y if mirror else y
                a = math.radians(ang)
                t.append((round(x * math.cos(a) - yy * math.sin(a), 4),
                          round(x * math.sin(a) + yy * math.cos(a), 4)))
            if sorted(t) == ref:
                return (ang, mirror)
    return None


ax, ay = cells[0]['cx'], cells[0]['cy']
print('Laves4612 各基础格朝向族（相对 b=0）:')
groups = defaultdict(list)
for b in range(B):
    tr = transform_to_ref(b, 0)
    print('  b=%d 斜边方向检查:' % b)
    c = cells[b]
    verts = [(p[0] - c['cx'], p[1] - c['cy']) for p in c['v']]
    for k in range(3):
        A, Bv = verts[k], verts[(k + 1) % 3]
        dx, dy = Bv[0] - A[0], Bv[1] - A[1]
        ln = math.hypot(dx, dy)
        ang = math.degrees(math.atan2(dy, dx)) % 180
        if ang > 90:
            ang -= 180
        print('    边 %.3f@%.1f°' % (ln, ang))
    groups[str(tr)].append(b)
print('\n朝向族分组:')
for k, v in sorted(groups.items()):
    print('  %s -> %s' % (k, v))
