#!/usr/bin/env python3
"""精确构造 Arch31212 L9/L10 的正反两种变体偏移（Config 存储格式）。

文档：
- 9: 一个三角形 + 周围 3 十二边形 + 3 三角形（十二边形两两之间） = 4 三角 + 3 十二边。有正反两种。
- 10: 三个互共边十二边形（三角形周围的 3 个）+ 周围所有三角形 = 13 三角 + 3 十二边。有正反两种。

方法：以锚格 b=0（参考格，运行时坐标）为中心，枚举顶点/邻格，按文档语义选格。
正反 = 锚三角形两种朝向（b=0 正 vs b=3 反），锚朝向变换自动产生；但用户要求"每个等级
两个变体"显式出现，故输出两个偏移集：变体 A（参考朝向下按文档布局）、变体 B（镜像）。
"""
import json
import math
from collections import defaultdict

TOL = 1e-6

d = json.load(open('data/tiling_specs_arch.json', encoding='utf-8'))
j = d['arch_31212']
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

vert_map = defaultdict(list)
for r in range(-8, 9):
    for c in range(-8, 9):
        for b in range(B):
            ox = c * tab['wx'] + r * tab['hx']
            oy = r * tab['hy']
            for p in cells[b]['v']:
                key = (round((p[0] + ox) * 1e4), round((p[1] + oy) * 1e4))
                vert_map[key].append((b, r, c))


def cc_center(cc):
    b, r, c = cc
    return (cells[b]['cx'] + c * tab['wx'] + r * tab['hx'],
            cells[b]['cy'] + r * tab['hy'])


def rel(cc, ax, ay):
    cx0, cy0 = cc_center(cc)
    return (cx0 - ax, cy0 - ay)


def neighbors(cc):
    b, r, c = cc
    out = []
    for e in edges[b]:
        if e is None:
            continue
        out.append((e[0], r + e[1], c + e[2]))
    return out


def shared_verts(cc):
    b, r, c = cc
    out = []
    for p in cells[b]['v']:
        ox = c * tab['wx'] + r * tab['hx']
        oy = r * tab['hy']
        key = (round((p[0] + ox) * 1e4), round((p[1] + oy) * 1e4))
        out.append({'rel': (p[0] + ox - ax, p[1] + oy - ay), 'val': len(vert_map.get(key, [])),
                    'shared': vert_map.get(key, [])})
    return out


def to_config(offs):
    out = []
    for rx, ry in offs:
        out.append((round(ry, 12), round(-rx, 12)))
    seen = set()
    res = []
    for o in out:
        if o in seen:
            continue
        seen.add(o)
        res.append(o)
    return res


def fmt(offs):
    return '{' + ', '.join('{%.12f, %.12f, 0}' % o for o in offs) + '}'


ax, ay = cells[0]['cx'], cells[0]['cy']
anchor = (0, 0, 0)
sv = shared_verts(anchor)
print('锚格 b=0 顶点（价数应全 3）:')
for v in sv:
    print('  rel=(%.4f,%.4f) val=%d' % (v['rel'][0], v['rel'][1], v['val']))

# L9 = 三角形 + 3 边邻十二边形 + 3 顶点邻三角形
l9 = set([anchor])
tri_nbs = [nb for nb in neighbors(anchor) if cells[nb[0]]['n'] == 12]
print('\nL9 三角形锚的 3 个十二边形边邻:', [rel(nb, ax, ay) for nb in tri_nbs])
for nb in tri_nbs:
    l9.add(nb)
    # 该十二边形与锚三角形的"另一侧"三角形（十二边形的非锚边邻三角形中，与锚三角共顶点的）
    for v in sv:
        for cc in v['shared']:
            if cc == anchor:
                continue
            if cc in l9:
                continue
            if cells[cc[0]]['n'] == 3 and cc in neighbors(nb):
                l9.add(cc)
print('L9 候选格数 =', len(l9))
l9_offs = [rel(cc, ax, ay) for cc in sorted(l9, key=lambda c: (rel(c, ax, ay)[1], rel(c, ax, ay)[0]))]
print('L9 config =', fmt(to_config(l9_offs)))
for cc in sorted(l9, key=lambda c: (rel(c, ax, ay)[1], rel(c, ax, ay)[0])):
    print('   rel=(%.4f,%.4f) n=%d' % (rel(cc, ax, ay)[0], rel(cc, ax, ay)[1], cells[cc[0]]['n']))

# L10 = 3 个互共边十二边形 + 周围所有三角形（13 三角 + 3 十二边 = 16 格）
# 锚 = 三角形？不——L10 锚十二边形（anchorN=12）。3 个互共边十二边形围绕一个三角形，
# 该三角形 = 锚十二边形的一个顶点邻三角形？文档："三个互相有公共边的正12边形(可以看成是
# 一个三角形周围的3个12边形)"。锚 = 三角形更合理？但当前 Config L10 是 shp(12)。
# 用三角形锚：三角形 + 3 边邻十二边形 + 所有与十二边形共顶点的三角形。
print('\n--- L10 探针（三角形锚）---')
l10 = set([anchor])
for nb in tri_nbs:
    l10.add(nb)
# 3 个十二边形两两之间共顶点 → 夹着的三角形；再扩张：所有与 3 个十二边形共顶点的三角形
changed = True
while changed:
    changed = False
    for cc in list(l10):
        if cells[cc[0]]['n'] != 12:
            continue
        for v in shared_verts(cc):
            for sc in v['shared']:
                if sc in l10:
                    continue
                if cells[sc[0]]['n'] == 3 and sc in neighbors(cc):
                    l10.add(sc)
                    changed = True
print('L10(三角锚) 候选格数 =', len(l10))
for cc in sorted(l10, key=lambda c: (rel(c, ax, ay)[1], rel(c, ax, ay)[0])):
    print('   rel=(%.4f,%.4f) n=%d' % (rel(cc, ax, ay)[0], rel(cc, ax, ay)[1], cells[cc[0]]['n']))
if len(l10) == 16:
    l10_offs = [rel(cc, ax, ay) for cc in sorted(l10, key=lambda c: (rel(c, ax, ay)[1], rel(c, ax, ay)[0]))]
    print('L10 config =', fmt(to_config(l10_offs)))
