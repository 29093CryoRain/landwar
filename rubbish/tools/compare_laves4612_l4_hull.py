#!/usr/bin/env python3
"""比较 Laves4612 L4/L4b 两变体的真实多边形轮廓（格子多边形并集的外边界）。

文档：L4/L4b"组成的菱形实际一致, 只是内部三角形排列不一样"——即两变体轮廓应相同。
输出两变体的轮廓多边形顶点（相对锚格），比较是否一致。
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


def edge_neighbor(cc, k):
    b, r, c = cc
    e = edges[b][k]
    if e is None:
        return None
    return (e[0], r + e[1], c + e[2])


def edge_info(b, k):
    A = cells[b]['v'][k]
    Bv = cells[b]['v'][(k + 1) % cells[b]['n']]
    dx, dy = Bv[0] - A[0], Bv[1] - A[1]
    return math.hypot(dx, dy)


def long_leg_neighbor(cc):
    b, r, c = cc
    lens = sorted((edge_info(b, k), k) for k in range(3))
    k2 = lens[-2][1]
    e = edges[b][k2]
    if e is None:
        return None
    return (e[0], r + e[1], c + e[2])


ax, ay = cells[0]['cx'], cells[0]['cy']
anchor = (0, 0, 0)

# L4 变体1：L2 + 两个长直角边邻格
maxlen, maxk = -1, -1
for k in range(3):
    ln = edge_info(0, k)
    if ln > maxlen:
        maxlen, maxk = ln, k
slope = edge_neighbor(anchor, maxk)
l4 = set([anchor, slope])
for cc in [anchor, slope]:
    nb = long_leg_neighbor(cc)
    if nb is not None:
        l4.add(nb)
print('L4 变体1 格子:', sorted((rel for rel in [])) if False else '')
for cc in l4:
    cx0, cy0 = cc_center(cc)
    print('  格 rel=(%.4f,%.4f)' % (cx0 - ax, cy0 - ay))

# 轮廓 = 全部格子多边形顶点的凸包（或外边界）
def poly_of(cc):
    b, r, c = cc
    out = []
    for p in cells[b]['v']:
        out.append((p[0] + c * tab['wx'] + r * tab['hx'] - ax,
                    p[1] + r * tab['hy'] - ay))
    return out

def convex_hull(pts):
    pts = sorted(set((round(x, 6), round(y, 6)) for x, y in pts))
    if len(pts) <= 3:
        return pts
    def cross(o, a, b):
        return (a[0]-o[0])*(b[1]-o[1]) - (a[1]-o[1])*(b[0]-o[0])
    lower = []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    upper = []
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]

def hull_of(ccs):
    allp = []
    for cc in ccs:
        allp.extend(poly_of(cc))
    return convex_hull(allp)

h4 = hull_of(l4)
print('L4 变体1 轮廓顶点:', [(round(x, 4), round(y, 4)) for x, y in h4])

# L4b 变体2：直角（4-valent）周围 4 格
vert_map = defaultdict(list)
for r in range(-8, 9):
    for c in range(-8, 9):
        for b in range(B):
            ox = c * tab['wx'] + r * tab['hx']
            oy = r * tab['hy']
            for p in cells[b]['v']:
                key = (round((p[0] + ox) * 1e4), round((p[1] + oy) * 1e4))
                vert_map[key].append((b, r, c))

l4b = None
for p in cells[0]['v']:
    key = (round(p[0] * 1e4), round(p[1] * 1e4))
    shared = vert_map.get(key, [])
    if len(shared) == 4:
        l4b = shared
        print('直角顶点 rel=(%.4f,%.4f)' % (p[0] - ax, p[1] - ay))
        break
for cc in l4b:
    cx0, cy0 = cc_center(cc)
    print('  格 rel=(%.4f,%.4f)' % (cx0 - ax, cy0 - ay))
h4b = hull_of(l4b)
print('L4b 变体2 轮廓顶点:', [(round(x, 4), round(y, 4)) for x, y in h4b])

print('\n轮廓一致:', sorted(h4) == sorted(h4b))
