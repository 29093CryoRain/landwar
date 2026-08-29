#!/usr/bin/env python3
"""分析 Laves4612：L2 各朝向族下的形态；L4/L4b 在 b=0 上的轮廓比较。

目标：
- L2 应有两种朝向（用户反馈）——确定哪两个朝向族。
- L4/L4b 是否轮廓相同（文档"组成的菱形实际一致, 只是内部三角形排列不一样"）。
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


def rel(cc):
    cx0, cy0 = cc_center(cc)
    return (round(cx0 - ax, 4), round(cy0 - ay, 4))


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
        key = (round((p[0] + c * tab['wx'] + r * tab['hx']) * 1e4),
               round((p[1] + r * tab['hy']) * 1e4))
        out.append({'rel': (p[0] + c * tab['wx'] + r * tab['hx'] - ax,
                            p[1] + r * tab['hy'] - ay),
                    'shared': vert_map.get(key, [])})
    return out


vert_map = defaultdict(list)
for r in range(-8, 9):
    for c in range(-8, 9):
        for b in range(B):
            ox = c * tab['wx'] + r * tab['hx']
            oy = r * tab['hy']
            for p in cells[b]['v']:
                key = (round((p[0] + ox) * 1e4), round((p[1] + oy) * 1e4))
                vert_map[key].append((b, r, c))

# ---- 各朝向族下 L2 解析形态 ----
print('==== L2 各朝向族形态 ====')
for family, b_list in [(('0,False'), [0, 11]), (('0,True'), [14, 20]),
                       (('180,False'), [2, 21]), (('180,True'), [4, 10])]:
    print('族', family, b_list)
    seen = set()
    for b in b_list:
        # L2 = 锚 + 共享斜边（最长边）格
        l2 = [(0, 0, 0)]
        maxlen = -1
        maxk = -1
        for k in range(3):
            A = cells[b]['v'][k]
            Bv = cells[b]['v'][(k + 1) % 3]
            dd = (A[0] - Bv[0]) ** 2 + (A[1] - Bv[1]) ** 2
            if dd > maxlen:
                maxlen = dd
                maxk = k
        e = edges[b][maxk]
        if e is not None:
            l2.append((e[0], e[1], e[2]))
        key = tuple(sorted((rel(cc)[0], rel(cc)[1]) for cc in l2))
        seen.add(key)
        print('  b=%d L2 rel=%s' % (b, [(rel(cc)[0], rel(cc)[1]) for cc in l2]))
    print('  不同形态数 =', len(seen))

# ---- L4 轮廓（b=0 参考）----
print('\n==== L4 在 b=0 的轮廓（4 格多边形合并）====')
# L4 = L2 + 两个长直角边邻格（shape 1 config）
L4_cfg = [(-0.358189998588, -1.240806390097), (0.0, 0.0),
          (-0.358189982603, 0.620403242477), (0.000000005328, -0.620403239402)]
# 运行时偏移 = (-dy, dx)
L4_rt = [(-dy, dx) for dx, dy in L4_cfg]
print('L4 运行时格子中心 rel:', [(round(x, 4), round(y, 4)) for x, y in L4_rt])

L4b_cfg = [(-0.358189982603, 0.620403242477), (0.0, 0.0),
           (1.074569926495, 0.620403248630), (0.716379943892, 1.240806484955)]
L4b_rt = [(-dy, dx) for dx, dy in L4b_cfg]
print('L4b 运行时格子中心 rel:', [(round(x, 4), round(y, 4)) for x, y in L4b_rt])

# 轮廓 = 所有格子的顶点并集（相对锚中心），用于比较形状
def shape_verts(offs, b=0):
    """在 b=0 锚上解析偏移 → 各格子顶点世界坐标（相对锚中心）。"""
    c0x, c0y = cells[0]['cx'], cells[0]['cy']
    allv = []
    for ox, oy in offs:
        # 找偏移点所在格
        best = None
        bd = 1e9
        for dr in range(-3, 4):
            for dc in range(-3, 4):
                for nb in range(B):
                    cx0 = cells[nb]['cx'] + dc * tab['wx'] + dr * tab['hx']
                    cy0 = cells[nb]['cy'] + dr * tab['hy']
                    dd = (c0x + ox - cx0) ** 2 + (c0y + oy - cy0) ** 2
                    if dd < bd:
                        bd = dd
                        best = (nb, dr, dc)
        nb, dr, dc = best
        for p in cells[nb]['v']:
            allv.append((round(p[0] + dc * tab['wx'] + dr * tab['hx'] - c0x, 4),
                         round(p[1] + dr * tab['hy'] - c0y, 4)))
    return sorted(set(allv))

v4 = shape_verts(L4_rt)
v4b = shape_verts(L4b_rt)
print('\nL4 轮廓顶点（相对锚中心）:', sorted(v4))
print('L4b 轮廓顶点（相对锚中心）:', sorted(v4b))
print('L4 == L4b 轮廓:', sorted(v4) == sorted(v4b))
