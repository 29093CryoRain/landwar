#!/usr/bin/env python3
"""精确验证 Laves4612 L4/L4b：文档要求两变体"组成的菱形实际一致"。

L4 变体1 = 2级城（锚+斜边格）+ 两个长直角边邻格（锚的长直角边邻格 + 斜边格的长直角边邻格）
L4b 变体2 = 直角顶点（4-valent）周围 4 格

输出两构造的格子集合（相对锚格），比较是否同一菱形。
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


def rel(cc, ax, ay):
    cx0, cy0 = cc_center(cc)
    return (round(cx0 - ax, 4), round(cy0 - ay, 4))


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


anchor = (0, 0, 0)
ax, ay = cells[0]['cx'], cells[0]['cy']

# ---- 构造 L4 变体1：L2 + 两个长直角边邻格 ----
maxlen, maxk = -1, -1
for k in range(3):
    ln = edge_info(0, k)
    if ln > maxlen:
        maxlen, maxk = ln, k
slope = edge_neighbor(anchor, maxk)  # 斜边格
l2 = [anchor, slope]
print('L2:', [rel(cc, ax, ay) for cc in l2])

def long_leg_neighbor(cc):
    b, r, c = cc
    lens = sorted((edge_info(b, k), k) for k in range(3))
    k2 = lens[-2][1]
    e = edges[b][k2]
    if e is None:
        return None
    return (e[0], r + e[1], c + e[2])

l4 = set(l2)
for cc in l2:
    nb = long_leg_neighbor(cc)
    if nb is not None:
        l4.add(nb)
l4_rt = [rel(cc, ax, ay) for cc in l4]
print('L4 变体1 格子(运行时 rel):', sorted(l4_rt))

# ---- L4b 变体2：直角（4-valent）共点 ----
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
    if len(shared) == 4:  # 直角
        l4b = shared
        print('直角顶点 rel=(%.4f,%.4f)' % (p[0] - ax, p[1] - ay))
        break
l4b_rt = sorted(rel(cc, ax, ay) for cc in l4b)
print('L4b 变体2 格子(运行时 rel):', l4b_rt)

# ---- 比较 ----
print('\nL4 变体1 == L4b 变体2 格子集:', sorted(l4_rt) == l4b_rt)

# ---- 输出 Config 存储格式 (ry, -rx) ----
def to_config(offs):
    return sorted((round(ry, 12), round(-rx, 12)) for rx, ry in offs)

print('L4 变体1 config:', to_config(l4_rt))
print('L4b 变体2 config:', to_config(l4b_rt))
